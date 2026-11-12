#include "ChronoBuildingSubsystem.h"

#include "ChronoBuildingRenderActor.h"
#include "ChronoCore.h"
#include "ChronoHeightfield.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoSimulationSubsystem.h"
#include "ChronoTerrainSubsystem.h"
#include "ChronoZoningSubsystem.h"
#include "Engine/World.h"

void UChronoBuildingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Buildings stand on zoned cells, along roads, on the terrain, and grow
	// with the simulation clock: all four must exist first.
	UChronoTerrainSubsystem* Terrain = Collection.InitializeDependency<UChronoTerrainSubsystem>();
	Collection.InitializeDependency<UChronoRoadSubsystem>();
	UChronoZoningSubsystem* Zoning = Collection.InitializeDependency<UChronoZoningSubsystem>();
	UChronoSimulationSubsystem* Simulation = Collection.InitializeDependency<UChronoSimulationSubsystem>();
	Super::Initialize(Collection);

	const UChronoBuildingSettings* Settings = UChronoBuildingSettings::Get();
	Rules = FChronoBuildingRules::FromSettings();
	Definitions = Settings->Definitions;
	GrowthIntervalTicks = FMath::Max(Settings->GrowthIntervalTicks, 1);
	GrowthPerStep = FMath::Max(Settings->GrowthPerStep, 0);
	GrowthSeed = Settings->GrowthSeed;

	if (Definitions.Num() == 0)
	{
		UE_LOG(LogChrono, Warning, TEXT("No building definitions in the project settings: nothing will grow"));
	}

	if (Terrain)
	{
		TerrainChangedHandle = Terrain->OnTerrainChanged.AddUObject(this, &UChronoBuildingSubsystem::HandleTerrainChanged);
	}
	if (Zoning)
	{
		ZoningChangedHandle = Zoning->OnZoningChanged.AddUObject(this, &UChronoBuildingSubsystem::HandleZoningChanged);
	}
	if (Simulation)
	{
		SimTickHandle = Simulation->OnSimTick.AddUObject(this, &UChronoBuildingSubsystem::HandleSimTick);
	}
}

void UChronoBuildingSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		if (UChronoTerrainSubsystem* Terrain = World->GetSubsystem<UChronoTerrainSubsystem>())
		{
			Terrain->OnTerrainChanged.Remove(TerrainChangedHandle);
		}
		if (UChronoZoningSubsystem* Zoning = World->GetSubsystem<UChronoZoningSubsystem>())
		{
			Zoning->OnZoningChanged.Remove(ZoningChangedHandle);
		}
		if (UChronoSimulationSubsystem* Simulation = World->GetSubsystem<UChronoSimulationSubsystem>())
		{
			Simulation->OnSimTick.Remove(SimTickHandle);
		}
	}
	TerrainChangedHandle.Reset();
	ZoningChangedHandle.Reset();
	SimTickHandle.Reset();

	Super::Deinitialize();
}

bool UChronoBuildingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChronoBuildingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RenderActor = InWorld.SpawnActor<AChronoBuildingRenderActor>(AChronoBuildingRenderActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (RenderActor)
	{
		RenderActor->InitializeFromSubsystem(this);
	}
	else
	{
		UE_LOG(LogChrono, Error, TEXT("Failed to spawn the building render actor"));
	}
}

const FChronoBuildingDefData* UChronoBuildingSubsystem::FindDefinition(FName DefId) const
{
	return Definitions.FindByPredicate([DefId](const FChronoBuildingDefData& Definition) { return Definition.DefId == DefId; });
}

float UChronoBuildingSubsystem::GetLandValueAt(FVector2D WorldXY) const
{
	// A point fronts on a road when it lies in a first-row cell.
	bool bFrontsOnRoad = false;
	if (const UChronoZoningSubsystem* Zoning = GetWorld() ? GetWorld()->GetSubsystem<UChronoZoningSubsystem>() : nullptr)
	{
		const FChronoZoneCell* Cell = Zoning->GetGrid().FindCell(Zoning->GetGrid().FindCellAt(WorldXY));
		bFrontsOnRoad = Cell && Cell->Row == 0;
	}
	return Buildings.ComputeLandValue(WorldXY, bFrontsOnRoad, Rules);
}

