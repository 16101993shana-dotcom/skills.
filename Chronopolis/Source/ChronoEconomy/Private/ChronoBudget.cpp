#include "ChronoBudget.h"

#include "ChronoEconomySettings.h"
#include "ChronoRoadGeometry.h"
#include "ChronoRoadGraph.h"
#include "ChronoRoadTypes.h"

FChronoEconomyRules FChronoEconomyRules::FromSettings()
{
	const UChronoEconomySettings* Settings = UChronoEconomySettings::Get();

	FChronoEconomyRules Rules;
	Rules.StartingTreasury = Settings->StartingTreasury;
	Rules.MaxTaxRate = Settings->MaxTaxRate;
	Rules.DailyWage = Settings->DailyWage;
	Rules.OutputPerCommercialWorker = Settings->OutputPerCommercialWorker;
	Rules.OutputPerIndustrialWorker = Settings->OutputPerIndustrialWorker;
	Rules.OutputPerAgriculturalWorker = Settings->OutputPerAgriculturalWorker;
	Rules.RoadUpkeepPerM2PerDay = Settings->RoadUpkeepPerM2PerDay;
	Rules.RoadCostPerM2 = Settings->RoadCostPerM2;
	Rules.DaysPerMonth = Settings->DaysPerMonth;
	Rules.MaxDebt = Settings->MaxDebt;
	Rules.LoanInterestRate = Settings->LoanInterestRate;
	Rules.LoanTermDays = Settings->LoanTermDays;
	Rules.BaseSciencePerDay = Settings->BaseSciencePerDay;
	Rules.BaseCulturePerDay = Settings->BaseCulturePerDay;
	Rules.SciencePerEducatedHouseholdPerDay = Settings->SciencePerEducatedHouseholdPerDay;
	Rules.SciencePerThousandCitizensPerDay = Settings->SciencePerThousandCitizensPerDay;
	Rules.SciencePerCivicWorkerPerDay = Settings->SciencePerCivicWorkerPerDay;
	Rules.CulturePerThousandCitizensPerDay = Settings->CulturePerThousandCitizensPerDay;
	Rules.CulturePerCivicWorkerPerDay = Settings->CulturePerCivicWorkerPerDay;
	return Rules;
}

void FChronoLedger::Add(const FChronoLedger& Other)
{
	ResidentialTax += Other.ResidentialTax;
	CommercialTax += Other.CommercialTax;
	IndustrialTax += Other.IndustrialTax;
	AgriculturalTax += Other.AgriculturalTax;
	RoadUpkeep += Other.RoadUpkeep;
	ServiceUpkeep += Other.ServiceUpkeep;
	Construction += Other.Construction;
	LoanRepayment += Other.LoanRepayment;
}

void FChronoLedger::Serialize(FArchive& Ar)
{
	Ar << ResidentialTax;
	Ar << CommercialTax;
	Ar << IndustrialTax;
	Ar << AgriculturalTax;
	Ar << RoadUpkeep;
	Ar << ServiceUpkeep;
	Ar << Construction;
	Ar << LoanRepayment;
}

void FChronoBudget::Reset(const FChronoEconomyRules& Rules, float NeutralTaxRate)
{
	*this = FChronoBudget();
	Treasury = Rules.StartingTreasury;

	for (const EChronoZoneType Zone : { EChronoZoneType::Residential, EChronoZoneType::Commercial, EChronoZoneType::Industrial, EChronoZoneType::Agricultural })
	{
		SetTaxRate(Zone, NeutralTaxRate, Rules);
	}
}

void FChronoBudget::SetTaxRate(EChronoZoneType Zone, float Rate, const FChronoEconomyRules& Rules)
{
	// Unzoned land and civic buildings are not taxed.
	if (Zone == EChronoZoneType::None || Zone == EChronoZoneType::Civic || Zone >= EChronoZoneType::Count)
	{
		return;
	}

	Conditions.TaxRate[static_cast<int32>(Zone)] = FMath::Clamp(Rate, 0.0f, FMath::Max(Rules.MaxTaxRate, 0.0f));
}

