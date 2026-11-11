#include "ChronoHeightfield.h"

namespace ChronoHeightfieldPrivate
{
	// Bump when the binary layout changes.
	const int32 SerializationVersion = 1;
}

void FChronoHeightfield::Init(int32 InSizeCells, float InCellSizeCm, float InitialHeightCm)
{
	SizeCells = FMath::Max(1, InSizeCells);
	CellSizeCm = FMath::Max(1.0f, InCellSizeCm);

	const int32 NumVerts = GetNumVertsPerSide();
	Heights.Init(InitialHeightCm, NumVerts * NumVerts);
}

bool FChronoHeightfield::IsInsideWorld(const FVector2D& WorldXY) const
{
	const FVector2D Origin = GetOrigin();
	const double Size = GetWorldSizeCm();
	return WorldXY.X >= Origin.X && WorldXY.Y >= Origin.Y && WorldXY.X <= Origin.X + Size && WorldXY.Y <= Origin.Y + Size;
}

FVector2D FChronoHeightfield::ClampToWorld(const FVector2D& WorldXY) const
{
	const FVector2D Origin = GetOrigin();
	const double Size = GetWorldSizeCm();
	return FVector2D(
		FMath::Clamp(WorldXY.X, Origin.X, Origin.X + Size),
		FMath::Clamp(WorldXY.Y, Origin.Y, Origin.Y + Size));
}

FVector2D FChronoHeightfield::VertexToWorld(int32 X, int32 Y) const
{
	const FVector2D Origin = GetOrigin();
	return FVector2D(Origin.X + static_cast<double>(X) * CellSizeCm, Origin.Y + static_cast<double>(Y) * CellSizeCm);
}

float FChronoHeightfield::GetVertexHeight(int32 X, int32 Y) const
{
	if (Heights.Num() == 0)
	{
		return 0.0f;
	}

	X = FMath::Clamp(X, 0, SizeCells);
	Y = FMath::Clamp(Y, 0, SizeCells);
	return Heights[Index(X, Y)];
}

void FChronoHeightfield::SetVertexHeight(int32 X, int32 Y, float HeightCm)
{
	if (X >= 0 && Y >= 0 && X <= SizeCells && Y <= SizeCells && Heights.Num() > 0)
	{
		Heights[Index(X, Y)] = HeightCm;
	}
}

FVector FChronoHeightfield::GetVertexNormal(int32 X, int32 Y) const
{
	// Central differences with clamped neighbors. The same formula is used by
	// every tile, so normals are continuous across tile borders.
	const int32 XL = FMath::Max(X - 1, 0);
	const int32 XR = FMath::Min(X + 1, SizeCells);
	const int32 YD = FMath::Max(Y - 1, 0);
	const int32 YU = FMath::Min(Y + 1, SizeCells);

	const float SpanX = static_cast<float>(FMath::Max(XR - XL, 1)) * CellSizeCm;
	const float SpanY = static_cast<float>(FMath::Max(YU - YD, 1)) * CellSizeCm;

	const float SlopeX = (GetVertexHeight(XR, Y) - GetVertexHeight(XL, Y)) / SpanX;
	const float SlopeY = (GetVertexHeight(X, YU) - GetVertexHeight(X, YD)) / SpanY;

	return FVector(-SlopeX, -SlopeY, 1.0).GetSafeNormal();
}

