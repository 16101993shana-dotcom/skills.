// Project-wide tunables. Values are read from Config/DefaultGame.ini so that
// no system hardcodes them. Editable in Project Settings > Game > Chronopolis.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChronoSettings.generated.h"

class UMaterialInterface;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Chronopolis"))
class CHRONOCORE_API UChronoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UChronoSettings* Get() { return GetDefault<UChronoSettings>(); }

	// ---- Simulation clock ----

	/** Fixed simulation steps per second of simulated time at speed x1. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "120"))
	int32 SimulationTicksPerSecond = 10;

	/** Upper bound of fixed steps run in one rendered frame (prevents the spiral of death). */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxSimStepsPerFrame = 8;

	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "0.1"))
	float NormalSpeedMultiplier = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "0.1"))
	float FastSpeedMultiplier = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "0.1"))
	float FastestSpeedMultiplier = 3.0f;

	// ---- Terrain ----

	/** Number of cells per side of the square heightfield. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "16", ClampMax = "8192"))
	int32 TerrainSizeCells = 1024;

	/** Size of one cell in centimeters. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "25.0"))
	float TerrainCellSizeCm = 200.0f;

	/** Number of cells per side of one render tile. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "8", ClampMax = "255"))
	int32 TerrainTileCells = 64;

	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	float TerrainMinHeightCm = -5000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	float TerrainMaxHeightCm = 30000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	int32 TerrainNoiseSeed = 1337;

	/** Peak height of the generated relief. 0 gives a perfectly flat map. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0"))
	float TerrainNoiseAmplitudeCm = 1500.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "100.0"))
	float TerrainNoiseWavelengthCm = 120000.0f;

	/** Dirty tiles re-meshed per frame after an edit. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxTerrainTileRebuildsPerFrame = 4;

	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	bool bTerrainCollisionEnabled = true;

	/** Safety valve: flip the triangle winding if the terrain is only visible from below. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	bool bFlipTerrainWinding = false;

	/** Material applied to terrain tiles. Empty uses the engine default material. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	TSoftObjectPtr<UMaterialInterface> TerrainMaterial;
};
