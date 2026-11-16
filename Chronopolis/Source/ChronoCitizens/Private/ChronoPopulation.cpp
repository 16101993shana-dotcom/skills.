#include "ChronoPopulation.h"

#include "ChronoBuildingSet.h"
#include "ChronoPopulationSettings.h"

namespace ChronoPopulationPrivate
{
	const float DaysPerYear = 365.0f;

	bool IsWorkingAge(float AgeYears, const FChronoPopulationRules& Rules)
	{
		return AgeYears >= Rules.WorkingAgeMin && AgeYears < Rules.RetirementAge;
	}

	int32 ZoneIndex(EChronoZoneType Zone)
	{
		return static_cast<int32>(Zone);
	}
}

FChronoPopulationRules FChronoPopulationRules::FromSettings()
{
	const UChronoPopulationSettings* Settings = UChronoPopulationSettings::Get();

	FChronoPopulationRules Rules;
	Rules.AreaPerHouseholdM2 = Settings->AreaPerHouseholdM2;
	Rules.AreaPerCommercialJobM2 = Settings->AreaPerCommercialJobM2;
	Rules.AreaPerIndustrialJobM2 = Settings->AreaPerIndustrialJobM2;
	Rules.AreaPerAgriculturalJobM2 = Settings->AreaPerAgriculturalJobM2;
	Rules.AreaPerCivicJobM2 = Settings->AreaPerCivicJobM2;
	Rules.WorkingAgeMin = Settings->WorkingAgeMin;
	Rules.RetirementAge = Settings->RetirementAge;
	Rules.FertileAgeMin = Settings->FertileAgeMin;
	Rules.FertileAgeMax = Settings->FertileAgeMax;
	Rules.MaxAge = Settings->MaxAge;
	Rules.MaxHouseholdSize = Settings->MaxHouseholdSize;
	Rules.BirthsPerHouseholdPerYear = Settings->BirthsPerHouseholdPerYear;
	Rules.BaseAnnualMortality = Settings->BaseAnnualMortality;
	Rules.MortalityStartAge = Settings->MortalityStartAge;
	Rules.MortalityDoublingYears = Settings->MortalityDoublingYears;
	Rules.ImmigrationRatePerDay = Settings->ImmigrationRatePerDay;
	Rules.MinAttractiveness = Settings->MinAttractiveness;
	Rules.JoblessDaysBeforeLeaving = Settings->JoblessDaysBeforeLeaving;
	Rules.UnhappyThreshold = Settings->UnhappyThreshold;
	Rules.EmigrationChancePerDay = Settings->EmigrationChancePerDay;
	Rules.HappinessBase = Settings->HappinessBase;
	Rules.EmployedBonus = Settings->EmployedBonus;
	Rules.UnemployedPenalty = Settings->UnemployedPenalty;
	Rules.TaxPenaltyPerPoint = Settings->TaxPenaltyPerPoint;
	Rules.NeutralTaxRate = Settings->NeutralTaxRate;
	Rules.HappinessAdjustPerDay = Settings->HappinessAdjustPerDay;
	Rules.TargetCommercialJobShare = Settings->TargetCommercialJobShare;
	Rules.TargetIndustrialJobShare = Settings->TargetIndustrialJobShare;
	Rules.TargetAgriculturalJobShare = Settings->TargetAgriculturalJobShare;
	Rules.TargetCivicJobShare = Settings->TargetCivicJobShare;
	Rules.DemandTaxSensitivity = Settings->DemandTaxSensitivity;
	Rules.WaterHappiness = Settings->WaterHappiness;
	Rules.HealthHappiness = Settings->HealthHappiness;
	Rules.SecurityHappiness = Settings->SecurityHappiness;
	Rules.FireHappiness = Settings->FireHappiness;
	Rules.EducationHappiness = Settings->EducationHappiness;
	Rules.NoWaterPenalty = Settings->NoWaterPenalty;
	Rules.Seed = Settings->Seed;
	return Rules;
}

float FChronoPopulationRules::GetServiceHappiness(uint32 ServiceMask) const
{
	auto Has = [ServiceMask](EChronoServiceType Service)
	{
		return (ServiceMask & (1u << static_cast<uint32>(Service))) != 0;
	};

	float Change = 0.0f;
	Change += Has(EChronoServiceType::Water) ? WaterHappiness : -NoWaterPenalty;
	Change += Has(EChronoServiceType::Health) ? HealthHappiness : 0.0f;
	Change += Has(EChronoServiceType::Security) ? SecurityHappiness : 0.0f;
	Change += Has(EChronoServiceType::Fire) ? FireHappiness : 0.0f;
	Change += Has(EChronoServiceType::Education) ? EducationHappiness : 0.0f;
	return Change;
}

