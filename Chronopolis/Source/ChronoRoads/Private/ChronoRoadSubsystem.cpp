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

	Rules = FChronoRoadRules::FromSettings();
	Rules.ApplyTo(Graph);
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

	const UChronoTerrainSubsystem* Terrain = GetWorld() ? GetWorld()->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	if (!Terrain)
	{
		return EChronoRoadBuildResult::OutOfBounds;
	}

	// Try the edit on a copy: the real graph is only replaced on success, so
	// a refused road leaves no trace (no split, no stray node).
	FChronoRoadGraph Scratch = Graph;
	Scratch.ClearChanges();

	int32 NewSegmentId = INDEX_NONE;
	const EChronoRoadBuildResult Result = ChronoRoadBuilder::ApplyAndValidate(Scratch, Terrain->GetHeightfield(), Rules, Request, NewSegmentId);
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
	const UChronoTerrainSubsystem* Terrain = GetWorld() ? GetWorld()->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	if (!Terrain)
	{
		return EChronoRoadBuildResult::OutOfBounds;
	}

	FChronoRoadGraph Scratch = Graph;
	Scratch.ClearChanges();

	int32 NewSegmentId = INDEX_NONE;
	return ChronoRoadBuilder::ApplyAndValidate(Scratch, Terrain->GetHeightfield(), Rules, Request, NewSegmentId);
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

void UChronoRoadSubsystem::CommitChanges()
{
	const FChronoRoadChangeSet Changes = ChronoRoadBuilder::TakeChanges(Graph);
	if (Changes.IsEmpty())
	{
		return;
	}

	if (UChronoTerrainSubsystem* Terrain = GetWorld() ? GetWorld()->GetSubsystem<UChronoTerrainSubsystem>() : nullptr)
	{
		Terrain->ApplyEdit([this, &Changes](FChronoHeightfield& Heightfield)
		{
			return ChronoRoadBuilder::ConformTerrain(Graph, Changes, Rules, Heightfield);
		});
	}

	OnRoadNetworkChanged.Broadcast(Changes);
}

bool UChronoRoadSubsystem::SerializeRoads(FArchive& Ar)
{
	if (!Ar.IsLoading())
	{
		return Graph.Serialize(Ar);
	}

	FChronoRoadGraph Loaded;
	Rules.ApplyTo(Loaded);
	if (!Loaded.Serialize(Ar))
	{
		UE_LOG(LogChrono, Error, TEXT("Road data is invalid, keeping the current road network"));
		return false;
	}

	// Everything that existed is removed, everything loaded is new. The
	// terrain is not reshaped: the saved terrain already fits the saved roads.
	FChronoRoadChangeSet Changes;
	Graph.GetSegments().GetKeys(Changes.RemovedSegments);
	Graph.GetNodes().GetKeys(Changes.RemovedNodes);

	Graph = MoveTemp(Loaded);
	Graph.ClearChanges();

	Graph.GetSegments().GetKeys(Changes.ChangedSegments);
	Graph.GetNodes().GetKeys(Changes.ChangedNodes);

	OnRoadNetworkChanged.Broadcast(Changes);
	return true;
}

#undef LOCTEXT_NAMESPACE
