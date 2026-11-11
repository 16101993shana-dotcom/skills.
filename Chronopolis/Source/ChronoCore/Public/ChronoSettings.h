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

	/** Safety valve: flip the triangle winding of every generated mesh (terrain, roads) if they are only visible from below. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	bool bFlipTerrainWinding = false;

	/** Material applied to terrain tiles. Empty uses the engine default material. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	TSoftObjectPtr<UMaterialInterface> TerrainMaterial;

	// ---- Roads ----

	/** Two roads may not meet at a node with a smaller angle than this. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "10.0", ClampMax = "90.0"))
	float RoadMinIntersectionAngleDegrees = 30.0f;

	/** Maximum number of roads meeting at one node. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "3", ClampMax = "8"))
	int32 RoadMaxNodeDegree = 6;

	/** Minimum length of a segment between its two intersections. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "100.0"))
	float RoadMinUsableLengthCm = 400.0f;

	/** Smallest distance by which a segment is cut back at a node shared with another segment. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "1.0"))
	float RoadMinTrimCm = 100.0f;

	/** A road end closer than this to an existing node or road snaps onto it. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "0.0"))
	float RoadSnapDistanceCm = 800.0f;

	/**
	 * Minimum free space between the edges of two roads that are not connected.
	 * Keep it above three terrain cells: closer roads at different heights
	 * would need a ground step that the terrain grid cannot represent.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "0.0"))
	float RoadClearanceCm = 600.0f;

	/**
	 * A road stays level over this distance after leaving a junction, so that
	 * neighboring roads are apart before their heights start to differ.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "0.0"))
	float RoadJunctionLevelRunCm = 600.0f;

	/** Distance between two cross-sections of a road mesh. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "25.0"))
	float RoadSampleSpacingCm = 200.0f;

	/**
	 * The ground is set this far below the road surface, so the two never
	 * z-fight. Where the grade of a road changes, the terrain grid can sit up to
	 * (grade x cell size / 2) above the ideal roadbed: keep this offset larger
	 * than that (15 cm for a 15 % grade on 2 m cells).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "1.0"))
	float RoadbedOffsetCm = 20.0f;

	/** Depth of the side faces of a road mesh. Must be larger than RoadbedOffsetCm. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "5.0"))
	float RoadSkirtDepthCm = 60.0f;

	/** Width over which the ground blends from the roadbed back to the natural relief. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "0.0"))
	float RoadTerrainBlendCm = 800.0f;

	/** Road meshes rebuilt per frame after an edit. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxRoadMeshRebuildsPerFrame = 8;

	/** Material slot 0: carriageway. Empty uses the engine default material. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads")
	TSoftObjectPtr<UMaterialInterface> RoadSurfaceMaterial;

	/** Material slot 1: sidewalks and curbs. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads")
	TSoftObjectPtr<UMaterialInterface> RoadSidewalkMaterial;

	/** Material slot 2: side faces and end caps. */
	UPROPERTY(Config, EditAnywhere, Category = "Roads")
	TSoftObjectPtr<UMaterialInterface> RoadSideMaterial;
};
