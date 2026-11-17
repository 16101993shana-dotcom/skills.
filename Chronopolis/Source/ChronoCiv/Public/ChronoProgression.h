// How a city moves through the eras, as plain data with no world or
// rendering dependency.
//
// The city studies one technology (with science) and one civic (with
// culture) at a time. What the city does can give a head start (a boost).
// It enters the next era when it knows enough of the current era's
// technologies and civics AND has enough inhabitants: the trees come from the
// 4X games, the population milestones from the city builders.

#pragma once

#include "CoreMinimal.h"
#include "ChronoBuildingTypes.h"
#include "ChronoCivTypes.h"

/** All the data of the civilization layer, copied out of the settings. */
struct CHRONOCIV_API FChronoCivData
{
	TArray<FChronoEraDef> Eras;
	TArray<FChronoTechDef> Techs;
	TArray<FChronoCivilizationDef> Civilizations;
	float BoostFraction = 0.4f;
	float HeritageCulturePerDay = 0.05f;

	static FChronoCivData FromSettings();

	const FChronoTechDef* FindTech(FName Id) const;
	const FChronoCivilizationDef* FindCivilization(FName Id) const;

	/**
	 * Checks the data: unique ids, known prerequisites from the same or an
	 * earlier era, no cycle, costs above zero, enough technologies and civics
	 * in each era to leave it, population milestones that never go down.
	 * Returns false and lists every problem in OutErrors.
	 */
	bool Validate(TArray<FString>& OutErrors) const;

	/**
	 * Checks the building definitions against the trees: every building can
	 * be unlocked, every unlock opens a building, civilizations and
	 * replacements exist, and every civilization can grow something on each
	 * kind of zone in every era once that era's trees are known. Returns
	 * false and lists every problem in OutErrors (appended).
	 */
	bool ValidateBuildings(TConstArrayView<FChronoBuildingDefData> Definitions, TArray<FString>& OutErrors) const;
};

/** What the city has, as far as boosts are concerned. */
struct CHRONOCIV_API FChronoCityFacts
{
	int32 Population = 0;

	/** Number of buildings of each kind. A building that replaces another also counts as the one it replaces. */
	TMap<FName, int32> BuildingCounts;
};

enum class EChronoProgressEventType : uint8
{
	TechResearched,
	CivicAdopted,
	Boosted,
	EraReached
};

struct FChronoProgressEvent
{
	EChronoProgressEventType Type = EChronoProgressEventType::TechResearched;
	/** The technology or civic, or the era's id. */
	FName Id = NAME_None;
	/** Era of the city after the event. */
	int32 Era = 0;
};

class CHRONOCIV_API FChronoProgression
{
public:
	/** A new city of a civilization: first era, nothing known but what needs no unlock. */
	void Start(FName CivilizationId, const FChronoCivData& Data);

	FName GetCivilization() const { return Civilization; }
	int32 GetEra() const { return Era; }

	bool IsKnown(FName TechId) const { return Known.Contains(TechId); }
	int32 GetNumKnown() const { return Known.Num(); }

	/** True for a name unlocked by a known technology or civic, or by the civilization. */
	bool IsUnlocked(FName Name) const { return Unlocked.Contains(Name); }
	const TSet<FName>& GetUnlocked() const { return Unlocked; }

	/** True if the technology or civic can be studied now: not known, of an era reached, every prerequisite known. */
	bool IsAvailable(FName TechId, const FChronoCivData& Data) const;

	/**
	 * Chooses what to study next in its tree (technology or civic). Progress
	 * already made on the previous choice is kept for later. Returns false if
	 * it is not available.
	 */
	bool SetCurrent(FName TechId, const FChronoCivData& Data);

	FName GetCurrentTech() const { return CurrentTech; }
	FName GetCurrentCivic() const { return CurrentCivic; }

	/** Science or culture already put into a technology or civic. */
	double GetProgress(FName TechId) const;

	/**
	 * One day: science and culture of the day go to the current choices (the
	 * civilization's multipliers apply), boosts are checked, and the city
	 * enters the next era if it qualifies. With nothing chosen, the cheapest
	 * available is picked, so a city never stops progressing. Leftovers carry
	 * over to the next choice.
	 */
	void StepDay(double Science, double Culture, const FChronoCityFacts& Facts, const FChronoCivData& Data, TArray<FChronoProgressEvent>& OutEvents);

	/** True if the city meets every condition of the next era, other than its population. */
	bool HasTreesForNextEra(const FChronoCivData& Data) const;

	/**
	 * Checks that the state is consistent with the data: every known entry
	 * has its prerequisites known, and every era entered was earned.
	 */
	bool CheckInvariants(const FChronoCivData& Data, FString& OutError) const;

	/**
	 * Binary serialization for save games (both directions). On load, names
	 * the data no longer has are dropped, so a save survives a change of the
	 * trees. Returns false, leaving the state reset, on invalid data.
	 */
	bool Serialize(FArchive& Ar, const FChronoCivData& Data);

private:
	void Learn(const FChronoTechDef& Tech, TArray<FChronoProgressEvent>& OutEvents);
	void RebuildUnlocked(const FChronoCivData& Data);
	/** Puts Points into the tree (technologies or civics), finishing as many entries as they pay for. */
	void Spend(bool bCivic, double Points, const FChronoCivData& Data, TArray<FChronoProgressEvent>& OutEvents);
	FName PickCheapest(bool bCivic, const FChronoCivData& Data) const;
	int32 CountKnownOfEra(int32 InEra, bool bCivic, const FChronoCivData& Data) const;

	FName Civilization = NAME_None;
	int32 Era = 0;
	TSet<FName> Known;
	TSet<FName> Boosted;
	TSet<FName> Unlocked;
	TMap<FName, double> Progress;
	FName CurrentTech = NAME_None;
	FName CurrentCivic = NAME_None;

	/** Science and culture produced while nothing was left to study, kept for the next era. */
	double BankedScience = 0.0;
	double BankedCulture = 0.0;
};
