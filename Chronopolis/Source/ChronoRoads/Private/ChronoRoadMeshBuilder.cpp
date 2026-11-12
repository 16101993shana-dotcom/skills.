#include "ChronoRoadMeshBuilder.h"

namespace ChronoRoadMeshBuilderPrivate
{
	/** One point of the road cross-section. */
	struct FProfilePoint
	{
		/** Lateral offset from the centerline, along the side vector. */
		double Offset = 0.0;
		/** Height above the carriageway. */
		double Height = 0.0;
	};

	/** Counterclockwise perpendicular of a tangent, in the ground plane. */
	FVector2D SideOf(const FVector2D& Tangent)
	{
		return FVector2D(-Tangent.Y, Tangent.X);
	}

	/**
	 * Cross-section, from one outer edge to the other:
	 * 0 side bottom, 1 outer top, 2 curb top, 3 curb bottom,
	 * 4 curb bottom, 5 curb top, 6 outer top, 7 side bottom.
	 * Without sidewalks, points 1 to 3 (and 4 to 6) coincide.
	 */
	void BuildProfile(const FChronoRoadProfileData& Profile, float SkirtDepthCm, FProfilePoint OutPoints[8])
	{
		const double HalfWidth = Profile.GetHalfWidthCm();
		const double HalfCarriageway = 0.5 * Profile.CarriagewayWidthCm;
		const double Curb = Profile.HasSidewalk() ? FMath::Max(Profile.CurbHeightCm, 0.0f) : 0.0;

		OutPoints[0] = { -HalfWidth, -static_cast<double>(SkirtDepthCm) };
		OutPoints[1] = { -HalfWidth, Curb };
		OutPoints[2] = { -HalfCarriageway, Curb };
		OutPoints[3] = { -HalfCarriageway, 0.0 };
		OutPoints[4] = { HalfCarriageway, 0.0 };
		OutPoints[5] = { HalfCarriageway, Curb };
		OutPoints[6] = { HalfWidth, Curb };
		OutPoints[7] = { HalfWidth, -static_cast<double>(SkirtDepthCm) };
	}

	FVector ProfilePointToWorld(const FVector& Center, const FVector2D& Side, const FProfilePoint& Point)
	{
		return Center + FVector(Side.X, Side.Y, 0.0) * Point.Offset + FVector(0.0, 0.0, Point.Height);
	}
}

bool ChronoRoadMeshBuilder::GetMouthCorners(const FChronoRoadGraph& Graph, const FChronoRoadSegment& Segment, int32 NodeId, double SampleSpacingCm, FVector2D& OutClockwise, FVector2D& OutCounterClockwise)
{
	using namespace ChronoRoadMeshBuilderPrivate;

	const bool bIsStart = (Segment.StartNodeId == NodeId);
	if (!bIsStart && Segment.EndNodeId != NodeId)
	{
		return false;
	}

	// Same polyline and same sampling as BuildSegmentMesh, so the corners are
	// exactly the corners of the ribbon.
	TArray<FChronoRoadSample> Samples;
	ChronoRoadGeometry::BuildPolyline(Segment.Curve, SampleSpacingCm, Samples);

	const double Distance = bIsStart ? static_cast<double>(Segment.TrimStart) : Segment.Length - Segment.TrimEnd;
	const FChronoRoadSample Sample = ChronoRoadGeometry::SampleAtDistance(Samples, Distance);

	// Side vector of the direction that leaves the node.
	const FVector2D LeavingTangent = bIsStart ? Sample.Tangent : -Sample.Tangent;
	const FVector2D Side = SideOf(LeavingTangent);
	const double HalfWidth = Segment.Profile.GetHalfWidthCm();

	OutClockwise = Sample.Position - Side * HalfWidth;
	OutCounterClockwise = Sample.Position + Side * HalfWidth;
	return true;
}

