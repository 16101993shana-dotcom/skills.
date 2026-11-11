#include "ChronoRoadGeometry.h"

namespace ChronoRoadGeometryPrivate
{
	double Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/** True if Point is inside or on the border of the counterclockwise triangle ABC. */
	bool IsPointInTriangle(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const double D1 = Cross2D(B - A, Point - A);
		const double D2 = Cross2D(C - B, Point - B);
		const double D3 = Cross2D(A - C, Point - C);
		return D1 >= 0.0 && D2 >= 0.0 && D3 >= 0.0;
	}
}

FChronoBezier2D FChronoBezier2D::MakeLine(const FVector2D& A, const FVector2D& B)
{
	// Control points at one third and two thirds: the parameter then moves at
	// constant speed along the line and the derivative is never zero.
	FChronoBezier2D Curve;
	Curve.P0 = A;
	Curve.P1 = A + (B - A) / 3.0;
	Curve.P2 = A + (B - A) * (2.0 / 3.0);
	Curve.P3 = B;
	return Curve;
}

FChronoBezier2D FChronoBezier2D::MakeQuadratic(const FVector2D& A, const FVector2D& Control, const FVector2D& B)
{
	// Exact degree elevation of a quadratic Bezier.
	FChronoBezier2D Curve;
	Curve.P0 = A;
	Curve.P1 = A + (Control - A) * (2.0 / 3.0);
	Curve.P2 = B + (Control - B) * (2.0 / 3.0);
	Curve.P3 = B;
	return Curve;
}

FVector2D FChronoBezier2D::Evaluate(double T) const
{
	const double U = 1.0 - T;
	return P0 * (U * U * U) + P1 * (3.0 * U * U * T) + P2 * (3.0 * U * T * T) + P3 * (T * T * T);
}

FVector2D FChronoBezier2D::Derivative(double T) const
{
	const double U = 1.0 - T;
	return (P1 - P0) * (3.0 * U * U) + (P2 - P1) * (6.0 * U * T) + (P3 - P2) * (3.0 * T * T);
}

FVector2D FChronoBezier2D::SecondDerivative(double T) const
{
	const double U = 1.0 - T;
	return (P2 - P1 * 2.0 + P0) * (6.0 * U) + (P3 - P2 * 2.0 + P1) * (6.0 * T);
}

double FChronoBezier2D::ComputeLengthTo(double T, int32 Steps) const
{
	const int32 NumSteps = FMath::Max(Steps, 1);
	const double EndT = FMath::Clamp(T, 0.0, 1.0);

	double Length = 0.0;
	FVector2D Previous = P0;
	for (int32 Step = 1; Step <= NumSteps; ++Step)
	{
		const FVector2D Current = Evaluate(EndT * static_cast<double>(Step) / NumSteps);
		Length += FVector2D::Distance(Previous, Current);
		Previous = Current;
	}
	return Length;
}

double FChronoBezier2D::ComputeMinRadius(int32 Steps) const
{
	const int32 NumSteps = FMath::Max(Steps, 1);

	double MaxCurvature = 0.0;
	for (int32 Step = 0; Step <= NumSteps; ++Step)
	{
		const double T = static_cast<double>(Step) / NumSteps;
		const FVector2D D1 = Derivative(T);
		const FVector2D D2 = SecondDerivative(T);
		const double Speed = D1.Size();
		if (Speed > UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			const double Curvature = FMath::Abs(ChronoRoadGeometryPrivate::Cross2D(D1, D2)) / (Speed * Speed * Speed);
			MaxCurvature = FMath::Max(MaxCurvature, Curvature);
		}
	}

	return (MaxCurvature > 1.0e-12) ? 1.0 / MaxCurvature : 1.0e12;
}

void FChronoBezier2D::Split(double T, FChronoBezier2D& OutFirst, FChronoBezier2D& OutSecond) const
{
	// De Casteljau subdivision.
	const FVector2D A = FMath::Lerp(P0, P1, T);
	const FVector2D B = FMath::Lerp(P1, P2, T);
	const FVector2D C = FMath::Lerp(P2, P3, T);
	const FVector2D D = FMath::Lerp(A, B, T);
	const FVector2D E = FMath::Lerp(B, C, T);
	const FVector2D Mid = FMath::Lerp(D, E, T);

	OutFirst.P0 = P0;
	OutFirst.P1 = A;
	OutFirst.P2 = D;
	OutFirst.P3 = Mid;

	OutSecond.P0 = Mid;
	OutSecond.P1 = E;
	OutSecond.P2 = C;
	OutSecond.P3 = P3;
}

