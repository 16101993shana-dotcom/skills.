#include "ChronoGameMode.h"

#include "ChronoCameraPawn.h"
#include "ChronoPlayerController.h"

AChronoGameMode::AChronoGameMode()
{
	// Blueprint subclasses (BP_ChronoGameMode) override these with the
	// Blueprint pawn and controller that carry the input assets.
	DefaultPawnClass = AChronoCameraPawn::StaticClass();
	PlayerControllerClass = AChronoPlayerController::StaticClass();
}
