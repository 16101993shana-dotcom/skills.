#include "ChronoRoadBuilder.h"

#include "ChronoHeightfield.h"
#include "ChronoSettings.h"

namespace ChronoRoadBuilderPrivate
{
	/** Smallest rectangle containing both. An empty rectangle is ignored. */
	FIntRect UnionRects(const FIntRect& A, const FIntRect& B)
	{
		if (A.Width() <= 0 || A.Height() <= 0)
		{
			return B;
		}
		if (B.Width() <= 0 || B.Height() <= 0)
		{
			return A;
		}
		return FIntRect(
			FMath::Min(A.Min.X, B.Min.X), FMath::Min(A.Min.Y, B.Min.Y),
			FMath::Max(A.Max.X, B.Max.X), FMath::Max(A.Max.Y, B.Max.Y));
	}

	/** Finds or creates the node for one end of a new road. */
	int32 ResolveEndpoint(FChronoRoadGraph& InOutGraph, const FVector2D& Point, const FChronoHeightfield& Heightfield, const FChronoRoadRules& Rules)
	{
		// 1. An existing node nearby: connect to it.
		const int32 NearNodeId = InOutGraph.FindNearestNode(Point, Rules.SnapDistanceCm);
		if (NearNodeId != INDEX_NONE)
		{
			return NearNodeId;
		}

		// 2. An existing road nearby: split it and connect to the new node.
		double T = 0.0;
		FVector2D OnRoad;
		const int32 NearSegmentId = InOutGraph.FindNearestSegment(Point, Rules.SnapDistanceCm, T, OnRoad);
		if (NearSegmentId != INDEX_NONE)
		{
			const int32 SplitNodeId = InOutGraph.SplitSegment(NearSegmentId, T);
			if (SplitNodeId != INDEX_NONE)
			{
				return SplitNodeId;
			}
		}

		// 3. Open ground: a new node on the terrain.
		return InOutGraph.AddNode(FVector(Point.X, Point.Y, Heightfield.SampleHeight(Point)));
	}

	EChronoRoadBuildResult ValidateChanges(const FChronoRoadGraph& Graph, int32 NewSegmentId, const FChronoHeightfield& Heightfield, const FChronoRoadRules& Rules)
	{
		const FChronoRoadSegment* NewSegment = Graph.FindSegment(NewSegmentId);
		if (!NewSegment)
		{
			return EChronoRoadBuildResult::SameEndpoint;
		}

		// The whole centerline must stay on the map.
		TArray<FChronoRoadSample> NewSamples;
		ChronoRoadGeometry::BuildPolyline(NewSegment->Curve, Rules.SampleSpacingCm, NewSamples);
		for (const FChronoRoadSample& Sample : NewSamples)
		{
			if (!Heightfield.IsInsideWorld(Sample.Position))
			{
				return EChronoRoadBuildResult::OutOfBounds;
			}
		}

		if (NewSegment->Curve.ComputeMinRadius() < NewSegment->Profile.MinCurveRadiusCm)
		{
			return EChronoRoadBuildResult::CurveTooTight;
		}

		// Every node touched by the edit: number of roads and angles between them.
		for (const int32 NodeId : Graph.GetDirtyNodes())
		{
			const FChronoRoadNode* Node = Graph.FindNode(NodeId);
			if (!Node)
			{
				continue;
			}
			if (Node->SegmentIds.Num() > Rules.MaxNodeDegree)
			{
				return EChronoRoadBuildResult::TooManyRoads;
			}
			if (Node->SegmentIds.Num() >= 2 && Graph.GetNodeMinAngleDegrees(NodeId) < Rules.MinIntersectionAngleDegrees)
			{
				return EChronoRoadBuildResult::AngleTooSharp;
			}
		}

		// Every segment touched by the edit (the new one, the two halves of a
		// split road, and the neighbors whose junction grew): each must keep
		// enough length between its junctions and an acceptable grade.
		for (const int32 SegmentId : Graph.GetDirtySegments())
		{
			const FChronoRoadSegment* Segment = Graph.FindSegment(SegmentId);
			if (!Segment)
			{
				continue;
			}
			if (Segment->GetUsableLength() < Rules.MinUsableLengthCm)
			{
				return EChronoRoadBuildResult::TooShort;
			}
			if (Graph.GetSegmentSlopePercent(*Segment) > Segment->Profile.MaxSlopePercent)
			{
				return EChronoRoadBuildResult::TooSteep;
			}
		}

		for (const TPair<int32, FChronoRoadSegment>& Pair : Graph.GetSegments())
		{
			if (Pair.Key != NewSegmentId && ChronoRoadBuilder::AreSegmentsTooClose(Graph, *NewSegment, Pair.Value, Rules, Rules.ClearanceCm))
			{
				return EChronoRoadBuildResult::OverlapsRoad;
			}
		}

		return EChronoRoadBuildResult::Success;
	}
}

