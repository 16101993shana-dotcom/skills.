// 2D geometry used by roads: cubic Bezier centerlines, arc-length polylines,
// junction trimming and polygon triangulation. Pure math, no engine objects.

#pragma once

#include "CoreMinimal.h"

/** Cubic Bezier curve in the ground plane. Road centerlines are stored this way. */
struct CHRONOROADS_API FChronoBezier2D
{
	FVector2D P0 = FVector2D::ZeroVector;
	FVector2D P1 = FVector2D::ZeroVector;
	FVector2D P2 = FVector2D::ZeroVector;
	FVector2D P3 = FVector2D::ZeroVector;

	/** Straight line from A to B. */
	static FChronoBezier2D MakeLine(const FVector2D& A, const FVector2D& B);

	/** Quadratic curve from A to B bending toward Control, expressed as a cubic. */
	static FChronoBezier2D MakeQuadratic(const FVector2D& A, const FVector2D& Control, const FVector2D& B);

	FVector2D Evaluate(double T) const;
	FVector2D Derivative(double T) const;
	FVector2D SecondDerivative(double T) const;

	/** Arc length from parameter 0 to T, by sampling. */
	double ComputeLengthTo(double T, int32 Steps = 32) const;
	double ComputeLength(int32 Steps = 32) const { return ComputeLengthTo(1.0, Steps); }

	/** Smallest radius of curvature along the curve. Very large for a straight line. */
	double ComputeMinRadius(int32 Steps = 24) const;

	/** Splits the curve at T into two curves that trace exactly the same path. */
	void Split(double T, FChronoBezier2D& OutFirst, FChronoBezier2D& OutSecond) const;

	/** Box containing the whole curve (bounds of the control points). */
	FBox2D ComputeBounds() const;

	/** Parameter of the curve point nearest to Point. */
	double FindClosestParameter(const FVector2D& Point, double& OutDistance, int32 Steps = 32) const;

	/** Unit direction leaving the start point along the curve. */
	FVector2D GetStartDirection() const;

	/** Unit direction leaving the end point along the curve, toward the start. */
	FVector2D GetEndDirection() const;
};

/** One point of a centerline sampled by arc length. */
struct FChronoRoadSample
{
	FVector2D Position = FVector2D::ZeroVector;

	/** Unit tangent, pointing from the start of the segment to its end. */
	FVector2D Tangent = FVector2D(1.0, 0.0);

	/** Distance from the start of the segment, along the curve. */
	double Distance = 0.0;
};

/** One road arriving at a node. */
struct FChronoRoadArm
{
	/** Unit direction leaving the node along the road. */
	FVector2D Direction = FVector2D(1.0, 0.0);
	float HalfWidthCm = 0.0f;
};

namespace ChronoRoadGeometry
{
	/** Samples a curve into a polyline whose points are about SpacingCm apart. */
	CHRONOROADS_API void BuildPolyline(const FChronoBezier2D& Curve, double SpacingCm, TArray<FChronoRoadSample>& OutSamples);

	/** Interpolated sample at a distance from the start. */
	CHRONOROADS_API FChronoRoadSample SampleAtDistance(const TArray<FChronoRoadSample>& Samples, double Distance);

	/** Keeps the part of a polyline between two distances, with exact end samples. */
	CHRONOROADS_API void ClipPolyline(const TArray<FChronoRoadSample>& Samples, double FromDistance, double ToDistance, TArray<FChronoRoadSample>& OutSamples);

	CHRONOROADS_API double DistancePointToPolyline(const FVector2D& Point, const TArray<FChronoRoadSample>& Samples);

	/** Indices of the arms sorted counterclockwise (increasing atan2 of the direction). */
	CHRONOROADS_API void SortArmsCounterClockwise(const TArray<FChronoRoadArm>& Arms, TArray<int32>& OutOrder);

	/**
	 * For each arm, the distance by which its segment must be cut back from the
	 * node so that the edges of neighboring roads do not overlap. The junction
	 * mesh fills the space left free.
	 * @param OutMinAngleDegrees smallest angle between two neighboring arms (360 for a single arm).
	 */
	CHRONOROADS_API void ComputeJunctionTrims(const TArray<FChronoRoadArm>& Arms, float MinTrimCm, TArray<float>& OutTrims, float& OutMinAngleDegrees);

	/**
	 * Triangulates a simple polygon given counterclockwise (ear clipping).
	 * Duplicate and collinear points are tolerated.
	 * @param OutTriangles three polygon vertex indices per triangle.
	 * @return false if the polygon could not be fully triangulated (self-intersecting input).
	 */
	CHRONOROADS_API bool TriangulatePolygon(const TArray<FVector2D>& Polygon, TArray<int32>& OutTriangles);

	/** Signed area of a polygon: positive when counterclockwise. */
	CHRONOROADS_API double ComputePolygonArea(const TArray<FVector2D>& Polygon);
}
