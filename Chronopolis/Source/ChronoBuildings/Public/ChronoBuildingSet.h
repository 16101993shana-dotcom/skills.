// The buildings of the city as plain data, and the rules by which they
// appear on zoned cells and disappear when their lot goes away.
//
// Zero clipping: a building is only created if its walls are inside its lot,
// clear of every road and of every other building. It does not reshape the
// terrain: its floor is set above the highest ground under it and its
// foundation goes below the lowest, so it is never buried and never floats.

#pragma once

#include "CoreMinimal.h"
#include "ChronoBuildingTypes.h"

class FChronoRoadGraph;
class FChronoZoneGrid;
struct FChronoHeightfield;

class CHRONOBUILDINGS_API FChronoBuildingSet
{
public:
	const TMap<int32, FChronoBuilding>& GetBuildings() const { return Buildings; }
	const FChronoBuilding* FindBuilding(int32 BuildingId) const { return Buildings.Find(BuildingId); }
	int32 GetNumBuildings() const { return Buildings.Num(); }

	/** Ids of the buildings whose lot fronts on a segment. */
	TArray<int32> GetSegmentBuildings(int32 SegmentId) const;

	bool IsCellOccupied(int32 SegmentId, int32 Side, int32 Slot, int32 Row) const;

	/**
	 * Lets up to MaxNewBuildings appear on free zoned lots, picked with Random.
	 * @return the number of buildings created.
	 */
	int32 Grow(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, TConstArrayView<FChronoBuildingDefData> Definitions, const FChronoBuildingRules& Rules, FRandomStream& Random, int32 MaxNewBuildings);

	/**
	 * Removes the buildings of the given segments whose lot no longer exists
	 * or was given another zone. Returns the number removed.
	 */
	int32 RemoveInvalid(const FChronoZoneGrid& Grid, TConstArrayView<int32> SegmentIds);

	/**
	 * Recomputes the floor and foundation heights of the buildings that touch
	 * WorldBounds, after the ground changed there. Returns the number updated.
	 */
	int32 RefreshHeights(const FChronoHeightfield& Heightfield, const FBox2D& WorldBounds, const FChronoBuildingRules& Rules);

	/** Land value at a point, from 0 to Rules.MaxLandValue. */
	float ComputeLandValue(const FVector2D& Point, bool bFrontsOnRoad, const FChronoBuildingRules& Rules) const;

	void Reset();

	/**
	 * Binary serialization for save games (both directions). Loading replaces
	 * the set and returns false, leaving it empty, on invalid data.
	 */
	bool Serialize(FArchive& Ar);

	/** Segments whose buildings changed since the last call, sorted by id. */
	TArray<int32> TakeDirtySegments();

private:
	/** Tries to place one definition with its lot starting at a front-row cell. */
	bool TryPlace(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoZoneCell& Anchor, const FChronoBuildingDefData& Definition, const FChronoBuildingRules& Rules);

	void AddBuilding(const FChronoBuilding& Building);
	void RemoveBuilding(int32 BuildingId);
	void RebuildIndices();

	bool OverlapsExistingBuilding(const FChronoOrientedBox2D& Footprint, double ToleranceCm) const;
	void QueryBuildings(const FBox2D& Bounds, TArray<int32>& OutBuildingIds) const;

	static int64 MakeCellKey(int32 SegmentId, int32 Side, int32 Slot, int32 Row);
	static FIntPoint GetBucket(const FVector2D& Point);

	TMap<int32, FChronoBuilding> Buildings;

	/** Cells covered by a lot, by cell key. */
	TMap<int64, int32> OccupiedCells;

	/** Spatial hash: building ids by bucket of their footprint center. */
	TMap<FIntPoint, TArray<int32>> Buckets;

	TSet<int32> DirtySegments;
	int32 NextBuildingId = 1;
};
