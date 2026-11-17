#include "ChronoProgression.h"

namespace ChronoProgressionPrivate
{
	/** Names as saved: NAME_None is written as an empty string. */
	FString NameToSave(FName Name)
	{
		return Name == NAME_None ? FString() : Name.ToString();
	}

	FName NameFromSave(const FString& Text)
	{
		return (Text.IsEmpty() || Text == TEXT("None")) ? NAME_None : FName(*Text);
	}

	/** Passes made over a tree in one day: a day can pay for several cheap entries. */
	const int32 MaxEntriesPerDay = 64;
}

FChronoCivData FChronoCivData::FromSettings()
{
	const UChronoCivSettings* Settings = UChronoCivSettings::Get();

	FChronoCivData Data;
	Data.Eras = Settings->Eras;
	Data.Techs = Settings->Techs;
	Data.Civilizations = Settings->Civilizations;
	Data.BoostFraction = Settings->BoostFraction;
	Data.HeritageCulturePerDay = Settings->HeritageCulturePerDay;
	return Data;
}

const FChronoTechDef* FChronoCivData::FindTech(FName Id) const
{
	return Techs.FindByPredicate([Id](const FChronoTechDef& Tech) { return Tech.Id == Id; });
}

const FChronoCivilizationDef* FChronoCivData::FindCivilization(FName Id) const
{
	return Civilizations.FindByPredicate([Id](const FChronoCivilizationDef& Civilization) { return Civilization.Id == Id; });
}

