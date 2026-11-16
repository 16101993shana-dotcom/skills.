// Tunables of traffic, read from Config/DefaultGame.ini. Editable in
// Project Settings > Game > Chronopolis Traffic.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChronoTrafficSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Chronopolis Traffic"))
class CHRONOTRAFFIC_API UChronoTrafficSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UChronoTrafficSettings* Get() { return GetDefault<UChronoTrafficSettings>(); }

	// ---- Network ----

	/** Right-hand traffic (on) or left-hand traffic (off). */
	UPROPERTY(Config, EditAnywhere, Category = "Network")
	bool bDriveOnRight = true;

	/** Spacing of the points of a lane. */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "25.0"))
	float SampleSpacingCm = 200.0f;

	/** Speed inside a junction, in centimeters per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "30.0"))
	float TurnSpeedCmPerS = 300.0f;

	/** Two paths through a junction closer than this cannot be used at the same time. */
	UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "0.0"))
	float ConflictClearanceCm = 180.0f;

	// ---- Vehicles ----

	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "50.0"))
	float VehicleLengthCm = 400.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "50.0"))
	float VehicleWidthCm = 180.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "50.0"))
	float VehicleHeightCm = 160.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "30.0"))
	float VehicleMaxSpeedCmPerS = 1000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "10.0"))
	float MaxAccelerationCmPerS2 = 150.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "10.0"))
	float ComfortDecelerationCmPerS2 = 200.0f;

	/** Gap kept behind a stopped vehicle. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "10.0"))
	float MinGapCm = 150.0f;

	/** Time gap kept behind a moving vehicle, in seconds. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "0.1"))
	float TimeHeadwaySeconds = 1.2f;

	/** Distance from the end of a lane at which a waiting vehicle stops. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "0.0"))
	float StopMarginCm = 50.0f;

	/** A vehicle asks for its way through a junction from this distance. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "100.0"))
	float ReserveDistanceCm = 1500.0f;

	/** A vehicle that has not moved for this long is taken off the road and its trip counted as done. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "5.0"))
	float StuckTimeoutSeconds = 120.0f;

	/** Vehicles on the road at one time. Trips beyond that are resolved without driving. */
	UPROPERTY(Config, EditAnywhere, Category = "Vehicles", meta = (ClampMin = "0"))
	int32 MaxActiveVehicles = 20000;

	// ---- Trips ----

	/** Real seconds, at speed x1, of one day of traffic: every commuter drives to work and back once. */
	UPROPERTY(Config, EditAnywhere, Category = "Trips", meta = (ClampMin = "10.0"))
	float TrafficDaySeconds = 600.0f;

	/** Share of the commuters who drive as visible vehicles. The others are counted without driving. */
	UPROPERTY(Config, EditAnywhere, Category = "Trips", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TripSampleRate = 1.0f;

	/** A commute up to this long, in seconds, does not bother anybody. */
	UPROPERTY(Config, EditAnywhere, Category = "Trips", meta = (ClampMin = "0.0"))
	float ComfortableCommuteSeconds = 60.0f;

	/** Happiness lost by commuters per minute of commute beyond the comfortable time. */
	UPROPERTY(Config, EditAnywhere, Category = "Trips", meta = (ClampMin = "0.0"))
	float HappinessLostPerExtraMinute = 5.0f;

	/** Largest happiness loss that commuting can cause. */
	UPROPERTY(Config, EditAnywhere, Category = "Trips", meta = (ClampMin = "0.0"))
	float MaxCommuteHappinessLoss = 20.0f;
};
