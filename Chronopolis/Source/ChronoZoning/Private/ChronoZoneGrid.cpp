#include "ChronoZoneGrid.h"

#include "ChronoHeightfield.h"
#include "ChronoRoadGeometry.h"
#include "ChronoRoadGraph.h"
#include "ChronoRoadTypes.h"
#include "ChronoSettings.h"

/** Road centerlines sampled once per grid update instead of once per cell. */
struct FChronoZoneGrid::FRoadCache
{
	FRoadCache(const FChronoRoadGraph& InGraph, double InSpacingCm)
		: Graph(InGraph), SpacingCm(InSpacingCm)
	{
	}

	const TArray<FChronoRoadSample>& GetPolyline(const FChronoRoadSegment& Segment)
	{
		if (const TArray<FChronoRoadSample>* Found = Polylines.Find(Segment.Id))
		{
			return *Found;
		}

		TArray<FChronoRoadSample>& Samples = Polylines.Add(Segment.Id);
		ChronoRoadGeometry::BuildPolyline(Segment.Curve, SpacingCm, Samples);
		return Samples;
	}

	const FChronoRoadGraph& Graph;
	double SpacingCm;
	TMap<int32, TArray<FChronoRoadSample>> Polylines;
};

namespace ChronoZoneGridPrivate
{
	/** Distance from a box to the centerline of a road, node to node. */
	double DistanceBoxToPolyline(const FChronoOrientedBox2D& Box, const TArray<FChronoRoadSample>& Samples)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Samples.Num() && Best > 0.0; ++Index)
		{
			Best = FMath::Min(Best, Box.DistanceToSegment(Samples[Index - 1].Position, Samples[Index].Position));
		}
		return Best;
	}

	/** True if the junction of a node comes closer than ClearanceCm to the box. */
	bool IsBoxNearJunction(const FChronoRoadGraph& Graph, int32 NodeId, const FChronoOrientedBox2D& Box, double ClearanceCm)
	{
		const FChronoRoadNode* Node = Graph.FindNode(NodeId);
		if (!Node || Node->SegmentIds.Num() < 2)
		{
			return false;
		}

		// The junction is approximated by the disc that contains it: a little
		// larger than the real outline, which only makes the test stricter.
		const double Radius = Graph.GetNodeRadiusCm(NodeId);
		return Box.DistanceToPoint(FVector2D(Node->Position.X, Node->Position.Y)) < Radius + ClearanceCm;
	}

	template <typename PolylineGetter>
	bool IsBoxClearOfRoadsImpl(const FChronoRoadGraph& Graph, const FChronoOrientedBox2D& Box, double ClearanceCm, PolylineGetter&& GetPolyline)
	{
		const FBox2D BoxBounds = Box.GetBounds().ExpandBy(ClearanceCm);

		for (const TPair<int32, FChronoRoadSegment>& Pair : Graph.GetSegments())
		{
			const FChronoRoadSegment& Segment = Pair.Value;

			// Segment bounds already include the half width of the road.
			if (!Segment.Bounds.Intersect(BoxBounds))
			{
				continue;
			}

			if (DistanceBoxToPolyline(Box, GetPolyline(Segment)) < Segment.Profile.GetHalfWidthCm() + ClearanceCm)
			{
				return false;
			}

			if (IsBoxNearJunction(Graph, Segment.StartNodeId, Box, ClearanceCm) || IsBoxNearJunction(Graph, Segment.EndNodeId, Box, ClearanceCm))
			{
				return false;
			}
		}

		return true;
	}
}

FChronoZoningRules FChronoZoningRules::FromSettings()
{
	const UChronoSettings* Settings = UChronoSettings::Get();

	FChronoZoningRules Rules;
	Rules.CellSizeCm = Settings->ZoneCellSizeCm;
	Rules.MaxDepthCells = Settings->ZoneMaxDepthCells;
	Rules.RoadSetbackCm = Settings->ZoneRoadSetbackCm;
	Rules.RoadClearanceCm = FMath::Min(Settings->ZoneRoadClearanceCm, Settings->ZoneRoadSetbackCm);
	Rules.MaxHeightRangeCm = Settings->ZoneMaxHeightRangeCm;
	Rules.RoadSampleSpacingCm = Settings->RoadSampleSpacingCm;
	return Rules;
}

