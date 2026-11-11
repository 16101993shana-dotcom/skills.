#include "ChronoRoadGraph.h"

int32 FChronoRoadGraph::AddNode(const FVector& Position)
{
	FChronoRoadNode Node;
	Node.Id = NextNodeId++;
	Node.Position = Position;
	Nodes.Add(Node.Id, Node);
	DirtyNodes.Add(Node.Id);
	return Node.Id;
}

void FChronoRoadGraph::RefreshSegmentMetrics(FChronoRoadSegment& Segment) const
{
	Segment.Length = Segment.Curve.ComputeLength();
	Segment.Bounds = Segment.Curve.ComputeBounds().ExpandBy(Segment.Profile.GetHalfWidthCm());
}

int32 FChronoRoadGraph::AddSegment(int32 StartNodeId, int32 EndNodeId, const FChronoBezier2D& Curve, const FChronoRoadProfileData& Profile)
{
	if (StartNodeId == EndNodeId)
	{
		return INDEX_NONE;
	}

	FChronoRoadNode* StartNode = Nodes.Find(StartNodeId);
	FChronoRoadNode* EndNode = Nodes.Find(EndNodeId);
	if (!StartNode || !EndNode)
	{
		return INDEX_NONE;
	}

	FChronoRoadSegment Segment;
	Segment.Id = NextSegmentId++;
	Segment.StartNodeId = StartNodeId;
	Segment.EndNodeId = EndNodeId;
	Segment.Profile = Profile;
	Segment.Curve = Curve;

	// The curve always starts and ends exactly on its nodes.
	Segment.Curve.P0 = FVector2D(StartNode->Position.X, StartNode->Position.Y);
	Segment.Curve.P3 = FVector2D(EndNode->Position.X, EndNode->Position.Y);
	RefreshSegmentMetrics(Segment);

	StartNode->SegmentIds.Add(Segment.Id);
	EndNode->SegmentIds.Add(Segment.Id);

	const int32 SegmentId = Segment.Id;
	Segments.Add(SegmentId, MoveTemp(Segment));
	DirtySegments.Add(SegmentId);

	RecomputeNodeTrims(StartNodeId);
	RecomputeNodeTrims(EndNodeId);
	return SegmentId;
}

bool FChronoRoadGraph::RemoveSegment(int32 SegmentId)
{
	FChronoRoadSegment Removed;
	if (!Segments.RemoveAndCopyValue(SegmentId, Removed))
	{
		return false;
	}

	DirtySegments.Remove(SegmentId);
	RemovedSegments.Add(SegmentId);

	for (const int32 NodeId : { Removed.StartNodeId, Removed.EndNodeId })
	{
		FChronoRoadNode* Node = Nodes.Find(NodeId);
		if (!Node)
		{
			continue;
		}

		Node->SegmentIds.Remove(SegmentId);
		if (Node->SegmentIds.Num() == 0)
		{
			Nodes.Remove(NodeId);
			DirtyNodes.Remove(NodeId);
			RemovedNodes.Add(NodeId);
		}
		else
		{
			RecomputeNodeTrims(NodeId);
		}
	}

	return true;
}

