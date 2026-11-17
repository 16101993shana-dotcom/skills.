// Automation tests for the civilization layer: data checks, the trees, eras,
// boosts, saving, what a city can build, and buildings renewed in the style of
// a new era. Filter: "Chronopolis.Civ".

#include "ChronoBuildingSet.h"
#include "ChronoClippingAudit.h"
#include "ChronoHeightfield.h"
#include "ChronoProgression.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "ChronoZoneGrid.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoCivTests
{
	FChronoTechDef MakeTech(const TCHAR* Id, bool bCivic, int32 Era, float Cost, TArray<FName> Prerequisites = {}, TArray<FName> Unlocks = {})
	{
		FChronoTechDef Tech;
		Tech.Id = FName(Id);
		Tech.bCivic = bCivic;
		Tech.Era = Era;
		Tech.Cost = Cost;
		Tech.Prerequisites = Prerequisites;
		Tech.Unlocks = Unlocks;
		return Tech;
	}

	/** Three eras, a small tree in each, two civilizations. */
	FChronoCivData MakeData()
	{
		FChronoCivData Data;
		const TCHAR* EraIds[] = { TEXT("EraA"), TEXT("EraB"), TEXT("EraC") };
		const int32 Populations[] = { 0, 100, 300 };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FChronoEraDef Era;
			Era.Id = FName(EraIds[Index]);
			Era.MinPopulation = Populations[Index];
			Era.TechsToAdvance = Index < 2 ? 2 : 0;
			Era.CivicsToAdvance = Index < 2 ? 1 : 0;
			Data.Eras.Add(Era);
		}

		Data.Techs.Add(MakeTech(TEXT("A1"), false, 0, 10.0f, {}, { FName("HouseB") }));
		Data.Techs.Add(MakeTech(TEXT("A2"), false, 0, 20.0f));
		Data.Techs.Add(MakeTech(TEXT("A3"), false, 0, 30.0f, { FName("A1") }));
		Data.Techs.Add(MakeTech(TEXT("CA1"), true, 0, 10.0f));
		Data.Techs.Add(MakeTech(TEXT("B1"), false, 1, 40.0f, { FName("A1") }));
		Data.Techs.Add(MakeTech(TEXT("B2"), false, 1, 40.0f, { FName("A2"), FName("CA1") }));
		Data.Techs.Add(MakeTech(TEXT("CB1"), true, 1, 30.0f, { FName("CA1") }));
		Data.Techs.Add(MakeTech(TEXT("C1"), false, 2, 80.0f, { FName("B1") }));

		FChronoCivilizationDef Fast;
		Fast.Id = FName("Fast");
		Fast.ScienceMultiplier = 2.0f;
		Data.Civilizations.Add(Fast);

		FChronoCivilizationDef Plain;
		Plain.Id = FName("Plain");
		Data.Civilizations.Add(Plain);
		return Data;
	}

	/** Runs days until a condition holds or the limit is reached. Returns the number of days run. */
	template <typename FCondition>
	int32 RunDays(FChronoProgression& Progression, const FChronoCivData& Data, double Science, double Culture, const FChronoCityFacts& Facts, int32 MaxDays, TArray<FChronoProgressEvent>& OutEvents, FCondition Condition)
	{
		int32 Day = 0;
		for (; Day < MaxDays && !Condition(); ++Day)
		{
			Progression.StepDay(Science, Culture, Facts, Data, OutEvents);
		}
		return Day;
	}

	int32 CountEvents(const TArray<FChronoProgressEvent>& Events, EChronoProgressEventType Type)
	{
		int32 Count = 0;
		for (const FChronoProgressEvent& Event : Events)
		{
			Count += Event.Type == Type ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoCivDataCheckTest, "Chronopolis.Civ.Data.ValidationCatchesMistakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoCivDataCheckTest::RunTest(const FString& Parameters)
{
	using namespace ChronoCivTests;

	TArray<FString> Errors;
	TestTrue(TEXT("The test data is valid"), MakeData().Validate(Errors));

	FChronoCivData Duplicate = MakeData();
	Duplicate.Techs.Add(MakeTech(TEXT("A1"), false, 0, 5.0f));
	TestFalse(TEXT("A duplicate id is refused"), Duplicate.Validate(Errors));

	FChronoCivData Missing = MakeData();
	Missing.Techs.Add(MakeTech(TEXT("X"), false, 1, 5.0f, { FName("Nowhere") }));
	TestFalse(TEXT("An unknown prerequisite is refused"), Missing.Validate(Errors));

	FChronoCivData Later = MakeData();
	Later.Techs[0].Prerequisites.Add(FName("B1"));
	TestFalse(TEXT("A prerequisite from a later era is refused"), Later.Validate(Errors));

	FChronoCivData Cycle = MakeData();
	Cycle.Techs[0].Prerequisites.Add(FName("A3"));
	TestFalse(TEXT("A cycle of prerequisites is refused"), Cycle.Validate(Errors));

	FChronoCivData Stuck = MakeData();
	Stuck.Eras[1].TechsToAdvance = 5;
	TestFalse(TEXT("An era that cannot be left is refused"), Stuck.Validate(Errors));

	// Buildings against the trees.
	FChronoCivData Data = MakeData();
	TArray<FChronoBuildingDefData> Definitions;
	const EChronoZoneType Zones[] = { EChronoZoneType::Residential, EChronoZoneType::Commercial, EChronoZoneType::Industrial, EChronoZoneType::Agricultural, EChronoZoneType::Civic };
	for (const EChronoZoneType Zone : Zones)
	{
		FChronoBuildingDefData Definition;
		Definition.DefId = FName(*FString::Printf(TEXT("Basic%d"), static_cast<int32>(Zone)));
		Definition.Zone = Zone;
		Definitions.Add(Definition);
	}
	FChronoBuildingDefData HouseB;
	HouseB.DefId = FName("HouseB");
	HouseB.RequiredUnlock = FName("HouseB");
	Definitions.Add(HouseB);
	TestTrue(TEXT("Buildings and trees agree"), Data.ValidateBuildings(Definitions, Errors));

	TArray<FChronoBuildingDefData> Orphan = Definitions;
	Orphan.Last().RequiredUnlock = FName("NothingUnlocksThis");
	Errors.Reset();
	TestFalse(TEXT("A building nothing unlocks is refused"), Data.ValidateBuildings(Orphan, Errors));

	TArray<FChronoBuildingDefData> NoFarm = Definitions;
	NoFarm.RemoveAt(3);
	Errors.Reset();
	TestFalse(TEXT("A zone with nothing to grow is refused"), Data.ValidateBuildings(NoFarm, Errors));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoCivTreesTest, "Chronopolis.Civ.Progression.TreesBoostsAndEras",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoCivTreesTest::RunTest(const FString& Parameters)
{
	using namespace ChronoCivTests;
	const FChronoCivData Data = MakeData();
	FString Error;

	FChronoProgression City;
	City.Start(FName("Plain"), Data);
	TestEqual(TEXT("A city starts in the first era"), City.GetEra(), 0);
	TestFalse(TEXT("Nothing is known at the start"), City.IsKnown(FName("A1")));
	TestFalse(TEXT("A later era's entry cannot be chosen"), City.SetCurrent(FName("B1"), Data));
	TestFalse(TEXT("An entry without its prerequisite cannot be chosen"), City.SetCurrent(FName("A3"), Data));
	TestTrue(TEXT("An available entry can be chosen"), City.SetCurrent(FName("A2"), Data));

	// One day of 25 science: A2 (20) is finished and the 5 left go to the cheapest next one (A1, 10).
	FChronoCityFacts Facts;
	Facts.Population = 50;
	TArray<FChronoProgressEvent> Events;
	City.StepDay(25.0, 0.0, Facts, Data, Events);
	TestTrue(TEXT("The chosen technology is researched"), City.IsKnown(FName("A2")));
	TestEqual(TEXT("The leftover goes to the next one"), City.GetProgress(FName("A1")), 5.0);
	TestTrue(TEXT("The next one is picked by itself"), City.GetCurrentTech() == FName("A1"));
	TestTrue(TEXT("What a technology unlocks is not known before it"), !City.IsUnlocked(FName("HouseB")));

	// Several entries in one day, and the unlocks follow.
	City.StepDay(100.0, 100.0, Facts, Data, Events);
	TestTrue(TEXT("A1 and A3 are researched"), City.IsKnown(FName("A1")) && City.IsKnown(FName("A3")));
	TestTrue(TEXT("A known technology unlocks its buildings"), City.IsUnlocked(FName("HouseB")) && City.IsUnlocked(FName("A1")));
	TestTrue(TEXT("The civic tree moves with culture"), City.IsKnown(FName("CA1")));

	// The trees of the era are known, but the city is too small for the next one.
	TestTrue(TEXT("The trees are ready for the next era"), City.HasTreesForNextEra(Data));
	TestEqual(TEXT("Too few inhabitants: still the first era"), City.GetEra(), 0);
	TestFalse(TEXT("Next era's entries wait for the era"), City.IsAvailable(FName("B1"), Data));

	// With nothing left in this era, science is kept for the next one.
	City.StepDay(7.0, 3.0, Facts, Data, Events);
	Facts.Population = 120;
	Events.Reset();
	City.StepDay(0.0, 0.0, Facts, Data, Events);
	TestEqual(TEXT("Enough inhabitants: the second era begins"), City.GetEra(), 1);
	TestEqual(TEXT("One era event"), CountEvents(Events, EChronoProgressEventType::EraReached), 1);
	TestTrue(TEXT("The state is consistent"), City.CheckInvariants(Data, Error));

	// Boosts: a city that has the building gets 40 % of the cost at once, only once.
	FChronoCivData Boosting = Data;
	Boosting.Techs[4].BoostBuilding = FName("Forge");
	Boosting.Techs[4].BoostBuildingCount = 2;
	FChronoProgression Boosted;
	Boosted.Start(FName("Plain"), Boosting);
	FChronoCityFacts WithForges;
	WithForges.BuildingCounts.Add(FName("Forge"), 1);
	Events.Reset();
	Boosted.StepDay(0.0, 0.0, WithForges, Boosting, Events);
	TestEqual(TEXT("One forge is not enough"), Boosted.GetProgress(FName("B1")), 0.0);
	WithForges.BuildingCounts.Add(FName("Forge"), 2);
	Boosted.StepDay(0.0, 0.0, WithForges, Boosting, Events);
	Boosted.StepDay(0.0, 0.0, WithForges, Boosting, Events);
	TestTrue(TEXT("Two forges give 40 % of B1, once"), FMath::IsNearlyEqual(Boosted.GetProgress(FName("B1")), 16.0, 0.001));
	TestEqual(TEXT("One boost event"), CountEvents(Events, EChronoProgressEventType::Boosted), 1);

	// A civilization's multiplier: twice the science, the same trees in half the days.
	FChronoProgression Fast;
	Fast.Start(FName("Fast"), Data);
	FChronoProgression Plain;
	Plain.Start(FName("Plain"), Data);
	const int32 FastDays = RunDays(Fast, Data, 1.0, 1.0, Facts, 1000, Events, [&Fast]() { return Fast.IsKnown(FName("A3")); });
	const int32 PlainDays = RunDays(Plain, Data, 1.0, 1.0, Facts, 1000, Events, [&Plain]() { return Plain.IsKnown(FName("A3")); });
	AddInfo(FString::Printf(TEXT("A1, A2 and A3 in %d days with the science bonus, %d without"), FastDays, PlainDays));
	TestTrue(TEXT("Twice the science, about half the time"), FastDays * 2 <= PlainDays + 2 && FastDays * 2 >= PlainDays - 2);

	// A whole game: the city reaches the last era and knows everything, never breaking the rules.
	FChronoProgression Whole;
	Whole.Start(FName("Plain"), Data);
	FChronoCityFacts Big;
	Big.Population = 1000;
	bool bAlwaysConsistent = true;
	for (int32 Day = 0; Day < 400; ++Day)
	{
		Whole.StepDay(1.0, 0.5, Big, Data, Events);
		bAlwaysConsistent = bAlwaysConsistent && Whole.CheckInvariants(Data, Error);
	}
	TestTrue(TEXT("Consistent every day"), bAlwaysConsistent);
	TestEqual(TEXT("The last era is reached"), Whole.GetEra(), 2);
	TestEqual(TEXT("Everything is known"), Whole.GetNumKnown(), Data.Techs.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoCivSaveTest, "Chronopolis.Civ.Progression.SaveAndLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoCivSaveTest::RunTest(const FString& Parameters)
{
	using namespace ChronoCivTests;
	const FChronoCivData Data = MakeData();

	FChronoProgression City;
	City.Start(FName("Fast"), Data);
	FChronoCityFacts Facts;
	Facts.Population = 150;
	TArray<FChronoProgressEvent> Events;
	for (int32 Day = 0; Day < 30; ++Day)
	{
		City.StepDay(1.3, 0.7, Facts, Data, Events);
	}
	City.SetCurrent(FName("B2"), Data);

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Saved"), City.Serialize(Writer, Data));

	FChronoProgression Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Loaded"), Loaded.Serialize(Reader, Data));
	TestEqual(TEXT("Same era"), Loaded.GetEra(), City.GetEra());
	TestEqual(TEXT("Same number known"), Loaded.GetNumKnown(), City.GetNumKnown());
	TestTrue(TEXT("Same civilization and choice"), Loaded.GetCivilization() == City.GetCivilization() && Loaded.GetCurrentTech() == City.GetCurrentTech());
	TestTrue(TEXT("Same progress"), FMath::IsNearlyEqual(Loaded.GetProgress(FName("B2")), City.GetProgress(FName("B2")), 1.0e-9));

	// Saving the loaded state gives the same bytes.
	TArray<uint8> Again;
	FMemoryWriter AgainWriter(Again);
	Loaded.Serialize(AgainWriter, Data);
	TestTrue(TEXT("Same bytes"), Again == Bytes);

	// A tree that lost an entry since the save: the save still loads, without it.
	FChronoCivData Changed = Data;
	Changed.Techs.RemoveAt(Changed.Techs.Num() - 1);
	FChronoProgression Migrated;
	FMemoryReader ChangedReader(Bytes);
	TestTrue(TEXT("A save survives a change of the trees"), Migrated.Serialize(ChangedReader, Changed));

	// Damaged data is refused.
	TArray<uint8> Damaged = Bytes;
	Damaged.SetNum(Damaged.Num() / 2);
	FChronoProgression Refused;
	FMemoryReader DamagedReader(Damaged);
	TestFalse(TEXT("A cut file is refused"), Refused.Serialize(DamagedReader, Data));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoCivBuildingsTest, "Chronopolis.Civ.Buildings.AvailabilityRenewalAndHeritage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoCivBuildingsTest::RunTest(const FString& Parameters)
{
	// ---- What a city can build ----
	TArray<FChronoBuildingDefData> Definitions;
	auto Add = [&Definitions](const TCHAR* Id, int32 MinEra, int32 MaxEra, const TCHAR* Unlock, const TCHAR* Civilization, const TCHAR* Replaces) -> FChronoBuildingDefData&
	{
		FChronoBuildingDefData& Definition = Definitions.AddDefaulted_GetRef();
		Definition.DefId = FName(Id);
		Definition.Zone = EChronoZoneType::Residential;
		Definition.MinEra = MinEra;
		Definition.MaxEra = MaxEra;
		Definition.RequiredUnlock = Unlock ? FName(Unlock) : NAME_None;
		Definition.Civilization = Civilization ? FName(Civilization) : NAME_None;
		Definition.Replaces = Replaces ? FName(Replaces) : NAME_None;
		return Definition;
	};
	Add(TEXT("Hut"), 0, 0, nullptr, nullptr, nullptr);
	Add(TEXT("House"), 0, 99, nullptr, nullptr, nullptr);
	Add(TEXT("Block"), 1, 99, TEXT("Concrete"), nullptr, nullptr);
	Add(TEXT("CourtyardHouse"), 0, 0, nullptr, TEXT("CivA"), TEXT("House"));

	FChronoBuildingAvailability Open;
	TArray<FChronoBuildingDefData> Filtered;
	Open.Filter(Definitions, Filtered);
	TestEqual(TEXT("Unrestricted: everything but the civilizations' own buildings"), Filtered.Num(), 3);

	FChronoBuildingAvailability CivA;
	CivA.bRestricted = true;
	CivA.Civilization = FName("CivA");
	CivA.Filter(Definitions, Filtered);
	TestEqual(TEXT("Era 0, civilization A: hut and courtyard house"), Filtered.Num(), 2);
	TestTrue(TEXT("The civilization's house replaces the common one"), CivA.Resolve(FName("House"), Definitions) && CivA.Resolve(FName("House"), Definitions)->DefId == FName("CourtyardHouse"));

	FChronoBuildingAvailability CivB = CivA;
	CivB.Civilization = FName("CivB");
	TestTrue(TEXT("Another civilization keeps the common house"), CivB.Resolve(FName("House"), Definitions) && CivB.Resolve(FName("House"), Definitions)->DefId == FName("House"));

	CivA.Era = 1;
	CivA.Filter(Definitions, Filtered);
	TestEqual(TEXT("Era 1 without the unlock: only the common house"), Filtered.Num(), 1);
	CivA.Unlocked.Add(FName("Concrete"));
	CivA.Filter(Definitions, Filtered);
	TestEqual(TEXT("Era 1 with the unlock: house and block"), Filtered.Num(), 2);
	TestTrue(TEXT("A locked building cannot be resolved"), CivB.Resolve(FName("Block"), Definitions) == nullptr);

	// ---- Renewal and heritage, on a real town ----
	FChronoHeightfield Heightfield;
	Heightfield.Init(256, 200.0f, 0.0f);
	Heightfield.GenerateNoise(7, 150.0f, 30000.0f, -5000.0f, 30000.0f);
	FChronoRoadGraph Graph;
	FChronoRoadRules RoadRules;
	RoadRules.ApplyTo(Graph);
	FChronoZoneGrid Zoning;
	FChronoZoningRules ZoningRules;
	for (int32 Row = 0; Row < 3; ++Row)
	{
		FChronoRoadBuildRequest Request;
		Request.Start = FVector2D(-12000.0, -6000.0 + Row * 6000.0);
		Request.End = FVector2D(12000.0, -6000.0 + Row * 6000.0);
		Request.Profile.CarriagewayWidthCm = 700.0f;
		Request.Profile.SidewalkWidthCm = 200.0f;
		int32 SegmentId = INDEX_NONE;
		if (ChronoRoadBuilder::ApplyAndValidate(Graph, Heightfield, RoadRules, Request, SegmentId) == EChronoRoadBuildResult::Success)
		{
			const FChronoRoadChangeSet Changes = ChronoRoadBuilder::TakeChanges(Graph);
			ChronoRoadBuilder::ConformTerrain(Graph, Changes, RoadRules, Heightfield);
			Zoning.ApplyRoadChanges(Graph, Heightfield, Changes, ZoningRules);
		}
	}
	Zoning.PaintZone(FVector2D(0.0, 0.0), 30000.0f, EChronoZoneType::Residential);
	Zoning.TakeDirtySegments();

	FChronoBuildingSet Buildings;
	FChronoBuildingRules BuildingRules;
	FChronoBuildingAvailability EraZero;
	EraZero.bRestricted = true;
	EraZero.Civilization = FName("CivA");
	EraZero.Filter(Definitions, Filtered);
	Buildings.SetStyle(EraZero.Civilization, 0);
	for (int32 Step = 0; Step < 200; ++Step)
	{
		FRandomStream Random(50 + Step);
		Buildings.Grow(Zoning, Heightfield, Graph, Filtered, BuildingRules, Random, 4);
	}
	const int32 NumBuilt = Buildings.GetNumBuildings();
	TestTrue(TEXT("The town grew"), NumBuilt >= 40);
	TestEqual(TEXT("Nothing is outdated in its own era"), Buildings.CountOutdated(), 0);

	// The player lists three buildings as heritage.
	TArray<int32> Ids;
	Buildings.GetBuildings().GetKeys(Ids);
	Ids.Sort();
	TArray<FChronoBuilding> Listed;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Buildings.SetHeritage(Ids[Index * 5], true);
		Listed.Add(*Buildings.FindBuilding(Ids[Index * 5]));
	}
	TestEqual(TEXT("Three heritage buildings"), Buildings.CountHeritage(), 3);

	// A new era: the old town is rebuilt little by little, heritage excepted.
	FChronoBuildingAvailability EraOne = EraZero;
	EraOne.Era = 1;
	EraOne.Unlocked.Add(FName("Concrete"));
	EraOne.Filter(Definitions, Filtered);
	Buildings.SetStyle(EraOne.Civilization, 1);
	TestEqual(TEXT("Every building but heritage is outdated"), Buildings.CountOutdated(), NumBuilt - 3);

	int32 Renewed = 0;
	for (int32 Step = 0; Step < 400 && Buildings.CountOutdated() > 0; ++Step)
	{
		FRandomStream Random(900 + Step);
		Renewed += Buildings.RenewBuildings(Zoning, Heightfield, Graph, Filtered, BuildingRules, Random, 2);
	}
	AddInfo(FString::Printf(TEXT("%d buildings in era 0, %d rebuilt, %d still outdated, %d now"), NumBuilt, Renewed, Buildings.CountOutdated(), Buildings.GetNumBuildings()));
	TestTrue(TEXT("Most of the town was rebuilt"), Renewed >= (NumBuilt - 3) * 8 / 10);

	bool bHeritageKept = true;
	for (const FChronoBuilding& Before : Listed)
	{
		const FChronoBuilding* After = Buildings.FindBuilding(Before.Id);
		bHeritageKept = bHeritageKept && After && After->Era == 0 && After->DefId == Before.DefId && After->Footprint.Center == Before.Footprint.Center;
	}
	TestTrue(TEXT("Heritage buildings are untouched"), bHeritageKept);

	bool bNoHut = true;
	bool bNewStyle = true;
	for (const TPair<int32, FChronoBuilding>& Pair : Buildings.GetBuildings())
	{
		if (!Pair.Value.bHeritage && Pair.Value.Era == 1)
		{
			bNoHut = bNoHut && Pair.Value.DefId != FName("Hut") && Pair.Value.DefId != FName("CourtyardHouse");
		}
		bNewStyle = bNewStyle && (Pair.Value.bHeritage || Pair.Value.Era == 1 || Pair.Value.Era == 0);
	}
	TestTrue(TEXT("Rebuilt buildings are of the new era"), bNoHut && bNewStyle);

	// Zero clipping after the renewal.
	TArray<FChronoAuditIssue> Issues;
	FChronoAuditSettings AuditSettings;
	ChronoClippingAudit::RunBuildings(Heightfield, Graph, Zoning, Buildings, BuildingRules, AuditSettings, Issues);
	TestEqual(TEXT("The audit finds nothing after the renewal"), Issues.Num(), 0);
	for (int32 Index = 0; Index < Issues.Num() && Index < 5; ++Index)
	{
		AddError(Issues[Index].Description);
	}

	// Era, civilization and heritage survive a save.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Buildings.Serialize(Writer);
	FChronoBuildingSet Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Loaded"), Loaded.Serialize(Reader));
	TestEqual(TEXT("Same heritage"), Loaded.CountHeritage(), 3);
	TestTrue(TEXT("Same style"), Loaded.GetEra() == 1 && Loaded.GetCivilization() == FName("CivA"));
	TestEqual(TEXT("Same outdated count"), Loaded.CountOutdated(), Buildings.CountOutdated());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
