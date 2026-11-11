// Plain C++ heightfield: the single source of truth for ground elevation.
// Roads, buildings, the camera and the zero-clipping audit all read this
// structure, never the render mesh. It has no UObject or rendering dependency.

#pragma once

#include "CoreMinimal.h"
#include "Math/IntRect.h"

/** How FChronoHeightfield::ConformToPath writes heights under the path. */
enum class EChronoConformMode : uint8
{
	/** Set the ground under the path and blend the surroundings toward it. */
	BlendAndSet,
	/** Set the ground under the path, leave the surroundings untouched. */
	Set,
	/** Only lower the ground under the path, never raise it. */
	Min
};

struct CHRONOTERRAIN_API FChronoHeightfield
{
public:
	/** Allocates a square grid of InSizeCells x InSizeCells cells, centered on the world origin. */
	void Init(int32 InSizeCells, float InCellSizeCm, float InitialHeightCm);

	bool IsValid() const { return SizeCells > 0 && Heights.Num() == GetNumVertsPerSide() * GetNumVertsPerSide(); }

	int32 GetSizeCells() const { return SizeCells; }
	int32 GetNumVertsPerSide() const { return SizeCells + 1; }
	float GetCellSizeCm() const { return CellSizeCm; }
	float GetWorldSizeCm() const { return static_cast<float>(SizeCells) * CellSizeCm; }

	/** World XY of vertex (0,0). */
	FVector2D GetOrigin() const { return FVector2D(-0.5 * GetWorldSizeCm(), -0.5 * GetWorldSizeCm()); }

	bool IsInsideWorld(const FVector2D& WorldXY) const;
	FVector2D ClampToWorld(const FVector2D& WorldXY) const;
	FVector2D VertexToWorld(int32 X, int32 Y) const;

	/** Vertex accessors. Coordinates are clamped to the grid. */
	float GetVertexHeight(int32 X, int32 Y) const;
	void SetVertexHeight(int32 X, int32 Y, float HeightCm);
	FVector GetVertexNormal(int32 X, int32 Y) const;

	/**
	 * Height at any world position. Uses the exact triangulation of the render
	 * mesh (each cell split along the (0,0)-(1,1) diagonal), so an object placed
	 * at this height sits on the visible surface with no gap and no penetration.
	 */
	float SampleHeight(const FVector2D& WorldXY) const;
	FVector SampleNormal(const FVector2D& WorldXY) const;
	float SampleSlopeDegrees(const FVector2D& WorldXY) const;

	// ---- Edit operations ----
	// Each returns the modified vertex rectangle: Min inclusive, Max exclusive.
	// An empty rectangle (zero area) means nothing changed.

	/** Adds DeltaCm at the center with a smooth falloff to zero at RadiusCm. Negative DeltaCm digs. */
	FIntRect ApplyRaiseBrush(const FVector2D& Center, float RadiusCm, float DeltaCm, float MinHeightCm, float MaxHeightCm);

	/** Moves heights toward TargetHeightCm. Strength is in [0,1] at the center. */
	FIntRect ApplyFlattenBrush(const FVector2D& Center, float RadiusCm, float TargetHeightCm, float Strength);

	/** Moves heights toward the average of their neighbors. Strength is in [0,1] at the center. */
	FIntRect ApplySmoothBrush(const FVector2D& Center, float RadiusCm, float Strength);

	/**
	 * Makes the ground exactly flat at TargetHeightCm under an oriented rectangle
	 * (building footprint, road segment piece), then blends back to the existing
	 * ground over SkirtCm. Flatness is guaranteed inside the whole rectangle.
	 */
	FIntRect FlattenOrientedBox(const FVector2D& Center, const FVector2D& HalfExtentCm, float YawRadians, float TargetHeightCm, float SkirtCm);

	/**
	 * Shapes the ground under a ribbon that follows a 3D polyline (a road).
	 * Every vertex closer than HalfWidthCm (plus one cell diagonal) to the path
	 * gets the height of the nearest path point minus OffsetBelowCm. A single
	 * point gives a disc (an intersection).
	 */
	FIntRect ConformToPath(TConstArrayView<FVector> Path, float HalfWidthCm, float OffsetBelowCm, float BlendCm, EChronoConformMode Mode);

	/** Lowest and highest ground height under an oriented rectangle. Used to validate placements. */
	void GetHeightRangeInOrientedBox(const FVector2D& Center, const FVector2D& HalfExtentCm, float YawRadians, float& OutMinCm, float& OutMaxCm) const;

	/** Binary serialization for save games (both directions). */
	void Serialize(FArchive& Ar);

	const TArray<float>& GetHeights() const { return Heights; }

private:
	int32 Index(int32 X, int32 Y) const { return Y * (SizeCells + 1) + X; }

	/** Vertex rectangle covering a world-space box, clamped to the grid. */
	FIntRect ComputeVertexRect(const FVector2D& WorldMin, const FVector2D& WorldMax) const;

	static float SmoothFalloff(float Distance, float Radius);

	int32 SizeCells = 0;
	float CellSizeCm = 200.0f;
	TArray<float> Heights;
};
