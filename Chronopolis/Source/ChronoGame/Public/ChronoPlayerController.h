// Player controller: mouse cursor, simulation speed commands and the two
// Phase 1 tools (terraforming brush, road drawing).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ChronoZoneGrid.h"
#include "ChronoPlayerController.generated.h"

class UChronoRoadProfile;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UENUM(BlueprintType)
enum class EChronoToolMode : uint8
{
	Terraform,
	Road,
	Zone
};

UCLASS()
class CHRONOGAME_API AChronoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AChronoPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** Console: ChronoSetSpeed 0..3 (0 = pause, 1 = x1, 2 = fast, 3 = fastest). */
	UFUNCTION(Exec)
	void ChronoSetSpeed(int32 SpeedIndex);

	/** Console: ChronoTool 0 (terraform), 1 (road) or 2 (zoning). */
	UFUNCTION(Exec)
	void ChronoTool(int32 ToolIndex);

	/** Console: ChronoRoadCurve 0 (straight roads) or 1 (curved roads, three clicks). */
	UFUNCTION(Exec)
	void ChronoRoadCurve(int32 Enabled);

	/** Console: ChronoZone 1..5 (residential, commercial, industrial, agricultural, civic). */
	UFUNCTION(Exec)
	void ChronoZone(int32 ZoneIndex);

	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Tools")
	void SetToolMode(EChronoToolMode NewMode);

	/** Zone type painted by the zoning tool. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Tools")
	void SetPaintZone(EChronoZoneType NewZone);

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Tools")
	EChronoToolMode GetToolMode() const { return ToolMode; }

	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Tools")
	void SetCurvedRoads(bool bInCurved);

	/** World position of the ground under the mouse cursor. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Input")
	bool GetGroundUnderCursor(FVector& OutLocation) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> ToolMappingContext;

	// ---- Terraform tool ----

	/** Bool, held: raises the ground under the cursor. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Terraform")
	TObjectPtr<UInputAction> RaiseAction;

	/** Bool, held: lowers the ground under the cursor. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Terraform")
	TObjectPtr<UInputAction> LowerAction;

	/** Bool, held: levels the ground to the height where the press started. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Terraform")
	TObjectPtr<UInputAction> FlattenAction;

	/** Bool, held: smooths the ground under the cursor. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Terraform")
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

	// ---- Road tool ----

	/** Bool, pressed: places the next point of the road. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Road")
	TObjectPtr<UInputAction> RoadPlaceAction;

	/** Bool, pressed: cancels the road being drawn. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Road")
	TObjectPtr<UInputAction> RoadCancelAction;

	/** Bool, pressed: demolishes the road under the cursor. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Road")
	TObjectPtr<UInputAction> RoadDemolishAction;

	/** Type of road drawn by the road tool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Road")
	TObjectPtr<UChronoRoadProfile> RoadProfile;

	// ---- Zoning tool ----
	// The zoning tool reuses the terraform inputs: the raise input paints the
	// selected zone, the lower input removes the zone.

	UPROPERTY(EditDefaultsOnly, Category = "Zoning", meta = (ClampMin = "100.0"))
	float ZoneBrushRadiusCm = 1200.0f;

	/** Seconds a tool message stays on screen. */
	UPROPERTY(EditDefaultsOnly, Category = "Road", meta = (ClampMin = "0.5"))
	float MessageDurationSeconds = 4.0f;

private:
	void OnRaise(const FInputActionValue& Value);
	void OnLower(const FInputActionValue& Value);
	void OnFlattenStarted(const FInputActionValue& Value);
	void OnFlatten(const FInputActionValue& Value);
	void OnSmooth(const FInputActionValue& Value);

	void OnRoadPlace(const FInputActionValue& Value);
	void OnRoadCancel(const FInputActionValue& Value);
	void OnRoadDemolish(const FInputActionValue& Value);

	void ResetRoadDrawing();
	void ShowToolMessage(const FText& Message, bool bIsError) const;
	void DrawRoadPreview() const;

	void PaintZoneUnderCursor(EChronoZoneType Zone);

	EChronoToolMode ToolMode = EChronoToolMode::Terraform;
	EChronoZoneType PaintZone = EChronoZoneType::Residential;

	float FlattenTargetHeightCm = 0.0f;
	bool bHasFlattenTarget = false;

	// Road being drawn: start point, then (curved roads only) control point.
	bool bCurvedRoads = false;
	bool bHasRoadStart = false;
	bool bHasRoadControl = false;
	FVector2D RoadStart = FVector2D::ZeroVector;
	FVector2D RoadControl = FVector2D::ZeroVector;
};
