#include "ChronoEconomySubsystem.h"

#include "ChronoBuildingSubsystem.h"
#include "ChronoCore.h"
#include "ChronoPopulationSubsystem.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoRoadTypes.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace ChronoEconomySubsystemPrivate
{
	void StatsFromConsole(UWorld* World)
	{
		if (const UChronoEconomySubsystem* Economy = World ? World->GetSubsystem<UChronoEconomySubsystem>() : nullptr)
		{
			Economy->LogCityStats();
		}
	}

	void TaxFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		UChronoEconomySubsystem* Economy = World ? World->GetSubsystem<UChronoEconomySubsystem>() : nullptr;
		if (!Economy || Args.Num() < 2)
		{
			UE_LOG(LogChrono, Warning, TEXT("Usage: Chrono.Tax <zone: 1 residential, 2 commercial, 3 industrial, 4 agricultural> <percent>"));
			return;
		}

		const int32 ZoneIndex = FCString::Atoi(*Args[0]);
		if (ZoneIndex < 1 || ZoneIndex > 4)
		{
			UE_LOG(LogChrono, Warning, TEXT("Chrono.Tax: the zone must be between 1 and 4"));
			return;
		}

		const EChronoZoneType Zone = static_cast<EChronoZoneType>(ZoneIndex);
		Economy->SetTaxRatePercent(Zone, FCString::Atof(*Args[1]));
		UE_LOG(LogChrono, Display, TEXT("Tax of zone %d set to %.1f %%"), ZoneIndex, Economy->GetTaxRatePercent(Zone));
	}

	void LoanFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		UChronoEconomySubsystem* Economy = World ? World->GetSubsystem<UChronoEconomySubsystem>() : nullptr;
		if (!Economy || Args.Num() < 1)
		{
			UE_LOG(LogChrono, Warning, TEXT("Usage: Chrono.Loan <amount>"));
			return;
		}

		const double Amount = FCString::Atod(*Args[0]);
		if (Economy->TakeLoan(Amount))
		{
			UE_LOG(LogChrono, Display, TEXT("Borrowed %.0f. Debt: %.0f, treasury: %.0f"), Amount, Economy->GetDebt(), Economy->GetTreasury());
		}
		else
		{
			UE_LOG(LogChrono, Warning, TEXT("Loan refused: it would exceed the debt limit"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs LoanCommand(
		TEXT("Chrono.Loan"),
		TEXT("Chrono.Loan <amount>: borrows money, repaid with interest day by day."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoanFromConsole));

	FAutoConsoleCommandWithWorld StatsCommand(
		TEXT("Chrono.City.Stats"),
		TEXT("Prints the population, the demand per zone and the budget."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&StatsFromConsole));

	FAutoConsoleCommandWithWorldAndArgs TaxCommand(
		TEXT("Chrono.Tax"),
		TEXT("Chrono.Tax <zone 1-4> <percent>: sets the tax rate of a zone."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TaxFromConsole));
}

void UChronoEconomySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The budget taxes the population and pays for the roads.
	Collection.InitializeDependency<UChronoRoadSubsystem>();
	UChronoPopulationSubsystem* Population = Collection.InitializeDependency<UChronoPopulationSubsystem>();
	Super::Initialize(Collection);

	Rules = FChronoEconomyRules::FromSettings();

	if (Population)
	{
		Budget.Reset(Rules, Population->GetRules().NeutralTaxRate);
		Population->SetConditions(Budget.GetConditions());
		DaySteppedHandle = Population->OnDayStepped.AddUObject(this, &UChronoEconomySubsystem::HandleDayStepped);
	}
}

void UChronoEconomySubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		if (UChronoPopulationSubsystem* Population = World->GetSubsystem<UChronoPopulationSubsystem>())
		{
			Population->OnDayStepped.Remove(DaySteppedHandle);
		}
	}
	DaySteppedHandle.Reset();

	Super::Deinitialize();
}

bool UChronoEconomySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChronoEconomySubsystem::SetTaxRatePercent(EChronoZoneType Zone, float Percent)
{
	Budget.SetTaxRate(Zone, Percent / 100.0f, Rules);
	PushConditions();
}

float UChronoEconomySubsystem::GetTaxRatePercent(EChronoZoneType Zone) const
{
	return Zone < EChronoZoneType::Count ? Budget.GetTaxRate(Zone) * 100.0f : 0.0f;
}

bool UChronoEconomySubsystem::Spend(double Cost)
{
	if (!Budget.Spend(Cost))
	{
		return false;
	}
	OnBudgetChanged.Broadcast();
	return true;
}

