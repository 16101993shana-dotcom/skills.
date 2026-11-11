#include "ChronoRoadSubsystem.h"

#include "ChronoCore.h"
#include "ChronoHeightfield.h"
#include "ChronoRoadNetworkActor.h"
#include "ChronoSettings.h"
#include "ChronoTerrainSubsystem.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ChronoRoads"

void UChronoRoadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Roads read and shape the terrain: make sure it exists first.
	Collection.InitializeDependency<UChronoTerrainSubsystem>();
	Super::Initialize(Collection);

	Graph.SetMinTrimCm(UChronoSettings::Get()->RoadMinTrimCm);
}

bool UChronoRoadSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChronoRoadSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	NetworkActor = InWorld.SpawnActor<AChronoRoadNetworkActor>(AChronoRoadNetworkActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (NetworkActor)
	{
		NetworkActor->InitializeFromSubsystem(this);
	}
	else
	{
		UE_LOG(LogChrono, Error, TEXT("Failed to spawn the road network actor"));
	}
}

FText UChronoRoadSubsystem::GetBuildResultText(EChronoRoadBuildResult Result)
{
	switch (Result)
	{
	case EChronoRoadBuildResult::Success:
		return LOCTEXT("RoadBuilt", "Road built.");
	case EChronoRoadBuildResult::InvalidProfile:
		return LOCTEXT("RoadInvalidProfile", "No road type is selected.");
	case EChronoRoadBuildResult::OutOfBounds:
		return LOCTEXT("RoadOutOfBounds", "The road leaves the map.");
	case EChronoRoadBuildResult::SameEndpoint:
		return LOCTEXT("RoadSameEndpoint", "The road starts and ends at the same place.");
	case EChronoRoadBuildResult::TooShort:
		return LOCTEXT("RoadTooShort", "Too short: there is not enough room between the intersections.");
	case EChronoRoadBuildResult::TooSteep:
		return LOCTEXT("RoadTooSteep", "Too steep for this type of road. Level the ground or make the road longer.");
	case EChronoRoadBuildResult::CurveTooTight:
		return LOCTEXT("RoadCurveTooTight", "The curve is too tight for this type of road.");
	case EChronoRoadBuildResult::AngleTooSharp:
		return LOCTEXT("RoadAngleTooSharp", "The angle with a neighboring road is too sharp.");
	case EChronoRoadBuildResult::TooManyRoads:
		return LOCTEXT("RoadTooManyRoads", "Too many roads meet at this intersection.");
	case EChronoRoadBuildResult::OverlapsRoad:
		return LOCTEXT("RoadOverlaps", "The road overlaps another road. End it on that road to create an intersection.");
	default:
		return FText::GetEmpty();
	}
}

EChronoRoadBuildResult UChronoRoadSubsystem::BuildRoad(const FChronoRoadBuildRequest& Request, int32& OutSegmentId)
{
	OutSegmentId = INDEX_NONE;

	// Try the edit on a copy: the real graph is only replaced on success, so
	// a refused road leaves no trace (no split, no stray node).
	FChronoRoadGraph Scratch = Graph;
	Scratch.ClearChanges();

	int32 NewSegmentId = INDEX_NONE;
	const EChronoRoadBuildResult Result = ApplyAndValidate(Scratch, Request, NewSegmentId);
	if (Result != EChronoRoadBuildResult::Success)
	{
		return Result;
	}

	Graph = MoveTemp(Scratch);
	OutSegmentId = NewSegmentId;
	CommitChanges();
	return EChronoRoadBuildResult::Success;
}

EChronoRoadBuildResult UChronoRoadSubsystem::CanBuildRoad(const FChronoRoadBuildRequest& Request) const
{
	FChronoRoadGraph Scratch = Graph;
	Scratch.ClearChanges();

	int32 NewSegmentId = INDEX_NONE;
	return ApplyAndValidate(Scratch, Request, NewSegmentId);
}