float FChronoHeightfield::SampleHeight(const FVector2D& WorldXY) const
{
	if (Heights.Num() == 0)
	{
		return 0.0f;
	}

	const FVector2D Origin = GetOrigin();
	const double GridX = FMath::Clamp((WorldXY.X - Origin.X) / CellSizeCm, 0.0, static_cast<double>(SizeCells));
	const double GridY = FMath::Clamp((WorldXY.Y - Origin.Y) / CellSizeCm, 0.0, static_cast<double>(SizeCells));

	const int32 X0 = FMath::Min(FMath::FloorToInt32(GridX), SizeCells - 1);
	const int32 Y0 = FMath::Min(FMath::FloorToInt32(GridY), SizeCells - 1);
	const float Fx = static_cast<float>(GridX - X0);
	const float Fy = static_cast<float>(GridY - Y0);

	const float H00 = Heights[Index(X0, Y0)];
	const float H10 = Heights[Index(X0 + 1, Y0)];
	const float H01 = Heights[Index(X0, Y0 + 1)];
	const float H11 = Heights[Index(X0 + 1, Y0 + 1)];

	// Barycentric interpolation inside the triangle that contains the point.
	// The cell is split along the diagonal from corner (0,0) to corner (1,1),
	// exactly like the render mesh built by AChronoTerrainActor.
	if (Fx >= Fy)
	{
		// Triangle (0,0) (1,0) (1,1)
		return H00 + Fx * (H10 - H00) + Fy * (H11 - H10);
	}

	// Triangle (0,0) (1,1) (0,1)
	return H00 + Fx * (H11 - H01) + Fy * (H01 - H00);
}

FVector FChronoHeightfield::SampleNormal(const FVector2D& WorldXY) const
{
	const double Step = CellSizeCm;
	const float HL = SampleHeight(FVector2D(WorldXY.X - Step, WorldXY.Y));
	const float HR = SampleHeight(FVector2D(WorldXY.X + Step, WorldXY.Y));
	const float HD = SampleHeight(FVector2D(WorldXY.X, WorldXY.Y - Step));
	const float HU = SampleHeight(FVector2D(WorldXY.X, WorldXY.Y + Step));

	return FVector(HL - HR, HD - HU, 2.0 * Step).GetSafeNormal();
}

float FChronoHeightfield::SampleSlopeDegrees(const FVector2D& WorldXY) const
{
	const FVector Normal = SampleNormal(WorldXY);
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(Normal.Z), -1.0f, 1.0f)));
}

FIntRect FChronoHeightfield::ComputeVertexRect(const FVector2D& WorldMin, const FVector2D& WorldMax) const
{
	const FVector2D Origin = GetOrigin();

	const int32 MinX = FMath::Clamp(FMath::FloorToInt32((WorldMin.X - Origin.X) / CellSizeCm), 0, SizeCells);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt32((WorldMin.Y - Origin.Y) / CellSizeCm), 0, SizeCells);
	const int32 MaxX = FMath::Clamp(FMath::CeilToInt32((WorldMax.X - Origin.X) / CellSizeCm), 0, SizeCells);
	const int32 MaxY = FMath::Clamp(FMath::CeilToInt32((WorldMax.Y - Origin.Y) / CellSizeCm), 0, SizeCells);

	// Max is exclusive.
	return FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
}

float FChronoHeightfield::SmoothFalloff(float Distance, float Radius)
{
	if (Radius <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float T = FMath::Clamp(Distance / Radius, 0.0f, 1.0f);
	// 1 at the center, 0 at the radius, zero slope at both ends.
	return 1.0f - (T * T * (3.0f - 2.0f * T));
}

FIntRect FChronoHeightfield::ApplyRaiseBrush(const FVector2D& Center, float RadiusCm, float DeltaCm, float MinHeightCm, float MaxHeightCm)
{
	if (!IsValid() || RadiusCm <= 0.0f || FMath::IsNearlyZero(DeltaCm))
	{
		return FIntRect();
	}

	const FIntRect Rect = ComputeVertexRect(Center - FVector2D(RadiusCm, RadiusCm), Center + FVector2D(RadiusCm, RadiusCm));
	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const float Distance = static_cast<float>(FVector2D::Distance(VertexToWorld(X, Y), Center));
			const float Weight = SmoothFalloff(Distance, RadiusCm);
			if (Weight > 0.0f)
			{
				float& Height = Heights[Index(X, Y)];
				Height = FMath::Clamp(Height + DeltaCm * Weight, MinHeightCm, MaxHeightCm);
			}
		}
	}

	return Rect;
}

