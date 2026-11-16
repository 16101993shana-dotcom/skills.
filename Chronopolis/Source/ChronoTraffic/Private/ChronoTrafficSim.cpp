#include "ChronoTrafficSim.h"

#include "ChronoGeometry2D.h"

namespace ChronoTrafficSimPrivate
{
	// "No obstacle ahead": far enough that it has no effect on speed.
	const double FreeGapCm = 1.0e9;

	// A vehicle stops this close to its destination, and counts as arrived.
	const double ArrivalToleranceCm = 60.0;

	// Vehicles never come closer than this to the one ahead, whatever the model says.
	const double HardGapCm = 20.0;

	// Speed below which a vehicle counts as stopped.
	const float StoppedSpeedCmPerS = 5.0f;

	// How fast the measured speed of a lane follows the vehicles on it.
	const float LaneSpeedBlend = 0.05f;

	// How fast the average trip time follows the latest trips.
	const float TripTimeBlend = 0.05f;
}

void FChronoTrafficSim::SetNetwork(const FChronoRoadGraph& Graph, const FChronoTrafficRules& Rules)
{
	// Whoever was on the road or waiting gets where they were going.
	for (int32 Index = 0; Index < Vehicles.Num(); ++Index)
	{
		if (Vehicles[Index].bActive)
		{
			Arrivals.Add({ Vehicles[Index].UserId, Vehicles[Index].TripSeconds, false });
		}
	}
	for (const FPendingTrip& Pending : PendingTrips)
	{
		Arrivals.Add({ Pending.Request.UserId, Pending.WaitedSeconds, false });
	}

	Vehicles.Reset();
	FreeVehicles.Reset();
	PendingTrips.Reset();

	LaneGraph.Build(Graph, Rules);

	LaneVehicles.Reset();
	LaneVehicles.SetNum(LaneGraph.GetLanes().Num());
	ConnectorVehicles.Reset();
	ConnectorVehicles.SetNum(LaneGraph.GetConnectors().Num());
	ConnectorHolders.Init(0, LaneGraph.GetConnectors().Num());
	LaneIncoming.Init(0, LaneGraph.GetLanes().Num());
	LaneLeavers.Init(INDEX_NONE, LaneGraph.GetLanes().Num());

	LaneSpeeds.Reset();
	for (const FChronoLane& Lane : LaneGraph.GetLanes())
	{
		LaneSpeeds.Add(Lane.SpeedLimitCmPerS);
	}

	Stats = FChronoTrafficStats();
}

void FChronoTrafficSim::RequestTrip(const FChronoTripRequest& Request)
{
	FPendingTrip Pending;
	Pending.Request = Request;
	PendingTrips.Add(Pending);
}

TArray<FChronoTripArrival> FChronoTrafficSim::TakeArrivals()
{
	TArray<FChronoTripArrival> Result = Arrivals;
	Arrivals.Reset();
	return Result;
}

void FChronoTrafficSim::ReleaseConnector(int32& InOutConnectorId)
{
	if (ConnectorHolders.IsValidIndex(InOutConnectorId))
	{
		ConnectorHolders[InOutConnectorId] = FMath::Max(ConnectorHolders[InOutConnectorId] - 1, 0);

		const int32 ToLaneId = LaneGraph.GetConnectors()[InOutConnectorId].ToLaneId;
		LaneIncoming[ToLaneId] = FMath::Max(LaneIncoming[ToLaneId] - 1, 0);
	}
	InOutConnectorId = INDEX_NONE;
}

void FChronoTrafficSim::RemoveVehicle(int32 VehicleIndex, bool bDrove)
{
	FChronoVehicle& Vehicle = Vehicles[VehicleIndex];
	if (!Vehicle.bActive)
	{
		return;
	}

	if (Vehicle.bOnConnector)
	{
		ConnectorVehicles[Vehicle.ConnectorId].Remove(VehicleIndex);
	}
	else
	{
		LaneVehicles[Vehicle.Route[Vehicle.RouteIndex]].Remove(VehicleIndex);
	}

	// A vehicle on a connector holds it through ClearingConnectorId.
	ReleaseConnector(Vehicle.ReservedConnectorId);
	ReleaseConnector(Vehicle.ClearingConnectorId);

	Arrivals.Add({ Vehicle.UserId, Vehicle.TripSeconds, bDrove });
	if (bDrove)
	{
		++Stats.TripsDriven;
		Stats.AverageTripSeconds = (Stats.TripsDriven == 1)
			? Vehicle.TripSeconds
			: Stats.AverageTripSeconds + (Vehicle.TripSeconds - Stats.AverageTripSeconds) * ChronoTrafficSimPrivate::TripTimeBlend;
	}
	else
	{
		++Stats.TripsResolvedWithoutDriving;
	}

	Vehicle = FChronoVehicle();
	FreeVehicles.Add(VehicleIndex);
}

