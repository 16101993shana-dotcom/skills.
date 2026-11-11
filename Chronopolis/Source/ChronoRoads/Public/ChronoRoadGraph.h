// The road network as plain data: nodes, segments and the distances by which
// segments are cut back at intersections. Cheap to copy, so an edit can be
// tried on a copy and discarded if it is invalid.

#pragma once

#include "CoreMinimal.h"
#include "ChronoRoadGeometry.h"
#include "ChronoRoadTypes.h"

struct FChronoRoadNode
{
	int32 Id = INDEX_NONE;

	/** World position. Z is the height of the road surface at the node. */
	FVector Position = FVector::ZeroVector;

	TArray<int32> SegmentIds;
};

struct FChronoRoadSegment
{
	int32 Id = INDEX_NONE;
	int32 StartNodeId = INDEX_NONE;
	int32 EndNodeId = INDEX_NONE;

	/** Centerline in the ground plane, from the start node to the end node. */
	FChronoBezier2D Curve;

	FChronoRoadProfileData Profile;

	/** Length of the centerline, node to node. */
	double Length = 0.0;

	/** Part of the segment replaced by the junction mesh at each end. */
	float TrimStart = 0.0f;
	float TrimEnd = 0.0f;

	/** Bounds of the centerline grown by the half width. */
	FBox2D Bounds = FBox2D(ForceInit);

	/** Length that actually carries a road mesh. */
	double GetUsableLength() const { return Length - TrimStart - TrimEnd; }
};

class CHRONOROADS_API FChronoRoadGraph
{
public:
	/** Smallest trim applied where two or more segments meet. Set once from the project settings. */
	void SetMinTrimCm(float InMinTrimCm) { MinTrimCm = InMinTrimCm; }

	/** Distance over which a road stays level after leaving a junction. Set once from the project settings. */
	void SetJunctionLevelRunCm(float InLevelRunCm) { LevelRunCm = FMath::Max(InLevelRunCm, 0.0f); }

	int32 AddNode(const FVector& Position);

	/** Adds a segment between two existing nodes. Returns INDEX_NONE on invalid input. */
	int32 AddSegment(int32 StartNodeId, int32 EndNodeId, const FChronoBezier2D& Curve, const FChronoRoadProfileData& Profile);

	/** Removes a segment and any node left without a segment. */
	bool RemoveSegment(int32 SegmentId);

	/**
	 * Cuts a segment in two at curve parameter T and inserts a node there.
	 * The original segment keeps its id and becomes the first half.
	 * @return the id of the new node, or INDEX_NONE.
	 */
	int32 SplitSegment(int32 SegmentId, double T);

	const FChronoRoadNode* FindNode(int32 NodeId) const { return Nodes.Find(NodeId); }
	const FChronoRoadSegment* FindSegment(int32 SegmentId) const { return Segments.Find(SegmentId); }
	const TMap<int32, FChronoRoadNode>& GetNodes() const { return Nodes; }
	const TMap<int32, FChronoRoadSegment>& GetSegments() const { return Segments; }

	/** Nearest node within MaxDistanceCm, or INDEX_NONE. */
	int32 FindNearestNode(const FVector2D& Point, double MaxDistanceCm) const;

	/** Nearest segment centerline within MaxDistanceCm, or INDEX_NONE. */
	int32 FindNearestSegment(const FVector2D& Point, double MaxDistanceCm, double& OutT, FVector2D& OutPoint) const;

	/** Roads arriving at a node, in the order of the node's SegmentIds. */
	void GetNodeArms(int32 NodeId, TArray<FChronoRoadArm>& OutArms) const;

	/** Smallest angle between two neighboring roads at a node (360 for a dead end). */
	float GetNodeMinAngleDegrees(int32 NodeId) const;

	/** Radius of the disc around a node that contains its whole junction. */
	float GetNodeRadiusCm(int32 NodeId) const;

	/**
	 * Height of the road surface at a distance from the start node. The road is
	 * level inside each junction and over the level run that follows it, and has
	 * a constant grade in between.
	 */
	double GetSegmentHeightAtDistance(const FChronoRoadSegment& Segment, double Distance) const;

	/** Length of the part of a segment that carries the grade. */
	double GetSlopedLength(const FChronoRoadSegment& Segment) const;

	/** Grade of the sloped part of a segment, in percent. */
	double GetSegmentSlopePercent(const FChronoRoadSegment& Segment) const;

	/** Centerline of a whole segment as 3D points, node to node. */
	void BuildSegmentPath(const FChronoRoadSegment& Segment, double SpacingCm, TArray<FVector>& OutPath) const;

	/**
	 * Binary serialization for save games (both directions). Loading replaces
	 * the whole graph and returns false, leaving it empty, on invalid data.
	 */
	bool Serialize(FArchive& Ar);

	// ---- Change tracking (consumed by UChronoRoadSubsystem) ----

	const TSet<int32>& GetDirtySegments() const { return DirtySegments; }
	const TSet<int32>& GetDirtyNodes() const { return DirtyNodes; }
	const TSet<int32>& GetRemovedSegments() const { return RemovedSegments; }
	const TSet<int32>& GetRemovedNodes() const { return RemovedNodes; }
	void ClearChanges();

private:
	void RefreshSegmentMetrics(FChronoRoadSegment& Segment) const;
	void RecomputeNodeTrims(int32 NodeId);

	TMap<int32, FChronoRoadNode> Nodes;
	TMap<int32, FChronoRoadSegment> Segments;

	int32 NextNodeId = 1;
	int32 NextSegmentId = 1;
	float MinTrimCm = 100.0f;
	float LevelRunCm = 0.0f;

	TSet<int32> DirtySegments;
	TSet<int32> DirtyNodes;
	TSet<int32> RemovedSegments;
	TSet<int32> RemovedNodes;
};
