// Automation tests for building growth. Filter: "Chronopolis.Buildings".

#include "ChronoBuildingMeshBuilder.h"
#include "ChronoBuildingSet.h"
#include "ChronoHeightfield.h"
#include "ChronoMeshData.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "ChronoZoneGrid.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoBuildingTests
{
	/** Rolling ground, one road, zoning and buildings kept in step, as in the game. */
	struct FTown
	{
		FChronoHeightfield Heightfield;
		FChronoRoadGraph Graph;
		FChronoRoadRules RoadRules;
		FChronoZoneGrid Zoning;
		FChronoZoningRules ZoningRules;
		FChronoBuildingSet Buildings;
		FChronoBuildingRules BuildingRules;
		TArray<FChronoBuildingDefData> Definitions;

		FTown()
		{
			Heightfield.Init(256, 200.0f, 0.0f);
			Heightfield.GenerateNoise(11, 200.0f, 30000.0f, -5000.0f, 30000.0f);
			RoadRules.ApplyTo(Graph);

			FChronoBuildingDefData House;
			House.DefId = TEXT("TestHouse");
			House.Zone = EChronoZoneType::Residential;
			House.Roof = EChronoRoofType::Gable;
			Definitions.Add(House);

			FChronoBuildingDefData Block;
			Block.DefId = TEXT("TestBlock");
			Block.Zone = EChronoZoneType::Residential;
			Block.WidthCells = 2;
			Block.DepthCells = 2;
			Block.MaxFloors = 4;
			Definitions.Add(Block);
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
			Buildings.RemoveInvalid(Zoning, Zoning.TakeDirtySegments());
			return true;
		}

		int32 Grow(int32 Steps)
		{
			int32 Total = 0;
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				FRandomStream Random(1000 + Step);
				Total += Buildings.Grow(Zoning, Heightfield, Graph, Definitions, BuildingRules, Random, 4);
			}
			return Total;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoBuildingGrowthTest, "Chronopolis.Buildings.GrowOnZonedCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoBuildingGrowthTest::RunTest(const FString& Parameters)
{
	ChronoBuildingTests::FTown Town;
	TestTrue(TEXT("Road is built"), Town.Build(FVector2D(-10200.0, 0.0), FVector2D(10200.0, 0.0)));

	// Nothing grows on cells without a zone.
	TestEqual(TEXT("No building without a zone"), Town.Grow(5), 0);

	TestTrue(TEXT("Zone is painted"), Town.Zoning.PaintZone(FVector2D(0.0, 0.0), 20000.0f, EChronoZoneType::Residential) > 0);
	Town.Zoning.TakeDirtySegments();

	const int32 NumGrown = Town.Grow(40);
	AddInfo(FString::Printf(TEXT("Grew %d buildings"), NumGrown));
	TestTrue(TEXT("Buildings grow on zoned cells"), NumGrown >= 20);
	TestEqual(TEXT("Every grown building is kept"), Town.Buildings.GetNumBuildings(), NumGrown);

	// Zero clipping contract, checked directly on the data.
	TArray<int32> Ids;
	Town.Buildings.GetBuildings().GetKeys(Ids);
	int32 NumOnRoad = 0;
	int32 NumOverlaps = 0;
	int32 NumBadHeights = 0;
	bool bHasTallBuilding = false;
	for (int32 IndexA = 0; IndexA < Ids.Num(); ++IndexA)
	{
		const FChronoBuilding& A = Town.Buildings.GetBuildings().FindChecked(Ids[IndexA]);
		if (!ChronoZoning::IsBoxClearOfRoads(Town.Graph, A.Footprint, 0.5 * Town.BuildingRules.RoadClearanceCm, Town.BuildingRules.RoadSampleSpacingCm))
		{
			++NumOnRoad;
		}

		// The floor is above the ground at the center and at every corner,
		// and the foundation below it.
		FVector2D Corners[4];
		A.Footprint.GetCorners(Corners);
		const FVector2D Points[5] = { A.Footprint.Center, Corners[0], Corners[1], Corners[2], Corners[3] };
		for (const FVector2D& Point : Points)
		{
			const double Ground = Town.Heightfield.SampleHeight(Point);
			if (Ground > A.FloorHeightCm || Ground < A.FoundationBottomCm)
			{
				++NumBadHeights;
			}
		}

		bHasTallBuilding = bHasTallBuilding || A.BodyHeightCm > 300.0f;

		for (int32 IndexB = IndexA + 1; IndexB < Ids.Num(); ++IndexB)
		{
			if (FChronoOrientedBox2D::Overlap(A.Footprint, Town.Buildings.GetBuildings().FindChecked(Ids[IndexB]).Footprint, 2.0))
			{
				++NumOverlaps;
			}
		}
	}
	TestEqual(TEXT("No building touches a road"), NumOnRoad, 0);
	TestEqual(TEXT("No two buildings overlap"), NumOverlaps, 0);
	TestEqual(TEXT("No building is buried or floating"), NumBadHeights, 0);
	TestTrue(TEXT("Land value makes some buildings taller"), bHasTallBuilding);

	// The greybox volume of a building is a closed set of outward faces.
	FChronoMeshData Mesh;
	ChronoBuildingMeshBuilder::AppendBuilding(Town.Buildings.GetBuildings().FindChecked(Ids[0]), FVector::ZeroVector, Mesh);
	TestTrue(TEXT("A building has a mesh"), Mesh.NumTriangles() >= 12);

	// Save and load.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Buildings are saved"), Town.Buildings.Serialize(Writer));
	FChronoBuildingSet Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Buildings are loaded"), Loaded.Serialize(Reader));
	TestEqual(TEXT("Loaded set has the same buildings"), Loaded.GetNumBuildings(), Town.Buildings.GetNumBuildings());

	// Erasing the zone removes the buildings that stood on it.
	Town.Zoning.PaintZone(FVector2D(0.0, 0.0), 20000.0f, EChronoZoneType::None);
	Town.Buildings.RemoveInvalid(Town.Zoning, Town.Zoning.TakeDirtySegments());
	TestEqual(TEXT("Buildings disappear with their zone"), Town.Buildings.GetNumBuildings(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
