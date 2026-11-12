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

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	int32 GetNumBuildings() const { return Buildings.GetNumBuildings(); }

	/** Land value at a point, from 0 to the maximum set in the project settings. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Buildings")
	float GetLandValueAt(FVector2D WorldXY) const;

	/** Runs one growth step now, whatever the simulation speed. Returns the number of buildings created. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Buildings")
	int32 GrowNow(int32 MaxNewBuildings);

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

	int32 GrowthIntervalTicks = 5;
	int32 GrowthPerStep = 2;
	int32 GrowthSeed = 0;

	FDelegateHandle SimTickHandle;
	FDelegateHandle ZoningChangedHandle;
	FDelegateHandle TerrainChangedHandle;

	UPROPERTY(Transient)
	TObjectPtr<AChronoBuildingRenderActor> RenderActor;
};
