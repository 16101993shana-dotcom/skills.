// Owns the buildings of the current world: lets them grow on zoned cells at
// the pace of the simulation, removes them when their lot goes away, and tells
// the render actor what changed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoBuildingSet.h"
#include "ChronoBuildingTypes.h"
#include "ChronoBuildingSubsystem.generated.h"

class AChronoBuildingRenderActor;

/** Ids of the road segments whose buildings changed, sorted. */
DECLARE_MULTICAST_DELEGATE_OneParam(FChronoOnBuildingsChanged, const TArray<int32>&);

UCLASS()
class CHRONOBUILDINGS_API UChronoBuildingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	const FChronoBuildingSet& GetBuildingSet() const { return Buildings; }
	const TArray<FChronoBuildingDefData>& GetDefinitions() const { return Definitions; }

	/** Definition of a building kind, or null if it is no longer in the project settings. */
	const FChronoBuildingDefData* FindDefinition(FName DefId) const;

	/** The definitions the city can build now, given its civilization, era and unlocks. */
	const TArray<FChronoBuildingDefData>& GetActiveDefinitions() const { return ActiveDefinitions; }

	/**
	 * What the city can build, pushed by the civilization system when the
	 * game starts, when something is unlocked and when an era begins. New
	 * buildings take the style of that civilization and era, and older ones
	 * are rebuilt in it little by little, except heritage buildings.
	 */
	void SetAvailability(const FChronoBuildingAvailability& InAvailability);
	const FChronoBuildingAvailability& GetAvailability() const { return Availability; }

	/**
	 * Bonuses of the civilization: price of the buildings the player places,
	 * and reach of its water services.
	 */
	void SetCivilizationModifiers(float InConstructionCostMultiplier, float InWaterRadiusMultiplier);

	/** Lists the building whose walls contain a point as heritage, or takes it off the list. False if there is none. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Buildings")
	bool SetHeritageAt(FVector2D WorldXY, bool bHeritage);

	/** True if the building whose walls contain a point is heritage. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	bool IsHeritageAt(FVector2D WorldXY) const;

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	int32 GetNumHeritage() const { return Buildings.CountHeritage(); }

	/** Grown buildings of an earlier era, still waiting to be rebuilt in the current style. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	int32 GetNumOutdated() const { return Buildings.CountOutdated(); }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	int32 GetNumBuildings() const { return Buildings.GetNumBuildings(); }

	/** Land value at a point, from 0 to the maximum set in the project settings. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	float GetLandValueAt(FVector2D WorldXY) const;

	/**
	 * Places a service building chosen by the player on the lot under a
	 * point. Paying for it is the caller's business (see UChronoEconomySubsystem).
	 */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Buildings")
	EChronoPlaceResult PlaceServiceBuilding(FName DefId, FVector2D WorldXY, int32& OutBuildingId);

	/** Removes the building whose walls contain a point. Returns false if there is none. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Buildings")
	bool DemolishBuildingAt(FVector2D WorldXY);

	/** Identifiers of the buildings the player can place, in the order of the project settings. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	TArray<FName> GetServiceBuildingIds() const;

	/** Price of a building the player can place (0 for an unknown one). */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	float GetConstructionCost(FName DefId) const;

	/** Player-facing explanation of a placement result. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	static FText GetPlaceResultText(EChronoPlaceResult Result);

	/** Runs one growth step now, whatever the simulation speed. Returns the number of buildings created. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Buildings")
	int32 GrowNow(int32 MaxNewBuildings);

	/**
	 * Demand per zone, pushed by the population simulation once a day. Until
	 * it is set, buildings grow on any zoned lot.
	 */
	void SetZoneDemand(const FChronoZoneDemand& InDemand);

	/** Demand for a zone, from -1 to +1 (1 while no demand has been set yet). */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	float GetZoneDemand(EChronoZoneType Zone) const;

	/** Save and load. Loading replaces every building. Returns false on invalid data. */
	bool SerializeBuildings(FArchive& Ar);

	FChronoOnBuildingsChanged OnBuildingsChanged;

private:
	void HandleSimTick(int64 TickIndex, float FixedDeltaSeconds);
	void HandleZoningChanged(const TArray<int32>& SegmentIds);
	void HandleTerrainChanged(const FIntRect& DirtyVertexRect);
	int32 GrowStep(int64 TickIndex, int32 MaxNewBuildings);
	void BroadcastChanges();

	FChronoBuildingSet Buildings;
	FChronoBuildingRules Rules;
	TArray<FChronoBuildingDefData> Definitions;
	TArray<FChronoBuildingDefData> ActiveDefinitions;
	FChronoBuildingAvailability Availability;
	float ConstructionCostMultiplier = 1.0f;
	float WaterRadiusMultiplier = 1.0f;
	int32 RenewalsPerStep = 1;

	FChronoZoneDemand ZoneDemand;
	bool bHasZoneDemand = false;

	int32 GrowthIntervalTicks = 5;
	int32 GrowthPerStep = 2;
	int32 UpgradesPerStep = 1;
	int32 GrowthSeed = 0;

	FDelegateHandle SimTickHandle;
	FDelegateHandle ZoningChangedHandle;
	FDelegateHandle TerrainChangedHandle;

	UPROPERTY(Transient)
	TObjectPtr<AChronoBuildingRenderActor> RenderActor;
};
