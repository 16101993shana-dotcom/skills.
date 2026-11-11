#include "ChronoClippingAudit.h"

#include "ChronoHeightfield.h"
#include "ChronoRoadBuilder.h"
#include "ChronoRoadGeometry.h"
#include "ChronoRoadGraph.h"
#include "ChronoRoadMeshBuilder.h"

namespace ChronoAuditRule
{
	const FName TerrainThroughRoad(TEXT("TerrainThroughRoad"));
	const FName RoadFloating(TEXT("RoadFloating"));
	const FName RoadOverlap(TEXT("RoadOverlap"));
	const FName JunctionAngle(TEXT("JunctionAngle"));
	const FName SegmentSwallowed(TEXT("SegmentSwallowed"));
	const FName JunctionMesh(TEXT("JunctionMesh"));
	const FName JunctionStep(TEXT("JunctionStep"));
}

namespace ChronoClippingAuditPrivate
{
	struct FContext
	{
		const FChronoHeightfield& Heightfield;
		const FChronoRoadGraph& Graph;
		const FChronoRoadRules& Rules;
		const FChronoAuditSettings& Settings;
		TArray<FChronoAuditIssue>& Issues;
		int32 NumAdded = 0;

		bool IsFull() const { return Issues.Num() >= Settings.MaxIssues; }

		void Add(const FName& Rule, const FVector& Location, FString&& Description)
		{
			if (!IsFull())
			{
				FChronoAuditIssue& Issue = Issues.AddDefaulted_GetRef();
				Issue.Rule = Rule;
				Issue.Location = Location;
				Issue.Description = MoveTemp(Description);
				++NumAdded;
			}
		}
	};

	/**
	 * Compares the ground with a road surface at one point.
	 * @param SurfaceHeight height of the visible surface at that point.
	 * @param RoadHeight height of the carriageway there (the side faces go down from it).
	 * @return false if an issue was reported.
	 */
	bool CheckGround(FContext& Context, const FVector2D& Point, double SurfaceHeight, double RoadHeight, const TCHAR* What, int32 Id)
	{
		const double Ground = Context.Heightfield.SampleHeight(Point);

		if (Ground > SurfaceHeight - Context.Settings.MinGroundGapCm)
		{
			Context.Add(ChronoAuditRule::TerrainThroughRoad, FVector(Point.X, Point.Y, SurfaceHeight),
				FString::Printf(TEXT("%s %d: ground at %.1f cm, surface at %.1f cm"), What, Id, Ground, SurfaceHeight));
			return false;
		}

		if (Ground < RoadHeight - Context.Rules.SkirtDepthCm)
		{
			Context.Add(ChronoAuditRule::RoadFloating, FVector(Point.X, Point.Y, RoadHeight),
				FString::Printf(TEXT("%s %d: ground at %.1f cm is %.1f cm below the road, side faces are %.1f cm deep"), What, Id, Ground, RoadHeight - Ground, Context.Rules.SkirtDepthCm));
			return false;
		}

		return true;
	}

	/** Terrain against every segment ribbon, between its junctions. */
	void AuditSegmentsAgainstTerrain(FContext& Context)
	{
		TArray<FChronoRoadSample> FullSamples;
		TArray<FChronoRoadSample> Samples;

		for (const TPair<int32, FChronoRoadSegment>& Pair : Context.Graph.GetSegments())
		{
			if (Context.IsFull())
			{
				return;
			}

			const FChronoRoadSegment& Segment = Pair.Value;
			if (Segment.GetUsableLength() <= 1.0)
			{
				Context.Add(ChronoAuditRule::SegmentSwallowed, FVector(Segment.Curve.Evaluate(0.5), 0.0),
					FString::Printf(TEXT("Segment %d: usable length %.1f cm"), Segment.Id, Segment.GetUsableLength()));
				continue;
			}

			ChronoRoadGeometry::BuildPolyline(Segment.Curve, Context.Rules.SampleSpacingCm, FullSamples);
			ChronoRoadGeometry::ClipPolyline(FullSamples, Segment.TrimStart, Segment.Length - Segment.TrimEnd, Samples);

			const double HalfWidth = Segment.Profile.GetHalfWidthCm();
			const double HalfCarriageway = 0.5 * Segment.Profile.CarriagewayWidthCm;
			const double Curb = Segment.Profile.HasSidewalk() ? Segment.Profile.CurbHeightCm : 0.0;

			// Across the road: both outer edges, both curbs and the centerline.
			// The curb points use the carriageway height, the stricter of the two.
			const double Offsets[5] = { -HalfWidth, -HalfCarriageway, 0.0, HalfCarriageway, HalfWidth };
			const double Raise[5] = { Curb, 0.0, 0.0, 0.0, Curb };

			bool bSegmentClean = true;
			for (int32 SampleIndex = 0; SampleIndex < Samples.Num() && bSegmentClean; ++SampleIndex)
			{
				const FChronoRoadSample& Sample = Samples[SampleIndex];
				const double RoadHeight = Context.Graph.GetSegmentHeightAtDistance(Segment, Sample.Distance);
				const FVector2D Side(-Sample.Tangent.Y, Sample.Tangent.X);

				for (int32 OffsetIndex = 0; OffsetIndex < 5 && bSegmentClean; ++OffsetIndex)
				{
					// One issue per segment is enough to locate the problem.
					bSegmentClean = CheckGround(Context, Sample.Position + Side * Offsets[OffsetIndex], RoadHeight + Raise[OffsetIndex], RoadHeight, TEXT("Segment"), Segment.Id);
				}
			}
		}
	}

