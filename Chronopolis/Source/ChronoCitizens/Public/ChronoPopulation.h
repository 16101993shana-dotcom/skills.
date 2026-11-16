// The inhabitants of the city, simulated one by one: each citizen has an age,
// a household, a home and possibly a job. Once per simulated day households
// arrive, find work, have children, grow old, and leave if life is bad.
//
// Out of this comes the demand for each zone (the residential, commercial and
// industrial bars of city builders): buildings only grow where the population
// asks for them. Plain data, with no world or rendering dependency.

#pragma once

#include "CoreMinimal.h"
#include "ChronoBuildingTypes.h"
#include "ChronoZoneGrid.h"

class FChronoBuildingSet;

/** Tunables of the population simulation. In game they come from UChronoPopulationSettings. */
struct CHRONOCITIZENS_API FChronoPopulationRules
{
	// ---- Capacities, from the floor area of each building ----
	float AreaPerHouseholdM2 = 70.0f;
	float AreaPerCommercialJobM2 = 40.0f;
	float AreaPerIndustrialJobM2 = 60.0f;
	float AreaPerAgriculturalJobM2 = 120.0f;
	float AreaPerCivicJobM2 = 50.0f;

	// ---- Life ----
	float WorkingAgeMin = 16.0f;
	float RetirementAge = 65.0f;
	float FertileAgeMin = 18.0f;
	float FertileAgeMax = 45.0f;
	float MaxAge = 100.0f;
	int32 MaxHouseholdSize = 6;
	float BirthsPerHouseholdPerYear = 0.12f;
	float BaseAnnualMortality = 0.004f;
	float MortalityStartAge = 40.0f;
	float MortalityDoublingYears = 9.0f;

	// ---- Moving in and out ----
	/** Share of the free homes filled per day when the city is fully attractive. */
	float ImmigrationRatePerDay = 0.04f;
	/** Attractiveness of a city without a single free job: settlers still come. */
	float MinAttractiveness = 0.2f;
	/** A household without any income for this long starts thinking of leaving. */
	int32 JoblessDaysBeforeLeaving = 90;
	/** Below this average happiness a household also thinks of leaving. */
	float UnhappyThreshold = 25.0f;
	float EmigrationChancePerDay = 0.05f;

	// ---- Happiness, from 0 to 100 ----
	float HappinessBase = 50.0f;
	float EmployedBonus = 15.0f;
	float UnemployedPenalty = 25.0f;
	/** Happiness lost per percentage point of residential tax above the neutral rate. */
	float TaxPenaltyPerPoint = 2.0f;
	float NeutralTaxRate = 0.09f;
	/** Share of the gap to the target happiness closed each day. */
	float HappinessAdjustPerDay = 0.1f;

	// ---- Public services: happiness gained by a household whose home is covered ----
	float WaterHappiness = 6.0f;
	float HealthHappiness = 5.0f;
	float SecurityHappiness = 5.0f;
	float FireHappiness = 3.0f;
	float EducationHappiness = 5.0f;
	/** Happiness lost by a household whose home has no water. */
	float NoWaterPenalty = 10.0f;

	/** Happiness change of a household, from the services that cover its home (bit mask of EChronoServiceType). */
	float GetServiceHappiness(uint32 ServiceMask) const;

	// ---- Demand ----
	/** Share of all jobs that each kind of workplace should provide. */
	float TargetCommercialJobShare = 0.35f;
	float TargetIndustrialJobShare = 0.30f;
	float TargetAgriculturalJobShare = 0.25f;
	float TargetCivicJobShare = 0.10f;
	/** Demand lost per unit of tax rate above the neutral rate (0.01 = one point). */
	float DemandTaxSensitivity = 4.0f;

	/** Seed of the population random stream: same seed, same history. */
	int32 Seed = 9001;

	static FChronoPopulationRules FromSettings();
};

/** What the rest of the city imposes on its inhabitants. Set by the economy. */
struct FChronoCityConditions
{
	/** Tax rates, as fractions (0.09 = 9 %), indexed by EChronoZoneType. */
	float TaxRate[static_cast<int32>(EChronoZoneType::Count)] = { 0.0f, 0.09f, 0.09f, 0.09f, 0.09f, 0.0f };

	float GetTaxRate(EChronoZoneType Zone) const { return TaxRate[static_cast<int32>(Zone)]; }

	/** Happiness change of everybody who works, from the time spent commuting (0 or negative). Set by traffic. */
	float CommuteHappiness = 0.0f;
};

struct FChronoCitizen
{
	/** INDEX_NONE marks a free slot. */
	int32 HouseholdId = INDEX_NONE;
	int32 WorkBuildingId = INDEX_NONE;
	float AgeYears = 0.0f;
	float Happiness = 50.0f;
};

struct FChronoHousehold
{
	/** False marks a free slot. */
	bool bInUse = false;

	/** INDEX_NONE while the household is looking for a home (its building was demolished). */
	int32 HomeBuildingId = INDEX_NONE;
	TArray<int32> Members;
	/** Days in a row without a single employed member. */
	int32 JoblessDays = 0;
};

