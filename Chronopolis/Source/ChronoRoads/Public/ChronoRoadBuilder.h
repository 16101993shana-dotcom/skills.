// Road editing rules as plain functions on plain data: no world, no actors.
// UChronoRoadSubsystem calls them in game; automation tests and the
// zero-clipping audit call them headless.

#pragma once

#include "CoreMinimal.h"
#include "Math/IntRect.h"
#include "ChronoRoadGraph.h"
#include "ChronoRoadTypes.h"

struct FChronoHeightfield;

/** Tunables of the road rules. In game they come from UChronoSettings. */
struct CHRONOROADS_API FChronoRoadRules
{
	float MinIntersectionAngleDegrees = 30.0f;
	int32 MaxNodeDegree = 6;
	float MinUsableLengthCm = 400.0f;
	float MinTrimCm = 100.0f;
	float JunctionLevelRunCm = 600.0f;
	float SnapDistanceCm = 800.0f;
	float ClearanceCm = 600.0f;
	float SampleSpacingCm = 200.0f;
	float RoadbedOffsetCm = 20.0f;
	float SkirtDepthCm = 60.0f;
	float TerrainBlendCm = 800.0f;

	/** Rules read from the project settings. */
	static FChronoRoadRules FromSettings();

	/** Copies the rules that the graph itself needs (trim and level run). */
	void ApplyTo(FChronoRoadGraph& Graph) const;
};

namespace ChronoRoadBuilder
{
	/**
	 * Applies a build request to InOutGraph and checks every rule. On failure
	 * the graph is left modified: call it on a copy and discard the copy.
	 */
	CHRONOROADS_API EChronoRoadBuildResult ApplyAndValidate(FChronoRoadGraph& InOutGraph, const FChronoHeightfield& Heightfield, const FChronoRoadRules& Rules, const FChronoRoadBuildRequest& Request, int32& OutSegmentId);

	/** Moves the pending changes out of the graph, sorted by id. */
	CHRONOROADS_API FChronoRoadChangeSet TakeChanges(FChronoRoadGraph& InOutGraph);

	/**
	 * Shapes the ground under the changed roads and junctions.
	 * @return the modified vertex rectangle (Min inclusive, Max exclusive).
	 */
	CHRONOROADS_API FIntRect ConformTerrain(const FChronoRoadGraph& Graph, const FChronoRoadChangeSet& Changes, const FChronoRoadRules& Rules, FChronoHeightfield& InOutHeightfield);

	/**
	 * True if the centerlines of two segments come closer than their two half
	 * widths plus ExtraClearanceCm, away from the nodes they share.
	 */
	CHRONOROADS_API bool AreSegmentsTooClose(const FChronoRoadGraph& Graph, const FChronoRoadSegment& A, const FChronoRoadSegment& B, const FChronoRoadRules& Rules, double ExtraClearanceCm);
}
