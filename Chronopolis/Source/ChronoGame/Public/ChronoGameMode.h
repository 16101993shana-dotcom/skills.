#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ChronoGameMode.generated.h"

/** Sets the strategy camera pawn and the Chronopolis player controller. */
UCLASS()
class CHRONOGAME_API AChronoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AChronoGameMode();

	virtual void StartPlay() override;
};