FIntRect FChronoHeightfield::ApplyFlattenBrush(const FVector2D& Center, float RadiusCm, float TargetHeightCm, float Strength)
{
	if (!IsValid() || RadiusCm <= 0.0f || Strength <= 0.0f)
	{
		return FIntRect();
	}

	const float ClampedStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	const FIntRect Rect = ComputeVertexRect(Center - FVector2D(RadiusCm, RadiusCm), Center + FVector2D(RadiusCm, RadiusCm));
	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const float Distance = static_cast<float>(FVector2D::Distance(VertexToWorld(X, Y), Center));
			const float Weight = SmoothFalloff(Distance, RadiusCm) * ClampedStrength;
			if (Weight > 0.0f)
			{
				float& Height = Heights[Index(X, Y)];
				Height = FMath::Lerp(Height, TargetHeightCm, Weight);
			}
		}
	}

	return Rect;
}

FIntRect FChronoHeightfield::ApplySmoothBrush(const FVector2D& Center, float RadiusCm, float Strength)
{
	if (!IsValid() || RadiusCm <= 0.0f || Strength <= 0.0f)
	{
		return FIntRect();
	}

	const float ClampedStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	const FIntRect Rect = ComputeVertexRect(Center - FVector2D(RadiusCm, RadiusCm), Center + FVector2D(RadiusCm, RadiusCm));
	const int32 Width = Rect.Width();
	const int32 Height = Rect.Height();
	if (Width <= 0 || Height <= 0)
	{
		return FIntRect();
	}

	// Compute the averages from the untouched heights first, so the result
	// does not depend on the iteration order.
	TArray<float> Averages;
	Averages.SetNumUninitialized(Width * Height);
	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const float Sum =
				GetVertexHeight(X, Y) +
				GetVertexHeight(X - 1, Y) + GetVertexHeight(X + 1, Y) +
				GetVertexHeight(X, Y - 1) + GetVertexHeight(X, Y + 1);
			Averages[(Y - Rect.Min.Y) * Width + (X - Rect.Min.X)] = Sum / 5.0f;
		}
	}

	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const float Distance = static_cast<float>(FVector2D::Distance(VertexToWorld(X, Y), Center));
			const float Weight = SmoothFalloff(Distance, RadiusCm) * ClampedStrength;
			if (Weight > 0.0f)
			{
				float& HeightRef = Heights[Index(X, Y)];
				HeightRef = FMath::Lerp(HeightRef, Averages[(Y - Rect.Min.Y) * Width + (X - Rect.Min.X)], Weight);
			}
		}
	}

	return Rect;
}

FIntRect FChronoHeightfield::FlattenOrientedBox(const FVector2D& Center, const FVector2D& HalfExtentCm, float YawRadians, float TargetHeightCm, float SkirtCm)
{
	if (!IsValid() || HalfExtentCm.X <= 0.0 || HalfExtentCm.Y <= 0.0)
	{
		return FIntRect();
	}

	// A triangle that straddles the edge of the box interpolates between an
	// inside vertex and an outside vertex. To keep the surface exactly flat
	// inside the box, every vertex within one cell diagonal of the box is
	// snapped to the target too. The blend skirt starts beyond that margin.
	const float HardMargin = CellSizeCm * UE_SQRT_2;
	const float Skirt = FMath::Max(SkirtCm, 0.0f);

	const double BoundRadius = HalfExtentCm.Size() + HardMargin + Skirt;
	const FIntRect Rect = ComputeVertexRect(Center - FVector2D(BoundRadius, BoundRadius), Center + FVector2D(BoundRadius, BoundRadius));

	const double CosYaw = FMath::Cos(YawRadians);
	const double SinYaw = FMath::Sin(YawRadians);

	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const FVector2D Delta = VertexToWorld(X, Y) - Center;

			// Vertex position in the local frame of the box.
			const double LocalX = Delta.X * CosYaw + Delta.Y * SinYaw;
			const double LocalY = -Delta.X * SinYaw + Delta.Y * CosYaw;

			// Distance from the vertex to the box (0 inside).
			const double OutX = FMath::Max(FMath::Abs(LocalX) - HalfExtentCm.X, 0.0);
			const double OutY = FMath::Max(FMath::Abs(LocalY) - HalfExtentCm.Y, 0.0);
			const float Distance = static_cast<float>(FMath::Sqrt(OutX * OutX + OutY * OutY));

			float& Height = Heights[Index(X, Y)];
			if (Distance <= HardMargin)
			{
				Height = TargetHeightCm;
			}
			else if (Skirt > 0.0f && Distance < HardMargin + Skirt)
			{
				const float Weight = SmoothFalloff(Distance - HardMargin, Skirt);
				Height = FMath::Lerp(Height, TargetHeightCm, Weight);
			}
		}
	}

	return Rect;
}

