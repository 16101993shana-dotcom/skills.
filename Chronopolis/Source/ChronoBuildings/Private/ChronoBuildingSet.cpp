#include "ChronoBuildingSet.h"

#include "ChronoHeightfield.h"
#include "ChronoRoadGraph.h"
#include "ChronoSettings.h"
#include "ChronoZoneGrid.h"

namespace ChronoBuildingSetPrivate
{
	// Side of a spatial hash bucket. Larger than any footprint, so a
	// building can only overlap buildings filed in neighboring buckets.
	const double BucketSizeCm = 6400.0;

	// Walls shorter than this make no usable building.
	const double MinWallLengthCm = 200.0;
}

FChronoBuildingRules FChronoBuildingRules::FromSettings()
{
	const UChronoBuildingSettings* Settings = UChronoBuildingSettings::Get();

	FChronoBuildingRules Rules;
	Rules.RoadClearanceCm = Settings->RoadClearanceCm;
	Rules.FloorLiftCm = Settings->FloorLiftCm;
	Rules.FoundationDepthCm = Settings->FoundationDepthCm;
	Rules.MaxLotYawDeltaDegrees = Settings->MaxLotYawDeltaDegrees;
	Rules.RoadSampleSpacingCm = UChronoSettings::Get()->RoadSampleSpacingCm;
	Rules.BaseLandValue = Settings->BaseLandValue;
	Rules.FrontRowLandValue = Settings->FrontRowLandValue;
	Rules.NeighborLandValue = Settings->NeighborLandValue;
	Rules.NeighborLandValueCap = Settings->NeighborLandValueCap;
	Rules.IndustryLandValuePenalty = Settings->IndustryLandValuePenalty;
	Rules.NeighborRadiusCm = Settings->NeighborRadiusCm;
	Rules.MaxLandValue = Settings->MaxLandValue;
	return Rules;
}

int64 FChronoBuildingSet::MakeCellKey(int32 SegmentId, int32 Side, int32 Slot, int32 Row)
{
	// Segment id in the high half; side, row and slot in the low half.
	const uint64 Low = (static_cast<uint64>(Side > 0 ? 1 : 0) << 31) | (static_cast<uint64>(Row & 0x7F) << 24) | static_cast<uint64>(Slot & 0xFFFFFF);
	return static_cast<int64>((static_cast<uint64>(static_cast<uint32>(SegmentId)) << 32) | Low);
}

FIntPoint FChronoBuildingSet::GetBucket(const FVector2D& Point)
{
	return FIntPoint(
		FMath::FloorToInt32(Point.X / ChronoBuildingSetPrivate::BucketSizeCm),
		FMath::FloorToInt32(Point.Y / ChronoBuildingSetPrivate::BucketSizeCm));
}

TArray<int32> FChronoBuildingSet::GetSegmentBuildings(int32 SegmentId) const
{
	TArray<int32> Result;
	for (const TPair<int32, FChronoBuilding>& Pair : Buildings)
	{
		if (Pair.Value.SegmentId == SegmentId)
		{
			Result.Add(Pair.Key);
		}
	}
	Result.Sort();
	return Result;
}

bool FChronoBuildingSet::IsCellOccupied(int32 SegmentId, int32 Side, int32 Slot, int32 Row) const
{
	return OccupiedCells.Contains(MakeCellKey(SegmentId, Side, Slot, Row));
}

void FChronoBuildingSet::QueryBuildings(const FBox2D& Bounds, TArray<int32>& OutBuildingIds) const
{
	OutBuildingIds.Reset();

	// A building is filed under the bucket of its center: grow the query by one bucket.
	const FBox2D Grown = Bounds.ExpandBy(ChronoBuildingSetPrivate::BucketSizeCm);
	const FIntPoint MinBucket = GetBucket(Grown.Min);
	const FIntPoint MaxBucket = GetBucket(Grown.Max);

	for (int32 Y = MinBucket.Y; Y <= MaxBucket.Y; ++Y)
	{
		for (int32 X = MinBucket.X; X <= MaxBucket.X; ++X)
		{
			if (const TArray<int32>* Bucket = Buckets.Find(FIntPoint(X, Y)))
			{
				OutBuildingIds.Append(*Bucket);
			}
		}
	}
}

