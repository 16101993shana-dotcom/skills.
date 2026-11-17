// Checks the civilization data and the building definitions of the project
// settings, as the engine loaded them from Config/DefaultGame.ini.
// Filter: "Chronopolis.Civ.ProjectData".
//
// Outside the engine, Build/LogicTests runs the same checks on the same file.

#include "ChronoBuildingTypes.h"
#include "ChronoProgression.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoCivProjectDataTest, "Chronopolis.Civ.ProjectData.IsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoCivProjectDataTest::RunTest(const FString& Parameters)
{
	const FChronoCivData Data = FChronoCivData::FromSettings();
	TArray<FString> Errors;
	Data.Validate(Errors);
	Data.ValidateBuildings(UChronoBuildingSettings::Get()->Definitions, Errors);
	for (const FString& Error : Errors)
	{
		AddError(Error);
	}
	TestEqual(TEXT("Seven eras"), Data.Eras.Num(), 7);
	TestTrue(TEXT("Sumer, Egypt and Rome"), Data.FindCivilization(FName("Civ_Sumer")) && Data.FindCivilization(FName("Civ_Egypt")) && Data.FindCivilization(FName("Civ_Rome")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
