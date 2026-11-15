#include "ChronoBudget.h"

#include "ChronoEconomySettings.h"
#include "ChronoRoadGraph.h"

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
	Rules.DaysPerMonth = Settings->DaysPerMonth;
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
}

void FChronoLedger::Serialize(FArchive& Ar)
{
	Ar << ResidentialTax;
	Ar << CommercialTax;
	Ar << IndustrialTax;
	Ar << AgriculturalTax;
	Ar << RoadUpkeep;
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

void FChronoBudget::StepDay(const FChronoPopulationStats& Stats, double RoadAreaM2, const FChronoEconomyRules& Rules)
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

	Treasury += Today.GetNet();

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

	SciencePerDay = Thousands * Rules.SciencePerThousandCitizensPerDay + CivicWorkers * Rules.SciencePerCivicWorkerPerDay;
	CulturePerDay = (Thousands * Rules.CulturePerThousandCitizensPerDay + CivicWorkers * Rules.CulturePerCivicWorkerPerDay) * Mood;
	Science += SciencePerDay;
	Culture += CulturePerDay;
}

bool FChronoBudget::Serialize(FArchive& Ar)
{
	// Bump when the binary layout changes.
	const int32 CurrentVersion = 1;

	int32 Version = CurrentVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != CurrentVersion)
	{
		Ar.SetError();
		return false;
	}

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
		bool bValid = !Ar.IsError() && DayOfMonth >= 0 && Science >= 0.0 && Culture >= 0.0 && FMath::IsFinite(Treasury);
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