double FChronoTrafficSim::GetLastRearOnLane(int32 LaneId, float& OutSpeed) const
{
	const TArray<int32>& OnLane = LaneVehicles[LaneId];
	if (OnLane.Num() == 0)
	{
		OutSpeed = 0.0f;
		return ChronoTrafficSimPrivate::FreeGapCm;
	}

	const FChronoVehicle& Last = Vehicles[OnLane.Last()];
	OutSpeed = Last.Speed;
	return Last.Distance - Last.Length;
}

double FChronoTrafficSim::GetLeavingRearOnLane(int32 LaneId, float& OutSpeed) const
{
	const int32 Leaver = LaneLeavers[LaneId];
	if (Vehicles.IsValidIndex(Leaver))
	{
		// Still the case only while it is in the junction, coming from this
		// lane, and not yet a full length into it.
		const FChronoVehicle& Vehicle = Vehicles[Leaver];
		if (Vehicle.bActive && Vehicle.bOnConnector && Vehicle.Route[Vehicle.RouteIndex] == LaneId && Vehicle.Distance < Vehicle.Length)
		{
			OutSpeed = Vehicle.Speed;
			return LaneGraph.GetLanes()[LaneId].Length + Vehicle.Distance - Vehicle.Length;
		}
	}

	OutSpeed = 0.0f;
	return ChronoTrafficSimPrivate::FreeGapCm;
}

bool FChronoTrafficSim::TrySpawn(const FChronoTripRequest& Request, float WaitedSeconds, const FChronoTrafficRules& Rules, bool& bOutNoRoute)
{
	bOutNoRoute = false;

	const EChronoLaneKind Kind = Request.bWalk ? EChronoLaneKind::Pedestrian : EChronoLaneKind::Vehicle;
	const int32 LaneId = LaneGraph.FindLaneOnSide(Request.FromSegmentId, Request.FromSide, Kind);
	if (LaneId == INDEX_NONE)
	{
		bOutNoRoute = true;
		return false;
	}

	const FChronoLane& Lane = LaneGraph.GetLanes()[LaneId];
	const float Length = Request.bWalk ? Rules.PedestrianLengthCm : Rules.VehicleLengthCm;
	const float MinGap = Request.bWalk ? Rules.PedestrianMinGapCm : Rules.MinGapCm;
	const float TimeHeadway = Request.bWalk ? Rules.PedestrianTimeHeadwaySeconds : Rules.TimeHeadwaySeconds;

	// The whole vehicle must be on the lane, and the stop margin away from
	// the junction at either end: pedestrians cross right along those lines.
	if (Lane.Length < Length + 2.0 * Rules.StopMarginCm)
	{
		bOutNoRoute = true;
		return false;
	}
	const double StartDistance = FMath::Clamp(LaneGraph.SegmentToLaneDistance(LaneId, Request.FromSegmentDistance), static_cast<double>(Length) + Rules.StopMarginCm, Lane.Length - Rules.StopMarginCm);

	TArray<int32> Route;
	double TargetDistance = 0.0;
	if (!LaneGraph.FindRoute(LaneId, StartDistance, Request.ToSegmentId, Request.ToSegmentDistance, Rules, &LaneSpeeds, Route, TargetDistance))
	{
		bOutNoRoute = true;
		return false;
	}

	// Room on the lane: behind the vehicle ahead, and far enough in front of
	// the vehicle behind for it to keep its distance.
	TArray<int32>& OnLane = LaneVehicles[LaneId];
	int32 InsertAt = 0;
	while (InsertAt < OnLane.Num() && Vehicles[OnLane[InsertAt]].Distance > StartDistance)
	{
		++InsertAt;
	}
	if (InsertAt > 0)
	{
		const FChronoVehicle& Ahead = Vehicles[OnLane[InsertAt - 1]];
		if ((Ahead.Distance - Ahead.Length) - StartDistance < MinGap)
		{
			return false;
		}
	}
	else
	{
		// First on the lane, but a vehicle entering the junction ahead may
		// still have its tail here.
		float LeavingSpeed = 0.0f;
		if (GetLeavingRearOnLane(LaneId, LeavingSpeed) - StartDistance < MinGap)
		{
			return false;
		}
	}
	if (InsertAt < OnLane.Num())
	{
		const FChronoVehicle& Behind = Vehicles[OnLane[InsertAt]];
		if ((StartDistance - Length) - Behind.Distance < MinGap + Behind.Speed * FMath::Max(TimeHeadway, Behind.TimeHeadway))
		{
			return false;
		}
	}
	else if (LaneIncoming[LaneId] > 0 && StartDistance - Length < (LaneIncoming[LaneId] + 1) * (Length + MinGap))
	{
		// Vehicles are about to enter this lane from the junction behind.
		return false;
	}

	int32 VehicleIndex;
	if (FreeVehicles.Num() > 0)
	{
		VehicleIndex = FreeVehicles.Last();
		FreeVehicles.RemoveAt(FreeVehicles.Num() - 1);
	}
	else
	{
		VehicleIndex = Vehicles.Num();
		Vehicles.AddDefaulted_GetRef();
	}

	FChronoVehicle& Vehicle = Vehicles[VehicleIndex];
	Vehicle = FChronoVehicle();
	Vehicle.bActive = true;
	Vehicle.UserId = Request.UserId;
	Vehicle.Route = MoveTemp(Route);
	Vehicle.Distance = StartDistance;
	Vehicle.bPedestrian = Request.bWalk;
	Vehicle.Length = Length;
	Vehicle.Width = Request.bWalk ? Rules.PedestrianWidthCm : Rules.VehicleWidthCm;
	Vehicle.MaxSpeed = Request.bWalk ? Rules.PedestrianMaxSpeedCmPerS : Rules.VehicleMaxSpeedCmPerS;
	Vehicle.MaxAcceleration = Request.bWalk ? Rules.PedestrianAccelerationCmPerS2 : Rules.MaxAccelerationCmPerS2;
	Vehicle.ComfortDeceleration = Request.bWalk ? Rules.PedestrianDecelerationCmPerS2 : Rules.ComfortDecelerationCmPerS2;
	Vehicle.MinGap = MinGap;
	Vehicle.TimeHeadway = TimeHeadway;
	Vehicle.TripSeconds = WaitedSeconds;

	// The destination is a place to stop at: like any stopped vehicle, not
	// closer to the junction ahead than the stop margin.
	const FChronoLane& LastLane = LaneGraph.GetLanes()[Vehicle.Route.Last()];
	Vehicle.TargetDistance = FMath::Min(TargetDistance, FMath::Max(LastLane.Length - Rules.StopMarginCm, 0.0));

	// A destination on the starting lane must lie ahead of the vehicle.
	if (Vehicle.Route.Num() == 1)
	{
		Vehicle.TargetDistance = FMath::Max(Vehicle.TargetDistance, StartDistance);
	}

	OnLane.Add(VehicleIndex);
	for (int32 Index = OnLane.Num() - 1; Index > InsertAt; --Index)
	{
		Swap(OnLane[Index], OnLane[Index - 1]);
	}
	return true;
}

