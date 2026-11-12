#include "ChronoZoningSubsystem.h"

#include "ChronoCore.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoRoadTypes.h"
#include "ChronoTerrainSubsystem.h"
#include "ChronoZoneOverlayActor.h"
#include "Engine/World.h"

void UChronoZoningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Cells are laid out along roads, on the terrain: both must exist first.
	Collection.InitializeDependency<UChronoTerrainSubsystem>();
	UChronoRoadSubsystem* Roads = Collection.InitializeDependency<UChronoRoadSubsystem>();
	Super::Initialize(Collection);

	Rules = FChronoZoningRules::FromSettings();

	if (Roads)
	{
		RoadsChangedHandle = Roads->OnRoadNetworkChanged.AddUObject(this, &UChronoZoningSubsystem::HandleRoadNetworkChanged);
	}
}

void UChronoZoningSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		if (UChronoRoadSubsystem* Roads = World->GetSubsystem<UChronoRoadSubsystem>())
		{
			Roads->OnRoadNetworkChanged.Remove(RoadsChangedHandle);
		}
	}
	RoadsChangedHandle.Reset();

	Super::Deinitialize();
}

bool UChronoZoningSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChronoZoningSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	OverlayActor = InWorld.SpawnActor<AChronoZoneOverlayActor>(AChronoZoneOverlayActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (OverlayActor)
	{
		OverlayActor->InitializeFromSubsystem(this);
	}
	else
	{
		UE_LOG(LogChrono, Error, TEXT("Failed to spawn the zone overlay actor"));
	}
}

void UChronoZoningSubsystem::HandleRoadNetworkChanged(const FChronoRoadChangeSet& Changes)
{
	const UWorld* World = GetWorld();
	const UChronoTerrainSubsystem* Terrain = World ? World->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	const UChronoRoadSubsystem* Roads = World ? World->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	if (!Terrain || !Roads)
	{
		return;
	}

	// The road subsystem shapes the terrain before it broadcasts, so the
	// height checks below already see the final ground.
	Grid.ApplyRoadChanges(Roads->GetGraph(), Terrain->GetHeightfield(), Changes, Rules);
	BroadcastChanges();
}

int32 UChronoZoningSubsystem::PaintZone(FVector2D Center, float RadiusCm, EChronoZoneType Zone)
{
	const int32 NumChanged = Grid.PaintZone(Center, RadiusCm, Zone);
	if (NumChanged > 0)
	{
		BroadcastChanges();
	}
	return NumChanged;
}

EChronoZoneType UChronoZoningSubsystem::GetZoneAt(FVector2D WorldXY) const
{
	const FChronoZoneCell* Cell = Grid.FindCell(Grid.FindCellAt(WorldXY));
	return Cell ? Cell->Zone : EChronoZoneType::None;
}

void UChronoZoningSubsystem::SetOverlayVisible(bool bVisible)
{
	if (OverlayActor)
	{
		OverlayActor->SetOverlayVisible(bVisible);
	}
}

void UChronoZoningSubsystem::RebuildAll()
{
	const UWorld* World = GetWorld();
	const UChronoRoadSubsystem* Roads = World ? World->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	if (!Roads)
	{
		return;
	}

	// Presenting every segment and node as changed regenerates everything
	// through the normal path, which also keeps the painted zones.
	FChronoRoadChangeSet Everything;
	Roads->GetGraph().GetSegments().GetKeys(Everything.ChangedSegments);
	Roads->GetGraph().GetNodes().GetKeys(Everything.ChangedNodes);
	HandleRoadNetworkChanged(Everything);
}

bool UChronoZoningSubsystem::SerializeZoning(FArchive& Ar)
{
	if (!Ar.IsLoading())
	{
		return Grid.Serialize(Ar);
	}

	FChronoZoneGrid Loaded;
	if (!Loaded.Serialize(Ar))
	{
		UE_LOG(LogChrono, Error, TEXT("Zoning data is invalid, keeping the current zoning"));
		return false;
	}

	// Mark what is about to disappear, so the overlay drops it.
	Grid.Reset();
	TArray<int32> Dirty = Grid.TakeDirtySegments();

	Grid = MoveTemp(Loaded);
	if (const UChronoRoadSubsystem* Roads = GetWorld() ? GetWorld()->GetSubsystem<UChronoRoadSubsystem>() : nullptr)
	{
		Grid.RemoveOrphanCells(Roads->GetGraph());
	}

	for (const int32 SegmentId : Grid.TakeDirtySegments())
	{
		Dirty.AddUnique(SegmentId);
	}
	Dirty.Sort();
	OnZoningChanged.Broadcast(Dirty);
	return true;
}

void UChronoZoningSubsystem::BroadcastChanges()
{
	const TArray<int32> Dirty = Grid.TakeDirtySegments();
	if (Dirty.Num() > 0)
	{
		OnZoningChanged.Broadcast(Dirty);
	}
}
