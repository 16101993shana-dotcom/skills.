#include "ChronoGeometry2D.h"

namespace ChronoGeometry2DPrivate
{
	/** Distance from a point to the axis-aligned box [-Half, +Half]. */
	double DistancePointToCenteredBox(const FVector2D& Point, const FVector2D& Half)
	{
		const double OutX = FMath::Max(FMath::Abs(Point.X) - Half.X, 0.0);
		const double OutY = FMath::Max(FMath::Abs(Point.Y) - Half.Y, 0.0);
		return FMath::Sqrt(OutX * OutX + OutY * OutY);
	}

	double DistancePointToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		const double T = (LengthSquared > UE_DOUBLE_SMALL_NUMBER) ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
		return FVector2D::Distance(Point, A + AB * T);
	}

	/** True if the segment AB crosses the axis-aligned box [-Half, +Half] (slab test). */
	bool SegmentIntersectsCenteredBox(const FVector2D& A, const FVector2D& B, const FVector2D& Half)
	{
		double TMin = 0.0;
		double TMax = 1.0;
		const FVector2D Delta = B - A;

		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const double Start = (Axis == 0) ? A.X : A.Y;
			const double Direction = (Axis == 0) ? Delta.X : Delta.Y;
			const double Limit = (Axis == 0) ? Half.X : Half.Y;

			if (FMath::Abs(Direction) < UE_DOUBLE_SMALL_NUMBER)
			{
				// Parallel to this slab: outside means no intersection at all.
				if (FMath::Abs(Start) > Limit)
				{
					return false;
				}
			}
			else
			{
				double T1 = (-Limit - Start) / Direction;
				double T2 = (Limit - Start) / Direction;
				if (T1 > T2)
				{
					Swap(T1, T2);
				}
				TMin = FMath::Max(TMin, T1);
				TMax = FMath::Min(TMax, T2);
				if (TMin > TMax)
				{
					return false;
				}
			}
		}

		return true;
	}
}

FVector2D FChronoOrientedBox2D::ToLocal(const FVector2D& Point) const
{
	const FVector2D Delta = Point - Center;
	return FVector2D(FVector2D::DotProduct(Delta, GetAxisX()), FVector2D::DotProduct(Delta, GetAxisY()));
}

void FChronoOrientedBox2D::GetCorners(FVector2D OutCorners[4]) const
{
	const FVector2D X = GetAxisX() * HalfExtent.X;
	const FVector2D Y = GetAxisY() * HalfExtent.Y;

	// Counterclockwise.
	OutCorners[0] = Center - X - Y;
	OutCorners[1] = Center + X - Y;
	OutCorners[2] = Center + X + Y;
	OutCorners[3] = Center - X + Y;
}

FBox2D FChronoOrientedBox2D::GetBounds() const
{
	FVector2D Corners[4];
	GetCorners(Corners);

	FBox2D Bounds(ForceInit);
	for (const FVector2D& Corner : Corners)
	{
		Bounds += Corner;
	}
	return Bounds;
}

bool FChronoOrientedBox2D::Contains(const FVector2D& Point) const
{
	const FVector2D Local = ToLocal(Point);
	return FMath::Abs(Local.X) <= HalfExtent.X && FMath::Abs(Local.Y) <= HalfExtent.Y;
}

double FChronoOrientedBox2D::DistanceToPoint(const FVector2D& Point) const
{
	return ChronoGeometry2DPrivate::DistancePointToCenteredBox(ToLocal(Point), HalfExtent);
}

double FChronoOrientedBox2D::DistanceToSegment(const FVector2D& SegmentStart, const FVector2D& SegmentEnd) const
{
	using namespace ChronoGeometry2DPrivate;

	// Work in the frame of the box, where it is axis-aligned.
	const FVector2D A = ToLocal(SegmentStart);
	const FVector2D B = ToLocal(SegmentEnd);

	if (SegmentIntersectsCenteredBox(A, B, HalfExtent))
	{
		return 0.0;
	}

	// Otherwise the closest pair involves an end of the segment or a corner of the box.
	double Best = FMath::Min(DistancePointToCenteredBox(A, HalfExtent), DistancePointToCenteredBox(B, HalfExtent));

	const FVector2D Corners[4] =
	{
		FVector2D(-HalfExtent.X, -HalfExtent.Y),
		FVector2D(HalfExtent.X, -HalfExtent.Y),
		FVector2D(HalfExtent.X, HalfExtent.Y),
		FVector2D(-HalfExtent.X, HalfExtent.Y)
	};
	for (const FVector2D& Corner : Corners)
	{
		Best = FMath::Min(Best, DistancePointToSegment(Corner, A, B));
	}

	return Best;
}

bool FChronoOrientedBox2D::Overlap(const FChronoOrientedBox2D& A, const FChronoOrientedBox2D& B, double ToleranceCm)
{
	// Separating axis theorem: two convex shapes are apart if their
	// projections on one of their edge normals do not overlap.
	const FVector2D Axes[4] = { A.GetAxisX(), A.GetAxisY(), B.GetAxisX(), B.GetAxisY() };
	const FVector2D Delta = B.Center - A.Center;

	// Shrinking both boxes by half the tolerance ignores overlaps below it.
	const double Shrink = 0.5 * ToleranceCm;
	const FVector2D HalfA(FMath::Max(A.HalfExtent.X - Shrink, 0.0), FMath::Max(A.HalfExtent.Y - Shrink, 0.0));
	const FVector2D HalfB(FMath::Max(B.HalfExtent.X - Shrink, 0.0), FMath::Max(B.HalfExtent.Y - Shrink, 0.0));

	for (const FVector2D& Axis : Axes)
	{
		const double RadiusA = HalfA.X * FMath::Abs(FVector2D::DotProduct(A.GetAxisX(), Axis)) + HalfA.Y * FMath::Abs(FVector2D::DotProduct(A.GetAxisY(), Axis));
		const double RadiusB = HalfB.X * FMath::Abs(FVector2D::DotProduct(B.GetAxisX(), Axis)) + HalfB.Y * FMath::Abs(FVector2D::DotProduct(B.GetAxisY(), Axis));
		if (FMath::Abs(FVector2D::DotProduct(Delta, Axis)) >= RadiusA + RadiusB)
		{
			return false;
		}
	}

	return true;
}
