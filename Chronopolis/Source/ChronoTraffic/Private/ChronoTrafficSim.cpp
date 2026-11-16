#include "ChronoTrafficSim.h"

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

bool FChronoTrafficSim::TrySpawn(const FChronoTripRequest& Request, float WaitedSeconds, const FChronoTrafficRules& Rules, bool& bOutNoRoute)
{
	bOutNoRoute = false;

	const int32 LaneId = LaneGraph.FindLaneOnSide(Request.FromSegmentId, Request.FromSide);
	if (LaneId == INDEX_NONE)
	{
		bOutNoRoute = true;
		return false;
	}

	const FChronoLane& Lane = LaneGraph.GetLanes()[LaneId];
	const float Length = Rules.VehicleLengthCm;

	// The whole vehicle must be on the lane, not sticking out into a junction.
	if (Lane.Length < Length + Rules.StopMarginCm)
	{
		bOutNoRoute = true;
		return false;
	}
	const double StartDistance = FMath::Clamp(LaneGraph.SegmentToLaneDistance(LaneId, Request.FromSegmentDistance), static_cast<double>(Length), Lane.Length - Rules.StopMarginCm);

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
		if ((Ahead.Distance - Ahead.Length) - StartDistance < Rules.MinGapCm)
		{
			return false;
		}
	}
	if (InsertAt < OnLane.Num())
	{
		const FChronoVehicle& Behind = Vehicles[OnLane[InsertAt]];
		if ((StartDistance - Length) - Behind.Distance < Rules.MinGapCm + Behind.Speed * Rules.TimeHeadwaySeconds)
		{
			return false;
		}
	}
	else if (LaneIncoming[LaneId] > 0 && StartDistance - Length < (LaneIncoming[LaneId] + 1) * (Length + Rules.MinGapCm))
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
	Vehicle.Length = Length;
	Vehicle.TargetDistance = TargetDistance;
	Vehicle.TripSeconds = WaitedSeconds;

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
	const double Needed = (LaneIncoming[Connector.ToLaneId] + 1) * (static_cast<double>(Vehicle.Length) + Rules.MinGapCm);

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
	auto StopAtLine = [&OutGap, &OutLeadSpeed, &OutMaxAdvance, &Rules](double DistanceToLine)
	{
		const double Gap = FMath::Max(DistanceToLine, 0.0) + Rules.MinGapCm;
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
		OutDesiredSpeed = FMath::Min(Rules.TurnSpeedCmPerS, Rules.VehicleMaxSpeedCmPerS);

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
	OutDesiredSpeed = FMath::Min(Lane.SpeedLimitCmPerS, Rules.VehicleMaxSpeedCmPerS);

	const TArray<int32>& OnLane = LaneVehicles[LaneId];
	const int32 Position = OnLane.Find(VehicleIndex);
	const bool bIsFirst = (Position == 0);
	if (Position > 0)
	{
		const FChronoVehicle& Ahead = Vehicles[OnLane[Position - 1]];
		FollowVehicle((Ahead.Distance - Ahead.Length) - Vehicle.Distance, Ahead.Speed);
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
	const double BrakingDistance = (static_cast<double>(Vehicle.Speed) * Vehicle.Speed - static_cast<double>(TurnSpeed) * TurnSpeed) / (2.0 * FMath::Max(Rules.ComfortDecelerationCmPerS2, 1.0f));
	if (ToLaneEnd <= FMath::Max(BrakingDistance, 0.0) + Rules.MinGapCm)
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

	const double MaxAcceleration = FMath::Max(Rules.MaxAccelerationCmPerS2, 1.0f);
	const double ComfortDeceleration = FMath::Max(Rules.ComfortDecelerationCmPerS2, 1.0f);

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

		// Intelligent Driver Model: accelerate toward the desired speed,
		// brake according to the gap and the closing speed.
		const double Speed = Vehicle.Speed;
		const double Desired = FMath::Max(DesiredSpeed, 1.0f);
		const double Closing = Speed - LeadSpeed;
		const double WantedGap = Rules.MinGapCm + FMath::Max(0.0, Speed * Rules.TimeHeadwaySeconds + Speed * Closing / (2.0 * FMath::Sqrt(MaxAcceleration * ComfortDeceleration)));
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

		SpeedSum += Vehicle.Speed;
		++SpeedCount;

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

			// The rear has left the junction behind: its path is free again.
			if (Vehicle.ClearingConnectorId != INDEX_NONE && Vehicle.Distance >= Vehicle.Length)
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
	Stats.WaitingTrips = PendingTrips.Num();
	Stats.AverageSpeedCmPerS = SpeedCount > 0 ? static_cast<float>(SpeedSum / SpeedCount) : 0.0f;
}

bool FChronoTrafficSim::GetVehicleTransform(int32 VehicleIndex, FVector& OutPosition, FVector& OutDirection) const
{
	if (!Vehicles.IsValidIndex(VehicleIndex) || !Vehicles[VehicleIndex].bActive)
	{
		return false;
	}

	// The middle of the vehicle is half a length behind its front.
	const FChronoVehicle& Vehicle = Vehicles[VehicleIndex];
	const double Middle = Vehicle.Distance - 0.5 * Vehicle.Length;

	if (Vehicle.bOnConnector)
	{
		if (Middle >= 0.0)
		{
			LaneGraph.SampleConnector(Vehicle.ConnectorId, Middle, OutPosition, OutDirection);
		}
		else
		{
			// The middle is still on the lane the vehicle is leaving.
			const int32 LaneId = Vehicle.Route[Vehicle.RouteIndex];
			LaneGraph.SampleLane(LaneId, LaneGraph.GetLanes()[LaneId].Length + Middle, OutPosition, OutDirection);
		}
		return true;
	}

	const int32 LaneId = Vehicle.Route[Vehicle.RouteIndex];
	if (Middle >= 0.0 || Vehicle.RouteIndex == 0)
	{
		LaneGraph.SampleLane(LaneId, FMath::Max(Middle, 0.0), OutPosition, OutDirection);
	}
	else
	{
		// The middle is still in the junction the vehicle is leaving.
		const int32 ConnectorId = LaneGraph.FindConnector(Vehicle.Route[Vehicle.RouteIndex - 1], LaneId);
		if (ConnectorId != INDEX_NONE)
		{
			LaneGraph.SampleConnector(ConnectorId, LaneGraph.GetConnectors()[ConnectorId].Length + Middle, OutPosition, OutDirection);
		}
		else
		{
			LaneGraph.SampleLane(LaneId, 0.0, OutPosition, OutDirection);
		}
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