int32 FChronoRoadGraph::SplitSegment(int32 SegmentId, double T)
{
	const FChronoRoadSegment* Original = Segments.Find(SegmentId);
	if (!Original || T <= 0.0 || T >= 1.0)
	{
		return INDEX_NONE;
	}

	// Copy what is needed before the maps are modified.
	const FChronoRoadProfileData Profile = Original->Profile;
	const int32 OldEndNodeId = Original->EndNodeId;
	const int32 StartNodeId = Original->StartNodeId;

	FChronoBezier2D FirstCurve;
	FChronoBezier2D SecondCurve;
	Original->Curve.Split(T, FirstCurve, SecondCurve);

	// The new node sits on the existing road surface.
	const double SplitDistance = Original->Curve.ComputeLengthTo(T);
	const double SplitHeight = GetSegmentHeightAtDistance(*Original, SplitDistance);
	const FVector2D SplitPoint = Original->Curve.Evaluate(T);

	const int32 NewNodeId = AddNode(FVector(SplitPoint.X, SplitPoint.Y, SplitHeight));

	// First half: the original segment now ends at the new node.
	if (FChronoRoadNode* OldEndNode = Nodes.Find(OldEndNodeId))
	{
		OldEndNode->SegmentIds.Remove(SegmentId);
	}

	FChronoRoadSegment& FirstHalf = Segments.FindChecked(SegmentId);
	FirstHalf.Curve = FirstCurve;
	FirstHalf.EndNodeId = NewNodeId;
	FirstHalf.TrimEnd = 0.0f;
	RefreshSegmentMetrics(FirstHalf);
	DirtySegments.Add(SegmentId);

	Nodes.FindChecked(NewNodeId).SegmentIds.Add(SegmentId);

	// Second half: a new segment from the new node to the old end node.
	// AddSegment recomputes the trims of both of its nodes.
	const int32 SecondSegmentId = AddSegment(NewNodeId, OldEndNodeId, SecondCurve, Profile);
	if (SecondSegmentId == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	RecomputeNodeTrims(StartNodeId);
	return NewNodeId;
}

int32 FChronoRoadGraph::FindNearestNode(const FVector2D& Point, double MaxDistanceCm) const
{
	int32 BestId = INDEX_NONE;
	double BestSquared = MaxDistanceCm * MaxDistanceCm;

	for (const TPair<int32, FChronoRoadNode>& Pair : Nodes)
	{
		const double DistanceSquared = FVector2D::DistSquared(Point, FVector2D(Pair.Value.Position.X, Pair.Value.Position.Y));
		// The id breaks ties so the result does not depend on the map order.
		if (DistanceSquared < BestSquared || (DistanceSquared == BestSquared && BestId != INDEX_NONE && Pair.Key < BestId))
		{
			BestSquared = DistanceSquared;
			BestId = Pair.Key;
		}
	}

	return BestId;
}

int32 FChronoRoadGraph::FindNearestSegment(const FVector2D& Point, double MaxDistanceCm, double& OutT, FVector2D& OutPoint) const
{
	int32 BestId = INDEX_NONE;
	double BestDistance = MaxDistanceCm;
	OutT = 0.0;
	OutPoint = Point;

	for (const TPair<int32, FChronoRoadSegment>& Pair : Segments)
	{
		const FChronoRoadSegment& Segment = Pair.Value;
		if (!Segment.Bounds.ExpandBy(MaxDistanceCm).IsInside(Point))
		{
			continue;
		}

		double Distance = 0.0;
		const double T = Segment.Curve.FindClosestParameter(Point, Distance);
		if (Distance < BestDistance || (Distance == BestDistance && BestId != INDEX_NONE && Pair.Key < BestId))
		{
			BestDistance = Distance;
			BestId = Pair.Key;
			OutT = T;
			OutPoint = Segment.Curve.Evaluate(T);
		}
	}

	return BestId;
}

void FChronoRoadGraph::GetNodeArms(int32 NodeId, TArray<FChronoRoadArm>& OutArms) const
{
	OutArms.Reset();

	const FChronoRoadNode* Node = Nodes.Find(NodeId);
	if (!Node)
	{
		return;
	}

	for (const int32 SegmentId : Node->SegmentIds)
	{
		const FChronoRoadSegment* Segment = Segments.Find(SegmentId);
		FChronoRoadArm Arm;
		if (Segment)
		{
			Arm.Direction = (Segment->StartNodeId == NodeId) ? Segment->Curve.GetStartDirection() : Segment->Curve.GetEndDirection();
			Arm.HalfWidthCm = Segment->Profile.GetHalfWidthCm();
		}
		// One arm per segment id, even for a missing segment, so indices stay aligned.
		OutArms.Add(Arm);
	}
}

void FChronoRoadGraph::RecomputeNodeTrims(int32 NodeId)
{
	const FChronoRoadNode* Node = Nodes.Find(NodeId);
	if (!Node)
	{
		return;
	}

	const TArray<int32> SegmentIds = Node->SegmentIds;

	TArray<FChronoRoadArm> Arms;
	GetNodeArms(NodeId, Arms);

	TArray<float> Trims;
	float MinAngleDegrees = 0.0f;
	ChronoRoadGeometry::ComputeJunctionTrims(Arms, MinTrimCm, Trims, MinAngleDegrees);

	for (int32 Index = 0; Index < SegmentIds.Num(); ++Index)
	{
		if (FChronoRoadSegment* Segment = Segments.Find(SegmentIds[Index]))
		{
			if (Segment->StartNodeId == NodeId)
			{
				Segment->TrimStart = Trims[Index];
			}
			else
			{
				Segment->TrimEnd = Trims[Index];
			}
			DirtySegments.Add(Segment->Id);
		}
	}

	DirtyNodes.Add(NodeId);
}

float FChronoRoadGraph::GetNodeMinAngleDegrees(int32 NodeId) const
{
	TArray<FChronoRoadArm> Arms;
	GetNodeArms(NodeId, Arms);

	TArray<float> Trims;
	float MinAngleDegrees = 360.0f;
	ChronoRoadGeometry::ComputeJunctionTrims(Arms, MinTrimCm, Trims, MinAngleDegrees);
	return MinAngleDegrees;
}

float FChronoRoadGraph::GetNodeRadiusCm(int32 NodeId) const
{
	const FChronoRoadNode* Node = Nodes.Find(NodeId);
	if (!Node)
	{
		return 0.0f;
	}

	float Radius = 0.0f;
	for (const int32 SegmentId : Node->SegmentIds)
	{
		if (const FChronoRoadSegment* Segment = Segments.Find(SegmentId))
		{
			const float Trim = (Segment->StartNodeId == NodeId) ? Segment->TrimStart : Segment->TrimEnd;
			const float HalfWidth = Segment->Profile.GetHalfWidthCm();
			Radius = FMath::Max(Radius, FMath::Sqrt(Trim * Trim + HalfWidth * HalfWidth));
		}
	}
	return Radius;
}

double FChronoRoadGraph::GetSlopedLength(const FChronoRoadSegment& Segment) const
{
	return Segment.GetUsableLength() - 2.0 * LevelRunCm;
}

double FChronoRoadGraph::GetSegmentHeightAtDistance(const FChronoRoadSegment& Segment, double Distance) const
{
	const FChronoRoadNode* StartNode = Nodes.Find(Segment.StartNodeId);
	const FChronoRoadNode* EndNode = Nodes.Find(Segment.EndNodeId);
	if (!StartNode || !EndNode)
	{
		return 0.0;
	}

	const double StartHeight = StartNode->Position.Z;
	const double EndHeight = EndNode->Position.Z;
	const double Sloped = GetSlopedLength(Segment);
	if (Sloped <= 1.0)
	{
		return (Distance < 0.5 * Segment.Length) ? StartHeight : EndHeight;
	}

	// Level inside both junctions and over the level run after each of them,
	// constant grade in between.
	const double SlopeStart = Segment.TrimStart + LevelRunCm;
	const double Alpha = FMath::Clamp((Distance - SlopeStart) / Sloped, 0.0, 1.0);
	return FMath::Lerp(StartHeight, EndHeight, Alpha);
}

double FChronoRoadGraph::GetSegmentSlopePercent(const FChronoRoadSegment& Segment) const
{
	const FChronoRoadNode* StartNode = Nodes.Find(Segment.StartNodeId);
	const FChronoRoadNode* EndNode = Nodes.Find(Segment.EndNodeId);
	if (!StartNode || !EndNode)
	{
		return 0.0;
	}

	// A segment too short to hold a grade must be level: any height
	// difference then reads as an impossible slope and is refused.
	const double Sloped = FMath::Max(GetSlopedLength(Segment), 1.0);
	return 100.0 * FMath::Abs(EndNode->Position.Z - StartNode->Position.Z) / Sloped;
}

void FChronoRoadGraph::BuildSegmentPath(const FChronoRoadSegment& Segment, double SpacingCm, TArray<FVector>& OutPath) const
{
	TArray<FChronoRoadSample> Samples;
	ChronoRoadGeometry::BuildPolyline(Segment.Curve, SpacingCm, Samples);

	OutPath.Reset(Samples.Num());
	for (const FChronoRoadSample& Sample : Samples)
	{
		OutPath.Add(FVector(Sample.Position.X, Sample.Position.Y, GetSegmentHeightAtDistance(Segment, Sample.Distance)));
	}
}

void FChronoRoadGraph::ClearChanges()
{
	DirtySegments.Reset();
	DirtyNodes.Reset();
	RemovedSegments.Reset();
	RemovedNodes.Reset();
}

bool FChronoRoadGraph::Serialize(FArchive& Ar)
{
	// Bump when the binary layout changes.
	const int32 CurrentVersion = 1;
	// Sanity bound on counts read from a file.
	const int32 MaxCount = 4 * 1024 * 1024;

	int32 Version = CurrentVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != CurrentVersion)
	{
		Ar.SetError();
		return false;
	}

	if (Ar.IsLoading())
	{
		Nodes.Reset();
		Segments.Reset();
		ClearChanges();
	}

	Ar << NextNodeId;
	Ar << NextSegmentId;

	// Saved in id order so that the same graph always gives the same bytes.
	TArray<int32> NodeIds;
	Nodes.GetKeys(NodeIds);
	NodeIds.Sort();
	int32 NumNodes = NodeIds.Num();
	Ar << NumNodes;

	TArray<int32> SegmentIds;
	Segments.GetKeys(SegmentIds);
	SegmentIds.Sort();
	int32 NumSegments = SegmentIds.Num();
	Ar << NumSegments;

	bool bValid = !Ar.IsError() && NumNodes >= 0 && NumNodes <= MaxCount && NumSegments >= 0 && NumSegments <= MaxCount;

	for (int32 Index = 0; bValid && Index < NumNodes; ++Index)
	{
		FChronoRoadNode Node;
		if (Ar.IsSaving())
		{
			Node = Nodes.FindChecked(NodeIds[Index]);
		}

		Ar << Node.Id;
		Ar << Node.Position;
		Ar << Node.SegmentIds;

		if (Ar.IsLoading())
		{
			bValid = !Ar.IsError() && Node.Id > 0 && !Nodes.Contains(Node.Id);
			if (bValid)
			{
				Nodes.Add(Node.Id, MoveTemp(Node));
			}
		}
	}

	for (int32 Index = 0; bValid && Index < NumSegments; ++Index)
	{
		FChronoRoadSegment Segment;
		FString ProfileId;
		if (Ar.IsSaving())
		{
			Segment = Segments.FindChecked(SegmentIds[Index]);
			ProfileId = Segment.Profile.ProfileId.ToString();
		}

		Ar << Segment.Id;
		Ar << Segment.StartNodeId;
		Ar << Segment.EndNodeId;
		Ar << Segment.Curve.P0;
		Ar << Segment.Curve.P1;
		Ar << Segment.Curve.P2;
		Ar << Segment.Curve.P3;
		Ar << ProfileId;
		Ar << Segment.Profile.CarriagewayWidthCm;
		Ar << Segment.Profile.SidewalkWidthCm;
		Ar << Segment.Profile.CurbHeightCm;
		Ar << Segment.Profile.LanesPerDirection;
		Ar << Segment.Profile.bOneWay;
		Ar << Segment.Profile.MaxSlopePercent;
		Ar << Segment.Profile.MinCurveRadiusCm;

		if (Ar.IsLoading())
		{
			bValid = !Ar.IsError()
				&& Segment.Id > 0
				&& !Segments.Contains(Segment.Id)
				&& Segment.StartNodeId != Segment.EndNodeId
				&& Nodes.Contains(Segment.StartNodeId)
				&& Nodes.Contains(Segment.EndNodeId)
				&& Segment.Profile.CarriagewayWidthCm > 0.0f;
			if (bValid)
			{
				Segment.Profile.ProfileId = FName(*ProfileId);
				RefreshSegmentMetrics(Segment);
				Segments.Add(Segment.Id, MoveTemp(Segment));
			}
		}
	}

	if (Ar.IsLoading())
	{
		// Every node must list exactly the segments that end on it.
		for (const TPair<int32, FChronoRoadNode>& Pair : Nodes)
		{
			for (const int32 SegmentId : Pair.Value.SegmentIds)
			{
				const FChronoRoadSegment* Segment = Segments.Find(SegmentId);
				bValid = bValid && Segment && (Segment->StartNodeId == Pair.Key || Segment->EndNodeId == Pair.Key);
			}
		}
		for (const TPair<int32, FChronoRoadSegment>& Pair : Segments)
		{
			bValid = bValid
				&& Nodes.FindChecked(Pair.Value.StartNodeId).SegmentIds.Contains(Pair.Key)
				&& Nodes.FindChecked(Pair.Value.EndNodeId).SegmentIds.Contains(Pair.Key);
		}

		if (!bValid)
		{
			Nodes.Reset();
			Segments.Reset();
			NextNodeId = 1;
			NextSegmentId = 1;
			Ar.SetError();
			return false;
		}

		// Trims are derived data: recompute them instead of trusting the file.
		TArray<int32> LoadedNodeIds;
		Nodes.GetKeys(LoadedNodeIds);
		LoadedNodeIds.Sort();
		for (const int32 NodeId : LoadedNodeIds)
		{
			RecomputeNodeTrims(NodeId);
		}
		ClearChanges();
	}

	return !Ar.IsError();
}