bool ChronoZoning::IsBoxClearOfRoads(const FChronoRoadGraph& Graph, const FChronoOrientedBox2D& Box, double ClearanceCm, double RoadSampleSpacingCm)
{
	TArray<FChronoRoadSample> Samples;
	return ChronoZoneGridPrivate::IsBoxClearOfRoadsImpl(Graph, Box, ClearanceCm, [&Samples, RoadSampleSpacingCm](const FChronoRoadSegment& Segment) -> const TArray<FChronoRoadSample>&
	{
		ChronoRoadGeometry::BuildPolyline(Segment.Curve, RoadSampleSpacingCm, Samples);
		return Samples;
	});
}

TConstArrayView<int32> FChronoZoneGrid::GetSegmentCells(int32 SegmentId) const
{
	const TArray<int32>* Found = CellsBySegment.Find(SegmentId);
	return Found ? TConstArrayView<int32>(*Found) : TConstArrayView<int32>();
}

const FChronoZoneCell* FChronoZoneGrid::FindCellByKey(int32 SegmentId, int32 Side, int32 Slot, int32 Row) const
{
	for (const int32 CellId : GetSegmentCells(SegmentId))
	{
		const FChronoZoneCell& Cell = Cells.FindChecked(CellId);
		if (Cell.Side == Side && Cell.Slot == Slot && Cell.Row == Row)
		{
			return &Cell;
		}
	}
	return nullptr;
}

FChronoOrientedBox2D FChronoZoneGrid::GetCellBox(const FChronoZoneCell& Cell) const
{
	const double Half = 0.5 * CellSizeCm;
	return FChronoOrientedBox2D(Cell.Center, FVector2D(Half, Half), Cell.YawRadians);
}

FIntPoint FChronoZoneGrid::GetBucket(const FVector2D& Point) const
{
	const double BucketSize = 2.0 * FMath::Max(CellSizeCm, 1.0f);
	return FIntPoint(FMath::FloorToInt32(Point.X / BucketSize), FMath::FloorToInt32(Point.Y / BucketSize));
}

void FChronoZoneGrid::QueryCells(const FBox2D& Bounds, TArray<int32>& OutCellIds) const
{
	OutCellIds.Reset();

	// A cell is filed under the bucket of its center, so grow the query by
	// the largest distance from a center to a corner.
	const FBox2D Grown = Bounds.ExpandBy(CellSizeCm);
	const FIntPoint MinBucket = GetBucket(Grown.Min);
	const FIntPoint MaxBucket = GetBucket(Grown.Max);

	for (int32 Y = MinBucket.Y; Y <= MaxBucket.Y; ++Y)
	{
		for (int32 X = MinBucket.X; X <= MaxBucket.X; ++X)
		{
			if (const TArray<int32>* Bucket = Buckets.Find(FIntPoint(X, Y)))
			{
				OutCellIds.Append(*Bucket);
			}
		}
	}
}

int32 FChronoZoneGrid::FindCellAt(const FVector2D& Point) const
{
	TArray<int32> Candidates;
	QueryCells(FBox2D(Point, Point), Candidates);

	// The lowest id wins so the answer does not depend on bucket order.
	int32 BestId = INDEX_NONE;
	for (const int32 CellId : Candidates)
	{
		const FChronoZoneCell& Cell = Cells.FindChecked(CellId);
		if (GetCellBox(Cell).Contains(Point) && (BestId == INDEX_NONE || CellId < BestId))
		{
			BestId = CellId;
		}
	}
	return BestId;
}

