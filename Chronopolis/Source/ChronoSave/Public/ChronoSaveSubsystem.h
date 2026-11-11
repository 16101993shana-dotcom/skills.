// Save and load of the whole game state to a binary file under
// Saved/SaveGames. Phase 1 stores the simulation clock, the terrain and the
// road network; later systems append their own block and bump the version.
// Console: "Chrono.Save <slot>" and "Chrono.Load <slot>".

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoSaveSubsystem.generated.h"

UCLASS()
class CHRONOSAVE_API UChronoSaveSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Writes the current game to a slot. Returns false if the file could not be written. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Save")
	bool SaveGame(const FString& SlotName);

	/**
	 * Replaces the current game by a saved one. On any error (missing file,
	 * other version, damaged data) the current game is left untouched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Save")
	bool LoadGame(const FString& SlotName);

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Save")
	bool DoesSaveExist(const FString& SlotName) const;

	/** Full path of the file used by a slot. The slot name is reduced to a safe file name. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Save")
	static FString GetSaveFilePath(const FString& SlotName);
};
