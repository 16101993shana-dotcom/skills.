#include "ChronoSaveSubsystem.h"

#include "ChronoCore.h"
#include "ChronoHeightfield.h"
#include "ChronoRoadGraph.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoSimulationSubsystem.h"
#include "ChronoTerrainSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ChronoSavePrivate
{
	// "CHRN" in ASCII: identifies a Chronopolis save file.
	const uint32 FileMagic = 0x4348524E;

	// Bump when a block is added or changed. Older files are refused for now;
	// migration code goes here when the format has shipped.
	const int32 FileVersion = 1;

	const TCHAR* DefaultSlot = TEXT("quicksave");

	void SaveFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		if (UChronoSaveSubsystem* Save = World ? World->GetSubsystem<UChronoSaveSubsystem>() : nullptr)
		{
			Save->SaveGame(Args.Num() > 0 ? Args[0] : FString(DefaultSlot));
		}
	}

	void LoadFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		if (UChronoSaveSubsystem* Save = World ? World->GetSubsystem<UChronoSaveSubsystem>() : nullptr)
		{
			Save->LoadGame(Args.Num() > 0 ? Args[0] : FString(DefaultSlot));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs SaveCommand(
		TEXT("Chrono.Save"),
		TEXT("Chrono.Save [slot]: saves the current game."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SaveFromConsole));

	FAutoConsoleCommandWithWorldAndArgs LoadCommand(
		TEXT("Chrono.Load"),
		TEXT("Chrono.Load [slot]: loads a saved game."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoadFromConsole));
}

bool UChronoSaveSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FString UChronoSaveSubsystem::GetSaveFilePath(const FString& SlotName)
{
	// Only a plain file name is kept, so a slot name can never point outside
	// the save folder.
	FString SafeName = FPaths::MakeValidFileName(FPaths::GetCleanFilename(SlotName));
	if (SafeName.IsEmpty())
	{
		SafeName = ChronoSavePrivate::DefaultSlot;
	}

	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), SafeName + TEXT(".chrono"));
}

bool UChronoSaveSubsystem::DoesSaveExist(const FString& SlotName) const
{
	return FPaths::FileExists(GetSaveFilePath(SlotName));
}

bool UChronoSaveSubsystem::SaveGame(const FString& SlotName)
{
	UWorld* World = GetWorld();
	UChronoSimulationSubsystem* Simulation = World ? World->GetSubsystem<UChronoSimulationSubsystem>() : nullptr;
	UChronoTerrainSubsystem* Terrain = World ? World->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	UChronoRoadSubsystem* Roads = World ? World->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	if (!Simulation || !Terrain || !Roads)
	{
		UE_LOG(LogChrono, Error, TEXT("Save failed: the game systems are not running"));
		return false;
	}

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);

	uint32 Magic = ChronoSavePrivate::FileMagic;
	int32 Version = ChronoSavePrivate::FileVersion;
	int64 TickIndex = Simulation->GetTickIndex();
	Writer << Magic;
	Writer << Version;
	Writer << TickIndex;

	const bool bTerrainSaved = Terrain->SerializeTerrain(Writer);
	const bool bRoadsSaved = Roads->SerializeRoads(Writer);
	if (!bTerrainSaved || !bRoadsSaved || Writer.IsError())
	{
		UE_LOG(LogChrono, Error, TEXT("Save failed: could not serialize the game state"));
		return false;
	}

	const FString Path = GetSaveFilePath(SlotName);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
	{
		UE_LOG(LogChrono, Error, TEXT("Save failed: could not write %s"), *Path);
		return false;
	}

	UE_LOG(LogChrono, Display, TEXT("Game saved to %s (%d bytes)"), *Path, Bytes.Num());
	return true;
}

bool UChronoSaveSubsystem::LoadGame(const FString& SlotName)
{
	UWorld* World = GetWorld();
	UChronoSimulationSubsystem* Simulation = World ? World->GetSubsystem<UChronoSimulationSubsystem>() : nullptr;
	UChronoTerrainSubsystem* Terrain = World ? World->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	UChronoRoadSubsystem* Roads = World ? World->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	if (!Simulation || !Terrain || !Roads)
	{
		UE_LOG(LogChrono, Error, TEXT("Load failed: the game systems are not running"));
		return false;
	}

	const FString Path = GetSaveFilePath(SlotName);
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		UE_LOG(LogChrono, Error, TEXT("Load failed: could not read %s"), *Path);
		return false;
	}

	uint32 Magic = 0;
	int32 Version = 0;
	int64 TickIndex = 0;
	{
		FMemoryReader Header(Bytes);
		Header << Magic;
		Header << Version;
		Header << TickIndex;
		if (Header.IsError() || Magic != ChronoSavePrivate::FileMagic)
		{
			UE_LOG(LogChrono, Error, TEXT("Load failed: %s is not a Chronopolis save"), *Path);
			return false;
		}
		if (Version != ChronoSavePrivate::FileVersion)
		{
			UE_LOG(LogChrono, Error, TEXT("Load failed: %s has version %d, this build reads version %d"), *Path, Version, ChronoSavePrivate::FileVersion);
			return false;
		}
	}

	// Dry run first: decode the whole file into temporary objects, so that a
	// damaged file is refused before anything in the running game changes.
	{
		FMemoryReader Probe(Bytes);
		Probe << Magic;
		Probe << Version;
		Probe << TickIndex;

		FChronoHeightfield ProbeTerrain;
		ProbeTerrain.Serialize(Probe);
		FChronoRoadGraph ProbeRoads;
		const bool bRoadsValid = ProbeRoads.Serialize(Probe);

		const FChronoHeightfield& Current = Terrain->GetHeightfield();
		const bool bSameSize = ProbeTerrain.IsValid()
			&& ProbeTerrain.GetSizeCells() == Current.GetSizeCells()
			&& FMath::IsNearlyEqual(ProbeTerrain.GetCellSizeCm(), Current.GetCellSizeCm());

		if (Probe.IsError() || !bRoadsValid || !bSameSize || TickIndex < 0)
		{
			UE_LOG(LogChrono, Error, TEXT("Load failed: %s is damaged or was saved on a map of another size"), *Path);
			return false;
		}
	}

	// Real load. Terrain first: roads are drawn on top of it.
	FMemoryReader Reader(Bytes);
	Reader << Magic;
	Reader << Version;
	Reader << TickIndex;

	const bool bTerrainLoaded = Terrain->SerializeTerrain(Reader);
	const bool bRoadsLoaded = bTerrainLoaded && Roads->SerializeRoads(Reader);
	if (!bRoadsLoaded)
	{
		// Should not happen after a successful dry run.
		UE_LOG(LogChrono, Error, TEXT("Load failed while applying %s"), *Path);
		return false;
	}

	Simulation->RestoreTickIndex(TickIndex);

	UE_LOG(LogChrono, Display, TEXT("Game loaded from %s (tick %lld, %d road segments)"), *Path, TickIndex, Roads->GetNumSegments());
	return true;
}