bool FChronoCivData::Validate(TArray<FString>& OutErrors) const
{
	OutErrors.Reset();

	// ---- Eras ----
	if (Eras.Num() == 0)
	{
		OutErrors.Add(TEXT("There is no era"));
	}
	TSet<FName> EraIds;
	for (int32 Index = 0; Index < Eras.Num(); ++Index)
	{
		const FChronoEraDef& EraDef = Eras[Index];
		if (EraDef.Id == NAME_None || EraIds.Contains(EraDef.Id))
		{
			OutErrors.Add(FString::Printf(TEXT("Era %d has no id or the id of another era"), Index));
		}
		EraIds.Add(EraDef.Id);
		if (Index >= 2 && EraDef.MinPopulation < Eras[Index - 1].MinPopulation)
		{
			OutErrors.Add(FString::Printf(TEXT("Era %d needs fewer inhabitants than the era before it"), Index));
		}
	}

	// ---- Technologies and civics ----
	TMap<FName, int32> IndexById;
	for (int32 Index = 0; Index < Techs.Num(); ++Index)
	{
		const FChronoTechDef& Tech = Techs[Index];
		if (Tech.Id == NAME_None || IndexById.Contains(Tech.Id))
		{
			OutErrors.Add(FString::Printf(TEXT("Entry %d of the trees has no id or the id of another entry"), Index));
			continue;
		}
		IndexById.Add(Tech.Id, Index);
	}

	for (const FChronoTechDef& Tech : Techs)
	{
		const FString Name = Tech.Id.ToString();
		if (Tech.Era < 0 || Tech.Era >= Eras.Num())
		{
			OutErrors.Add(FString::Printf(TEXT("%s belongs to era %d, which does not exist"), *Name, Tech.Era));
		}
		if (!(Tech.Cost > 0.0f))
		{
			OutErrors.Add(FString::Printf(TEXT("%s costs nothing"), *Name));
		}
		for (const FName& Prerequisite : Tech.Prerequisites)
		{
			const int32* PrerequisiteIndex = IndexById.Find(Prerequisite);
			if (!PrerequisiteIndex)
			{
				OutErrors.Add(FString::Printf(TEXT("%s needs %s, which does not exist"), *Name, *Prerequisite.ToString()));
			}
			else if (Techs[*PrerequisiteIndex].Era > Tech.Era)
			{
				OutErrors.Add(FString::Printf(TEXT("%s needs %s, which belongs to a later era"), *Name, *Prerequisite.ToString()));
			}
		}
		if (Tech.BoostBuilding != NAME_None && Tech.BoostBuildingCount < 1)
		{
			OutErrors.Add(FString::Printf(TEXT("%s has a building boost that needs no building"), *Name));
		}
	}

	// Every era can be left: it has enough technologies and civics of its own.
	for (int32 EraIndex = 0; EraIndex + 1 < Eras.Num(); ++EraIndex)
	{
		int32 NumTechs = 0;
		int32 NumCivics = 0;
		for (const FChronoTechDef& Tech : Techs)
		{
			if (Tech.Era == EraIndex)
			{
				++(Tech.bCivic ? NumCivics : NumTechs);
			}
		}
		if (NumTechs < Eras[EraIndex].TechsToAdvance || NumCivics < Eras[EraIndex].CivicsToAdvance)
		{
			OutErrors.Add(FString::Printf(TEXT("Era %d asks for %d technologies and %d civics but has %d and %d"),
				EraIndex, Eras[EraIndex].TechsToAdvance, Eras[EraIndex].CivicsToAdvance, NumTechs, NumCivics));
		}
	}

	// Everything can be learned: a city that learns whatever is available
	// and moves on whenever it may ends up knowing every entry, in the last
	// era. This also rules out cycles of prerequisites.
	if (OutErrors.Num() == 0 && Eras.Num() > 0)
	{
		TSet<FName> Learned;
		int32 Era = 0;
		for (bool bChanged = true; bChanged;)
		{
			bChanged = false;
			for (const FChronoTechDef& Tech : Techs)
			{
				if (Learned.Contains(Tech.Id) || Tech.Era > Era)
				{
					continue;
				}
				bool bReady = true;
				for (const FName& Prerequisite : Tech.Prerequisites)
				{
					bReady = bReady && Learned.Contains(Prerequisite);
				}
				if (bReady)
				{
					Learned.Add(Tech.Id);
					bChanged = true;
				}
			}
			if (Era + 1 < Eras.Num())
			{
				int32 NumTechs = 0;
				int32 NumCivics = 0;
				for (const FChronoTechDef& Tech : Techs)
				{
					if (Tech.Era == Era && Learned.Contains(Tech.Id))
					{
						++(Tech.bCivic ? NumCivics : NumTechs);
					}
				}
				if (NumTechs >= Eras[Era].TechsToAdvance && NumCivics >= Eras[Era].CivicsToAdvance)
				{
					++Era;
					bChanged = true;
				}
			}
		}
		if (Learned.Num() != Techs.Num() || Era != Eras.Num() - 1)
		{
			OutErrors.Add(FString::Printf(TEXT("The trees cannot be completed: %d of %d entries can be learned, era %d of %d can be reached (a prerequisite cycle, or an era that cannot be left)"),
				Learned.Num(), Techs.Num(), Era, Eras.Num() - 1));
		}
	}

	// ---- Civilizations ----
	if (Civilizations.Num() == 0)
	{
		OutErrors.Add(TEXT("There is no civilization"));
	}
	TSet<FName> CivilizationIds;
	for (const FChronoCivilizationDef& Civilization : Civilizations)
	{
		if (Civilization.Id == NAME_None || CivilizationIds.Contains(Civilization.Id))
		{
			OutErrors.Add(TEXT("A civilization has no id or the id of another one"));
		}
		CivilizationIds.Add(Civilization.Id);
		if (!(Civilization.ScienceMultiplier > 0.0f) || !(Civilization.CultureMultiplier > 0.0f) || !(Civilization.ConstructionCostMultiplier > 0.0f) || !(Civilization.WaterRadiusMultiplier > 0.0f))
		{
			OutErrors.Add(FString::Printf(TEXT("%s has a multiplier at or below zero"), *Civilization.Id.ToString()));
		}
	}

	if (BoostFraction < 0.0f || BoostFraction > 1.0f)
	{
		OutErrors.Add(TEXT("The boost fraction must be between 0 and 1"));
	}

	return OutErrors.Num() == 0;
}

