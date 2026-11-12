// Owns the road graph of the current world: validates and applies edits,
// shapes the terrain under the roads and tells the render actor what changed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGraph.h"
#include "ChronoRoadTypes.h"
#include "ChronoRoadSubsystem.generated.h"

class AChronoRoadNetworkActor;
DECLARE_MULTICAST_DELEGATE_OneParam(FChronoOnRoadNetworkChanged, const FChronoRoadChangeSet&);

UCLASS()
class CHRONOROADS_API UChronoRoadSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	const FChronoRoadGraph& GetGraph() const { return Graph; }

	/**
	 * Builds a road. Ends close to an existing node connect to it; ends close
	 * to an existing road split it and create an intersection. Nothing is
	 * built unless every rule passes: the result says which rule failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Roads")
	EChronoRoadBuildResult BuildRoad(const FChronoRoadBuildRequest& Request, int32& OutSegmentId);

	/** Same checks as BuildRoad, without building. Used for the placement preview. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Roads")
	EChronoRoadBuildResult CanBuildRoad(const FChronoRoadBuildRequest& Request) const;

	/** Removes the road nearest to a point. Returns false if none is close enough. */
	UFUNCTION(BlueprintCallable, Category = "Chronopolis|Roads")
	bool DemolishRoadAt(FVector2D WorldXY, float MaxDistanceCm);

	/** Player-facing explanation of a build result. */
	UFUNCTION(BlueprintPure, Category = "Chronopolis|Roads")
	static FText GetBuildResultText(EChronoRoadBuildResult Result);

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Roads")
	int32 GetNumSegments() const { return Graph.GetSegments().Num(); }

	UFUNCTION(BlueprintPure, Category = "Chronopolis|Roads")
	int32 GetNumNodes() const { return Graph.GetNodes().Num(); }

	FChronoOnRoadNetworkChanged OnRoadNetworkChanged;

	/** Save and load. Loading replaces the whole network. Returns false on invalid data. */
	bool SerializeRoads(FArchive& Ar);

private:
	/** Shapes the terrain, notifies listeners and clears the change tracking of the graph. */
	void CommitChanges();

	FChronoRoadRules Rules;
	FChronoRoadGraph Graph;

	UPROPERTY(Transient)
	TObjectPtr<AChronoRoadNetworkActor> NetworkActor;
};