	/** Junction outlines: mesh validity, height continuity and terrain. */
	void AuditJunctions(FContext& Context)
	{
		TArray<FVector2D> Polygon;
		TArray<bool> EdgeIsMouth;
		TArray<int32> Triangles;

		for (const TPair<int32, FChronoRoadNode>& Pair : Context.Graph.GetNodes())
		{
			if (Context.IsFull())
			{
				return;
			}

			const FChronoRoadNode& Node = Pair.Value;
			if (Node.SegmentIds.Num() < 2)
			{
				continue;
			}

			const float MinAngle = Context.Graph.GetNodeMinAngleDegrees(Node.Id);
			if (MinAngle < Context.Rules.MinIntersectionAngleDegrees - 0.01f)
			{
				Context.Add(ChronoAuditRule::JunctionAngle, Node.Position,
					FString::Printf(TEXT("Node %d: smallest angle %.1f degrees, minimum %.1f"), Node.Id, MinAngle, Context.Rules.MinIntersectionAngleDegrees));
			}

			// Every road must arrive at the height of the junction.
			for (const int32 SegmentId : Node.SegmentIds)
			{
				const FChronoRoadSegment* Segment = Context.Graph.FindSegment(SegmentId);
				if (!Segment)
				{
					continue;
				}

				const double MouthDistance = (Segment->StartNodeId == Node.Id) ? static_cast<double>(Segment->TrimStart) : Segment->Length - Segment->TrimEnd;
				const double Step = FMath::Abs(Context.Graph.GetSegmentHeightAtDistance(*Segment, MouthDistance) - Node.Position.Z);
				if (Step > Context.Settings.MaxJunctionStepCm)
				{
					Context.Add(ChronoAuditRule::JunctionStep, Node.Position,
						FString::Printf(TEXT("Node %d: segment %d arrives %.2f cm off the junction height"), Node.Id, SegmentId, Step));
				}
			}

			if (!ChronoRoadMeshBuilder::BuildNodePolygon(Context.Graph, Node.Id, Context.Rules.SampleSpacingCm, Polygon, EdgeIsMouth))
			{
				Context.Add(ChronoAuditRule::JunctionMesh, Node.Position, FString::Printf(TEXT("Node %d: no junction outline"), Node.Id));
				continue;
			}

			// A straight joint between two roads has a valid outline of zero
			// area: nothing to triangulate, nothing to check against the ground.
			if (FMath::Abs(ChronoRoadGeometry::ComputePolygonArea(Polygon)) < 1.0)
			{
				continue;
			}

			if (!ChronoRoadGeometry::TriangulatePolygon(Polygon, Triangles))
			{
				Context.Add(ChronoAuditRule::JunctionMesh, Node.Position, FString::Printf(TEXT("Node %d: junction outline cannot be triangulated"), Node.Id));
				continue;
			}

			// Terrain under the junction: every corner and the middle of every triangle.
			bool bClean = true;
			for (int32 Index = 0; Index < Polygon.Num() && bClean; ++Index)
			{
				bClean = CheckGround(Context, Polygon[Index], Node.Position.Z, Node.Position.Z, TEXT("Node"), Node.Id);
			}
			for (int32 Index = 0; Index + 2 < Triangles.Num() && bClean; Index += 3)
			{
				const FVector2D Middle = (Polygon[Triangles[Index]] + Polygon[Triangles[Index + 1]] + Polygon[Triangles[Index + 2]]) / 3.0;
				bClean = CheckGround(Context, Middle, Node.Position.Z, Node.Position.Z, TEXT("Node"), Node.Id);
			}
		}
	}