void FChronoPopulation::ComputeCapacity(const FChronoBuilding& Building, const FChronoPopulationRules& Rules, int32& OutHousingUnits, int32& OutJobs)
{
	OutHousingUnits = 0;
	OutJobs = 0;

	// Floor area in square meters: footprint times the number of storeys.
	const double FootprintM2 = (2.0 * Building.Footprint.HalfExtent.X) * (2.0 * Building.Footprint.HalfExtent.Y) / 10000.0;
	const double AreaM2 = FootprintM2 * FMath::Max(Building.Floors, 1);

	auto Units = [AreaM2](float AreaPerUnitM2)
	{
		// Every building holds at least one household or one job.
		return FMath::Max(1, FMath::FloorToInt32(AreaM2 / FMath::Max(AreaPerUnitM2, 1.0f)));
	};

	switch (Building.Zone)
	{
	case EChronoZoneType::Residential:
		OutHousingUnits = Units(Rules.AreaPerHouseholdM2);
		break;
	case EChronoZoneType::Commercial:
		OutJobs = Units(Rules.AreaPerCommercialJobM2);
		break;
	case EChronoZoneType::Industrial:
		OutJobs = Units(Rules.AreaPerIndustrialJobM2);
		break;
	case EChronoZoneType::Agricultural:
		OutJobs = Units(Rules.AreaPerAgriculturalJobM2);
		break;
	case EChronoZoneType::Civic:
		OutJobs = Units(Rules.AreaPerCivicJobM2);
		break;
	default:
		break;
	}
}

void FChronoPopulation::GetBuildingOccupancy(int32 BuildingId, int32& OutHousingUnits, int32& OutHouseholds, int32& OutJobs, int32& OutWorkers) const
{
	OutHousingUnits = OutHouseholds = OutJobs = OutWorkers = 0;
	if (const FOccupancy* Found = Occupancy.Find(BuildingId))
	{
		OutHousingUnits = Found->HousingUnits;
		OutHouseholds = Found->Households;
		OutJobs = Found->Jobs;
		OutWorkers = Found->Workers;
	}
}

void FChronoPopulation::Reset()
{
	Citizens.Reset();
	Households.Reset();
	FreeCitizens.Reset();
	FreeHouseholds.Reset();
	Occupancy.Reset();
	BuildingOrder.Reset();
	HomeCursor = 0;
	JobCursor = 0;
	Stats = FChronoPopulationStats();
	Demand = FChronoZoneDemand();
}

int32 FChronoPopulation::AddCitizen(int32 HouseholdId, float AgeYears)
{
	int32 CitizenId;
	if (FreeCitizens.Num() > 0)
	{
		CitizenId = FreeCitizens.Last();
		FreeCitizens.RemoveAt(FreeCitizens.Num() - 1);
	}
	else
	{
		CitizenId = Citizens.Num();
		Citizens.AddDefaulted_GetRef();
	}

	FChronoCitizen& Citizen = Citizens[CitizenId];
	Citizen = FChronoCitizen();
	Citizen.HouseholdId = HouseholdId;
	Citizen.AgeYears = AgeYears;

	Households[HouseholdId].Members.Add(CitizenId);
	return CitizenId;
}

void FChronoPopulation::RemoveCitizen(int32 CitizenId)
{
	FChronoCitizen& Citizen = Citizens[CitizenId];
	const int32 HouseholdId = Citizen.HouseholdId;
	if (HouseholdId == INDEX_NONE)
	{
		return;
	}

	if (FOccupancy* Work = Occupancy.Find(Citizen.WorkBuildingId))
	{
		Work->Workers = FMath::Max(Work->Workers - 1, 0);
	}

	Citizen = FChronoCitizen();
	FreeCitizens.Add(CitizenId);

	FChronoHousehold& Household = Households[HouseholdId];
	Household.Members.Remove(CitizenId);
	if (Household.Members.Num() == 0)
	{
		RemoveHousehold(HouseholdId);
	}
}

int32 FChronoPopulation::AddHousehold(int32 HomeBuildingId)
{
	int32 HouseholdId;
	if (FreeHouseholds.Num() > 0)
	{
		HouseholdId = FreeHouseholds.Last();
		FreeHouseholds.RemoveAt(FreeHouseholds.Num() - 1);
	}
	else
	{
		HouseholdId = Households.Num();
		Households.AddDefaulted_GetRef();
	}

	FChronoHousehold& Household = Households[HouseholdId];
	Household = FChronoHousehold();
	Household.bInUse = true;
	Household.HomeBuildingId = HomeBuildingId;
	return HouseholdId;
}

