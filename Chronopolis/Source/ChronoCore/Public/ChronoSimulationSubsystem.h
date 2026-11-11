// Fixed-timestep simulation clock, decoupled from rendering.
// Every simulation system subscribes to OnSimTick instead of using Actor Tick.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoSimulationSubsystem.generated.h"

UENUM(BlueprintType)
enum class EChronoSimSpeed : uint8
{
	Paused,
	Normal,
	Fast,
	Fastest
};

/** Native delegate: (TickIndex, FixedDeltaSeconds). Called once per fixed step. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FChronoOnSimTick, int64, float);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChronoOnSimSpeedChanged, EChronoSimSpeed, NewSpeed);

UCLASS()
class CHRONOCORE_API UChronoSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Simulation")
	void SetSpeed(EChronoSimSpeed NewSpeed);

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Simulation")
	EChronoSimSpeed GetSpeed() const { return Speed; }

	/** Number of fixed steps executed since the start of the game (saved and restored). */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Simulation")
	int64 GetTickIndex() const { return TickIndex; }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Simulation")
	float GetFixedDeltaSeconds() const { return FixedDeltaSeconds; }

	/** Simulated seconds elapsed, derived from the tick index (deterministic). */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Simulation")
	double GetSimTimeSeconds() const { return static_cast<double>(TickIndex) * FixedDeltaSeconds; }

	/** Fraction [0,1) of the next step already elapsed. Used by rendering to interpolate. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Simulation")
	float GetInterpolationAlpha() const;

	/** Used by the save system when loading a game. */
	void RestoreTickIndex(int64 InTickIndex);

	FChronoOnSimTick OnSimTick;

	UPROPERTY(BlueprintAssignable, Category = "Chronopolis|Simulation")
	FChronoOnSimSpeedChanged OnSpeedChanged;

private:
	float GetSpeedMultiplier() const;

	EChronoSimSpeed Speed = EChronoSimSpeed::Normal;
	int64 TickIndex = 0;
	float FixedDeltaSeconds = 0.1f;
	float Accumulator = 0.0f;
	int32 MaxStepsPerFrame = 8;
};
