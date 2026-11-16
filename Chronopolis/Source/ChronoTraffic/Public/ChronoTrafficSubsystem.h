// Runs traffic in the world: rebuilds the lanes when roads change, sends the
// commuters of the population on the road through each traffic day, advances
// the vehicles with the simulation clock, and reports commute times back to
// the population. Console: "Chrono.Traffic.Stats".

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoCommute.h"
#include "ChronoTrafficSim.h"
#include "ChronoTrafficSubsystem.generated.h"

class AChronoTrafficRenderActor;
struct FChronoRoadChangeSet;

UCLASS()
class CHRONOTRAFFIC_API UChronoTrafficSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	const FChronoTrafficSim& GetSim() const { return Sim; }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Traffic")
	int32 GetNumVehicles() const { return Sim.GetStats().ActiveVehicles; }

	/** Average duration of recent trips, in seconds. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Traffic")
	float GetAverageTripSeconds() const { return Sim.GetStats().AverageTripSeconds; }

	/** Average speed of the vehicles on the road, in km/h. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Traffic")
	float GetAverageSpeedKmh() const { return Sim.GetStats().AverageSpeedCmPerS * 0.036f; }

	/** Moment of the traffic day, from 0 to 1 (0.2 to 0.35 is the morning rush, 0.65 to 0.8 the evening one). */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Traffic")
	float GetDayFraction() const { return static_cast<float>(DayFraction); }

	/** Writes traffic figures to the log. */
	void LogStats() const;

private:
	void HandleSimTick(int64 TickIndex, float FixedDeltaSeconds);
	void HandleRoadNetworkChanged(const FChronoRoadChangeSet& Changes);
	void RequestCommuteTrips(double FromDayFraction, double ToDayFraction);

	FChronoTrafficSim Sim;
	FChronoTrafficRules Rules;
	FChronoCommuteRules CommuteRules;

	/** Lanes must be rebuilt before the next step. True at start, so the first step builds them. */
	bool bNetworkDirty = true;

	double DayFraction = 0.0;
	float TrafficDaySeconds = 600.0f;

	FDelegateHandle SimTickHandle;
	FDelegateHandle RoadsChangedHandle;

	UPROPERTY(Transient)
	TObjectPtr<AChronoTrafficRenderActor> RenderActor;
};
