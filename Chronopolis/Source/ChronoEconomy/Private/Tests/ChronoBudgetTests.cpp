// Automation tests for the budget. Filter: "Chronopolis.Economy".

#include "ChronoBudget.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoBudgetTest, "Chronopolis.Economy.TaxesUpkeepAndProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoBudgetTest::RunTest(const FString& Parameters)
{
	FChronoEconomyRules Rules;
	FChronoBudget Budget;
	Budget.Reset(Rules, 0.09f);
	TestTrue(TEXT("A new game starts with the starting treasury"), Budget.GetTreasury() == Rules.StartingTreasury);

	// A town of 1000: 450 at work, of whom 150 in shops, 150 in industry, 100 on farms, 50 in civic buildings.
	FChronoPopulationStats Stats;
	Stats.Population = 1000;
	Stats.Employed = 450;
	Stats.AverageHappiness = 50.0f;
	Stats.WorkersByZone[static_cast<int32>(EChronoZoneType::Commercial)] = 150;
	Stats.WorkersByZone[static_cast<int32>(EChronoZoneType::Industrial)] = 150;
	Stats.WorkersByZone[static_cast<int32>(EChronoZoneType::Agricultural)] = 100;
	Stats.WorkersByZone[static_cast<int32>(EChronoZoneType::Civic)] = 50;
	const double RoadAreaM2 = 50000.0;

	Budget.StepDay(Stats, RoadAreaM2, 0.0, Rules);
	const FChronoLedger& Today = Budget.GetToday();

	// 450 x 1 x 9 % = 40.5; 150 x 2 x 9 % = 27; 150 x 2.5 x 9 % = 33.75; 100 x 1.5 x 9 % = 13.5; 50000 x 0.0008 = 40.
	TestTrue(TEXT("Residential tax"), FMath::IsNearlyEqual(Today.ResidentialTax, 40.5, 0.001));
	TestTrue(TEXT("Commercial tax"), FMath::IsNearlyEqual(Today.CommercialTax, 27.0, 0.001));
	TestTrue(TEXT("Industrial tax"), FMath::IsNearlyEqual(Today.IndustrialTax, 33.75, 0.001));
	TestTrue(TEXT("Agricultural tax"), FMath::IsNearlyEqual(Today.AgriculturalTax, 13.5, 0.001));
	TestTrue(TEXT("Road upkeep"), FMath::IsNearlyEqual(Today.RoadUpkeep, 40.0, 0.001));
	TestTrue(TEXT("The treasury moves by income minus expenses"), FMath::IsNearlyEqual(Budget.GetTreasury(), Rules.StartingTreasury + 114.75 - 40.0, 0.001));

	// Science and culture: 1 thousand x 0.1 + 50 civic workers x 0.02 = 1.1 each per day at neutral happiness.
	TestTrue(TEXT("Science accumulates"), FMath::IsNearlyEqual(Budget.GetScience(), 1.1, 0.001));
	TestTrue(TEXT("Culture accumulates"), FMath::IsNearlyEqual(Budget.GetCulture(), 1.1, 0.001));

	// The month closes after DaysPerMonth days.
	for (int32 Day = 1; Day < Rules.DaysPerMonth; ++Day)
	{
		Budget.StepDay(Stats, RoadAreaM2, 0.0, Rules);
	}
	TestEqual(TEXT("A new month starts"), Budget.GetDayOfMonth(), 0);
	TestTrue(TEXT("Last month holds thirty days of income"), FMath::IsNearlyEqual(Budget.GetLastMonth().GetIncome(), 30.0 * 114.75, 0.01));

	// Tax rates are clamped, and civic buildings are never taxed.
	Budget.SetTaxRate(EChronoZoneType::Commercial, 0.9f, Rules);
	TestTrue(TEXT("Tax rates are capped"), FMath::IsNearlyEqual(Budget.GetTaxRate(EChronoZoneType::Commercial), Rules.MaxTaxRate, 0.0001f));
	Budget.SetTaxRate(EChronoZoneType::Civic, 0.2f, Rules);
	TestTrue(TEXT("Civic buildings pay no tax"), Budget.GetTaxRate(EChronoZoneType::Civic) == 0.0f);

	// No tax, no income; the roads still cost.
	for (const EChronoZoneType Zone : { EChronoZoneType::Residential, EChronoZoneType::Commercial, EChronoZoneType::Industrial, EChronoZoneType::Agricultural })
	{
		Budget.SetTaxRate(Zone, 0.0f, Rules);
	}
	const double Before = Budget.GetTreasury();
	Budget.StepDay(Stats, RoadAreaM2, 0.0, Rules);
	TestTrue(TEXT("Without taxes the treasury only pays"), FMath::IsNearlyEqual(Budget.GetTreasury(), Before - 40.0, 0.001));

	// Construction: paid at once, shown in the ledger of the next day, never paid twice.
	Budget.SetTaxRate(EChronoZoneType::Residential, 0.09f, Rules);
	const double BeforeBuilding = Budget.GetTreasury();
	TestTrue(TEXT("An affordable construction is paid"), Budget.Spend(500.0));
	TestTrue(TEXT("The treasury drops by the price"), FMath::IsNearlyEqual(Budget.GetTreasury(), BeforeBuilding - 500.0, 0.001));
	TestFalse(TEXT("A construction beyond the treasury is refused"), Budget.Spend(1.0e9));
	Budget.StepDay(Stats, 0.0, 12.0, Rules);
	TestTrue(TEXT("The construction appears in the ledger"), FMath::IsNearlyEqual(Budget.GetToday().Construction, 500.0, 0.001));
	TestTrue(TEXT("Service upkeep is paid"), FMath::IsNearlyEqual(Budget.GetToday().ServiceUpkeep, 12.0, 0.001));
	TestTrue(TEXT("The construction is not paid twice"), FMath::IsNearlyEqual(Budget.GetTreasury(), BeforeBuilding - 500.0 + 40.5 - 12.0, 0.001));

	// Loans: money now, repaid with interest in equal daily payments.
	const double BeforeLoan = Budget.GetTreasury();
	TestTrue(TEXT("A loan is granted"), Budget.TakeLoan(10000.0, Rules));
	TestTrue(TEXT("The treasury receives the loan"), FMath::IsNearlyEqual(Budget.GetTreasury(), BeforeLoan + 10000.0, 0.001));
	TestTrue(TEXT("The debt includes the interest"), FMath::IsNearlyEqual(Budget.GetDebt(), 11000.0, 0.001));
	TestFalse(TEXT("A loan beyond the debt limit is refused"), Budget.TakeLoan(45000.0, Rules));
	Budget.StepDay(Stats, 0.0, 0.0, Rules);
	TestTrue(TEXT("One day repays one payment"), FMath::IsNearlyEqual(Budget.GetToday().LoanRepayment, 11000.0 / Rules.LoanTermDays, 0.001));
	for (int32 Day = 1; Day < Rules.LoanTermDays; ++Day)
	{
		Budget.StepDay(Stats, 0.0, 0.0, Rules);
	}
	TestTrue(TEXT("The loan is repaid at the end of its term"), Budget.GetNumLoans() == 0 && Budget.GetDebt() < 0.001);

	// Save and load, with a loan running.
	TestTrue(TEXT("A second loan is granted"), Budget.TakeLoan(2000.0, Rules));
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Budget is saved"), Budget.Serialize(Writer));
	FChronoBudget Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Budget is loaded"), Loaded.Serialize(Reader));
	TestTrue(TEXT("Loaded treasury is the same"), Loaded.GetTreasury() == Budget.GetTreasury());
	TestTrue(TEXT("Loaded science is the same"), Loaded.GetScience() == Budget.GetScience());
	TestTrue(TEXT("Loaded debt is the same"), Loaded.GetDebt() == Budget.GetDebt());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