bool FChronoCivData::ValidateBuildings(TConstArrayView<FChronoBuildingDefData> Definitions, TArray<FString>& OutErrors) const
{
	const int32 NumErrorsBefore = OutErrors.Num();

	// Every name some technology, civic or civilization unlocks.
	TSet<FName> Unlockable;
	for (const FChronoTechDef& Tech : Techs)
	{
		Unlockable.Add(Tech.Id);
		Unlockable.Append(Tech.Unlocks);
	}
	for (const FChronoCivilizationDef& Civilization : Civilizations)
	{
		Unlockable.Append(Civilization.StartingUnlocks);
	}

	TSet<FName> DefinitionIds;
	TSet<FName> Required;
	for (const FChronoBuildingDefData& Definition : Definitions)
	{
		const FString Name = Definition.DefId.ToString();
		if (DefinitionIds.Contains(Definition.DefId))
		{
			OutErrors.Add(FString::Printf(TEXT("Building %s is defined twice"), *Name));
		}
		DefinitionIds.Add(Definition.DefId);
		Required.Add(Definition.RequiredUnlock);

		if (Definition.Civilization != NAME_None && !FindCivilization(Definition.Civilization))
		{
			OutErrors.Add(FString::Printf(TEXT("Building %s belongs to %s, which is not a civilization"), *Name, *Definition.Civilization.ToString()));
		}
		if (Definition.RequiredUnlock != NAME_None && !Unlockable.Contains(Definition.RequiredUnlock))
		{
			OutErrors.Add(FString::Printf(TEXT("Building %s needs %s, which nothing unlocks"), *Name, *Definition.RequiredUnlock.ToString()));
		}
		if (Definition.MinEra > Definition.MaxEra || Definition.MinEra >= Eras.Num())
		{
			OutErrors.Add(FString::Printf(TEXT("Building %s has no era in which it can be built"), *Name));
		}
		if (Definition.Replaces != NAME_None)
		{
			const FChronoBuildingDefData* Replaced = Definitions.FindByPredicate([&Definition](const FChronoBuildingDefData& Other) { return Other.DefId == Definition.Replaces; });
			if (!Replaced || Replaced->Civilization != NAME_None || Definition.Civilization == NAME_None)
			{
				OutErrors.Add(FString::Printf(TEXT("Building %s must belong to a civilization and replace a building of every civilization"), *Name));
			}
			else if (Replaced->Zone != Definition.Zone || Replaced->Service != Definition.Service)
			{
				OutErrors.Add(FString::Printf(TEXT("Building %s replaces %s, which has another zone or service"), *Name, *Definition.Replaces.ToString()));
			}
		}
	}

	for (const FChronoTechDef& Tech : Techs)
	{
		for (const FName& Unlock : Tech.Unlocks)
		{
			if (!Required.Contains(Unlock))
			{
				OutErrors.Add(FString::Printf(TEXT("%s unlocks %s, which no building needs"), *Tech.Id.ToString(), *Unlock.ToString()));
			}
		}
		if (Tech.BoostBuilding != NAME_None && !DefinitionIds.Contains(Tech.BoostBuilding))
		{
			OutErrors.Add(FString::Printf(TEXT("%s is boosted by %s, which is not a building"), *Tech.Id.ToString(), *Tech.BoostBuilding.ToString()));
		}
	}

	// Every civilization can grow something on every kind of zone in every
	// era, once the trees of that era are known.
	const EChronoZoneType GrownZones[] = { EChronoZoneType::Residential, EChronoZoneType::Commercial, EChronoZoneType::Industrial, EChronoZoneType::Agricultural, EChronoZoneType::Civic };
	for (const FChronoCivilizationDef& Civilization : Civilizations)
	{
		FChronoBuildingAvailability Availability;
		Availability.bRestricted = true;
		Availability.Civilization = Civilization.Id;
		Availability.Unlocked.Append(Civilization.StartingUnlocks);
		for (int32 EraIndex = 0; EraIndex < Eras.Num(); ++EraIndex)
		{
			Availability.Era = EraIndex;
			for (const FChronoTechDef& Tech : Techs)
			{
				if (Tech.Era == EraIndex)
				{
					Availability.Unlocked.Add(Tech.Id);
					Availability.Unlocked.Append(Tech.Unlocks);
				}
			}
			for (const EChronoZoneType Zone : GrownZones)
			{
				bool bSomething = false;
				for (const FChronoBuildingDefData& Definition : Definitions)
				{
					bSomething = bSomething || (Definition.Zone == Zone && Definition.Service == EChronoServiceType::None && Definition.Weight > 0.0f && Availability.Allows(Definition, Definitions));
				}
				if (!bSomething)
				{
					OutErrors.Add(FString::Printf(TEXT("%s cannot grow anything on zone %d in era %d"), *Civilization.Id.ToString(), static_cast<int32>(Zone), EraIndex));
				}
			}
		}
	}

	return OutErrors.Num() == NumErrorsBefore;
}

