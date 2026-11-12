// Zoning cells: square plots laid out along both sides of every road, row
// after row. The player paints a zone type on them; buildings will grow on
// groups of cells. Plain data, with no world or rendering dependency.
//
// Zero clipping is decided here, before anything is built: a cell only exists
// if it is clear of every road, of every other cell, inside the map and on
// ground that is level enough.

#pragma once

#include "CoreMinimal.h"
#include "ChronoGeometry2D.h"
#include "ChronoZoneGrid.generated.h"

class FChronoRoadGraph;
struct FChronoHeightfield;
struct FChronoRoadChangeSet;

UENUM(BlueprintType)
enum class EChronoZoneType : uint8
{
	None,
	Residential,
	Commercial,
	Industrial,
	Agricultural,
	Civic,
	Count UMETA(Hidden)
};

/** Tunables of the zoning rules. In game they come from UChronoSettings. */
struct CHRONOZONING_API FChronoZoningRules
{
	float CellSizeCm = 800.0f;
	int32 MaxDepthCells = 4;
	float RoadSetbackCm = 100.0f;
	float RoadClearanceCm = 20.0f;
	float MaxHeightRangeCm = 250.0f;
	float RoadSampleSpacingCm = 200.0f;

	/** Two cells may overlap by less than this (numerical noise on shared edges). */
	float CellOverlapToleranceCm = 1.0f;

	static FChronoZoningRules FromSettings();
};

struct FChronoZoneCell
{
	int32 Id = INDEX_NONE;

	/** Road segment the cell fronts on. */
	int32 SegmentId = INDEX_NONE;

	/** +1 or -1: side of the segment, along its counterclockwise perpendicular. */
	int32 Side = 1;

	/** Index along the segment, counted from its start node. */
	int32 Slot = 0;

	/** 0 for the row next to the road, increasing away from it. */
	int32 Row = 0;

	FVector2D Center = FVector2D::ZeroVector;

	/** Orientation of the cell: its X axis follows the road. */
	double YawRadians = 0.0;

	EChronoZoneType Zone = EChronoZoneType::None;
};

class CHRONOZONING_API FChronoZoneGrid
{
public:
	float GetCellSizeCm() const { return CellSizeCm; }
	int32 GetNumCells() const { return Cells.Num(); }
	const TMap<int32, FChronoZoneCell>& GetCells() const { return Cells; }
	const FChronoZoneCell* FindCell(int32 CellId) const { return Cells.Find(CellId); }

	/** Ids of the cells that front on a segment (empty if none). */
	TConstArrayView<int32> GetSegmentCells(int32 SegmentId) const;

	/** Footprint of a cell. */
	FChronoOrientedBox2D GetCellBox(const FChronoZoneCell& Cell) const;

	/** Cell that contains a point, or INDEX_NONE. */
	int32 FindCellAt(const FVector2D& Point) const;

	/**
	 * Updates the cells after a road edit: cells of removed or changed
	 * segments are regenerated (painted zones are kept where the cell still
	 * exists) and cells of other roads that the edit now covers are removed.
	 */
	void ApplyRoadChanges(const FChronoRoadGraph& Graph, const FChronoHeightfield& Heightfield, const FChronoRoadChangeSet& Changes, const FChronoZoningRules& Rules);

	/** Sets the zone of every cell whose center is within RadiusCm. Returns the number of cells changed. */
	int32 PaintZone(const FVector2D& Center, float RadiusCm, EChronoZoneType Zone);

	/** Removes everything. */
	void Reset();

	/**
	 * Binary serialization for save games (both directions). Loading replaces
	 * the grid and returns false, leaving it empty, on invalid data.
	 */
	bool Serialize(FArchive& Ar);

	/** Drops cells whose segment no longer exists (after loading). */
	void RemoveOrphanCells(const FChronoRoadGraph& Graph);

	// ---- Change tracking (consumed by UChronoZoningSubsystem) ----

	/** Segments whose cells changed since the last call, sorted by id. */
	TArray<int32> TakeDirtySegments();

private:
	struct FRoadCache;

	int32 AddCell(const FChronoZoneCell& Cell);
	void RemoveCell(int32 CellId);
	void RemoveSegmentCells(int32 SegmentId);

	/** Removes a cell and every cell behind it (same segment, side and slot, deeper rows). */
	void RemoveCellAndDeeperRows(int32 CellId);

	void GenerateSegmentCells(const FChronoRoadGraph& Graph, const FChronoHeightfield& Heightfield, int32 SegmentId, const FChronoZoningRules& Rules, FRoadCache& RoadCache, const TMap<int64, EChronoZoneType>* SavedZones);

	bool OverlapsExistingCell(const FChronoOrientedBox2D& Box, double ToleranceCm) const;

	/** Ids of the cells whose bucket touches Bounds. */
	void QueryCells(const FBox2D& Bounds, TArray<int32>& OutCellIds) const;

	FIntPoint GetBucket(const FVector2D& Point) const;
	void RebuildIndices();

	static int64 MakeCellKey(int32 Side, int32 Slot, int32 Row);

	TMap<int32, FChronoZoneCell> Cells;
	TMap<int32, TArray<int32>> CellsBySegment;

	/** Spatial hash: cell ids by bucket of their center. */
	TMap<FIntPoint, TArray<int32>> Buckets;

	TSet<int32> DirtySegments;

	int32 NextCellId = 1;
	float CellSizeCm = 800.0f;
};

namespace ChronoZoning
{
	/**
	 * True if no road ribbon and no junction comes closer than ClearanceCm to
	 * the box. Shared by cell generation and by the zero-clipping audit.
	 */
	CHRONOZONING_API bool IsBoxClearOfRoads(const FChronoRoadGraph& Graph, const FChronoOrientedBox2D& Box, double ClearanceCm, double RoadSampleSpacingCm);
}
