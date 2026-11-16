// Vehicles as real agents on the lane graph. Each one follows its route,
// keeps a safe distance from the vehicle ahead (Intelligent Driver Model),
// and only enters a junction once it has reserved a path that no other
// vehicle is using or crossing. Plain data, with no world or rendering
// dependency.
//
// Zero clipping: vehicles never leave their lane, never overlap the vehicle
// ahead, and two crossing paths of a junction are never used at once.

#pragma once

#include "CoreMinimal.h"
#include "ChronoLaneGraph.h"

/** A trip to make: from a place along a road to a place along a road. */
struct FChronoTripRequest
{
	int32 FromSegmentId = INDEX_NONE;
	double FromSegmentDistance = 0.0;
	/** Side of the road the trip starts from (+1 or -1), as for buildings. */
	int32 FromSide = 1;

	int32 ToSegmentId = INDEX_NONE;
	double ToSegmentDistance = 0.0;

	/** Free for the caller: who makes the trip. Returned with the arrival. */
	int32 UserId = INDEX_NONE;
};

/** A trip that ended. */
struct FChronoTripArrival
{
	int32 UserId = INDEX_NONE;
	/** Time from the request to the arrival. */
	float Seconds = 0.0f;
	/** False if the trip was resolved without driving (no route, road full, vehicle stuck). */
	bool bDrove = true;
};

struct FChronoVehicle
{
	bool bActive = false;
	int32 UserId = INDEX_NONE;

	/** Lanes to follow, and the one the vehicle is on or heading to. */
	TArray<int32> Route;
	int32 RouteIndex = 0;

	/** True while crossing a junction, on connector ConnectorId toward Route[RouteIndex + 1]. */
	bool bOnConnector = false;
	int32 ConnectorId = INDEX_NONE;

	/** Position of the front of the vehicle along its lane or connector. */
	double Distance = 0.0;
	float Speed = 0.0f;
	float Length = 400.0f;

	/** Where to stop on the last lane. */
	double TargetDistance = 0.0;

	/** Connector reserved for the next junction, not entered yet. */
	int32 ReservedConnectorId = INDEX_NONE;
	/** Connector that the rear of the vehicle has not left yet. */
	int32 ClearingConnectorId = INDEX_NONE;

	float TripSeconds = 0.0f;
	float StoppedSeconds = 0.0f;
};

struct FChronoTrafficStats
{
	int32 ActiveVehicles = 0;
	int32 WaitingTrips = 0;

	/** Since the network was last set. */
	int32 TripsDriven = 0;
	int32 TripsResolvedWithoutDriving = 0;

	/** Average speed of the vehicles on the road, in cm/s. */
	float AverageSpeedCmPerS = 0.0f;
	/** Average duration of the recent trips that were driven, in seconds. */
	float AverageTripSeconds = 0.0f;
};

class CHRONOTRAFFIC_API FChronoTrafficSim
{
public:
	/**
	 * Rebuilds the lanes from the road graph. Every vehicle on the road and
	 * every waiting trip is resolved at once: lanes are rebuilt from scratch,
	 * so no vehicle could be left on a road that changed.
	 */
	void SetNetwork(const FChronoRoadGraph& Graph, const FChronoTrafficRules& Rules);

	const FChronoLaneGraph& GetLaneGraph() const { return LaneGraph; }

	/** Queues a trip. It starts as soon as the road is free at its origin. */
	void RequestTrip(const FChronoTripRequest& Request);

	/** Advances every vehicle by DeltaSeconds. */
	void Step(float DeltaSeconds, const FChronoTrafficRules& Rules);

	/** Trips that ended since the last call. */
	TArray<FChronoTripArrival> TakeArrivals();

	const TArray<FChronoVehicle>& GetVehicles() const { return Vehicles; }
	const FChronoTrafficStats& GetStats() const { return Stats; }

	/** Current speed of traffic on each lane (cm/s), indexed by lane id. */
	const TArray<float>& GetLaneSpeeds() const { return LaneSpeeds; }

	/** Position of the middle of a vehicle and its direction of travel. False for a free slot. */
	bool GetVehicleTransform(int32 VehicleIndex, FVector& OutPosition, FVector& OutDirection) const;

	/**
	 * Checks the zero-clipping contract of traffic: no vehicle overlaps the
	 * one ahead, and no two conflicting paths of a junction are in use at
	 * once. Returns false and explains in OutError.
	 */
	bool CheckInvariants(FString& OutError) const;

private:
	struct FPendingTrip
	{
		FChronoTripRequest Request;
		float WaitedSeconds = 0.0f;
	};

	bool TrySpawn(const FChronoTripRequest& Request, float WaitedSeconds, const FChronoTrafficRules& Rules, bool& bOutNoRoute);
	void RemoveVehicle(int32 VehicleIndex, bool bDrove);
	void ReleaseConnector(int32& InOutConnectorId);

	/**
	 * What the vehicle must react to: the gap and speed of the obstacle ahead
	 * for the driver model, and the distance it may cover at most in this step
	 * without touching a vehicle or passing a line it must stop at.
	 */
	void ComputeLead(int32 VehicleIndex, const FChronoTrafficRules& Rules, double& OutGap, float& OutLeadSpeed, float& OutDesiredSpeed, double& OutMaxAdvance);
	bool TryReserve(int32 VehicleIndex, int32 ConnectorId, const FChronoTrafficRules& Rules);

	/** Rear position of the last vehicle on a lane, or a very large value if the lane is empty. */
	double GetLastRearOnLane(int32 LaneId, float& OutSpeed) const;

	FChronoLaneGraph LaneGraph;

	TArray<FChronoVehicle> Vehicles;
	TArray<int32> FreeVehicles;

	/** Vehicles on each lane and connector, front-most first. */
	TArray<TArray<int32>> LaneVehicles;
	TArray<TArray<int32>> ConnectorVehicles;

	/** Vehicles that reserved, are crossing, or are still clearing each connector. */
	TArray<int32> ConnectorHolders;
	/** Vehicles with a reservation or inside a junction, heading to each lane. */
	TArray<int32> LaneIncoming;

	TArray<float> LaneSpeeds;

	TArray<FPendingTrip> PendingTrips;
	TArray<FChronoTripArrival> Arrivals;

	FChronoTrafficStats Stats;
};
