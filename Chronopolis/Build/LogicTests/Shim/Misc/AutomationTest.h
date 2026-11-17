#pragma once
#include "CoreMinimal.h"
enum class EAutomationTestFlags : uint32 { EditorContext = 1, ClientContext = 2, ProductFilter = 4 };
inline EAutomationTestFlags operator|(EAutomationTestFlags A, EAutomationTestFlags B) { return static_cast<EAutomationTestFlags>(static_cast<uint32>(A) | static_cast<uint32>(B)); }
class FAutomationTestBase { public:
	const char* Name; int32 NumChecks = 0; int32 NumFailures = 0;
	explicit FAutomationTestBase(const char* InName) : Name(InName) { Registry().push_back(this); }
	virtual ~FAutomationTestBase() = default;
	virtual bool RunTest(const FString& Parameters) = 0;
	static std::vector<FAutomationTestBase*>& Registry() { static std::vector<FAutomationTestBase*> R; return R; }
	void Fail(const char* What) { ++NumFailures; std::printf("    FAIL: %s\n", What); }
	bool TestTrue(const char* What, bool V) { ++NumChecks; if (!V) Fail(What); return V; }
	bool TestFalse(const char* What, bool V) { ++NumChecks; if (V) Fail(What); return !V; }
	template <typename A, typename B> bool TestEqual(const char* What, const A& Actual, const B& Expected) { ++NumChecks; const bool Ok = (Actual == Expected); if (!Ok) { Fail(What); std::printf("          actual %lld, expected %lld\n", (long long)Actual, (long long)Expected); } return Ok; }
	void AddInfo(const FString& S) { std::printf("    info: %s\n", *S); }
	void AddError(const FString& S) { ++NumFailures; std::printf("    ERROR: %s\n", *S); }
};
#define IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, PrettyName, Flags) \
	class Class : public FAutomationTestBase { public: Class() : FAutomationTestBase(PrettyName) {} bool RunTest(const FString& Parameters) override; }; \
	static Class Class##Instance;
