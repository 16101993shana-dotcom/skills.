// The city's money: taxes per zone, upkeep, a treasury and a monthly ledger,
// as in SimCity and Cities: Skylines. It also accumulates the two yields that
// drive progress through the eras, as in Civilization: science and culture.
// Plain data, with no world or rendering dependency.

#pragma once

#include "CoreMinimal.h"
#include "ChronoPopulation.h"
#include "ChronoZoneGrid.h"

class FChronoRoadGraph;
struct FChronoRoadBuildRequest;

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

	/** Price of one square meter of new road. */
	float RoadCostPerM2 = 0.5f;

	int32 DaysPerMonth = 30;

	// Loans: a fixed total interest, repaid in equal daily payments.
	double MaxDebt = 50000.0;
	float LoanInterestRate = 0.1f;
	int32 LoanTermDays = 360;

	// Science and culture: a base yield from the city itself, some from
	// everybody, more from civic workers, and science from the households a
	// school serves. Culture beyond the base follows how happy people are.
	float BaseSciencePerDay = 0.3f;
	float BaseCulturePerDay = 0.3f;
	float SciencePerThousandCitizensPerDay = 1.0f;
	float SciencePerCivicWorkerPerDay = 0.05f;
	float SciencePerEducatedHouseholdPerDay = 0.01f;
	float CulturePerThousandCitizensPerDay = 0.8f;
	float CulturePerCivicWorkerPerDay = 0.05f;

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
	double ServiceUpkeep = 0.0;
	double Construction = 0.0;
	double LoanRepayment = 0.0;

	double GetIncome() const { return ResidentialTax + CommercialTax + IndustrialTax + AgriculturalTax; }
	double GetExpenses() const { return RoadUpkeep + ServiceUpkeep + Construction + LoanRepayment; }
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

	/** Collects one day of taxes, pays one day of upkeep and loan repayments, and adds one day of science and culture. */
	void StepDay(const FChronoPopulationStats& Stats, double RoadAreaM2, double ServiceUpkeepPerDay, const FChronoEconomyRules& Rules);

	/** True if the treasury holds at least Cost. */
	bool CanAfford(double Cost) const { return Treasury >= Cost; }

	/** Pays for a construction now. Returns false, and pays nothing, if the treasury is too low. */
	bool Spend(double Cost);

	/**
	 * Borrows Amount: the treasury receives it at once, and the amount plus
	 * interest is repaid day by day. Returns false if it would exceed the debt limit.
	 */
	bool TakeLoan(double Amount, const FChronoEconomyRules& Rules);

	/** What remains to be repaid, interest included. */
	double GetDebt() const;
	int32 GetNumLoans() const { return Loans.Num(); }

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
	struct FLoan
	{
		double Remaining = 0.0;
		double DailyPayment = 0.0;
	};

	FChronoCityConditions Conditions;
	double Treasury = 0.0;

	TArray<FLoan> Loans;

	/** Paid since the last day step, reported in the ledger of the next one. */
	double ConstructionSinceLastDay = 0.0;

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

	/** Price of the road a request would build, from its length and width. */
	CHRONOECONOMY_API double EstimateRoadCost(const FChronoRoadBuildRequest& Request, const FChronoEconomyRules& Rules);
}
