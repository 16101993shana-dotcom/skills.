#include "ChronoCivSubsystem.h"

#include "ChronoBuildingSubsystem.h"
#include "ChronoCore.h"
#include "ChronoEconomySubsystem.h"
#include "ChronoPopulation.h"
#include "ChronoPopulationSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "ChronoCiv"

namespace ChronoCivSubsystemPrivate
{
	UChronoCivSubsystem* GetCiv(UWorld* World)
	{
		return World ? World->GetSubsystem<UChronoCivSubsystem>() : nullptr;
	}

	void StatusFromConsole(UWorld* World)
	{
		if (const UChronoCivSubsystem* Civ = GetCiv(World))
		{
			Civ->LogStatus();
		}
	}

	void ResearchFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		UChronoCivSubsystem* Civ = GetCiv(World);
		if (!Civ || Args.Num() < 1)
		{
			return;
		}
		if (!Civ->SetResearch(FName(*Args[0])))
		{
			UE_LOG(LogChrono, Warning, TEXT("%s cannot be studied now (unknown, already known, of a later era, or a prerequisite is missing)"), *Args[0]);
		}
	}

	void ChooseFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		UChronoCivSubsystem* Civ = GetCiv(World);
		if (Civ && Args.Num() >= 1 && !Civ->ChooseCivilization(FName(*Args[0])))
		{
			UE_LOG(LogChrono, Warning, TEXT("Unknown civilization %s"), *Args[0]);
		}
	}

	FAutoConsoleCommandWithWorld StatusCommand(
		TEXT("Chrono.Civ.Status"),
		TEXT("Prints the civilization, the era, what is being studied and what is available."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&StatusFromConsole));

	FAutoConsoleCommandWithWorldAndArgs ResearchCommand(
		TEXT("Chrono.Civ.Research"),
		TEXT("Chrono.Civ.Research <id>: chooses the next technology or civic to study."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ResearchFromConsole));

	FAutoConsoleCommandWithWorldAndArgs ChooseCommand(
		TEXT("Chrono.Civ.Choose"),
		TEXT("Chrono.Civ.Choose <id>: founds the city anew as this civilization (start of a game)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ChooseFromConsole));
}

void UChronoCivSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Unlocks go to the buildings; science and culture come from the economy, once a day.
	UChronoBuildingSubsystem* Buildings = Collection.InitializeDependency<UChronoBuildingSubsystem>();
	UChronoPopulationSubsystem* Population = Collection.InitializeDependency<UChronoPopulationSubsystem>();
	Collection.InitializeDependency<UChronoEconomySubsystem>();
	Super::Initialize(Collection);

	Data = FChronoCivData::FromSettings();
	TArray<FString> Errors;
	if (!Data.Validate(Errors))
	{
		for (const FString& Error : Errors)
		{
			UE_LOG(LogChrono, Error, TEXT("Civilization data: %s"), *Error);
		}
	}

	FName Civilization = UChronoCivSettings::Get()->DefaultCivilization;
	if (!Data.FindCivilization(Civilization) && Data.Civilizations.Num() > 0)
	{
		Civilization = Data.Civilizations[0].Id;
	}
	Progression.Start(Civilization, Data);
	PushToBuildings(Buildings);

	if (Population)
	{
		DaySteppedHandle = Population->OnDayStepped.AddUObject(this, &UChronoCivSubsystem::HandleDayStepped);
	}
}

void UChronoCivSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		if (UChronoPopulationSubsystem* Population = World->GetSubsystem<UChronoPopulationSubsystem>())
		{
			Population->OnDayStepped.Remove(DaySteppedHandle);
		}
	}
	DaySteppedHandle.Reset();

	Super::Deinitialize();
}

bool UChronoCivSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UChronoCivSubsystem::ChooseCivilization(FName CivilizationId)
{
	if (!Data.FindCivilization(CivilizationId))
	{
		return false;
	}
	Progression.Start(CivilizationId, Data);
	PushToBuildings();
	UE_LOG(LogChrono, Display, TEXT("The city is founded as %s"), *GetEraDisplayText().ToString());
	return true;
}

FText UChronoCivSubsystem::GetEraDisplayText() const
{
	const FChronoCivilizationDef* Civilization = Data.FindCivilization(Progression.GetCivilization());
	const FText CivilizationName = Civilization ? Civilization->DisplayName : FText::FromName(Progression.GetCivilization());
	const FText EraName = Data.Eras.IsValidIndex(Progression.GetEra()) ? Data.Eras[Progression.GetEra()].DisplayName : FText::AsNumber(Progression.GetEra() + 1);
	return FText::Format(LOCTEXT("EraOfCivilization", "{0}, {1}"), CivilizationName, EraName);
}

bool UChronoCivSubsystem::SetResearch(FName TechId)
{
	return Progression.SetCurrent(TechId, Data);
}

TArray<FName> UChronoCivSubsystem::GetAvailable() const
{
	TArray<FName> Result;
	for (const FChronoTechDef& Tech : Data.Techs)
	{
		if (Progression.IsAvailable(Tech.Id, Data))
		{
			Result.Add(Tech.Id);
		}
	}
	return Result;
}

