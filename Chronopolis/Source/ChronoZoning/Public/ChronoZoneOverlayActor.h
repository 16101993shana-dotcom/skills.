// Draws the zoning cells as colored tiles laid on the ground, one mesh per
// road segment. Hidden unless the zoning tool is active.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChronoMeshData.h"
#include "ChronoZoneOverlayActor.generated.h"

class FChronoZoneGrid;
class UChronoZoningSubsystem;
class UDynamicMeshComponent;
class UMaterialInterface;
struct FChronoHeightfield;

namespace ChronoZoneOverlay
{
	/**
	 * Tiles for the cells of one segment, draped on the terrain. Positions are
	 * relative to Origin. The material slot of a tile is its zone type.
	 */
	CHRONOZONING_API void BuildSegmentMesh(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, int32 SegmentId, float LiftCm, const FVector& Origin, FChronoMeshData& OutMesh);
}

UCLASS(NotPlaceable)
class CHRONOZONING_API AChronoZoneOverlayActor : public AActor
{
	GENERATED_BODY()

public:
	AChronoZoneOverlayActor();

	void InitializeFromSubsystem(UChronoZoningSubsystem* InZoning);
	void SetOverlayVisible(bool bVisible);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleZoningChanged(const TArray<int32>& SegmentIds);
	void RebuildSegment(int32 SegmentId);

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDynamicMeshComponent>> SegmentComponents;

	/** Indexed by EChronoZoneType. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	TWeakObjectPtr<UChronoZoningSubsystem> Zoning;
	FDelegateHandle ChangedHandle;

	/** Segments waiting for a rebuild. The actor only ticks while visible and not empty. */
	TSet<int32> DirtySegments;

	int32 MaxRebuildsPerFrame = 8;
	float LiftCm = 12.0f;
	bool bFlipWinding = false;
	bool bOverlayVisible = false;
};
