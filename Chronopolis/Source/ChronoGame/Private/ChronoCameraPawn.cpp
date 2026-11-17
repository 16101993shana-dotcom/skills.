#include "ChronoCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "ChronoTerrainSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

AChronoCameraPawn::AChronoCameraPawn()
{
	// The camera interpolates every frame, independently of the simulation speed.
	PrimaryActorTick.bCanEverTick = true;

	FocusRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FocusRoot"));
	RootComponent = FocusRoot;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(FocusRoot);
	// The ground clearance is enforced analytically from the heightfield,
	// which is exact and cheaper than a collision sweep.
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bEnableCameraLag = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void AChronoCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	const FVector Location = GetActorLocation();
	TargetFocus = CurrentFocus = FVector2D(Location.X, Location.Y);
	TargetZoomAlpha = CurrentZoomAlpha = FMath::Clamp(InitialZoomAlpha, 0.0f, 1.0f);
	TargetPitchDegrees = CurrentPitchDegrees = FMath::Clamp(InitialPitchDegrees, MinPitchDegrees, MaxPitchDegrees);
	TargetYawDegrees = CurrentYawDegrees = 0.0f;

	ApplyCameraTransform();
}

void AChronoCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (CameraMappingContext)
			{
				InputSubsystem->AddMappingContext(CameraMappingContext, 0);
			}
		}
	}

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AChronoCameraPawn::OnMove);
		}
		if (ZoomAction)
		{
			EnhancedInput->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AChronoCameraPawn::OnZoom);
		}
		if (RotateAction)
		{
			EnhancedInput->BindAction(RotateAction, ETriggerEvent::Triggered, this, &AChronoCameraPawn::OnRotate);
		}
		if (TiltAction)
		{
			EnhancedInput->BindAction(TiltAction, ETriggerEvent::Triggered, this, &AChronoCameraPawn::OnTilt);
		}
	}
}

void AChronoCameraPawn::OnMove(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();

	// Move in the ground plane, relative to where the camera looks.
	const FRotator YawRotation(0.0, TargetYawDegrees, 0.0);
	const FVector Forward = YawRotation.Vector();
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	const double Speed = PanSpeedFactor * ComputeArmLength(TargetZoomAlpha);
	const FVector Delta = (Forward * Input.Y + Right * Input.X) * Speed * DeltaSeconds;
	TargetFocus += FVector2D(Delta.X, Delta.Y);
}

void AChronoCameraPawn::OnZoom(const FInputActionValue& Value)
{
	// Positive input zooms in, which lowers the alpha. Not scaled by delta
	// time: a mouse wheel notch is an impulse, not a rate.
	TargetZoomAlpha = FMath::Clamp(TargetZoomAlpha - Value.Get<float>() * ZoomStep, 0.0f, 1.0f);
}

void AChronoCameraPawn::OnRotate(const FInputActionValue& Value)
{
	TargetYawDegrees += Value.Get<float>() * RotateSpeedDegrees * GetWorld()->GetDeltaSeconds();
}

void AChronoCameraPawn::OnTilt(const FInputActionValue& Value)
{
	const float Delta = Value.Get<float>() * TiltSpeedDegrees * GetWorld()->GetDeltaSeconds();
	TargetPitchDegrees = FMath::Clamp(TargetPitchDegrees + Delta, MinPitchDegrees, MaxPitchDegrees);
}

void AChronoCameraPawn::FocusOn(FVector2D WorldXY, bool bInstant)
{
	TargetFocus = WorldXY;
	if (bInstant)
	{
		CurrentFocus = WorldXY;
		ApplyCameraTransform();
	}
}

float AChronoCameraPawn::ComputeArmLength(float ZoomAlpha) const
{
	// Exponential interpolation: each zoom step changes the distance by the
	// same ratio, which feels uniform from street level to the regional view.
	const float SafeMin = FMath::Max(MinArmLengthCm, 1.0f);
	const float SafeMax = FMath::Max(MaxArmLengthCm, SafeMin);
	return SafeMin * FMath::Pow(SafeMax / SafeMin, FMath::Clamp(ZoomAlpha, 0.0f, 1.0f));
}

void AChronoCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Keep the focus point inside the playable area.
	if (const UChronoTerrainSubsystem* Terrain = GetWorld()->GetSubsystem<UChronoTerrainSubsystem>())
	{
		TargetFocus = Terrain->GetHeightfield().ClampToWorld(TargetFocus);
	}

	CurrentFocus = FMath::Vector2DInterpTo(CurrentFocus, TargetFocus, DeltaSeconds, PanInterpSpeed);
	CurrentZoomAlpha = FMath::FInterpTo(CurrentZoomAlpha, TargetZoomAlpha, DeltaSeconds, ZoomInterpSpeed);
	CurrentYawDegrees = FMath::FInterpTo(CurrentYawDegrees, TargetYawDegrees, DeltaSeconds, RotationInterpSpeed);
	CurrentPitchDegrees = FMath::FInterpTo(CurrentPitchDegrees, TargetPitchDegrees, DeltaSeconds, RotationInterpSpeed);

	ApplyCameraTransform();
}

void AChronoCameraPawn::ApplyCameraTransform()
{
	const UChronoTerrainSubsystem* Terrain = GetWorld() ? GetWorld()->GetSubsystem<UChronoTerrainSubsystem>() : nullptr;

	const float FocusHeight = Terrain ? Terrain->GetHeightAt(CurrentFocus) : 0.0f;
	const FVector FocusLocation(CurrentFocus.X, CurrentFocus.Y, FocusHeight);
	SetActorLocation(FocusLocation);

	const FRotator ArmRotation(CurrentPitchDegrees, CurrentYawDegrees, 0.0);
	const float ArmLength = ComputeArmLength(CurrentZoomAlpha);

	SpringArm->SetWorldRotation(ArmRotation);
	SpringArm->TargetArmLength = ArmLength;

	// Zero clipping applies to the camera too: if the camera would end up
	// under the ground (hill behind the focus point), lift it.
	float LiftCm = 0.0f;
	if (Terrain)
	{
		const FVector CameraLocation = FocusLocation - ArmRotation.Vector() * ArmLength;
		const float GroundUnderCamera = Terrain->GetHeightAt(FVector2D(CameraLocation.X, CameraLocation.Y));
		LiftCm = FMath::Max(0.0f, GroundUnderCamera + MinGroundClearanceCm - static_cast<float>(CameraLocation.Z));
	}
	SpringArm->TargetOffset = FVector(0.0, 0.0, LiftCm);
}