FBox2D FChronoBezier2D::ComputeBounds() const
{
	// A Bezier curve always lies inside the convex hull of its control points.
	FBox2D Bounds(ForceInit);
	Bounds += P0;
	Bounds += P1;
	Bounds += P2;
	Bounds += P3;
	return Bounds;
}

double FChronoBezier2D::FindClosestParameter(const FVector2D& Point, double& OutDistance, int32 Steps) const
{
	const int32 NumSteps = FMath::Max(Steps, 2);

	// Coarse search.
	double BestT = 0.0;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (int32 Step = 0; Step <= NumSteps; ++Step)
	{
		const double T = static_cast<double>(Step) / NumSteps;
		const double DistanceSquared = FVector2D::DistSquared(Evaluate(T), Point);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestT = T;
		}
	}

	// Refine around the best sample by ternary search.
	double Low = FMath::Max(0.0, BestT - 1.0 / NumSteps);
	double High = FMath::Min(1.0, BestT + 1.0 / NumSteps);
	for (int32 Iteration = 0; Iteration < 24; ++Iteration)
	{
		const double A = Low + (High - Low) / 3.0;
		const double B = High - (High - Low) / 3.0;
		if (FVector2D::DistSquared(Evaluate(A), Point) < FVector2D::DistSquared(Evaluate(B), Point))
		{
			High = B;
		}
		else
		{
			Low = A;
		}
	}

	const double T = 0.5 * (Low + High);
	OutDistance = FVector2D::Distance(Evaluate(T), Point);
	return T;
}

FVector2D FChronoBezier2D::GetStartDirection() const
{
	FVector2D Direction = P1 - P0;
	if (Direction.SizeSquared() < UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		Direction = P3 - P0;
	}
	return Direction.GetSafeNormal();
}

FVector2D FChronoBezier2D::GetEndDirection() const
{
	FVector2D Direction = P2 - P3;
	if (Direction.SizeSquared() < UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		Direction = P0 - P3;
	}
	return Direction.GetSafeNormal();
}

void ChronoRoadGeometry::BuildPolyline(const FChronoBezier2D& Curve, double SpacingCm, TArray<FChronoRoadSample>& OutSamples)
{
	OutSamples.Reset();

	const double Length = Curve.ComputeLength();
	const int32 NumSteps = FMath::Max(2, FMath::CeilToInt32(Length / FMath::Max(SpacingCm, 1.0)));

	OutSamples.Reserve(NumSteps + 1);
	FVector2D PreviousTangent = Curve.GetStartDirection();
	double Distance = 0.0;
	for (int32 Step = 0; Step <= NumSteps; ++Step)
	{
		const double T = static_cast<double>(Step) / NumSteps;

		FChronoRoadSample Sample;
		Sample.Position = Curve.Evaluate(T);
		if (Step > 0)
		{
			Distance += FVector2D::Distance(OutSamples.Last().Position, Sample.Position);
		}
		Sample.Distance = Distance;

		const FVector2D Derivative = Curve.Derivative(T);
		Sample.Tangent = (Derivative.SizeSquared() > UE_DOUBLE_KINDA_SMALL_NUMBER) ? Derivative.GetSafeNormal() : PreviousTangent;
		PreviousTangent = Sample.Tangent;

		OutSamples.Add(Sample);
	}
}

FChronoRoadSample ChronoRoadGeometry::SampleAtDistance(const TArray<FChronoRoadSample>& Samples, double Distance)
{
	if (Samples.Num() == 0)
	{
		return FChronoRoadSample();
	}
	if (Distance <= Samples[0].Distance)
	{
		return Samples[0];
	}
	if (Distance >= Samples.Last().Distance)
	{
		return Samples.Last();
	}

	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		const FChronoRoadSample& A = Samples[Index - 1];
		const FChronoRoadSample& B = Samples[Index];
		if (Distance <= B.Distance)
		{
			const double Span = B.Distance - A.Distance;
			const double Alpha = (Span > UE_DOUBLE_KINDA_SMALL_NUMBER) ? (Distance - A.Distance) / Span : 0.0;

			FChronoRoadSample Result;
			Result.Position = FMath::Lerp(A.Position, B.Position, Alpha);
			Result.Tangent = FMath::Lerp(A.Tangent, B.Tangent, Alpha).GetSafeNormal();
			if (Result.Tangent.IsNearlyZero())
			{
				Result.Tangent = A.Tangent;
			}
			Result.Distance = Distance;
			return Result;
		}
	}

	return Samples.Last();
}

