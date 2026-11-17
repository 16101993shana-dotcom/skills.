#include "ChronoMeshData.h"

void FChronoMeshData::Reset()
{
	Positions.Reset();
	Normals.Reset();
	UVs.Reset();
	Triangles.Reset();
	MaterialIds.Reset();
}

int32 FChronoMeshData::AddVertex(const FVector& Position, const FVector& Normal, const FVector2D& UV)
{
	Positions.Add(Position);
	Normals.Add(FVector3f(Normal));
	UVs.Add(FVector2f(static_cast<float>(UV.X), static_cast<float>(UV.Y)));
	return Positions.Num() - 1;
}

FVector FChronoMeshData::GetWindingNormal(const FVector& A, const FVector& B, const FVector& C)
{
	return FVector::CrossProduct(C - A, B - A);
}

FVector FChronoMeshData::GetTriangleWindingNormal(int32 TriangleIndex) const
{
	const int32 Base = TriangleIndex * 3;
	if (!Triangles.IsValidIndex(Base + 2))
	{
		return FVector::ZeroVector;
	}

	return GetWindingNormal(Positions[Triangles[Base]], Positions[Triangles[Base + 1]], Positions[Triangles[Base + 2]]);
}

void FChronoMeshData::AddTriangleFacing(int32 A, int32 B, int32 C, const FVector& Facing, int32 MaterialId)
{
	if (!Positions.IsValidIndex(A) || !Positions.IsValidIndex(B) || !Positions.IsValidIndex(C))
	{
		return;
	}

	const FVector Winding = GetWindingNormal(Positions[A], Positions[B], Positions[C]);

	// Twice the triangle area, in cm2. Slivers below this add nothing visible.
	if (Winding.SizeSquared() < 1.0e-4)
	{
		return;
	}

	if (FVector::DotProduct(Winding, Facing) < 0.0)
	{
		Swap(B, C);
	}

	Triangles.Add(A);
	Triangles.Add(B);
	Triangles.Add(C);
	MaterialIds.Add(MaterialId);
}

void FChronoMeshData::AddQuadFacing(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FVector& Facing, int32 MaterialId)
{
	const FVector Normal = Facing.GetSafeNormal();

	// UVs in meters along the two edges that start at P0.
	const double U = FVector::Distance(P0, P1) * 0.01;
	const double V = FVector::Distance(P0, P3) * 0.01;

	const int32 I0 = AddVertex(P0, Normal, FVector2D(0.0, 0.0));
	const int32 I1 = AddVertex(P1, Normal, FVector2D(U, 0.0));
	const int32 I2 = AddVertex(P2, Normal, FVector2D(U, V));
	const int32 I3 = AddVertex(P3, Normal, FVector2D(0.0, V));

	AddTriangleFacing(I0, I1, I2, Normal, MaterialId);
	AddTriangleFacing(I0, I2, I3, Normal, MaterialId);
}
