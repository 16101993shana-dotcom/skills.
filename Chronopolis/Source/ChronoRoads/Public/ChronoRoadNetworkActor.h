// Render side of the road network: one dynamic mesh component per segment and
// per junction, rebuilt only when the graph says it changed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChronoRoadTypes.h"
#include "ChronoRoadNetworkActor.generated.h"

class UChronoRoadSubsystem;
class UDynamicMeshComponent;
class UMaterialInterface;
struct FChronoMeshData;

UCLASS(NotPlaceable)
class CHRONOROADS_API AChronoRoadNetworkActor : public AActor
{
	GENERATED_BODY()

public:
	AChronoRoadNetworkActor();

	void InitializeFromSubsystem(UChronoRoadSubsystem* InRoads);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleRoadNetworkChanged(const FChronoRoadChangeSet& Changes);

	void RebuildSegment(int32 SegmentId);
	void RebuildNode(int32 NodeId);

	/** Creates, updates or destroys the component stored under Id in Components. */
	void ApplyMesh(TMap<int32, TObjectPtr<UDynamicMeshComponent>>& Components, int32 Id, const FChronoMeshData& MeshData, const FVector& Origin);
	void DestroyComponentFor(TMap<int32, TObjectPtr<UDynamicMeshComponent>>& Components, int32 Id);

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDynamicMeshComponent>> SegmentComponents;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDynamicMeshComponent>> NodeComponents;

	/** Indexed by ChronoRoadMaterialSlot. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	TWeakObjectPtr<UChronoRoadSubsystem> Roads;
	FDelegateHandle ChangedHandle;

	/** Meshes waiting for a rebuild. The actor only ticks while these are not empty. */
	TSet<int32> DirtySegments;
	TSet<int32> DirtyNodes;

	int32 MaxRebuildsPerFrame = 8;
	double SampleSpacingCm = 200.0;
	float SkirtDepthCm = 40.0f;
	bool bFlipWinding = false;
};
