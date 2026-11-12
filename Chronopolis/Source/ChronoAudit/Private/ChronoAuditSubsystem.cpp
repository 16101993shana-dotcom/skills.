#include "ChronoAuditSubsystem.h"

#include "ChronoCore.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoTerrainSubsystem.h"
#include "ChronoZoneGrid.h"
#include "ChronoZoningSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace ChronoAuditSubsystemPrivate
{
	// How long the markers of "Chrono.Audit.Run" stay visible.
	const float ConsoleDrawDurationSeconds = 60.0f;

	void RunFromConsole(UWorld* World)
	{
		UChronoAuditSubsystem* Audit = World ? World->GetSubsystem<UChronoAuditSubsystem>() : nullptr;
		if (!Audit)
		{
			UE_LOG(LogChrono, Warning, TEXT("Zero-clipping audit: no game world"));
			return;
		}

		Audit->ClearDrawing();
		Audit->RunAudit();
		Audit->DrawIssues(ConsoleDrawDurationSeconds);
	}

	void ClearFromConsole(UWorld* World)
	{
		if (const UChronoAuditSubsystem* Audit = World ? World->GetSubsystem<UChronoAuditSubsystem>() : nullptr)
		{
			Audit->ClearDrawing();
		}
	}

	FAutoConsoleCommandWithWorld RunCommand(
		TEXT("Chrono.Audit.Run"),
		TEXT("Runs the zero-clipping audit, logs every issue and marks it in the world."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunFromConsole));

	FAutoConsoleCommandWithWorld ClearCommand(
		TEXT("Chrono.Audit.Clear"),
		TEXT("Removes the markers drawn by Chrono.Audit.Run."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ClearFromConsole));
}

bool UChronoAuditSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

int32 UChronoAuditSubsystem::RunAudit()
{
	Issues.Reset();

	const UWorld* World = GetWorld();
	const UChronoTerrainSubsystem* Terrain = World ? World->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;
	const UChronoRoadSubsystem* Roads = World ? World->GetSubsystem<UChronoRoadSubsystem>() : nullptr;
	if (!Terrain || !Roads)
	{
		UE_LOG(LogChrono, Warning, TEXT("Zero-clipping audit: terrain or roads are missing"));
		return 0;
	}

	const FChronoAuditSettings AuditSettings;
	ChronoClippingAudit::Run(Terrain->GetHeightfield(), Roads->GetGraph(), FChronoRoadRules::FromSettings(), AuditSettings, Issues);

	if (const UChronoZoningSubsystem* Zoning = World->GetSubsystem<UChronoZoningSubsystem>())
	{
		ChronoClippingAudit::RunZoning(Roads->GetGraph(), Zoning->GetGrid(), FChronoZoningRules::FromSettings(), AuditSettings, Issues);
	}

	if (Issues.Num() == 0)
	{
		UE_LOG(LogChrono, Display, TEXT("Zero-clipping audit: PASSED (%d segments, %d nodes)"), Roads->GetNumSegments(), Roads->GetNumNodes());
	}
	else
	{
		UE_LOG(LogChrono, Error, TEXT("Zero-clipping audit: FAILED with %d issue(s)"), Issues.Num());
		for (const FChronoAuditIssue& Issue : Issues)
		{
			UE_LOG(LogChrono, Error, TEXT("  [%s] %s at %s"), *Issue.Rule.ToString(), *Issue.Description, *Issue.Location.ToCompactString());
		}
	}

	return Issues.Num();
}

FString UChronoAuditSubsystem::GetIssueText(int32 Index) const
{
	return Issues.IsValidIndex(Index) ? FString::Printf(TEXT("%s: %s"), *Issues[Index].Rule.ToString(), *Issues[Index].Description) : FString();
}

FVector UChronoAuditSubsystem::GetIssueLocation(int32 Index) const
{
	return Issues.IsValidIndex(Index) ? Issues[Index].Location : FVector::ZeroVector;
}

void UChronoAuditSubsystem::DrawIssues(float DurationSeconds) const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Sizes chosen to stay visible from the city-wide view.
	const float MarkerRadiusCm = 300.0f;
	const double PoleHeightCm = 3000.0;

	for (const FChronoAuditIssue& Issue : Issues)
	{
		DrawDebugSphere(World, Issue.Location, MarkerRadiusCm, 12, FColor::Red, false, DurationSeconds, 0, 10.0f);
		DrawDebugLine(World, Issue.Location, Issue.Location + FVector(0.0, 0.0, PoleHeightCm), FColor::Red, false, DurationSeconds, 0, 20.0f);
		DrawDebugString(World, Issue.Location + FVector(0.0, 0.0, PoleHeightCm), Issue.Rule.ToString(), nullptr, FColor::White, DurationSeconds, true);
	}
#endif
}

void UChronoAuditSubsystem::ClearDrawing() const
{
#if ENABLE_DRAW_DEBUG
	if (const UWorld* World = GetWorld())
	{
		FlushPersistentDebugLines(World);
		FlushDebugStrings(World);
	}
#endif
}