bool FChronoTrafficSim::TryReserve(int32 VehicleIndex, int32 ConnectorId, const FChronoTrafficRules& Rules)
{
	const FChronoLaneConnector& Connector = LaneGraph.GetConnectors()[ConnectorId];

	// No vehicle on, or promised, a path that crosses or joins this one.
	for (const int32 ConflictId : Connector.Conflicts)
	{
		if (ConnectorHolders[ConflictId] > 0)
		{
			return false;
		}
	}

	// Room to leave the junction: never stop in the middle of it. Vehicles
	// already promised the same lane are counted as if they were on it.
	const FChronoVehicle& Vehicle = Vehicles[VehicleIndex];
	const FChronoLane& ExitLane = LaneGraph.GetLanes()[Connector.ToLaneId];
	const double Needed = (LaneIncoming[Connector.ToLaneId] + 1) * (static_cast<double>(Vehicle.Length) + Vehicle.MinGap);

	float LastSpeed = 0.0f;
	const double LastRear = GetLastRearOnLane(Connector.ToLaneId, LastSpeed);
	if (LaneVehicles[Connector.ToLaneId].Num() > 0)
	{
		if (LastRear < Needed)
		{
			return false;
		}
	}
	else if (ExitLane.Length < Needed && LaneIncoming[Connector.ToLaneId] > 0)
	{
		// A lane too short to hold two vehicles takes them one at a time.
		return false;
	}

	++ConnectorHolders[ConnectorId];
	++LaneIncoming[Connector.ToLaneId];
	Vehicles[VehicleIndex].ReservedConnectorId = ConnectorId;
	return true;
}

