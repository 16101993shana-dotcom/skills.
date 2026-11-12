// Automation tests for the zoning grid. Filter: "Chronopolis.Zoning".

#include "ChronoGeometry2D.h"
#include "ChronoHeightfield.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "ChronoZoneGrid.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoZoningTests
{
	/** Flat ground, roads and zoning kept in step, as in the game. */
	struct FTown
	{
		FChronoHeightfield Heightfield;
		FChronoRoadGraph Graph;
		FChronoRoadRules RoadRules;
		FChronoZoneGrid Zoning;
		FChronoZoningRules ZoningRules;

		FTown()
		{
			Heightfield.Init(256, 200.0f, 0.0f);
			RoadRules.ApplyTo(Graph);
		}

		bool Build(const FVector2D& Start, const FVector2D& End)
		{
			FChronoRoadBuildRequest Request;
			Request.Start = Start;
			Request.End = End;
			Request.Profile.CarriagewayWidthCm = 700.0f;
			Request.Profile.SidewalkWidthCm = 200.0f;

			FChronoRoadGraph Scratch = Graph;
			Scratch.ClearChanges();
			int32 SegmentId = INDEX_NONE;
			if (ChronoRoadBuilder::ApplyAndValidate(Scratch, Heightfield, RoadRules, Request, SegmentId) != EChronoRoadBuildResult::Success)
			{
				return false;
			}

			Graph = MoveTemp(Scratch);
			const FChronoRoadChangeSet Changes = ChronoRoadBuilder::TakeChanges(Graph);
			ChronoRoadBuilder::ConformTerrain(Graph, Changes, RoadRules, Heightfield);
			Zoning.ApplyRoadChanges(Graph, Heightfield, Changes, ZoningRules);
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoGeometryBoxTest, "Chronopolis.Zoning.Geometry.OrientedBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoGeometryBoxTest::RunTest(const FString& Parameters)
{
	const FChronoOrientedBox2D A(FVector2D(0.0, 0.0), FVector2D(400.0, 400.0), 0.0);

	// Sharing an edge is not an overlap.
	const FChronoOrientedBox2D Neighbor(FVector2D(800.0, 0.0), FVector2D(400.0, 400.0), 0.0);
	TestFalse(TEXT("Boxes that share an edge do not overlap"), FChronoOrientedBox2D::Overlap(A, Neighbor, 1.0));

	const FChronoOrientedBox2D Intruder(FVector2D(700.0, 0.0), FVector2D(400.0, 400.0), 0.0);
	TestTrue(TEXT("Boxes 100 cm into each other overlap"), FChronoOrientedBox2D::Overlap(A, Intruder, 1.0));

	// A box turned 45 degrees reaches further along the axes.
	const FChronoOrientedBox2D Turned(FVector2D(900.0, 0.0), FVector2D(400.0, 400.0), FMath::DegreesToRadians(45.0));
	TestTrue(TEXT("A turned box reaches its neighbor"), FChronoOrientedBox2D::Overlap(A, Turned, 1.0));
	const FChronoOrientedBox2D TurnedAway(FVector2D(1000.0, 0.0), FVector2D(400.0, 400.0), FMath::DegreesToRadians(45.0));
	TestFalse(TEXT("A turned box far enough does not overlap"), FChronoOrientedBox2D::Overlap(A, TurnedAway, 1.0));

	// Distance to a segment: crossing, beside, and beyond a corner.
	TestTrue(TEXT("A crossing segment is at distance 0"), A.DistanceToSegment(FVector2D(-1000.0, 0.0), FVector2D(1000.0, 0.0)) == 0.0);
	TestTrue(TEXT("A parallel segment is at its offset"), FMath::IsNearlyEqual(A.DistanceToSegment(FVector2D(-1000.0, 650.0), FVector2D(1000.0, 650.0)), 250.0, 0.001));
	TestTrue(TEXT("A segment beyond a corner is measured from the corner"), FMath::IsNearlyEqual(A.DistanceToSegment(FVector2D(700.0, 800.0), FVector2D(2000.0, 800.0)), 500.0, 0.001));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoZoningLayoutTest, "Chronopolis.Zoning.Grid.CellsFollowRoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoZoningLayoutTest::RunTest(const FString& Parameters)
{
	ChronoZoningTests::FTown Town;

	// A 204 m road with two dead ends: 25 whole slots of 8 m, both sides, 4 rows deep.
	TestTrue(TEXT("Main road is built"), Town.Build(FVector2D(-10200.0, 0.0), FVector2D(10200.0, 0.0)));
	TestEqual(TEXT("A straight road gets every cell"), Town.Zoning.GetNumCells(), 25 * 2 * 4);

	// First row: half width 550 + setback 100 + half a cell 400.
	const FVector2D FirstRowPoint(-1600.0, 1050.0);
	TestTrue(TEXT("There is a cell in the first row"), Town.Zoning.FindCellAt(FirstRowPoint) != INDEX_NONE);
	TestTrue(TEXT("There is no cell on the road"), Town.Zoning.FindCellAt(FVector2D(-1600.0, 0.0)) == INDEX_NONE);

	// Paint, then cut the road with a branch: cells under the branch go
	// away, painted cells that still exist keep their zone.
	TestTrue(TEXT("Painting changes cells"), Town.Zoning.PaintZone(FVector2D(0.0, 2000.0), 3000.0f, EChronoZoneType::Residential) > 0);
	const int32 CellsBefore = Town.Zoning.GetNumCells();

	TestTrue(TEXT("Branch is built"), Town.Build(FVector2D(0.0, -8000.0), FVector2D(0.0, 0.0)));
	TestTrue(TEXT("The junction removes cells"), Town.Zoning.GetNumCells() != CellsBefore);
	TestTrue(TEXT("There is no cell on the branch"), Town.Zoning.FindCellAt(FVector2D(0.0, -3000.0)) == INDEX_NONE);
	TestTrue(TEXT("There is no cell on the junction"), Town.Zoning.FindCellAt(FVector2D(0.0, 800.0)) == INDEX_NONE);

	const FChronoZoneCell* Painted = Town.Zoning.FindCell(Town.Zoning.FindCellAt(FirstRowPoint));
	TestTrue(TEXT("A painted cell keeps its zone after the road is split"), Painted && Painted->Zone == EChronoZoneType::Residential);

	// Zero clipping contract: every cell is clear of every road, and no two cells overlap.
	TArray<int32> CellIds;
	Town.Zoning.GetCells().GetKeys(CellIds);
	int32 NumOnRoad = 0;
	int32 NumOverlaps = 0;
	for (int32 IndexA = 0; IndexA < CellIds.Num(); ++IndexA)
	{
		const FChronoOrientedBox2D BoxA = Town.Zoning.GetCellBox(Town.Zoning.GetCells().FindChecked(CellIds[IndexA]));
		if (!ChronoZoning::IsBoxClearOfRoads(Town.Graph, BoxA, 0.5 * Town.ZoningRules.RoadClearanceCm, Town.ZoningRules.RoadSampleSpacingCm))
		{
			++NumOnRoad;
		}
		for (int32 IndexB = IndexA + 1; IndexB < CellIds.Num(); ++IndexB)
		{
			const FChronoOrientedBox2D BoxB = Town.Zoning.GetCellBox(Town.Zoning.GetCells().FindChecked(CellIds[IndexB]));
			if (FChronoOrientedBox2D::Overlap(BoxA, BoxB, 2.0))
			{
				++NumOverlaps;
			}
		}
	}
	TestEqual(TEXT("No cell touches a road"), NumOnRoad, 0);
	TestEqual(TEXT("No two cells overlap"), NumOverlaps, 0);

	// Save and load.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Grid is saved"), Town.Zoning.Serialize(Writer));

	FChronoZoneGrid Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Grid is loaded"), Loaded.Serialize(Reader));
	TestEqual(TEXT("Loaded grid has the same cells"), Loaded.GetNumCells(), Town.Zoning.GetNumCells());
	const FChronoZoneCell* LoadedPainted = Loaded.FindCell(Loaded.FindCellAt(FirstRowPoint));
	TestTrue(TEXT("Loaded grid keeps the painted zone"), LoadedPainted && LoadedPainted->Zone == EChronoZoneType::Residential);

	// Removing the road removes its cells.
	FChronoRoadGraph Empty;
	Loaded.RemoveOrphanCells(Empty);
	TestEqual(TEXT("Cells without a road are removed"), Loaded.GetNumCells(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
