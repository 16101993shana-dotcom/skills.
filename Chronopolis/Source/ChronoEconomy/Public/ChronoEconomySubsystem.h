// Runs the budget once per simulated day and lets the player set taxes.
// Console: "Chrono.City.Stats" prints population and budget,
// "Chrono.Tax <zone 1-4> <percent>" sets a tax rate.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoBudget.h"
#include "ChronoEconomySubsystem.generated.h"

struct FChronoPopulationStats;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FChronoOnBudgetChanged);

UCLASS()
class CHRONOECONOMY_API UChronoEconomySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	const FChronoBudget& GetBudget() const { return Budget; }

	/** Sets the tax of a zone, in percent. Clamped to the maximum of the project settings. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Economy")
	void SetTaxRatePercent(EChronoZoneType Zone, float Percent);

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Economy")
	float GetTaxRatePercent(EChronoZoneType Zone) const;

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Economy")
	double GetTreasury() const { return Budget.GetTreasury(); }

	/** Income and expenses of the last full month. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Economy")
	double GetLastMonthIncome() const { return Budget.GetLastMonth().GetIncome(); }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Economy")
	double GetLastMonthExpenses() const { return Budget.GetLastMonth().GetExpenses(); }

	/** Science and culture accumulated so far, and what one day adds. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Economy")
	double GetScience() const { return Budget.GetScience(); }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Economy")
	double GetCulture() const { return Budget.GetCulture(); }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Economy")
	double GetSciencePerDay() const { return Budget.GetSciencePerDay(); }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Economy")
	double GetCulturePerDay() const { return Budget.GetCulturePerDay(); }

	/** Writes population and budget to the log. */
	void LogCityStats() const;

	/** Save and load. Returns false on invalid data. */
	bool SerializeEconomy(FArchive& Ar);

	/** Fired once a day, after taxes and upkeep. */
	UPROPERTY(BlueprintAssignable, Category = "Chronopolis|Economy")
	FChronoOnBudgetChanged OnBudgetChanged;

private:
	void HandleDayStepped(int64 DayIndex, const FChronoPopulationStats& Stats);
	void PushConditions() const;

	FChronoBudget Budget;
	FChronoEconomyRules Rules;
	FDelegateHandle DaySteppedHandle;
};
