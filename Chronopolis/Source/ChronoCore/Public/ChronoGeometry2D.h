// Oriented rectangle in the ground plane: the footprint of a zoning cell or
// of a building. Shared by placement validation and by the zero-clipping audit.

#pragma once

#include "CoreMinimal.h"

struct CHRONOCORE_API FChronoOrientedBox2D
{
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D HalfExtent = FVector2D::ZeroVector;
	double YawRadians = 0.0;

	FChronoOrientedBox2D() = default;
	FChronoOrientedBox2D(const FVector2D& InCenter, const FVector2D& InHalfExtent, double InYawRadians)
		: Center(InCenter), HalfExtent(InHalfExtent), YawRadians(InYawRadians)
	{
	}

	FVector2D GetAxisX() const { return FVector2D(FMath::Cos(YawRadians), FMath::Sin(YawRadians)); }
	FVector2D GetAxisY() const { return FVector2D(-FMath::Sin(YawRadians), FMath::Cos(YawRadians)); }

	/** Point expressed in the frame of the box (origin at the center). */
	FVector2D ToLocal(const FVector2D& Point) const;

	void GetCorners(FVector2D OutCorners[4]) const;

	/** Axis-aligned box that contains the oriented box. */
	FBox2D GetBounds() const;

	bool Contains(const FVector2D& Point) const;

	/** Distance from the box to a point (0 if the point is inside). */
	double DistanceToPoint(const FVector2D& Point) const;

	/** Distance from the box to a line segment (0 if they touch or cross). */
	double DistanceToSegment(const FVector2D& SegmentStart, const FVector2D& SegmentEnd) const;

	/**
	 * True if the two boxes overlap by more than ToleranceCm. Boxes that only
	 * share an edge, or overlap by less than the tolerance, do not count.
	 */
	static bool Overlap(const FChronoOrientedBox2D& A, const FChronoOrientedBox2D& B, double ToleranceCm);
};