bool FChronoZoneGrid::OverlapsExistingCell(const FChronoOrientedBox2D& Box, double ToleranceCm) const
{
	TArray<int32> Candidates;
	QueryCells(Box.GetBounds(), Candidates);

	for (const int32 CellId : Candidates)
	{
		if (FChronoOrientedBox2D::Overlap(Box, GetCellBox(Cells.FindChecked(CellId)), ToleranceCm))
		{
			return true;
		}
	}
	return false;
}

int64 FChronoZoneGrid::MakeCellKey(int32 Side, int32 Slot, int32 Row)
{
	// Side in the top bit, then the row, then the slot.
	const int64 SideBit = (Side > 0) ? 1 : 0;
	return (SideBit << 62) | (static_cast<int64>(Row & 0xFFFF) << 32) | static_cast<int64>(static_cast<uint32>(Slot));
}

int32 FChronoZoneGrid::AddCell(const FChronoZoneCell& Cell)
{
	FChronoZoneCell NewCell = Cell;
	NewCell.Id = NextCellId++;

	Cells.Add(NewCell.Id, NewCell);
	CellsBySegment.FindOrAdd(NewCell.SegmentId).Add(NewCell.Id);
	Buckets.FindOrAdd(GetBucket(NewCell.Center)).Add(NewCell.Id);
	DirtySegments.Add(NewCell.SegmentId);
	return NewCell.Id;
}

void FChronoZoneGrid::RemoveCell(int32 CellId)
{
	FChronoZoneCell Cell;
	if (!Cells.RemoveAndCopyValue(CellId, Cell))
	{
		return;
	}

	if (TArray<int32>* SegmentCells = CellsBySegment.Find(Cell.SegmentId))
	{
		SegmentCells->Remove(CellId);
		if (SegmentCells->Num() == 0)
		{
			CellsBySegment.Remove(Cell.SegmentId);
		}
	}

	const FIntPoint BucketKey = GetBucket(Cell.Center);
	if (TArray<int32>* Bucket = Buckets.Find(BucketKey))
	{
		Bucket->Remove(CellId);
		if (Bucket->Num() == 0)
		{
			Buckets.Remove(BucketKey);
		}
	}

	DirtySegments.Add(Cell.SegmentId);
}

void FChronoZoneGrid::RemoveSegmentCells(int32 SegmentId)
{
	// Copy: RemoveCell edits the per-segment list.
	const TArray<int32> CellIds = CellsBySegment.FindRef(SegmentId);
	for (const int32 CellId : CellIds)
	{
		RemoveCell(CellId);
	}
	DirtySegments.Add(SegmentId);
}

void FChronoZoneGrid::RemoveCellAndDeeperRows(int32 CellId)
{
	const FChronoZoneCell* Cell = Cells.Find(CellId);
	if (!Cell)
	{
		return;
	}

	const int32 SegmentId = Cell->SegmentId;
	const int32 Side = Cell->Side;
	const int32 Slot = Cell->Slot;
	const int32 Row = Cell->Row;

	// A plot must stay connected to its road: nothing is kept behind a removed cell.
	const TArray<int32> CellIds = CellsBySegment.FindRef(SegmentId);
	for (const int32 OtherId : CellIds)
	{
		const FChronoZoneCell& Other = Cells.FindChecked(OtherId);
		if (Other.Side == Side && Other.Slot == Slot && Other.Row >= Row)
		{
			RemoveCell(OtherId);
		}
	}
}

void FChronoZoneGrid::Reset()
{
	for (const TPair<int32, TArray<int32>>& Pair : CellsBySegment)
	{
		DirtySegments.Add(Pair.Key);
	}

	Cells.Reset();
	CellsBySegment.Reset();
	Buckets.Reset();
}

TArray<int32> FChronoZoneGrid::TakeDirtySegments()
{
	TArray<int32> Result = DirtySegments.Array();
	Result.Sort();
	DirtySegments.Reset();
	return Result;
}