FChronoRoadRules FChronoRoadRules::FromSettings()
{
	const UChronoSettings* Settings = UChronoSettings::Get();

	FChronoRoadRules Rules;
	Rules.MinIntersectionAngleDegrees = Settings->RoadMinIntersectionAngleDegrees;
	Rules.MaxNodeDegree = Settings->RoadMaxNodeDegree;
	Rules.MinUsableLengthCm = Settings->RoadMinUsableLengthCm;
	Rules.MinTrimCm = Settings->RoadMinTrimCm;
	Rules.JunctionLevelRunCm = Settings->RoadJunctionLevelRunCm;
	Rules.SnapDistanceCm = Settings->RoadSnapDistanceCm;
	Rules.ClearanceCm = Settings->RoadClearanceCm;
	Rules.SampleSpacingCm = Settings->RoadSampleSpacingCm;
	Rules.RoadbedOffsetCm = Settings->RoadbedOffsetCm;
	Rules.SkirtDepthCm = Settings->RoadSkirtDepthCm;
	Rules.TerrainBlendCm = Settings->RoadTerrainBlendCm;
	return Rules;
}

void FChronoRoadRules::ApplyTo(FChronoRoadGraph& Graph) const
{
	Graph.SetMinTrimCm(MinTrimCm);
	Graph.SetJunctionLevelRunCm(JunctionLevelRunCm);
}

EChronoRoadBuildResult ChronoRoadBuilder::ApplyAndValidate(FChronoRoadGraph& InOutGraph, const FChronoHeightfield& Heightfield, const FChronoRoadRules& Rules, const FChronoRoadBuildRequest& Request, int32& OutSegmentId)
{
	using namespace ChronoRoadBuilderPrivate;

	OutSegmentId = INDEX_NONE;

	if (Request.Profile.CarriagewayWidthCm <= 0.0f)
	{
		return EChronoRoadBuildResult::InvalidProfile;
	}

	if (!Heightfield.IsValid() || !Heightfield.IsInsideWorld(Request.Start) || !Heightfield.IsInsideWorld(Request.End))
	{
		return EChronoRoadBuildResult::OutOfBounds;
	}

	// The second end is resolved after the first: if both ends land on the
	// same existing road, the second one sees the already split road.
	const int32 StartNodeId = ResolveEndpoint(InOutGraph, Request.Start, Heightfield, Rules);
	const int32 EndNodeId = ResolveEndpoint(InOutGraph, Request.End, Heightfield, Rules);
	if (StartNodeId == INDEX_NONE || EndNodeId == INDEX_NONE || StartNodeId == EndNodeId)
	{
		return EChronoRoadBuildResult::SameEndpoint;
	}

	const FChronoRoadNode* StartNode = InOutGraph.FindNode(StartNodeId);
	const FChronoRoadNode* EndNode = InOutGraph.FindNode(EndNodeId);
	const FVector2D A(StartNode->Position.X, StartNode->Position.Y);
	const FVector2D B(EndNode->Position.X, EndNode->Position.Y);

	// A control point on top of an end gives a straight road.
	const bool bCurved = Request.bCurved
		&& FVector2D::DistSquared(Request.Control, A) > 1.0
		&& FVector2D::DistSquared(Request.Control, B) > 1.0;
	const FChronoBezier2D Curve = bCurved ? FChronoBezier2D::MakeQuadratic(A, Request.Control, B) : FChronoBezier2D::MakeLine(A, B);

	OutSegmentId = InOutGraph.AddSegment(StartNodeId, EndNodeId, Curve, Request.Profile);
	if (OutSegmentId == INDEX_NONE)
	{
		return EChronoRoadBuildResult::SameEndpoint;
	}

	return ValidateChanges(InOutGraph, OutSegmentId, Heightfield, Rules);
}