bool FChronoBuildingSet::OverlapsExistingBuilding(const FChronoOrientedBox2D& Footprint, double ToleranceCm) const
{
	TArray<int32> Candidates;
	QueryBuildings(Footprint.GetBounds(), Candidates);

	for (const int32 BuildingId : Candidates)
	{
		if (FChronoOrientedBox2D::Overlap(Footprint, Buildings.FindChecked(BuildingId).Footprint, ToleranceCm))
		{
			return true;
		}
	}
	return false;
}

void FChronoBuildingSet::AddBuilding(const FChronoBuilding& Building)
{
	Buildings.Add(Building.Id, Building);
	Buckets.FindOrAdd(GetBucket(Building.Footprint.Center)).Add(Building.Id);

	for (int32 Slot = Building.FirstSlot; Slot < Building.FirstSlot + Building.WidthCells; ++Slot)
	{
		for (int32 Row = 0; Row < Building.DepthCells; ++Row)
		{
			OccupiedCells.Add(MakeCellKey(Building.SegmentId, Building.Side, Slot, Row), Building.Id);
		}
	}

	DirtySegments.Add(Building.SegmentId);
}

void FChronoBuildingSet::RemoveBuilding(int32 BuildingId)
{
	FChronoBuilding Building;
	if (!Buildings.RemoveAndCopyValue(BuildingId, Building))
	{
		return;
	}

	const FIntPoint BucketKey = GetBucket(Building.Footprint.Center);
	if (TArray<int32>* Bucket = Buckets.Find(BucketKey))
	{
		Bucket->Remove(BuildingId);
		if (Bucket->Num() == 0)
		{
			Buckets.Remove(BucketKey);
		}
	}

	for (int32 Slot = Building.FirstSlot; Slot < Building.FirstSlot + Building.WidthCells; ++Slot)
	{
		for (int32 Row = 0; Row < Building.DepthCells; ++Row)
		{
			OccupiedCells.Remove(MakeCellKey(Building.SegmentId, Building.Side, Slot, Row));
		}
	}

	DirtySegments.Add(Building.SegmentId);
}

void FChronoBuildingSet::Reset()
{
	for (const TPair<int32, FChronoBuilding>& Pair : Buildings)
	{
		DirtySegments.Add(Pair.Value.SegmentId);
	}

	Buildings.Reset();
	OccupiedCells.Reset();
	Buckets.Reset();
}

TArray<int32> FChronoBuildingSet::TakeDirtySegments()
{
	TArray<int32> Result = DirtySegments.Array();
	Result.Sort();
	DirtySegments.Reset();
	return Result;
}

float FChronoBuildingSet::ComputeLandValue(const FVector2D& Point, bool bFrontsOnRoad, const FChronoBuildingRules& Rules) const
{
	float Value = Rules.BaseLandValue;
	if (bFrontsOnRoad)
	{
		Value += Rules.FrontRowLandValue;
	}

	// Neighbors make a place more valuable, industry less.
	const double Radius = Rules.NeighborRadiusCm;
	TArray<int32> Candidates;
	QueryBuildings(FBox2D(Point - FVector2D(Radius, Radius), Point + FVector2D(Radius, Radius)), Candidates);

	int32 NumNeighbors = 0;
	int32 NumIndustry = 0;
	for (const int32 BuildingId : Candidates)
	{
		const FChronoBuilding& Building = Buildings.FindChecked(BuildingId);
		if (FVector2D::DistSquared(Building.Footprint.Center, Point) <= Radius * Radius)
		{
			++NumNeighbors;
			if (Building.Zone == EChronoZoneType::Industrial)
			{
				++NumIndustry;
			}
		}
	}

	Value += FMath::Min(NumNeighbors * Rules.NeighborLandValue, Rules.NeighborLandValueCap);
	Value -= NumIndustry * Rules.IndustryLandValuePenalty;
	return FMath::Clamp(Value, 0.0f, Rules.MaxLandValue);
}