void FChronoTrafficSim::ComputeLead(int32 VehicleIndex, const FChronoTrafficRules& Rules, double& OutGap, float& OutLeadSpeed, float& OutDesiredSpeed, double& OutMaxAdvance)
{
	using namespace ChronoTrafficSimPrivate;

	FChronoVehicle& Vehicle = Vehicles[VehicleIndex];
	OutGap = FreeGapCm;
	OutLeadSpeed = 0.0f;
	OutMaxAdvance = FreeGapCm;

	// A vehicle ahead: follow it, and never come closer than the hard gap.
	auto FollowVehicle = [&OutGap, &OutLeadSpeed, &OutMaxAdvance](double Gap, float Speed)
	{
		if (Gap < OutGap)
		{
			OutGap = Gap;
			OutLeadSpeed = Speed;
		}
		OutMaxAdvance = FMath::Min(OutMaxAdvance, FMath::Max(Gap - HardGapCm, 0.0));
	};

	// A line to stop at (destination, end of lane). The driver model keeps
	// its standstill gap from what it follows, so the line is presented to it
	// as an obstacle standing that gap beyond: the vehicle then stops on the
	// line. The hard limit is the line itself.
	const double OwnMinGap = Vehicle.MinGap;
	auto StopAtLine = [&OutGap, &OutLeadSpeed, &OutMaxAdvance, OwnMinGap](double DistanceToLine)
	{
		const double Gap = FMath::Max(DistanceToLine, 0.0) + OwnMinGap;
		if (Gap < OutGap)
		{
			OutGap = Gap;
			OutLeadSpeed = 0.0f;
		}
		OutMaxAdvance = FMath::Min(OutMaxAdvance, FMath::Max(DistanceToLine, 0.0));
	};

	if (Vehicle.bOnConnector)
	{
		const FChronoLaneConnector& Connector = LaneGraph.GetConnectors()[Vehicle.ConnectorId];
		OutDesiredSpeed = FMath::Min(Rules.TurnSpeedCmPerS, Vehicle.MaxSpeed);

		const TArray<int32>& OnConnector = ConnectorVehicles[Vehicle.ConnectorId];
		const int32 Position = OnConnector.Find(VehicleIndex);
		if (Position > 0)
		{
			const FChronoVehicle& Ahead = Vehicles[OnConnector[Position - 1]];
			FollowVehicle((Ahead.Distance - Ahead.Length) - Vehicle.Distance, Ahead.Speed);
		}
		else
		{
			// Nobody ahead in the junction: the last vehicle on the lane it leads to.
			float LastSpeed = 0.0f;
			const double LastRear = GetLastRearOnLane(Connector.ToLaneId, LastSpeed);
			if (LastRear < FreeGapCm)
			{
				FollowVehicle((Connector.Length - Vehicle.Distance) + LastRear, LastSpeed);
			}
		}
		return;
	}

	const int32 LaneId = Vehicle.Route[Vehicle.RouteIndex];
	const FChronoLane& Lane = LaneGraph.GetLanes()[LaneId];
	OutDesiredSpeed = FMath::Min(Lane.SpeedLimitCmPerS, Vehicle.MaxSpeed);

	const TArray<int32>& OnLane = LaneVehicles[LaneId];
	const int32 Position = OnLane.Find(VehicleIndex);
	const bool bIsFirst = (Position == 0);
	if (Position > 0)
	{
		const FChronoVehicle& Ahead = Vehicles[OnLane[Position - 1]];
		FollowVehicle((Ahead.Distance - Ahead.Length) - Vehicle.Distance, Ahead.Speed);
	}
	else
	{
		// First on the lane: the vehicle that just entered the junction
		// ahead may still have its tail on this lane.
		float LeavingSpeed = 0.0f;
		const double LeavingRear = GetLeavingRearOnLane(LaneId, LeavingSpeed);
		if (LeavingRear < FreeGapCm)
		{
			FollowVehicle(LeavingRear - Vehicle.Distance, LeavingSpeed);
		}
	}

	const bool bLastLane = (Vehicle.RouteIndex == Vehicle.Route.Num() - 1);
	if (bLastLane)
	{
		StopAtLine(Vehicle.TargetDistance - Vehicle.Distance);
		return;
	}

	// The lane ends at a junction.
	const double ToLaneEnd = Lane.Length - Vehicle.Distance;
	const int32 NextConnectorId = LaneGraph.FindConnector(LaneId, Vehicle.Route[Vehicle.RouteIndex + 1]);

	// Slow down to turning speed on the approach.
	const float TurnSpeed = FMath::Min(Rules.TurnSpeedCmPerS, OutDesiredSpeed);
	const double BrakingDistance = (static_cast<double>(Vehicle.Speed) * Vehicle.Speed - static_cast<double>(TurnSpeed) * TurnSpeed) / (2.0 * FMath::Max(Vehicle.ComfortDeceleration, 1.0f));
	if (ToLaneEnd <= FMath::Max(BrakingDistance, 0.0) + OwnMinGap)
	{
		OutDesiredSpeed = TurnSpeed;
	}

	// Only the first vehicle of a lane asks for its way: a vehicle further
	// back could otherwise hold a path that blocks the one in front of it.
	if (Vehicle.ReservedConnectorId == INDEX_NONE && bIsFirst && NextConnectorId != INDEX_NONE && ToLaneEnd <= Rules.ReserveDistanceCm)
	{
		TryReserve(VehicleIndex, NextConnectorId, Rules);
	}

	if (Vehicle.ReservedConnectorId != INDEX_NONE)
	{
		// The way is ours: follow whoever is ahead through the junction.
		const FChronoLaneConnector& Connector = LaneGraph.GetConnectors()[Vehicle.ReservedConnectorId];
		const TArray<int32>& OnConnector = ConnectorVehicles[Vehicle.ReservedConnectorId];
		if (OnConnector.Num() > 0)
		{
			const FChronoVehicle& Ahead = Vehicles[OnConnector.Last()];
			FollowVehicle(ToLaneEnd + (Ahead.Distance - Ahead.Length), Ahead.Speed);
		}
		else
		{
			float LastSpeed = 0.0f;
			const double LastRear = GetLastRearOnLane(Connector.ToLaneId, LastSpeed);
			if (LastRear < FreeGapCm)
			{
				FollowVehicle(ToLaneEnd + Connector.Length + LastRear, LastSpeed);
			}
		}
	}
	else
	{
		// Not yet: stop before the end of the lane.
		StopAtLine(ToLaneEnd - Rules.StopMarginCm);
	}
}