void UChronoBuildingSubsystem::HandleSimTick(int64 TickIndex, float FixedDeltaSeconds)
{
	if (GrowthPerStep > 0 && TickIndex % GrowthIntervalTicks == 0)
	{
		GrowStep(TickIndex, GrowthPerStep);
	}
}

int32 UChronoBuildingSubsystem::GrowNow(int32 MaxNewBuildings)
{
	const UChronoSimulationSubsystem* Simulation = GetWorld() ? GetWorld()->GetSubsystem<UChronoSimulationSubsystem>() : nullptr;
	return GrowStep(Simulation ? Simulation->GetTickIndex() : 0, MaxNewBuildings);
}

int32 UChronoBuildingSubsystem::GrowStep(int64 TickIndex, int32 MaxNewBuildings)
{
	const UWorld* World = GetWorld();
	const UChronoTerrainSubsystem* Terrain = World ? World->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	const UChronoRoadSubsystem* Roads = World ? World->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	const UChronoZoningSubsystem* Zoning = World ? World->GetSubsystem<UChronoZoningSubsystem>() : nullptr;
	if (!Terrain || !Roads || !Zoning)
	{
		return 0;
	}

	// The stream depends only on the seed, the tick and the number of
	// buildings: replaying the same inputs grows the same city.
	const int32 StepSeed = GrowthSeed ^ static_cast<int32>(TickIndex & 0x7FFFFFFF) ^ (Buildings.GetNumBuildings() * 7919);
	FRandomStream Random(StepSeed);

	const int32 NumCreated = Buildings.Grow(Zoning->GetGrid(), Terrain->GetHeightfield(), Roads->GetGraph(), Definitions, Rules, Random, MaxNewBuildings);
	if (NumCreated > 0)
	{
		BroadcastChanges();
	}
	return NumCreated;
}

void UChronoBuildingSubsystem::HandleZoningChanged(const TArray<int32>& SegmentIds)
{
	const UChronoZoningSubsystem* Zoning = GetWorld() ? GetWorld()->GetSubsystem<UChronoZoningSubsystem>() : nullptr;
	if (!Zoning)
	{
		return;
	}

	if (Buildings.RemoveInvalid(Zoning->GetGrid(), SegmentIds) > 0)
	{
		BroadcastChanges();
	}
}

void UChronoBuildingSubsystem::HandleTerrainChanged(const FIntRect& DirtyVertexRect)
{
	const UChronoTerrainSubsystem* Terrain = GetWorld() ? GetWorld()->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	if (!Terrain || Buildings.GetNumBuildings() == 0)
	{
		return;
	}

	// The ground moved (a road was laid nearby, the player terraformed):
	// buildings there are re-seated so they are neither buried nor floating.
	const FChronoHeightfield& Heightfield = Terrain->GetHeightfield();
	const FBox2D WorldBounds(
		Heightfield.VertexToWorld(DirtyVertexRect.Min.X, DirtyVertexRect.Min.Y),
		Heightfield.VertexToWorld(DirtyVertexRect.Max.X, DirtyVertexRect.Max.Y));

	if (Buildings.RefreshHeights(Heightfield, WorldBounds.ExpandBy(Heightfield.GetCellSizeCm()), Rules) > 0)
	{
		BroadcastChanges();
	}
}

bool UChronoBuildingSubsystem::SerializeBuildings(FArchive& Ar)
{
	if (!Ar.IsLoading())
	{
		return Buildings.Serialize(Ar);
	}

	FChronoBuildingSet Loaded;
	if (!Loaded.Serialize(Ar))
	{
		UE_LOG(LogChrono, Error, TEXT("Building data is invalid, keeping the current buildings"));
		return false;
	}

	// Mark what is about to disappear, so the render actor drops it.
	Buildings.Reset();
	TArray<int32> Dirty = Buildings.TakeDirtySegments();

	Buildings = MoveTemp(Loaded);
	for (const int32 SegmentId : Buildings.TakeDirtySegments())
	{
		Dirty.AddUnique(SegmentId);
	}
	Dirty.Sort();
	OnBuildingsChanged.Broadcast(Dirty);
	return true;
}

void UChronoBuildingSubsystem::BroadcastChanges()
{
	const TArray<int32> Dirty = Buildings.TakeDirtySegments();
	if (Dirty.Num() > 0)
	{
		OnBuildingsChanged.Broadcast(Dirty);
	}
}