double UChronoEconomySubsystem::EstimateRoadCost(const FChronoRoadBuildRequest& Request) const
{
	return ChronoEconomy::EstimateRoadCost(Request, Rules);
}

bool UChronoEconomySubsystem::TakeLoan(double Amount)
{
	if (!Budget.TakeLoan(Amount, Rules))
	{
		return false;
	}
	OnBudgetChanged.Broadcast();
	return true;
}

void UChronoEconomySubsystem::HandleDayStepped(int64 DayIndex, const FChronoPopulationStats& Stats)
{
	const UChronoRoadSubsystem* Roads = GetWorld() ? GetWorld()->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	const double RoadAreaM2 = Roads ? ChronoEconomy::ComputeRoadAreaM2(Roads->GetGraph()) : 0.0;

	const UChronoBuildingSubsystem* Buildings = GetWorld() ? GetWorld()->GetSubsystem<UChronoBuildingSubsystem>() : nullptr;
	const double ServiceUpkeep = Buildings ? Buildings->GetBuildingSet().GetTotalUpkeepPerDay() : 0.0;

	Budget.StepDay(Stats, RoadAreaM2, ServiceUpkeep, Rules);
	OnBudgetChanged.Broadcast();
}

void UChronoEconomySubsystem::PushConditions() const
{
	if (UChronoPopulationSubsystem* Population = GetWorld() ? GetWorld()->GetSubsystem<UChronoPopulationSubsystem>() : nullptr)
	{
		Population->SetConditions(Budget.GetConditions());
	}
}

void UChronoEconomySubsystem::LogCityStats() const
{
	const UChronoPopulationSubsystem* Population = GetWorld() ? GetWorld()->GetSubsystem<UChronoPopulationSubsystem>() : nullptr;
	if (!Population)
	{
		return;
	}

	const FChronoPopulationStats& Stats = Population->GetStats();
	UE_LOG(LogChrono, Display, TEXT("Day %lld: %d inhabitants in %d households (%d children, %d of working age, %d elderly), happiness %.0f"),
		Population->GetCurrentDay(), Stats.Population, Stats.Households, Stats.Children, Stats.WorkingAge, Stats.Elderly, Stats.AverageHappiness);
	UE_LOG(LogChrono, Display, TEXT("  Homes: %d, %d free. Jobs: %d, %d free. Employed: %d, unemployed: %d"),
		Stats.HousingUnits, Stats.FreeHousing, Stats.Jobs, Stats.FreeJobs, Stats.Employed, Stats.Unemployed);
	UE_LOG(LogChrono, Display, TEXT("  Demand: residential %+.2f, commercial %+.2f, industrial %+.2f, agricultural %+.2f, civic %+.2f"),
		Population->GetZoneDemand(EChronoZoneType::Residential), Population->GetZoneDemand(EChronoZoneType::Commercial),
		Population->GetZoneDemand(EChronoZoneType::Industrial), Population->GetZoneDemand(EChronoZoneType::Agricultural),
		Population->GetZoneDemand(EChronoZoneType::Civic));
	UE_LOG(LogChrono, Display, TEXT("  Households served: water %d, health %d, security %d, fire %d, education %d (of %d)"),
		Stats.HouseholdsServed[static_cast<int32>(EChronoServiceType::Water)], Stats.HouseholdsServed[static_cast<int32>(EChronoServiceType::Health)],
		Stats.HouseholdsServed[static_cast<int32>(EChronoServiceType::Security)], Stats.HouseholdsServed[static_cast<int32>(EChronoServiceType::Fire)],
		Stats.HouseholdsServed[static_cast<int32>(EChronoServiceType::Education)], Stats.Households);
	UE_LOG(LogChrono, Display, TEXT("  Treasury: %.0f, debt: %.0f. Last month: income %.0f, expenses %.0f"),
		Budget.GetTreasury(), Budget.GetDebt(), Budget.GetLastMonth().GetIncome(), Budget.GetLastMonth().GetExpenses());
	UE_LOG(LogChrono, Display, TEXT("  Science: %.1f (+%.2f per day). Culture: %.1f (+%.2f per day)"),
		Budget.GetScience(), Budget.GetSciencePerDay(), Budget.GetCulture(), Budget.GetCulturePerDay());
}

bool UChronoEconomySubsystem::SerializeEconomy(FArchive& Ar)
{
	if (!Ar.IsLoading())
	{
		return Budget.Serialize(Ar);
	}

	FChronoBudget Loaded;
	if (!Loaded.Serialize(Ar))
	{
		UE_LOG(LogChrono, Error, TEXT("Budget data is invalid, keeping the current budget"));
		return false;
	}

	Budget = Loaded;
	PushConditions();
	OnBudgetChanged.Broadcast();
	return true;
}
