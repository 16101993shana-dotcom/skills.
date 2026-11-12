#include "ChronoClippingAudit.h"

#include "ChronoBuildingSet.h"
#include "ChronoHeightfield.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGeometry.h"
#include "ChronoRoadGraph.h"
#include "ChronoRoadMeshBuilder.h"
#include "ChronoZoneGrid.h"

namespace ChronoAuditRule
{
	const FName TerrainThroughRoad(TEXT("TerrainThroughRoad"));
	const FName RoadFloating(TEXT("RoadFloating"));
	const FName RoadOverlap(TEXT("RoadOverlap"));
	const FName JunctionAngle(TEXT("JunctionAngle"));
	const FName SegmentSwallowed(TEXT("SegmentSwallowed"));
	const FName JunctionMesh(TEXT("JunctionMesh"));
	const FName JunctionStep(TEXT("JunctionStep"));
	const FName ZoneCellOnRoad(TEXT("ZoneCellOnRoad"));
	const FName ZoneCellOverlap(TEXT("ZoneCellOverlap"));
	const FName ZoneCellOrphan(TEXT("ZoneCellOrphan"));
	const FName BuildingOnRoad(TEXT("BuildingOnRoad"));
	const FName BuildingOverlap(TEXT("BuildingOverlap"));
	const FName BuildingBuried(TEXT("BuildingBuried"));
	const FName BuildingFloating(TEXT("BuildingFloating"));
	const FName BuildingWithoutLot(TEXT("BuildingWithoutLot"));
}

namespace ChronoClippingAuditPrivate
{
	struct FContext
	{
		const FChronoHeightfield& Heightfield;
		const FChronoRoadGraph& Graph;
		const FChronoRoadRules& Rules;
		const FChronoAuditSettings& Settings;
		TArray<FChronoAuditIssue>& Issues;
		int32 NumAdded = 0;

		bool IsFull() const { return Issues.Num() >= Settings.MaxIssues; }

		void Add(const FName& Rule, const FVector& Location, FString&& Description)
		{
			if (!IsFull())
			{
				FChronoAuditIssue& Issue = Issues.AddDefaulted_GetRef();
				Issue.Rule = Rule;
				Issue.Location = Location;
				Issue.Description = MoveTemp(Description);
				++NumAdded;
			}
		}
	};

	/**
	 * Compares the ground with a road surface at one point.
	 * @param SurfaceHeight height of the visible surface at that point.
	 * @param RoadHeight height of the carriageway there (the side faces go down from it).
	 * @return false if an issue was reported.
	 */
	bool CheckGround(FContext& Context, const FVector2D& Point, double SurfaceHeight, double RoadHeight, const TCHAR* What, int32 Id)
	{
		const double Ground = Context.Heightfield.SampleHeight(Point);

		if (Ground > SurfaceHeight - Context.Settings.MinGroundGapCm)
		{
			Context.Add(ChronoAuditRule::TerrainThroughRoad, FVector(Point.X, Point.Y, SurfaceHeight),
				FString::Printf(TEXT("%s %d: ground at %.1f cm, surface at %.1f cm"), What, Id, Ground, SurfaceHeight));
			return false;
		}

		if (Ground < RoadHeight - Context.Rules.SkirtDepthCm)
		{
			Context.Add(ChronoAuditRule::RoadFloating, FVector(Point.X, Point.Y, RoadHeight),
				FString::Printf(TEXT("%s %d: ground at %.1f cm is %.1f cm below the road, side faces are %.1f cm deep"), What, Id, Ground, RoadHeight - Ground, Context.Rules.SkirtDepthCm));
			return false;
		}

		return true;
	}