void FChronoPopulation::RemoveHousehold(int32 HouseholdId)
{
	FChronoHousehold& Household = Households[HouseholdId];
	if (!Household.bInUse)
	{
		return;
	}

	for (const int32 CitizenId : Household.Members)
	{
		FChronoCitizen& Citizen = Citizens[CitizenId];
		if (FOccupancy* Work = Occupancy.Find(Citizen.WorkBuildingId))
		{
			Work->Workers = FMath::Max(Work->Workers - 1, 0);
		}
		Citizen = FChronoCitizen();
		FreeCitizens.Add(CitizenId);
	}

	if (FOccupancy* Home = Occupancy.Find(Household.HomeBuildingId))
	{
		Home->Households = FMath::Max(Home->Households - 1, 0);
	}

	Household = FChronoHousehold();
	FreeHouseholds.Add(HouseholdId);
}

int32 FChronoPopulation::TakeFreeHome()
{
	for (; HomeCursor < BuildingOrder.Num(); ++HomeCursor)
	{
		FOccupancy& Home = Occupancy.FindChecked(BuildingOrder[HomeCursor]);
		if (Home.Households < Home.HousingUnits)
		{
			++Home.Households;
			return BuildingOrder[HomeCursor];
		}
	}
	return INDEX_NONE;
}

int32 FChronoPopulation::TakeFreeJob()
{
	for (; JobCursor < BuildingOrder.Num(); ++JobCursor)
	{
		FOccupancy& Work = Occupancy.FindChecked(BuildingOrder[JobCursor]);
		if (Work.Workers < Work.Jobs)
		{
			++Work.Workers;
			return BuildingOrder[JobCursor];
		}
	}
	return INDEX_NONE;
}

void FChronoPopulation::SyncBuildings(const FChronoBuildingSet& Buildings, const FChronoPopulationRules& Rules, TArray<int32>& OutHomelessHouseholds)
{
	OutHomelessHouseholds.Reset();

	// Capacities of the buildings that exist today.
	Occupancy.Reset();
	BuildingOrder.Reset();
	for (const TPair<int32, FChronoBuilding>& Pair : Buildings.GetBuildings())
	{
		FOccupancy Entry;
		Entry.Zone = Pair.Value.Zone;
		ComputeCapacity(Pair.Value, Rules, Entry.HousingUnits, Entry.Jobs);
		if (Entry.HousingUnits > 0)
		{
			Entry.ServiceMask = Buildings.GetServiceMaskAt(Pair.Value.Footprint.Center);
		}
		Occupancy.Add(Pair.Key, Entry);
		BuildingOrder.Add(Pair.Key);
	}

	// Homes and jobs are handed out in building id order, so the same city
	// always fills the same way.
	BuildingOrder.Sort();
	HomeCursor = 0;
	JobCursor = 0;

	// Who still has a home: the building may be gone, or smaller than before.
	for (int32 HouseholdId = 0; HouseholdId < Households.Num(); ++HouseholdId)
	{
		FChronoHousehold& Household = Households[HouseholdId];
		if (!Household.bInUse)
		{
			continue;
		}

		FOccupancy* Home = Occupancy.Find(Household.HomeBuildingId);
		if (Home && Home->Households < Home->HousingUnits)
		{
			++Home->Households;
		}
		else
		{
			Household.HomeBuildingId = INDEX_NONE;
			OutHomelessHouseholds.Add(HouseholdId);
		}
	}

	// Who still has a job.
	for (FChronoCitizen& Citizen : Citizens)
	{
		if (Citizen.HouseholdId == INDEX_NONE || Citizen.WorkBuildingId == INDEX_NONE)
		{
			continue;
		}

		FOccupancy* Work = Occupancy.Find(Citizen.WorkBuildingId);
		if (Work && Work->Workers < Work->Jobs)
		{
			++Work->Workers;
		}
		else
		{
			Citizen.WorkBuildingId = INDEX_NONE;
		}
	}
}

