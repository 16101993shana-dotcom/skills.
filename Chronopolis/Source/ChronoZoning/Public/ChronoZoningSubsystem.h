// Owns the zoning grid of the current world: keeps it in step with the road
// network, applies the player's zone painting and tells the overlay what
// changed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoZoneGrid.h"
#include "ChronoZoningSubsystem.generated.h"

class AChronoZoneOverlayActor;
struct FChronoRoadChangeSet;

/** Ids of the road segments whose cells changed, sorted. */
DECLARE_MULTICAST_DELEGATE_OneParam(FChronoOnZoningChanged, const TArray<int32>&);

UCLASS()
class CHRONOZONING_API UChronoZoningSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	const FChronoZoneGrid& GetGrid() const { return Grid; }

	/** Sets the zone of every cell within RadiusCm of a point. Returns the number of cells changed. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Zoning")
	int32 PaintZone(FVector2D Center, float RadiusCm, EChronoZoneType Zone);

	/** Zone of the cell under a point (None if there is no cell). */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Zoning")
	EChronoZoneType GetZoneAt(FVector2D WorldXY) const;

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Zoning")
	int32 GetNumCells() const { return Grid.GetNumCells(); }

	/** Shows or hides the zoning overlay. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Zoning")
	void SetOverlayVisible(bool bVisible);

	/** Throws the grid away and lays every cell out again (painted zones are kept where possible). */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Zoning")
	void RebuildAll();

	/** Save and load. Loading replaces the whole grid. Returns false on invalid data. */
	bool SerializeZoning(FArchive& Ar);

	FChronoOnZoningChanged OnZoningChanged;

private:
	void HandleRoadNetworkChanged(const FChronoRoadChangeSet& Changes);
	void BroadcastChanges();

	FChronoZoneGrid Grid;
	FChronoZoningRules Rules;
	FDelegateHandle RoadsChangedHandle;

	UPROPERTY(Transient)
	TObjectPtr<AChronoZoneOverlayActor> OverlayActor;
};