bool ChronoRoadMeshBuilder::BuildNodePolygon(const FChronoRoadGraph& Graph, int32 NodeId, double SampleSpacingCm, TArray<FVector2D>& OutPolygon, TArray<bool>& OutEdgeIsMouth)
{
	OutPolygon.Reset();
	OutEdgeIsMouth.Reset();

	const FChronoRoadNode* Node = Graph.FindNode(NodeId);
	if (!Node || Node->SegmentIds.Num() < 2)
	{
		return false;
	}

	TArray<FChronoRoadArm> Arms;
	Graph.GetNodeArms(NodeId, Arms);

	TArray<int32> Order;
	ChronoRoadGeometry::SortArmsCounterClockwise(Arms, Order);

	// Walking counterclockwise around the node, each road contributes its
	// clockwise corner first, then its counterclockwise corner.
	for (const int32 ArmIndex : Order)
	{
		const FChronoRoadSegment* Segment = Graph.FindSegment(Node->SegmentIds[ArmIndex]);
		FVector2D Clockwise;
		FVector2D CounterClockwise;
		if (!Segment || !GetMouthCorners(Graph, *Segment, NodeId, SampleSpacingCm, Clockwise, CounterClockwise))
		{
			return false;
		}

		OutPolygon.Add(Clockwise);
		OutEdgeIsMouth.Add(true);
		OutPolygon.Add(CounterClockwise);
		OutEdgeIsMouth.Add(false);
	}

	// Where two neighboring roads meet at their inner corner, the two corners
	// are the same point for straight roads but only nearly so for curved
	// ones, and may even be slightly crossed. Joining them keeps the outline
	// simple. The junction then differs from the ribbon corner by a few
	// centimeters at most, on a surface that is level and of the same material.
	const double JoinToleranceCm = 15.0;
	for (int32 Edge = 1; Edge < OutPolygon.Num(); Edge += 2)
	{
		FVector2D& A = OutPolygon[Edge];
		FVector2D& B = OutPolygon[(Edge + 1) % OutPolygon.Num()];
		if (FVector2D::Distance(A, B) <= JoinToleranceCm)
		{
			const FVector2D Middle = (A + B) * 0.5;
			A = Middle;
			B = Middle;
		}
	}

	return true;
}

