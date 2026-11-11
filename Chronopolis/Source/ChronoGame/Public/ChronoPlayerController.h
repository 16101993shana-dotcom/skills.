// Player controller: mouse cursor, simulation speed commands and the
// terraforming brush used to validate the terrain in Phase 1.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ChronoPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS()
class CHRONOGAME_API AChronoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AChronoPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Console: ChronoSetSpeed 0..3 (0 = pause, 1 = x1, 2 = fast, 3 = fastest). */
	UFUNCTION(Exec)
	void ChronoSetSpeed(int32 SpeedIndex);

	/** World position of the ground under the mouse cursor. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Input")
	bool GetGroundUnderCursor(FVector& OutLocation) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> ToolMappingContext;

	/** Bool, held: raises the ground under the cursor. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> RaiseAction;

	/** Bool, held: lowers the ground under the cursor. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LowerAction;

	/** Bool, held: levels the ground to the height where the press started. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> FlattenAction;

	/** Bool, held: smooths the ground under the cursor. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SmoothAction;

	UPROPERTY(EditDefaultsOnly, Category = "Terraform", meta = (ClampMin = "100.0"))
	float BrushRadiusCm = 2500.0f;

	/** Height change per second at the brush center. */
	UPROPERTY(EditDefaultsOnly, Category = "Terraform", meta = (ClampMin = "1.0"))
	float RaiseRateCmPerSecond = 800.0f;

	/** Blend per second toward the target (flatten) or the neighbors (smooth). */
	UPROPERTY(EditDefaultsOnly, Category = "Terraform", meta = (ClampMin = "0.1"))
	float BlendRatePerSecond = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Terraform", meta = (ClampMin = "1000.0"))
	float CursorTraceDistanceCm = 2000000.0f;

private:
	void OnRaise(const FInputActionValue& Value);
	void OnLower(const FInputActionValue& Value);
	void OnFlattenStarted(const FInputActionValue& Value);
	void OnFlatten(const FInputActionValue& Value);
	void OnSmooth(const FInputActionValue& Value);

	float FlattenTargetHeightCm = 0.0f;
	bool bHasFlattenTarget = false;
};