int32 FChronoBuildingSet::Grow(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, TConstArrayView<FChronoBuildingDefData> Definitions, const FChronoBuildingRules& Rules, FRandomStream& Random, int32 MaxNewBuildings)
{
	if (MaxNewBuildings <= 0 || Definitions.Num() == 0)
	{
		return 0;
	}

	// Every free, zoned cell of a front row can start a lot. Collected in id
	// order so that the same random stream always gives the same city.
	TArray<int32> Anchors;
	for (const TPair<int32, FChronoZoneCell>& Pair : Grid.GetCells())
	{
		const FChronoZoneCell& Cell = Pair.Value;
		if (Cell.Row == 0 && Cell.Zone != EChronoZoneType::None && !IsCellOccupied(Cell.SegmentId, Cell.Side, Cell.Slot, 0))
		{
			Anchors.Add(Pair.Key);
		}
	}
	Anchors.Sort();

	int32 NumCreated = 0;
	TArray<int32> Fitting;

	// A few attempts per wanted building: some anchors have no definition that fits.
	const int32 MaxAttempts = MaxNewBuildings * 8;
	for (int32 Attempt = 0; Attempt < MaxAttempts && NumCreated < MaxNewBuildings && Anchors.Num() > 0; ++Attempt)
	{
		const int32 AnchorIndex = Random.RandRange(0, Anchors.Num() - 1);
		const FChronoZoneCell* Anchor = Grid.FindCell(Anchors[AnchorIndex]);
		Anchors.RemoveAtSwap(AnchorIndex);
		if (!Anchor || IsCellOccupied(Anchor->SegmentId, Anchor->Side, Anchor->Slot, 0))
		{
			continue;
		}

		// Definitions of the right zone, tried in weighted random order.
		Fitting.Reset();
		float TotalWeight = 0.0f;
		for (int32 Index = 0; Index < Definitions.Num(); ++Index)
		{
			if (Definitions[Index].Zone == Anchor->Zone && Definitions[Index].Weight > 0.0f)
			{
				Fitting.Add(Index);
				TotalWeight += Definitions[Index].Weight;
			}
		}

		while (Fitting.Num() > 0)
		{
			float Pick = Random.FRandRange(0.0f, TotalWeight);
			int32 Chosen = Fitting.Num() - 1;
			for (int32 Index = 0; Index < Fitting.Num(); ++Index)
			{
				Pick -= Definitions[Fitting[Index]].Weight;
				if (Pick <= 0.0f)
				{
					Chosen = Index;
					break;
				}
			}

			const FChronoBuildingDefData& Definition = Definitions[Fitting[Chosen]];
			if (TryPlace(Grid, Heightfield, Graph, *Anchor, Definition, Rules))
			{
				++NumCreated;
				break;
			}

			TotalWeight -= Definition.Weight;
			Fitting.RemoveAt(Chosen);
		}
	}

	return NumCreated;
}

