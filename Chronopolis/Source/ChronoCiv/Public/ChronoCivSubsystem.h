// The civilization layer in the running game: the city's civilization and
// era, what it studies and what it knows. Once a day it takes the science and
// culture of the economy, moves the trees forward, and tells the building
// system what the city can now build and in which style.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoProgression.h"
#include "ChronoCivSubsystem.generated.h"

struct FChronoPopulationStats;
class UChronoBuildingSubsystem;

/** Fired for every technology, civic, boost and era of the day. */
DECLARE_MULTICAST_DELEGATE_OneParam(FChronoOnProgress, const FChronoProgressEvent&);

UCLASS()
class CHRONOCIV_API UChronoCivSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	const FChronoCivData& GetData() const { return Data; }
	const FChronoProgression& GetProgression() const { return Progression; }

	/**
	 * Founds the city anew as this civilization: first era, nothing known.
	 * Meant for the start of a game. Returns false for an unknown id.
	 */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Civilization")
	bool ChooseCivilization(FName CivilizationId);

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Civilization")
	FName GetCivilization() const { return Progression.GetCivilization(); }

	/** Index of the current era, 0 for the first. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Civilization")
	int32 GetEra() const { return Progression.GetEra(); }

	/** Name shown to the player, for example "Sumer, Middle Ages". */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Civilization")
	FText GetEraDisplayText() const;

	/** Chooses the next technology or civic to study. False if it is not available now. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Civilization")
	bool SetResearch(FName TechId);

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Civilization")
	bool IsKnown(FName TechId) const { return Progression.IsKnown(TechId); }

	/** Technologies and civics that can be studied now, in the order of the data. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Civilization")
	TArray<FName> GetAvailable() const;

	/** Share (0 to 1) of a technology or civic already paid for. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Civilization")
	float GetProgressFraction(FName TechId) const;

	void LogStatus() const;

	/** Save and load. Returns false on invalid data. */
	bool SerializeCiv(FArchive& Ar);

	FChronoOnProgress OnProgress;

private:
	void HandleDayStepped(int64 DayIndex, const FChronoPopulationStats& Stats);
	/** Tells the building system what the city can build. Null finds the subsystem in the world. */
	void PushToBuildings(UChronoBuildingSubsystem* Buildings = nullptr) const;

	FChronoCivData Data;
	FChronoProgression Progression;
	FDelegateHandle DaySteppedHandle;
};