	/** Terrain against every segment ribbon, between its junctions. */
	void AuditSegmentsAgainstTerrain(FContext& Context)
	{
		TArray<FChronoRoadSample> FullSamples;
		TArray<FChronoRoadSample> Samples;

		for (const TPair<int32, FChronoRoadSegment>& Pair : Context.Graph.GetSegments())
		{
			if (Context.IsFull())
			{
				return;
			}

			const FChronoRoadSegment& Segment = Pair.Value;
			if (Segment.GetUsableLength() <= 1.0)
			{
				Context.Add(ChronoAuditRule::SegmentSwallowed, FVector(Segment.Curve.Evaluate(0.5), 0.0),
					FString::Printf(TEXT("Segment %d: usable length %.1f cm"), Segment.Id, Segment.GetUsableLength()));
				continue;
			}

			ChronoRoadGeometry::BuildPolyline(Segment.Curve, Context.Rules.SampleSpacingCm, FullSamples);
			ChronoRoadGeometry::ClipPolyline(FullSamples, Segment.TrimStart, Segment.Length - Segment.TrimEnd, Samples);

			const double HalfWidth = Segment.Profile.GetHalfWidthCm();
			const double HalfCarriageway = 0.5 * Segment.Profile.CarriagewayWidthCm;
			const double Curb = Segment.Profile.HasSidewalk() ? Segment.Profile.CurbHeightCm : 0.0;

			// Across the road: both outer edges, both curbs and the centerline.
			// The curb points use the carriageway height, the stricter of the two.
			const double Offsets[5] = { -HalfWidth, -HalfCarriageway, 0.0, HalfCarriageway, HalfWidth };
			const double Raise[5] = { Curb, 0.0, 0.0, 0.0, Curb };

			bool bSegmentClean = true;
			for (int32 SampleIndex = 0; SampleIndex < Samples.Num() && bSegmentClean; ++SampleIndex)
			{
				const FChronoRoadSample& Sample = Samples[SampleIndex];
				const double RoadHeight = Context.Graph.GetSegmentHeightAtDistance(Segment, Sample.Distance);
				const FVector2D Side(-Sample.Tangent.Y, Sample.Tangent.X);

				for (int32 OffsetIndex = 0; OffsetIndex < 5 && bSegmentClean; ++OffsetIndex)
				{
					// One issue per segment is enough to locate the problem.
					bSegmentClean = CheckGround(Context, Sample.Position + Side * Offsets[OffsetIndex], RoadHeight + Raise[OffsetIndex], RoadHeight, TEXT("Segment"), Segment.Id);
				}
			}
		}
	}

	/** Junction outlines: mesh validity, height continuity and terrain. */
	void AuditJunctions(FContext& Context)
	{
		TArray<FVector2D> Polygon;
		TArray<bool> EdgeIsMouth;
		TArray<int32> Triangles;

		for (const TPair<int32, FChronoRoadNode>& Pair : Context.Graph.GetNodes())
		{
			if (Context.IsFull())
			{
				return;
			}

			const FChronoRoadNode& Node = Pair.Value;
			if (Node.SegmentIds.Num() < 2)
			{
				continue;
			}

			const float MinAngle = Context.Graph.GetNodeMinAngleDegrees(Node.Id);
			if (MinAngle < Context.Rules.MinIntersectionAngleDegrees - 0.01f)
			{
				Context.Add(ChronoAuditRule::JunctionAngle, Node.Position,
					FString::Printf(TEXT("Node %d: smallest angle %.1f degrees, minimum %.1f"), Node.Id, MinAngle, Context.Rules.MinIntersectionAngleDegrees));
			}

			// Every road must arrive at the height of the junction.
			for (const int32 SegmentId : Node.SegmentIds)
			{
				const FChronoRoadSegment* Segment = Context.Graph.FindSegment(SegmentId);
				if (!Segment)
				{
					continue;
				}

				const double MouthDistance = (Segment->StartNodeId == Node.Id) ? static_cast<double>(Segment->TrimStart) : Segment->Length - Segment->TrimEnd;
				const double Step = FMath::Abs(Context.Graph.GetSegmentHeightAtDistance(*Segment, MouthDistance) - Node.Position.Z);
				if (Step > Context.Settings.MaxJunctionStepCm)
				{
					Context.Add(ChronoAuditRule::JunctionStep, Node.Position,
						FString::Printf(TEXT("Node %d: segment %d arrives %.2f cm off the junction height"), Node.Id, SegmentId, Step));
				}
			}

			if (!ChronoRoadMeshBuilder::BuildNodePolygon(Context.Graph, Node.Id, Context.Rules.SampleSpacingCm, Polygon, EdgeIsMouth))
			{
				Context.Add(ChronoAuditRule::JunctionMesh, Node.Position, FString::Printf(TEXT("Node %d: no junction outline"), Node.Id));
				continue;
			}

			// A straight joint between two roads has a valid outline of zero
			// area: nothing to triangulate, nothing to check against the ground.
			if (FMath::Abs(ChronoRoadGeometry::ComputePolygonArea(Polygon)) < 1.0)
			{
				continue;
			}

			if (!ChronoRoadGeometry::TriangulatePolygon(Polygon, Triangles))
			{
				Context.Add(ChronoAuditRule::JunctionMesh, Node.Position, FString::Printf(TEXT("Node %d: junction outline cannot be triangulated"), Node.Id));
				continue;
			}

			// Terrain under the junction: every corner and the middle of every triangle.
			bool bClean = true;
			for (int32 Index = 0; Index < Polygon.Num() && bClean; ++Index)
			{
				bClean = CheckGround(Context, Polygon[Index], Node.Position.Z, Node.Position.Z, TEXT("Node"), Node.Id);
			}
			for (int32 Index = 0; Index + 2 < Triangles.Num() && bClean; Index += 3)
			{
				const FVector2D Middle = (Polygon[Triangles[Index]] + Polygon[Triangles[Index + 1]] + Polygon[Triangles[Index + 2]]) / 3.0;
				bClean = CheckGround(Context, Middle, Node.Position.Z, Node.Position.Z, TEXT("Node"), Node.Id);
			}
		}
	}

