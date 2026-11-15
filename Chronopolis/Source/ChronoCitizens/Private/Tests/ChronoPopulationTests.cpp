// Automation tests for the population simulation. Filter: "Chronopolis.Population".

#include "ChronoBuildingSet.h"
#include "ChronoHeightfield.h"
#include "ChronoPopulation.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "ChronoZoneGrid.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoPopulationTests
{
	/**
	 * A small town where buildings only grow when the population asks for
	 * them: the same loop as in the game, without a world.
	 */
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
		FChronoPopulation Population;
		FChronoPopulationRules PopulationRules;
		FChronoCityConditions Conditions;
		int64 Day = 0;

		FTown()
		{
			Heightfield.Init(256, 200.0f, 0.0f);
			RoadRules.ApplyTo(Graph);

			// A street grid: five streets each way, 100 m apart.
			const double Lines[5] = { -20000.0, -10000.0, 0.0, 10000.0, 20000.0 };
			for (const double Y : Lines)
			{
				Build(FVector2D(-24000.0, Y), FVector2D(24000.0, Y));
			}
			for (const double X : Lines)
			{
				for (int32 Row = 0; Row + 1 < 5; ++Row)
				{
					Build(FVector2D(X, Lines[Row]), FVector2D(X, Lines[Row + 1]));
				}
			}

			// Housing in the middle, workplaces all around it, with room to spare:
			// the test is about demand, not about running out of land.
			Zoning.PaintZone(FVector2D(0.0, 0.0), 60000.0f, EChronoZoneType::Agricultural);
			Zoning.PaintZone(FVector2D(-17000.0, 0.0), 10000.0f, EChronoZoneType::Commercial);
			Zoning.PaintZone(FVector2D(17000.0, 0.0), 10000.0f, EChronoZoneType::Industrial);
			Zoning.PaintZone(FVector2D(0.0, -17000.0), 5000.0f, EChronoZoneType::Civic);
			Zoning.PaintZone(FVector2D(0.0, 0.0), 11000.0f, EChronoZoneType::Residential);
			Zoning.TakeDirtySegments();

			const EChronoZoneType Zones[5] = { EChronoZoneType::Residential, EChronoZoneType::Commercial, EChronoZoneType::Industrial, EChronoZoneType::Agricultural, EChronoZoneType::Civic };
			for (const EChronoZoneType Zone : Zones)
			{
				FChronoBuildingDefData Definition;
				Definition.DefId = TEXT("TestBuilding");
				Definition.Zone = Zone;
				Definition.MaxFloors = 3;
				Definitions.Add(Definition);
			}
		}

		void Build(const FVector2D& Start, const FVector2D& End)
		{
			FChronoRoadBuildRequest Request;
			Request.Start = Start;
			Request.End = End;
			Request.Profile.CarriagewayWidthCm = 700.0f;
			Request.Profile.SidewalkWidthCm = 200.0f;

			FChronoRoadGraph Scratch = Graph;
			Scratch.ClearChanges();
			int32 SegmentId = INDEX_NONE;
			if (ChronoRoadBuilder::ApplyAndValidate(Scratch, Heightfield, RoadRules, Request, SegmentId) == EChronoRoadBuildResult::Success)
			{
				Graph = MoveTemp(Scratch);
				const FChronoRoadChangeSet Changes = ChronoRoadBuilder::TakeChanges(Graph);
				Zoning.ApplyRoadChanges(Graph, Heightfield, Changes, ZoningRules);
			}
		}

		/** One day: the population lives, then buildings grow where it asks for them. */
		void StepDays(int32 NumDays)
		{
			for (int32 Index = 0; Index < NumDays; ++Index)
			{
				Population.StepDay(Day, Buildings, Conditions, PopulationRules);

				FRandomStream Random(4242 ^ static_cast<int32>(Day));
				Buildings.Grow(Zoning, Heightfield, Graph, Definitions, BuildingRules, Random, 2, &Population.GetDemand());
				++Day;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoPopulationGrowthTest, "Chronopolis.Population.CityFillsAndBalances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoPopulationGrowthTest::RunTest(const FString& Parameters)
{
	ChronoPopulationTests::FTown Town;

	// An empty map wants homes first: nobody lives there yet, so no workplace is needed.
	Town.Population.StepDay(0, Town.Buildings, Town.Conditions, Town.PopulationRules);
	TestTrue(TEXT("An empty city wants housing"), Town.Population.GetDemand().Get(EChronoZoneType::Residential) > 0.0f);
	TestTrue(TEXT("An empty city does not want industry yet"), Town.Population.GetDemand().Get(EChronoZoneType::Industrial) <= 0.0f);

	// Three years of city life.
	Town.StepDays(3 * 365);
	const FChronoPopulationStats& Stats = Town.Population.GetStats();
	AddInfo(FString::Printf(TEXT("After 3 years: %d inhabitants in %d households, %d of %d homes free, %d employed, %d unemployed, %d of %d jobs free, happiness %.0f, %d buildings"),
		Stats.Population, Stats.Households, Stats.FreeHousing, Stats.HousingUnits, Stats.Employed, Stats.Unemployed, Stats.FreeJobs, Stats.Jobs, Stats.AverageHappiness, Town.Buildings.GetNumBuildings()));

	TestTrue(TEXT("People moved in"), Stats.Population >= 200);
	TestTrue(TEXT("Workplaces were built for them"), Stats.Jobs > 0 && Stats.Employed > 0);
	TestTrue(TEXT("Every kind of workplace exists"),
		Stats.JobsByZone[static_cast<int32>(EChronoZoneType::Commercial)] > 0
		&& Stats.JobsByZone[static_cast<int32>(EChronoZoneType::Industrial)] > 0
		&& Stats.JobsByZone[static_cast<int32>(EChronoZoneType::Agricultural)] > 0);

	// Demand keeps supply close to needs: neither mass unemployment nor rows of empty homes.
	const float UnemploymentRate = Stats.WorkingAge > 0 ? static_cast<float>(Stats.Unemployed) / Stats.WorkingAge : 0.0f;
	const float HousingVacancy = Stats.HousingUnits > 0 ? static_cast<float>(Stats.FreeHousing) / Stats.HousingUnits : 0.0f;
	TestTrue(TEXT("Unemployment stays moderate"), UnemploymentRate < 0.25f);
	TestTrue(TEXT("Few homes stand empty"), HousingVacancy < 0.25f);
	TestTrue(TEXT("People are reasonably happy"), Stats.AverageHappiness > 40.0f);

	FString Error;
	TestTrue(TEXT("Population data is coherent"), Town.Population.CheckInvariants(Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	// Demolition: when buildings disappear, their people move or leave, and the data stays coherent.
	Town.Zoning.PaintZone(FVector2D(0.0, 0.0), 6000.0f, EChronoZoneType::None);
	Town.Buildings.RemoveInvalid(Town.Zoning, Town.Zoning.TakeDirtySegments());
	Town.StepDays(1);
	TestTrue(TEXT("Population data is coherent after demolition"), Town.Population.CheckInvariants(Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoPopulationDeterminismTest, "Chronopolis.Population.SameSeedSameHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoPopulationDeterminismTest::RunTest(const FString& Parameters)
{
	// Two towns with the same inputs live the same history.
	ChronoPopulationTests::FTown A;
	ChronoPopulationTests::FTown B;
	A.StepDays(400);
	B.StepDays(400);
	TestEqual(TEXT("Same population"), A.Population.GetStats().Population, B.Population.GetStats().Population);
	TestEqual(TEXT("Same employment"), A.Population.GetStats().Employed, B.Population.GetStats().Employed);
	TestEqual(TEXT("Same buildings"), A.Buildings.GetNumBuildings(), B.Buildings.GetNumBuildings());

	// A game saved and loaded continues exactly as the game that was never interrupted.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Population is saved"), A.Population.Serialize(Writer));

	FChronoPopulation Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Population is loaded"), Loaded.Serialize(Reader));
	Loaded.Refresh(A.Buildings, A.Conditions, A.PopulationRules);
	TestEqual(TEXT("Loaded population has the same size"), Loaded.GetStats().Population, A.Population.GetStats().Population);

	A.Population = Loaded;
	A.StepDays(200);
	B.StepDays(200);
	TestEqual(TEXT("Same population after loading"), A.Population.GetStats().Population, B.Population.GetStats().Population);
	TestEqual(TEXT("Same employment after loading"), A.Population.GetStats().Employed, B.Population.GetStats().Employed);
	TestEqual(TEXT("Same buildings after loading"), A.Buildings.GetNumBuildings(), B.Buildings.GetNumBuildings());

	// Damaged data is refused, and leaves an empty population rather than a broken one.
	TArray<uint8> Damaged = Bytes;
	Damaged.SetNum(Bytes.Num() / 2);
	FChronoPopulation Broken;
	FMemoryReader DamagedReader(Damaged);
	TestFalse(TEXT("Truncated data is refused"), Broken.Serialize(DamagedReader));
	TestEqual(TEXT("A refused load leaves nobody"), Broken.GetCitizens().Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoPopulationTaxTest, "Chronopolis.Population.HighTaxesDriveDemandDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoPopulationTaxTest::RunTest(const FString& Parameters)
{
	ChronoPopulationTests::FTown Fair;
	ChronoPopulationTests::FTown Greedy;
	Greedy.Conditions.TaxRate[static_cast<int32>(EChronoZoneType::Residential)] = 0.25f;

	Fair.StepDays(2 * 365);
	Greedy.StepDays(2 * 365);

	AddInfo(FString::Printf(TEXT("Tax 9 %%: %d inhabitants, happiness %.0f. Tax 25 %%: %d inhabitants, happiness %.0f"),
		Fair.Population.GetStats().Population, Fair.Population.GetStats().AverageHappiness,
		Greedy.Population.GetStats().Population, Greedy.Population.GetStats().AverageHappiness));

	TestTrue(TEXT("High taxes make people less happy"), Greedy.Population.GetStats().AverageHappiness < Fair.Population.GetStats().AverageHappiness);
	TestTrue(TEXT("High taxes keep the city smaller"), Greedy.Population.GetStats().Population < Fair.Population.GetStats().Population);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
