#include "ChronoTerrainSubsystem.h"

#include "ChronoCore.h"
#include "ChronoSettings.h"
#include "ChronoTerrainActor.h"
#include "Engine/World.h"

void UChronoTerrainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UChronoSettings* Settings = UChronoSettings::Get();
	MinHeightCm = FMath::Min(Settings->TerrainMinHeightCm, Settings->TerrainMaxHeightCm);
	MaxHeightCm = FMath::Max(Settings->TerrainMinHeightCm, Settings->TerrainMaxHeightCm);

	// Pure data: generated here so every other system can query heights
	// before any actor begins play.
	GenerateInitialTerrain(Settings->TerrainNoiseSeed);
}

bool UChronoTerrainSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChronoTerrainSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TerrainActor = InWorld.SpawnActor<AChronoTerrainActor>(AChronoTerrainActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (TerrainActor)
	{
		TerrainActor->InitializeFromSubsystem(this);
	}
	else
	{
		UE_LOG(LogChrono, Error, TEXT("Failed to spawn the terrain actor"));
	}
}

float UChronoTerrainSubsystem::GetHeightAt(FVector2D WorldXY) const
{
	return Heightfield.SampleHeight(WorldXY);
}

float UChronoTerrainSubsystem::GetSlopeDegreesAt(FVector2D WorldXY) const
{
	return Heightfield.SampleSlopeDegrees(WorldXY);
}

float UChronoTerrainSubsystem::GetWorldHalfSizeCm() const
{
	return 0.5f * Heightfield.GetWorldSizeCm();
}

void UChronoTerrainSubsystem::RaiseTerrain(FVector2D Center, float RadiusCm, float DeltaCm)
{
	NotifyChanged(Heightfield.ApplyRaiseBrush(Center, RadiusCm, DeltaCm, MinHeightCm, MaxHeightCm));
}

void UChronoTerrainSubsystem::FlattenTerrain(FVector2D Center, float RadiusCm, float TargetHeightCm, float Strength)
{
	const float Target = FMath::Clamp(TargetHeightCm, MinHeightCm, MaxHeightCm);
	NotifyChanged(Heightfield.ApplyFlattenBrush(Center, RadiusCm, Target, Strength));
}

void UChronoTerrainSubsystem::SmoothTerrain(FVector2D Center, float RadiusCm, float Strength)
{
	NotifyChanged(Heightfield.ApplySmoothBrush(Center, RadiusCm, Strength));
}

void UChronoTerrainSubsystem::FlattenFootprint(FVector2D Center, FVector2D HalfExtentCm, float YawDegrees, float TargetHeightCm, float SkirtCm)
{
	const float Target = FMath::Clamp(TargetHeightCm, MinHeightCm, MaxHeightCm);
	NotifyChanged(Heightfield.FlattenOrientedBox(Center, HalfExtentCm, FMath::DegreesToRadians(YawDegrees), Target, SkirtCm));
}

void UChronoTerrainSubsystem::ConformToPath(TConstArrayView<FVector> Path, float HalfWidthCm, float OffsetBelowCm, float BlendCm, EChronoConformMode Mode)
{
	NotifyChanged(Heightfield.ConformToPath(Path, HalfWidthCm, OffsetBelowCm, BlendCm, Mode));
}