bool FChronoBuildingSet::TryPlace(const FChronoZoneGrid& Grid, const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoZoneCell& Anchor, const FChronoBuildingDefData& Definition, const FChronoBuildingRules& Rules)
{
	using namespace ChronoBuildingSetPrivate;

	const int32 Width = FMath::Max(Definition.WidthCells, 1);
	const int32 Depth = FMath::Max(Definition.DepthCells, 1);
	const double CellSize = Grid.GetCellSizeCm();

	// 1. The lot: every cell exists, has the zone of the anchor, is free and
	// is aligned with the anchor (a lot wider than one cell needs a straight road).
	const double MaxYawDelta = FMath::DegreesToRadians(static_cast<double>(Rules.MaxLotYawDeltaDegrees));
	FVector2D CenterSum = FVector2D::ZeroVector;
	TArray<FChronoOrientedBox2D, TInlineAllocator<16>> LotCells;

	for (int32 Slot = Anchor.Slot; Slot < Anchor.Slot + Width; ++Slot)
	{
		for (int32 Row = 0; Row < Depth; ++Row)
		{
			const FChronoZoneCell* Cell = Grid.FindCellByKey(Anchor.SegmentId, Anchor.Side, Slot, Row);
			if (!Cell || Cell->Zone != Anchor.Zone || IsCellOccupied(Anchor.SegmentId, Anchor.Side, Slot, Row))
			{
				return false;
			}
			if (FMath::Abs(FMath::FindDeltaAngleRadians(Anchor.YawRadians, Cell->YawRadians)) > MaxYawDelta)
			{
				return false;
			}

			CenterSum += Cell->Center;
			LotCells.Add(Grid.GetCellBox(*Cell));
		}
	}

	const FVector2D LotCenter = CenterSum / static_cast<double>(Width * Depth);
	const FVector2D LotHalf(0.5 * Width * CellSize, 0.5 * Depth * CellSize);

	// 2. Land value decides whether the building may appear, and how tall.
	const float LandValue = ComputeLandValue(LotCenter, true, Rules);
	if (LandValue < Definition.MinLandValue)
	{
		return false;
	}

	// 3. The walls: the lot minus its margins. The front of the lot is the
	// edge next to the road, on the opposite side of the lot from Anchor.Side.
	const double HalfX = LotHalf.X - Definition.SideMarginCm;
	const double HalfY = LotHalf.Y - 0.5 * (Definition.FrontSetbackCm + Definition.RearMarginCm);
	if (2.0 * HalfX < MinWallLengthCm || 2.0 * HalfY < MinWallLengthCm)
	{
		return false;
	}

	const FVector2D AxisY(-FMath::Sin(Anchor.YawRadians), FMath::Cos(Anchor.YawRadians));
	const double ShiftAwayFromRoad = Anchor.Side * 0.5 * (Definition.FrontSetbackCm - Definition.RearMarginCm);
	const FChronoOrientedBox2D Footprint(LotCenter + AxisY * ShiftAwayFromRoad, FVector2D(HalfX, HalfY), Anchor.YawRadians);

	// 4. Zero clipping: every corner of the walls is inside the lot, and the
	// walls are clear of every road and of every other building.
	FVector2D Corners[4];
	Footprint.GetCorners(Corners);
	for (const FVector2D& Corner : Corners)
	{
		bool bInsideLot = false;
		for (const FChronoOrientedBox2D& LotCell : LotCells)
		{
			bInsideLot = bInsideLot || LotCell.Contains(Corner);
		}
		if (!bInsideLot)
		{
			return false;
		}
	}

	if (!ChronoZoning::IsBoxClearOfRoads(Graph, Footprint, Rules.RoadClearanceCm, Rules.RoadSampleSpacingCm)
		|| OverlapsExistingBuilding(Footprint, Rules.OverlapToleranceCm))
	{
		return false;
	}

	// 5. Heights: floor above the highest ground under the walls, foundation
	// below the lowest. The range is taken over a slightly larger area than
	// the walls, which only makes it safer.
	float MinGround = 0.0f;
	float MaxGround = 0.0f;
	Heightfield.GetHeightRangeInOrientedBox(Footprint.Center, Footprint.HalfExtent, static_cast<float>(Footprint.YawRadians), MinGround, MaxGround);

	const int32 MinFloors = FMath::Max(Definition.MinFloors, 1);
	const int32 MaxFloors = FMath::Max(Definition.MaxFloors, MinFloors);
	const float ValueRatio = FMath::Clamp(LandValue / FMath::Max(Rules.MaxLandValue, 1.0f), 0.0f, 1.0f);
	const int32 Floors = MinFloors + FMath::RoundToInt32((MaxFloors - MinFloors) * ValueRatio);

	FChronoBuilding Building;
	Building.Id = NextBuildingId++;
	Building.DefId = Definition.DefId;
	Building.Zone = Anchor.Zone;
	Building.SegmentId = Anchor.SegmentId;
	Building.Side = Anchor.Side;
	Building.FirstSlot = Anchor.Slot;
	Building.WidthCells = Width;
	Building.DepthCells = Depth;
	Building.Footprint = Footprint;
	Building.FloorHeightCm = static_cast<double>(MaxGround) + Rules.FloorLiftCm;
	Building.FoundationBottomCm = static_cast<double>(MinGround) - Rules.FoundationDepthCm;
	Building.BodyHeightCm = Floors * Definition.FloorHeightCm;
	Building.Roof = Definition.Roof;
	Building.RoofHeightCm = Definition.RoofHeightCm;
	Building.LandValue = LandValue;

	AddBuilding(Building);
	return true;
}

