#include "ChronoTrafficSubsystem.h"

#include "ChronoBuildingSubsystem.h"
#include "ChronoCore.h"
#include "ChronoPopulationSubsystem.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoRoadTypes.h"
#include "ChronoSimulationSubsystem.h"
#include "ChronoTrafficRenderActor.h"
#include "ChronoTrafficSettings.h"
#include "ChronoZoningSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace ChronoTrafficSubsystemPrivate
{
	void StatsFromConsole(UWorld* World)
	{
		if (const UChronoTrafficSubsystem* Traffic = World ? World->GetSubsystem<UChronoTrafficSubsystem>() : nullptr)
		{
			Traffic->LogStats();
		}
	}

	FAutoConsoleCommandWithWorld StatsCommand(
		TEXT("Chrono.Traffic.Stats"),
		TEXT("Prints the number of vehicles, trip times and speeds."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&StatsFromConsole));
}

void UChronoTrafficSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Traffic runs on the roads, between the buildings, for the population.
	UChronoRoadSubsystem* Roads = Collection.InitializeDependency<UChronoRoadSubsystem>();
	Collection.InitializeDependency<UChronoZoningSubsystem>();
	Collection.InitializeDependency<UChronoBuildingSubsystem>();
	Collection.InitializeDependency<UChronoPopulationSubsystem>();
	UChronoSimulationSubsystem* Simulation = Collection.InitializeDependency<UChronoSimulationSubsystem>();
	Super::Initialize(Collection);

	Rules = FChronoTrafficRules::FromSettings();
	CommuteRules = FChronoCommuteRules::FromSettings();
	TrafficDaySeconds = FMath::Max(UChronoTrafficSettings::Get()->TrafficDaySeconds, 10.0f);

	if (Roads)
	{
		RoadsChangedHandle = Roads->OnRoadNetworkChanged.AddUObject(this, &UChronoTrafficSubsystem::HandleRoadNetworkChanged);
	}
	if (Simulation)
	{
		SimTickHandle = Simulation->OnSimTick.AddUObject(this, &UChronoTrafficSubsystem::HandleSimTick);
	}
}

void UChronoTrafficSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		if (UChronoRoadSubsystem* Roads = World->GetSubsystem<UChronoRoadSubsystem>())
		{
			Roads->OnRoadNetworkChanged.Remove(RoadsChangedHandle);
		}
		if (UChronoSimulationSubsystem* Simulation = World->GetSubsystem<UChronoSimulationSubsystem>())
		{
			Simulation->OnSimTick.Remove(SimTickHandle);
		}
	}
	RoadsChangedHandle.Reset();
	SimTickHandle.Reset();

	Super::Deinitialize();
}

bool UChronoTrafficSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChronoTrafficSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RenderActor = InWorld.SpawnActor<AChronoTrafficRenderActor>(AChronoTrafficRenderActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (RenderActor)
	{
		RenderActor->InitializeFromSubsystem(this);
	}
	else
	{
		UE_LOG(LogChrono, Error, TEXT("Failed to spawn the traffic render actor"));
	}
}

void UChronoTrafficSubsystem::HandleRoadNetworkChanged(const FChronoRoadChangeSet& Changes)
{
	// Several edits in one tick cost one rebuild.
	bNetworkDirty = true;
}

void UChronoTrafficSubsystem::HandleSimTick(int64 TickIndex, float FixedDeltaSeconds)
{
	const UWorld* World = GetWorld();
	const UChronoRoadSubsystem* Roads = World ? World->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	if (!Roads)
	{
		return;
	}

	if (bNetworkDirty)
	{
		// Lanes are rebuilt from scratch: every vehicle is taken off the road
		// first, so none is ever left on a road that changed or disappeared.
		Sim.SetNetwork(Roads->GetGraph(), Rules);
		bNetworkDirty = false;
	}

	// The traffic day runs on its own clock: a calendar day is far too short to cross a city.
	const double PreviousFraction = DayFraction;
	DayFraction += FixedDeltaSeconds / TrafficDaySeconds;
	if (DayFraction >= 1.0)
	{
		RequestCommuteTrips(PreviousFraction, 1.0);
		DayFraction -= 1.0;
		RequestCommuteTrips(0.0, DayFraction);
	}
	else
	{
		RequestCommuteTrips(PreviousFraction, DayFraction);
	}

	Sim.Step(FixedDeltaSeconds, Rules);
	Sim.TakeArrivals();

	// Time lost on the road weighs on everybody who works.
	if (UChronoPopulationSubsystem* Population = World->GetSubsystem<UChronoPopulationSubsystem>())
	{
		Population->SetCommuteHappiness(ChronoCommute::ComputeCommuteHappiness(Sim.GetStats().AverageTripSeconds, CommuteRules));
	}
}

void UChronoTrafficSubsystem::RequestCommuteTrips(double FromDayFraction, double ToDayFraction)
{
	const UWorld* World = GetWorld();
	const UChronoPopulationSubsystem* Population = World ? World->GetSubsystem<UChronoPopulationSubsystem>() : nullptr;
	const UChronoBuildingSubsystem* Buildings = World ? World->GetSubsystem<UChronoBuildingSubsystem>() : nullptr;
	const UChronoZoningSubsystem* Zoning = World ? World->GetSubsystem<UChronoZoningSubsystem>() : nullptr;
	if (!Population || !Buildings || !Zoning || ToDayFraction <= FromDayFraction)
	{
		return;
	}

	TArray<FChronoTripRequest> Trips;
	ChronoCommute::GenerateTrips(Population->GetPopulation(), Buildings->GetBuildingSet(), Zoning->GetGrid().GetCellSizeCm(), FromDayFraction, ToDayFraction, CommuteRules, Trips);
	for (const FChronoTripRequest& Trip : Trips)
	{
		Sim.RequestTrip(Trip);
	}
}

void UChronoTrafficSubsystem::LogStats() const
{
	const FChronoTrafficStats& Stats = Sim.GetStats();
	UE_LOG(LogChrono, Display, TEXT("Traffic at %.0f %% of the day: %d vehicles on the road, %d trips waiting"), DayFraction * 100.0, Stats.ActiveVehicles, Stats.WaitingTrips);
	UE_LOG(LogChrono, Display, TEXT("  Average speed %.1f km/h, average trip %.0f s"), Stats.AverageSpeedCmPerS * 0.036f, Stats.AverageTripSeconds);
	UE_LOG(LogChrono, Display, TEXT("  Trips driven: %d, resolved without driving: %d"), Stats.TripsDriven, Stats.TripsResolvedWithoutDriving);
	UE_LOG(LogChrono, Display, TEXT("  Lanes: %d, paths through junctions: %d"), Sim.GetLaneGraph().GetLanes().Num(), Sim.GetLaneGraph().GetConnectors().Num());
}
