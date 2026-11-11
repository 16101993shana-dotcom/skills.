#include "ChronoSimulationSubsystem.h"

#include "ChronoCore.h"
#include "ChronoSettings.h"

void UChronoSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UChronoSettings* Settings = UChronoSettings::Get();
	FixedDeltaSeconds = 1.0f / static_cast<float>(FMath::Max(1, Settings->SimulationTicksPerSecond));
	MaxStepsPerFrame = FMath::Max(1, Settings->MaxSimStepsPerFrame);
}

bool UChronoSimulationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UChronoSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UChronoSimulationSubsystem, STATGROUP_Tickables);
}

void UChronoSimulationSubsystem::Tick(float DeltaTime)
{
	// No Super::Tick: the base declaration comes from FTickableGameObject and is pure virtual.
	if (Speed == EChronoSimSpeed::Paused)
	{
		return;
	}

	Accumulator += DeltaTime * GetSpeedMultiplier();

	int32 Steps = 0;
	while (Accumulator >= FixedDeltaSeconds && Steps < MaxStepsPerFrame)
	{
		Accumulator -= FixedDeltaSeconds;
		++TickIndex;
		++Steps;
		OnSimTick.Broadcast(TickIndex, FixedDeltaSeconds);
	}

	// The frame was too slow to keep up: drop the backlog so the simulation
	// slows down instead of freezing the game with ever more steps per frame.
	if (Accumulator >= FixedDeltaSeconds)
	{
		UE_LOG(LogChrono, Verbose, TEXT("Simulation is behind, dropping %.3f s of backlog"), Accumulator);
		Accumulator = 0.0f;
	}
}

void UChronoSimulationSubsystem::SetSpeed(EChronoSimSpeed NewSpeed)
{
	if (Speed == NewSpeed)
	{
		return;
	}

	Speed = NewSpeed;
	OnSpeedChanged.Broadcast(Speed);
}

float UChronoSimulationSubsystem::GetInterpolationAlpha() const
{
	return FMath::Clamp(Accumulator / FixedDeltaSeconds, 0.0f, 1.0f);
}

void UChronoSimulationSubsystem::RestoreTickIndex(int64 InTickIndex)
{
	TickIndex = FMath::Max<int64>(0, InTickIndex);
	Accumulator = 0.0f;
}

float UChronoSimulationSubsystem::GetSpeedMultiplier() const
{
	const UChronoSettings* Settings = UChronoSettings::Get();
	switch (Speed)
	{
	case EChronoSimSpeed::Normal:
		return Settings->NormalSpeedMultiplier;
	case EChronoSimSpeed::Fast:
		return Settings->FastSpeedMultiplier;
	case EChronoSimSpeed::Fastest:
		return Settings->FastestSpeedMultiplier;
	default:
		return 0.0f;
	}
}
