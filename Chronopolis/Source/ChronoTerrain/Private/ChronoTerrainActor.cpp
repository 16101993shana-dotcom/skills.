#include "ChronoTerrainActor.h"

#include "ChronoCore.h"
#include "ChronoHeightfield.h"
#include "ChronoSettings.h"
#include "ChronoTerrainSubsystem.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "IndexTypes.h"
#include "Materials/MaterialInterface.h"

AChronoTerrainActor::AChronoTerrainActor()
{
	// Ticks only while tiles are dirty (see HandleTerrainChanged).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AChronoTerrainActor::InitializeFromSubsystem(UChronoTerrainSubsystem* InTerrain)
{
	if (!InTerrain || TileComponents.Num() > 0)
	{
		return;
	}

	Terrain = InTerrain;

	const UChronoSettings* Settings = UChronoSettings::Get();
	const FChronoHeightfield& Heightfield = InTerrain->GetHeightfield();

	TileCells = FMath::Clamp(Settings->TerrainTileCells, 8, 255);
	TilesPerSide = FMath::DivideAndRoundUp(Heightfield.GetSizeCells(), TileCells);
	MaxRebuildsPerFrame = FMath::Max(1, Settings->MaxTerrainTileRebuildsPerFrame);
	bCollisionEnabled = Settings->bTerrainCollisionEnabled;
	bFlipWinding = Settings->bFlipTerrainWinding;
	TerrainMaterial = Settings->TerrainMaterial.LoadSynchronous();

	const FVector2D Origin = Heightfield.GetOrigin();
	const double TileSizeCm = static_cast<double>(TileCells) * Heightfield.GetCellSizeCm();

	TileComponents.Reserve(TilesPerSide * TilesPerSide);
	for (int32 TileY = 0; TileY < TilesPerSide; ++TileY)
	{
		for (int32 TileX = 0; TileX < TilesPerSide; ++TileX)
		{
			UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(this);
			Component->SetupAttachment(RootComponent);

			// Vertices are stored relative to the tile corner to keep float precision.
			Component->SetRelativeLocation(FVector(Origin.X + TileX * TileSizeCm, Origin.Y + TileY * TileSizeCm, 0.0));
			Component->SetCollisionProfileName(bCollisionEnabled ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision")));
			Component->RegisterComponent();

			if (bCollisionEnabled)
			{
				Component->EnableComplexAsSimpleCollision();
			}

			if (TerrainMaterial)
			{
				Component->SetMaterial(0, TerrainMaterial);
			}

			TileComponents.Add(Component);
		}
	}

	// First build is synchronous so the world is complete on the first frame.
	for (int32 TileIndex = 0; TileIndex < TileComponents.Num(); ++TileIndex)
	{
		RebuildTile(TileIndex);
	}

	TerrainChangedHandle = InTerrain->OnTerrainChanged.AddUObject(this, &AChronoTerrainActor::HandleTerrainChanged);

	UE_LOG(LogChrono, Log, TEXT("Terrain: %d x %d tiles of %d cells"), TilesPerSide, TilesPerSide, TileCells);
}

void AChronoTerrainActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UChronoTerrainSubsystem* TerrainPtr = Terrain.Get())
	{
		TerrainPtr->OnTerrainChanged.Remove(TerrainChangedHandle);
	}
	TerrainChangedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

void AChronoTerrainActor::HandleTerrainChanged(const FIntRect& DirtyVertexRect)
{
	if (TilesPerSide <= 0)
	{
		return;
	}

	// A vertex on a tile border belongs to both tiles, and normals read one
	// neighbor on each side: grow the rectangle by two vertices before mapping
	// it to tiles.
	const int32 MinTileX = FMath::Clamp(FMath::Max(DirtyVertexRect.Min.X - 2, 0) / TileCells, 0, TilesPerSide - 1);
	const int32 MinTileY = FMath::Clamp(FMath::Max(DirtyVertexRect.Min.Y - 2, 0) / TileCells, 0, TilesPerSide - 1);
	const int32 MaxTileX = FMath::Clamp((DirtyVertexRect.Max.X + 1) / TileCells, 0, TilesPerSide - 1);
	const int32 MaxTileY = FMath::Clamp((DirtyVertexRect.Max.Y + 1) / TileCells, 0, TilesPerSide - 1);

	for (int32 TileY = MinTileY; TileY <= MaxTileY; ++TileY)
	{
		for (int32 TileX = MinTileX; TileX <= MaxTileX; ++TileX)
		{
			DirtyTiles.Add(TileY * TilesPerSide + TileX);
		}
	}

	if (DirtyTiles.Num() > 0)
	{
		SetActorTickEnabled(true);
	}
}

void AChronoTerrainActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	int32 Rebuilt = 0;
	for (auto It = DirtyTiles.CreateIterator(); It && Rebuilt < MaxRebuildsPerFrame; ++It)
	{
		RebuildTile(*It);
		It.RemoveCurrent();
		++Rebuilt;
	}

	if (DirtyTiles.Num() == 0)
	{
		SetActorTickEnabled(false);
	}
}