void FChronoProgression::Start(FName CivilizationId, const FChronoCivData& Data)
{
	Civilization = CivilizationId;
	Era = 0;
	Known.Reset();
	Boosted.Reset();
	Progress.Reset();
	CurrentTech = NAME_None;
	CurrentCivic = NAME_None;
	BankedScience = 0.0;
	BankedCulture = 0.0;
	RebuildUnlocked(Data);
}

void FChronoProgression::RebuildUnlocked(const FChronoCivData& Data)
{
	Unlocked.Reset();
	if (const FChronoCivilizationDef* CivilizationDef = Data.FindCivilization(Civilization))
	{
		Unlocked.Append(CivilizationDef->StartingUnlocks);
	}
	for (const FChronoTechDef& Tech : Data.Techs)
	{
		if (Known.Contains(Tech.Id))
		{
			Unlocked.Add(Tech.Id);
			Unlocked.Append(Tech.Unlocks);
		}
	}
}

bool FChronoProgression::IsAvailable(FName TechId, const FChronoCivData& Data) const
{
	const FChronoTechDef* Tech = Data.FindTech(TechId);
	if (!Tech || Known.Contains(TechId) || Tech->Era > Era)
	{
		return false;
	}
	for (const FName& Prerequisite : Tech->Prerequisites)
	{
		if (!Known.Contains(Prerequisite))
		{
			return false;
		}
	}
	return true;
}

bool FChronoProgression::SetCurrent(FName TechId, const FChronoCivData& Data)
{
	if (!IsAvailable(TechId, Data))
	{
		return false;
	}
	(Data.FindTech(TechId)->bCivic ? CurrentCivic : CurrentTech) = TechId;
	return true;
}

double FChronoProgression::GetProgress(FName TechId) const
{
	const double* Value = Progress.Find(TechId);
	return Value ? *Value : 0.0;
}

FName FChronoProgression::PickCheapest(bool bCivic, const FChronoCivData& Data) const
{
	// Cheapest first, then the order of the data: always the same choice.
	FName Best = NAME_None;
	double BestRemaining = 0.0;
	for (const FChronoTechDef& Tech : Data.Techs)
	{
		if (Tech.bCivic != bCivic || !IsAvailable(Tech.Id, Data))
		{
			continue;
		}
		const double Remaining = Tech.Cost - GetProgress(Tech.Id);
		if (Best == NAME_None || Remaining < BestRemaining)
		{
			Best = Tech.Id;
			BestRemaining = Remaining;
		}
	}
	return Best;
}

void FChronoProgression::Learn(const FChronoTechDef& Tech, TArray<FChronoProgressEvent>& OutEvents)
{
	Known.Add(Tech.Id);
	Progress.Remove(Tech.Id);
	Unlocked.Add(Tech.Id);
	Unlocked.Append(Tech.Unlocks);

	FChronoProgressEvent& Event = OutEvents.AddDefaulted_GetRef();
	Event.Type = Tech.bCivic ? EChronoProgressEventType::CivicAdopted : EChronoProgressEventType::TechResearched;
	Event.Id = Tech.Id;
	Event.Era = Era;
}

void FChronoProgression::Spend(bool bCivic, double Points, const FChronoCivData& Data, TArray<FChronoProgressEvent>& OutEvents)
{
	FName& Current = bCivic ? CurrentCivic : CurrentTech;
	double& Banked = bCivic ? BankedCulture : BankedScience;
	Points += Banked;
	Banked = 0.0;

	for (int32 Pass = 0; Pass < ChronoProgressionPrivate::MaxEntriesPerDay && Points > 0.0; ++Pass)
	{
		if (Current == NAME_None || !IsAvailable(Current, Data))
		{
			Current = PickCheapest(bCivic, Data);
		}
		if (Current == NAME_None)
		{
			// Nothing left to study in this era: kept for the next one.
			Banked = Points;
			return;
		}

		const FChronoTechDef& Tech = *Data.FindTech(Current);
		double& Done = Progress.FindOrAdd(Current);
		Done += Points;
		if (Done < Tech.Cost)
		{
			return;
		}

		Points = Done - Tech.Cost;
		Current = NAME_None;
		Learn(Tech, OutEvents);
	}
	Banked += FMath::Max(Points, 0.0);
}