bool ChronoRoadBuilder::AreSegmentsTooClose(const FChronoRoadGraph& Graph, const FChronoRoadSegment& A, const FChronoRoadSegment& B, const FChronoRoadRules& Rules, double ExtraClearanceCm)
{
	if (A.Id == B.Id)
	{
		return false;
	}

	// Centerlines closer than this mean the two ribbons are too close.
	const double Required = A.Profile.GetHalfWidthCm() + B.Profile.GetHalfWidthCm() + ExtraClearanceCm;

	// Segment bounds already include the half widths.
	if (!A.Bounds.ExpandBy(ExtraClearanceCm).Intersect(B.Bounds))
	{
		return false;
	}

	// Two roads that share a node legitimately come close around it. The
	// minimum angle rule guarantees they are apart beyond this distance.
	const double SinMinAngle = FMath::Max(FMath::Sin(FMath::DegreesToRadians(static_cast<double>(Rules.MinIntersectionAngleDegrees))), 0.05);
	const double ExclusionRadius = Required / SinMinAngle + Rules.MinTrimCm;

	TArray<FVector2D, TInlineAllocator<2>> SharedNodePositions;
	for (const int32 NodeId : { A.StartNodeId, A.EndNodeId })
	{
		if (NodeId == B.StartNodeId || NodeId == B.EndNodeId)
		{
			if (const FChronoRoadNode* Node = Graph.FindNode(NodeId))
			{
				SharedNodePositions.Add(FVector2D(Node->Position.X, Node->Position.Y));
			}
		}
	}

	TArray<FChronoRoadSample> SamplesA;
	TArray<FChronoRoadSample> SamplesB;
	ChronoRoadGeometry::BuildPolyline(A.Curve, Rules.SampleSpacingCm, SamplesA);
	ChronoRoadGeometry::BuildPolyline(B.Curve, Rules.SampleSpacingCm, SamplesB);

	for (const FChronoRoadSample& Sample : SamplesA)
	{
		bool bNearSharedNode = false;
		for (const FVector2D& NodePosition : SharedNodePositions)
		{
			bNearSharedNode |= FVector2D::DistSquared(Sample.Position, NodePosition) < ExclusionRadius * ExclusionRadius;
		}
		if (!bNearSharedNode && ChronoRoadGeometry::DistancePointToPolyline(Sample.Position, SamplesB) < Required)
		{
			return true;
		}
	}

	return false;
}

FChronoRoadChangeSet ChronoRoadBuilder::TakeChanges(FChronoRoadGraph& InOutGraph)
{
	FChronoRoadChangeSet Changes;
	Changes.ChangedSegments = InOutGraph.GetDirtySegments().Array();
	Changes.RemovedSegments = InOutGraph.GetRemovedSegments().Array();
	Changes.ChangedNodes = InOutGraph.GetDirtyNodes().Array();
	Changes.RemovedNodes = InOutGraph.GetRemovedNodes().Array();
	InOutGraph.ClearChanges();

	// Sorted so that terrain shaping and listeners run in a deterministic order.
	Changes.ChangedSegments.Sort();
	Changes.RemovedSegments.Sort();
	Changes.ChangedNodes.Sort();
	Changes.RemovedNodes.Sort();
	return Changes;
}