float UChronoCivSubsystem::GetProgressFraction(FName TechId) const
{
	if (Progression.IsKnown(TechId))
	{
		return 1.0f;
	}
	const FChronoTechDef* Tech = Data.FindTech(TechId);
	return Tech ? static_cast<float>(FMath::Clamp(Progression.GetProgress(TechId) / Tech->Cost, 0.0, 1.0)) : 0.0f;
}

void UChronoCivSubsystem::HandleDayStepped(int64 DayIndex, const FChronoPopulationStats& Stats)
{
	const UWorld* World = GetWorld();
	const UChronoBuildingSubsystem* Buildings = World ? World->GetSubsystem<UChronoBuildingSubsystem>() : nullptr;
	const UChronoEconomySubsystem* Economy = World ? World->GetSubsystem<UChronoEconomySubsystem>() : nullptr;
	if (!Buildings || !Economy)
	{
		return;
	}

	// What the city has: buildings count as their own kind and as the kind they replace.
	FChronoCityFacts Facts;
	Facts.Population = Stats.Population;
	for (const TPair<int32, FChronoBuilding>& Pair : Buildings->GetBuildingSet().GetBuildings())
	{
		++Facts.BuildingCounts.FindOrAdd(Pair.Value.DefId);
		const FChronoBuildingDefData* Definition = Buildings->FindDefinition(Pair.Value.DefId);
		if (Definition && Definition->Replaces != NAME_None)
		{
			++Facts.BuildingCounts.FindOrAdd(Definition->Replaces);
		}
	}

	// Heritage buildings add to the culture of the day.
	const double Culture = Economy->GetCulturePerDay() + Buildings->GetNumHeritage() * Data.HeritageCulturePerDay;

	TArray<FChronoProgressEvent> Events;
	Progression.StepDay(Economy->GetSciencePerDay(), Culture, Facts, Data, Events);

	bool bUnlocksChanged = false;
	for (const FChronoProgressEvent& Event : Events)
	{
		switch (Event.Type)
		{
		case EChronoProgressEventType::TechResearched:
			UE_LOG(LogChrono, Display, TEXT("Technology researched: %s"), *Event.Id.ToString());
			bUnlocksChanged = true;
			break;
		case EChronoProgressEventType::CivicAdopted:
			UE_LOG(LogChrono, Display, TEXT("Civic adopted: %s"), *Event.Id.ToString());
			bUnlocksChanged = true;
			break;
		case EChronoProgressEventType::Boosted:
			UE_LOG(LogChrono, Display, TEXT("Boost earned for %s"), *Event.Id.ToString());
			break;
		case EChronoProgressEventType::EraReached:
			UE_LOG(LogChrono, Display, TEXT("A new era begins: %s"), *GetEraDisplayText().ToString());
			bUnlocksChanged = true;
			break;
		default:
			break;
		}
		OnProgress.Broadcast(Event);
	}

	if (bUnlocksChanged)
	{
		PushToBuildings();
	}
}

void UChronoCivSubsystem::PushToBuildings(UChronoBuildingSubsystem* Buildings) const
{
	if (!Buildings)
	{
		Buildings = GetWorld() ? GetWorld()->GetSubsystem<UChronoBuildingSubsystem>() : nullptr;
	}
	if (!Buildings)
	{
		return;
	}

	FChronoBuildingAvailability Availability;
	Availability.bRestricted = true;
	Availability.Civilization = Progression.GetCivilization();
	Availability.Era = Progression.GetEra();
	Availability.Unlocked = Progression.GetUnlocked();
	Buildings->SetAvailability(Availability);

	const FChronoCivilizationDef* Civilization = Data.FindCivilization(Progression.GetCivilization());
	Buildings->SetCivilizationModifiers(Civilization ? Civilization->ConstructionCostMultiplier : 1.0f, Civilization ? Civilization->WaterRadiusMultiplier : 1.0f);
}

void UChronoCivSubsystem::LogStatus() const
{
	UE_LOG(LogChrono, Display, TEXT("%s: %d technologies and civics known"), *GetEraDisplayText().ToString(), Progression.GetNumKnown());
	UE_LOG(LogChrono, Display, TEXT("  Studying %s (%.0f %%) and %s (%.0f %%)"),
		*Progression.GetCurrentTech().ToString(), GetProgressFraction(Progression.GetCurrentTech()) * 100.0f,
		*Progression.GetCurrentCivic().ToString(), GetProgressFraction(Progression.GetCurrentCivic()) * 100.0f);
	const TArray<FName> Available = GetAvailable();
	for (const FName& Id : Available)
	{
		UE_LOG(LogChrono, Display, TEXT("  Available: %s"), *Id.ToString());
	}
	if (Progression.HasTreesForNextEra(Data))
	{
		UE_LOG(LogChrono, Display, TEXT("  Ready for the next era once the city has %d inhabitants"), Data.Eras[Progression.GetEra() + 1].MinPopulation);
	}
}

bool UChronoCivSubsystem::SerializeCiv(FArchive& Ar)
{
	const bool bResult = Progression.Serialize(Ar, Data);
	if (Ar.IsLoading() && bResult)
	{
		PushToBuildings();
	}
	return bResult;
}

#undef LOCTEXT_NAMESPACE