void FChronoZoneGrid::ApplyRoadChanges(const FChronoRoadGraph& Graph, const FChronoHeightfield& Heightfield, const FChronoRoadChangeSet& Changes, const FChronoZoningRules& Rules)
{
	using namespace ChronoZoneGridPrivate;

	// The cell size is fixed for the life of a grid: the spatial index depends on it.
	if (Cells.Num() == 0)
	{
		CellSizeCm = FMath::Max(Rules.CellSizeCm, 100.0f);
	}

	// 1. Forget the cells of every segment that changed or disappeared, but
	// remember what the player painted on them.
	TMap<int32, TMap<int64, EChronoZoneType>> SavedZones;
	auto ForgetSegment = [this, &SavedZones](int32 SegmentId)
	{
		TMap<int64, EChronoZoneType>& Saved = SavedZones.FindOrAdd(SegmentId);
		for (const int32 CellId : GetSegmentCells(SegmentId))
		{
			const FChronoZoneCell& Cell = Cells.FindChecked(CellId);
			if (Cell.Zone != EChronoZoneType::None)
			{
				Saved.Add(MakeCellKey(Cell.Side, Cell.Slot, Cell.Row), Cell.Zone);
			}
		}
		RemoveSegmentCells(SegmentId);
	};
	for (const int32 SegmentId : Changes.RemovedSegments)
	{
		ForgetSegment(SegmentId);
	}
	for (const int32 SegmentId : Changes.ChangedSegments)
	{
		ForgetSegment(SegmentId);
	}

	FRoadCache RoadCache(Graph, Rules.RoadSampleSpacingCm);
	TArray<int32> Candidates;

	// 2. Cells of other roads that a changed road or junction now covers.
	for (const int32 SegmentId : Changes.ChangedSegments)
	{
		const FChronoRoadSegment* Segment = Graph.FindSegment(SegmentId);
		if (!Segment)
		{
			continue;
		}

		const double Reach = Segment->Profile.GetHalfWidthCm() + Rules.RoadClearanceCm;
		const TArray<FChronoRoadSample>& Polyline = RoadCache.GetPolyline(*Segment);

		QueryCells(Segment->Bounds.ExpandBy(Rules.RoadClearanceCm), Candidates);
		for (const int32 CellId : Candidates)
		{
			const FChronoZoneCell* Cell = Cells.Find(CellId);
			if (Cell && DistanceBoxToPolyline(GetCellBox(*Cell), Polyline) < Reach)
			{
				RemoveCellAndDeeperRows(CellId);
			}
		}
	}
	for (const int32 NodeId : Changes.ChangedNodes)
	{
		const FChronoRoadNode* Node = Graph.FindNode(NodeId);
		if (!Node || Node->SegmentIds.Num() < 2)
		{
			continue;
		}

		const double Radius = Graph.GetNodeRadiusCm(NodeId) + Rules.RoadClearanceCm;
		const FVector2D Center(Node->Position.X, Node->Position.Y);

		QueryCells(FBox2D(Center - FVector2D(Radius, Radius), Center + FVector2D(Radius, Radius)), Candidates);
		for (const int32 CellId : Candidates)
		{
			const FChronoZoneCell* Cell = Cells.Find(CellId);
			if (Cell && IsBoxNearJunction(Graph, NodeId, GetCellBox(*Cell), Rules.RoadClearanceCm))
			{
				RemoveCellAndDeeperRows(CellId);
			}
		}
	}

	// 3. New cells along every changed segment, in id order so that the
	// result does not depend on the order of the change list.
	TArray<int32> ToGenerate = Changes.ChangedSegments;
	ToGenerate.Sort();
	for (const int32 SegmentId : ToGenerate)
	{
		GenerateSegmentCells(Graph, Heightfield, SegmentId, Rules, RoadCache, SavedZones.Find(SegmentId));
	}
}

