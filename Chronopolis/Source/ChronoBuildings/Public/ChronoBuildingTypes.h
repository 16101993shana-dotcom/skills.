// Data types of the building system.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChronoGeometry2D.h"
#include "ChronoZoneGrid.h"
#include "ChronoBuildingTypes.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * Public services, placed by the player. Each one serves the buildings within
 * its radius: they are happier and worth more.
 */
UENUM(BlueprintType)
enum class EChronoServiceType : uint8
{
	None,
	Water,
	Health,
	Security,
	Fire,
	Education,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EChronoRoofType : uint8
{
	Flat,
	Gable
};

/**
 * One kind of building that can grow on zoned cells, or that the player
 * places. Authored as data in the project settings. Which ones a city can use
 * depends on its civilization, its era and what it has researched (see
 * FChronoBuildingAvailability).
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

	// ---- Availability ----

	/** Civilization that builds this, or None for every civilization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Availability")
	FName Civilization = NAME_None;

	/**
	 * Building of every civilization that this one takes the place of, for
	 * its own civilization (for example a civilization's own school). While
	 * this one is available, the replaced one is not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Availability")
	FName Replaces = NAME_None;

	/** First and last era (0 for the first) in which it is built. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Availability", meta = (ClampMin = "0"))
	int32 MinEra = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Availability", meta = (ClampMin = "0"))
	int32 MaxEra = 99;

	/** Technology or civic that unlocks it, or None if it is known from the start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Availability")
	FName RequiredUnlock = NAME_None;

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
	 * Service provided. None for the buildings that grow by themselves on
	 * zoned land; any other value makes this a building the player places.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service")
	EChronoServiceType Service = EChronoServiceType::None;

	/** Buildings within this distance are served. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service", meta = (ClampMin = "0.0"))
	float ServiceRadiusCm = 15000.0f;

	/** Price paid when the player places the building. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service", meta = (ClampMin = "0.0"))
	float ConstructionCost = 0.0f;

	/** Paid every day for as long as the building stands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service", meta = (ClampMin = "0.0"))
	float UpkeepPerDay = 0.0f;

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

	/** Number of storeys. Housing and job capacities are derived from it. */
	int32 Floors = 1;

	/** Height of the walls above the floor. */
	float BodyHeightCm = 300.0f;

	EChronoRoofType Roof = EChronoRoofType::Flat;
	float RoofHeightCm = 0.0f;

	/** Land value when the building appeared or was last upgraded. */
	float LandValue = 0.0f;

	/** 1 when built. A grown building gains a level, and a storey, when its land value rises enough. */
	int32 Level = 1;

	/** True for a building placed by the player (a service), false for one that grew on zoned land. */
	bool bPlayerPlaced = false;

	EChronoServiceType Service = EChronoServiceType::None;
	float ServiceRadiusCm = 0.0f;
	float UpkeepPerDay = 0.0f;

	/** Era in which it was built (0 for the first), and by which civilization: they give its style. */
	int32 Era = 0;
	FName Civilization = NAME_None;

	/**
	 * Listed as heritage by the player: kept as it is when the city moves on
	 * to a new era, instead of being rebuilt in the style of that era.
	 */
	bool bHeritage = false;
};

/**
 * What a city can build: its civilization, its era and what it has unlocked.
 * By default nothing is restricted, which is how the building tests and a
 * world without the civilization system run.
 */
struct CHRONOBUILDINGS_API FChronoBuildingAvailability
{
	bool bRestricted = false;
	FName Civilization = NAME_None;
	int32 Era = 0;
	TSet<FName> Unlocked;

	/** True if the definition can be built now, for its own sake (not looking at replacements). */
	bool AllowsOnItsOwn(const FChronoBuildingDefData& Definition) const;

	/** True if the definition can be built now and no available building of this civilization replaces it. */
	bool Allows(const FChronoBuildingDefData& Definition, TConstArrayView<FChronoBuildingDefData> AllDefinitions) const;

	/** The definitions that can be built now, in their original order. */
	void Filter(TConstArrayView<FChronoBuildingDefData> AllDefinitions, TArray<FChronoBuildingDefData>& OutDefinitions) const;

	/**
	 * Definition to build when the player asks for RequestedId: that one, or
	 * the building of this civilization that replaces it. Null if neither is
	 * available now.
	 */
	const FChronoBuildingDefData* Resolve(FName RequestedId, TConstArrayView<FChronoBuildingDefData> AllDefinitions) const;
};

/** Why a building could not be placed by the player. */
UENUM(BlueprintType)
enum class EChronoPlaceResult : uint8
{
	Success,
	UnknownBuilding,
	NoLot,
	DoesNotFit,
	NotEnoughMoney,
	/** Not known yet: a technology or civic unlocks it, or it belongs to another era or civilization. */
	NotUnlocked
};

/**
 * How much the city wants more of each zone, from -1 (far too much of it) to
 * +1 (badly needed). Buildings only grow on zones whose demand is positive,
 * the more often the higher it is. Indexed by EChronoZoneType.
 */
struct CHRONOBUILDINGS_API FChronoZoneDemand
{
	float Value[static_cast<int32>(EChronoZoneType::Count)] = {};

	float Get(EChronoZoneType Zone) const { return Value[static_cast<int32>(Zone)]; }
	void Set(EChronoZoneType Zone, float InValue) { Value[static_cast<int32>(Zone)] = FMath::Clamp(InValue, -1.0f, 1.0f); }
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

	/** Land value added by each public service that covers a place. */
	float ServiceLandValue = 5.0f;

	// Levels: a grown building gains a storey when its land value reaches the
	// threshold of the next level.
	int32 MaxLevel = 3;
	float Level2LandValue = 55.0f;
	float Level3LandValue = 75.0f;

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

	/** Land value added by each public service that covers a place. */
	UPROPERTY(Config, EditAnywhere, Category = "Land Value", meta = (ClampMin = "0.0"))
	float ServiceLandValue = 5.0f;

	// ---- Levels ----

	/** Highest level a grown building can reach. Each level adds a storey. */
	UPROPERTY(Config, EditAnywhere, Category = "Levels", meta = (ClampMin = "1", ClampMax = "3"))
	int32 MaxLevel = 3;

	UPROPERTY(Config, EditAnywhere, Category = "Levels", meta = (ClampMin = "0.0"))
	float Level2LandValue = 55.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Levels", meta = (ClampMin = "0.0"))
	float Level3LandValue = 75.0f;

	/** Buildings that may gain a level in one growth step. */
	UPROPERTY(Config, EditAnywhere, Category = "Levels", meta = (ClampMin = "0", ClampMax = "64"))
	int32 UpgradesPerStep = 1;

	// ---- Eras ----

	/**
	 * Buildings of an earlier era that may be rebuilt in the style of the
	 * current one, in one growth step. Heritage buildings never are.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Eras", meta = (ClampMin = "0", ClampMax = "64"))
	int32 RenewalsPerStep = 1;

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