bool UChronoTerrainSubsystem::RaycastTerrain(FVector Origin, FVector Direction, float MaxDistanceCm, FVector& OutHit) const
{
	OutHit = FVector::ZeroVector;

	if (!Heightfield.IsValid() || !Direction.Normalize())
	{
		return false;
	}

	// Skip the part of the ray that is above the highest possible ground.
	double StartT = 0.0;
	if (Origin.Z > MaxHeightCm)
	{
		if (Direction.Z >= 0.0)
		{
			return false;
		}
		StartT = (Origin.Z - MaxHeightCm) / -Direction.Z;
	}

	if (StartT > MaxDistanceCm)
	{
		return false;
	}

	auto HeightAbove = [this, &Origin, &Direction](double T)
	{
		const FVector Point = Origin + Direction * T;
		return Point.Z - Heightfield.SampleHeight(FVector2D(Point.X, Point.Y));
	};

	// The ray must start above the ground.
	if (HeightAbove(StartT) < 0.0)
	{
		return false;
	}

	// March with a step smaller than a cell, then refine by bisection.
	const double Step = 0.5 * Heightfield.GetCellSizeCm();
	double PreviousT = StartT;
	for (double T = StartT + Step; T <= MaxDistanceCm; T += Step)
	{
		if (HeightAbove(T) <= 0.0)
		{
			double Low = PreviousT;
			double High = T;
			for (int32 Iteration = 0; Iteration < 16; ++Iteration)
			{
				const double Mid = 0.5 * (Low + High);
				if (HeightAbove(Mid) > 0.0)
				{
					Low = Mid;
				}
				else
				{
					High = Mid;
				}
			}

			const FVector Point = Origin + Direction * High;
			const FVector2D PointXY(Point.X, Point.Y);
			if (!Heightfield.IsInsideWorld(PointXY))
			{
				return false;
			}

			OutHit = FVector(Point.X, Point.Y, Heightfield.SampleHeight(PointXY));
			return true;
		}

		PreviousT = T;
	}

	return false;
}

void UChronoTerrainSubsystem::GenerateInitialTerrain(int32 Seed)
{
	const UChronoSettings* Settings = UChronoSettings::Get();
	Heightfield.Init(Settings->TerrainSizeCells, Settings->TerrainCellSizeCm, 0.0f);

	const float Amplitude = Settings->TerrainNoiseAmplitudeCm;
	if (Amplitude > 0.0f)
	{
		const double Wavelength = FMath::Max(100.0f, Settings->TerrainNoiseWavelengthCm);

		// Deterministic offset derived from the seed, so the same seed gives the same map.
		FRandomStream Stream(Seed);
		const FVector2D Offset(Stream.FRandRange(-1000.0f, 1000.0f), Stream.FRandRange(-1000.0f, 1000.0f));

		const int32 NumVerts = Heightfield.GetNumVertsPerSide();
		for (int32 Y = 0; Y < NumVerts; ++Y)
		{
			for (int32 X = 0; X < NumVerts; ++X)
			{
				const FVector2D World = Heightfield.VertexToWorld(X, Y);
				const FVector2D P = World / Wavelength + Offset;

				// Two octaves of Perlin noise: broad relief plus gentle detail.
				const float Noise = FMath::PerlinNoise2D(P) + 0.35f * FMath::PerlinNoise2D(P * 3.1);
				Heightfield.SetVertexHeight(X, Y, FMath::Clamp(Noise * Amplitude, MinHeightCm, MaxHeightCm));
			}
		}
	}

	NotifyChanged(GetFullRect());
}

bool UChronoTerrainSubsystem::SerializeTerrain(FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		FChronoHeightfield Loaded;
		Loaded.Serialize(Ar);
		if (Ar.IsError() || !Loaded.IsValid())
		{
			UE_LOG(LogChrono, Error, TEXT("Terrain data is invalid, keeping the current terrain"));
			return false;
		}

		// The render tiles are laid out for the size chosen at startup.
		if (Loaded.GetSizeCells() != Heightfield.GetSizeCells() || !FMath::IsNearlyEqual(Loaded.GetCellSizeCm(), Heightfield.GetCellSizeCm()))
		{
			UE_LOG(LogChrono, Error, TEXT("Saved terrain size (%d cells) does not match the current world (%d cells)"), Loaded.GetSizeCells(), Heightfield.GetSizeCells());
			return false;
		}

		Heightfield = MoveTemp(Loaded);
		NotifyChanged(GetFullRect());
		return true;
	}

	Heightfield.Serialize(Ar);
	return !Ar.IsError();
}

void UChronoTerrainSubsystem::NotifyChanged(const FIntRect& DirtyVertexRect)
{
	if (DirtyVertexRect.Width() > 0 && DirtyVertexRect.Height() > 0)
	{
		OnTerrainChanged.Broadcast(DirtyVertexRect);
	}
}

FIntRect UChronoTerrainSubsystem::GetFullRect() const
{
	const int32 NumVerts = Heightfield.GetNumVertsPerSide();
	return FIntRect(0, 0, NumVerts, NumVerts);
}