int32 FChronoProgression::CountKnownOfEra(int32 InEra, bool bCivic, const FChronoCivData& Data) const
{
	int32 Count = 0;
	for (const FChronoTechDef& Tech : Data.Techs)
	{
		Count += (Tech.Era == InEra && Tech.bCivic == bCivic && Known.Contains(Tech.Id)) ? 1 : 0;
	}
	return Count;
}

bool FChronoProgression::HasTreesForNextEra(const FChronoCivData& Data) const
{
	if (Era + 1 >= Data.Eras.Num())
	{
		return false;
	}
	const FChronoEraDef& Current = Data.Eras[Era];
	return CountKnownOfEra(Era, false, Data) >= Current.TechsToAdvance && CountKnownOfEra(Era, true, Data) >= Current.CivicsToAdvance;
}

void FChronoProgression::StepDay(double Science, double Culture, const FChronoCityFacts& Facts, const FChronoCivData& Data, TArray<FChronoProgressEvent>& OutEvents)
{
	if (Data.Eras.Num() == 0)
	{
		return;
	}

	const FChronoCivilizationDef* CivilizationDef = Data.FindCivilization(Civilization);
	const double ScienceMultiplier = CivilizationDef ? CivilizationDef->ScienceMultiplier : 1.0;
	const double CultureMultiplier = CivilizationDef ? CivilizationDef->CultureMultiplier : 1.0;

	// 1. Boosts: what the city has done gives a head start, once per entry.
	for (const FChronoTechDef& Tech : Data.Techs)
	{
		if (Known.Contains(Tech.Id) || Boosted.Contains(Tech.Id))
		{
			continue;
		}

		bool bEarned = Tech.BoostPopulation > 0 && Facts.Population >= Tech.BoostPopulation;
		if (!bEarned && Tech.BoostBuilding != NAME_None)
		{
			const int32* Count = Facts.BuildingCounts.Find(Tech.BoostBuilding);
			bEarned = Count && *Count >= Tech.BoostBuildingCount;
		}
		if (bEarned)
		{
			Boosted.Add(Tech.Id);
			double& Done = Progress.FindOrAdd(Tech.Id);
			Done = FMath::Min(Done + Data.BoostFraction * Tech.Cost, static_cast<double>(Tech.Cost));

			FChronoProgressEvent& Event = OutEvents.AddDefaulted_GetRef();
			Event.Type = EChronoProgressEventType::Boosted;
			Event.Id = Tech.Id;
			Event.Era = Era;
		}
	}

	// 2. The day's science and culture.
	Spend(false, FMath::Max(Science, 0.0) * ScienceMultiplier, Data, OutEvents);
	Spend(true, FMath::Max(Culture, 0.0) * CultureMultiplier, Data, OutEvents);

	// 3. A new era: the trees of this one are known well enough, and the city is big enough.
	while (HasTreesForNextEra(Data) && Facts.Population >= Data.Eras[Era + 1].MinPopulation)
	{
		++Era;
		FChronoProgressEvent& Event = OutEvents.AddDefaulted_GetRef();
		Event.Type = EChronoProgressEventType::EraReached;
		Event.Id = Data.Eras[Era].Id;
		Event.Era = Era;
	}
}

bool FChronoProgression::CheckInvariants(const FChronoCivData& Data, FString& OutError) const
{
	if (Era < 0 || (Data.Eras.Num() > 0 && Era >= Data.Eras.Num()))
	{
		OutError = FString::Printf(TEXT("Era %d does not exist"), Era);
		return false;
	}
	for (const FChronoTechDef& Tech : Data.Techs)
	{
		if (!Known.Contains(Tech.Id))
		{
			continue;
		}
		if (Tech.Era > Era)
		{
			OutError = FString::Printf(TEXT("%s is known but belongs to era %d, after the city's era %d"), *Tech.Id.ToString(), Tech.Era, Era);
			return false;
		}
		for (const FName& Prerequisite : Tech.Prerequisites)
		{
			if (!Known.Contains(Prerequisite))
			{
				OutError = FString::Printf(TEXT("%s is known without its prerequisite %s"), *Tech.Id.ToString(), *Prerequisite.ToString());
				return false;
			}
		}
	}
	for (int32 Earlier = 0; Earlier < Era; ++Earlier)
	{
		if (CountKnownOfEra(Earlier, false, Data) < Data.Eras[Earlier].TechsToAdvance || CountKnownOfEra(Earlier, true, Data) < Data.Eras[Earlier].CivicsToAdvance)
		{
			OutError = FString::Printf(TEXT("Era %d was left without knowing enough of it"), Earlier);
			return false;
		}
	}
	if (BankedScience < 0.0 || BankedCulture < 0.0)
	{
		OutError = TEXT("Negative science or culture in reserve");
		return false;
	}
	return true;
}

