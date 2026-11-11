#include "ChronoPlayerController.h"

#include "ChronoCore.h"
#include "ChronoRoadGeometry.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoRoadTypes.h"
#include "ChronoSimulationSubsystem.h"
#include "ChronoTerrainSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

AChronoPlayerController::AChronoPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void AChronoPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Strategy game: the cursor stays visible and free while the game has focus.
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (ToolMappingContext)
		{
			InputSubsystem->AddMappingContext(ToolMappingContext, 1);
		}
	}
}

void AChronoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (RaiseAction)
		{
			EnhancedInput->BindAction(RaiseAction, ETriggerEvent::Triggered, this, &AChronoPlayerController::OnRaise);
		}
		if (LowerAction)
		{
			EnhancedInput->BindAction(LowerAction, ETriggerEvent::Triggered, this, &AChronoPlayerController::OnLower);
		}
		if (FlattenAction)
		{
			EnhancedInput->BindAction(FlattenAction, ETriggerEvent::Started, this, &AChronoPlayerController::OnFlattenStarted);
			EnhancedInput->BindAction(FlattenAction, ETriggerEvent::Triggered, this, &AChronoPlayerController::OnFlatten);
		}
		if (SmoothAction)
		{
			EnhancedInput->BindAction(SmoothAction, ETriggerEvent::Triggered, this, &AChronoPlayerController::OnSmooth);
		}
		if (RoadPlaceAction)
		{
			EnhancedInput->BindAction(RoadPlaceAction, ETriggerEvent::Started, this, &AChronoPlayerController::OnRoadPlace);
		}
		if (RoadCancelAction)
		{
			EnhancedInput->BindAction(RoadCancelAction, ETriggerEvent::Started, this, &AChronoPlayerController::OnRoadCancel);
		}
		if (RoadDemolishAction)
		{
			EnhancedInput->BindAction(RoadDemolishAction, ETriggerEvent::Started, this, &AChronoPlayerController::OnRoadDemolish);
		}
	}
}

void AChronoPlayerController::ChronoSetSpeed(int32 SpeedIndex)
{
	if (UChronoSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UChronoSimulationSubsystem>())
	{
		const int32 Clamped = FMath::Clamp(SpeedIndex, 0, static_cast<int32>(EChronoSimSpeed::Fastest));
		Simulation->SetSpeed(static_cast<EChronoSimSpeed>(Clamped));
	}
}

bool AChronoPlayerController::GetGroundUnderCursor(FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;

	const UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>();
	FVector RayOrigin;
	FVector RayDirection;
	if (!Terrain || !DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	return Terrain->RaycastTerrain(RayOrigin, RayDirection, CursorTraceDistanceCm, OutLocation);
}

void AChronoPlayerController::OnRaise(const FInputActionValue& Value)
{
	if (ToolMode != EChronoToolMode::Terraform)
	{
		return;
	}

	FVector Ground;
	if (GetGroundUnderCursor(Ground))
	{
		UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>();
		Terrain->RaiseTerrain(FVector2D(Ground.X, Ground.Y), BrushRadiusCm, RaiseRateCmPerSecond * GetWorld()->GetDeltaSeconds());
	}
}

void AChronoPlayerController::OnLower(const FInputActionValue& Value)
{
	if (ToolMode != EChronoToolMode::Terraform)
	{
		return;
	}

	FVector Ground;
	if (GetGroundUnderCursor(Ground))
	{
		UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>();
		Terrain->RaiseTerrain(FVector2D(Ground.X, Ground.Y), BrushRadiusCm, -RaiseRateCmPerSecond * GetWorld()->GetDeltaSeconds());
	}
}

void AChronoPlayerController::OnFlattenStarted(const FInputActionValue& Value)
{
	FVector Ground;
	bHasFlattenTarget = (ToolMode == EChronoToolMode::Terraform) && GetGroundUnderCursor(Ground);
	if (bHasFlattenTarget)
	{
		FlattenTargetHeightCm = static_cast<float>(Ground.Z);
	}
}

void AChronoPlayerController::OnFlatten(const FInputActionValue& Value)
{
	if (ToolMode != EChronoToolMode::Terraform)
	{
		return;
	}

	FVector Ground;
	if (bHasFlattenTarget && GetGroundUnderCursor(Ground))
	{
		UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>();
		const float Strength = FMath::Clamp(BlendRatePerSecond * GetWorld()->GetDeltaSeconds(), 0.0f, 1.0f);
		Terrain->FlattenTerrain(FVector2D(Ground.X, Ground.Y), BrushRadiusCm, FlattenTargetHeightCm, Strength);
	}
}

void AChronoPlayerController::OnSmooth(const FInputActionValue& Value)
{
	if (ToolMode != EChronoToolMode::Terraform)
	{
		return;
	}

	FVector Ground;
	if (GetGroundUnderCursor(Ground))
	{
		UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>();
		const float Strength = FMath::Clamp(BlendRatePerSecond * GetWorld()->GetDeltaSeconds(), 0.0f, 1.0f);
		Terrain->SmoothTerrain(FVector2D(Ground.X, Ground.Y), BrushRadiusCm, Strength);
	}
}

void AChronoPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (ToolMode == EChronoToolMode::Road && bHasRoadStart)
	{
		DrawRoadPreview();
	}
}

