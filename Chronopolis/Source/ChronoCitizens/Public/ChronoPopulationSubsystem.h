// Runs the population simulation once per simulated day, feeds the demand for
// each zone back to the buildings, and publishes the day's statistics.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoPopulation.h"
#include "ChronoPopulationSubsystem.generated.h"

/** Called after each simulated day: (day index, statistics of that day). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FChronoOnDayStepped, int64, const FChronoPopulationStats&);

UCLASS()
class CHRONOCITIZENS_API UChronoPopulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	const FChronoPopulation& GetPopulation() const { return Population; }
	const FChronoPopulationStats& GetStats() const { return Population.GetStats(); }
	const FChronoPopulationRules& GetRules() const { return Rules; }

	/** Tax rates and other conditions set by the economy. Applied from the next day. */
	void SetConditions(const FChronoCityConditions& InConditions) { Conditions = InConditions; }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Population")
	int32 GetPopulationCount() const { return Population.GetStats().Population; }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Population")
	int32 GetHouseholdCount() const { return Population.GetStats().Households; }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Population")
	int32 GetEmployedCount() const { return Population.GetStats().Employed; }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Population")
	int32 GetUnemployedCount() const { return Population.GetStats().Unemployed; }

	/** Average happiness of the inhabitants, from 0 to 100. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Population")
	float GetAverageHappiness() const { return Population.GetStats().AverageHappiness; }

	/** Demand for a zone, from -1 (far too much) to +1 (badly needed): the demand bars of the interface. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Population")
	float GetZoneDemand(EChronoZoneType Zone) const;

	/** Number of days simulated since the start of the game. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Population")
	int64 GetCurrentDay() const { return CurrentDay; }

	/** Save and load. Loading replaces the whole population. Returns false on invalid data. */
	bool SerializePopulation(FArchive& Ar);

	FChronoOnDayStepped OnDayStepped;

private:
	void HandleSimTick(int64 TickIndex, float FixedDeltaSeconds);
	void PushDemand() const;

	FChronoPopulation Population;
	FChronoPopulationRules Rules;
	FChronoCityConditions Conditions;

	int32 TicksPerDay = 10;
	int64 CurrentDay = 0;
	FDelegateHandle SimTickHandle;
};
