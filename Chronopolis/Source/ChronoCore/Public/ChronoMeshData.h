// Plain triangle soup produced by the procedural builders (roads now,
// buildings later). It has no engine rendering dependency, so builders can be
// unit tested and the zero-clipping audit can inspect the exact geometry.

#pragma once

#include "CoreMinimal.h"

struct CHRONOCORE_API FChronoMeshData
{
	TArray<FVector> Positions;
	TArray<FVector3f> Normals;
	TArray<FVector2f> UVs;

	/** Three vertex indices per triangle. */
	TArray<int32> Triangles;

	/** One material slot index per triangle. */
	TArray<int32> MaterialIds;

	void Reset();
	bool IsEmpty() const { return Triangles.Num() == 0; }
	int32 NumTriangles() const { return Triangles.Num() / 3; }

	int32 AddVertex(const FVector& Position, const FVector& Normal, const FVector2D& UV);

	/**
	 * Adds a triangle whose front face points toward Facing, whatever the
	 * order of A, B and C. Degenerate triangles are skipped.
	 */
	void AddTriangleFacing(int32 A, int32 B, int32 C, const FVector& Facing, int32 MaterialId);

	/** Adds a flat quad (corners given around its outline) with its own four vertices. */
	void AddQuadFacing(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FVector& Facing, int32 MaterialId);

	/**
	 * Front-face direction of a triangle with the winding used by the engine
	 * geometry library: (C - A) x (B - A).
	 */
	static FVector GetWindingNormal(const FVector& A, const FVector& B, const FVector& C);

	/** Front-face direction of triangle TriangleIndex (not normalized). */
	FVector GetTriangleWindingNormal(int32 TriangleIndex) const;
};
