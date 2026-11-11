// Strategy camera: pan, continuous zoom from street level to the regional
// view, rotation and tilt. The focus point follows the terrain and the camera
// itself is never allowed below the ground.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ChronoCameraPawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USceneComponent;
class USpringArmComponent;
struct FInputActionValue;

UCLASS()
class CHRONOGAME_API AChronoCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AChronoCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Moves the focus point instantly (used by "go to" buttons and by loading). */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Camera")
	void FocusOn(FVector2D WorldXY, bool bInstant);

	/** 0 = closest (street level), 1 = farthest (regional view). */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Camera")
	float GetZoomAlpha() const { return CurrentZoomAlpha; }

protected:
	// ---- Components ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> FocusRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	// ---- Input assets (assigned in the Blueprint subclass) ----

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> CameraMappingContext;

	/** Axis2D: X = right, Y = forward. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** Axis1D: positive zooms in. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ZoomAction;

	/** Axis1D: positive rotates clockwise seen from above. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> RotateAction;

	/** Axis1D: positive tilts toward the horizon. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> TiltAction;

	// ---- Tuning (edit in the Blueprint subclass) ----

	/** Camera distance at full zoom in, in centimeters. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "100.0"))
	float MinArmLengthCm = 800.0f;

	/** Camera distance at full zoom out, in centimeters. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "1000.0"))
	float MaxArmLengthCm = 400000.0f;

	/** Fraction of the zoom range covered by one unit of zoom input (one wheel notch). */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float ZoomStep = 0.06f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "0.1"))
	float ZoomInterpSpeed = 8.0f;

	/** Pan speed, in camera distances per second: panning feels the same at every zoom level. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Pan", meta = (ClampMin = "0.01"))
	float PanSpeedFactor = 1.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Pan", meta = (ClampMin = "0.1"))
	float PanInterpSpeed = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Rotation", meta = (ClampMin = "1.0"))
	float RotateSpeedDegrees = 90.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Rotation", meta = (ClampMin = "1.0"))
	float TiltSpeedDegrees = 45.0f;

	/** Steepest pitch (looking almost straight down). */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Rotation", meta = (ClampMin = "-89.0", ClampMax = "-1.0"))
	float MinPitchDegrees = -85.0f;

	/** Shallowest pitch (looking toward the horizon). */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Rotation", meta = (ClampMin = "-89.0", ClampMax = "-1.0"))
	float MaxPitchDegrees = -12.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Rotation", meta = (ClampMin = "-89.0", ClampMax = "-1.0"))
	float InitialPitchDegrees = -50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Rotation", meta = (ClampMin = "0.1"))
	float RotationInterpSpeed = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialZoomAlpha = 0.55f;

	/** The camera is kept at least this far above the ground. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "10.0"))
	float MinGroundClearanceCm = 200.0f;

private:
	void OnMove(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnRotate(const FInputActionValue& Value);
	void OnTilt(const FInputActionValue& Value);

	float ComputeArmLength(float ZoomAlpha) const;
	void ApplyCameraTransform();

	FVector2D TargetFocus = FVector2D::ZeroVector;
	FVector2D CurrentFocus = FVector2D::ZeroVector;
	float TargetZoomAlpha = 0.5f;
	float CurrentZoomAlpha = 0.5f;
	float TargetYawDegrees = 0.0f;
	float CurrentYawDegrees = 0.0f;
	float TargetPitchDegrees = -50.0f;
	float CurrentPitchDegrees = -50.0f;
};
