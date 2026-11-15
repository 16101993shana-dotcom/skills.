// The city's money: taxes per zone, upkeep, a treasury and a monthly ledger,
// as in SimCity and Cities: Skylines. It also accumulates the two yields that
// drive progress through the eras, as in Civilization: science and culture.
// Plain data, with no world or rendering dependency.

#pragma once

#include "CoreMinimal.h"
#include "ChronoPopulation.h"
#include "ChronoZoneGrid.h"

class FChronoRoadGraph;

/** Tunables of the economy. In game they come from UChronoEconomySettings. */
struct CHRONOECONOMY_API FChronoEconomyRules
{
	double StartingTreasury = 20000.0;

	/** Highest tax rate the player may set, as a fraction. */
	float MaxTaxRate = 0.3f;

	/** Daily income of an employed resident, taxed at the residential rate. */
	float DailyWage = 1.0f;

	/** Daily output of a worker, taxed at the rate of the zone. */
	float OutputPerCommercialWorker = 2.0f;
	float OutputPerIndustrialWorker = 2.5f;
	float OutputPerAgriculturalWorker = 1.5f;

	/** Daily upkeep of one square meter of road. */
	float RoadUpkeepPerM2PerDay = 0.0008f;

	int32 DaysPerMonth = 30;

	// Science and culture: a little from everybody, more from civic workers.
	// Culture also follows how happy people are.
	float SciencePerThousandCitizensPerDay = 0.1f;
	float SciencePerCivicWorkerPerDay = 0.02f;
	float CulturePerThousandCitizensPerDay = 0.1f;
	float CulturePerCivicWorkerPerDay = 0.02f;

	static FChronoEconomyRules FromSettings();
};

/** Money in and out over a period, by source. */
struct CHRONOECONOMY_API FChronoLedger
{
	double ResidentialTax = 0.0;
	double CommercialTax = 0.0;
	double IndustrialTax = 0.0;
	double AgriculturalTax = 0.0;
	double RoadUpkeep = 0.0;

	double GetIncome() const { return ResidentialTax + CommercialTax + IndustrialTax + AgriculturalTax; }
	double GetExpenses() const { return RoadUpkeep; }
	double GetNet() const { return GetIncome() - GetExpenses(); }

	void Add(const FChronoLedger& Other);
	void Serialize(FArchive& Ar);
};

class CHRONOECONOMY_API FChronoBudget
{
public:
	/** Starts a new game: full treasury, neutral taxes, no history. */
	void Reset(const FChronoEconomyRules& Rules, float NeutralTaxRate);

	/** Sets the tax of a zone, as a fraction, clamped to what the rules allow. Civic buildings pay no tax. */
	void SetTaxRate(EChronoZoneType Zone, float Rate, const FChronoEconomyRules& Rules);
	float GetTaxRate(EChronoZoneType Zone) const { return Conditions.GetTaxRate(Zone); }

	/** Tax rates in the form the population simulation takes them. */
	const FChronoCityConditions& GetConditions() const { return Conditions; }

	/** Collects one day of taxes, pays one day of upkeep, and adds one day of science and culture. */
	void StepDay(const FChronoPopulationStats& Stats, double RoadAreaM2, const FChronoEconomyRules& Rules);

	double GetTreasury() const { return Treasury; }
	const FChronoLedger& GetToday() const { return Today; }
	const FChronoLedger& GetCurrentMonth() const { return CurrentMonth; }
	const FChronoLedger& GetLastMonth() const { return LastMonth; }
	int32 GetDayOfMonth() const { return DayOfMonth; }

	double GetScience() const { return Science; }
	double GetCulture() const { return Culture; }
	double GetSciencePerDay() const { return SciencePerDay; }
	double GetCulturePerDay() const { return CulturePerDay; }

	/** Binary serialization for save games (both directions). Returns false on invalid data. */
	bool Serialize(FArchive& Ar);

private:
	FChronoCityConditions Conditions;
	double Treasury = 0.0;

	FChronoLedger Today;
	FChronoLedger CurrentMonth;
	FChronoLedger LastMonth;
	int32 DayOfMonth = 0;

	double Science = 0.0;
	double Culture = 0.0;
	double SciencePerDay = 0.0;
	double CulturePerDay = 0.0;
};

namespace ChronoEconomy
{
	/** Surface of every road of the network, sidewalks included, in square meters. */
	CHRONOECONOMY_API double ComputeRoadAreaM2(const FChronoRoadGraph& Graph);
}
