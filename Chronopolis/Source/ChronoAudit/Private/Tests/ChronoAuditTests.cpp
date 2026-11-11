// Zero-clipping audit run headless on a generated city. This is the test that
// continuous integration runs: it builds terrain and roads through the same
// rules as the game, then requires the audit to find nothing.
// Filter: "Chronopolis.Audit".

#include "ChronoClippingAudit.h"
#include "ChronoHeightfield.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoAuditTests
{
	struct FCity
	{
		FChronoHeightfield Heightfield;
		FChronoRoadGraph Graph;
		FChronoRoadRules Rules;
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
	}
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

	// A refused road must leave no trace: an overlapping road is rejected and
	// the audit is still clean afterwards.
	const int32 SegmentsBefore = City.Graph.GetSegments().Num();
	const EChronoRoadBuildResult Overlap = City.Build(FVector2D(-19000.0, -4000.0), FVector2D(19000.0, -4000.0));
	TestTrue(TEXT("A road laid along another one is refused"), Overlap != EChronoRoadBuildResult::Success);
	TestEqual(TEXT("A refused road changes nothing"), City.Graph.GetSegments().Num(), SegmentsBefore);

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