void FChronoTrafficSim::Step(float DeltaSeconds, const FChronoTrafficRules& Rules)
{
	using namespace ChronoTrafficSimPrivate;

	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	// ---- 1. Trips waiting to enter the road, in the order they were asked ----
	int32 NumActive = Vehicles.Num() - FreeVehicles.Num();
	for (int32 Index = 0; Index < PendingTrips.Num();)
	{
		FPendingTrip& Pending = PendingTrips[Index];
		Pending.WaitedSeconds += DeltaSeconds;

		bool bNoRoute = false;
		bool bDone = false;
		if (NumActive < Rules.MaxActiveVehicles && TrySpawn(Pending.Request, Pending.WaitedSeconds, Rules, bNoRoute))
		{
			++NumActive;
			bDone = true;
		}
		else if (bNoRoute || Pending.WaitedSeconds > Rules.StuckTimeoutSeconds)
		{
			// No way there, or the road never freed up: the trip is counted without driving.
			Arrivals.Add({ Pending.Request.UserId, Pending.WaitedSeconds, false });
			++Stats.TripsResolvedWithoutDriving;
			bDone = true;
		}

		if (bDone)
		{
			PendingTrips.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}

	// ---- 2. Speeds, from the situation at the start of the step ----
	// MaxAdvance is how far each vehicle may move without touching what is
	// ahead of it now. Vehicles only move forward, so this holds whatever
	// the order in which they are then moved.
	TArray<double> MaxAdvance;
	MaxAdvance.Init(0.0, Vehicles.Num());


	for (int32 VehicleIndex = 0; VehicleIndex < Vehicles.Num(); ++VehicleIndex)
	{
		if (!Vehicles[VehicleIndex].bActive)
		{
			continue;
		}

		double Gap = FreeGapCm;
		float LeadSpeed = 0.0f;
		float DesiredSpeed = 0.0f;
		double Limit = FreeGapCm;
		ComputeLead(VehicleIndex, Rules, Gap, LeadSpeed, DesiredSpeed, Limit);

		FChronoVehicle& Vehicle = Vehicles[VehicleIndex];
		const double MaxAcceleration = FMath::Max(Vehicle.MaxAcceleration, 1.0f);
		const double ComfortDeceleration = FMath::Max(Vehicle.ComfortDeceleration, 1.0f);

		// Intelligent Driver Model: accelerate toward the desired speed,
		// brake according to the gap and the closing speed. Walkers follow
		// the same law with their own, much gentler, numbers.
		const double Speed = Vehicle.Speed;
		const double Desired = FMath::Max(DesiredSpeed, 1.0f);
		const double Closing = Speed - LeadSpeed;
		const double WantedGap = Vehicle.MinGap + FMath::Max(0.0, Speed * Vehicle.TimeHeadway + Speed * Closing / (2.0 * FMath::Sqrt(MaxAcceleration * ComfortDeceleration)));
		const double SafeGap = FMath::Max(Gap, 1.0);
		double Acceleration = MaxAcceleration * (1.0 - FMath::Pow(Speed / Desired, 4.0) - (WantedGap / SafeGap) * (WantedGap / SafeGap));

		// Braking is bounded: harder than comfortable, but not instant.
		Acceleration = FMath::Max(Acceleration, -4.0 * ComfortDeceleration);

		Vehicle.Speed = static_cast<float>(FMath::Clamp(Speed + Acceleration * DeltaSeconds, 0.0, Desired * 1.05));
		MaxAdvance[VehicleIndex] = Limit;
	}

	// ---- 3. Movement ----
	double SpeedSum = 0.0;
	int32 SpeedCount = 0;
	int32 NumPedestrians = 0;

	for (int32 VehicleIndex = 0; VehicleIndex < Vehicles.Num(); ++VehicleIndex)
	{
		FChronoVehicle& Vehicle = Vehicles[VehicleIndex];
		if (!Vehicle.bActive)
		{
			continue;
		}

		const double Advance = FMath::Min(static_cast<double>(Vehicle.Speed) * DeltaSeconds, MaxAdvance[VehicleIndex]);
		if (Advance < static_cast<double>(Vehicle.Speed) * DeltaSeconds)
		{
			// Held back by the hard limit: the speed follows.
			Vehicle.Speed = static_cast<float>(Advance / DeltaSeconds);
		}
		Vehicle.Distance += Advance;
		Vehicle.TripSeconds += DeltaSeconds;
		Vehicle.StoppedSeconds = (Vehicle.Speed < StoppedSpeedCmPerS) ? Vehicle.StoppedSeconds + DeltaSeconds : 0.0f;

		// The average speed is about vehicles: walkers would only drag it down.
		if (Vehicle.bPedestrian)
		{
			++NumPedestrians;
		}
		else
		{
			SpeedSum += Vehicle.Speed;
			++SpeedCount;
		}

		if (Vehicle.bOnConnector)
		{
			const FChronoLaneConnector& Connector = LaneGraph.GetConnectors()[Vehicle.ConnectorId];
			if (Vehicle.Distance >= Connector.Length)
			{
				// Out of the junction, onto the next lane, behind everybody there.
				ConnectorVehicles[Vehicle.ConnectorId].Remove(VehicleIndex);
				Vehicle.Distance -= Connector.Length;
				Vehicle.bOnConnector = false;
				Vehicle.ConnectorId = INDEX_NONE;
				++Vehicle.RouteIndex;
				LaneVehicles[Vehicle.Route[Vehicle.RouteIndex]].Add(VehicleIndex);
			}
		}
		else
		{
			const int32 LaneId = Vehicle.Route[Vehicle.RouteIndex];
			const FChronoLane& Lane = LaneGraph.GetLanes()[LaneId];

			// The rear has left the junction behind, by the same margin as
			// vehicles keep before a stop line: its path is free again.
			if (Vehicle.ClearingConnectorId != INDEX_NONE && Vehicle.Distance >= Vehicle.Length + Rules.StopMarginCm)
			{
				ReleaseConnector(Vehicle.ClearingConnectorId);
			}

			const bool bLastLane = (Vehicle.RouteIndex == Vehicle.Route.Num() - 1);
			if (bLastLane)
			{
				if (Vehicle.Distance >= Vehicle.TargetDistance - ArrivalToleranceCm)
				{
					RemoveVehicle(VehicleIndex, true);
					continue;
				}
			}
			else if (Vehicle.Distance >= Lane.Length && Vehicle.ReservedConnectorId != INDEX_NONE)
			{
				// Into the junction, on the reserved path. A previous
				// junction still being cleared (very short road) is let go.
				LaneVehicles[LaneId].Remove(VehicleIndex);
				ReleaseConnector(Vehicle.ClearingConnectorId);

				LaneLeavers[LaneId] = VehicleIndex;
				Vehicle.Distance -= Lane.Length;
				Vehicle.bOnConnector = true;
				Vehicle.ConnectorId = Vehicle.ReservedConnectorId;
				Vehicle.ClearingConnectorId = Vehicle.ReservedConnectorId;
				Vehicle.ReservedConnectorId = INDEX_NONE;
				ConnectorVehicles[Vehicle.ConnectorId].Add(VehicleIndex);
			}

			if (!Vehicle.bOnConnector)
			{
				LaneSpeeds[LaneId] += (Vehicle.Speed - LaneSpeeds[LaneId]) * LaneSpeedBlend;
			}
		}

		// A vehicle that has not moved for too long is taken off the road,
		// so that one blockage cannot freeze the whole city.
		if (Vehicle.bActive && Vehicle.StoppedSeconds > Rules.StuckTimeoutSeconds)
		{
			RemoveVehicle(VehicleIndex, false);
		}
	}

	// Empty lanes drift back to their free speed.
	for (int32 LaneId = 0; LaneId < LaneSpeeds.Num(); ++LaneId)
	{
		if (LaneVehicles[LaneId].Num() == 0)
		{
			const float FreeSpeed = LaneGraph.GetLanes()[LaneId].SpeedLimitCmPerS;
			LaneSpeeds[LaneId] += (FreeSpeed - LaneSpeeds[LaneId]) * LaneSpeedBlend;
		}
	}

	Stats.ActiveVehicles = Vehicles.Num() - FreeVehicles.Num();
	Stats.ActivePedestrians = NumPedestrians;
	Stats.WaitingTrips = PendingTrips.Num();
	Stats.AverageSpeedCmPerS = SpeedCount > 0 ? static_cast<float>(SpeedSum / SpeedCount) : 0.0f;
}

bool FChronoTrafficSim::GetVehicleTransform(int32 VehicleIndex, FVector& OutPosition, FVector& OutDirection) const
{
	if (!Vehicles.IsValidIndex(VehicleIndex) || !Vehicles[VehicleIndex].bActive)
	{
		return false;
	}

	const FChronoVehicle& Vehicle = Vehicles[VehicleIndex];

	// A point of the vehicle, given as a distance along the lane or connector
	// its front is on. Behind the start of that one, the point is still on
	// the one the vehicle is leaving.
	auto SampleAt = [this, &Vehicle](double Distance, FVector& Position, FVector& Direction)
	{
		const int32 LaneId = Vehicle.Route[Vehicle.RouteIndex];
		if (Vehicle.bOnConnector)
		{
			if (Distance >= 0.0)
			{
				LaneGraph.SampleConnector(Vehicle.ConnectorId, Distance, Position, Direction);
			}
			else
			{
				LaneGraph.SampleLane(LaneId, LaneGraph.GetLanes()[LaneId].Length + Distance, Position, Direction);
			}
			return;
		}

		const int32 ConnectorId = (Distance < 0.0 && Vehicle.RouteIndex > 0) ? LaneGraph.FindConnector(Vehicle.Route[Vehicle.RouteIndex - 1], LaneId) : INDEX_NONE;
		if (ConnectorId != INDEX_NONE)
		{
			LaneGraph.SampleConnector(ConnectorId, LaneGraph.GetConnectors()[ConnectorId].Length + Distance, Position, Direction);
		}
		else
		{
			LaneGraph.SampleLane(LaneId, FMath::Max(Distance, 0.0), Position, Direction);
		}
	};

	// The middle of the vehicle is half a length behind its front.
	SampleAt(Vehicle.Distance - 0.5 * Vehicle.Length, OutPosition, OutDirection);

	if (Vehicle.bPedestrian)
	{
		// A walker stands on the highest surface under it, so that stepping
		// on or off a sidewalk never puts its feet inside the curb, and
		// stays upright whatever the slope.
		FVector Position;
		FVector Direction;
		SampleAt(Vehicle.Distance, Position, Direction);
		OutPosition.Z = FMath::Max(OutPosition.Z, Position.Z);
		SampleAt(Vehicle.Distance - Vehicle.Length, Position, Direction);
		OutPosition.Z = FMath::Max(OutPosition.Z, Position.Z);

		const FVector Flat(OutDirection.X, OutDirection.Y, 0.0);
		OutDirection = Flat.SizeSquared() > 1.0e-6 ? Flat.GetSafeNormal() : FVector(1.0, 0.0, 0.0);
	}
	return true;
}

bool FChronoTrafficSim::CheckInvariants(FString& OutError) const
{
	// 1. On every lane and connector: front-most first, and nobody inside the vehicle ahead.
	auto CheckLine = [this, &OutError](const TArray<int32>& Line, const TCHAR* What, int32 Id)
	{
		for (int32 Index = 1; Index < Line.Num(); ++Index)
		{
			const FChronoVehicle& Ahead = Vehicles[Line[Index - 1]];
			const FChronoVehicle& Behind = Vehicles[Line[Index]];
			const double Gap = (Ahead.Distance - Ahead.Length) - Behind.Distance;
			if (Gap < -0.01)
			{
				OutError = FString::Printf(TEXT("%s %d: vehicle %d is %.1f cm inside vehicle %d"), What, Id, Line[Index], -Gap, Line[Index - 1]);
				return false;
			}
		}
		return true;
	};

	for (int32 LaneId = 0; LaneId < LaneVehicles.Num(); ++LaneId)
	{
		if (!CheckLine(LaneVehicles[LaneId], TEXT("Lane"), LaneId))
		{
			return false;
		}
	}
	for (int32 ConnectorId = 0; ConnectorId < ConnectorVehicles.Num(); ++ConnectorId)
	{
		if (!CheckLine(ConnectorVehicles[ConnectorId], TEXT("Connector"), ConnectorId))
		{
			return false;
		}
	}

	// 2. A vehicle entering a lane from a junction does not run into the last one on it.
	for (int32 ConnectorId = 0; ConnectorId < ConnectorVehicles.Num(); ++ConnectorId)
	{
		if (ConnectorVehicles[ConnectorId].Num() == 0)
		{
			continue;
		}

		const FChronoLaneConnector& Connector = LaneGraph.GetConnectors()[ConnectorId];
		const TArray<int32>& OnExitLane = LaneVehicles[Connector.ToLaneId];
		if (OnExitLane.Num() > 0)
		{
			const FChronoVehicle& First = Vehicles[ConnectorVehicles[ConnectorId][0]];
			const FChronoVehicle& Last = Vehicles[OnExitLane.Last()];
			const double Gap = (Connector.Length - First.Distance) + (Last.Distance - Last.Length);
			if (Gap < -0.01)
			{
				OutError = FString::Printf(TEXT("Connector %d: vehicle %d is %.1f cm inside the last vehicle of lane %d"), ConnectorId, ConnectorVehicles[ConnectorId][0], -Gap, Connector.ToLaneId);
				return false;
			}
		}
	}

	// 3. The counts of holders match the vehicles, and two conflicting paths are never held at once.
	TArray<int32> Recount;
	Recount.Init(0, ConnectorHolders.Num());
	for (const FChronoVehicle& Vehicle : Vehicles)
	{
		if (!Vehicle.bActive)
		{
			continue;
		}
		if (Vehicle.ReservedConnectorId != INDEX_NONE)
		{
			++Recount[Vehicle.ReservedConnectorId];
		}
		if (Vehicle.ClearingConnectorId != INDEX_NONE)
		{
			++Recount[Vehicle.ClearingConnectorId];
		}
		if (Vehicle.bOnConnector && Vehicle.ClearingConnectorId != Vehicle.ConnectorId)
		{
			OutError = TEXT("A vehicle is crossing a junction on a path it does not hold");
			return false;
		}
	}
	for (int32 ConnectorId = 0; ConnectorId < ConnectorHolders.Num(); ++ConnectorId)
	{
		if (Recount[ConnectorId] != ConnectorHolders[ConnectorId])
		{
			OutError = FString::Printf(TEXT("Connector %d: %d holders counted, %d recorded"), ConnectorId, Recount[ConnectorId], ConnectorHolders[ConnectorId]);
			return false;
		}
		if (ConnectorHolders[ConnectorId] > 0)
		{
			for (const int32 ConflictId : LaneGraph.GetConnectors()[ConnectorId].Conflicts)
			{
				if (ConnectorHolders[ConflictId] > 0)
				{
					OutError = FString::Printf(TEXT("Conflicting paths %d and %d of node %d are in use at the same time"), ConnectorId, ConflictId, LaneGraph.GetConnectors()[ConnectorId].NodeId);
					return false;
				}
			}
		}
	}

	return true;
}

bool FChronoTrafficSim::CheckNoOverlap(double ToleranceCm, FString& OutError) const
{
	// Footprint of every agent, then a grid so that only neighbors are compared.
	struct FAgentBox
	{
		int32 VehicleIndex = INDEX_NONE;
		FChronoOrientedBox2D Box;
	};

	TArray<FAgentBox> Boxes;
	double LargestCm = 100.0;
	for (int32 VehicleIndex = 0; VehicleIndex < Vehicles.Num(); ++VehicleIndex)
	{
		FVector Position;
		FVector Direction;
		if (!GetVehicleTransform(VehicleIndex, Position, Direction))
		{
			continue;
		}

		const FChronoVehicle& Vehicle = Vehicles[VehicleIndex];
		FAgentBox& Agent = Boxes.AddDefaulted_GetRef();
		Agent.VehicleIndex = VehicleIndex;
		Agent.Box = FChronoOrientedBox2D(FVector2D(Position.X, Position.Y), FVector2D(0.5 * Vehicle.Length, 0.5 * Vehicle.Width), FMath::Atan2(Direction.Y, Direction.X));
		LargestCm = FMath::Max(LargestCm, static_cast<double>(FMath::Max(Vehicle.Length, Vehicle.Width)));
	}

	// Two boxes that overlap have centers closer than the largest diagonal,
	// so they are in the same cell or in adjacent ones.
	const double CellCm = 1.5 * LargestCm;
	TMap<FIntPoint, TArray<int32>> Cells;
	for (int32 Index = 0; Index < Boxes.Num(); ++Index)
	{
		const FVector2D& Center = Boxes[Index].Box.Center;
		Cells.FindOrAdd(FIntPoint(FMath::FloorToInt32(Center.X / CellCm), FMath::FloorToInt32(Center.Y / CellCm))).Add(Index);
	}

	for (int32 Index = 0; Index < Boxes.Num(); ++Index)
	{
		const FVector2D& Center = Boxes[Index].Box.Center;
		const FIntPoint Cell(FMath::FloorToInt32(Center.X / CellCm), FMath::FloorToInt32(Center.Y / CellCm));
		for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
		{
			for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
			{
				const TArray<int32>* Others = Cells.Find(FIntPoint(Cell.X + OffsetX, Cell.Y + OffsetY));
				if (!Others)
				{
					continue;
				}
				for (const int32 Other : *Others)
				{
					if (Other > Index && FChronoOrientedBox2D::Overlap(Boxes[Index].Box, Boxes[Other].Box, ToleranceCm))
					{
						const FChronoVehicle& A = Vehicles[Boxes[Index].VehicleIndex];
						const FChronoVehicle& B = Vehicles[Boxes[Other].VehicleIndex];
						OutError = FString::Printf(TEXT("%s %d overlaps %s %d at (%.0f, %.0f)"),
							A.bPedestrian ? TEXT("Pedestrian") : TEXT("Vehicle"), Boxes[Index].VehicleIndex,
							B.bPedestrian ? TEXT("pedestrian") : TEXT("vehicle"), Boxes[Other].VehicleIndex,
							Center.X, Center.Y);
						return false;
					}
				}
			}
		}
	}

	return true;
}