bool UChronoRoadSubsystem::DemolishRoadAt(FVector2D WorldXY, float MaxDistanceCm)
{
	double T = 0.0;
	FVector2D Point;
	const int32 SegmentId = Graph.FindNearestSegment(WorldXY, MaxDistanceCm, T, Point);
	if (SegmentId == INDEX_NONE || !Graph.RemoveSegment(SegmentId))
	{
		return false;
	}

	CommitChanges();
	return true;
}

EChronoRoadBuildResult UChronoRoadSubsystem::ApplyAndValidate(FChronoRoadGraph& InOutGraph, const FChronoRoadBuildRequest& Request, int32& OutSegmentId) const
{
	OutSegmentId = INDEX_NONE;

	if (Request.Profile.CarriagewayWidthCm <= 0.0f)
	{
		return EChronoRoadBuildResult::InvalidProfile;
	}

	const UChronoTerrainSubsystem* Terrain = GetWorld() ? GetWorld()->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	if (!Terrain || !Terrain->GetHeightfield().IsValid())
	{
		return EChronoRoadBuildResult::OutOfBounds;
	}
	const FChronoHeightfield& Heightfield = Terrain->GetHeightfield();

	if (!Heightfield.IsInsideWorld(Request.Start) || !Heightfield.IsInsideWorld(Request.End))
	{
		return EChronoRoadBuildResult::OutOfBounds;
	}

	// The second end is resolved after the first: if both ends land on the
	// same existing road, the second one sees the already split road.
	const int32 StartNodeId = ResolveEndpoint(InOutGraph, Request.Start, Heightfield);
	const int32 EndNodeId = ResolveEndpoint(InOutGraph, Request.End, Heightfield);
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

	return ValidateChanges(InOutGraph, OutSegmentId, Heightfield);
}

