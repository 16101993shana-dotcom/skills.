// Owns the heightfield of the current world and exposes the edit API.
// Rendering (AChronoTerrainActor) listens to OnTerrainChanged; nothing here
// depends on rendering.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoHeightfield.h"
#include "ChronoTerrainSubsystem.generated.h"

class AChronoTerrainActor;

/** Modified vertex rectangle: Min inclusive, Max exclusive. */
DECLARE_MULTICAST_DELEGATE_OneParam(FChronoOnTerrainChanged, const FIntRect&);

UCLASS()
class CHRONOTERRAIN_API UChronoTerrainSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	const FChronoHeightfield& GetHeightfield() const { return Heightfield; }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Terrain")
	float GetHeightAt(FVector2D WorldXY) const;

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Terrain")
	float GetSlopeDegreesAt(FVector2D WorldXY) const;

	/** Half of the world side length, in centimeters. The playable area is [-Half, +Half] on X and Y. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Terrain")
	float GetWorldHalfSizeCm() const;

	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Terrain")
	void RaiseTerrain(FVector2D Center, float RadiusCm, float DeltaCm);

	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Terrain")
	void FlattenTerrain(FVector2D Center, float RadiusCm, float TargetHeightCm, float Strength);

	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Terrain")
	void SmoothTerrain(FVector2D Center, float RadiusCm, float Strength);

	/** Exact flattening under an oriented rectangle (building footprint, road piece). */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Terrain")
	void FlattenFootprint(FVector2D Center, FVector2D HalfExtentCm, float YawDegrees, float TargetHeightCm, float SkirtCm);

	/**
	 * Intersects a ray with the heightfield (not with collision geometry), so
	 * cursor picking stays exact even while tiles are being re-meshed.
	 * @return true if the ray hits the ground inside the world bounds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Terrain")
	bool RaycastTerrain(FVector Origin, FVector Direction, float MaxDistanceCm, FVector& OutHit) const;

	/** Regenerates the initial relief from the project settings. */
	void GenerateInitialTerrain(int32 Seed);

	/** Save and load. After loading, the whole terrain is marked dirty. Returns false on invalid data. */
	bool SerializeTerrain(FArchive& Ar);

	FChronoOnTerrainChanged OnTerrainChanged;

private:
	void NotifyChanged(const FIntRect& DirtyVertexRect);
	FIntRect GetFullRect() const;

	FChronoHeightfield Heightfield;

	float MinHeightCm = 0.0f;
	float MaxHeightCm = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<AChronoTerrainActor> TerrainActor;
};