bool FChronoBudget::Spend(double Cost)
{
	if (Cost <= 0.0)
	{
		return true;
	}
	if (!CanAfford(Cost))
	{
		return false;
	}

	Treasury -= Cost;
	ConstructionSinceLastDay += Cost;
	return true;
}

bool FChronoBudget::TakeLoan(double Amount, const FChronoEconomyRules& Rules)
{
	const double Owed = Amount * (1.0 + FMath::Max(Rules.LoanInterestRate, 0.0f));
	if (Amount <= 0.0 || GetDebt() + Owed > Rules.MaxDebt)
	{
		return false;
	}

	FLoan Loan;
	Loan.Remaining = Owed;
	Loan.DailyPayment = Owed / FMath::Max(Rules.LoanTermDays, 1);
	Loans.Add(Loan);

	Treasury += Amount;
	return true;
}

double FChronoBudget::GetDebt() const
{
	double Debt = 0.0;
	for (const FLoan& Loan : Loans)
	{
		Debt += Loan.Remaining;
	}
	return Debt;
}

void FChronoBudget::StepDay(const FChronoPopulationStats& Stats, double RoadAreaM2, double ServiceUpkeepPerDay, const FChronoEconomyRules& Rules)
{
	auto Workers = [&Stats](EChronoZoneType Zone)
	{
		return static_cast<double>(Stats.WorkersByZone[static_cast<int32>(Zone)]);
	};

	Today = FChronoLedger();
	Today.ResidentialTax = Stats.Employed * static_cast<double>(Rules.DailyWage) * GetTaxRate(EChronoZoneType::Residential);
	Today.CommercialTax = Workers(EChronoZoneType::Commercial) * Rules.OutputPerCommercialWorker * GetTaxRate(EChronoZoneType::Commercial);
	Today.IndustrialTax = Workers(EChronoZoneType::Industrial) * Rules.OutputPerIndustrialWorker * GetTaxRate(EChronoZoneType::Industrial);
	Today.AgriculturalTax = Workers(EChronoZoneType::Agricultural) * Rules.OutputPerAgriculturalWorker * GetTaxRate(EChronoZoneType::Agricultural);
	Today.RoadUpkeep = FMath::Max(RoadAreaM2, 0.0) * Rules.RoadUpkeepPerM2PerDay;
	Today.ServiceUpkeep = FMath::Max(ServiceUpkeepPerDay, 0.0);

	// Each loan is repaid by its daily payment until nothing remains.
	for (int32 Index = Loans.Num() - 1; Index >= 0; --Index)
	{
		FLoan& Loan = Loans[Index];
		const double Payment = FMath::Min(Loan.DailyPayment, Loan.Remaining);
		Loan.Remaining -= Payment;
		Today.LoanRepayment += Payment;
		if (Loan.Remaining <= 1.0e-6)
		{
			Loans.RemoveAt(Index);
		}
	}

	// Constructions were paid when they were built: they appear in the
	// ledger of the day, but are not taken from the treasury a second time.
	Today.Construction = ConstructionSinceLastDay;
	ConstructionSinceLastDay = 0.0;
	Treasury += Today.GetNet() + Today.Construction;

	CurrentMonth.Add(Today);
	if (++DayOfMonth >= FMath::Max(Rules.DaysPerMonth, 1))
	{
		LastMonth = CurrentMonth;
		CurrentMonth = FChronoLedger();
		DayOfMonth = 0;
	}

	// Science and culture: the yields that will unlock technologies and
	// civics, and through them the next era.
	const double Thousands = Stats.Population / 1000.0;
	const double CivicWorkers = Workers(EChronoZoneType::Civic);
	const double Mood = FMath::Clamp(Stats.AverageHappiness / 50.0, 0.0, 2.0);

	const double Educated = Stats.HouseholdsServed[static_cast<int32>(EChronoServiceType::Education)];
	const double Inhabited = Stats.Population > 0 ? 1.0 : 0.0;

	SciencePerDay = Inhabited * Rules.BaseSciencePerDay + Thousands * Rules.SciencePerThousandCitizensPerDay
		+ CivicWorkers * Rules.SciencePerCivicWorkerPerDay + Educated * Rules.SciencePerEducatedHouseholdPerDay;
	CulturePerDay = Inhabited * Rules.BaseCulturePerDay
		+ (Thousands * Rules.CulturePerThousandCitizensPerDay + CivicWorkers * Rules.CulturePerCivicWorkerPerDay) * Mood;
	Science += SciencePerDay;
	Culture += CulturePerDay;
}

