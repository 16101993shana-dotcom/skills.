// Automation tests for lanes, routing and the vehicle simulation.
// Filter: "Chronopolis.Traffic".

#include "ChronoBuildingSet.h"
#include "ChronoCommute.h"
#include "ChronoHeightfield.h"
#include "ChronoLaneGraph.h"
#include "ChronoPopulation.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "ChronoTrafficSim.h"
#include "ChronoZoneGrid.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoTrafficTests
{
	/** Flat ground and roads built through the game's rules. */
	struct FRoads
	{
		FChronoHeightfield Heightfield;
		FChronoRoadGraph Graph;
		FChronoRoadRules RoadRules;
		FChronoTrafficRules TrafficRules;

		FRoads()
		{
			Heightfield.Init(256, 200.0f, 0.0f);
			RoadRules.ApplyTo(Graph);
		}

		int32 Build(const FVector2D& Start, const FVector2D& End)
		{
			FChronoRoadBuildRequest Request;
			Request.Start = Start;
			Request.End = End;
			Request.Profile.CarriagewayWidthCm = 700.0f;
			Request.Profile.SidewalkWidthCm = 200.0f;

			FChronoRoadGraph Scratch = Graph;
			Scratch.ClearChanges();
			int32 SegmentId = INDEX_NONE;
			if (ChronoRoadBuilder::ApplyAndValidate(Scratch, Heightfield, RoadRules, Request, SegmentId) != EChronoRoadBuildResult::Success)
			{
				return INDEX_NONE;
			}
			Graph = MoveTemp(Scratch);
			Graph.ClearChanges();
			return SegmentId;
		}

		/** Street grid: Count streets each way, SpacingCm apart, centered on the origin. */
		void BuildGrid(int32 Count, double SpacingCm)
		{
			const double Half = 0.5 * (Count - 1) * SpacingCm;
			for (int32 Row = 0; Row < Count; ++Row)
			{
				Build(FVector2D(-Half - 3000.0, -Half + Row * SpacingCm), FVector2D(Half + 3000.0, -Half + Row * SpacingCm));
			}
			for (int32 Column = 0; Column < Count; ++Column)
			{
				for (int32 Row = 0; Row + 1 < Count; ++Row)
				{
					const double X = -Half + Column * SpacingCm;
					Build(FVector2D(X, -Half + Row * SpacingCm), FVector2D(X, -Half + (Row + 1) * SpacingCm));
				}
			}
		}
	};

	/** Footprints of vehicles and pedestrians may not overlap by more than this (numerical noise). */
	const double OverlapToleranceCm = 1.0;

	/** Runs the simulation, checking the zero-clipping contract after every step. Returns the first error. */
	FString RunChecked(FChronoTrafficSim& Sim, const FChronoTrafficRules& Rules, float Seconds, TArray<FChronoTripArrival>& OutArrivals, bool bStopWhenIdle)
	{
		const float Step = 0.1f;
		FString Error;
		for (float Time = 0.0f; Time < Seconds; Time += Step)
		{
			Sim.Step(Step, Rules);
			OutArrivals.Append(Sim.TakeArrivals());
			if (!Sim.CheckInvariants(Error) || !Sim.CheckNoOverlap(OverlapToleranceCm, Error))
			{
				return FString::Printf(TEXT("At %.1f s: %s"), Time, *Error);
			}
			if (bStopWhenIdle && Sim.GetStats().ActiveVehicles == 0 && Sim.GetStats().WaitingTrips == 0)
			{
				break;
			}
		}
		return FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoLaneGraphTest, "Chronopolis.Traffic.Lanes.CrossroadsAndRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoLaneGraphTest::RunTest(const FString& Parameters)
{
	// A crossroads: one east-west road, then a north-south road in two halves that end on it.
	ChronoTrafficTests::FRoads Roads;
	Roads.Build(FVector2D(-10000.0, 0.0), FVector2D(10000.0, 0.0));
	Roads.Build(FVector2D(0.0, -10000.0), FVector2D(0.0, 0.0));
	Roads.Build(FVector2D(0.0, 0.0), FVector2D(0.0, 10000.0));
	TestEqual(TEXT("Four road segments meet"), Roads.Graph.GetSegments().Num(), 4);

	FChronoLaneGraph Lanes;
	Lanes.Build(Roads.Graph, Roads.TrafficRules);
	int32 NumVehicleLanes = 0;
	int32 NumWalkLanes = 0;
	for (const FChronoLane& Lane : Lanes.GetLanes())
	{
		++(Lane.Kind == EChronoLaneKind::Vehicle ? NumVehicleLanes : NumWalkLanes);
	}
	TestEqual(TEXT("Two traffic lanes per segment"), NumVehicleLanes, 8);
	TestEqual(TEXT("Two sidewalks per segment"), NumWalkLanes, 8);

	// At the crossroads each of the 4 arriving traffic lanes can go 3 ways,
	// and each of the 4 arriving sidewalks 4 ways: pedestrians may also cross
	// the street they came by. Each of the 4 dead ends has one turn-back for
	// vehicles and one for pedestrians.
	int32 NumDriveAtCrossroads = 0;
	int32 NumWalkAtCrossroads = 0;
	int32 NumDriveTurnBacks = 0;
	int32 NumWalkTurnBacks = 0;
	bool bKindsNeverMix = true;
	int32 CrossroadsNode = INDEX_NONE;
	for (const TPair<int32, FChronoRoadNode>& Pair : Roads.Graph.GetNodes())
	{
		if (Pair.Value.SegmentIds.Num() == 4)
		{
			CrossroadsNode = Pair.Key;
		}
	}
	for (const FChronoLaneConnector& Connector : Lanes.GetConnectors())
	{
		const FChronoLane& From = Lanes.GetLanes()[Connector.FromLaneId];
		const FChronoLane& To = Lanes.GetLanes()[Connector.ToLaneId];
		const bool bWalk = From.Kind == EChronoLaneKind::Pedestrian;
		bKindsNeverMix = bKindsNeverMix && From.Kind == To.Kind;
		if (Connector.NodeId == CrossroadsNode)
		{
			++(bWalk ? NumWalkAtCrossroads : NumDriveAtCrossroads);
		}
		else
		{
			++(bWalk ? NumWalkTurnBacks : NumDriveTurnBacks);
			TestTrue(TEXT("A turn-back returns on the same road"), From.SegmentId == To.SegmentId);
		}
	}
	TestEqual(TEXT("Twelve ways to drive through the crossroads"), NumDriveAtCrossroads, 12);
	TestEqual(TEXT("Sixteen ways to walk through the crossroads"), NumWalkAtCrossroads, 16);
	TestEqual(TEXT("One turn-back per dead end for vehicles"), NumDriveTurnBacks, 4);
	TestEqual(TEXT("One turn-back per dead end for pedestrians"), NumWalkTurnBacks, 4);
	TestTrue(TEXT("No path leads from the carriageway to a sidewalk or back"), bKindsNeverMix);

	// Sidewalk lanes stay off the carriageway: at least half of it, plus
	// half a walker, away from the centerline of their road.
	bool bWalkersOffTheCarriageway = true;
	for (const FChronoLane& Lane : Lanes.GetLanes())
	{
		if (Lane.Kind != EChronoLaneKind::Pedestrian)
		{
			continue;
		}
		const FChronoRoadSegment* Segment = Roads.Graph.FindSegment(Lane.SegmentId);
		for (const FVector& Point : Lane.Points)
		{
			double Distance = 0.0;
			Segment->Curve.FindClosestParameter(FVector2D(Point.X, Point.Y), Distance);
			bWalkersOffTheCarriageway = bWalkersOffTheCarriageway && Distance >= 0.5 * Segment->Profile.CarriagewayWidthCm + 0.5 * Roads.TrafficRules.PedestrianWidthCm;
		}
	}
	TestTrue(TEXT("Sidewalk lanes never run on the carriageway"), bWalkersOffTheCarriageway);

	// Lanes keep to the right: a lane's points are on the right of the centerline, seen in its direction of travel.
	bool bAllOnTheRight = true;
	for (const FChronoLane& Lane : Lanes.GetLanes())
	{
		const FChronoRoadSegment* Segment = Roads.Graph.FindSegment(Lane.SegmentId);
		FVector Position;
		FVector Direction;
		Lanes.SampleLane(Lane.Id, 0.5 * Lane.Length, Position, Direction);
		double Distance = 0.0;
		const double T = Segment->Curve.FindClosestParameter(FVector2D(Position.X, Position.Y), Distance);
		const FVector2D ToLane = FVector2D(Position.X, Position.Y) - Segment->Curve.Evaluate(T);
		// Right of the direction of travel, in the engine's axes: (-dy, dx).
		const FVector2D Right(-Direction.Y, Direction.X);
		bAllOnTheRight = bAllOnTheRight && FVector2D::DotProduct(ToLane, Right) > 100.0;
	}
	TestTrue(TEXT("Every lane runs on the right of its road"), bAllOnTheRight);

	// Conflicts: going straight in opposite directions is compatible, a left
	// turn crosses the oncoming straight path, and two paths into the same lane merge.
	bool bOppositeStraightsConflict = false;
	bool bSomeCrossing = false;
	bool bConflictsAreMutual = true;
	for (const FChronoLaneConnector& Connector : Lanes.GetConnectors())
	{
		for (const int32 OtherId : Connector.Conflicts)
		{
			const FChronoLaneConnector& Other = Lanes.GetConnectors()[OtherId];
			bConflictsAreMutual = bConflictsAreMutual && Other.Conflicts.Contains(Connector.Id) && Other.NodeId == Connector.NodeId;
			bSomeCrossing = true;

			const FChronoLane& FromA = Lanes.GetLanes()[Connector.FromLaneId];
			const FChronoLane& ToA = Lanes.GetLanes()[Connector.ToLaneId];
			const FChronoLane& FromB = Lanes.GetLanes()[Other.FromLaneId];
			const FChronoLane& ToB = Lanes.GetLanes()[Other.ToLaneId];
			auto DirectionOf = [](const FChronoLane& Lane, bool bAtEnd)
			{
				const int32 Last = Lane.Points.Num() - 1;
				return bAtEnd ? (Lane.Points[Last] - Lane.Points[Last - 1]).GetSafeNormal() : (Lane.Points[1] - Lane.Points[0]).GetSafeNormal();
			};
			const bool bStraightA = FVector::DotProduct(DirectionOf(FromA, true), DirectionOf(ToA, false)) > 0.9;
			const bool bStraightB = FVector::DotProduct(DirectionOf(FromB, true), DirectionOf(ToB, false)) > 0.9;
			const bool bOpposite = FVector::DotProduct(DirectionOf(FromA, true), DirectionOf(FromB, true)) < -0.9;
			bOppositeStraightsConflict = bOppositeStraightsConflict || (bStraightA && bStraightB && bOpposite);
		}
	}
	TestTrue(TEXT("Some paths cross"), bSomeCrossing);

	// Every way of walking across a street conflicts with at least one way
	// of driving through: nobody crosses while a vehicle has that path.
	bool bCrossingsAreGuarded = true;
	for (const FChronoLaneConnector& Connector : Lanes.GetConnectors())
	{
		const FChronoLane& From = Lanes.GetLanes()[Connector.FromLaneId];
		const FChronoLane& To = Lanes.GetLanes()[Connector.ToLaneId];
		if (From.Kind != EChronoLaneKind::Pedestrian || Connector.NodeId != CrossroadsNode || Connector.Length < Roads.TrafficRules.VehicleWidthCm * 2.0)
		{
			continue;
		}
		bool bGuarded = false;
		for (const int32 OtherId : Connector.Conflicts)
		{
			bGuarded = bGuarded || Lanes.GetLanes()[Lanes.GetConnectors()[OtherId].FromLaneId].Kind == EChronoLaneKind::Vehicle;
		}
		bCrossingsAreGuarded = bCrossingsAreGuarded && bGuarded;
	}
	TestTrue(TEXT("Crossing a street on foot excludes the traffic it crosses"), bCrossingsAreGuarded);
	TestTrue(TEXT("Conflicts are mutual and local to one junction"), bConflictsAreMutual);
	TestFalse(TEXT("Opposite straight paths are compatible"), bOppositeStraightsConflict);

	// Route from the west arm to the north arm: found, and every step is connected.
	int32 WestSegment = INDEX_NONE;
	int32 NorthSegment = INDEX_NONE;
	for (const TPair<int32, FChronoRoadSegment>& Pair : Roads.Graph.GetSegments())
	{
		const FVector2D Middle = Pair.Value.Curve.Evaluate(0.5);
		if (Middle.X < -1000.0)
		{
			WestSegment = Pair.Key;
		}
		if (Middle.Y > 1000.0)
		{
			NorthSegment = Pair.Key;
		}
	}

	const int32 StartLane = Lanes.FindLaneOnSide(WestSegment, 1);
	TArray<int32> Route;
	double TargetDistance = 0.0;
	TestTrue(TEXT("A route is found"), Lanes.FindRoute(StartLane, 500.0, NorthSegment, 5000.0, Roads.TrafficRules, nullptr, Route, TargetDistance));
	TestTrue(TEXT("The route starts on the starting lane"), Route.Num() >= 2 && Route[0] == StartLane);
	bool bConnected = true;
	for (int32 Index = 0; Index + 1 < Route.Num(); ++Index)
	{
		bConnected = bConnected && Lanes.FindConnector(Route[Index], Route[Index + 1]) != INDEX_NONE;
	}
	TestTrue(TEXT("Every step of the route is connected"), bConnected);
	TestTrue(TEXT("The route ends on the destination road"), Route.Num() > 0 && Lanes.GetLanes()[Route.Last()].SegmentId == NorthSegment);

	// A place nothing leads to has no route.
	FChronoRoadGraph Island = Roads.Graph;
	const int32 A = Island.AddNode(FVector(20000.0, 20000.0, 0.0));
	const int32 B = Island.AddNode(FVector(24000.0, 20000.0, 0.0));
	FChronoRoadProfileData Profile;
	const int32 IslandSegment = Island.AddSegment(A, B, FChronoBezier2D::MakeLine(FVector2D(20000.0, 20000.0), FVector2D(24000.0, 20000.0)), Profile);
	FChronoLaneGraph IslandLanes;
	IslandLanes.Build(Island, Roads.TrafficRules);
	const int32 MainLane = IslandLanes.FindLaneOnSide(WestSegment, 1);
	TestFalse(TEXT("An unconnected road cannot be reached"), IslandLanes.FindRoute(MainLane, 500.0, IslandSegment, 2000.0, Roads.TrafficRules, nullptr, Route, TargetDistance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoTrafficFollowTest, "Chronopolis.Traffic.Sim.VehiclesKeepTheirDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoTrafficFollowTest::RunTest(const FString& Parameters)
{
	// One long road. Twelve vehicles leave the same door for the same destination.
	ChronoTrafficTests::FRoads Roads;
	const int32 Road = Roads.Build(FVector2D(-20000.0, 0.0), FVector2D(20000.0, 0.0));

	FChronoTrafficSim Sim;
	Sim.SetNetwork(Roads.Graph, Roads.TrafficRules);
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FChronoTripRequest Trip;
		Trip.FromSegmentId = Road;
		Trip.FromSegmentDistance = 2000.0;
		Trip.FromSide = 1;
		Trip.ToSegmentId = Road;
		Trip.ToSegmentDistance = 36000.0;
		Trip.UserId = Index;
		Sim.RequestTrip(Trip);
	}

	TArray<FChronoTripArrival> Arrivals;
	const FString Error = ChronoTrafficTests::RunChecked(Sim, Roads.TrafficRules, 400.0f, Arrivals, true);
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	TestEqual(TEXT("Every trip ends"), Arrivals.Num(), 12);
	bool bAllDrove = true;
	float Longest = 0.0f;
	for (const FChronoTripArrival& Arrival : Arrivals)
	{
		bAllDrove = bAllDrove && Arrival.bDrove;
		Longest = FMath::Max(Longest, Arrival.Seconds);
	}
	TestTrue(TEXT("Every vehicle drove to its destination"), bAllDrove);

	// 340 m at up to 8 m/s takes at least 42 s; the last of twelve leaves later but still arrives in reasonable time.
	AddInfo(FString::Printf(TEXT("Longest trip: %.0f s, average: %.0f s"), Longest, Sim.GetStats().AverageTripSeconds));
	TestTrue(TEXT("Trips take a plausible time"), Longest > 42.0f && Longest < 200.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoTrafficJunctionTest, "Chronopolis.Traffic.Sim.CrossingFlowsNeverCollide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoTrafficJunctionTest::RunTest(const FString& Parameters)
{
	// A street grid and a few hundred trips between random places: flows
	// cross, merge and queue at every junction.
	ChronoTrafficTests::FRoads Roads;
	Roads.BuildGrid(4, 10000.0);

	TArray<int32> SegmentIds;
	Roads.Graph.GetSegments().GetKeys(SegmentIds);
	SegmentIds.Sort();

	// Trips leave one after the other, a few per second, like a rush hour.
	auto RunScenario = [&Roads, &SegmentIds](int32 Seed, int32 NumTrips, float TripsPerSecond, TArray<FChronoTripArrival>& OutArrivals, FChronoTrafficStats& OutStats)
	{
		FChronoTrafficSim Sim;
		Sim.SetNetwork(Roads.Graph, Roads.TrafficRules);

		FRandomStream Random(Seed);
		const float Step = 0.1f;
		int32 NumRequested = 0;
		FString Error;

		for (float Time = 0.0f; Time < 3600.0f; Time += Step)
		{
			while (NumRequested < NumTrips && NumRequested < Time * TripsPerSecond)
			{
				const FChronoRoadSegment* From = Roads.Graph.FindSegment(SegmentIds[Random.RandRange(0, SegmentIds.Num() - 1)]);
				const FChronoRoadSegment* To = Roads.Graph.FindSegment(SegmentIds[Random.RandRange(0, SegmentIds.Num() - 1)]);

				FChronoTripRequest Trip;
				Trip.FromSegmentId = From->Id;
				Trip.FromSegmentDistance = Random.FRandRange(0.2f, 0.8f) * From->Length;
				Trip.FromSide = Random.FRandRange(0.0f, 1.0f) < 0.5f ? 1 : -1;
				Trip.ToSegmentId = To->Id;
				Trip.ToSegmentDistance = Random.FRandRange(0.2f, 0.8f) * To->Length;
				Trip.UserId = NumRequested;
				Sim.RequestTrip(Trip);
				++NumRequested;
			}

			Sim.Step(Step, Roads.TrafficRules);
			OutArrivals.Append(Sim.TakeArrivals());
			if (!Sim.CheckInvariants(Error) || !Sim.CheckNoOverlap(ChronoTrafficTests::OverlapToleranceCm, Error))
			{
				Error = FString::Printf(TEXT("At %.1f s: %s"), Time, *Error);
				break;
			}
			if (NumRequested == NumTrips && Sim.GetStats().ActiveVehicles == 0 && Sim.GetStats().WaitingTrips == 0)
			{
				break;
			}
		}

		OutStats = Sim.GetStats();
		return Error;
	};

	TArray<FChronoTripArrival> Arrivals;
	FChronoTrafficStats Stats;
	const FString Error = RunScenario(77, 400, 2.0f, Arrivals, Stats);
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	int32 NumDrove = 0;
	for (const FChronoTripArrival& Arrival : Arrivals)
	{
		NumDrove += Arrival.bDrove ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d trips ended, %d driven, average %.0f s"), Arrivals.Num(), NumDrove, Stats.AverageTripSeconds));

	TestEqual(TEXT("Every trip ends"), Arrivals.Num(), 400);
	// All-way stops have little capacity: at two departures a second a few
	// trips wait past the timeout and are resolved without driving.
	TestTrue(TEXT("Nearly every trip is driven to the end"), NumDrove >= 400 * 95 / 100);

	// Light traffic flows freely: one departure every five seconds.
	TArray<FChronoTripArrival> Light;
	FChronoTrafficStats LightStats;
	RunScenario(77, 100, 0.2f, Light, LightStats);
	AddInfo(FString::Printf(TEXT("Light traffic: %d trips ended, average %.0f s"), Light.Num(), LightStats.AverageTripSeconds));
	TestTrue(TEXT("Light traffic is faster than a rush hour"), LightStats.AverageTripSeconds < Stats.AverageTripSeconds);

	// Saturation: the same trips all at once jam the junctions. Vehicles
	// queue, some trips are resolved without driving, but nothing ever
	// collides and every trip still ends.
	TArray<FChronoTripArrival> Jammed;
	FChronoTrafficStats JammedStats;
	const FString JamError = RunScenario(77, 400, 1000.0f, Jammed, JammedStats);
	if (!JamError.IsEmpty())
	{
		AddError(JamError);
	}
	AddInfo(FString::Printf(TEXT("All at once: %d trips ended, average %.0f s"), Jammed.Num(), JammedStats.AverageTripSeconds));
	TestEqual(TEXT("Every trip ends even in a jam"), Jammed.Num(), 400);
	TestTrue(TEXT("A jam makes trips longer"), JammedStats.AverageTripSeconds > Stats.AverageTripSeconds);

	// Same seed, same traffic.
	TArray<FChronoTripArrival> Again;
	FChronoTrafficStats StatsAgain;
	RunScenario(77, 400, 2.0f, Again, StatsAgain);
	bool bSame = Again.Num() == Arrivals.Num();
	for (int32 Index = 0; bSame && Index < Arrivals.Num(); ++Index)
	{
		bSame = Again[Index].UserId == Arrivals[Index].UserId && Again[Index].Seconds == Arrivals[Index].Seconds;
	}
	TestTrue(TEXT("The same trips give the same traffic"), bSame);

	// Changing the roads takes every vehicle off them: nothing is left on a road that changed.
	FChronoTrafficSim Sim;
	Sim.SetNetwork(Roads.Graph, Roads.TrafficRules);
	for (int32 Index = 0; Index < 20; ++Index)
	{
		FChronoTripRequest Trip;
		Trip.FromSegmentId = SegmentIds[0];
		Trip.FromSegmentDistance = 2000.0;
		Trip.ToSegmentId = SegmentIds.Last();
		Trip.ToSegmentDistance = 2000.0;
		Trip.UserId = Index;
		Sim.RequestTrip(Trip);
	}
	TArray<FChronoTripArrival> Interrupted;
	ChronoTrafficTests::RunChecked(Sim, Roads.TrafficRules, 20.0f, Interrupted, false);
	Sim.SetNetwork(Roads.Graph, Roads.TrafficRules);
	Interrupted.Append(Sim.TakeArrivals());
	TestEqual(TEXT("Every interrupted trip is resolved"), Interrupted.Num(), 20);
	TestEqual(TEXT("No vehicle is left after the roads change"), Sim.GetStats().ActiveVehicles, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoTrafficPedestrianTest, "Chronopolis.Traffic.Sim.PedestriansAndVehiclesShareJunctions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoTrafficPedestrianTest::RunTest(const FString& Parameters)
{
	// A street grid, vehicles and walkers between random places: walkers
	// cross every street the vehicles drive on.
	ChronoTrafficTests::FRoads Roads;
	Roads.BuildGrid(4, 8000.0);

	TArray<int32> SegmentIds;
	Roads.Graph.GetSegments().GetKeys(SegmentIds);
	SegmentIds.Sort();

	FChronoTrafficSim Sim;
	Sim.SetNetwork(Roads.Graph, Roads.TrafficRules);

	const int32 NumTrips = 500;
	FRandomStream Random(2024);
	TArray<bool> bWalked;
	const float Step = 0.1f;
	int32 NumRequested = 0;
	int32 MostWalkers = 0;
	int32 MostAgents = 0;
	FString Error;
	TArray<FChronoTripArrival> Arrivals;

	for (float Time = 0.0f; Time < 3600.0f; Time += Step)
	{
		while (NumRequested < NumTrips && NumRequested < Time * 3.0f)
		{
			const FChronoRoadSegment* From = Roads.Graph.FindSegment(SegmentIds[Random.RandRange(0, SegmentIds.Num() - 1)]);
			const FChronoRoadSegment* To = Roads.Graph.FindSegment(SegmentIds[Random.RandRange(0, SegmentIds.Num() - 1)]);

			FChronoTripRequest Trip;
			Trip.FromSegmentId = From->Id;
			Trip.FromSegmentDistance = Random.FRandRange(0.2f, 0.8f) * From->Length;
			Trip.FromSide = Random.FRandRange(0.0f, 1.0f) < 0.5f ? 1 : -1;
			Trip.ToSegmentId = To->Id;
			Trip.ToSegmentDistance = Random.FRandRange(0.2f, 0.8f) * To->Length;
			Trip.UserId = NumRequested;
			Trip.bWalk = (NumRequested % 2) == 0;
			bWalked.Add(Trip.bWalk);
			Sim.RequestTrip(Trip);
			++NumRequested;
		}

		Sim.Step(Step, Roads.TrafficRules);
		Arrivals.Append(Sim.TakeArrivals());
		MostWalkers = FMath::Max(MostWalkers, Sim.GetStats().ActivePedestrians);
		MostAgents = FMath::Max(MostAgents, Sim.GetStats().ActiveVehicles);
		if (!Sim.CheckInvariants(Error) || !Sim.CheckNoOverlap(ChronoTrafficTests::OverlapToleranceCm, Error))
		{
			AddError(FString::Printf(TEXT("At %.1f s: %s"), Time, *Error));
			break;
		}
		if (NumRequested == NumTrips && Sim.GetStats().ActiveVehicles == 0 && Sim.GetStats().WaitingTrips == 0)
		{
			break;
		}
	}

	int32 NumWalked = 0;
	int32 NumDrove = 0;
	float WalkSeconds = 0.0f;
	float DriveSeconds = 0.0f;
	for (const FChronoTripArrival& Arrival : Arrivals)
	{
		if (!Arrival.bDrove)
		{
			continue;
		}
		if (bWalked[Arrival.UserId])
		{
			++NumWalked;
			WalkSeconds += Arrival.Seconds;
		}
		else
		{
			++NumDrove;
			DriveSeconds += Arrival.Seconds;
		}
	}
	AddInfo(FString::Printf(TEXT("%d trips ended: %d walked (average %.0f s), %d driven (average %.0f s); up to %d walkers among %d on the move"),
		Arrivals.Num(), NumWalked, NumWalked > 0 ? WalkSeconds / NumWalked : 0.0f, NumDrove, NumDrove > 0 ? DriveSeconds / NumDrove : 0.0f, MostWalkers, MostAgents));

	TestEqual(TEXT("Every trip ends"), Arrivals.Num(), NumTrips);
	TestTrue(TEXT("Nearly every walker arrives on foot"), NumWalked >= NumTrips / 2 * 95 / 100);
	TestTrue(TEXT("Nearly every vehicle arrives"), NumDrove >= NumTrips / 2 * 95 / 100);
	TestTrue(TEXT("Walkers and vehicles are on the move together"), MostWalkers >= 20 && MostAgents - MostWalkers >= 5);
	TestTrue(TEXT("Walking is slower than driving"), NumWalked > 0 && NumDrove > 0 && WalkSeconds / NumWalked > DriveSeconds / NumDrove);

	// The geometric check does see an overlap when there is one: two
	// vehicles are set off, then the one behind is pushed into the one ahead.
	ChronoTrafficTests::FRoads Single;
	const int32 Road = Single.Build(FVector2D(-20000.0, 0.0), FVector2D(20000.0, 0.0));
	FChronoTrafficSim Pair;
	Pair.SetNetwork(Single.Graph, Single.TrafficRules);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FChronoTripRequest Trip;
		Trip.FromSegmentId = Road;
		Trip.FromSegmentDistance = 2000.0;
		Trip.ToSegmentId = Road;
		Trip.ToSegmentDistance = 36000.0;
		Trip.UserId = Index;
		Pair.RequestTrip(Trip);
	}
	TArray<FChronoTripArrival> Unused;
	TestTrue(TEXT("Two vehicles following each other do not overlap"), ChronoTrafficTests::RunChecked(Pair, Single.TrafficRules, 20.0f, Unused, false).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoCommuteTest, "Chronopolis.Traffic.Commute.CitizensGoToWork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoCommuteTest::RunTest(const FString& Parameters)
{
	// The whole chain: roads, zones, buildings that grow on demand, a
	// population that fills them, then the morning commute on the same roads.
	ChronoTrafficTests::FRoads Roads;
	Roads.BuildGrid(5, 10000.0);

	FChronoZoneGrid Zoning;
	FChronoZoningRules ZoningRules;
	FChronoRoadChangeSet Everything;
	Roads.Graph.GetSegments().GetKeys(Everything.ChangedSegments);
	Roads.Graph.GetNodes().GetKeys(Everything.ChangedNodes);
	Zoning.ApplyRoadChanges(Roads.Graph, Roads.Heightfield, Everything, ZoningRules);
	Zoning.PaintZone(FVector2D(0.0, 0.0), 60000.0f, EChronoZoneType::Agricultural);
	Zoning.PaintZone(FVector2D(-17000.0, 0.0), 10000.0f, EChronoZoneType::Commercial);
	Zoning.PaintZone(FVector2D(17000.0, 0.0), 10000.0f, EChronoZoneType::Industrial);
	Zoning.PaintZone(FVector2D(0.0, 0.0), 11000.0f, EChronoZoneType::Residential);
	Zoning.TakeDirtySegments();

	TArray<FChronoBuildingDefData> Definitions;
	for (const EChronoZoneType Zone : { EChronoZoneType::Residential, EChronoZoneType::Commercial, EChronoZoneType::Industrial, EChronoZoneType::Agricultural })
	{
		FChronoBuildingDefData Definition;
		Definition.DefId = TEXT("TestBuilding");
		Definition.Zone = Zone;
		Definition.MaxFloors = 3;
		Definitions.Add(Definition);
	}

	FChronoBuildingSet Buildings;
	FChronoBuildingRules BuildingRules;
	FChronoPopulation Population;
	FChronoPopulationRules PopulationRules;
	FChronoCityConditions Conditions;
	for (int64 Day = 0; Day < 2 * 365; ++Day)
	{
		Population.StepDay(Day, Buildings, Conditions, PopulationRules);
		FRandomStream Random(4242 ^ static_cast<int32>(Day));
		Buildings.Grow(Zoning, Roads.Heightfield, Roads.Graph, Definitions, BuildingRules, Random, 2, &Population.GetDemand());
	}
	const int32 Employed = Population.GetStats().Employed;
	TestTrue(TEXT("The town has commuters"), Employed >= 50);

	// Over a whole day everybody who works leaves home once and leaves work once.
	FChronoCommuteRules CommuteRules;
	TArray<FChronoTripRequest> AllDay;
	ChronoCommute::GenerateTrips(Population, Buildings, Zoning.GetCellSizeCm(), 0.0, 1.0, CommuteRules, AllDay);
	TestEqual(TEXT("Two trips a day per commuter"), AllDay.Num(), 2 * Employed);

	// Work close to home is walked to, the rest is driven to.
	int32 NumWalkTrips = 0;
	for (const FChronoTripRequest& Trip : AllDay)
	{
		NumWalkTrips += Trip.bWalk ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d trips a day, %d on foot"), AllDay.Num(), NumWalkTrips));
	TestTrue(TEXT("Some commutes are walked and some are driven"), NumWalkTrips > 0 && NumWalkTrips < AllDay.Num());

	FChronoCommuteRules NobodyWalks = CommuteRules;
	NobodyWalks.WalkDistanceCm = 0.0f;
	TArray<FChronoTripRequest> AllDriven;
	ChronoCommute::GenerateTrips(Population, Buildings, Zoning.GetCellSizeCm(), 0.0, 1.0, NobodyWalks, AllDriven);
	bool bNoneWalked = AllDriven.Num() == AllDay.Num();
	for (const FChronoTripRequest& Trip : AllDriven)
	{
		bNoneWalked = bNoneWalked && !Trip.bWalk;
	}
	TestTrue(TEXT("With no walking distance every commute is driven"), bNoneWalked);

	// Cutting the day in slices gives the same trips, each exactly once.
	int32 InSlices = 0;
	for (int32 Slice = 0; Slice < 100; ++Slice)
	{
		TArray<FChronoTripRequest> Trips;
		ChronoCommute::GenerateTrips(Population, Buildings, Zoning.GetCellSizeCm(), Slice / 100.0, (Slice + 1) / 100.0, CommuteRules, Trips);
		InSlices += Trips.Num();
	}
	TestEqual(TEXT("No trip is lost or doubled between slices"), InSlices, AllDay.Num());

	// The morning rush, spread over its window as in the game (a 600 s day).
	const float DaySeconds = 600.0f;
	const float Step = 0.1f;
	FChronoTrafficSim Sim;
	Sim.SetNetwork(Roads.Graph, Roads.TrafficRules);

	TArray<FChronoTripArrival> Arrivals;
	FString Error;
	double DayFraction = 0.15;
	for (float Time = 0.0f; Time < 900.0f && Error.IsEmpty(); Time += Step)
	{
		const double NextFraction = FMath::Min(DayFraction + Step / DaySeconds, 0.5);
		TArray<FChronoTripRequest> Trips;
		ChronoCommute::GenerateTrips(Population, Buildings, Zoning.GetCellSizeCm(), DayFraction, NextFraction, CommuteRules, Trips);
		for (const FChronoTripRequest& Trip : Trips)
		{
			Sim.RequestTrip(Trip);
		}
		DayFraction = NextFraction;

		Sim.Step(Step, Roads.TrafficRules);
		Arrivals.Append(Sim.TakeArrivals());
		if (!Sim.CheckInvariants(Error) || !Sim.CheckNoOverlap(ChronoTrafficTests::OverlapToleranceCm, Error))
		{
			Error = FString::Printf(TEXT("At %.1f s: %s"), Time, *Error);
		}
		if (DayFraction >= 0.5 && Sim.GetStats().ActiveVehicles == 0 && Sim.GetStats().WaitingTrips == 0)
		{
			break;
		}
	}
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	int32 NumDrove = 0;
	for (const FChronoTripArrival& Arrival : Arrivals)
	{
		NumDrove += Arrival.bDrove ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d commuters: %d arrived, %d on the roads and sidewalks all the way, average commute %.0f s"), Employed, Arrivals.Num(), NumDrove, Sim.GetStats().AverageTripSeconds));
	TestEqual(TEXT("Every commuter arrives"), Arrivals.Num(), Employed);
	TestTrue(TEXT("Most commuters travel all the way"), NumDrove >= Employed * 9 / 10);

	// Long commutes cost happiness, short ones do not, and the loss is capped.
	TestTrue(TEXT("A short commute costs nothing"), ChronoCommute::ComputeCommuteHappiness(40.0f, CommuteRules) == 0.0f);
	TestTrue(TEXT("Three minutes cost ten points"), FMath::IsNearlyEqual(ChronoCommute::ComputeCommuteHappiness(180.0f, CommuteRules), -10.0f, 0.001f));
	TestTrue(TEXT("The loss is capped"), FMath::IsNearlyEqual(ChronoCommute::ComputeCommuteHappiness(100000.0f, CommuteRules), -CommuteRules.MaxHappinessLoss, 0.001f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
