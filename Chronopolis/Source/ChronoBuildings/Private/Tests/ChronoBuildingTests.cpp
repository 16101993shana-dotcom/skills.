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
	};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoBuildingServiceTest, "Chronopolis.Buildings.ServicesAndLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoBuildingServiceTest::RunTest(const FString& Parameters)
{
	ChronoBuildingTests::FTown Town;
	TestTrue(TEXT("Road is built"), Town.Build(FVector2D(-10200.0, 0.0), FVector2D(10200.0, 0.0)));

	FChronoBuildingDefData Well;
	Well.DefId = TEXT("TestWell");
	Well.Zone = EChronoZoneType::Civic;
	Well.Service = EChronoServiceType::Water;
	Well.ServiceRadiusCm = 6000.0f;
	Well.UpkeepPerDay = 2.0f;
	Well.MaxFloors = 1;

	// The player places a well on unzoned land, by clicking a cell of the second row.
	const FVector2D Near(-1800.0, 1050.0);
	const FVector2D Far(9000.0, 1050.0);
	const float ValueBefore = Town.Buildings.ComputeLandValue(Near, true, Town.BuildingRules);

	const int32 ClickedCell = Town.Zoning.FindCellAt(FVector2D(-1800.0, 1850.0));
	TestTrue(TEXT("There is a cell to click"), ClickedCell != INDEX_NONE);
	const int32 WellId = Town.Buildings.PlaceBuilding(Town.Zoning, Town.Heightfield, Town.Graph, ClickedCell, Well, Town.BuildingRules);
	TestTrue(TEXT("The well is placed"), WellId != INDEX_NONE);

	const FChronoBuilding* Placed = Town.Buildings.FindBuilding(WellId);
	TestTrue(TEXT("The well stands on the front row"), Placed && Placed->bPlayerPlaced && Placed->Footprint.Contains(Near));
	TestTrue(TEXT("The same lot cannot be used twice"), Town.Buildings.PlaceBuilding(Town.Zoning, Town.Heightfield, Town.Graph, ClickedCell, Well, Town.BuildingRules) == INDEX_NONE);

	// Coverage: inside the radius, not outside.
	const uint32 WaterBit = 1u << static_cast<uint32>(EChronoServiceType::Water);
	TestTrue(TEXT("The well serves its surroundings"), (Town.Buildings.GetServiceMaskAt(Near) & WaterBit) != 0);
	TestTrue(TEXT("The well does not serve the far end of the road"), Town.Buildings.GetServiceMaskAt(Far) == 0);
	TestEqual(TEXT("One service is counted"), FChronoBuildingSet::CountServices(Town.Buildings.GetServiceMaskAt(Near)), 1);
	TestTrue(TEXT("A service raises the land value"), Town.Buildings.ComputeLandValue(Near, true, Town.BuildingRules) > ValueBefore);
	TestTrue(TEXT("The well costs upkeep"), FMath::IsNearlyEqual(Town.Buildings.GetTotalUpkeepPerDay(), 2.0, 0.001));
	TestEqual(TEXT("The well is found under the cursor"), Town.Buildings.FindBuildingAt(Placed ? Placed->Footprint.Center : Far), WellId);

	// A city grows around it. Services never grow by themselves.
	Town.Definitions.Add(Well);
	Town.Zoning.PaintZone(FVector2D(0.0, 0.0), 20000.0f, EChronoZoneType::Residential);
	Town.Zoning.TakeDirtySegments();
	Town.Grow(40);
	TestTrue(TEXT("Only the placed well provides a service"), FMath::IsNearlyEqual(Town.Buildings.GetTotalUpkeepPerDay(), 2.0, 0.001));

	// Levels: where neighbors and services raise the land value, buildings gain a storey.
	int32 NumUpgraded = 0;
	for (int32 Step = 0; Step < 200; ++Step)
	{
		FRandomStream Random(7000 + Step);
		NumUpgraded += Town.Buildings.UpgradeBuildings(Town.BuildingRules, Random, 1);
	}
	AddInfo(FString::Printf(TEXT("%d upgrades among %d buildings"), NumUpgraded, Town.Buildings.GetNumBuildings()));
	TestTrue(TEXT("Some buildings gain a level"), NumUpgraded > 0);

	bool bLevelsCoherent = true;
	bool bWellUntouched = true;
	for (const TPair<int32, FChronoBuilding>& Pair : Town.Buildings.GetBuildings())
	{
		const FChronoBuilding& Building = Pair.Value;
		bLevelsCoherent = bLevelsCoherent && Building.Level >= 1 && Building.Level <= Town.BuildingRules.MaxLevel && Building.Floors >= Building.Level;
		if (Building.bPlayerPlaced)
		{
			bWellUntouched = bWellUntouched && Building.Level == 1;
		}
	}
	TestTrue(TEXT("Levels stay within bounds and add storeys"), bLevelsCoherent);
	TestTrue(TEXT("Services do not level up"), bWellUntouched);

	// Erasing the zone removes what grew, not what the player placed.
	Town.Zoning.PaintZone(FVector2D(0.0, 0.0), 20000.0f, EChronoZoneType::None);
	Town.Buildings.RemoveInvalid(Town.Zoning, Town.Zoning.TakeDirtySegments());
	TestEqual(TEXT("Only the placed well remains"), Town.Buildings.GetNumBuildings(), 1);

	// Save and load keep the service.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Buildings are saved"), Town.Buildings.Serialize(Writer));
	FChronoBuildingSet Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Buildings are loaded"), Loaded.Serialize(Reader));
	TestTrue(TEXT("The loaded well still serves its surroundings"), (Loaded.GetServiceMaskAt(Near) & WaterBit) != 0);

	TestTrue(TEXT("The well is demolished"), Town.Buildings.DemolishBuilding(WellId));
	TestTrue(TEXT("Nothing is served any more"), Town.Buildings.GetServiceMaskAt(Near) == 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
