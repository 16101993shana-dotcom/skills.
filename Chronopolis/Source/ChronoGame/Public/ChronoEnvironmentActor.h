// Sky and lighting of the city: sun, physically based atmosphere, sky light,
// height fog, volumetric clouds and a global post process. Spawned by the
// game mode so that an empty level is correctly lit without any setup.
//
// This is the lighting half of realism. The other half is the art itself:
// finished models, textures and materials replace the greybox volumes.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/Actor.h"
#include "ChronoEnvironmentActor.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UVolumetricCloudComponent;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Chronopolis Environment"))
class CHRONOGAME_API UChronoEnvironmentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UChronoEnvironmentSettings* Get() { return GetDefault<UChronoEnvironmentSettings>(); }

	/** Turn off if the level brings its own sun and sky. */
	UPROPERTY(Config, EditAnywhere, Category = "Environment")
	bool bSpawnEnvironment = true;

	/** Elevation of the sun: -90 is straight overhead, values close to 0 are sunrise and sunset. */
	UPROPERTY(Config, EditAnywhere, Category = "Sun", meta = (ClampMin = "-90.0", ClampMax = "0.0"))
	float SunPitchDegrees = -42.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Sun")
	float SunYawDegrees = 35.0f;

	/** Illuminance of direct sunlight, in lux. About 100000 on a clear day. */
	UPROPERTY(Config, EditAnywhere, Category = "Sun", meta = (ClampMin = "0.0"))
	float SunIntensityLux = 100000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Atmosphere", meta = (ClampMin = "0.0"))
	float FogDensity = 0.008f;

	/** Light shafts and depth in the air. Costs GPU time: first thing to turn off on a slow machine. */
	UPROPERTY(Config, EditAnywhere, Category = "Atmosphere")
	bool bVolumetricFog = true;

	UPROPERTY(Config, EditAnywhere, Category = "Atmosphere")
	bool bVolumetricClouds = true;

	/** Exposure bias in stops. 0 leaves the automatic exposure as it is. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera")
	float ExposureCompensation = 0.0f;
};

UCLASS(NotPlaceable)
class CHRONOGAME_API AChronoEnvironmentActor : public AActor
{
	GENERATED_BODY()

public:
	AChronoEnvironmentActor();

	virtual void BeginPlay() override;

	/** Moves the sun. Pitch -90 is noon, values close to 0 are sunrise and sunset. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Environment")
	void SetSunAngles(float PitchDegrees, float YawDegrees);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UVolumetricCloudComponent> Clouds;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UPostProcessComponent> PostProcess;
};