void ChronoRoadGeometry::ClipPolyline(const TArray<FChronoRoadSample>& Samples, double FromDistance, double ToDistance, TArray<FChronoRoadSample>& OutSamples)
{
	OutSamples.Reset();
	if (Samples.Num() < 2 || ToDistance <= FromDistance)
	{
		return;
	}

	// Interior samples closer than this to an end are dropped, to avoid
	// needle-thin triangles next to the exact end samples.
	const double Tolerance = 1.0;

	OutSamples.Add(SampleAtDistance(Samples, FromDistance));
	for (const FChronoRoadSample& Sample : Samples)
	{
		if (Sample.Distance > FromDistance + Tolerance && Sample.Distance < ToDistance - Tolerance)
		{
			OutSamples.Add(Sample);
		}
	}
	OutSamples.Add(SampleAtDistance(Samples, ToDistance));
}

double ChronoRoadGeometry::DistancePointToPolyline(const FVector2D& Point, const TArray<FChronoRoadSample>& Samples)
{
	if (Samples.Num() == 0)
	{
		return TNumericLimits<double>::Max();
	}
	if (Samples.Num() == 1)
	{
		return FVector2D::Distance(Point, Samples[0].Position);
	}

	double BestSquared = TNumericLimits<double>::Max();
	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		const FVector2D& A = Samples[Index - 1].Position;
		const FVector2D& B = Samples[Index].Position;
		const FVector2D AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		const double T = (LengthSquared > UE_DOUBLE_SMALL_NUMBER) ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
		BestSquared = FMath::Min(BestSquared, FVector2D::DistSquared(Point, A + AB * T));
	}
	return FMath::Sqrt(BestSquared);
}

bool ChronoRoadGeometry::IsPointInsideRibbon(const FVector2D& Point, const TArray<FChronoRoadSample>& Samples, double HalfWidthCm)
{
	if (Samples.Num() < 2 || HalfWidthCm <= 0.0)
	{
		return false;
	}

	const double LimitSquared = HalfWidthCm * HalfWidthCm;

	// Inside one of the straight pieces: the foot of the perpendicular falls
	// within the piece (so nothing beyond the square ends counts).
	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		const FVector2D& A = Samples[Index - 1].Position;
		const FVector2D AB = Samples[Index].Position - A;
		const double LengthSquared = AB.SizeSquared();
		if (LengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		const double T = FVector2D::DotProduct(Point - A, AB) / LengthSquared;
		if (T >= 0.0 && T <= 1.0 && FVector2D::DistSquared(Point, A + AB * T) < LimitSquared)
		{
			return true;
		}
	}

	// Inside the wedge between two pieces, on the outside of a bend.
	for (int32 Index = 1; Index < Samples.Num() - 1; ++Index)
	{
		if (FVector2D::DistSquared(Point, Samples[Index].Position) < LimitSquared)
		{
			return true;
		}
	}

	return false;
}

void ChronoRoadGeometry::SortArmsCounterClockwise(const TArray<FChronoRoadArm>& Arms, TArray<int32>& OutOrder)
{
	OutOrder.Reset(Arms.Num());
	for (int32 Index = 0; Index < Arms.Num(); ++Index)
	{
		OutOrder.Add(Index);
	}

	OutOrder.Sort([&Arms](int32 A, int32 B)
	{
		const double AngleA = FMath::Atan2(Arms[A].Direction.Y, Arms[A].Direction.X);
		const double AngleB = FMath::Atan2(Arms[B].Direction.Y, Arms[B].Direction.X);
		// The index breaks ties so the order is deterministic.
		return (AngleA != AngleB) ? (AngleA < AngleB) : (A < B);
	});
}

