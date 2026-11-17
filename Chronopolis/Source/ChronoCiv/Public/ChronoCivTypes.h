// Data of the civilization layer: eras, technologies and civics, and the
// civilizations a city can belong to. All of it is authored in the project
// settings (Config/DefaultGame.ini); no value is written in the code.
//
// A city keeps its civilization for the whole game: a Sumerian city stays
// Sumerian through all seven eras. Era names are the same for everybody and
// are shown next to the civilization's name ("Sumer, Middle Ages").

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChronoCivTypes.generated.h"

/** One era. The first in the list is era 0, where every city starts. */
USTRUCT(BlueprintType)
struct CHRONOCIV_API FChronoEraDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Era")
	FName Id = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Era")
	FText DisplayName;

	/** Inhabitants needed to enter this era. Ignored for the first one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Era", meta = (ClampMin = "0"))
	int32 MinPopulation = 0;

	/** Technologies, then civics, of this era that must be known to leave it for the next one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Era", meta = (ClampMin = "0"))
	int32 TechsToAdvance = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Era", meta = (ClampMin = "0"))
	int32 CivicsToAdvance = 0;
};

/**
 * One technology (paid with science) or civic (paid with culture). Both
 * trees are in one list; bCivic tells them apart, and a prerequisite may come
 * from either tree.
 */
USTRUCT(BlueprintType)
struct CHRONOCIV_API FChronoTechDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech")
	FName Id = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech")
	FText DisplayName;

	/** False: a technology, researched with science. True: a civic, adopted with culture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech")
	bool bCivic = false;

	/** Era it belongs to (0 for the first). It can only be studied once the city has reached that era. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech", meta = (ClampMin = "0"))
	int32 Era = 0;

	/** Science or culture needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech", meta = (ClampMin = "1.0"))
	float Cost = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech")
	TArray<FName> Prerequisites;

	/**
	 * Names unlocked when it is known. A building lists the name it needs in
	 * its RequiredUnlock; every technology also unlocks its own Id.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech")
	TArray<FName> Unlocks;

	// ---- Boost: a head start earned by what the city does (0 or None: no boost) ----

	/** Boosted when the city reaches this many inhabitants. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boost", meta = (ClampMin = "0"))
	int32 BoostPopulation = 0;

	/** Boosted when the city has BoostBuildingCount buildings of this kind (or of a building that replaces it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boost")
	FName BoostBuilding = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boost", meta = (ClampMin = "1"))
	int32 BoostBuildingCount = 1;
};

/** A civilization the player can found a city of. */
USTRUCT(BlueprintType)
struct CHRONOCIV_API FChronoCivilizationDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilization")
	FName Id = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilization")
	FText DisplayName;

	/** One line for the selection screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Civilization")
	FText Summary;

	// ---- Bonuses (1 = none) ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bonuses", meta = (ClampMin = "0.0"))
	float ScienceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bonuses", meta = (ClampMin = "0.0"))
	float CultureMultiplier = 1.0f;

	/** Price of the buildings the player places. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bonuses", meta = (ClampMin = "0.0"))
	float ConstructionCostMultiplier = 1.0f;

	/** Reach of the water services the player places. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bonuses", meta = (ClampMin = "0.0"))
	float WaterRadiusMultiplier = 1.0f;

	/** Names known from the start, on top of everything that needs no unlock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bonuses")
	TArray<FName> StartingUnlocks;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Chronopolis Civilizations"))
class CHRONOCIV_API UChronoCivSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UChronoCivSettings* Get() { return GetDefault<UChronoCivSettings>(); }

	/** In order: the first is where every city starts. */
	UPROPERTY(Config, EditAnywhere, Category = "Eras")
	TArray<FChronoEraDef> Eras;

	/** Technologies and civics, both trees in one list. */
	UPROPERTY(Config, EditAnywhere, Category = "Trees")
	TArray<FChronoTechDef> Techs;

	UPROPERTY(Config, EditAnywhere, Category = "Civilizations")
	TArray<FChronoCivilizationDef> Civilizations;

	/** Civilization of a new game until the player picks one. */
	UPROPERTY(Config, EditAnywhere, Category = "Civilizations")
	FName DefaultCivilization = NAME_None;

	/** Share of its cost that a boost gives a technology or civic. */
	UPROPERTY(Config, EditAnywhere, Category = "Trees", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BoostFraction = 0.4f;

	/** Culture brought each day by every heritage building. */
	UPROPERTY(Config, EditAnywhere, Category = "Heritage", meta = (ClampMin = "0.0"))
	float HeritageCulturePerDay = 0.05f;
};