void FChronoZoneGrid::GenerateSegmentCells(const FChronoRoadGraph& Graph, const FChronoHeightfield& Heightfield, int32 SegmentId, const FChronoZoningRules& Rules, FRoadCache& RoadCache, const TMap<int64, EChronoZoneType>* SavedZones)
{
	using namespace ChronoZoneGridPrivate;

	const FChronoRoadSegment* Segment = Graph.FindSegment(SegmentId);
	if (!Segment || Segment->GetUsableLength() < CellSizeCm)
	{
		return;
	}

	// A copy, not a reference: the cache grows while cells are tested against
	// other roads, which can move its entries.
	const TArray<FChronoRoadSample> Polyline = RoadCache.GetPolyline(*Segment);
	const double CellSize = CellSizeCm;
	const double HalfCell = 0.5 * CellSize;
	const double FirstRowOffset = Segment->Profile.GetHalfWidthCm() + Rules.RoadSetbackCm + HalfCell;

	auto GetPolyline = [&RoadCache](const FChronoRoadSegment& Other) -> const TArray<FChronoRoadSample>&
	{
		return RoadCache.GetPolyline(Other);
	};

	// Slots are counted from the start node, not from the junction, so a
	// slot keeps its index when a junction grows or shrinks.
	const int32 NumSlots = FMath::FloorToInt32(Segment->Length / CellSize);
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		const double SlotCenter = (Slot + 0.5) * CellSize;

		// Cells only front on the part of the road between its junctions.
		if (SlotCenter - HalfCell < Segment->TrimStart || SlotCenter + HalfCell > Segment->Length - Segment->TrimEnd)
		{
			continue;
		}

		const FChronoRoadSample Sample = ChronoRoadGeometry::SampleAtDistance(Polyline, SlotCenter);
		const FVector2D SideVector(-Sample.Tangent.Y, Sample.Tangent.X);
		const double Yaw = FMath::Atan2(Sample.Tangent.Y, Sample.Tangent.X);

		for (const int32 Side : { -1, 1 })
		{
			for (int32 Row = 0; Row < Rules.MaxDepthCells; ++Row)
			{
				FChronoZoneCell Cell;
				Cell.SegmentId = SegmentId;
				Cell.Side = Side;
				Cell.Slot = Slot;
				Cell.Row = Row;
				Cell.Center = Sample.Position + SideVector * (Side * (FirstRowOffset + Row * CellSize));
				Cell.YawRadians = Yaw;

				const FChronoOrientedBox2D Box = GetCellBox(Cell);

				// Inside the map.
				FVector2D Corners[4];
				Box.GetCorners(Corners);
				bool bValid = true;
				for (const FVector2D& Corner : Corners)
				{
					bValid = bValid && Heightfield.IsInsideWorld(Corner);
				}

				// Level enough to be built on.
				if (bValid)
				{
					float MinHeight = 0.0f;
					float MaxHeight = 0.0f;
					Heightfield.GetHeightRangeInOrientedBox(Box.Center, Box.HalfExtent, static_cast<float>(Box.YawRadians), MinHeight, MaxHeight);
					bValid = (MaxHeight - MinHeight) <= Rules.MaxHeightRangeCm;
				}

				// Clear of every road and junction, its own included (inner
				// side of a bend), and of every cell already in place.
				bValid = bValid
					&& IsBoxClearOfRoadsImpl(Graph, Box, Rules.RoadClearanceCm, GetPolyline)
					&& !OverlapsExistingCell(Box, Rules.CellOverlapToleranceCm);

				if (!bValid)
				{
					// Nothing is created behind a refused cell.
					break;
				}

				if (SavedZones)
				{
					if (const EChronoZoneType* Saved = SavedZones->Find(MakeCellKey(Side, Slot, Row)))
					{
						Cell.Zone = *Saved;
					}
				}

				AddCell(Cell);
			}
		}
	}

	DirtySegments.Add(SegmentId);
}

