// Zero-clipping audit: walks the simulation data (terrain, roads) and reports
// every place where two things would interpenetrate or leave a visible gap.
// Pure data in, list of issues out: it runs in game, in automation tests and
// in continuous integration. A phase is only valid if it reports nothing.

#pragma once

#include "CoreMinimal.h"

class FChronoRoadGraph;
struct FChronoHeightfield;
struct FChronoRoadRules;

/** One problem found by the audit. */
struct CHRONOAUDIT_API FChronoAuditIssue
{
	/** Short stable name of the broken rule, e.g. "TerrainThroughRoad". */
	FName Rule;

	/** What is wrong, with ids and measured values. */
	FString Description;

	/** Where to look in the world. */
	FVector Location = FVector::ZeroVector;
};

/** Names of the audit rules. */
namespace ChronoAuditRule
{
	/** The ground rises above the surface of a road or junction. */
	CHRONOAUDIT_API extern const FName TerrainThroughRoad;
	/** The ground is so far below a road that the side faces no longer hide the gap. */
	CHRONOAUDIT_API extern const FName RoadFloating;
	/** Two road ribbons overlap. */
	CHRONOAUDIT_API extern const FName RoadOverlap;
	/** Two roads meet at an angle below the allowed minimum. */
	CHRONOAUDIT_API extern const FName JunctionAngle;
	/** A segment has no length left between its two junctions. */
	CHRONOAUDIT_API extern const FName SegmentSwallowed;
	/** A junction outline could not be turned into a mesh. */
	CHRONOAUDIT_API extern const FName JunctionMesh;
	/** A segment and its junction are at different heights where they meet. */
	CHRONOAUDIT_API extern const FName JunctionStep;
}

struct CHRONOAUDIT_API FChronoAuditSettings
{
	/** The ground must stay at least this far below a road surface. */
	float MinGroundGapCm = 0.5f;

	/** Height difference tolerated where a segment meets its junction. */
	float MaxJunctionStepCm = 0.1f;

	/** Overlap between two ribbons below this depth is ignored (numerical noise). */
	float OverlapToleranceCm = 1.0f;

	/** The audit stops collecting after this many issues. */
	int32 MaxIssues = 500;
};

namespace ChronoClippingAudit
{
	/** Runs every rule. Returns the number of issues added to OutIssues. */
	CHRONOAUDIT_API int32 Run(const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoRoadRules& Rules, const FChronoAuditSettings& Settings, TArray<FChronoAuditIssue>& OutIssues);
}
