#include "ChronoTrafficRenderActor.h"

#include "ChronoSimulationSubsystem.h"
#include "ChronoTrafficSettings.h"
#include "ChronoTrafficSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

AChronoTrafficRenderActor::AChronoTrafficRenderActor()
{
	// Vehicles move every frame.
	PrimaryActorTick.bCanEverTick = true;

	VehicleInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VehicleInstances"));
	RootComponent = VehicleInstances;

	// Vehicles are found through the simulation, not through collision.
	VehicleInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AChronoTrafficRenderActor::InitializeFromSubsystem(UChronoTrafficSubsystem* InTraffic)
{
	Traffic = InTraffic;

	const UChronoTrafficSettings* Settings = UChronoTrafficSettings::Get();
	VehicleHeightCm = Settings->VehicleHeightCm;

	// The engine's basic cube is 100 cm on each side.
	VehicleScale = FVector(Settings->VehicleLengthCm / 100.0, Settings->VehicleWidthCm / 100.0, Settings->VehicleHeightCm / 100.0);
	PedestrianHeightCm = Settings->PedestrianHeightCm;
	PedestrianScale = FVector(Settings->PedestrianLengthCm / 100.0, Settings->PedestrianWidthCm / 100.0, Settings->PedestrianHeightCm / 100.0);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		VehicleInstances->SetStaticMesh(Cube);
	}
}

void AChronoTrafficRenderActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UChronoTrafficSubsystem* TrafficPtr = Traffic.Get();
	if (!TrafficPtr)
	{
		return;
	}

	const FChronoTrafficSim& Sim = TrafficPtr->GetSim();
	const TArray<FChronoVehicle>& Vehicles = Sim.GetVehicles();

	// One instance per vehicle slot. Slots are reused by the simulation, so
	// the number of instances only grows to the peak number of vehicles.
	while (VehicleInstances->GetInstanceCount() < Vehicles.Num())
	{
		VehicleInstances->AddInstance(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector), true);
	}

	// The simulation advances in fixed steps; between two steps each vehicle
	// is drawn a little ahead, by its own speed, so motion looks continuous.
	float Lead = 0.0f;
	if (const UChronoSimulationSubsystem* Simulation = GetWorld() ? GetWorld()->GetSubsystem<UChronoSimulationSubsystem>() : nullptr)
	{
		Lead = Simulation->GetInterpolationAlpha() * Simulation->GetFixedDeltaSeconds();
	}

	const int32 NumInstances = VehicleInstances->GetInstanceCount();
	for (int32 Index = 0; Index < NumInstances; ++Index)
	{
		FVector Position;
		FVector Direction;
		if (Index < Vehicles.Num() && Sim.GetVehicleTransform(Index, Position, Direction))
		{
			Position += Direction * (Vehicles[Index].Speed * Lead);

			// The lane gives the surface (road, sidewalk or verge): the box
			// sits on it. Walkers are the same box, at their own size.
			const bool bPedestrian = Vehicles[Index].bPedestrian;
			Position.Z += 0.5 * (bPedestrian ? PedestrianHeightCm : VehicleHeightCm);
			const FTransform Transform(Direction.Rotation(), Position, bPedestrian ? PedestrianScale : VehicleScale);
			VehicleInstances->UpdateInstanceTransform(Index, Transform, true, false, true);
		}
		else
		{
			// Free slot: an instance of size zero draws nothing.
			VehicleInstances->UpdateInstanceTransform(Index, FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector), true, false, true);
		}
	}

	VehicleInstances->MarkRenderStateDirty();
}
