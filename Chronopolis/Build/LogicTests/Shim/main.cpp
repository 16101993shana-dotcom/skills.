#include "Misc/AutomationTest.h"
int main() {
	int32 Failed = 0, Checks = 0;
	for (FAutomationTestBase* Test : FAutomationTestBase::Registry()) {
		std::printf("%s\n", Test->Name);
		Test->RunTest(FString());
		Checks += Test->NumChecks;
		std::printf("  -> %s (%d checks, %d failures)\n", Test->NumFailures == 0 ? "PASSED" : "FAILED", Test->NumChecks, Test->NumFailures);
		if (Test->NumFailures > 0) ++Failed;
	}
	std::printf("\n%zu tests, %d checks, %d failed\n", FAutomationTestBase::Registry().size(), Checks, Failed);
	return Failed == 0 ? 0 : 1;
}