bool FChronoBudget::Serialize(FArchive& Ar)
{
	// Bump when the binary layout changes. Version 2 adds loans, service
	// upkeep and construction costs.
	const int32 CurrentVersion = 2;
	// Sanity bound on the count read from a file.
	const int32 MaxLoans = 1024;

	int32 Version = CurrentVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != CurrentVersion)
	{
		Ar.SetError();
		return false;
	}

	int32 NumLoans = Loans.Num();
	Ar << NumLoans;
	if (Ar.IsLoading())
	{
		if (Ar.IsError() || NumLoans < 0 || NumLoans > MaxLoans)
		{
			*this = FChronoBudget();
			Ar.SetError();
			return false;
		}
		Loans.SetNum(NumLoans);
	}
	for (FLoan& Loan : Loans)
	{
		Ar << Loan.Remaining;
		Ar << Loan.DailyPayment;
	}
	Ar << ConstructionSinceLastDay;

	for (float& Rate : Conditions.TaxRate)
	{
		Ar << Rate;
	}
	Ar << Treasury;
	Today.Serialize(Ar);
	CurrentMonth.Serialize(Ar);
	LastMonth.Serialize(Ar);
	Ar << DayOfMonth;
	Ar << Science;
	Ar << Culture;
	Ar << SciencePerDay;
	Ar << CulturePerDay;

	if (Ar.IsLoading())
	{
		bool bValid = !Ar.IsError() && DayOfMonth >= 0 && Science >= 0.0 && Culture >= 0.0 && FMath::IsFinite(Treasury) && ConstructionSinceLastDay >= 0.0;
		for (const FLoan& Loan : Loans)
		{
			bValid = bValid && Loan.Remaining >= 0.0 && Loan.DailyPayment > 0.0;
		}
		for (const float Rate : Conditions.TaxRate)
		{
			bValid = bValid && Rate >= 0.0f && Rate <= 1.0f;
		}
		if (!bValid)
		{
			*this = FChronoBudget();
			Ar.SetError();
			return false;
		}
	}

	return !Ar.IsError();
}

double ChronoEconomy::ComputeRoadAreaM2(const FChronoRoadGraph& Graph)
{
	double AreaCm2 = 0.0;
	for (const TPair<int32, FChronoRoadSegment>& Pair : Graph.GetSegments())
	{
		AreaCm2 += Pair.Value.Length * 2.0 * Pair.Value.Profile.GetHalfWidthCm();
	}
	return AreaCm2 / 10000.0;
}

double ChronoEconomy::EstimateRoadCost(const FChronoRoadBuildRequest& Request, const FChronoEconomyRules& Rules)
{
	const FChronoBezier2D Curve = Request.bCurved
		? FChronoBezier2D::MakeQuadratic(Request.Start, Request.Control, Request.End)
		: FChronoBezier2D::MakeLine(Request.Start, Request.End);

	const double AreaM2 = Curve.ComputeLength() * 2.0 * Request.Profile.GetHalfWidthCm() / 10000.0;
	return AreaM2 * Rules.RoadCostPerM2;
}