void AChronoPlayerController::ChronoTool(int32 ToolIndex)
{
	SetToolMode(ToolIndex == 1 ? EChronoToolMode::Road : EChronoToolMode::Terraform);
}

void AChronoPlayerController::ChronoRoadCurve(int32 bEnabled)
{
	SetCurvedRoads(bEnabled != 0);
}

void AChronoPlayerController::SetToolMode(EChronoToolMode NewMode)
{
	ToolMode = NewMode;
	ResetRoadDrawing();
	bHasFlattenTarget = false;
}

void AChronoPlayerController::SetCurvedRoads(bool bInCurved)
{
	bCurvedRoads = bInCurved;
	ResetRoadDrawing();
}

void AChronoPlayerController::ResetRoadDrawing()
{
	bHasRoadStart = false;
	bHasRoadControl = false;
}

void AChronoPlayerController::ShowToolMessage(const FText& Message, bool bIsError) const
{
	// Temporary feedback until the Phase 7 notification UI exists.
	UE_LOG(LogChrono, Log, TEXT("%s"), *Message.ToString());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, MessageDurationSeconds, bIsError ? FColor::Orange : FColor::Green, Message.ToString());
	}
}

void AChronoPlayerController::OnRoadPlace(const FInputActionValue& Value)
{
	if (ToolMode != EChronoToolMode::Road)
	{
		return;
	}

	FVector Ground;
	if (!GetGroundUnderCursor(Ground))
	{
		return;
	}
	const FVector2D Point(Ground.X, Ground.Y);

	// First click: start of the road.
	if (!bHasRoadStart)
	{
		RoadStart = Point;
		bHasRoadStart = true;
		bHasRoadControl = false;
		return;
	}

	// Curved roads take a middle click that sets the bend.
	if (bCurvedRoads && !bHasRoadControl)
	{
		RoadControl = Point;
		bHasRoadControl = true;
		return;
	}

	UChronoRoadSubsystem* Roads = GetWorld()->GetSubsystem<UChronoRoadSubsystem>();
	if (!Roads)
	{
		return;
	}

	FChronoRoadBuildRequest Request;
	Request.Start = RoadStart;
	Request.End = Point;
	Request.bCurved = bCurvedRoads && bHasRoadControl;
	Request.Control = RoadControl;
	if (RoadProfile)
	{
		Request.Profile = RoadProfile->Data;
	}
	else
	{
		// No road type assigned: the subsystem refuses with a clear message.
		Request.Profile.CarriagewayWidthCm = 0.0f;
	}

	int32 SegmentId = INDEX_NONE;
	const EChronoRoadBuildResult Result = Roads->BuildRoad(Request, SegmentId);
	if (Result == EChronoRoadBuildResult::Success)
	{
		// Chain: the next road starts where this one ends.
		RoadStart = Point;
		bHasRoadControl = false;
	}
	else
	{
		// Refused placement: nothing was built, the player keeps the start
		// point and can try another end point.
		ShowToolMessage(UChronoRoadSubsystem::GetBuildResultText(Result), true);
		bHasRoadControl = false;
	}
}

void AChronoPlayerController::OnRoadCancel(const FInputActionValue& Value)
{
	if (ToolMode == EChronoToolMode::Road)
	{
		ResetRoadDrawing();
	}
}

void AChronoPlayerController::OnRoadDemolish(const FInputActionValue& Value)
{
	if (ToolMode != EChronoToolMode::Road)
	{
		return;
	}

	FVector Ground;
	UChronoRoadSubsystem* Roads = GetWorld()->GetSubsystem<UChronoRoadSubsystem>();
	if (Roads && RoadProfile && GetGroundUnderCursor(Ground))
	{
		// The cursor must be on the road: within its half width.
		Roads->DemolishRoadAt(FVector2D(Ground.X, Ground.Y), RoadProfile->Data.GetHalfWidthCm());
	}
}

void AChronoPlayerController::DrawRoadPreview() const
{
#if ENABLE_DRAW_DEBUG
	const UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>();
	FVector Ground;
	if (!Terrain || !GetGroundUnderCursor(Ground))
	{
		return;
	}

	const FVector2D Cursor(Ground.X, Ground.Y);

	// Centerline that a click would build right now.
	FChronoBezier2D Curve;
	if (bCurvedRoads && bHasRoadControl)
	{
		Curve = FChronoBezier2D::MakeQuadratic(RoadStart, RoadControl, Cursor);
	}
	else
	{
		Curve = FChronoBezier2D::MakeLine(RoadStart, Cursor);
	}

	// Drawn slightly above the ground so the line is not hidden by it.
	const double Lift = 30.0;
	const int32 NumSteps = 24;
	FVector Previous(RoadStart.X, RoadStart.Y, Terrain->GetHeightAt(RoadStart) + Lift);
	for (int32 Step = 1; Step <= NumSteps; ++Step)
	{
		const FVector2D Point = Curve.Evaluate(static_cast<double>(Step) / NumSteps);
		const FVector Current(Point.X, Point.Y, Terrain->GetHeightAt(Point) + Lift);
		DrawDebugLine(GetWorld(), Previous, Current, FColor::Yellow, false, -1.0f, 0, 20.0f);
		Previous = Current;
	}
#endif
}
