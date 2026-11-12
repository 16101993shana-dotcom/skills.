// Data types of the building system.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChronoGeometry2D.h"
#include "ChronoZoneGrid.h"
#include "ChronoBuildingTypes.generated.h"

class UMaterialInterface;
class UStaticMesh;

UENUM(BlueprintType)
enum class EChronoRoofType : uint8
{
	Flat,
	Gable
};

/**
 * One kind of building that can grow on zoned cells. Authored as data (project
 * settings for now, one data asset per civilization and era in Phase 5).
 */
USTRUCT(BlueprintType)
struct CHRONOBUILDINGS_API FChronoBuildingDefData
{
	GENERATED_BODY()

	/** Stable identifier used by save games. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	FName DefId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	EChronoZoneType Zone = EChronoZoneType::Residential;

	/** Lot size in zoning cells: along the road, then away from it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WidthCells = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot", meta = (ClampMin = "1", ClampMax = "8"))
	int32 DepthCells = 1;

	/** Free space kept between the building and each edge of its lot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot", meta = (ClampMin = "0.0"))
	float FrontSetbackCm = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot", meta = (ClampMin = "0.0"))
	float SideMarginCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot", meta = (ClampMin = "0.0"))
	float RearMarginCm = 150.0f;

	/** Number of floors at the lowest and at the highest land value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "1"))
	int32 MinFloors = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "1"))
	int32 MaxFloors = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "200.0"))
	float FloorHeightCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	EChronoRoofType Roof = EChronoRoofType::Flat;

	/** Height of the ridge above the walls (gable) or of the roof edge above the top floor (flat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0"))
	float RoofHeightCm = 200.0f;

	/** The building only appears where the land value reaches this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth", meta = (ClampMin = "0.0"))
	float MinLandValue = 0.0f;

	/** Relative chance of being picked among the definitions that fit a lot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/**
	 * Finished model of the building. When set, it is drawn instead of the
	 * generated greybox volume. Its pivot must be at the center of the
	 * footprint, at floor level, with +X along the road.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

/** One building standing in the city. Plain data. */
struct FChronoBuilding
{
	int32 Id = INDEX_NONE;
	FName DefId = NAME_None;
	EChronoZoneType Zone = EChronoZoneType::None;

	// Lot: cells [FirstSlot, FirstSlot + WidthCells) x [0, DepthCells) on one side of a segment.
	int32 SegmentId = INDEX_NONE;
	int32 Side = 1;
	int32 FirstSlot = 0;
	int32 WidthCells = 1;
	int32 DepthCells = 1;

	/** Outline of the walls on the ground. */
	FChronoOrientedBox2D Footprint;

	/** Height of the ground floor: just above the highest ground under the building. */
	double FloorHeightCm = 0.0;

	/** Bottom of the foundation: below the lowest ground under the building. */
	double FoundationBottomCm = 0.0;

	/** Height of the walls above the floor. */
	float BodyHeightCm = 300.0f;

	EChronoRoofType Roof = EChronoRoofType::Flat;
	float RoofHeightCm = 0.0f;

	/** Land value when the building appeared. */
	float LandValue = 0.0f;
};

/** Tunables of building placement and land value. In game they come from UChronoBuildingSettings. */
struct CHRONOBUILDINGS_API FChronoBuildingRules
{
	float RoadClearanceCm = 20.0f;
	float OverlapToleranceCm = 1.0f;
	float FloorLiftCm = 5.0f;
	float FoundationDepthCm = 50.0f;
	float MaxLotYawDeltaDegrees = 2.0f;
	float RoadSampleSpacingCm = 200.0f;

	float BaseLandValue = 20.0f;
	float FrontRowLandValue = 20.0f;
	float NeighborLandValue = 2.0f;
	float NeighborLandValueCap = 40.0f;
	float IndustryLandValuePenalty = 6.0f;
	float NeighborRadiusCm = 8000.0f;
	float MaxLandValue = 100.0f;

	static FChronoBuildingRules FromSettings();
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Chronopolis Buildings"))
class CHRONOBUILDINGS_API UChronoBuildingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UChronoBuildingSettings* Get() { return GetDefault<UChronoBuildingSettings>(); }

	/** Every kind of building that can grow. */
	UPROPERTY(Config, EditAnywhere, Category = "Definitions")
	TArray<FChronoBuildingDefData> Definitions;

	// ---- Growth ----

	/** Simulation ticks between two growth steps. */
	UPROPERTY(Config, EditAnywhere, Category = "Growth", meta = (ClampMin = "1"))
	int32 GrowthIntervalTicks = 5;

	/** Buildings that may appear in one growth step. */
	UPROPERTY(Config, EditAnywhere, Category = "Growth", meta = (ClampMin = "0", ClampMax = "64"))
	int32 GrowthPerStep = 2;

	/** Seed of the growth random stream: same seed, same city. */
	UPROPERTY(Config, EditAnywhere, Category = "Growth")
	int32 GrowthSeed = 4242;

	// ---- Placement ----

	/** A building is refused if any road comes closer than this to its walls. */
	UPROPERTY(Config, EditAnywhere, Category = "Placement", meta = (ClampMin = "0.0"))
	float RoadClearanceCm = 20.0f;

	/** The ground floor sits this far above the highest ground under the building. */
	UPROPERTY(Config, EditAnywhere, Category = "Placement", meta = (ClampMin = "0.0"))
	float FloorLiftCm = 5.0f;

	/** The foundation goes this far below the lowest ground under the building. */
	UPROPERTY(Config, EditAnywhere, Category = "Placement", meta = (ClampMin = "10.0"))
	float FoundationDepthCm = 50.0f;

	/** Cells of a lot wider than one cell must be aligned within this angle (straight road). */
	UPROPERTY(Config, EditAnywhere, Category = "Placement", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float MaxLotYawDeltaDegrees = 2.0f;

	// ---- Land value (0 to MaxLandValue) ----

	UPROPERTY(Config, EditAnywhere, Category = "Land Value", meta = (ClampMin = "0.0"))
	float BaseLandValue = 20.0f;

	/** Bonus of a lot that fronts directly on a road. */
	UPROPERTY(Config, EditAnywhere, Category = "Land Value", meta = (ClampMin = "0.0"))
	float FrontRowLandValue = 20.0f;

	/** Bonus per building within NeighborRadiusCm, up to NeighborLandValueCap. */
	UPROPERTY(Config, EditAnywhere, Category = "Land Value", meta = (ClampMin = "0.0"))
	float NeighborLandValue = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Land Value", meta = (ClampMin = "0.0"))
	float NeighborLandValueCap = 40.0f;

	/** Penalty per industrial building within NeighborRadiusCm. */
	UPROPERTY(Config, EditAnywhere, Category = "Land Value", meta = (ClampMin = "0.0"))
	float IndustryLandValuePenalty = 6.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Land Value", meta = (ClampMin = "100.0"))
	float NeighborRadiusCm = 8000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Land Value", meta = (ClampMin = "1.0"))
	float MaxLandValue = 100.0f;

	// ---- Rendering ----

	/** Greybox materials: slot 0 walls, slot 1 roof, slot 2 foundation. Empty uses the engine default material. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	TSoftObjectPtr<UMaterialInterface> WallMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	TSoftObjectPtr<UMaterialInterface> RoofMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	TSoftObjectPtr<UMaterialInterface> FoundationMaterial;

	/** Building meshes rebuilt per frame after a change. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxBuildingMeshRebuildsPerFrame = 8;
};
