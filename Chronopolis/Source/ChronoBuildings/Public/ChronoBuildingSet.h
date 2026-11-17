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
	 * @param Demand when given, a lot is only built if its zone is in demand,
	 * with a chance equal to that demand. Null builds on any zoned lot.
	 * @return the number of buildings created.
	 */
	int32 Grow(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, TConstArrayView<FChronoBuildingDefData> Definitions, const FChronoBuildingRules& Rules, FRandomStream& Random, int32 MaxNewBuildings, const FChronoZoneDemand* Demand = nullptr);

	/**
	 * Places a building chosen by the player (a service) with its lot starting
	 * at the front-row cell of AnchorCellId's slot. The zone of the cells does
	 * not matter. Returns the id of the new building, or INDEX_NONE if it does
	 * not fit there.
	 */
	int32 PlaceBuilding(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, int32 AnchorCellId, const FChronoBuildingDefData& Definition, const FChronoBuildingRules& Rules);

	/** Removes one building. Returns false if it does not exist. */
	bool DemolishBuilding(int32 BuildingId);

	/** Building whose walls contain a point, or INDEX_NONE. */
	int32 FindBuildingAt(const FVector2D& Point) const;

	/**
	 * Services that cover a point, as a bit mask: bit N is set when a building
	 * providing service N (EChronoServiceType) is within its radius.
	 */
	uint32 GetServiceMaskAt(const FVector2D& Point) const;

	/** Number of bits set in a service mask. */
	static int32 CountServices(uint32 ServiceMask);

	/** Daily upkeep of every building placed by the player. */
	double GetTotalUpkeepPerDay() const;

	/**
	 * Lets up to MaxUpgrades grown buildings gain a level, and a storey, where
	 * the land value has reached the threshold of their next level.
	 * @return the number of buildings upgraded.
	 */
	int32 UpgradeBuildings(const FChronoBuildingRules& Rules, FRandomStream& Random, int32 MaxUpgrades);

	/**
	 * Era and civilization given to the buildings created from now on: they
	 * decide their style. 0 and None until the civilization system sets them.
	 */
	void SetStyle(FName InCivilization, int32 InEra);
	int32 GetEra() const { return CurrentEra; }
	FName GetCivilization() const { return CurrentCivilization; }

	/**
	 * Rebuilds up to MaxRenewals grown buildings of an earlier era (or of
	 * another civilization) in the style of the current one, on the same lot,
	 * with one of Definitions. A building that cannot be rebuilt there is
	 * left as it was. Heritage buildings and the player's buildings are never
	 * touched. Every new building passes the same checks as one that grows.
	 * @return the number of buildings rebuilt.
	 */
	int32 RenewBuildings(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, TConstArrayView<FChronoBuildingDefData> Definitions, const FChronoBuildingRules& Rules, FRandomStream& Random, int32 MaxRenewals);

	/** Number of grown buildings that a renewal would rebuild. */
	int32 CountOutdated() const;

	/** Lists a building as heritage, or takes it off the list. False if there is no such building. */
	bool SetHeritage(int32 BuildingId, bool bHeritage);

	int32 CountHeritage() const;

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
	/**
	 * Tries to place one definition with its lot starting at a front-row cell.
	 * @param bPlayerPlaced true for a service: the zone of the cells and the land value are ignored.
	 * @return the id of the new building, or INDEX_NONE.
	 */
	/** Tries the definitions of the anchor's zone in weighted random order until one fits. Returns the new id or INDEX_NONE. */
	int32 TryGrowOnAnchor(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoZoneCell& Anchor, TConstArrayView<FChronoBuildingDefData> Definitions, const FChronoBuildingRules& Rules, FRandomStream& Random);

	int32 TryPlace(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoZoneCell& Anchor, const FChronoBuildingDefData& Definition, const FChronoBuildingRules& Rules, bool bPlayerPlaced);

	void AddBuilding(const FChronoBuilding& Building);
	void RemoveBuilding(int32 BuildingId);
	void RebuildIndices();

	bool OverlapsExistingBuilding(const FChronoOrientedBox2D& Footprint, double ToleranceCm) const;
	void QueryBuildings(const FBox2D& Bounds, TArray<int32>& OutBuildingIds) const;

	static int64 MakeCellKey(int32 SegmentId, int32 Side, int32 Slot, int32 Row);
	static FIntPoint GetBucket(const FVector2D& Point);

	TMap<int32, FChronoBuilding> Buildings;

	/** Style of the buildings created from now on. */
	FName CurrentCivilization = NAME_None;
	int32 CurrentEra = 0;

	/** Cells covered by a lot, by cell key. */
	TMap<int64, int32> OccupiedCells;

	/** Spatial hash: building ids by bucket of their footprint center. */
	TMap<FIntPoint, TArray<int32>> Buckets;

	/** Buildings that provide a service, for coverage queries. */
	TArray<int32> ServiceBuildingIds;

	TSet<int32> DirtySegments;
	int32 NextBuildingId = 1;
};
