// Zero-clipping audit run headless on a generated city. This is the test that
// continuous integration runs: it builds terrain and roads through the same
// rules as the game, then requires the audit to find nothing.
// Filter: "Chronopolis.Audit".

#include "ChronoBuildingSet.h"
#include "ChronoClippingAudit.h"
#include "ChronoHeightfield.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "ChronoZoneGrid.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoAuditTests
{
	struct FCity
	{
		FChronoHeightfield Heightfield;
		FChronoRoadGraph Graph;
		FChronoRoadRules Rules;
		FChronoZoneGrid Zoning;
		FChronoZoningRules ZoningRules;
		FChronoBuildingSet Buildings;
		FChronoBuildingRules BuildingRules;
		int32 NumBuilt = 0;
		int32 NumRefused = 0;

		FCity()
		{
			// 512 m of gently rolling ground.
			Heightfield.Init(256, 200.0f, 0.0f);
			Heightfield.GenerateNoise(7, 300.0f, 30000.0f, -5000.0f, 30000.0f);
			Rules.ApplyTo(Graph);
		}

		/** Same sequence as UChronoRoadSubsystem::BuildRoad, without a world. */
		EChronoRoadBuildResult Build(const FVector2D& Start, const FVector2D& End, const FVector2D* Control = nullptr)
		{
			FChronoRoadProfileData Profile;
			Profile.ProfileId = TEXT("TestStreet");
			Profile.CarriagewayWidthCm = 700.0f;
			Profile.SidewalkWidthCm = 200.0f;
			Profile.CurbHeightCm = 15.0f;

			FChronoRoadBuildRequest Request;
			Request.Start = Start;
			Request.End = End;
			Request.bCurved = (Control != nullptr);
			Request.Control = Control ? *Control : FVector2D::ZeroVector;
			Request.Profile = Profile;

			FChronoRoadGraph Scratch = Graph;
			Scratch.ClearChanges();

			int32 SegmentId = INDEX_NONE;
			const EChronoRoadBuildResult Result = ChronoRoadBuilder::ApplyAndValidate(Scratch, Heightfield, Rules, Request, SegmentId);
			if (Result != EChronoRoadBuildResult::Success)
			{
				++NumRefused;
				return Result;
			}

			Graph = MoveTemp(Scratch);
			const FChronoRoadChangeSet Changes = ChronoRoadBuilder::TakeChanges(Graph);
			ChronoRoadBuilder::ConformTerrain(Graph, Changes, Rules, Heightfield);
			Zoning.ApplyRoadChanges(Graph, Heightfield, Changes, ZoningRules);
			Buildings.RemoveInvalid(Zoning, Zoning.TakeDirtySegments());
			Buildings.RefreshHeights(Heightfield, FBox2D(FVector2D(-30000.0, -30000.0), FVector2D(30000.0, 30000.0)), BuildingRules);
			++NumBuilt;
			return Result;
		}

		/** A street grid with crossings and T junctions, plus one curved road. */
		void BuildGrid()
		{
			const double Rows[4] = { -15000.0, -5000.0, 5000.0, 15000.0 };
			const double Columns[4] = { -15000.0, -5000.0, 5000.0, 15000.0 };

			// East-west roads, each in one piece.
			for (const double Y : Rows)
			{
				Build(FVector2D(-20000.0, Y), FVector2D(20000.0, Y));
			}

			// North-south roads, block by block: each end lands on an
			// east-west road and splits it into an intersection.
			for (const double X : Columns)
			{
				for (int32 Row = 0; Row + 1 < 4; ++Row)
				{
					Build(FVector2D(X, Rows[Row]), FVector2D(X, Rows[Row + 1]));
				}
			}

			// A curved road on its own, away from the grid.
			const FVector2D Control(-17000.0, -24500.0);
			Build(FVector2D(-23000.0, -22500.0), FVector2D(-10000.0, -21500.0), &Control);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoAuditCleanCityTest, "Chronopolis.Audit.GeneratedCityIsClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoAuditCleanCityTest::RunTest(const FString& Parameters)
{
	ChronoAuditTests::FCity City;
	City.BuildGrid();

	AddInfo(FString::Printf(TEXT("Built %d roads (%d refused): %d segments, %d nodes"), City.NumBuilt, City.NumRefused, City.Graph.GetSegments().Num(), City.Graph.GetNodes().Num()));

	// The scenario must really contain a network, otherwise a clean audit proves nothing.
	TestTrue(TEXT("Most roads of the scenario are accepted"), City.NumBuilt >= 12);
	TestTrue(TEXT("The scenario contains intersections"), City.Graph.GetSegments().Num() > City.NumBuilt);

	TArray<FChronoAuditIssue> Issues;
	ChronoClippingAudit::Run(City.Heightfield, City.Graph, City.Rules, FChronoAuditSettings(), Issues);

	for (const FChronoAuditIssue& Issue : Issues)
	{
		AddError(FString::Printf(TEXT("[%s] %s at %s"), *Issue.Rule.ToString(), *Issue.Description, *Issue.Location.ToCompactString()));
	}
	TestEqual(TEXT("Zero-clipping audit reports nothing"), Issues.Num(), 0);

	// Zoning cells laid out along those roads, updated after every edit.
	AddInfo(FString::Printf(TEXT("Zoning: %d cells"), City.Zoning.GetNumCells()));
	TestTrue(TEXT("The scenario contains zoning cells"), City.Zoning.GetNumCells() > 100);

	TArray<FChronoAuditIssue> ZoningIssues;
	ChronoClippingAudit::RunZoning(City.Graph, City.Zoning, City.ZoningRules, FChronoAuditSettings(), ZoningIssues);
	for (const FChronoAuditIssue& Issue : ZoningIssues)
	{
		AddError(FString::Printf(TEXT("[%s] %s at %s"), *Issue.Rule.ToString(), *Issue.Description, *Issue.Location.ToCompactString()));
	}
	TestEqual(TEXT("Zoning audit reports nothing"), ZoningIssues.Num(), 0);

	// Buildings grown on those cells, then a new road laid through the city:
	// buildings in its way go, the others are re-seated on the reshaped ground.
	TArray<FChronoBuildingDefData> Definitions;
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

	City.Zoning.PaintZone(FVector2D(0.0, 0.0), 40000.0f, EChronoZoneType::Residential);
	City.Zoning.TakeDirtySegments();
	for (int32 Step = 0; Step < 60; ++Step)
	{
		FRandomStream Random(500 + Step);
		City.Buildings.Grow(City.Zoning, City.Heightfield, City.Graph, Definitions, City.BuildingRules, Random, 4);
	}
	AddInfo(FString::Printf(TEXT("Buildings: %d"), City.Buildings.GetNumBuildings()));
	TestTrue(TEXT("The scenario contains buildings"), City.Buildings.GetNumBuildings() >= 50);

	City.Build(FVector2D(0.0, -15000.0), FVector2D(0.0, -5000.0));

	TArray<FChronoAuditIssue> BuildingIssues;
	ChronoClippingAudit::RunBuildings(City.Heightfield, City.Graph, City.Zoning, City.Buildings, City.BuildingRules, FChronoAuditSettings(), BuildingIssues);
	ChronoClippingAudit::Run(City.Heightfield, City.Graph, City.Rules, FChronoAuditSettings(), BuildingIssues);
	ChronoClippingAudit::RunZoning(City.Graph, City.Zoning, City.ZoningRules, FChronoAuditSettings(), BuildingIssues);
	for (const FChronoAuditIssue& Issue : BuildingIssues)
	{
		AddError(FString::Printf(TEXT("[%s] %s at %s"), *Issue.Rule.ToString(), *Issue.Description, *Issue.Location.ToCompactString()));
	}
	TestEqual(TEXT("Audit with buildings reports nothing"), BuildingIssues.Num(), 0);

	// A refused road must leave no trace: an overlapping road is rejected and
	// the audit is still clean afterwards.
	const int32 SegmentsBefore = City.Graph.GetSegments().Num();
	const EChronoRoadBuildResult Overlap = City.Build(FVector2D(-19000.0, -4000.0), FVector2D(19000.0, -4000.0));
	TestTrue(TEXT("A road laid along another one is refused"), Overlap != EChronoRoadBuildResult::Success);
	TestEqual(TEXT("A refused road changes nothing"), City.Graph.GetSegments().Num(), SegmentsBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoAuditRandomRoadsTest, "Chronopolis.Audit.RandomRoadsStayClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoAuditRandomRoadsTest::RunTest(const FString& Parameters)
{
	// A tidy street grid only exercises right angles. Here roads are thrown
	// at random on rough ground: straight and curved, two road widths, half of
	// them chained from the end of the previous one. Most are refused, which
	// is the point: whatever is accepted must leave the city clean.
	const int32 NumSeeds = 4;
	const int32 AttemptsPerSeed = 300;
	const float ReliefAmplitudeCm = 800.0f;

	TArray<FChronoBuildingDefData> Definitions;
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

	for (int32 Seed = 1; Seed <= NumSeeds; ++Seed)
	{
		FChronoHeightfield Heightfield;
		Heightfield.Init(256, 200.0f, 0.0f);
		Heightfield.GenerateNoise(Seed, ReliefAmplitudeCm, 30000.0f, -5000.0f, 30000.0f);

		FChronoRoadGraph Graph;
		FChronoRoadRules Rules;
		Rules.ApplyTo(Graph);
		FChronoZoneGrid Zoning;
		FChronoZoningRules ZoningRules;
		FChronoBuildingSet Buildings;
		FChronoBuildingRules BuildingRules;

		FRandomStream Random(Seed * 977);
		FVector2D Last = FVector2D::ZeroVector;
		int32 NumBuilt = 0;

		for (int32 Attempt = 0; Attempt < AttemptsPerSeed; ++Attempt)
		{
			FChronoRoadBuildRequest Request;
			Request.Start = (Attempt % 2 == 0) ? Last : FVector2D(Random.FRandRange(-22000.0f, 22000.0f), Random.FRandRange(-22000.0f, 22000.0f));

			const double Angle = Random.FRandRange(0.0f, 2.0f * UE_PI);
			const double Length = Random.FRandRange(2500.0f, 14000.0f);
			const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
			Request.End = Request.Start + Direction * Length;

			Request.bCurved = Random.FRandRange(0.0f, 1.0f) < 0.4f;
			const FVector2D Sideways(-Direction.Y, Direction.X);
			Request.Control = (Request.Start + Request.End) * 0.5 + Sideways * (Random.FRandRange(-0.35f, 0.35f) * Length);

			// One road in three is a narrow path without sidewalks.
			const bool bPath = (Attempt % 3 == 0);
			Request.Profile.ProfileId = bPath ? TEXT("TestPath") : TEXT("TestStreet");
			Request.Profile.CarriagewayWidthCm = bPath ? 400.0f : 700.0f;
			Request.Profile.SidewalkWidthCm = bPath ? 0.0f : 200.0f;

			FChronoRoadGraph Scratch = Graph;
			Scratch.ClearChanges();
			int32 SegmentId = INDEX_NONE;
			if (ChronoRoadBuilder::ApplyAndValidate(Scratch, Heightfield, Rules, Request, SegmentId) != EChronoRoadBuildResult::Success)
			{
				continue;
			}

			Graph = MoveTemp(Scratch);
			const FChronoRoadChangeSet Changes = ChronoRoadBuilder::TakeChanges(Graph);
			ChronoRoadBuilder::ConformTerrain(Graph, Changes, Rules, Heightfield);
			Zoning.ApplyRoadChanges(Graph, Heightfield, Changes, ZoningRules);
			Buildings.RemoveInvalid(Zoning, Zoning.TakeDirtySegments());
			Buildings.RefreshHeights(Heightfield, FBox2D(FVector2D(-30000.0, -30000.0), FVector2D(30000.0, 30000.0)), BuildingRules);
			Last = Request.End;
			++NumBuilt;

			// The city grows between two roads, so later roads cut through buildings.
			Zoning.PaintZone(FVector2D::ZeroVector, 40000.0f, EChronoZoneType::Residential);
			Zoning.TakeDirtySegments();
			FRandomStream Growth(Seed * 31 + Attempt);
			Buildings.Grow(Zoning, Heightfield, Graph, Definitions, BuildingRules, Growth, 6);
		}

		TArray<FChronoAuditIssue> Issues;
		const FChronoAuditSettings AuditSettings;
		ChronoClippingAudit::Run(Heightfield, Graph, Rules, AuditSettings, Issues);
		ChronoClippingAudit::RunZoning(Graph, Zoning, ZoningRules, AuditSettings, Issues);
		ChronoClippingAudit::RunBuildings(Heightfield, Graph, Zoning, Buildings, BuildingRules, AuditSettings, Issues);

		AddInfo(FString::Printf(TEXT("Seed %d: %d roads accepted, %d segments, %d cells, %d buildings, %d issues"), Seed, NumBuilt, Graph.GetSegments().Num(), Zoning.GetNumCells(), Buildings.GetNumBuildings(), Issues.Num()));
		for (const FChronoAuditIssue& Issue : Issues)
		{
			AddError(FString::Printf(TEXT("Seed %d: [%s] %s at %s"), Seed, *Issue.Rule.ToString(), *Issue.Description, *Issue.Location.ToCompactString()));
		}

		TestTrue(TEXT("Random roads build a real network"), NumBuilt >= 20);
		TestEqual(TEXT("Audit of a random city reports nothing"), Issues.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoAuditDetectsClippingTest, "Chronopolis.Audit.DetectsClipping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoAuditDetectsClippingTest::RunTest(const FString& Parameters)
{
	// An audit that never fails is worthless: break the city on purpose and
	// check that each kind of damage is reported.
	auto HasRule = [](const TArray<FChronoAuditIssue>& Issues, const FName& Rule)
	{
		return Issues.ContainsByPredicate([&Rule](const FChronoAuditIssue& Issue) { return Issue.Rule == Rule; });
	};

	// Ground pushed up through a road.
	{
		ChronoAuditTests::FCity City;
		City.BuildGrid();
		City.Heightfield.ApplyRaiseBrush(FVector2D(0.0, -15000.0), 1500.0f, 200.0f, -5000.0f, 30000.0f);

		TArray<FChronoAuditIssue> Issues;
		ChronoClippingAudit::Run(City.Heightfield, City.Graph, City.Rules, FChronoAuditSettings(), Issues);
		TestTrue(TEXT("Raised ground is reported as terrain through a road"), HasRule(Issues, ChronoAuditRule::TerrainThroughRoad));
	}

	// Ground dug away under a road.
	{
		ChronoAuditTests::FCity City;
		City.BuildGrid();
		City.Heightfield.ApplyRaiseBrush(FVector2D(0.0, -15000.0), 1500.0f, -500.0f, -5000.0f, 30000.0f);

		TArray<FChronoAuditIssue> Issues;
		ChronoClippingAudit::Run(City.Heightfield, City.Graph, City.Rules, FChronoAuditSettings(), Issues);
		TestTrue(TEXT("Dug ground is reported as a floating road"), HasRule(Issues, ChronoAuditRule::RoadFloating));
	}

	// Two roads forced on top of each other, bypassing the placement rules.
	{
		ChronoAuditTests::FCity City;
		FChronoRoadProfileData Profile;
		Profile.CarriagewayWidthCm = 700.0f;

		const int32 A = City.Graph.AddNode(FVector(-5000.0, 0.0, 0.0));
		const int32 B = City.Graph.AddNode(FVector(5000.0, 0.0, 0.0));
		const int32 C = City.Graph.AddNode(FVector(-5000.0, 300.0, 0.0));
		const int32 D = City.Graph.AddNode(FVector(5000.0, 300.0, 0.0));
		City.Graph.AddSegment(A, B, FChronoBezier2D::MakeLine(FVector2D(-5000.0, 0.0), FVector2D(5000.0, 0.0)), Profile);
		City.Graph.AddSegment(C, D, FChronoBezier2D::MakeLine(FVector2D(-5000.0, 300.0), FVector2D(5000.0, 300.0)), Profile);

		TArray<FChronoAuditIssue> Issues;
		ChronoClippingAudit::Run(City.Heightfield, City.Graph, City.Rules, FChronoAuditSettings(), Issues);
		TestTrue(TEXT("Overlapping roads are reported"), HasRule(Issues, ChronoAuditRule::RoadOverlap));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
