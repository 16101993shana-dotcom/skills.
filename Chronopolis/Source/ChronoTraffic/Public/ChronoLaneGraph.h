// Lane-level description of the road network, derived from the road graph:
// one lane per direction of travel (more on wider roads), and at every
// junction the short paths (connectors) that lead from an arriving lane to a
// departing one. Vehicles drive on lanes and connectors, never off them.
// Plain data, with no world or rendering dependency.

#pragma once

#include "CoreMinimal.h"

class FChronoRoadGraph;

/** Who may use a lane. */
enum class EChronoLaneKind : uint8
{
	/** On the carriageway. */
	Vehicle,
	/** On the sidewalk, or along the edge of a road without sidewalks. */
	Pedestrian
};

/** Tunables of traffic. In game they come from UChronoTrafficSettings. */
struct CHRONOTRAFFIC_API FChronoTrafficRules
{
	/** Right-hand traffic (true) or left-hand traffic (false). */
	bool bDriveOnRight = true;

	/** Spacing of the points of a lane. */
	float SampleSpacingCm = 200.0f;

	/** Speed inside a junction. */
	float TurnSpeedCmPerS = 300.0f;

	/** Two paths through a junction closer than this cannot be used at the same time. */
	float ConflictClearanceCm = 180.0f;

	/** Same, between two paths that are both for pedestrians. */
	float PedestrianClearanceCm = 60.0f;

	/** On a road without sidewalks, pedestrians walk along the edge of the carriageway, this far inside it. */
	float RoadEdgeMarginCm = 10.0f;

	/** Room kept between such a pedestrian and the vehicles of the nearest lane. A road too narrow for it is not walked. */
	float PedestrianSideClearanceCm = 20.0f;

	/** Where pedestrians cross the street they came by, the crossing runs this far inside the junction. */
	float CrossingInsetCm = 30.0f;

	// ---- Vehicles (one generic kind for now; each era brings its own) ----
	float VehicleLengthCm = 400.0f;
	float VehicleWidthCm = 180.0f;
	float VehicleMaxSpeedCmPerS = 1000.0f;
	float MaxAccelerationCmPerS2 = 150.0f;
	float ComfortDecelerationCmPerS2 = 200.0f;
	/** Gap kept behind a stopped vehicle. */
	float MinGapCm = 150.0f;
	/** Time gap kept behind a moving vehicle. */
	float TimeHeadwaySeconds = 1.2f;
	/** Distance from the end of a lane at which a waiting vehicle stops. */
	float StopMarginCm = 50.0f;
	/** A vehicle asks for its way through a junction from this distance. */
	float ReserveDistanceCm = 1500.0f;

	/** A trip that cannot enter the road, or a vehicle that has not moved, for this long is resolved without driving. */
	float StuckTimeoutSeconds = 120.0f;

	/** Vehicles on the road at one time. Trips beyond that are resolved without driving. */
	int32 MaxActiveVehicles = 20000;

	// ---- Pedestrians ----
	float PedestrianLengthCm = 50.0f;
	float PedestrianWidthCm = 50.0f;
	float PedestrianMaxSpeedCmPerS = 140.0f;
	float PedestrianAccelerationCmPerS2 = 100.0f;
	float PedestrianDecelerationCmPerS2 = 150.0f;
	float PedestrianMinGapCm = 40.0f;
	float PedestrianTimeHeadwaySeconds = 0.5f;

	/** A trip between two buildings closer than this, as the crow flies, is walked. */
	float WalkDistanceCm = 30000.0f;

	static FChronoTrafficRules FromSettings();
};

struct FChronoLane
{
	int32 Id = INDEX_NONE;
	int32 SegmentId = INDEX_NONE;
	int32 FromNodeId = INDEX_NONE;
	int32 ToNodeId = INDEX_NONE;

	EChronoLaneKind Kind = EChronoLaneKind::Vehicle;

	/** True if the lane runs from the start node of its segment to the end node. */
	bool bForward = true;

	/** 0 for the lane next to the centerline, increasing outward. */
	int32 LaneIndex = 0;

	/** Centerline of the lane, in the direction of travel. */
	TArray<FVector> Points;
	/** Distance from the first point to each point. */
	TArray<double> Distances;
	double Length = 0.0;

	float SpeedLimitCmPerS = 800.0f;

	/** Part of the segment centerline that the lane covers (between the junctions). */
	double SegmentStartDistance = 0.0;
	double SegmentEndDistance = 0.0;

	/** Connectors that leave the end of this lane. */
	TArray<int32> OutConnectors;
};

struct FChronoLaneConnector
{
	int32 Id = INDEX_NONE;
	int32 NodeId = INDEX_NONE;
	int32 FromLaneId = INDEX_NONE;
	int32 ToLaneId = INDEX_NONE;

	TArray<FVector> Points;
	TArray<double> Distances;
	double Length = 0.0;

	/** Connectors of the same junction that cross this one or merge into the same lane. */
	TArray<int32> Conflicts;
};

class CHRONOTRAFFIC_API FChronoLaneGraph
{
public:
	/** Rebuilds every lane and connector from the road graph. Ids are not stable across rebuilds. */
	void Build(const FChronoRoadGraph& Graph, const FChronoTrafficRules& Rules);

	void Reset();

	const TArray<FChronoLane>& GetLanes() const { return Lanes; }
	const TArray<FChronoLaneConnector>& GetConnectors() const { return Connectors; }

	/** Lanes of a road segment (empty if the segment has none). */
	TConstArrayView<int32> GetSegmentLanes(int32 SegmentId) const;

	/**
	 * Lane of a segment that runs along the given side (+1 or -1, as for
	 * zoning cells and buildings): the lane someone leaving a building on
	 * that side enters. INDEX_NONE if the segment has no lane of that kind.
	 */
	int32 FindLaneOnSide(int32 SegmentId, int32 Side, EChronoLaneKind Kind = EChronoLaneKind::Vehicle) const;

	/** Distance along a lane of the point facing a distance along its segment centerline. */
	double SegmentToLaneDistance(int32 LaneId, double SegmentDistance) const;

	/** Position and direction of travel at a distance along a lane or a connector. */
	void SampleLane(int32 LaneId, double Distance, FVector& OutPosition, FVector& OutDirection) const;
	void SampleConnector(int32 ConnectorId, double Distance, FVector& OutPosition, FVector& OutDirection) const;

	/** Connector that leads from one lane to another, or INDEX_NONE. */
	int32 FindConnector(int32 FromLaneId, int32 ToLaneId) const;

	/**
	 * Fastest route from a point on a lane to a point on a segment, by any of
	 * its lanes of the same kind (vehicles stay on the carriageway,
	 * pedestrians on the sidewalks).
	 * @param LaneSpeeds optional current speed of traffic per lane (cm/s), to
	 * route around congestion. Null uses the speed limits.
	 * @param OutLaneIds lanes to follow, starting with FromLaneId.
	 * @param OutTargetDistance where to stop on the last lane.
	 * @return false if there is no way.
	 */
	bool FindRoute(int32 FromLaneId, double FromDistance, int32 ToSegmentId, double ToSegmentDistance, const FChronoTrafficRules& Rules, const TArray<float>* LaneSpeeds, TArray<int32>& OutLaneIds, double& OutTargetDistance) const;

private:
	TArray<FChronoLane> Lanes;
	TArray<FChronoLaneConnector> Connectors;
	TMap<int32, TArray<int32>> LanesBySegment;
	bool bDriveOnRight = true;
};
