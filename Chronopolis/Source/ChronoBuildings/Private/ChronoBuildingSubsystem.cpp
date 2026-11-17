#include "ChronoBuildingSubsystem.h"

#include "ChronoBuildingRenderActor.h"
#include "ChronoCore.h"
#include "ChronoHeightfield.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoSimulationSubsystem.h"
#include "ChronoTerrainSubsystem.h"
#include "ChronoZoningSubsystem.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ChronoBuildings"

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
	Availability.Filter(Definitions, ActiveDefinitions);
	RenewalsPerStep = FMath::Max(Settings->RenewalsPerStep, 0);
	GrowthIntervalTicks = FMath::Max(Settings->GrowthIntervalTicks, 1);
	GrowthPerStep = FMath::Max(Settings->GrowthPerStep, 0);
	UpgradesPerStep = FMath::Max(Settings->UpgradesPerStep, 0);
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

void UChronoBuildingSubsystem::SetAvailability(const FChronoBuildingAvailability& InAvailability)
{
	Availability = InAvailability;
	Availability.Filter(Definitions, ActiveDefinitions);
	Buildings.SetStyle(Availability.Civilization, Availability.Era);
}

void UChronoBuildingSubsystem::SetCivilizationModifiers(float InConstructionCostMultiplier, float InWaterRadiusMultiplier)
{
	ConstructionCostMultiplier = FMath::Max(InConstructionCostMultiplier, 0.0f);
	WaterRadiusMultiplier = FMath::Max(InWaterRadiusMultiplier, 0.0f);
}

bool UChronoBuildingSubsystem::SetHeritageAt(FVector2D WorldXY, bool bHeritage)
{
	const int32 BuildingId = Buildings.FindBuildingAt(WorldXY);
	const FChronoBuilding* Building = Buildings.FindBuilding(BuildingId);
	// Only buildings that grew on their own: the player's services are never renewed anyway.
	if (!Building || Building->bPlayerPlaced || !Buildings.SetHeritage(BuildingId, bHeritage))
	{
		return false;
	}

	BroadcastChanges();
	return true;
}

bool UChronoBuildingSubsystem::IsHeritageAt(FVector2D WorldXY) const
{
	const FChronoBuilding* Building = Buildings.FindBuilding(Buildings.FindBuildingAt(WorldXY));
	return Building && Building->bHeritage;
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

	const int32 NumCreated = Buildings.Grow(Zoning->GetGrid(), Terrain->GetHeightfield(), Roads->GetGraph(), ActiveDefinitions, Rules, Random, MaxNewBuildings, bHasZoneDemand ? &ZoneDemand : nullptr);
	// Buildings already standing gain a level where the land value allows it.
	const int32 NumUpgraded = Buildings.UpgradeBuildings(Rules, Random, UpgradesPerStep);
	// Buildings of an earlier era are rebuilt in the current style, a few at a time.
	const int32 NumRenewed = Buildings.RenewBuildings(Zoning->GetGrid(), Terrain->GetHeightfield(), Roads->GetGraph(), ActiveDefinitions, Rules, Random, RenewalsPerStep);

	if (NumCreated > 0 || NumUpgraded > 0 || NumRenewed > 0)
	{
		BroadcastChanges();
	}
	return NumCreated;
}

EChronoPlaceResult UChronoBuildingSubsystem::PlaceServiceBuilding(FName DefId, FVector2D WorldXY, int32& OutBuildingId)
{
	OutBuildingId = INDEX_NONE;

	// The building asked for, or this civilization's own version of it.
	const FChronoBuildingDefData* Known = FindDefinition(DefId);
	const FChronoBuildingDefData* Resolved = Availability.Resolve(DefId, Definitions);
	if (!Known || Known->Service == EChronoServiceType::None)
	{
		return EChronoPlaceResult::UnknownBuilding;
	}
	if (!Resolved || Resolved->Service == EChronoServiceType::None)
	{
		return EChronoPlaceResult::NotUnlocked;
	}

	FChronoBuildingDefData Placed = *Resolved;
	if (Placed.Service == EChronoServiceType::Water)
	{
		Placed.ServiceRadiusCm *= WaterRadiusMultiplier;
	}
	const FChronoBuildingDefData* Definition = &Placed;

	const UWorld* World = GetWorld();
	const UChronoTerrainSubsystem* Terrain = World ? World->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	const UChronoRoadSubsystem* Roads = World ? World->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	const UChronoZoningSubsystem* Zoning = World ? World->GetSubsystem<UChronoZoningSubsystem>() : nullptr;
	if (!Terrain || !Roads || !Zoning)
	{
		return EChronoPlaceResult::NoLot;
	}

	const int32 CellId = Zoning->GetGrid().FindCellAt(WorldXY);
	if (CellId == INDEX_NONE)
	{
		return EChronoPlaceResult::NoLot;
	}

	OutBuildingId = Buildings.PlaceBuilding(Zoning->GetGrid(), Terrain->GetHeightfield(), Roads->GetGraph(), CellId, *Definition, Rules);
	if (OutBuildingId == INDEX_NONE)
	{
		return EChronoPlaceResult::DoesNotFit;
	}

	BroadcastChanges();
	return EChronoPlaceResult::Success;
}

bool UChronoBuildingSubsystem::DemolishBuildingAt(FVector2D WorldXY)
{
	if (!Buildings.DemolishBuilding(Buildings.FindBuildingAt(WorldXY)))
	{
		return false;
	}

	BroadcastChanges();
	return true;
}

TArray<FName> UChronoBuildingSubsystem::GetServiceBuildingIds() const
{
	TArray<FName> Ids;
	for (const FChronoBuildingDefData& Definition : ActiveDefinitions)
	{
		if (Definition.Service != EChronoServiceType::None)
		{
			Ids.Add(Definition.DefId);
		}
	}
	return Ids;
}

float UChronoBuildingSubsystem::GetConstructionCost(FName DefId) const
{
	const FChronoBuildingDefData* Definition = Availability.Resolve(DefId, Definitions);
	return Definition ? Definition->ConstructionCost * ConstructionCostMultiplier : 0.0f;
}

FText UChronoBuildingSubsystem::GetPlaceResultText(EChronoPlaceResult Result)
{
	switch (Result)
	{
	case EChronoPlaceResult::Success:
		return LOCTEXT("PlaceBuilt", "Building placed.");
	case EChronoPlaceResult::UnknownBuilding:
		return LOCTEXT("PlaceUnknown", "No building is selected.");
	case EChronoPlaceResult::NoLot:
		return LOCTEXT("PlaceNoLot", "There is no free plot here. Buildings stand on the plots along a road.");
	case EChronoPlaceResult::DoesNotFit:
		return LOCTEXT("PlaceDoesNotFit", "The building does not fit here: the plot is too small, already taken, or on a bend.");
	case EChronoPlaceResult::NotEnoughMoney:
		return LOCTEXT("PlaceNoMoney", "Not enough money in the treasury.");
	case EChronoPlaceResult::NotUnlocked:
		return LOCTEXT("PlaceNotUnlocked", "Your city does not know how to build this yet: research the technology or civic that unlocks it.");
	default:
		return FText::GetEmpty();
	}
}

void UChronoBuildingSubsystem::SetZoneDemand(const FChronoZoneDemand& InDemand)
{
	ZoneDemand = InDemand;
	bHasZoneDemand = true;
}

float UChronoBuildingSubsystem::GetZoneDemand(EChronoZoneType Zone) const
{
	return (bHasZoneDemand && Zone < EChronoZoneType::Count) ? ZoneDemand.Get(Zone) : 1.0f;
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

#undef LOCTEXT_NAMESPACE
