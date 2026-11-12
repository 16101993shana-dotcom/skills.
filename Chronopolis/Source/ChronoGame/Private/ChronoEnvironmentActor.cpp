#include "ChronoEnvironmentActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"

AChronoEnvironmentActor::AChronoEnvironmentActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// The sun drives the atmosphere: the sky color follows its elevation.
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);

	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(RootComponent);

	// Captures the sky every frame, so ambient light follows the sun.
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(RootComponent);

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(RootComponent);

	// Unbound: applies to the whole world.
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(RootComponent);
	PostProcess->bUnbound = true;
}

void AChronoEnvironmentActor::BeginPlay()
{
	Super::BeginPlay();

	const UChronoEnvironmentSettings* Settings = UChronoEnvironmentSettings::Get();

	Sun->SetIntensity(Settings->SunIntensityLux);
	SetSunAngles(Settings->SunPitchDegrees, Settings->SunYawDegrees);

	HeightFog->SetFogDensity(Settings->FogDensity);
	HeightFog->SetVolumetricFog(Settings->bVolumetricFog);

	Clouds->SetVisibility(Settings->bVolumetricClouds);

	// Dynamic global illumination and reflections, with automatic exposure
	// left on so the picture stays readable from street level to the city view.
	FPostProcessSettings& PostSettings = PostProcess->Settings;
	PostSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	PostSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	PostSettings.bOverride_ReflectionMethod = true;
	PostSettings.ReflectionMethod = EReflectionMethod::Lumen;
	PostSettings.bOverride_AutoExposureBias = true;
	PostSettings.AutoExposureBias = Settings->ExposureCompensation;
}

void AChronoEnvironmentActor::SetSunAngles(float PitchDegrees, float YawDegrees)
{
	// The light points where the sun rays go: a negative pitch shines down.
	const float ClampedPitch = FMath::Clamp(PitchDegrees, -90.0f, 0.0f);
	Sun->SetWorldRotation(FRotator(ClampedPitch, YawDegrees, 0.0f));
}