void AChronoTerrainActor::RebuildTile(int32 TileIndex)
{
	const UChronoTerrainSubsystem* TerrainPtr = Terrain.Get();
	if (!TerrainPtr || !TileComponents.IsValidIndex(TileIndex) || !TileComponents[TileIndex])
	{
		return;
	}

	const FChronoHeightfield& Heightfield = TerrainPtr->GetHeightfield();
	const float CellSize = Heightfield.GetCellSizeCm();

	const int32 TileX = TileIndex % TilesPerSide;
	const int32 TileY = TileIndex / TilesPerSide;
	const int32 StartX = TileX * TileCells;
	const int32 StartY = TileY * TileCells;

	// The last row and column of tiles may be smaller than TileCells.
	const int32 CellsX = FMath::Min(TileCells, Heightfield.GetSizeCells() - StartX);
	const int32 CellsY = FMath::Min(TileCells, Heightfield.GetSizeCells() - StartY);
	if (CellsX <= 0 || CellsY <= 0)
	{
		return;
	}

	const int32 VertsX = CellsX + 1;
	const int32 VertsY = CellsY + 1;

	UE::Geometry::FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();
	UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
	UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();

	// Vertices, UVs and normals are appended in the same order, so the vertex
	// id, the UV element id and the normal element id are identical.
	for (int32 Y = 0; Y < VertsY; ++Y)
	{
		for (int32 X = 0; X < VertsX; ++X)
		{
			const int32 GridX = StartX + X;
			const int32 GridY = StartY + Y;

			Mesh.AppendVertex(FVector3d(X * CellSize, Y * CellSize, Heightfield.GetVertexHeight(GridX, GridY)));

			// World-space UVs in meters: the material decides the tiling.
			const FVector2D World = Heightfield.VertexToWorld(GridX, GridY);
			UVOverlay->AppendElement(FVector2f(static_cast<float>(World.X * 0.01), static_cast<float>(World.Y * 0.01)));

			NormalOverlay->AppendElement(FVector3f(Heightfield.GetVertexNormal(GridX, GridY)));
		}
	}

	auto AddTriangle = [this, &Mesh, UVOverlay, NormalOverlay](int32 A, int32 B, int32 C)
	{
		const UE::Geometry::FIndex3i Triangle = bFlipWinding ? UE::Geometry::FIndex3i(A, C, B) : UE::Geometry::FIndex3i(A, B, C);
		const int32 TriangleId = Mesh.AppendTriangle(Triangle.A, Triangle.B, Triangle.C);
		if (TriangleId >= 0)
		{
			UVOverlay->SetTriangle(TriangleId, Triangle);
			NormalOverlay->SetTriangle(TriangleId, Triangle);
		}
	};

	// Each cell is split along the (0,0)-(1,1) diagonal: this must match
	// FChronoHeightfield::SampleHeight. The vertex order makes the faces
	// point up with the triangle orientation used by GeometryCore.
	for (int32 Y = 0; Y < CellsY; ++Y)
	{
		for (int32 X = 0; X < CellsX; ++X)
		{
			const int32 V00 = Y * VertsX + X;
			const int32 V10 = V00 + 1;
			const int32 V01 = V00 + VertsX;
			const int32 V11 = V01 + 1;

			AddTriangle(V00, V11, V10);
			AddTriangle(V00, V01, V11);
		}
	}

	UDynamicMeshComponent* Component = TileComponents[TileIndex];
	Component->SetMesh(MoveTemp(Mesh));

	if (bCollisionEnabled)
	{
		Component->UpdateCollision(false);
	}
}