/** Counts of one day, kept for the interface and for the budget. */
struct FChronoPopulationStats
{
	int32 Population = 0;
	int32 Households = 0;
	int32 Children = 0;
	int32 WorkingAge = 0;
	int32 Elderly = 0;
	int32 Employed = 0;
	int32 Unemployed = 0;

	int32 HousingUnits = 0;
	int32 FreeHousing = 0;
	int32 Jobs = 0;
	int32 FreeJobs = 0;

	/** Indexed by EChronoZoneType. */
	int32 JobsByZone[static_cast<int32>(EChronoZoneType::Count)] = {};
	int32 WorkersByZone[static_cast<int32>(EChronoZoneType::Count)] = {};

	float AverageHappiness = 0.0f;

	/** Households whose home is covered by each service, indexed by EChronoServiceType. */
	int32 HouseholdsServed[static_cast<int32>(EChronoServiceType::Count)] = {};

	int32 BirthsToday = 0;
	int32 DeathsToday = 0;
	int32 ImmigrantsToday = 0;
	int32 EmigrantsToday = 0;
};

class CHRONOCITIZENS_API FChronoPopulation
{
public:
	/**
	 * Advances the population by one day.
	 * @param DayIndex number of the day, used to seed the day's random draws.
	 */
	void StepDay(int64 DayIndex, const FChronoBuildingSet& Buildings, const FChronoCityConditions& Conditions, const FChronoPopulationRules& Rules);

	/**
	 * Recounts homes and jobs from the buildings and refreshes the statistics,
	 * without advancing time. Used after loading a game.
	 */
	void Refresh(const FChronoBuildingSet& Buildings, const FChronoCityConditions& Conditions, const FChronoPopulationRules& Rules);

	const FChronoPopulationStats& GetStats() const { return Stats; }
	const FChronoZoneDemand& GetDemand() const { return Demand; }

	const TArray<FChronoCitizen>& GetCitizens() const { return Citizens; }
	const TArray<FChronoHousehold>& GetHouseholds() const { return Households; }

	/** Homes and jobs of one building, and how many are taken (zeros for an unknown building). */
	void GetBuildingOccupancy(int32 BuildingId, int32& OutHousingUnits, int32& OutHouseholds, int32& OutJobs, int32& OutWorkers) const;

	/** Homes and jobs that a building provides, from its zone and floor area. */
	static void ComputeCapacity(const FChronoBuilding& Building, const FChronoPopulationRules& Rules, int32& OutHousingUnits, int32& OutJobs);

	/**
	 * Checks that the data is coherent: every member belongs to its household,
	 * no home holds more households than it has units, no workplace more
	 * workers than it has jobs. Returns false and explains in OutError.
	 */
	bool CheckInvariants(FString& OutError) const;

	void Reset();

	/**
	 * Binary serialization for save games (both directions). Loading replaces
	 * the population and returns false, leaving it empty, on invalid data.
	 * Call Refresh after loading.
	 */
	bool Serialize(FArchive& Ar);

private:
	struct FOccupancy
	{
		EChronoZoneType Zone = EChronoZoneType::None;
		int32 HousingUnits = 0;
		int32 Households = 0;
		int32 Jobs = 0;
		int32 Workers = 0;
		/** Services that cover the building, as a bit mask of EChronoServiceType. Only filled for homes. */
		uint32 ServiceMask = 0;
	};

	/** Recounts occupancy; households and workers whose building is gone or full are displaced. */
	void SyncBuildings(const FChronoBuildingSet& Buildings, const FChronoPopulationRules& Rules, TArray<int32>& OutHomelessHouseholds);

	void AgeAndRenew(FRandomStream& Random, const FChronoPopulationRules& Rules);
	void AssignJobs(const FChronoPopulationRules& Rules);
	void UpdateHappiness(const FChronoCityConditions& Conditions, const FChronoPopulationRules& Rules);
	void Emigrate(FRandomStream& Random, const FChronoPopulationRules& Rules);
	void Immigrate(FRandomStream& Random, const FChronoPopulationRules& Rules);
	void ComputeStatsAndDemand(const FChronoCityConditions& Conditions, const FChronoPopulationRules& Rules);

	int32 AddCitizen(int32 HouseholdId, float AgeYears);
	void RemoveCitizen(int32 CitizenId);
	int32 AddHousehold(int32 HomeBuildingId);
	void RemoveHousehold(int32 HouseholdId);

	/** Next building, in id order, with a free home (or a free job). INDEX_NONE if none. */
	int32 TakeFreeHome();
	int32 TakeFreeJob();

	TArray<FChronoCitizen> Citizens;
	TArray<FChronoHousehold> Households;

	/** Free slots, reused last in first out. Saved, so a loaded game replays identically. */
	TArray<int32> FreeCitizens;
	TArray<int32> FreeHouseholds;

	// Rebuilt from the buildings every day, not saved.
	TMap<int32, FOccupancy> Occupancy;
	TArray<int32> BuildingOrder;
	int32 HomeCursor = 0;
	int32 JobCursor = 0;

	// Counters of the day being simulated.
	int32 BirthsToday = 0;
	int32 DeathsToday = 0;
	int32 ImmigrantsToday = 0;
	int32 EmigrantsToday = 0;

	FChronoPopulationStats Stats;
	FChronoZoneDemand Demand;
};