int32 FChronoBuildingSet::RemoveInvalid(const FChronoZoneGrid& Grid, TConstArrayView<int32> SegmentIds)
{
	int32 NumRemoved = 0;

	for (const int32 SegmentId : SegmentIds)
	{
		for (const int32 BuildingId : GetSegmentBuildings(SegmentId))
		{
			const FChronoBuilding& Building = Buildings.FindChecked(BuildingId);

			bool bLotIntact = true;
			for (int32 Slot = Building.FirstSlot; Slot < Building.FirstSlot + Building.WidthCells && bLotIntact; ++Slot)
			{
				for (int32 Row = 0; Row < Building.DepthCells && bLotIntact; ++Row)
				{
					const FChronoZoneCell* Cell = Grid.FindCellByKey(Building.SegmentId, Building.Side, Slot, Row);

					// The cell must still be where the building stands: after
					// a road is split, the same key can name a cell elsewhere.
					bLotIntact = Cell
						&& Cell->Zone == Building.Zone
						&& Building.Footprint.DistanceToPoint(Cell->Center) < Grid.GetCellSizeCm();
				}
			}

			if (!bLotIntact)
			{
				RemoveBuilding(BuildingId);
				++NumRemoved;
			}
		}
	}

	return NumRemoved;
}

int32 FChronoBuildingSet::RefreshHeights(const FChronoHeightfield& Heightfield, const FBox2D& WorldBounds, const FChronoBuildingRules& Rules)
{
	TArray<int32> Candidates;
	QueryBuildings(WorldBounds, Candidates);

	int32 NumUpdated = 0;
	for (const int32 BuildingId : Candidates)
	{
		FChronoBuilding& Building = Buildings.FindChecked(BuildingId);
		if (!WorldBounds.Intersect(Building.Footprint.GetBounds()))
		{
			continue;
		}

		float MinGround = 0.0f;
		float MaxGround = 0.0f;
		Heightfield.GetHeightRangeInOrientedBox(Building.Footprint.Center, Building.Footprint.HalfExtent, static_cast<float>(Building.Footprint.YawRadians), MinGround, MaxGround);

		const double NewFloor = static_cast<double>(MaxGround) + Rules.FloorLiftCm;
		const double NewFoundation = static_cast<double>(MinGround) - Rules.FoundationDepthCm;
		if (!FMath::IsNearlyEqual(NewFloor, Building.FloorHeightCm, 0.01) || !FMath::IsNearlyEqual(NewFoundation, Building.FoundationBottomCm, 0.01))
		{
			Building.FloorHeightCm = NewFloor;
			Building.FoundationBottomCm = NewFoundation;
			DirtySegments.Add(Building.SegmentId);
			++NumUpdated;
		}
	}
	return NumUpdated;
}

void FChronoBuildingSet::RebuildIndices()
{
	OccupiedCells.Reset();
	Buckets.Reset();

	TArray<int32> BuildingIds;
	Buildings.GetKeys(BuildingIds);
	BuildingIds.Sort();
	for (const int32 BuildingId : BuildingIds)
	{
		const FChronoBuilding& Building = Buildings.FindChecked(BuildingId);
		Buckets.FindOrAdd(GetBucket(Building.Footprint.Center)).Add(BuildingId);
		for (int32 Slot = Building.FirstSlot; Slot < Building.FirstSlot + Building.WidthCells; ++Slot)
		{
			for (int32 Row = 0; Row < Building.DepthCells; ++Row)
			{
				OccupiedCells.Add(MakeCellKey(Building.SegmentId, Building.Side, Slot, Row), BuildingId);
			}
		}
	}
}