int32 UChronoRoadSubsystem::ResolveEndpoint(FChronoRoadGraph& InOutGraph, const FVector2D& Point, const FChronoHeightfield& Heightfield) const
{
	const double SnapDistance = UChronoSettings::Get()->RoadSnapDistanceCm;

	// 1. An existing node nearby: connect to it.
	const int32 NearNodeId = InOutGraph.FindNearestNode(Point, SnapDistance);
	if (NearNodeId != INDEX_NONE)
	{
		return NearNodeId;
	}

	// 2. An existing road nearby: split it and connect to the new node.
	double T = 0.0;
	FVector2D OnRoad;
	const int32 NearSegmentId = InOutGraph.FindNearestSegment(Point, SnapDistance, T, OnRoad);
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

EChronoRoadBuildResult UChronoRoadSubsystem::ValidateChanges(const FChronoRoadGraph& InGraph, int32 NewSegmentId, const FChronoHeightfield& Heightfield) const
{
	const UChronoSettings* Settings = UChronoSettings::Get();

	const FChronoRoadSegment* NewSegment = InGraph.FindSegment(NewSegmentId);
	if (!NewSegment)
	{
		return EChronoRoadBuildResult::SameEndpoint;
	}

	// The whole centerline must stay on the map.
	TArray<FChronoRoadSample> NewSamples;
	ChronoRoadGeometry::BuildPolyline(NewSegment->Curve, Settings->RoadSampleSpacingCm, NewSamples);
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
	for (const int32 NodeId : InGraph.GetDirtyNodes())
	{
		const FChronoRoadNode* Node = InGraph.FindNode(NodeId);
		if (!Node)
		{
			continue;
		}
		if (Node->SegmentIds.Num() > Settings->RoadMaxNodeDegree)
		{
			return EChronoRoadBuildResult::TooManyRoads;
		}
		if (Node->SegmentIds.Num() >= 2 && InGraph.GetNodeMinAngleDegrees(NodeId) < Settings->RoadMinIntersectionAngleDegrees)
		{
			return EChronoRoadBuildResult::AngleTooSharp;
		}
	}

	// Every segment touched by the edit (the new one, the two halves of a
	// split road, and the neighbors whose junction grew): each must keep
	// enough length between its junctions and an acceptable grade.
	for (const int32 SegmentId : InGraph.GetDirtySegments())
	{
		const FChronoRoadSegment* Segment = InGraph.FindSegment(SegmentId);
		if (!Segment)
		{
			continue;
		}
		if (Segment->GetUsableLength() < Settings->RoadMinUsableLengthCm)
		{
			return EChronoRoadBuildResult::TooShort;
		}
		if (InGraph.GetSegmentSlopePercent(*Segment) > Segment->Profile.MaxSlopePercent)
		{
			return EChronoRoadBuildResult::TooSteep;
		}
	}

	if (OverlapsOtherRoad(InGraph, *NewSegment))
	{
		return EChronoRoadBuildResult::OverlapsRoad;
	}

	return EChronoRoadBuildResult::Success;
}

bool UChronoRoadSubsystem::OverlapsOtherRoad(const FChronoRoadGraph& InGraph, const FChronoRoadSegment& NewSegment) const
{
	const UChronoSettings* Settings = UChronoSettings::Get();
	const double Spacing = Settings->RoadSampleSpacingCm;
	const double Clearance = Settings->RoadClearanceCm;
	const double SinMinAngle = FMath::Max(FMath::Sin(FMath::DegreesToRadians(static_cast<double>(Settings->RoadMinIntersectionAngleDegrees))), 0.05);
	const double NewHalfWidth = NewSegment.Profile.GetHalfWidthCm();

	TArray<FChronoRoadSample> NewSamples;
	ChronoRoadGeometry::BuildPolyline(NewSegment.Curve, Spacing, NewSamples);

	TArray<FChronoRoadSample> OtherSamples;
	for (const TPair<int32, FChronoRoadSegment>& Pair : InGraph.GetSegments())
	{
		const FChronoRoadSegment& Other = Pair.Value;
		if (Other.Id == NewSegment.Id)
		{
			continue;
		}

		// Centerlines closer than this mean the two ribbons touch.
		const double Required = NewHalfWidth + Other.Profile.GetHalfWidthCm() + Clearance;

		// Segment bounds already include the half widths.
		if (!NewSegment.Bounds.ExpandBy(Clearance).Intersect(Other.Bounds))
		{
			continue;
		}

		// Two roads that share a node legitimately come close around it. The
		// minimum angle rule guarantees they are apart beyond this distance.
		const double ExclusionRadius = Required / SinMinAngle + Settings->RoadMinTrimCm;
		TArray<FVector2D, TInlineAllocator<2>> SharedNodePositions;
		for (const int32 NodeId : { NewSegment.StartNodeId, NewSegment.EndNodeId })
		{
			if (NodeId == Other.StartNodeId || NodeId == Other.EndNodeId)
			{
				if (const FChronoRoadNode* Node = InGraph.FindNode(NodeId))
				{
					SharedNodePositions.Add(FVector2D(Node->Position.X, Node->Position.Y));
				}
			}
		}

		ChronoRoadGeometry::BuildPolyline(Other.Curve, Spacing, OtherSamples);

		for (const FChronoRoadSample& Sample : NewSamples)
		{
			bool bNearSharedNode = false;
			for (const FVector2D& NodePosition : SharedNodePositions)
			{
				bNearSharedNode |= FVector2D::DistSquared(Sample.Position, NodePosition) < ExclusionRadius * ExclusionRadius;
			}
			if (bNearSharedNode)
			{
				continue;
			}

			if (ChronoRoadGeometry::DistancePointToPolyline(Sample.Position, OtherSamples) < Required)
			{
				return true;
			}
		}
	}

	return false;
}

void UChronoRoadSubsystem::CommitChanges()
{
	FChronoRoadChangeSet Changes;
	Changes.ChangedSegments = Graph.GetDirtySegments().Array();
	Changes.RemovedSegments = Graph.GetRemovedSegments().Array();
	Changes.ChangedNodes = Graph.GetDirtyNodes().Array();
	Changes.RemovedNodes = Graph.GetRemovedNodes().Array();
	Graph.ClearChanges();

	// Sorted so that terrain shaping and listeners run in a deterministic order.
	Changes.ChangedSegments.Sort();
	Changes.RemovedSegments.Sort();
	Changes.ChangedNodes.Sort();
	Changes.RemovedNodes.Sort();

	if (Changes.IsEmpty())
	{
		return;
	}

	ConformTerrain(Changes);
	OnRoadNetworkChanged.Broadcast(Changes);
}

void UChronoRoadSubsystem::ConformTerrain(const FChronoRoadChangeSet& Changes)
{
	UChronoTerrainSubsystem* Terrain = GetWorld() ? GetWorld()->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	if (!Terrain)
	{
		return;
	}

	const UChronoSettings* Settings = UChronoSettings::Get();
	const double Spacing = Settings->RoadSampleSpacingCm;
	const float Offset = Settings->RoadbedOffsetCm;
	const float Blend = Settings->RoadTerrainBlendCm;
	const double CellSize = Terrain->GetHeightfield().GetCellSizeCm();

	TArray<FVector> Path;

	// Pass 1: lay the roadbed of everything that changed and blend the
	// surrounding ground toward it.
	FBox2D TouchedArea(ForceInit);
	for (const int32 SegmentId : Changes.ChangedSegments)
	{
		if (const FChronoRoadSegment* Segment = Graph.FindSegment(SegmentId))
		{
			Graph.BuildSegmentPath(*Segment, Spacing, Path);
			Terrain->ConformToPath(Path, Segment->Profile.GetHalfWidthCm(), Offset, Blend, EChronoConformMode::BlendAndSet);
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
			Terrain->ConformToPath(Path, Radius, Offset, Blend, EChronoConformMode::BlendAndSet);

			const FVector2D Center(Node->Position.X, Node->Position.Y);
			TouchedArea += FBox2D(Center - FVector2D(Radius, Radius), Center + FVector2D(Radius, Radius));
		}
	}

	if (!TouchedArea.bIsValid)
	{
		return;
	}

	// Pass 2: the blend of one road may have moved the ground under another
	// road nearby. Restore the exact roadbed of every road in the touched
	// area, then, where two roadbeds at different heights overlap (around a
	// junction on a slope), keep the lower one. Ground below a road is hidden
	// by the side faces of the road; ground above a road would clip through.
	const FBox2D SearchArea = TouchedArea.ExpandBy(Blend + 2.0 * CellSize);

	TArray<int32> SegmentIds;
	for (const TPair<int32, FChronoRoadSegment>& Pair : Graph.GetSegments())
	{
		if (SearchArea.Intersect(Pair.Value.Bounds))
		{
			SegmentIds.Add(Pair.Key);
		}
	}
	SegmentIds.Sort();

	TArray<int32> NodeIds;
	for (const TPair<int32, FChronoRoadNode>& Pair : Graph.GetNodes())
	{
		if (Pair.Value.SegmentIds.Num() >= 2 && SearchArea.IsInside(FVector2D(Pair.Value.Position.X, Pair.Value.Position.Y)))
		{
			NodeIds.Add(Pair.Key);
		}
	}
	NodeIds.Sort();

	for (const EChronoConformMode Mode : { EChronoConformMode::Set, EChronoConformMode::Min })
	{
		for (const int32 SegmentId : SegmentIds)
		{
			const FChronoRoadSegment& Segment = Graph.GetSegments().FindChecked(SegmentId);
			Graph.BuildSegmentPath(Segment, Spacing, Path);
			Terrain->ConformToPath(Path, Segment.Profile.GetHalfWidthCm(), Offset, 0.0f, Mode);
		}
		for (const int32 NodeId : NodeIds)
		{
			const FChronoRoadNode& Node = Graph.GetNodes().FindChecked(NodeId);
			Path.Reset();
			Path.Add(Node.Position);
			Terrain->ConformToPath(Path, Graph.GetNodeRadiusCm(NodeId), Offset, 0.0f, Mode);
		}
	}
}

#undef LOCTEXT_NAMESPACE
