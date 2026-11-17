// Render side of the buildings. A building whose definition has a finished
// model is drawn as an instance of that model (instanced static mesh, Nanite
// if the model supports it). The others are drawn as generated greybox
// volumes, one mesh per road segment.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChronoBuildingRenderActor.generated.h"

class UChronoBuildingSubsystem;
class UDynamicMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

UCLASS(NotPlaceable)
class CHRONOBUILDINGS_API AChronoBuildingRenderActor : public AActor
{
	GENERATED_BODY()

public:
	AChronoBuildingRenderActor();

	void InitializeFromSubsystem(UChronoBuildingSubsystem* InBuildings);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleBuildingsChanged(const TArray<int32>& SegmentIds);

	/** Rebuilds the greybox mesh of one segment. */
	void RebuildSegment(int32 SegmentId);

	/** Rebuilds every instance of every finished model. */
	void RebuildModelInstances();

	/** Finished model of a building kind, loaded once. Null for a greybox kind. */
	UStaticMesh* FindModel(FName DefId);

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDynamicMeshComponent>> SegmentComponents;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>> ModelComponents;

	/** Finished models by building kind. A null value means the kind has none. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UStaticMesh>> ModelsByDefId;

	/** Indexed by ChronoBuildingMaterialSlot. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	TWeakObjectPtr<UChronoBuildingSubsystem> Buildings;
	FDelegateHandle ChangedHandle;

	/** Segments waiting for a rebuild. The actor only ticks while work is pending. */
	TSet<int32> DirtySegments;
	bool bModelInstancesDirty = false;

	int32 MaxRebuildsPerFrame = 8;
	bool bFlipWinding = false;
};
