// Render side of the terrain: one dynamic mesh component per tile, re-meshed
// only when the heightfield under it changes. Spawned by UChronoTerrainSubsystem.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChronoTerrainActor.generated.h"

class UChronoTerrainSubsystem;
class UDynamicMeshComponent;
class UMaterialInterface;

UCLASS(NotPlaceable)
class CHRONOTERRAIN_API AChronoTerrainActor : public AActor
{
	GENERATED_BODY()

public:
	AChronoTerrainActor();

	/** Creates the tile components and builds every tile once. */
	void InitializeFromSubsystem(UChronoTerrainSubsystem* InTerrain);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	int32 GetNumDirtyTiles() const { return DirtyTiles.Num(); }

private:
	void HandleTerrainChanged(const FIntRect& DirtyVertexRect);
	void RebuildTile(int32 TileIndex);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> TileComponents;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	TWeakObjectPtr<UChronoTerrainSubsystem> Terrain;
	FDelegateHandle TerrainChangedHandle;

	/** Tiles waiting for a re-mesh. The actor only ticks while this set is not empty. */
	TSet<int32> DirtyTiles;

	int32 TilesPerSide = 0;
	int32 TileCells = 64;
	int32 MaxRebuildsPerFrame = 4;
	bool bCollisionEnabled = true;
	bool bFlipWinding = false;
};
