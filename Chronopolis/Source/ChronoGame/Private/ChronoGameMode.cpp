#include "ChronoGameMode.h"

#include "ChronoCameraPawn.h"
#include "ChronoEnvironmentActor.h"
#include "Engine/World.h"
#include "ChronoPlayerController.h"

AChronoGameMode::AChronoGameMode()
{
	// Blueprint subclasses (BP_ChronoGameMode) override these with the
	// Blueprint pawn and controller that carry the input assets.
	DefaultPawnClass = AChronoCameraPawn::StaticClass();
	PlayerControllerClass = AChronoPlayerController::StaticClass();
}

void AChronoGameMode::StartPlay()
{
	// Sky, sun and atmosphere first, so the first frame is already lit.
	if (UChronoEnvironmentSettings::Get()->bSpawnEnvironment)
	{
		GetWorld()->SpawnActor<AChronoEnvironmentActor>(AChronoEnvironmentActor::StaticClass(), FTransform::Identity);
	}

	Super::StartPlay();
}