void ChronoRoadMeshBuilder::BuildSegmentMesh(const FChronoRoadGraph& Graph, int32 SegmentId, const FChronoRoadMeshSettings& Settings, const FVector& Origin, FChronoMeshData& OutMesh)
{
	using namespace ChronoRoadMeshBuilderPrivate;

	OutMesh.Reset();

	const FChronoRoadSegment* Segment = Graph.FindSegment(SegmentId);
	if (!Segment || Segment->GetUsableLength() <= 1.0)
	{
		return;
	}

	TArray<FChronoRoadSample> FullSamples;
	ChronoRoadGeometry::BuildPolyline(Segment->Curve, Settings.SampleSpacingCm, FullSamples);

	TArray<FChronoRoadSample> Samples;
	ChronoRoadGeometry::ClipPolyline(FullSamples, Segment->TrimStart, Segment->Length - Segment->TrimEnd, Samples);
	const int32 NumSamples = Samples.Num();
	if (NumSamples < 2)
	{
		return;
	}

	// Per sample: center of the cross-section, side vector and 3D tangent.
	TArray<FVector> Centers;
	TArray<FVector2D> Sides;
	TArray<FVector> Tangents;
	Centers.Reserve(NumSamples);
	Sides.Reserve(NumSamples);
	Tangents.Reserve(NumSamples);
	for (const FChronoRoadSample& Sample : Samples)
	{
		Centers.Add(FVector(Sample.Position.X, Sample.Position.Y, Graph.GetSegmentHeightAtDistance(*Segment, Sample.Distance)) - Origin);
		Sides.Add(SideOf(Sample.Tangent));
	}
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		// The grade comes from the neighboring samples.
		const int32 Before = FMath::Max(Index - 1, 0);
		const int32 After = FMath::Min(Index + 1, NumSamples - 1);
		const double Run = FMath::Max(Samples[After].Distance - Samples[Before].Distance, 1.0);
		const double Grade = (Centers[After].Z - Centers[Before].Z) / Run;
		Tangents.Add(FVector(Samples[Index].Tangent.X, Samples[Index].Tangent.Y, Grade).GetSafeNormal());
	}

	FProfilePoint Profile[8];
	BuildProfile(Segment->Profile, Settings.SkirtDepthCm, Profile);

	// Distance along the cross-section outline, used as the U coordinate.
	double Perimeter[8];
	Perimeter[0] = 0.0;
	for (int32 Index = 1; Index < 8; ++Index)
	{
		const double DeltaOffset = Profile[Index].Offset - Profile[Index - 1].Offset;
		const double DeltaHeight = Profile[Index].Height - Profile[Index - 1].Height;
		Perimeter[Index] = Perimeter[Index - 1] + FMath::Sqrt(DeltaOffset * DeltaOffset + DeltaHeight * DeltaHeight);
	}

	const int32 StripMaterials[7] =
	{
		ChronoRoadMaterialSlot::Side,
		ChronoRoadMaterialSlot::Sidewalk,
		ChronoRoadMaterialSlot::Sidewalk,
		ChronoRoadMaterialSlot::Carriageway,
		ChronoRoadMaterialSlot::Sidewalk,
		ChronoRoadMaterialSlot::Sidewalk,
		ChronoRoadMaterialSlot::Side
	};

	// One strip of quads per pair of neighboring cross-section points. Each
	// strip has its own vertices so that curbs keep hard edges.
	for (int32 Strip = 0; Strip < 7; ++Strip)
	{
		const FProfilePoint& PointA = Profile[Strip];
		const FProfilePoint& PointB = Profile[Strip + 1];
		const double DeltaOffset = PointB.Offset - PointA.Offset;
		const double DeltaHeight = PointB.Height - PointA.Height;
		if (FMath::Abs(DeltaOffset) < 0.01 && FMath::Abs(DeltaHeight) < 0.01)
		{
			// Collapsed strip (road without sidewalks).
			continue;
		}

		int32 PreviousA = INDEX_NONE;
		int32 PreviousB = INDEX_NONE;
		FVector PreviousNormal = FVector::UpVector;

		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			const FVector SideVector(Sides[Index].X, Sides[Index].Y, 0.0);
			const FVector Across = SideVector * DeltaOffset + FVector(0.0, 0.0, DeltaHeight);

			// With offsets increasing along the side vector, tangent x across
			// points up for the flat parts and outward for the vertical parts.
			const FVector Normal = FVector::CrossProduct(Tangents[Index], Across).GetSafeNormal();

			const double V = Samples[Index].Distance * 0.01;
			const int32 VertexA = OutMesh.AddVertex(ProfilePointToWorld(Centers[Index], Sides[Index], PointA), Normal, FVector2D(Perimeter[Strip] * 0.01, V));
			const int32 VertexB = OutMesh.AddVertex(ProfilePointToWorld(Centers[Index], Sides[Index], PointB), Normal, FVector2D(Perimeter[Strip + 1] * 0.01, V));

			if (Index > 0)
			{
				const FVector Facing = PreviousNormal + Normal;
				OutMesh.AddTriangleFacing(PreviousA, PreviousB, VertexA, Facing, StripMaterials[Strip]);
				OutMesh.AddTriangleFacing(PreviousB, VertexB, VertexA, Facing, StripMaterials[Strip]);
			}

			PreviousA = VertexA;
			PreviousB = VertexB;
			PreviousNormal = Normal;
		}
	}

	// End caps close the cross-section at both ends: they are the visible end
	// of the sidewalks at a junction and the end of the road at a dead end.
	for (int32 End = 0; End < 2; ++End)
	{
		const int32 Index = (End == 0) ? 0 : NumSamples - 1;
		const FVector Tangent2D(Samples[Index].Tangent.X, Samples[Index].Tangent.Y, 0.0);
		const FVector Facing = (End == 0) ? -Tangent2D : Tangent2D;

		FVector Points[8];
		for (int32 PointIndex = 0; PointIndex < 8; ++PointIndex)
		{
			Points[PointIndex] = ProfilePointToWorld(Centers[Index], Sides[Index], Profile[PointIndex]);
		}

		// Three convex quads: one sidewalk block, the roadbed, the other sidewalk block.
		OutMesh.AddQuadFacing(Points[0], Points[1], Points[2], Points[3], Facing, ChronoRoadMaterialSlot::Side);
		OutMesh.AddQuadFacing(Points[0], Points[3], Points[4], Points[7], Facing, ChronoRoadMaterialSlot::Side);
		OutMesh.AddQuadFacing(Points[4], Points[5], Points[6], Points[7], Facing, ChronoRoadMaterialSlot::Side);
	}
}