void FChronoPopulation::AgeAndRenew(FRandomStream& Random, const FChronoPopulationRules& Rules)
{
	using namespace ChronoPopulationPrivate;

	const float DayInYears = 1.0f / DaysPerYear;

	// Aging, retirement and death. Indexed loop: removing a citizen frees its
	// slot but never moves the others.
	for (int32 CitizenId = 0; CitizenId < Citizens.Num(); ++CitizenId)
	{
		FChronoCitizen& Citizen = Citizens[CitizenId];
		if (Citizen.HouseholdId == INDEX_NONE)
		{
			continue;
		}

		Citizen.AgeYears += DayInYears;

		// Mortality is low until MortalityStartAge, then doubles every few years.
		float AnnualMortality = Rules.BaseAnnualMortality;
		if (Citizen.AgeYears > Rules.MortalityStartAge)
		{
			AnnualMortality *= FMath::Pow(2.0f, (Citizen.AgeYears - Rules.MortalityStartAge) / FMath::Max(Rules.MortalityDoublingYears, 0.1f));
		}

		const float Draw = Random.FRand();
		if (Citizen.AgeYears >= Rules.MaxAge || Draw < AnnualMortality / DaysPerYear)
		{
			RemoveCitizen(CitizenId);
			++DeathsToday;
			continue;
		}

		if (Citizen.AgeYears >= Rules.RetirementAge && Citizen.WorkBuildingId != INDEX_NONE)
		{
			if (FOccupancy* Work = Occupancy.Find(Citizen.WorkBuildingId))
			{
				Work->Workers = FMath::Max(Work->Workers - 1, 0);
			}
			Citizen.WorkBuildingId = INDEX_NONE;
		}
	}

	// Births: a household with an adult of childbearing age, and room left.
	const float BirthChance = Rules.BirthsPerHouseholdPerYear / DaysPerYear;
	for (int32 HouseholdId = 0; HouseholdId < Households.Num(); ++HouseholdId)
	{
		if (!Households[HouseholdId].bInUse)
		{
			continue;
		}

		bool bFertile = false;
		for (const int32 CitizenId : Households[HouseholdId].Members)
		{
			const float Age = Citizens[CitizenId].AgeYears;
			bFertile = bFertile || (Age >= Rules.FertileAgeMin && Age <= Rules.FertileAgeMax);
		}

		const float Draw = Random.FRand();
		if (bFertile && Households[HouseholdId].Members.Num() < Rules.MaxHouseholdSize && Draw < BirthChance)
		{
			AddCitizen(HouseholdId, 0.0f);
			++BirthsToday;
		}
	}
}

void FChronoPopulation::AssignJobs(const FChronoPopulationRules& Rules)
{
	using namespace ChronoPopulationPrivate;

	for (FChronoCitizen& Citizen : Citizens)
	{
		if (Citizen.HouseholdId == INDEX_NONE || Citizen.WorkBuildingId != INDEX_NONE || !IsWorkingAge(Citizen.AgeYears, Rules))
		{
			continue;
		}

		const int32 WorkBuildingId = TakeFreeJob();
		if (WorkBuildingId == INDEX_NONE)
		{
			break;
		}
		Citizen.WorkBuildingId = WorkBuildingId;
	}

	// A household with nobody at work, although someone could work, counts its days.
	for (FChronoHousehold& Household : Households)
	{
		if (!Household.bInUse)
		{
			continue;
		}

		bool bAnyEmployed = false;
		bool bAnyWorkingAge = false;
		for (const int32 CitizenId : Household.Members)
		{
			bAnyEmployed = bAnyEmployed || Citizens[CitizenId].WorkBuildingId != INDEX_NONE;
			bAnyWorkingAge = bAnyWorkingAge || IsWorkingAge(Citizens[CitizenId].AgeYears, Rules);
		}

		Household.JoblessDays = (bAnyEmployed || !bAnyWorkingAge) ? 0 : Household.JoblessDays + 1;
	}
}

void FChronoPopulation::UpdateHappiness(const FChronoCityConditions& Conditions, const FChronoPopulationRules& Rules)
{
	using namespace ChronoPopulationPrivate;

	// Taxes above the neutral rate weigh on everybody, taxes below it please.
	const float TaxPoints = (Conditions.GetTaxRate(EChronoZoneType::Residential) - Rules.NeutralTaxRate) * 100.0f;
	const float TaxEffect = -TaxPoints * Rules.TaxPenaltyPerPoint;
	const float Adjust = FMath::Clamp(Rules.HappinessAdjustPerDay, 0.0f, 1.0f);

	// Household by household: everybody under one roof shares its services.
	for (const FChronoHousehold& Household : Households)
	{
		if (!Household.bInUse)
		{
			continue;
		}

		const FOccupancy* Home = Occupancy.Find(Household.HomeBuildingId);
		const float HomeEffect = Rules.GetServiceHappiness(Home ? Home->ServiceMask : 0);

		for (const int32 CitizenId : Household.Members)
		{
			FChronoCitizen& Citizen = Citizens[CitizenId];

			float Target = Rules.HappinessBase + TaxEffect + HomeEffect;
			if (Citizen.WorkBuildingId != INDEX_NONE)
			{
				// A job is good news, a long way to it less so.
				Target += Rules.EmployedBonus + Conditions.CommuteHappiness;
			}
			else if (IsWorkingAge(Citizen.AgeYears, Rules))
			{
				Target -= Rules.UnemployedPenalty;
			}

			// Moods change gradually, not overnight.
			Citizen.Happiness = FMath::Clamp(Citizen.Happiness + (FMath::Clamp(Target, 0.0f, 100.0f) - Citizen.Happiness) * Adjust, 0.0f, 100.0f);
		}
	}
}