bool FChronoBuildingSet::Serialize(FArchive& Ar)
{
	// Bump when the binary layout changes.
	const int32 CurrentVersion = 1;
	// Sanity bounds on values read from a file.
	const int32 MaxBuildings = 4 * 1024 * 1024;
	const int32 MaxLotCells = 64;

	int32 Version = CurrentVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != CurrentVersion)
	{
		Ar.SetError();
		return false;
	}

	if (Ar.IsLoading())
	{
		Reset();
	}

	Ar << NextBuildingId;

	// Saved in id order so that the same city always gives the same bytes.
	TArray<int32> BuildingIds;
	Buildings.GetKeys(BuildingIds);
	BuildingIds.Sort();
	int32 NumBuildings = BuildingIds.Num();
	Ar << NumBuildings;

	bool bValid = !Ar.IsError() && NumBuildings >= 0 && NumBuildings <= MaxBuildings;

	for (int32 Index = 0; bValid && Index < NumBuildings; ++Index)
	{
		FChronoBuilding Building;
		FString DefId;
		uint8 ZoneByte = 0;
		uint8 RoofByte = 0;
		if (Ar.IsSaving())
		{
			Building = Buildings.FindChecked(BuildingIds[Index]);
			DefId = Building.DefId.ToString();
			ZoneByte = static_cast<uint8>(Building.Zone);
			RoofByte = static_cast<uint8>(Building.Roof);
		}

		Ar << Building.Id;
		Ar << DefId;
		Ar << ZoneByte;
		Ar << Building.SegmentId;
		Ar << Building.Side;
		Ar << Building.FirstSlot;
		Ar << Building.WidthCells;
		Ar << Building.DepthCells;
		Ar << Building.Footprint.Center;
		Ar << Building.Footprint.HalfExtent;
		Ar << Building.Footprint.YawRadians;
		Ar << Building.FloorHeightCm;
		Ar << Building.FoundationBottomCm;
		Ar << Building.BodyHeightCm;
		Ar << RoofByte;
		Ar << Building.RoofHeightCm;
		Ar << Building.LandValue;

		if (Ar.IsLoading())
		{
			bValid = !Ar.IsError()
				&& Building.Id > 0
				&& !Buildings.Contains(Building.Id)
				&& ZoneByte > 0 && ZoneByte < static_cast<uint8>(EChronoZoneType::Count)
				&& RoofByte <= static_cast<uint8>(EChronoRoofType::Gable)
				&& (Building.Side == 1 || Building.Side == -1)
				&& Building.FirstSlot >= 0
				&& Building.WidthCells >= 1 && Building.DepthCells >= 1
				&& Building.WidthCells * Building.DepthCells <= MaxLotCells
				&& Building.Footprint.HalfExtent.X > 0.0 && Building.Footprint.HalfExtent.Y > 0.0
				&& Building.BodyHeightCm > 0.0f
				&& Building.FoundationBottomCm <= Building.FloorHeightCm;
			if (bValid)
			{
				Building.DefId = FName(*DefId);
				Building.Zone = static_cast<EChronoZoneType>(ZoneByte);
				Building.Roof = static_cast<EChronoRoofType>(RoofByte);
				Buildings.Add(Building.Id, Building);
			}
		}
	}

	if (Ar.IsLoading())
	{
		if (!bValid)
		{
			Buildings.Reset();
			OccupiedCells.Reset();
			Buckets.Reset();
			NextBuildingId = 1;
			Ar.SetError();
			return false;
		}

		RebuildIndices();
		for (const TPair<int32, FChronoBuilding>& Pair : Buildings)
		{
			DirtySegments.Add(Pair.Value.SegmentId);
		}
	}

	return !Ar.IsError();
}