	/** Every pair of segments: one ribbon must not enter the other. */
	void AuditRoadOverlaps(FContext& Context)
	{
		struct FRibbon
		{
			const FChronoRoadSegment* Segment = nullptr;
			TArray<FChronoRoadSample> Samples;
		};

		// Ribbons between junctions, built once.
		TArray<FRibbon> Ribbons;
		TArray<FChronoRoadSample> FullSamples;
		for (const TPair<int32, FChronoRoadSegment>& Pair : Context.Graph.GetSegments())
		{
			if (Pair.Value.GetUsableLength() > 1.0)
			{
				FRibbon& Ribbon = Ribbons.AddDefaulted_GetRef();
				Ribbon.Segment = &Pair.Value;
				ChronoRoadGeometry::BuildPolyline(Pair.Value.Curve, Context.Rules.SampleSpacingCm, FullSamples);
				ChronoRoadGeometry::ClipPolyline(FullSamples, Pair.Value.TrimStart, Pair.Value.Length - Pair.Value.TrimEnd, Ribbon.Samples);
			}
		}

		const double Tolerance = Context.Settings.OverlapToleranceCm;

		for (int32 IndexA = 0; IndexA < Ribbons.Num(); ++IndexA)
		{
			for (int32 IndexB = 0; IndexB < Ribbons.Num(); ++IndexB)
			{
				if (Context.IsFull())
				{
					return;
				}
				if (IndexA == IndexB)
				{
					continue;
				}

				const FRibbon& A = Ribbons[IndexA];
				const FRibbon& B = Ribbons[IndexB];
				if (!A.Segment->Bounds.Intersect(B.Segment->Bounds))
				{
					continue;
				}

				// Points on both edges of A must not be inside B. Shrinking B
				// by the tolerance ignores edges that merely touch.
				const double HalfWidthA = A.Segment->Profile.GetHalfWidthCm();
				const double HalfWidthB = B.Segment->Profile.GetHalfWidthCm() - Tolerance;

				bool bOverlap = false;
				FVector2D Where = FVector2D::ZeroVector;
				for (int32 SampleIndex = 0; SampleIndex < A.Samples.Num() && !bOverlap; ++SampleIndex)
				{
					const FChronoRoadSample& Sample = A.Samples[SampleIndex];
					const FVector2D Side(-Sample.Tangent.Y, Sample.Tangent.X);
					for (const double Sign : { -1.0, 1.0 })
					{
						const FVector2D EdgePoint = Sample.Position + Side * (HalfWidthA * Sign);
						if (ChronoRoadGeometry::IsPointInsideRibbon(EdgePoint, B.Samples, HalfWidthB))
						{
							bOverlap = true;
							Where = EdgePoint;
							break;
						}
					}
				}

				if (bOverlap)
				{
					Context.Add(ChronoAuditRule::RoadOverlap, FVector(Where.X, Where.Y, Context.Heightfield.SampleHeight(Where)),
						FString::Printf(TEXT("Segment %d overlaps segment %d"), A.Segment->Id, B.Segment->Id));
				}
			}
		}
	}
}

int32 ChronoClippingAudit::Run(const FChronoHeightfield& Heightfield, const FChronoRoadGraph& Graph, const FChronoRoadRules& Rules, const FChronoAuditSettings& Settings, TArray<FChronoAuditIssue>& OutIssues)
{
	using namespace ChronoClippingAuditPrivate;

	FContext Context{ Heightfield, Graph, Rules, Settings, OutIssues };
	if (!Heightfield.IsValid())
	{
		return 0;
	}

	AuditSegmentsAgainstTerrain(Context);
	AuditJunctions(Context);
	AuditRoadOverlaps(Context);
	return Context.NumAdded;
}