bool FChronoProgression::Serialize(FArchive& Ar, const FChronoCivData& Data)
{
	using namespace ChronoProgressionPrivate;

	const int32 CurrentVersion = 1;
	const int32 MaxEntries = 100000;

	int32 Version = CurrentVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != CurrentVersion)
	{
		Ar.SetError();
		return false;
	}

	FString CivilizationText = NameToSave(Civilization);
	FString CurrentTechText = NameToSave(CurrentTech);
	FString CurrentCivicText = NameToSave(CurrentCivic);
	int32 SavedEra = Era;
	double SavedScience = BankedScience;
	double SavedCulture = BankedCulture;
	Ar << CivilizationText;
	Ar << SavedEra;
	Ar << CurrentTechText;
	Ar << CurrentCivicText;
	Ar << SavedScience;
	Ar << SavedCulture;

	// Entries in the order of the data, so that the same state always gives the same bytes.
	TArray<FString> KnownTexts;
	TArray<FString> BoostedTexts;
	TArray<FString> ProgressTexts;
	TArray<double> ProgressValues;
	if (Ar.IsSaving())
	{
		for (const FChronoTechDef& Tech : Data.Techs)
		{
			if (Known.Contains(Tech.Id))
			{
				KnownTexts.Add(Tech.Id.ToString());
			}
			if (Boosted.Contains(Tech.Id))
			{
				BoostedTexts.Add(Tech.Id.ToString());
			}
			if (const double* Value = Progress.Find(Tech.Id))
			{
				ProgressTexts.Add(Tech.Id.ToString());
				ProgressValues.Add(*Value);
			}
		}
	}
	Ar << KnownTexts;
	Ar << BoostedTexts;
	Ar << ProgressTexts;
	Ar << ProgressValues;

	if (Ar.IsLoading())
	{
		const FName LoadedCivilization = NameFromSave(CivilizationText);
		bool bValid = !Ar.IsError()
			&& Data.FindCivilization(LoadedCivilization) != nullptr
			&& SavedEra >= 0 && SavedEra < FMath::Max(Data.Eras.Num(), 1)
			&& SavedScience >= 0.0 && SavedCulture >= 0.0
			&& KnownTexts.Num() <= MaxEntries && BoostedTexts.Num() <= MaxEntries
			&& ProgressTexts.Num() == ProgressValues.Num() && ProgressTexts.Num() <= MaxEntries;

		Start(bValid ? LoadedCivilization : NAME_None, Data);
		if (!bValid)
		{
			Ar.SetError();
			return false;
		}

		// Names the data no longer has are dropped.
		Era = SavedEra;
		BankedScience = SavedScience;
		BankedCulture = SavedCulture;
		for (const FString& Text : KnownTexts)
		{
			const FName Id = NameFromSave(Text);
			if (Data.FindTech(Id))
			{
				Known.Add(Id);
			}
		}
		for (const FString& Text : BoostedTexts)
		{
			const FName Id = NameFromSave(Text);
			if (Data.FindTech(Id))
			{
				Boosted.Add(Id);
			}
		}
		for (int32 Index = 0; Index < ProgressTexts.Num(); ++Index)
		{
			const FName Id = NameFromSave(ProgressTexts[Index]);
			if (Data.FindTech(Id) && ProgressValues[Index] >= 0.0)
			{
				Progress.Add(Id, ProgressValues[Index]);
			}
		}
		CurrentTech = NameFromSave(CurrentTechText);
		CurrentCivic = NameFromSave(CurrentCivicText);
		RebuildUnlocked(Data);

		FString Error;
		if (!CheckInvariants(Data, Error))
		{
			Start(NAME_None, Data);
			Ar.SetError();
			return false;
		}
	}

	return !Ar.IsError();
}
