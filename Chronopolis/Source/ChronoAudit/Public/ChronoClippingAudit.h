// Zero-clipping audit: walks the simulation data (terrain, roads) and reports
// every place where two things would interpenetrate or leave a visible gap.
// Pure data in, list of issues out: it runs in game, in automation tests and
// in continuous integration. A phase is only valid if it reports nothing.

#pragma once

#include "CoreMinimal.h"

class FChronoBuildingSet;
class FChronoRoadGraph;
class FChronoZoneGrid;
struct FChronoHeightfield;
struct FChronoBuildingRules;
struct FChronoRoadRules;
struct FChronoZoningRules;

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
	/** A zoning cell touches a road or a junction. */
	CHRONOAUDIT_API extern const FName ZoneCellOnRoad;
	/** Two zoning cells overlap. */
	CHRONOAUDIT_API extern const FName ZoneCellOverlap;
	/** A zoning cell fronts on a road that no longer exists. */
	CHRONOAUDIT_API extern const FName ZoneCellOrphan;
	/** The walls of a building touch a road or a junction. */
	CHRONOAUDIT_API extern const FName BuildingOnRoad;
	/** The walls of two buildings overlap. */
	CHRONOAUDIT_API extern const FName BuildingOverlap;
	/** The ground rises above the floor of a building. */
	CHRONOAUDIT_API extern const FName BuildingBuried;
	/** The ground is below the foundation of a building: a gap is visible under it. */
	CHRONOAUDIT_API extern const FName BuildingFloating;
	/** A building stands on cells that no longer exist or have another zone. */
	CHRONOAUDIT_API extern const FName BuildingWithoutLot;
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
	/** Zoning cells against roads and against each other. Returns the number of issues added to OutIssues. */
	CHRONOAUDIT_API int32 RunZoning(const FChronoRoadGraph& Graph, const FChronoZoneGrid& Grid, const FChronoZoningRules& ZoningRules, const FChronoAuditSettings& Settings, TArray<FChronoAuditIssue>& OutIssues);

	/** Buildings against roads, terrain, their lot and each other. Returns the number of issues added to OutIssues. */
	CHRONOAUDIT_API int32 RunBuildings(const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoZoneGrid& Grid, const FChronoBuildingSet& Buildings, const FChronoBuildingRules& BuildingRules, const FChronoAuditSettings& Settings, TArray<FChronoAuditIssue>& OutIssues);

	/** Terrain and roads. Returns the number of issues added to OutIssues. */
	CHRONOAUDIT_API int32 Run(const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoRoadRules& Rules, const FChronoAuditSettings& Settings, TArray<FChronoAuditIssue>& OutIssues);
}