	/** Every pair of segments: one ribbon must not enter the other. */
	void AuditRoadOverlaps(FContext& Context)
	{
		struct FRibbon
		{
			const FChronoRoadSegment* Segment = nullptr;
			TArray<FChronoRoadSample> Samples;
		};

		// Ribbons between junctions, built once.
		TArray<FRibbon> Ribbons;
		TArray<FChronoRoadSample> FullSamples;
		for (const TPair<int32, FChronoRoadSegment>& Pair : Context.Graph.GetSegments())
		{
			if (Pair.Value.GetUsableLength() > 1.0)
			{
				FRibbon& Ribbon = Ribbons.AddDefaulted_GetRef();
				Ribbon.Segment = &Pair.Value;
				ChronoRoadGeometry::BuildPolyline(Pair.Value.Curve, Context.Rules.SampleSpacingCm, FullSamples);
				ChronoRoadGeometry::ClipPolyline(FullSamples, Pair.Value.TrimStart, Pair.Value.Length - Pair.Value.TrimEnd, Ribbon.Samples);
			}
		}

		const double Tolerance = Context.Settings.OverlapToleranceCm;

		for (int32 IndexA = 0; IndexA < Ribbons.Num(); ++IndexA)
		{
			for (int32 IndexB = 0; IndexB < Ribbons.Num(); ++IndexB)
			{
				if (Context.IsFull())
				{
					return;
				}
				if (IndexA == IndexB)
				{
					continue;
				}

				const FRibbon& A = Ribbons[IndexA];
				const FRibbon& B = Ribbons[IndexB];
				if (!A.Segment->Bounds.Intersect(B.Segment->Bounds))
				{
					continue;
				}

				// Points on both edges of A must not be inside B. Shrinking B
				// by the tolerance ignores edges that merely touch.
				const double HalfWidthA = A.Segment->Profile.GetHalfWidthCm();
				const double HalfWidthB = B.Segment->Profile.GetHalfWidthCm() - Tolerance;

				bool bOverlap = false;
				FVector2D Where = FVector2D::ZeroVector;
				for (int32 SampleIndex = 0; SampleIndex < A.Samples.Num() && !bOverlap; ++SampleIndex)
				{
					const FChronoRoadSample& Sample = A.Samples[SampleIndex];
					const FVector2D Side(-Sample.Tangent.Y, Sample.Tangent.X);
					for (const double Sign : { -1.0, 1.0 })
					{
						const FVector2D EdgePoint = Sample.Position + Side * (HalfWidthA * Sign);
						if (ChronoRoadGeometry::IsPointInsideRibbon(EdgePoint, B.Samples, HalfWidthB))
						{
							bOverlap = true;
							Where = EdgePoint;
							break;
						}
					}
				}

				if (bOverlap)
				{
					Context.Add(ChronoAuditRule::RoadOverlap, FVector(Where.X, Where.Y, Context.Heightfield.SampleHeight(Where)),
						FString::Printf(TEXT("Segment %d overlaps segment %d"), A.Segment->Id, B.Segment->Id));
				}
			}
		}
	}
}

int32 ChronoClippingAudit::Run(const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoRoadRules& Rules, const FChronoAuditSettings& Settings, TArray<FChronoAuditIssue>& OutIssues)
{
	using namespace ChronoClippingAuditPrivate;

	FContext Context{ Heightfield, Graph, Rules, Settings, OutIssues };
	if (!Heightfield.IsValid())
	{
		return 0;
	}

	AuditSegmentsAgainstTerrain(Context);
	AuditJunctions(Context);
	AuditRoadOverlaps(Context);
	return Context.NumAdded;
}

