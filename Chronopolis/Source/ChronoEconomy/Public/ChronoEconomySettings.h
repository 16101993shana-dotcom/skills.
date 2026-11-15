// Tunables of the economy, read from Config/DefaultGame.ini. Editable in
// Project Settings > Game > Chronopolis Economy.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChronoEconomySettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Chronopolis Economy"))
class CHRONOECONOMY_API UChronoEconomySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UChronoEconomySettings* Get() { return GetDefault<UChronoEconomySettings>(); }

	UPROPERTY(Config, EditAnywhere, Category = "Budget")
	double StartingTreasury = 20000.0;

	/** Highest tax rate the player may set, as a fraction (0.3 = 30 %). */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxTaxRate = 0.3f;

	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1"))
	int32 DaysPerMonth = 30;

	/** Daily income of an employed resident, taxed at the residential rate. */
	UPROPERTY(Config, EditAnywhere, Category = "Income", meta = (ClampMin = "0.0"))
	float DailyWage = 1.0f;

	/** Daily output of a worker, taxed at the rate of the zone. */
	UPROPERTY(Config, EditAnywhere, Category = "Income", meta = (ClampMin = "0.0"))
	float OutputPerCommercialWorker = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Income", meta = (ClampMin = "0.0"))
	float OutputPerIndustrialWorker = 2.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Income", meta = (ClampMin = "0.0"))
	float OutputPerAgriculturalWorker = 1.5f;

	/** Daily upkeep of one square meter of road. */
	UPROPERTY(Config, EditAnywhere, Category = "Expenses", meta = (ClampMin = "0.0"))
	float RoadUpkeepPerM2PerDay = 0.0008f;

	/** Price of one square meter of new road. */
	UPROPERTY(Config, EditAnywhere, Category = "Expenses", meta = (ClampMin = "0.0"))
	float RoadCostPerM2 = 0.5f;

	/** Most the city may owe at one time, interest included. */
	UPROPERTY(Config, EditAnywhere, Category = "Loans", meta = (ClampMin = "0.0"))
	double MaxDebt = 50000.0;

	/** Total interest of a loan, as a fraction of the amount borrowed (0.1 = 10 %). */
	UPROPERTY(Config, EditAnywhere, Category = "Loans", meta = (ClampMin = "0.0"))
	float LoanInterestRate = 0.1f;

	/** Days over which a loan is repaid. */
	UPROPERTY(Config, EditAnywhere, Category = "Loans", meta = (ClampMin = "1"))
	int32 LoanTermDays = 360;

	UPROPERTY(Config, EditAnywhere, Category = "Progress", meta = (ClampMin = "0.0"))
	float SciencePerThousandCitizensPerDay = 0.1f;

	UPROPERTY(Config, EditAnywhere, Category = "Progress", meta = (ClampMin = "0.0"))
	float SciencePerCivicWorkerPerDay = 0.02f;

	UPROPERTY(Config, EditAnywhere, Category = "Progress", meta = (ClampMin = "0.0"))
	float CulturePerThousandCitizensPerDay = 0.1f;

	UPROPERTY(Config, EditAnywhere, Category = "Progress", meta = (ClampMin = "0.0"))
	float CulturePerCivicWorkerPerDay = 0.02f;
};
