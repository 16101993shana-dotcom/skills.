#include "ChronoBuildingRenderActor.h"

#include "ChronoBuildingMeshBuilder.h"
#include "ChronoBuildingSubsystem.h"
#include "ChronoMeshConversion.h"
#include "ChronoMeshData.h"
#include "ChronoSettings.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

AChronoBuildingRenderActor::AChronoBuildingRenderActor()
{
	// Ticks only while meshes are waiting for a rebuild.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AChronoBuildingRenderActor::InitializeFromSubsystem(UChronoBuildingSubsystem* InBuildings)
{
	if (!InBuildings || Buildings.IsValid())
	{
		return;
	}

	Buildings = InBuildings;

	const UChronoBuildingSettings* Settings = UChronoBuildingSettings::Get();
	MaxRebuildsPerFrame = FMath::Max(1, Settings->MaxBuildingMeshRebuildsPerFrame);
	bFlipWinding = UChronoSettings::Get()->bFlipTerrainWinding;

	Materials.SetNum(ChronoBuildingMaterialSlot::Count);
	Materials[ChronoBuildingMaterialSlot::Wall] = Settings->WallMaterial.LoadSynchronous();
	Materials[ChronoBuildingMaterialSlot::Roof] = Settings->RoofMaterial.LoadSynchronous();
	Materials[ChronoBuildingMaterialSlot::Foundation] = Settings->FoundationMaterial.LoadSynchronous();

	ChangedHandle = InBuildings->OnBuildingsChanged.AddUObject(this, &AChronoBuildingRenderActor::HandleBuildingsChanged);
}

void AChronoBuildingRenderActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UChronoBuildingSubsystem* BuildingsPtr = Buildings.Get())
	{
		BuildingsPtr->OnBuildingsChanged.Remove(ChangedHandle);
	}
	ChangedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

void AChronoBuildingRenderActor::HandleBuildingsChanged(const TArray<int32>& SegmentIds)
{
	DirtySegments.Append(SegmentIds);
	bModelInstancesDirty = true;
	SetActorTickEnabled(true);
}

void AChronoBuildingRenderActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	int32 Rebuilt = 0;
	for (auto It = DirtySegments.CreateIterator(); It && Rebuilt < MaxRebuildsPerFrame; ++It)
	{
		RebuildSegment(*It);
		It.RemoveCurrent();
		++Rebuilt;
	}

	if (bModelInstancesDirty)
	{
		RebuildModelInstances();
		bModelInstancesDirty = false;
	}

	if (DirtySegments.Num() == 0)
	{
		SetActorTickEnabled(false);
	}
}

UStaticMesh* AChronoBuildingRenderActor::FindModel(FName DefId)
{
	if (const TObjectPtr<UStaticMesh>* Known = ModelsByDefId.Find(DefId))
	{
		return *Known;
	}

	UStaticMesh* Model = nullptr;
	if (const UChronoBuildingSubsystem* BuildingsPtr = Buildings.Get())
	{
		if (const FChronoBuildingDefData* Definition = BuildingsPtr->FindDefinition(DefId))
		{
			Model = Definition->Mesh.LoadSynchronous();
		}
	}

	ModelsByDefId.Add(DefId, Model);
	return Model;
}

void AChronoBuildingRenderActor::RebuildSegment(int32 SegmentId)
{
	const UChronoBuildingSubsystem* BuildingsPtr = Buildings.Get();
	if (!BuildingsPtr)
	{
		return;
	}

	const FChronoBuildingSet& Set = BuildingsPtr->GetBuildingSet();

	FChronoMeshData MeshData;
	FVector Origin = FVector::ZeroVector;
	bool bHasOrigin = false;
	for (const int32 BuildingId : Set.GetSegmentBuildings(SegmentId))
	{
		const FChronoBuilding* Building = Set.FindBuilding(BuildingId);
		if (!Building)
		{
			continue;
		}

		if (!bHasOrigin)
		{
			// Vertices are stored relative to the first building to keep float precision.
			Origin = FVector(Building->Footprint.Center.X, Building->Footprint.Center.Y, Building->FloorHeightCm);
			bHasOrigin = true;
		}

		if (FindModel(Building->DefId))
		{
			// The finished model is drawn as an instance; only its foundation is generated.
			ChronoBuildingMeshBuilder::AppendFoundation(*Building, Origin, MeshData);
		}
		else
		{
			ChronoBuildingMeshBuilder::AppendBuilding(*Building, Origin, MeshData);
		}
	}

	if (MeshData.IsEmpty())
	{
		TObjectPtr<UDynamicMeshComponent> Removed;
		if (SegmentComponents.RemoveAndCopyValue(SegmentId, Removed) && Removed)
		{
			Removed->DestroyComponent();
		}
		return;
	}

	UDynamicMeshComponent* Component = SegmentComponents.FindRef(SegmentId);
	if (!Component)
	{
		Component = NewObject<UDynamicMeshComponent>(this);
		Component->SetupAttachment(RootComponent);
		// Buildings are picked through the building set, not through collision.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->RegisterComponent();

		for (int32 Slot = 0; Slot < Materials.Num(); ++Slot)
		{
			if (Materials[Slot])
			{
				Component->SetMaterial(Slot, Materials[Slot]);
			}
		}

		SegmentComponents.Add(SegmentId, Component);
	}

	Component->SetWorldLocation(Origin);
	ChronoMeshConversion::ApplyToComponent(MeshData, bFlipWinding, *Component);
}

void AChronoBuildingRenderActor::RebuildModelInstances()
{
	const UChronoBuildingSubsystem* BuildingsPtr = Buildings.Get();
	if (!BuildingsPtr)
	{
		return;
	}

	for (const TPair<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : ModelComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();
		}
	}

	// In id order, so instance indices are the same from one rebuild to the next.
	TArray<int32> BuildingIds;
	BuildingsPtr->GetBuildingSet().GetBuildings().GetKeys(BuildingIds);
	BuildingIds.Sort();

	for (const int32 BuildingId : BuildingIds)
	{
		const FChronoBuilding& Building = BuildingsPtr->GetBuildingSet().GetBuildings().FindChecked(BuildingId);
		UStaticMesh* Model = FindModel(Building.DefId);
		if (!Model)
		{
			continue;
		}

		UInstancedStaticMeshComponent* Component = ModelComponents.FindRef(Model);
		if (!Component)
		{
			Component = NewObject<UInstancedStaticMeshComponent>(this);
			Component->SetupAttachment(RootComponent);
			Component->SetStaticMesh(Model);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->RegisterComponent();
			ModelComponents.Add(Model, Component);
		}

		// The model pivot is at the center of the footprint, at floor level,
		// with +X along the road.
		const FRotator Rotation(0.0, FMath::RadiansToDegrees(Building.Footprint.YawRadians), 0.0);
		const FVector Location(Building.Footprint.Center.X, Building.Footprint.Center.Y, Building.FloorHeightCm);
		Component->AddInstance(FTransform(Rotation, Location), true);
	}
}