FIntRect FChronoHeightfield::ConformToPath(TConstArrayView<FVector> Path, float HalfWidthCm, float OffsetBelowCm, float BlendCm, EChronoConformMode Mode)
{
	if (!IsValid() || Path.Num() == 0 || HalfWidthCm <= 0.0f)
	{
		return FIntRect();
	}

	// Same reasoning as FlattenOrientedBox: vertices within one cell diagonal
	// of the ribbon also take the path height, so the ground is exact under
	// the whole ribbon and not only at grid vertices.
	const float HardRadius = HalfWidthCm + CellSizeCm * UE_SQRT_2;
	const float Blend = (Mode == EChronoConformMode::BlendAndSet) ? FMath::Max(BlendCm, 0.0f) : 0.0f;
	const double Reach = HardRadius + Blend;
	const FVector2D ReachVector(Reach, Reach);

	FBox2D PathBounds(ForceInit);
	for (const FVector& Point : Path)
	{
		PathBounds += FVector2D(Point.X, Point.Y);
	}

	const FIntRect Rect = ComputeVertexRect(PathBounds.Min - ReachVector, PathBounds.Max + ReachVector);
	const int32 Width = Rect.Width();
	const int32 Height = Rect.Height();
	if (Width <= 0 || Height <= 0)
	{
		return FIntRect();
	}

	// For every vertex of the rectangle: distance to the path and path height
	// at the nearest point.
	TArray<float> BestDistance;
	BestDistance.Init(TNumericLimits<float>::Max(), Width * Height);
	TArray<float> BestHeight;
	BestHeight.Init(0.0f, Width * Height);

	const int32 NumPieces = FMath::Max(Path.Num() - 1, 1);
	for (int32 Piece = 0; Piece < NumPieces; ++Piece)
	{
		const FVector& A = Path[Piece];
		const FVector& B = Path[FMath::Min(Piece + 1, Path.Num() - 1)];
		const FVector2D A2(A.X, A.Y);
		const FVector2D B2(B.X, B.Y);
		const FVector2D AB = B2 - A2;
		const double LengthSquared = AB.SizeSquared();

		const FVector2D PieceMin(FMath::Min(A2.X, B2.X), FMath::Min(A2.Y, B2.Y));
		const FVector2D PieceMax(FMath::Max(A2.X, B2.X), FMath::Max(A2.Y, B2.Y));
		const FIntRect PieceRect = ComputeVertexRect(PieceMin - ReachVector, PieceMax + ReachVector);

		for (int32 Y = FMath::Max(PieceRect.Min.Y, Rect.Min.Y); Y < FMath::Min(PieceRect.Max.Y, Rect.Max.Y); ++Y)
		{
			for (int32 X = FMath::Max(PieceRect.Min.X, Rect.Min.X); X < FMath::Min(PieceRect.Max.X, Rect.Max.X); ++X)
			{
				const FVector2D Vertex = VertexToWorld(X, Y);
				const double T = (LengthSquared > UE_DOUBLE_SMALL_NUMBER)
					? FMath::Clamp(FVector2D::DotProduct(Vertex - A2, AB) / LengthSquared, 0.0, 1.0)
					: 0.0;
				const float Distance = static_cast<float>(FVector2D::Distance(Vertex, A2 + AB * T));

				const int32 LocalIndex = (Y - Rect.Min.Y) * Width + (X - Rect.Min.X);
				if (Distance < BestDistance[LocalIndex])
				{
					BestDistance[LocalIndex] = Distance;
					BestHeight[LocalIndex] = static_cast<float>(FMath::Lerp(A.Z, B.Z, T));
				}
			}
		}
	}

	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const int32 LocalIndex = (Y - Rect.Min.Y) * Width + (X - Rect.Min.X);
			const float Distance = BestDistance[LocalIndex];
			if (Distance > Reach)
			{
				continue;
			}

			const float Target = BestHeight[LocalIndex] - OffsetBelowCm;
			float& HeightRef = Heights[Index(X, Y)];
			if (Distance <= HardRadius)
			{
				HeightRef = (Mode == EChronoConformMode::Min) ? FMath::Min(HeightRef, Target) : Target;
			}
			else if (Blend > 0.0f)
			{
				HeightRef = FMath::Lerp(HeightRef, Target, SmoothFalloff(Distance - HardRadius, Blend));
			}
		}
	}

	return Rect;
}

