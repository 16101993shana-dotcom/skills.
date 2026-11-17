#include "ChronoRoadNetworkActor.h"

#include "ChronoMeshConversion.h"
#include "ChronoMeshData.h"
#include "ChronoRoadMeshBuilder.h"
#include "ChronoRoadSubsystem.h"
#include "ChronoSettings.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"

AChronoRoadNetworkActor::AChronoRoadNetworkActor()
{
	// Ticks only while meshes are waiting for a rebuild.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AChronoRoadNetworkActor::InitializeFromSubsystem(UChronoRoadSubsystem* InRoads)
{
	if (!InRoads || Roads.IsValid())
	{
		return;
	}

	Roads = InRoads;

	const UChronoSettings* Settings = UChronoSettings::Get();
	MaxRebuildsPerFrame = FMath::Max(1, Settings->MaxRoadMeshRebuildsPerFrame);
	SampleSpacingCm = Settings->RoadSampleSpacingCm;
	SkirtDepthCm = Settings->RoadSkirtDepthCm;
	bFlipWinding = Settings->bFlipTerrainWinding;

	Materials.SetNum(ChronoRoadMaterialSlot::Count);
	Materials[ChronoRoadMaterialSlot::Carriageway] = Settings->RoadSurfaceMaterial.LoadSynchronous();
	Materials[ChronoRoadMaterialSlot::Sidewalk] = Settings->RoadSidewalkMaterial.LoadSynchronous();
	Materials[ChronoRoadMaterialSlot::Side] = Settings->RoadSideMaterial.LoadSynchronous();

	ChangedHandle = InRoads->OnRoadNetworkChanged.AddUObject(this, &AChronoRoadNetworkActor::HandleRoadNetworkChanged);

	// Roads that already exist (after loading a game).
	FChronoRoadChangeSet Existing;
	InRoads->GetGraph().GetSegments().GetKeys(Existing.ChangedSegments);
	InRoads->GetGraph().GetNodes().GetKeys(Existing.ChangedNodes);
	HandleRoadNetworkChanged(Existing);
}

void AChronoRoadNetworkActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UChronoRoadSubsystem* RoadsPtr = Roads.Get())
	{
		RoadsPtr->OnRoadNetworkChanged.Remove(ChangedHandle);
	}
	ChangedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

void AChronoRoadNetworkActor::HandleRoadNetworkChanged(const FChronoRoadChangeSet& Changes)
{
	for (const int32 SegmentId : Changes.RemovedSegments)
	{
		DirtySegments.Remove(SegmentId);
		DestroyComponentFor(SegmentComponents, SegmentId);
	}
	for (const int32 NodeId : Changes.RemovedNodes)
	{
		DirtyNodes.Remove(NodeId);
		DestroyComponentFor(NodeComponents, NodeId);
	}

	DirtySegments.Append(Changes.ChangedSegments);
	DirtyNodes.Append(Changes.ChangedNodes);

	if (DirtySegments.Num() > 0 || DirtyNodes.Num() > 0)
	{
		SetActorTickEnabled(true);
	}
}

void AChronoRoadNetworkActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	int32 Rebuilt = 0;
	for (auto It = DirtySegments.CreateIterator(); It && Rebuilt < MaxRebuildsPerFrame; ++It)
	{
		RebuildSegment(*It);
		It.RemoveCurrent();
		++Rebuilt;
	}
	for (auto It = DirtyNodes.CreateIterator(); It && Rebuilt < MaxRebuildsPerFrame; ++It)
	{
		RebuildNode(*It);
		It.RemoveCurrent();
		++Rebuilt;
	}

	if (DirtySegments.Num() == 0 && DirtyNodes.Num() == 0)
	{
		SetActorTickEnabled(false);
	}
}

void AChronoRoadNetworkActor::RebuildSegment(int32 SegmentId)
{
	const UChronoRoadSubsystem* RoadsPtr = Roads.Get();
	if (!RoadsPtr)
	{
		return;
	}

	const FChronoRoadGraph& Graph = RoadsPtr->GetGraph();
	const FChronoRoadSegment* Segment = Graph.FindSegment(SegmentId);
	const FChronoRoadNode* StartNode = Segment ? Graph.FindNode(Segment->StartNodeId) : nullptr;
	if (!StartNode)
	{
		DestroyComponentFor(SegmentComponents, SegmentId);
		return;
	}

	FChronoRoadMeshSettings MeshSettings;
	MeshSettings.SampleSpacingCm = SampleSpacingCm;
	MeshSettings.SkirtDepthCm = SkirtDepthCm;

	// Vertices are stored relative to the start node to keep float precision.
	const FVector Origin = StartNode->Position;
	FChronoMeshData MeshData;
	ChronoRoadMeshBuilder::BuildSegmentMesh(Graph, SegmentId, MeshSettings, Origin, MeshData);
	ApplyMesh(SegmentComponents, SegmentId, MeshData, Origin);
}

void AChronoRoadNetworkActor::RebuildNode(int32 NodeId)
{
	const UChronoRoadSubsystem* RoadsPtr = Roads.Get();
	if (!RoadsPtr)
	{
		return;
	}

	const FChronoRoadGraph& Graph = RoadsPtr->GetGraph();
	const FChronoRoadNode* Node = Graph.FindNode(NodeId);
	if (!Node)
	{
		DestroyComponentFor(NodeComponents, NodeId);
		return;
	}

	FChronoRoadMeshSettings MeshSettings;
	MeshSettings.SampleSpacingCm = SampleSpacingCm;
	MeshSettings.SkirtDepthCm = SkirtDepthCm;

	const FVector Origin = Node->Position;
	FChronoMeshData MeshData;
	ChronoRoadMeshBuilder::BuildNodeMesh(Graph, NodeId, MeshSettings, Origin, MeshData);
	ApplyMesh(NodeComponents, NodeId, MeshData, Origin);
}

void AChronoRoadNetworkActor::ApplyMesh(TMap<int32, TObjectPtr<UDynamicMeshComponent>>& Components, int32 Id, const FChronoMeshData& MeshData, const FVector& Origin)
{
	if (MeshData.IsEmpty())
	{
		// Dead end node, or segment entirely covered by its junctions.
		DestroyComponentFor(Components, Id);
		return;
	}

	UDynamicMeshComponent* Component = Components.FindRef(Id);
	if (!Component)
	{
		Component = NewObject<UDynamicMeshComponent>(this);
		Component->SetupAttachment(RootComponent);
		// Roads are picked through the road graph, not through collision.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->RegisterComponent();

		for (int32 Slot = 0; Slot < Materials.Num(); ++Slot)
		{
			if (Materials[Slot])
			{
				Component->SetMaterial(Slot, Materials[Slot]);
			}
		}

		Components.Add(Id, Component);
	}

	Component->SetWorldLocation(Origin);

	ChronoMeshConversion::ApplyToComponent(MeshData, bFlipWinding, *Component);
}

void AChronoRoadNetworkActor::DestroyComponentFor(TMap<int32, TObjectPtr<UDynamicMeshComponent>>& Components, int32 Id)
{
	TObjectPtr<UDynamicMeshComponent> Component;
	if (Components.RemoveAndCopyValue(Id, Component) && Component)
	{
		Component->DestroyComponent();
	}
}