void ChronoRoadGeometry::ComputeJunctionTrims(const TArray<FChronoRoadArm>& Arms, float MinTrimCm, TArray<float>& OutTrims, float& OutMinAngleDegrees)
{
	const int32 NumArms = Arms.Num();
	OutMinAngleDegrees = 360.0f;

	// A dead end is not cut back.
	OutTrims.Init((NumArms >= 2) ? MinTrimCm : 0.0f, NumArms);
	if (NumArms < 2)
	{
		return;
	}

	TArray<int32> Order;
	SortArmsCounterClockwise(Arms, Order);

	for (int32 Slot = 0; Slot < NumArms; ++Slot)
	{
		const int32 I = Order[Slot];
		const int32 J = Order[(Slot + 1) % NumArms];

		const double AngleI = FMath::Atan2(Arms[I].Direction.Y, Arms[I].Direction.X);
		const double AngleJ = FMath::Atan2(Arms[J].Direction.Y, Arms[J].Direction.X);

		// Angle swept counterclockwise from arm I to arm J, in (0, 2 pi].
		double Theta = AngleJ - AngleI;
		if (Theta <= 0.0)
		{
			Theta += 2.0 * UE_DOUBLE_PI;
		}

		OutMinAngleDegrees = FMath::Min(OutMinAngleDegrees, static_cast<float>(FMath::RadiansToDegrees(Theta)));

		// Edges only cross on the side where the roads form an angle below 180 degrees.
		if (Theta < UE_DOUBLE_PI - 0.01)
		{
			const double SinTheta = FMath::Sin(Theta);
			const double CosTheta = FMath::Cos(Theta);
			const double WidthI = Arms[I].HalfWidthCm;
			const double WidthJ = Arms[J].HalfWidthCm;

			// Distance from the node, along each arm, of the point where the
			// facing edges of the two roads cross.
			const double TrimI = (WidthJ + WidthI * CosTheta) / SinTheta;
			const double TrimJ = (WidthI + WidthJ * CosTheta) / SinTheta;

			OutTrims[I] = FMath::Max(OutTrims[I], static_cast<float>(TrimI));
			OutTrims[J] = FMath::Max(OutTrims[J], static_cast<float>(TrimJ));
		}
	}
}

double ChronoRoadGeometry::ComputePolygonArea(const TArray<FVector2D>& Polygon)
{
	double Twice = 0.0;
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		Twice += ChronoRoadGeometryPrivate::Cross2D(Polygon[Index], Polygon[(Index + 1) % Polygon.Num()]);
	}
	return 0.5 * Twice;
}

bool ChronoRoadGeometry::TriangulatePolygon(const TArray<FVector2D>& Polygon, TArray<int32>& OutTriangles)
{
	using namespace ChronoRoadGeometryPrivate;

	OutTriangles.Reset();
	if (Polygon.Num() < 3)
	{
		return false;
	}

	// Remaining polygon, as indices into the input.
	TArray<int32> Remaining;
	Remaining.Reserve(Polygon.Num());
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		Remaining.Add(Index);
	}

	// Twice the area (cm2) below which a corner is treated as flat.
	const double FlatTolerance = 1.0e-3;
	// Points closer than this (cm) to a corner of the candidate ear are the same point.
	const double SamePointToleranceSquared = 1.0e-2;

	while (Remaining.Num() >= 3)
	{
		bool bProgress = false;

		for (int32 Slot = 0; Slot < Remaining.Num(); ++Slot)
		{
			const int32 Count = Remaining.Num();
			const int32 IndexA = Remaining[(Slot + Count - 1) % Count];
			const int32 IndexB = Remaining[Slot];
			const int32 IndexC = Remaining[(Slot + 1) % Count];
			const FVector2D& A = Polygon[IndexA];
			const FVector2D& B = Polygon[IndexB];
			const FVector2D& C = Polygon[IndexC];

			const double Turn = Cross2D(B - A, C - B);

			// Duplicate or collinear corner: drop it, it encloses no area.
			if (FMath::Abs(Turn) <= FlatTolerance)
			{
				Remaining.RemoveAt(Slot);
				bProgress = true;
				break;
			}

			// Reflex corner: not an ear.
			if (Turn < 0.0)
			{
				continue;
			}

			// An ear contains no other remaining vertex.
			bool bContainsVertex = false;
			for (int32 Other = 0; Other < Count && !bContainsVertex; ++Other)
			{
				const int32 IndexP = Remaining[Other];
				if (IndexP == IndexA || IndexP == IndexB || IndexP == IndexC)
				{
					continue;
				}

				const FVector2D& P = Polygon[IndexP];
				if (FVector2D::DistSquared(P, A) < SamePointToleranceSquared ||
					FVector2D::DistSquared(P, B) < SamePointToleranceSquared ||
					FVector2D::DistSquared(P, C) < SamePointToleranceSquared)
				{
					continue;
				}

				bContainsVertex = IsPointInTriangle(P, A, B, C);
			}

			if (!bContainsVertex)
			{
				OutTriangles.Add(IndexA);
				OutTriangles.Add(IndexB);
				OutTriangles.Add(IndexC);
				Remaining.RemoveAt(Slot);
				bProgress = true;
				break;
			}
		}

		if (!bProgress)
		{
			// Only happens for a self-intersecting or clockwise polygon.
			return false;
		}
	}

	return OutTriangles.Num() > 0;
}