void FChronoPopulation::Emigrate(FRandomStream& Random, const FChronoPopulationRules& Rules)
{
	for (int32 HouseholdId = 0; HouseholdId < Households.Num(); ++HouseholdId)
	{
		const FChronoHousehold& Household = Households[HouseholdId];
		if (!Household.bInUse)
		{
			continue;
		}

		float HappinessSum = 0.0f;
		for (const int32 CitizenId : Household.Members)
		{
			HappinessSum += Citizens[CitizenId].Happiness;
		}
		const float AverageHappiness = HappinessSum / FMath::Max(Household.Members.Num(), 1);

		const bool bWantsToLeave = Household.JoblessDays > Rules.JoblessDaysBeforeLeaving || AverageHappiness < Rules.UnhappyThreshold;
		const float Draw = Random.FRand();
		if (bWantsToLeave && Draw < Rules.EmigrationChancePerDay)
		{
			EmigrantsToday += Household.Members.Num();
			RemoveHousehold(HouseholdId);
		}
	}
}

void FChronoPopulation::Immigrate(FRandomStream& Random, const FChronoPopulationRules& Rules)
{
	using namespace ChronoPopulationPrivate;

	int32 FreeHomes = 0;
	int32 Jobs = 0;
	int32 FreeJobs = 0;
	for (const TPair<int32, FOccupancy>& Pair : Occupancy)
	{
		FreeHomes += FMath::Max(Pair.Value.HousingUnits - Pair.Value.Households, 0);
		Jobs += Pair.Value.Jobs;
		FreeJobs += FMath::Max(Pair.Value.Jobs - Pair.Value.Workers, 0);
	}
	if (FreeHomes <= 0)
	{
		return;
	}

	int32 WorkingAge = 0;
	int32 Unemployed = 0;
	int32 Population = 0;
	float HappinessSum = 0.0f;
	for (const FChronoCitizen& Citizen : Citizens)
	{
		if (Citizen.HouseholdId == INDEX_NONE)
		{
			continue;
		}
		++Population;
		HappinessSum += Citizen.Happiness;
		if (IsWorkingAge(Citizen.AgeYears, Rules))
		{
			++WorkingAge;
			Unemployed += (Citizen.WorkBuildingId == INDEX_NONE) ? 1 : 0;
		}
	}

	// How attractive the city is: jobs waiting for workers draw people in,
	// unemployment and unhappiness keep them away. A city with no job at all
	// still draws a few settlers, or it could never start.
	const float JobsWaiting = static_cast<float>(FreeJobs - Unemployed);
	const float JobFactor = FMath::Clamp(JobsWaiting / FMath::Max(10.0f, 0.1f * Jobs), 0.0f, 1.0f);
	const float UnemploymentRate = WorkingAge > 0 ? static_cast<float>(Unemployed) / WorkingAge : 0.0f;
	const float HappinessFactor = Population > 0 ? FMath::Clamp((HappinessSum / Population) / FMath::Max(Rules.HappinessBase, 1.0f), 0.2f, 1.5f) : 1.0f;

	float Attractiveness = Rules.MinAttractiveness + (1.0f - Rules.MinAttractiveness) * JobFactor;
	Attractiveness *= HappinessFactor * FMath::Clamp(1.0f - 3.0f * UnemploymentRate, 0.0f, 1.0f);
	Attractiveness = FMath::Clamp(Attractiveness, 0.0f, 1.0f);
	if (Attractiveness <= 0.0f)
	{
		return;
	}

	const int32 Arrivals = FMath::Min(FreeHomes, FMath::CeilToInt32(FreeHomes * Rules.ImmigrationRatePerDay * Attractiveness));
	for (int32 Arrival = 0; Arrival < Arrivals; ++Arrival)
	{
		const int32 HomeBuildingId = TakeFreeHome();
		if (HomeBuildingId == INDEX_NONE)
		{
			break;
		}

		const int32 HouseholdId = AddHousehold(HomeBuildingId);

		// One or two adults, up to three children.
		const int32 NumAdults = 1 + (Random.FRand() < 0.7f ? 1 : 0);
		for (int32 Adult = 0; Adult < NumAdults; ++Adult)
		{
			AddCitizen(HouseholdId, Random.FRandRange(20.0f, 45.0f));
		}

		const float ChildDraw = Random.FRand();
		const int32 NumChildren = ChildDraw < 0.35f ? 0 : (ChildDraw < 0.65f ? 1 : (ChildDraw < 0.9f ? 2 : 3));
		for (int32 Child = 0; Child < NumChildren; ++Child)
		{
			AddCitizen(HouseholdId, Random.FRandRange(0.0f, 15.0f));
		}

		for (const int32 CitizenId : Households[HouseholdId].Members)
		{
			Citizens[CitizenId].Happiness = Rules.HappinessBase;
		}
		ImmigrantsToday += Households[HouseholdId].Members.Num();
	}
}