int32 FChronoZoneGrid::PaintZone(const FVector2D& Center, float RadiusCm, EChronoZoneType Zone)
{
	if (RadiusCm <= 0.0f || Zone >= EChronoZoneType::Count)
	{
		return 0;
	}

	TArray<int32> Candidates;
	QueryCells(FBox2D(Center - FVector2D(RadiusCm, RadiusCm), Center + FVector2D(RadiusCm, RadiusCm)), Candidates);

	int32 NumChanged = 0;
	const double RadiusSquared = static_cast<double>(RadiusCm) * RadiusCm;
	for (const int32 CellId : Candidates)
	{
		FChronoZoneCell& Cell = Cells.FindChecked(CellId);
		if (Cell.Zone != Zone && FVector2D::DistSquared(Cell.Center, Center) <= RadiusSquared)
		{
			Cell.Zone = Zone;
			DirtySegments.Add(Cell.SegmentId);
			++NumChanged;
		}
	}
	return NumChanged;
}

void FChronoZoneGrid::RebuildIndices()
{
	CellsBySegment.Reset();
	Buckets.Reset();

	// In id order, so the per-segment lists are deterministic.
	TArray<int32> CellIds;
	Cells.GetKeys(CellIds);
	CellIds.Sort();
	for (const int32 CellId : CellIds)
	{
		const FChronoZoneCell& Cell = Cells.FindChecked(CellId);
		CellsBySegment.FindOrAdd(Cell.SegmentId).Add(CellId);
		Buckets.FindOrAdd(GetBucket(Cell.Center)).Add(CellId);
	}
}

void FChronoZoneGrid::RemoveOrphanCells(const FChronoRoadGraph& Graph)
{
	TArray<int32> SegmentIds;
	CellsBySegment.GetKeys(SegmentIds);
	for (const int32 SegmentId : SegmentIds)
	{
		if (!Graph.FindSegment(SegmentId))
		{
			RemoveSegmentCells(SegmentId);
		}
	}
}

bool FChronoZoneGrid::Serialize(FArchive& Ar)
{
	// Bump when the binary layout changes.
	const int32 CurrentVersion = 1;
	// Sanity bound on the count read from a file.
	const int32 MaxCells = 16 * 1024 * 1024;

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

	Ar << CellSizeCm;
	Ar << NextCellId;

	// Saved in id order so that the same grid always gives the same bytes.
	TArray<int32> CellIds;
	Cells.GetKeys(CellIds);
	CellIds.Sort();
	int32 NumCells = CellIds.Num();
	Ar << NumCells;

	bool bValid = !Ar.IsError() && NumCells >= 0 && NumCells <= MaxCells && CellSizeCm > 0.0f;

	for (int32 Index = 0; bValid && Index < NumCells; ++Index)
	{
		FChronoZoneCell Cell;
		uint8 ZoneByte = 0;
		if (Ar.IsSaving())
		{
			Cell = Cells.FindChecked(CellIds[Index]);
			ZoneByte = static_cast<uint8>(Cell.Zone);
		}

		Ar << Cell.Id;
		Ar << Cell.SegmentId;
		Ar << Cell.Side;
		Ar << Cell.Slot;
		Ar << Cell.Row;
		Ar << Cell.Center;
		Ar << Cell.YawRadians;
		Ar << ZoneByte;

		if (Ar.IsLoading())
		{
			bValid = !Ar.IsError()
				&& Cell.Id > 0
				&& !Cells.Contains(Cell.Id)
				&& (Cell.Side == 1 || Cell.Side == -1)
				&& Cell.Slot >= 0
				&& Cell.Row >= 0
				&& ZoneByte < static_cast<uint8>(EChronoZoneType::Count);
			if (bValid)
			{
				Cell.Zone = static_cast<EChronoZoneType>(ZoneByte);
				Cells.Add(Cell.Id, Cell);
			}
		}
	}

	if (Ar.IsLoading())
	{
		if (!bValid)
		{
			Cells.Reset();
			CellsBySegment.Reset();
			Buckets.Reset();
			NextCellId = 1;
			Ar.SetError();
			return false;
		}

		RebuildIndices();
		for (const TPair<int32, TArray<int32>>& Pair : CellsBySegment)
		{
			DirtySegments.Add(Pair.Key);
		}
	}

	return !Ar.IsError();
}