FIntRect ChronoRoadBuilder::ConformTerrain(const FChronoRoadGraph& Graph, const FChronoRoadChangeSet& Changes, const FChronoRoadRules& Rules, FChronoHeightfield& InOutHeightfield)
{
	using namespace ChronoRoadBuilderPrivate;

	FIntRect Dirty;
	if (!InOutHeightfield.IsValid())
	{
		return Dirty;
	}

	const double Spacing = Rules.SampleSpacingCm;
	const float Offset = Rules.RoadbedOffsetCm;
	const float Blend = Rules.TerrainBlendCm;
	const double CellSize = InOutHeightfield.GetCellSizeCm();

	TArray<FVector> Path;

	// Pass 1: lay the roadbed of everything that changed and blend the
	// surrounding ground toward it.
	FBox2D TouchedArea(ForceInit);
	for (const int32 SegmentId : Changes.ChangedSegments)
	{
		if (const FChronoRoadSegment* Segment = Graph.FindSegment(SegmentId))
		{
			Graph.BuildSegmentPath(*Segment, Spacing, Path);
			Dirty = UnionRects(Dirty, InOutHeightfield.ConformToPath(Path, Segment->Profile.GetHalfWidthCm(), Offset, Blend, EChronoConformMode::BlendAndSet));
			TouchedArea += Segment->Bounds;
		}
	}
	for (const int32 NodeId : Changes.ChangedNodes)
	{
		const FChronoRoadNode* Node = Graph.FindNode(NodeId);
		if (Node && Node->SegmentIds.Num() >= 2)
		{
			const float Radius = Graph.GetNodeRadiusCm(NodeId);
			Path.Reset();
			Path.Add(Node->Position);
			Dirty = UnionRects(Dirty, InOutHeightfield.ConformToPath(Path, Radius, Offset, Blend, EChronoConformMode::BlendAndSet));

			const FVector2D Center(Node->Position.X, Node->Position.Y);
			TouchedArea += FBox2D(Center - FVector2D(Radius, Radius), Center + FVector2D(Radius, Radius));
		}
	}

	if (!TouchedArea.bIsValid)
	{
		return Dirty;
	}

	// Pass 2: the blend of one road may have moved the ground under another
	// road nearby. Restore the exact roadbed of everything in the touched
	// area. Junction discs go first and segments last, because a disc can
	// reach over the sloped part of a road and the road must win there.
	const FBox2D SearchArea = TouchedArea.ExpandBy(Blend + 2.0 * CellSize);

	TArray<int32> NodeIds;
	for (const TPair<int32, FChronoRoadNode>& Pair : Graph.GetNodes())
	{
		if (Pair.Value.SegmentIds.Num() >= 2 && SearchArea.IsInside(FVector2D(Pair.Value.Position.X, Pair.Value.Position.Y)))
		{
			NodeIds.Add(Pair.Key);
		}
	}
	NodeIds.Sort();

	TArray<int32> SegmentIds;
	for (const TPair<int32, FChronoRoadSegment>& Pair : Graph.GetSegments())
	{
		if (SearchArea.Intersect(Pair.Value.Bounds))
		{
			SegmentIds.Add(Pair.Key);
		}
	}
	SegmentIds.Sort();

	for (const int32 NodeId : NodeIds)
	{
		const FChronoRoadNode& Node = Graph.GetNodes().FindChecked(NodeId);
		Path.Reset();
		Path.Add(Node.Position);
		Dirty = UnionRects(Dirty, InOutHeightfield.ConformToPath(Path, Graph.GetNodeRadiusCm(NodeId), Offset, 0.0f, EChronoConformMode::Set));
	}

	// Segments: set, then keep the lowest where the margins of two roads
	// overlap. Ground below a road is hidden by the side faces of the road;
	// ground above a road would clip through it.
	for (const EChronoConformMode Mode : { EChronoConformMode::Set, EChronoConformMode::Min })
	{
		for (const int32 SegmentId : SegmentIds)
		{
			const FChronoRoadSegment& Segment = Graph.GetSegments().FindChecked(SegmentId);
			Graph.BuildSegmentPath(Segment, Spacing, Path);
			Dirty = UnionRects(Dirty, InOutHeightfield.ConformToPath(Path, Segment.Profile.GetHalfWidthCm(), Offset, 0.0f, Mode));
		}
	}

	return Dirty;
}
