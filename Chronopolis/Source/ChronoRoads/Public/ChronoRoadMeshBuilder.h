// Turns the road graph into triangles: one ribbon per segment (carriageway,
// curbs, sidewalks, side faces, end caps) and one polygon per junction.
// Segment ribbons stop where junction polygons start, and both use the same
// corner points, so the two never overlap and never leave a gap.

#pragma once

#include "CoreMinimal.h"
#include "ChronoMeshData.h"
#include "ChronoRoadGraph.h"

/** Material slots used by road meshes. */
namespace ChronoRoadMaterialSlot
{
	const int32 Carriageway = 0;
	const int32 Sidewalk = 1;
	const int32 Side = 2;
	const int32 Count = 3;
}

struct FChronoRoadMeshSettings
{
	/** Distance between two cross-sections. */
	double SampleSpacingCm = 200.0;

	/** How far the side faces go down below the road surface. */
	float SkirtDepthCm = 100.0f;
};

namespace ChronoRoadMeshBuilder
{
	/**
	 * The two corners of a segment's cross-section where it meets the junction
	 * of NodeId: OutClockwise is on the clockwise side of the direction that
	 * leaves the node, OutCounterClockwise on the other side.
	 */
	CHRONOROADS_API bool GetMouthCorners(const FChronoRoadGraph& Graph, const FChronoRoadSegment& Segment, int32 NodeId, double SampleSpacingCm, FVector2D& OutClockwise, FVector2D& OutCounterClockwise);

	/**
	 * Outline of a junction, counterclockwise. Edge k goes from point k to
	 * point k + 1; OutEdgeIsMouth[k] is true when that edge is the end of a
	 * segment (no side face needed there).
	 * @return false for a node with fewer than two segments (no junction).
	 */
	CHRONOROADS_API bool BuildNodePolygon(const FChronoRoadGraph& Graph, int32 NodeId, double SampleSpacingCm, TArray<FVector2D>& OutPolygon, TArray<bool>& OutEdgeIsMouth);

	/** Positions are relative to Origin. Leaves OutMesh empty if the segment has no usable length. */
	CHRONOROADS_API void BuildSegmentMesh(const FChronoRoadGraph& Graph, int32 SegmentId, const FChronoRoadMeshSettings& Settings, const FVector& Origin, FChronoMeshData& OutMesh);

	/** Positions are relative to Origin. Leaves OutMesh empty for a dead end. */
	CHRONOROADS_API void BuildNodeMesh(const FChronoRoadGraph& Graph, int32 NodeId, const FChronoRoadMeshSettings& Settings, const FVector& Origin, FChronoMeshData& OutMesh);
}