void ChronoRoadMeshBuilder::BuildNodeMesh(const FChronoRoadGraph& Graph, int32 NodeId, const FChronoRoadMeshSettings& Settings, const FVector& Origin, FChronoMeshData& OutMesh)
{
	OutMesh.Reset();

	const FChronoRoadNode* Node = Graph.FindNode(NodeId);
	TArray<FVector2D> Polygon;
	TArray<bool> EdgeIsMouth;
	if (!Node || !BuildNodePolygon(Graph, NodeId, Settings.SampleSpacingCm, Polygon, EdgeIsMouth))
	{
		return;
	}

	TArray<int32> PolygonTriangles;
	if (!ChronoRoadGeometry::TriangulatePolygon(Polygon, PolygonTriangles))
	{
		// Degenerate junction (zero area): nothing to draw.
		return;
	}

	// The whole junction is level, at the height of the node.
	const double Height = Node->Position.Z;

	// Top surface.
	const int32 FirstTopVertex = OutMesh.Positions.Num();
	for (const FVector2D& Point : Polygon)
	{
		OutMesh.AddVertex(FVector(Point.X, Point.Y, Height) - Origin, FVector::UpVector, FVector2D(Point.X * 0.01, Point.Y * 0.01));
	}
	for (int32 Index = 0; Index + 2 < PolygonTriangles.Num(); Index += 3)
	{
		OutMesh.AddTriangleFacing(
			FirstTopVertex + PolygonTriangles[Index],
			FirstTopVertex + PolygonTriangles[Index + 1],
			FirstTopVertex + PolygonTriangles[Index + 2],
			FVector::UpVector,
			ChronoRoadMaterialSlot::Carriageway);
	}

	// Side faces along the edges that are not the end of a segment.
	const FVector Down(0.0, 0.0, -static_cast<double>(Settings.SkirtDepthCm));
	for (int32 Edge = 0; Edge < Polygon.Num(); ++Edge)
	{
		if (EdgeIsMouth[Edge])
		{
			continue;
		}

		const FVector2D& A2 = Polygon[Edge];
		const FVector2D& B2 = Polygon[(Edge + 1) % Polygon.Num()];
		const FVector2D EdgeVector = B2 - A2;
		if (EdgeVector.SizeSquared() < 0.01)
		{
			continue;
		}

		// Outward normal of a counterclockwise polygon edge.
		const FVector Outward(EdgeVector.Y, -EdgeVector.X, 0.0);

		const FVector A = FVector(A2.X, A2.Y, Height) - Origin;
		const FVector B = FVector(B2.X, B2.Y, Height) - Origin;
		OutMesh.AddQuadFacing(A, B, B + Down, A + Down, Outward, ChronoRoadMaterialSlot::Side);
	}
}
