// Tests that run on the project's real data (Config/DefaultGame.ini, turned
// into C++ by ini_to_cpp.py). Only built outside the engine: in the engine,
// the same checks are Chronopolis.Civ.ProjectData.

#include "ChronoBudget.h"
#include "ChronoBuildingSet.h"
#include "ChronoClippingAudit.h"
#include "ChronoHeightfield.h"
#include "ChronoPopulation.h"
#include "ChronoProgression.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "ChronoZoneGrid.h"
#include "Misc/AutomationTest.h"
#include "ChronoIniData.inc"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoProjectDataTest, "Chronopolis.Data.ProjectDataIsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoProjectDataTest::RunTest(const FString& Parameters)
{
	FChronoCivData Data;
	TArray<FChronoBuildingDefData> Definitions;
	ChronoLoadIniData(Data, Definitions);

	TArray<FString> Errors;
	Data.Validate(Errors);
	Data.ValidateBuildings(Definitions, Errors);
	for (const FString& Error : Errors)
	{
		AddError(Error);
	}

	int32 NumCivics = 0;
	for (const FChronoTechDef& Tech : Data.Techs)
	{
		NumCivics += Tech.bCivic ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d eras, %d technologies, %d civics, %d civilizations, %d building definitions"),
		Data.Eras.Num(), Data.Techs.Num() - NumCivics, NumCivics, Data.Civilizations.Num(), Definitions.Num()));
	TestEqual(TEXT("Seven eras"), Data.Eras.Num(), 7);
	TestEqual(TEXT("Three civilizations"), Data.Civilizations.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoErasPlaythroughTest, "Chronopolis.Data.CitiesGrowThroughTheEras",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoErasPlaythroughTest::RunTest(const FString& Parameters)
{
	FChronoCivData Data;
	TArray<FChronoBuildingDefData> Definitions;
	ChronoLoadIniData(Data, Definitions);

	const int32 Years = 6;
	for (const FChronoCivilizationDef& Civilization : Data.Civilizations)
	{
		// The same town for every civilization: a street grid, zones painted like a player would.
		FChronoHeightfield Heightfield;
		Heightfield.Init(256, 200.0f, 0.0f);
		Heightfield.GenerateNoise(5, 200.0f, 30000.0f, -5000.0f, 30000.0f);
		FChronoRoadGraph Graph;
		FChronoRoadRules RoadRules;
		RoadRules.ApplyTo(Graph);
		FChronoZoneGrid Zoning;
		FChronoZoningRules ZoningRules;
		auto Build = [&](const FVector2D& Start, const FVector2D& End)
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
				ChronoRoadBuilder::ConformTerrain(Graph, Changes, RoadRules, Heightfield);
				Zoning.ApplyRoadChanges(Graph, Heightfield, Changes, ZoningRules);
			}
		};
		const double Spacing = 10000.0;
		for (int32 Line = 0; Line < 6; ++Line)
		{
			Build(FVector2D(-28000.0, -25000.0 + Line * Spacing), FVector2D(28000.0, -25000.0 + Line * Spacing));
		}
		for (int32 Column = 0; Column < 6; ++Column)
		{
			for (int32 Line = 0; Line + 1 < 6; ++Line)
			{
				const double X = -25000.0 + Column * Spacing;
				Build(FVector2D(X, -25000.0 + Line * Spacing), FVector2D(X, -25000.0 + (Line + 1) * Spacing));
			}
		}
		Zoning.PaintZone(FVector2D(0.0, 0.0), 60000.0f, EChronoZoneType::Agricultural);
		Zoning.PaintZone(FVector2D(0.0, 0.0), 22000.0f, EChronoZoneType::Residential);
		Zoning.PaintZone(FVector2D(-20000.0, 0.0), 9000.0f, EChronoZoneType::Commercial);
		Zoning.PaintZone(FVector2D(20000.0, 0.0), 9000.0f, EChronoZoneType::Industrial);
		Zoning.PaintZone(FVector2D(0.0, 15000.0), 6000.0f, EChronoZoneType::Civic);
		Zoning.PaintZone(FVector2D(0.0, -15000.0), 5000.0f, EChronoZoneType::Commercial);
		Zoning.TakeDirtySegments();

		FChronoBuildingSet Buildings;
		FChronoBuildingRules BuildingRules;
		FChronoPopulation Population;
		FChronoPopulationRules PopulationRules;
		FChronoCityConditions Conditions;
		FChronoBudget Budget;
		FChronoEconomyRules EconomyRules;
		FChronoProgression Progression;
		Progression.Start(Civilization.Id, Data);

		FChronoBuildingAvailability Availability;
		TArray<FChronoBuildingDefData> Active;
		auto Refresh = [&]()
		{
			Availability.bRestricted = true;
			Availability.Civilization = Progression.GetCivilization();
			Availability.Era = Progression.GetEra();
			Availability.Unlocked = Progression.GetUnlocked();
			Availability.Filter(Definitions, Active);
			Buildings.SetStyle(Availability.Civilization, Availability.Era);
		};
		Refresh();

		// The player places one of each service as soon as the city knows it, on civic land.
		TArray<int32> CellIds;
		Zoning.GetCells().GetKeys(CellIds);
		CellIds.Sort();
		TSet<FName> Placed;
		auto PlaceServices = [&]()
		{
			for (const FChronoBuildingDefData& Definition : Active)
			{
				if (Definition.Service == EChronoServiceType::None || Placed.Contains(Definition.DefId))
				{
					continue;
				}
				for (const int32 CellId : CellIds)
				{
					const FChronoZoneCell* Cell = Zoning.FindCell(CellId);
					if (Cell && Cell->Zone == EChronoZoneType::Civic && Cell->Row == 0
						&& Buildings.PlaceBuilding(Zoning, Heightfield, Graph, CellId, Definition, BuildingRules) != INDEX_NONE)
					{
						Placed.Add(Definition.DefId);
						break;
					}
				}
			}
		};

		const double RoadArea = ChronoEconomy::ComputeRoadAreaM2(Graph);
		int32 EraDays[8] = { 0, -1, -1, -1, -1, -1, -1, -1 };
		int32 NumBoosts = 0;
		int32 NumRenewed = 0;
		FString Error;
		bool bConsistent = true;
		TArray<FChronoProgressEvent> Events;

		for (int64 Day = 0; Day < Years * 365; ++Day)
		{
			Population.StepDay(Day, Buildings, Conditions, PopulationRules);
			Budget.StepDay(Population.GetStats(), RoadArea, Buildings.GetTotalUpkeepPerDay(), EconomyRules);

			FChronoCityFacts Facts;
			Facts.Population = Population.GetStats().Population;
			for (const TPair<int32, FChronoBuilding>& Pair : Buildings.GetBuildings())
			{
				++Facts.BuildingCounts.FindOrAdd(Pair.Value.DefId);
				for (const FChronoBuildingDefData& Definition : Definitions)
				{
					if (Definition.DefId == Pair.Value.DefId && Definition.Replaces != NAME_None)
					{
						++Facts.BuildingCounts.FindOrAdd(Definition.Replaces);
					}
				}
			}

			Events.Reset();
			const double Culture = Budget.GetCulturePerDay() + Buildings.CountHeritage() * Data.HeritageCulturePerDay;
			Progression.StepDay(Budget.GetSciencePerDay(), Culture, Facts, Data, Events);
			bool bChanged = false;
			for (const FChronoProgressEvent& Event : Events)
			{
				NumBoosts += Event.Type == EChronoProgressEventType::Boosted ? 1 : 0;
				if (Event.Type == EChronoProgressEventType::EraReached)
				{
					EraDays[Event.Era] = static_cast<int32>(Day);
				}
				bChanged = bChanged || Event.Type != EChronoProgressEventType::Boosted;
			}
			if (bChanged)
			{
				Refresh();
			}
			PlaceServices();
			bConsistent = bConsistent && Progression.CheckInvariants(Data, Error);

			// Two growth steps a day, as in the game (one every five ticks of ten a day).
			for (int32 Step = 0; Step < 2; ++Step)
			{
				FRandomStream Random(4242 ^ static_cast<int32>(Day * 2 + Step) ^ (Buildings.GetNumBuildings() * 7919));
				Buildings.Grow(Zoning, Heightfield, Graph, Active, BuildingRules, Random, 2, &Population.GetDemand());
				Buildings.UpgradeBuildings(BuildingRules, Random, 1);
				NumRenewed += Buildings.RenewBuildings(Zoning, Heightfield, Graph, Active, BuildingRules, Random, 1);
			}

			// The player lists the first civic building as heritage once the city has one.
			if (Day == 365)
			{
				for (const TPair<int32, FChronoBuilding>& Pair : Buildings.GetBuildings())
				{
					if (Pair.Value.Zone == EChronoZoneType::Civic && !Pair.Value.bPlayerPlaced)
					{
						Buildings.SetHeritage(Pair.Key, true);
						break;
					}
				}
			}
		}

		// What stands in the city at the end.
		TMap<FName, int32> Counts;
		int32 NumOwnStyle = 0;
		bool bOnlyAvailableOrOld = true;
		for (const TPair<int32, FChronoBuilding>& Pair : Buildings.GetBuildings())
		{
			++Counts.FindOrAdd(Pair.Value.DefId);
			const FChronoBuildingDefData* Definition = Definitions.FindByPredicate([&Pair](const FChronoBuildingDefData& Def) { return Def.DefId == Pair.Value.DefId; });
			NumOwnStyle += (Definition && Definition->Civilization == Civilization.Id) ? 1 : 0;
			// Nobody builds another civilization's buildings.
			bOnlyAvailableOrOld = bOnlyAvailableOrOld && Definition && (Definition->Civilization == NAME_None || Definition->Civilization == Civilization.Id);
		}
		FString Kinds;
		TArray<FName> Names;
		Counts.GetKeys(Names);
		for (const FName& Name : Names)
		{
			Kinds += FString::Printf(TEXT("%s %d, "), *Name.ToString(), Counts.FindChecked(Name));
		}
		FString EraText;
		for (int32 Era = 1; Era < 8; ++Era)
		{
			if (EraDays[Era] >= 0)
			{
				EraText += FString::Printf(TEXT("era %d on day %d, "), Era, EraDays[Era]);
			}
		}

		TArray<FChronoAuditIssue> Issues;
		FChronoAuditSettings AuditSettings;
		ChronoClippingAudit::RunBuildings(Heightfield, Graph, Zoning, Buildings, BuildingRules, AuditSettings, Issues);

		AddInfo(FString::Printf(TEXT("%s after %d years: %d inhabitants, era %d (%s), %d known, %d boosts, %d buildings (%d of its own style, %d rebuilt, %d heritage), science %.1f and culture %.1f a day"),
			*Civilization.Id.ToString(), Years, Population.GetStats().Population, Progression.GetEra(), *EraText, Progression.GetNumKnown(), NumBoosts,
			Buildings.GetNumBuildings(), NumOwnStyle, NumRenewed, Buildings.CountHeritage(), Budget.GetSciencePerDay(), Budget.GetCulturePerDay()));
		AddInfo(FString::Printf(TEXT("  %s"), *Kinds));

		const FString Who = Civilization.Id.ToString();
		TestTrue(*(Who + TEXT(": the trees stay consistent")), bConsistent);
		TestTrue(*(Who + TEXT(": the city leaves the first era")), Progression.GetEra() >= 1);
		TestTrue(*(Who + TEXT(": its own buildings appear")), NumOwnStyle > 0);
		TestTrue(*(Who + TEXT(": only its own civilization's buildings")), bOnlyAvailableOrOld);
		TestTrue(*(Who + TEXT(": the old city is rebuilt in the new era")), NumRenewed > 0);
		TestEqual(*(Who + TEXT(": the audit finds nothing")), Issues.Num(), 0);
		for (int32 Index = 0; Index < Issues.Num() && Index < 3; ++Index)
		{
			AddError(Issues[Index].Description);
		}
	}
	return true;
}