void FChronoPopulation::ComputeStatsAndDemand(const FChronoCityConditions& Conditions, const FChronoPopulationRules& Rules)
{
	using namespace ChronoPopulationPrivate;

	Stats = FChronoPopulationStats();
	Stats.BirthsToday = BirthsToday;
	Stats.DeathsToday = DeathsToday;
	Stats.ImmigrantsToday = ImmigrantsToday;
	Stats.EmigrantsToday = EmigrantsToday;

	float HappinessSum = 0.0f;
	for (const FChronoCitizen& Citizen : Citizens)
	{
		if (Citizen.HouseholdId == INDEX_NONE)
		{
			continue;
		}

		++Stats.Population;
		HappinessSum += Citizen.Happiness;

		if (Citizen.AgeYears < Rules.WorkingAgeMin)
		{
			++Stats.Children;
		}
		else if (Citizen.AgeYears >= Rules.RetirementAge)
		{
			++Stats.Elderly;
		}
		else
		{
			++Stats.WorkingAge;
			if (Citizen.WorkBuildingId != INDEX_NONE)
			{
				++Stats.Employed;
			}
			else
			{
				++Stats.Unemployed;
			}
		}
	}
	Stats.AverageHappiness = Stats.Population > 0 ? HappinessSum / Stats.Population : 0.0f;

	for (const FChronoHousehold& Household : Households)
	{
		if (!Household.bInUse)
		{
			continue;
		}

		++Stats.Households;
		if (const FOccupancy* Home = Occupancy.Find(Household.HomeBuildingId))
		{
			for (int32 Service = 1; Service < static_cast<int32>(EChronoServiceType::Count); ++Service)
			{
				Stats.HouseholdsServed[Service] += (Home->ServiceMask >> Service) & 1u;
			}
		}
	}

	for (const TPair<int32, FOccupancy>& Pair : Occupancy)
	{
		const FOccupancy& Entry = Pair.Value;
		Stats.HousingUnits += Entry.HousingUnits;
		Stats.FreeHousing += FMath::Max(Entry.HousingUnits - Entry.Households, 0);
		Stats.Jobs += Entry.Jobs;
		Stats.FreeJobs += FMath::Max(Entry.Jobs - Entry.Workers, 0);
		Stats.JobsByZone[ZoneIndex(Entry.Zone)] += Entry.Jobs;
		Stats.WorkersByZone[ZoneIndex(Entry.Zone)] += Entry.Workers;
	}

	// ---- Demand ----
	const float HousingVacancy = Stats.HousingUnits > 0 ? static_cast<float>(Stats.FreeHousing) / Stats.HousingUnits : 0.0f;
	const float JobVacancy = Stats.Jobs > 0 ? static_cast<float>(Stats.FreeJobs) / Stats.Jobs : 0.0f;
	const float UnemploymentRate = Stats.WorkingAge > 0 ? static_cast<float>(Stats.Unemployed) / Stats.WorkingAge : 0.0f;

	auto TaxEffect = [&Conditions, &Rules](EChronoZoneType Zone)
	{
		return (Conditions.GetTaxRate(Zone) - Rules.NeutralTaxRate) * Rules.DemandTaxSensitivity;
	};

	Demand = FChronoZoneDemand();

	// Homes are wanted when jobs wait for workers, and not wanted while built
	// homes stand empty. The small constant lets an empty map start.
	Demand.Set(EChronoZoneType::Residential, 0.2f + 2.0f * JobVacancy - 4.0f * HousingVacancy - TaxEffect(EChronoZoneType::Residential));

	// Workplaces are wanted when people look for work, and not wanted while jobs stay open.
	float WorkDemand = 3.0f * UnemploymentRate - 3.0f * JobVacancy;
	if (Stats.Population > 0 && Stats.Jobs == 0)
	{
		WorkDemand += 0.5f;
	}
	WorkDemand = FMath::Clamp(WorkDemand, -1.0f, 1.0f);

	struct FSector
	{
		EChronoZoneType Zone;
		float TargetShare;
	};
	const FSector Sectors[4] =
	{
		{ EChronoZoneType::Commercial, Rules.TargetCommercialJobShare },
		{ EChronoZoneType::Industrial, Rules.TargetIndustrialJobShare },
		{ EChronoZoneType::Agricultural, Rules.TargetAgriculturalJobShare },
		{ EChronoZoneType::Civic, Rules.TargetCivicJobShare }
	};
	for (const FSector& Sector : Sectors)
	{
		const float Share = Stats.Jobs > 0 ? static_cast<float>(Stats.JobsByZone[ZoneIndex(Sector.Zone)]) / Stats.Jobs : 0.0f;

		// When work is wanted, the kinds of workplace the city lacks most are wanted most.
		const float MixEffect = WorkDemand > 0.0f ? 2.0f * (Sector.TargetShare - Share) : 0.0f;
		Demand.Set(Sector.Zone, WorkDemand + MixEffect - TaxEffect(Sector.Zone));
	}
}

