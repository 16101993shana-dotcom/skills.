// Draws every vehicle and every pedestrian as an instance of one mesh (a box
// for now, at the size of each), moved each frame to where the simulation
// puts it. One draw call for all of them.
//
// This is the simple, reliable way to see traffic. The plan calls for Mass
// Entity with three levels of detail; that replaces this actor once the
// project builds in the engine, without touching the simulation.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChronoTrafficRenderActor.generated.h"

class UChronoTrafficSubsystem;
class UInstancedStaticMeshComponent;

UCLASS(NotPlaceable)
class CHRONOTRAFFIC_API AChronoTrafficRenderActor : public AActor
{
	GENERATED_BODY()

public:
	AChronoTrafficRenderActor();

	void InitializeFromSubsystem(UChronoTrafficSubsystem* InTraffic);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UInstancedStaticMeshComponent> VehicleInstances;

	TWeakObjectPtr<UChronoTrafficSubsystem> Traffic;

	/** Size of a vehicle, in units of the 100 cm box mesh. */
	FVector VehicleScale = FVector(4.0, 1.8, 1.6);
	float VehicleHeightCm = 160.0f;

	/** Same for a pedestrian. */
	FVector PedestrianScale = FVector(0.5, 0.5, 1.7);
	float PedestrianHeightCm = 170.0f;
};