void FChronoHeightfield::GetHeightRangeInOrientedBox(const FVector2D& Center, const FVector2D& HalfExtentCm, float YawRadians, float& OutMinCm, float& OutMaxCm) const
{
	OutMinCm = TNumericLimits<float>::Max();
	OutMaxCm = TNumericLimits<float>::Lowest();

	const double CosYaw = FMath::Cos(YawRadians);
	const double SinYaw = FMath::Sin(YawRadians);

	auto Accumulate = [&OutMinCm, &OutMaxCm](float Height)
	{
		OutMinCm = FMath::Min(OutMinCm, Height);
		OutMaxCm = FMath::Max(OutMaxCm, Height);
	};

	// The surface is piecewise linear, so its extremes over the box are reached
	// either at a grid vertex inside the box or on the border of the box.
	// Vertices: every vertex within one cell of the box (conservative).
	const double BoundRadius = HalfExtentCm.Size() + CellSizeCm;
	const FIntRect Rect = ComputeVertexRect(Center - FVector2D(BoundRadius, BoundRadius), Center + FVector2D(BoundRadius, BoundRadius));
	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const FVector2D Delta = VertexToWorld(X, Y) - Center;
			const double LocalX = Delta.X * CosYaw + Delta.Y * SinYaw;
			const double LocalY = -Delta.X * SinYaw + Delta.Y * CosYaw;
			if (FMath::Abs(LocalX) <= HalfExtentCm.X + CellSizeCm && FMath::Abs(LocalY) <= HalfExtentCm.Y + CellSizeCm)
			{
				Accumulate(Heights[Index(X, Y)]);
			}
		}
	}

	// Corners and center, for boxes smaller than one cell.
	const FVector2D AxisX(CosYaw, SinYaw);
	const FVector2D AxisY(-SinYaw, CosYaw);
	Accumulate(SampleHeight(Center));
	for (int32 SignX = -1; SignX <= 1; SignX += 2)
	{
		for (int32 SignY = -1; SignY <= 1; SignY += 2)
		{
			Accumulate(SampleHeight(Center + AxisX * (HalfExtentCm.X * SignX) + AxisY * (HalfExtentCm.Y * SignY)));
		}
	}
}

void FChronoHeightfield::Serialize(FArchive& Ar)
{
	int32 Version = ChronoHeightfieldPrivate::SerializationVersion;
	Ar << Version;
	Ar << SizeCells;
	Ar << CellSizeCm;
	Ar << Heights;

	if (Ar.IsLoading())
	{
		// Reject corrupted or incompatible data rather than reading out of bounds later.
		const int64 Expected = static_cast<int64>(SizeCells + 1) * static_cast<int64>(SizeCells + 1);
		if (Version != ChronoHeightfieldPrivate::SerializationVersion || SizeCells <= 0 || CellSizeCm <= 0.0f || Heights.Num() != Expected)
		{
			Ar.SetError();
			SizeCells = 0;
			Heights.Reset();
		}
	}
}