int32 ChronoClippingAudit::RunZoning(const FChronoRoadGraph& Graph, const FChronoZoneGrid& Grid, const FChronoZoningRules& ZoningRules, const FChronoAuditSettings& Settings, TArray<FChronoAuditIssue>& OutIssues)
{
	int32 NumAdded = 0;
	auto Add = [&OutIssues, &Settings, &NumAdded](const FName& Rule, const FVector2D& Where, FString&& Description)
	{
		if (OutIssues.Num() < Settings.MaxIssues)
		{
			FChronoAuditIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Rule = Rule;
			Issue.Location = FVector(Where.X, Where.Y, 0.0);
			Issue.Description = MoveTemp(Description);
			++NumAdded;
		}
	};

	// In id order, so the report is the same from one run to the next.
	TArray<int32> CellIds;
	Grid.GetCells().GetKeys(CellIds);
	CellIds.Sort();

	// The audit does not trust the spatial index of the grid: it files the
	// cells in its own buckets, so a broken index cannot hide an overlap.
	const double BucketSize = 2.0 * FMath::Max(Grid.GetCellSizeCm(), 1.0f);
	auto BucketOf = [BucketSize](const FVector2D& Point)
	{
		return FIntPoint(FMath::FloorToInt32(Point.X / BucketSize), FMath::FloorToInt32(Point.Y / BucketSize));
	};
	TMap<FIntPoint, TArray<int32>> Buckets;
	for (const int32 CellId : CellIds)
	{
		Buckets.FindOrAdd(BucketOf(Grid.GetCells().FindChecked(CellId).Center)).Add(CellId);
	}

	for (const int32 CellId : CellIds)
	{
		if (OutIssues.Num() >= Settings.MaxIssues)
		{
			break;
		}

		const FChronoZoneCell& Cell = Grid.GetCells().FindChecked(CellId);
		const FChronoOrientedBox2D Box = Grid.GetCellBox(Cell);

		if (!Graph.FindSegment(Cell.SegmentId))
		{
			Add(ChronoAuditRule::ZoneCellOrphan, Cell.Center, FString::Printf(TEXT("Cell %d fronts on missing segment %d"), Cell.Id, Cell.SegmentId));
			continue;
		}

		// A cell may come as close to a road as the rules allow, not closer.
		// Half of the allowed clearance leaves room for numerical noise.
		if (!ChronoZoning::IsBoxClearOfRoads(Graph, Box, 0.5 * ZoningRules.RoadClearanceCm, ZoningRules.RoadSampleSpacingCm))
		{
			Add(ChronoAuditRule::ZoneCellOnRoad, Cell.Center, FString::Printf(TEXT("Cell %d (segment %d, slot %d, row %d) touches a road or a junction"), Cell.Id, Cell.SegmentId, Cell.Slot, Cell.Row));
		}

		// Each pair is tested once: only against cells with a higher id.
		const FIntPoint Home = BucketOf(Cell.Center);
		for (int32 Y = Home.Y - 1; Y <= Home.Y + 1; ++Y)
		{
			for (int32 X = Home.X - 1; X <= Home.X + 1; ++X)
			{
				const TArray<int32>* Bucket = Buckets.Find(FIntPoint(X, Y));
				if (!Bucket)
				{
					continue;
				}
				for (const int32 OtherId : *Bucket)
				{
					if (OtherId > CellId && FChronoOrientedBox2D::Overlap(Box, Grid.GetCellBox(Grid.GetCells().FindChecked(OtherId)), Settings.OverlapToleranceCm + ZoningRules.CellOverlapToleranceCm))
					{
						Add(ChronoAuditRule::ZoneCellOverlap, Cell.Center, FString::Printf(TEXT("Cell %d overlaps cell %d"), CellId, OtherId));
					}
				}
			}
		}
	}

	return NumAdded;
}

