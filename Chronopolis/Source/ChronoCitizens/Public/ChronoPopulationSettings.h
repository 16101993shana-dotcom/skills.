// Tunables of the population simulation, read from Config/DefaultGame.ini.
// Editable in Project Settings > Game > Chronopolis Population. These are the
// values of a generic era; Phase 5 will let each era override them.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChronoPopulationSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Chronopolis Population"))
class CHRONOCITIZENS_API UChronoPopulationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UChronoPopulationSettings* Get() { return GetDefault<UChronoPopulationSettings>(); }

	// ---- Calendar ----

	/** Simulation ticks in one day of the city. */
	UPROPERTY(Config, EditAnywhere, Category = "Calendar", meta = (ClampMin = "1"))
	int32 TicksPerDay = 10;

	/** Seed of the population random stream: same seed, same history. */
	UPROPERTY(Config, EditAnywhere, Category = "Calendar")
	int32 Seed = 9001;

	// ---- Capacities: floor area needed per household and per job ----

	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "5.0"))
	float AreaPerHouseholdM2 = 70.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "5.0"))
	float AreaPerCommercialJobM2 = 40.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "5.0"))
	float AreaPerIndustrialJobM2 = 60.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "5.0"))
	float AreaPerAgriculturalJobM2 = 120.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Capacity", meta = (ClampMin = "5.0"))
	float AreaPerCivicJobM2 = 50.0f;

	// ---- Life ----

	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "0.0"))
	float WorkingAgeMin = 16.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "1.0"))
	float RetirementAge = 65.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "0.0"))
	float FertileAgeMin = 18.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "0.0"))
	float FertileAgeMax = 45.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "1.0"))
	float MaxAge = 100.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MaxHouseholdSize = 6;

	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "0.0"))
	float BirthsPerHouseholdPerYear = 0.12f;

	/** Chance of dying in a year before MortalityStartAge. */
	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "0.0"))
	float BaseAnnualMortality = 0.004f;

	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "0.0"))
	float MortalityStartAge = 40.0f;

	/** After MortalityStartAge, mortality doubles every this many years. */
	UPROPERTY(Config, EditAnywhere, Category = "Life", meta = (ClampMin = "0.1"))
	float MortalityDoublingYears = 9.0f;

	// ---- Moving in and out ----

	/** Share of the free homes filled per day when the city is fully attractive. */
	UPROPERTY(Config, EditAnywhere, Category = "Migration", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ImmigrationRatePerDay = 0.04f;

	/** Attractiveness of a city without a single free job: settlers still come. */
	UPROPERTY(Config, EditAnywhere, Category = "Migration", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinAttractiveness = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category = "Migration", meta = (ClampMin = "1"))
	int32 JoblessDaysBeforeLeaving = 90;

	UPROPERTY(Config, EditAnywhere, Category = "Migration", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float UnhappyThreshold = 25.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Migration", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EmigrationChancePerDay = 0.05f;

	// ---- Happiness, from 0 to 100 ----

	UPROPERTY(Config, EditAnywhere, Category = "Happiness", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float HappinessBase = 50.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Happiness", meta = (ClampMin = "0.0"))
	float EmployedBonus = 15.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Happiness", meta = (ClampMin = "0.0"))
	float UnemployedPenalty = 25.0f;

	/** Happiness lost per percentage point of residential tax above the neutral rate. */
	UPROPERTY(Config, EditAnywhere, Category = "Happiness", meta = (ClampMin = "0.0"))
	float TaxPenaltyPerPoint = 2.0f;

	/** Tax rate that nobody minds, as a fraction (0.09 = 9 %). */
	UPROPERTY(Config, EditAnywhere, Category = "Happiness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NeutralTaxRate = 0.09f;

	UPROPERTY(Config, EditAnywhere, Category = "Happiness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HappinessAdjustPerDay = 0.1f;

	// ---- Demand ----

	UPROPERTY(Config, EditAnywhere, Category = "Demand", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetCommercialJobShare = 0.35f;

	UPROPERTY(Config, EditAnywhere, Category = "Demand", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetIndustrialJobShare = 0.30f;

	UPROPERTY(Config, EditAnywhere, Category = "Demand", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetAgriculturalJobShare = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category = "Demand", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetCivicJobShare = 0.10f;

	/** Demand lost per unit of tax rate above the neutral rate. */
	UPROPERTY(Config, EditAnywhere, Category = "Demand", meta = (ClampMin = "0.0"))
	float DemandTaxSensitivity = 4.0f;
};
