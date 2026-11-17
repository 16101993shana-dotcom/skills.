#include "ChronoZoneOverlayActor.h"

#include "ChronoHeightfield.h"
#include "ChronoMeshConversion.h"
#include "ChronoSettings.h"
#include "ChronoTerrainSubsystem.h"
#include "ChronoZoneGrid.h"
#include "ChronoZoningSubsystem.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

void ChronoZoneOverlay::BuildSegmentMesh(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, int32 SegmentId, float LiftCm, const FVector& Origin, FChronoMeshData& OutMesh)
{
	OutMesh.Reset();

	// Each tile is cut into a small grid so that it follows the relief
	// instead of slicing through it.
	const int32 Subdivisions = 4;
	// Tiles are drawn slightly smaller than their cell, which leaves a
	// visible joint between neighbors.
	const double InsetRatio = 0.94;

	const double Half = 0.5 * Grid.GetCellSizeCm() * InsetRatio;

	for (const int32 CellId : Grid.GetSegmentCells(SegmentId))
	{
		const FChronoZoneCell* Cell = Grid.FindCell(CellId);
		if (!Cell)
		{
			continue;
		}

		const FVector2D AxisX(FMath::Cos(Cell->YawRadians), FMath::Sin(Cell->YawRadians));
		const FVector2D AxisY(-AxisX.Y, AxisX.X);
		const int32 MaterialId = static_cast<int32>(Cell->Zone);

		const int32 FirstVertex = OutMesh.Positions.Num();
		for (int32 Y = 0; Y <= Subdivisions; ++Y)
		{
			for (int32 X = 0; X <= Subdivisions; ++X)
			{
				const double U = static_cast<double>(X) / Subdivisions;
				const double V = static_cast<double>(Y) / Subdivisions;
				const FVector2D Point = Cell->Center + AxisX * ((2.0 * U - 1.0) * Half) + AxisY * ((2.0 * V - 1.0) * Half);
				const double Height = Heightfield.SampleHeight(Point) + LiftCm;
				OutMesh.AddVertex(FVector(Point.X, Point.Y, Height) - Origin, FVector::UpVector, FVector2D(U, V));
			}
		}

		const int32 Stride = Subdivisions + 1;
		for (int32 Y = 0; Y < Subdivisions; ++Y)
		{
			for (int32 X = 0; X < Subdivisions; ++X)
			{
				const int32 V00 = FirstVertex + Y * Stride + X;
				const int32 V10 = V00 + 1;
				const int32 V01 = V00 + Stride;
				const int32 V11 = V01 + 1;
				OutMesh.AddTriangleFacing(V00, V10, V11, FVector::UpVector, MaterialId);
				OutMesh.AddTriangleFacing(V00, V11, V01, FVector::UpVector, MaterialId);
			}
		}
	}
}

AChronoZoneOverlayActor::AChronoZoneOverlayActor()
{
	// Ticks only while the overlay is visible and tiles are waiting for a rebuild.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AChronoZoneOverlayActor::InitializeFromSubsystem(UChronoZoningSubsystem* InZoning)
{
	if (!InZoning || Zoning.IsValid())
	{
		return;
	}

	Zoning = InZoning;

	const UChronoSettings* Settings = UChronoSettings::Get();
	MaxRebuildsPerFrame = FMath::Max(1, Settings->MaxZoneMeshRebuildsPerFrame);
	LiftCm = Settings->ZoneOverlayLiftCm;
	bFlipWinding = Settings->bFlipTerrainWinding;

	Materials.SetNum(static_cast<int32>(EChronoZoneType::Count));
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		Materials[Index] = Settings->ZoneOverlayMaterials.IsValidIndex(Index) ? Settings->ZoneOverlayMaterials[Index].LoadSynchronous() : nullptr;
	}

	ChangedHandle = InZoning->OnZoningChanged.AddUObject(this, &AChronoZoneOverlayActor::HandleZoningChanged);

	// Hidden until the zoning tool asks for it.
	SetActorHiddenInGame(true);
}

void AChronoZoneOverlayActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UChronoZoningSubsystem* ZoningPtr = Zoning.Get())
	{
		ZoningPtr->OnZoningChanged.Remove(ChangedHandle);
	}
	ChangedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

void AChronoZoneOverlayActor::SetOverlayVisible(bool bVisible)
{
	bOverlayVisible = bVisible;
	SetActorHiddenInGame(!bVisible);

	// Tiles are only rebuilt while someone can see them.
	SetActorTickEnabled(bVisible && DirtySegments.Num() > 0);
}

void AChronoZoneOverlayActor::HandleZoningChanged(const TArray<int32>& SegmentIds)
{
	DirtySegments.Append(SegmentIds);
	SetActorTickEnabled(bOverlayVisible && DirtySegments.Num() > 0);
}

void AChronoZoneOverlayActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	int32 Rebuilt = 0;
	for (auto It = DirtySegments.CreateIterator(); It && Rebuilt < MaxRebuildsPerFrame; ++It)
	{
		RebuildSegment(*It);
		It.RemoveCurrent();
		++Rebuilt;
	}

	if (DirtySegments.Num() == 0)
	{
		SetActorTickEnabled(false);
	}
}

void AChronoZoneOverlayActor::RebuildSegment(int32 SegmentId)
{
	const UChronoZoningSubsystem* ZoningPtr = Zoning.Get();
	const UChronoTerrainSubsystem* Terrain = GetWorld() ? GetWorld()->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	if (!ZoningPtr || !Terrain)
	{
		return;
	}

	const FChronoZoneGrid& Grid = ZoningPtr->GetGrid();
	const TConstArrayView<int32> CellIds = Grid.GetSegmentCells(SegmentId);
	const FChronoZoneCell* FirstCell = CellIds.Num() > 0 ? Grid.FindCell(CellIds[0]) : nullptr;

	FChronoMeshData MeshData;
	FVector Origin = FVector::ZeroVector;
	if (FirstCell)
	{
		// Vertices are stored relative to the first cell to keep float precision.
		Origin = FVector(FirstCell->Center.X, FirstCell->Center.Y, 0.0);
		ChronoZoneOverlay::BuildSegmentMesh(Grid, Terrain->GetHeightfield(), SegmentId, LiftCm, Origin, MeshData);
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
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
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