int32 ChronoClippingAudit::RunBuildings(const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoZoneGrid& Grid, const FChronoBuildingSet& Buildings, const FChronoBuildingRules& BuildingRules, const FChronoAuditSettings& Settings, TArray<FChronoAuditIssue>& OutIssues)
{
	int32 NumAdded = 0;
	auto Add = [&OutIssues, &Settings, &NumAdded](const FName& Rule, const FChronoBuilding& Building, FString&& Description)
	{
		if (OutIssues.Num() < Settings.MaxIssues)
		{
			FChronoAuditIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Rule = Rule;
			Issue.Location = FVector(Building.Footprint.Center.X, Building.Footprint.Center.Y, Building.FloorHeightCm);
			Issue.Description = MoveTemp(Description);
			++NumAdded;
		}
	};

	// In id order, so the report is the same from one run to the next.
	TArray<int32> BuildingIds;
	Buildings.GetBuildings().GetKeys(BuildingIds);
	BuildingIds.Sort();

	// Number of ground samples along each side of a footprint.
	const int32 GroundSamplesPerSide = 9;

	for (int32 IndexA = 0; IndexA < BuildingIds.Num() && OutIssues.Num() < Settings.MaxIssues; ++IndexA)
	{
		const FChronoBuilding& Building = Buildings.GetBuildings().FindChecked(BuildingIds[IndexA]);
		const FChronoOrientedBox2D& Footprint = Building.Footprint;

		// Lot: every cell still exists, with the zone of the building.
		bool bLotIntact = true;
		for (int32 Slot = Building.FirstSlot; Slot < Building.FirstSlot + Building.WidthCells && bLotIntact; ++Slot)
		{
			for (int32 Row = 0; Row < Building.DepthCells && bLotIntact; ++Row)
			{
				const FChronoZoneCell* Cell = Grid.FindCellByKey(Building.SegmentId, Building.Side, Slot, Row);
				bLotIntact = Cell && Cell->Zone == Building.Zone;
			}
		}
		if (!bLotIntact)
		{
			Add(ChronoAuditRule::BuildingWithoutLot, Building, FString::Printf(TEXT("Building %d (%s) has lost part of its lot on segment %d"), Building.Id, *Building.DefId.ToString(), Building.SegmentId));
		}

		// Roads. Half of the allowed clearance leaves room for numerical noise.
		if (!ChronoZoning::IsBoxClearOfRoads(Graph, Footprint, 0.5 * BuildingRules.RoadClearanceCm, BuildingRules.RoadSampleSpacingCm))
		{
			Add(ChronoAuditRule::BuildingOnRoad, Building, FString::Printf(TEXT("Building %d (%s) touches a road or a junction"), Building.Id, *Building.DefId.ToString()));
		}

		// Ground under the walls and across the floor.
		const FVector2D AxisX = Footprint.GetAxisX() * Footprint.HalfExtent.X;
		const FVector2D AxisY = Footprint.GetAxisY() * Footprint.HalfExtent.Y;
		double MinGround = TNumericLimits<double>::Max();
		double MaxGround = TNumericLimits<double>::Lowest();
		for (int32 Y = 0; Y < GroundSamplesPerSide; ++Y)
		{
			for (int32 X = 0; X < GroundSamplesPerSide; ++X)
			{
				const double U = 2.0 * X / (GroundSamplesPerSide - 1) - 1.0;
				const double V = 2.0 * Y / (GroundSamplesPerSide - 1) - 1.0;
				const double Ground = Heightfield.SampleHeight(Footprint.Center + AxisX * U + AxisY * V);
				MinGround = FMath::Min(MinGround, Ground);
				MaxGround = FMath::Max(MaxGround, Ground);
			}
		}
		if (MaxGround > Building.FloorHeightCm)
		{
			Add(ChronoAuditRule::BuildingBuried, Building, FString::Printf(TEXT("Building %d (%s): ground at %.1f cm, floor at %.1f cm"), Building.Id, *Building.DefId.ToString(), MaxGround, Building.FloorHeightCm));
		}
		if (MinGround < Building.FoundationBottomCm)
		{
			Add(ChronoAuditRule::BuildingFloating, Building, FString::Printf(TEXT("Building %d (%s): ground at %.1f cm, foundation ends at %.1f cm"), Building.Id, *Building.DefId.ToString(), MinGround, Building.FoundationBottomCm));
		}

		// Other buildings. Every pair is tested, without the spatial index of
		// the set: a broken index must not hide an overlap.
		const FBox2D BoundsA = Footprint.GetBounds();
		for (int32 IndexB = IndexA + 1; IndexB < BuildingIds.Num(); ++IndexB)
		{
			const FChronoBuilding& Other = Buildings.GetBuildings().FindChecked(BuildingIds[IndexB]);
			if (BoundsA.Intersect(Other.Footprint.GetBounds()) && FChronoOrientedBox2D::Overlap(Footprint, Other.Footprint, Settings.OverlapToleranceCm + BuildingRules.OverlapToleranceCm))
			{
				Add(ChronoAuditRule::BuildingOverlap, Building, FString::Printf(TEXT("Building %d overlaps building %d"), Building.Id, Other.Id));
			}
		}
	}

	return NumAdded;
}
