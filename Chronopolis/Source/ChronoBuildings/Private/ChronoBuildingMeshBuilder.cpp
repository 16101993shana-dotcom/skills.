#include "ChronoBuildingMeshBuilder.h"

namespace ChronoBuildingMeshBuilderPrivate
{
	/** Four vertical faces between two heights, around a counterclockwise outline. */
	void AppendWalls(const FVector2D Corners[4], double BottomHeight, double TopHeight, const FVector& Origin, int32 MaterialId, FChronoMeshData& OutMesh)
	{
		if (TopHeight - BottomHeight < 0.1)
		{
			return;
		}

		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector2D& A = Corners[Index];
			const FVector2D& B = Corners[(Index + 1) % 4];
			const FVector2D Edge = B - A;

			// Outward normal of a counterclockwise outline edge.
			const FVector Outward(Edge.Y, -Edge.X, 0.0);

			OutMesh.AddQuadFacing(
				FVector(A.X, A.Y, BottomHeight) - Origin,
				FVector(B.X, B.Y, BottomHeight) - Origin,
				FVector(B.X, B.Y, TopHeight) - Origin,
				FVector(A.X, A.Y, TopHeight) - Origin,
				Outward,
				MaterialId);
		}
	}

	/** One triangle with its own three vertices. */
	void AppendTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector& Facing, int32 MaterialId, FChronoMeshData& OutMesh)
	{
		const FVector Normal = Facing.GetSafeNormal();
		const int32 IndexA = OutMesh.AddVertex(A, Normal, FVector2D(0.0, 0.0));
		const int32 IndexB = OutMesh.AddVertex(B, Normal, FVector2D(FVector::Distance(A, B) * 0.01, 0.0));
		const int32 IndexC = OutMesh.AddVertex(C, Normal, FVector2D(0.0, FVector::Distance(A, C) * 0.01));
		OutMesh.AddTriangleFacing(IndexA, IndexB, IndexC, Normal, MaterialId);
	}
}

void ChronoBuildingMeshBuilder::AppendFoundation(const FChronoBuilding& Building, const FVector& Origin, FChronoMeshData& OutMesh)
{
	FVector2D Corners[4];
	Building.Footprint.GetCorners(Corners);
	ChronoBuildingMeshBuilderPrivate::AppendWalls(Corners, Building.FoundationBottomCm, Building.FloorHeightCm, Origin, ChronoBuildingMaterialSlot::Foundation, OutMesh);
}

void ChronoBuildingMeshBuilder::AppendBuilding(const FChronoBuilding& Building, const FVector& Origin, FChronoMeshData& OutMesh)
{
	using namespace ChronoBuildingMeshBuilderPrivate;

	// Counterclockwise: 0 (-x,-y), 1 (+x,-y), 2 (+x,+y), 3 (-x,+y) in the frame of the footprint.
	FVector2D Corners[4];
	Building.Footprint.GetCorners(Corners);

	const double Floor = Building.FloorHeightCm;
	const double Eave = Floor + Building.BodyHeightCm;

	// Foundation: from below the lowest ground up to the floor, so the
	// building meets the ground all around whatever the slope.
	AppendWalls(Corners, Building.FoundationBottomCm, Floor, Origin, ChronoBuildingMaterialSlot::Foundation, OutMesh);
	AppendWalls(Corners, Floor, Eave, Origin, ChronoBuildingMaterialSlot::Wall, OutMesh);

	auto At = [&Origin](const FVector2D& Point, double Height)
	{
		return FVector(Point.X, Point.Y, Height) - Origin;
	};

	if (Building.Roof == EChronoRoofType::Gable && Building.RoofHeightCm > 1.0f)
	{
		// Ridge along the X axis of the footprint, above the middle of its depth.
		const double Ridge = Eave + Building.RoofHeightCm;
		const FVector2D RidgeStart = (Corners[0] + Corners[3]) * 0.5;
		const FVector2D RidgeEnd = (Corners[1] + Corners[2]) * 0.5;
		const FVector2D AxisY = Building.Footprint.GetAxisY();
		const double Rise = Building.RoofHeightCm;
		const double Run = Building.Footprint.HalfExtent.Y;

		// Two slopes. Each normal leans outward by the pitch of the roof.
		const FVector SlopeNormalFront(-AxisY.X * Rise, -AxisY.Y * Rise, Run);
		const FVector SlopeNormalBack(AxisY.X * Rise, AxisY.Y * Rise, Run);
		OutMesh.AddQuadFacing(At(Corners[0], Eave), At(Corners[1], Eave), At(RidgeEnd, Ridge), At(RidgeStart, Ridge), SlopeNormalFront, ChronoBuildingMaterialSlot::Roof);
		OutMesh.AddQuadFacing(At(Corners[2], Eave), At(Corners[3], Eave), At(RidgeStart, Ridge), At(RidgeEnd, Ridge), SlopeNormalBack, ChronoBuildingMaterialSlot::Roof);

		// Two gable ends, in wall material.
		const FVector2D AxisX = Building.Footprint.GetAxisX();
		AppendTriangle(At(Corners[3], Eave), At(Corners[0], Eave), At(RidgeStart, Ridge), FVector(-AxisX.X, -AxisX.Y, 0.0), ChronoBuildingMaterialSlot::Wall, OutMesh);
		AppendTriangle(At(Corners[1], Eave), At(Corners[2], Eave), At(RidgeEnd, Ridge), FVector(AxisX.X, AxisX.Y, 0.0), ChronoBuildingMaterialSlot::Wall, OutMesh);
	}
	else
	{
		// Flat roof: the walls rise to the roof line and a single slab closes the top.
		const double RoofLine = Eave + FMath::Max(Building.RoofHeightCm, 0.0f);
		AppendWalls(Corners, Eave, RoofLine, Origin, ChronoBuildingMaterialSlot::Wall, OutMesh);
		OutMesh.AddQuadFacing(At(Corners[0], RoofLine), At(Corners[1], RoofLine), At(Corners[2], RoofLine), At(Corners[3], RoofLine), FVector::UpVector, ChronoBuildingMaterialSlot::Roof);
	}
}