void FChronoPopulation::StepDay(int64 DayIndex, const FChronoBuildingSet& Buildings, const FChronoCityConditions& Conditions, const FChronoPopulationRules& Rules)
{
	BirthsToday = 0;
	DeathsToday = 0;
	ImmigrantsToday = 0;
	EmigrantsToday = 0;

	// The day's draws depend only on the seed and the day: same inputs, same history.
	FRandomStream Random(Rules.Seed ^ static_cast<int32>((DayIndex * 2654435761LL) & 0x7FFFFFFF));

	// 1. The city may have changed since yesterday: recount homes and jobs.
	// Households whose home is gone move to a free one, or leave the city.
	TArray<int32> Homeless;
	SyncBuildings(Buildings, Rules, Homeless);
	for (const int32 HouseholdId : Homeless)
	{
		const int32 HomeBuildingId = TakeFreeHome();
		if (HomeBuildingId != INDEX_NONE)
		{
			Households[HouseholdId].HomeBuildingId = HomeBuildingId;
		}
		else
		{
			EmigrantsToday += Households[HouseholdId].Members.Num();
			RemoveHousehold(HouseholdId);
		}
	}

	// 2. A day of life.
	AgeAndRenew(Random, Rules);
	AssignJobs(Rules);
	UpdateHappiness(Conditions, Rules);
	Emigrate(Random, Rules);
	Immigrate(Random, Rules);

	// 3. What the city needs next.
	ComputeStatsAndDemand(Conditions, Rules);
}

void FChronoPopulation::Refresh(const FChronoBuildingSet& Buildings, const FChronoCityConditions& Conditions, const FChronoPopulationRules& Rules)
{
	BirthsToday = 0;
	DeathsToday = 0;
	ImmigrantsToday = 0;
	EmigrantsToday = 0;

	// Households left without a home here are rehoused by the next day step.
	TArray<int32> Homeless;
	SyncBuildings(Buildings, Rules, Homeless);
	ComputeStatsAndDemand(Conditions, Rules);
}

bool FChronoPopulation::CheckInvariants(FString& OutError) const
{
	TMap<int32, int32> HouseholdsPerHome;
	TMap<int32, int32> WorkersPerBuilding;
	int32 NumMembers = 0;

	for (int32 HouseholdId = 0; HouseholdId < Households.Num(); ++HouseholdId)
	{
		const FChronoHousehold& Household = Households[HouseholdId];
		if (!Household.bInUse)
		{
			if (Household.Members.Num() != 0)
			{
				OutError = FString::Printf(TEXT("Free household %d still has members"), HouseholdId);
				return false;
			}
			continue;
		}

		if (Household.Members.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Household %d is empty"), HouseholdId);
			return false;
		}

		for (const int32 CitizenId : Household.Members)
		{
			if (!Citizens.IsValidIndex(CitizenId) || Citizens[CitizenId].HouseholdId != HouseholdId)
			{
				OutError = FString::Printf(TEXT("Household %d lists citizen %d, who does not belong to it"), HouseholdId, CitizenId);
				return false;
			}
		}
		NumMembers += Household.Members.Num();

		if (Household.HomeBuildingId != INDEX_NONE)
		{
			++HouseholdsPerHome.FindOrAdd(Household.HomeBuildingId);
		}
	}

	int32 NumCitizens = 0;
	for (int32 CitizenId = 0; CitizenId < Citizens.Num(); ++CitizenId)
	{
		const FChronoCitizen& Citizen = Citizens[CitizenId];
		if (Citizen.HouseholdId == INDEX_NONE)
		{
			continue;
		}

		++NumCitizens;
		if (!Households.IsValidIndex(Citizen.HouseholdId) || !Households[Citizen.HouseholdId].bInUse)
		{
			OutError = FString::Printf(TEXT("Citizen %d belongs to missing household %d"), CitizenId, Citizen.HouseholdId);
			return false;
		}
		if (Citizen.WorkBuildingId != INDEX_NONE)
		{
			++WorkersPerBuilding.FindOrAdd(Citizen.WorkBuildingId);
		}
	}

	if (NumCitizens != NumMembers)
	{
		OutError = FString::Printf(TEXT("%d citizens but %d household members"), NumCitizens, NumMembers);
		return false;
	}
	if (NumCitizens + FreeCitizens.Num() != Citizens.Num())
	{
		OutError = FString::Printf(TEXT("Citizen slots do not add up: %d in use, %d free, %d total"), NumCitizens, FreeCitizens.Num(), Citizens.Num());
		return false;
	}

	for (const TPair<int32, int32>& Pair : HouseholdsPerHome)
	{
		const FOccupancy* Home = Occupancy.Find(Pair.Key);
		if (!Home || Pair.Value > Home->HousingUnits || Pair.Value != Home->Households)
		{
			OutError = FString::Printf(TEXT("Building %d houses %d households, expected at most %d (counted %d)"), Pair.Key, Pair.Value, Home ? Home->HousingUnits : 0, Home ? Home->Households : 0);
			return false;
		}
	}
	for (const TPair<int32, int32>& Pair : WorkersPerBuilding)
	{
		const FOccupancy* Work = Occupancy.Find(Pair.Key);
		if (!Work || Pair.Value > Work->Jobs || Pair.Value != Work->Workers)
		{
			OutError = FString::Printf(TEXT("Building %d employs %d workers, expected at most %d (counted %d)"), Pair.Key, Pair.Value, Work ? Work->Jobs : 0, Work ? Work->Workers : 0);
			return false;
		}
	}

	return true;
}

