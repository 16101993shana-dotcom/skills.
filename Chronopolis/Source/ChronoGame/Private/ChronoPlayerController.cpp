#include "ChronoPlayerController.h"

#include "ChronoSimulationSubsystem.h"
#include "ChronoTerrainSubsystem.h"
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
	FVector Ground;
	if (GetGroundUnderCursor(Ground))
	{
		UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>();
		Terrain->RaiseTerrain(FVector2D(Ground.X, Ground.Y), BrushRadiusCm, RaiseRateCmPerSecond * GetWorld()->GetDeltaSeconds());
	}
}

void AChronoPlayerController::OnLower(const FInputActionValue& Value)
{
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
	bHasFlattenTarget = GetGroundUnderCursor(Ground);
	if (bHasFlattenTarget)
	{
		FlattenTargetHeightCm = static_cast<float>(Ground.Z);
	}
}

void AChronoPlayerController::OnFlatten(const FInputActionValue& Value)
{
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
	FVector Ground;
	if (GetGroundUnderCursor(Ground))
	{
		UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>();
		const float Strength = FMath::Clamp(BlendRatePerSecond * GetWorld()->GetDeltaSeconds(), 0.0f, 1.0f);
		Terrain->SmoothTerrain(FVector2D(Ground.X, Ground.Y), BrushRadiusCm, Strength);
	}
}
