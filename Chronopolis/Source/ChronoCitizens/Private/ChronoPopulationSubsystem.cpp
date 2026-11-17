#include "ChronoPopulationSubsystem.h"

#include "ChronoBuildingSubsystem.h"
#include "ChronoCore.h"
#include "ChronoPopulationSettings.h"
#include "ChronoSimulationSubsystem.h"
#include "Engine/World.h"

void UChronoPopulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// People live and work in buildings, at the pace of the simulation clock.
	Collection.InitializeDependency<UChronoBuildingSubsystem>();
	UChronoSimulationSubsystem* Simulation = Collection.InitializeDependency<UChronoSimulationSubsystem>();
	Super::Initialize(Collection);

	Rules = FChronoPopulationRules::FromSettings();
	TicksPerDay = FMath::Max(UChronoPopulationSettings::Get()->TicksPerDay, 1);

	if (Simulation)
	{
		SimTickHandle = Simulation->OnSimTick.AddUObject(this, &UChronoPopulationSubsystem::HandleSimTick);
	}
}

void UChronoPopulationSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		if (UChronoSimulationSubsystem* Simulation = World->GetSubsystem<UChronoSimulationSubsystem>())
		{
			Simulation->OnSimTick.Remove(SimTickHandle);
		}
	}
	SimTickHandle.Reset();

	Super::Deinitialize();
}

bool UChronoPopulationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChronoPopulationSubsystem::SetConditions(const FChronoCityConditions& InConditions)
{
	// The economy owns the tax rates; what traffic reported stays as it is.
	const float CommuteHappiness = Conditions.CommuteHappiness;
	Conditions = InConditions;
	Conditions.CommuteHappiness = CommuteHappiness;
}

float UChronoPopulationSubsystem::GetZoneDemand(EChronoZoneType Zone) const
{
	return Zone < EChronoZoneType::Count ? Population.GetDemand().Get(Zone) : 0.0f;
}

void UChronoPopulationSubsystem::HandleSimTick(int64 TickIndex, float FixedDeltaSeconds)
{
	if (TickIndex % TicksPerDay != 0)
	{
		return;
	}

	const UChronoBuildingSubsystem* Buildings = GetWorld() ? GetWorld()->GetSubsystem<UChronoBuildingSubsystem>() : nullptr;
	if (!Buildings)
	{
		return;
	}

	// The day number comes from the tick index, so a loaded game resumes on the right day.
	CurrentDay = TickIndex / TicksPerDay;
	Population.StepDay(CurrentDay, Buildings->GetBuildingSet(), Conditions, Rules);

	PushDemand();
	OnDayStepped.Broadcast(CurrentDay, Population.GetStats());
}

void UChronoPopulationSubsystem::PushDemand() const
{
	if (UChronoBuildingSubsystem* Buildings = GetWorld() ? GetWorld()->GetSubsystem<UChronoBuildingSubsystem>() : nullptr)
	{
		Buildings->SetZoneDemand(Population.GetDemand());
	}
}

bool UChronoPopulationSubsystem::SerializePopulation(FArchive& Ar)
{
	if (!Ar.IsLoading())
	{
		return Population.Serialize(Ar);
	}

	FChronoPopulation Loaded;
	if (!Loaded.Serialize(Ar))
	{
		UE_LOG(LogChrono, Error, TEXT("Population data is invalid, keeping the current population"));
		return false;
	}

	Population = MoveTemp(Loaded);

	// Homes, jobs, statistics and demand are derived from the buildings, which are loaded before the population.
	if (const UChronoBuildingSubsystem* Buildings = GetWorld() ? GetWorld()->GetSubsystem<UChronoBuildingSubsystem>() : nullptr)
	{
		Population.Refresh(Buildings->GetBuildingSet(), Conditions, Rules);
	}
	PushDemand();
	return true;
}