bool FChronoPopulation::Serialize(FArchive& Ar)
{
	// Bump when the binary layout changes.
	const int32 CurrentVersion = 1;
	// Sanity bound on counts read from a file.
	const int32 MaxCount = 16 * 1024 * 1024;

	int32 Version = CurrentVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != CurrentVersion)
	{
		Ar.SetError();
		return false;
	}

	if (Ar.IsLoading())
	{
		Reset();
	}

	int32 NumCitizens = Citizens.Num();
	int32 NumHouseholds = Households.Num();
	Ar << NumCitizens;
	Ar << NumHouseholds;

	bool bValid = !Ar.IsError() && NumCitizens >= 0 && NumCitizens <= MaxCount && NumHouseholds >= 0 && NumHouseholds <= MaxCount;
	if (bValid && Ar.IsLoading())
	{
		Citizens.SetNum(NumCitizens);
		Households.SetNum(NumHouseholds);
	}

	for (int32 Index = 0; bValid && Index < NumCitizens; ++Index)
	{
		FChronoCitizen& Citizen = Citizens[Index];
		Ar << Citizen.HouseholdId;
		Ar << Citizen.WorkBuildingId;
		Ar << Citizen.AgeYears;
		Ar << Citizen.Happiness;
		bValid = !Ar.IsError();
	}

	for (int32 Index = 0; bValid && Index < NumHouseholds; ++Index)
	{
		FChronoHousehold& Household = Households[Index];
		Ar << Household.bInUse;
		Ar << Household.HomeBuildingId;
		Ar << Household.Members;
		Ar << Household.JoblessDays;
		bValid = !Ar.IsError();
	}

	Ar << FreeCitizens;
	Ar << FreeHouseholds;
	bValid = bValid && !Ar.IsError();

	if (Ar.IsLoading())
	{
		// Every reference must point at something that exists and agrees.
		for (int32 HouseholdId = 0; bValid && HouseholdId < Households.Num(); ++HouseholdId)
		{
			const FChronoHousehold& Household = Households[HouseholdId];
			bValid = Household.bInUse ? Household.Members.Num() > 0 : Household.Members.Num() == 0;
			for (const int32 CitizenId : Household.Members)
			{
				bValid = bValid && Citizens.IsValidIndex(CitizenId) && Citizens[CitizenId].HouseholdId == HouseholdId;
			}
		}
		for (int32 CitizenId = 0; bValid && CitizenId < Citizens.Num(); ++CitizenId)
		{
			const FChronoCitizen& Citizen = Citizens[CitizenId];
			if (Citizen.HouseholdId != INDEX_NONE)
			{
				bValid = Households.IsValidIndex(Citizen.HouseholdId)
					&& Households[Citizen.HouseholdId].bInUse
					&& Households[Citizen.HouseholdId].Members.Contains(CitizenId)
					&& Citizen.AgeYears >= 0.0f;
			}
		}
		for (const int32 CitizenId : FreeCitizens)
		{
			bValid = bValid && Citizens.IsValidIndex(CitizenId) && Citizens[CitizenId].HouseholdId == INDEX_NONE;
		}
		for (const int32 HouseholdId : FreeHouseholds)
		{
			bValid = bValid && Households.IsValidIndex(HouseholdId) && !Households[HouseholdId].bInUse;
		}

		if (!bValid)
		{
			Reset();
			Ar.SetError();
			return false;
		}
	}

	return bValid;
}
