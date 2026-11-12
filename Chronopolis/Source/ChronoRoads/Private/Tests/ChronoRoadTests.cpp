// Automation tests for road geometry, the road graph and the mesh builder.
// Run from Tools > Session Frontend > Automation, filter "Chronopolis.Roads".

#include "ChronoMeshData.h"
#include "ChronoRoadGeometry.h"
#include "ChronoRoadGraph.h"
#include "ChronoRoadMeshBuilder.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ChronoRoadTests
{
	FChronoRoadProfileData MakeStreetProfile()
	{
		FChronoRoadProfileData Profile;
		Profile.ProfileId = TEXT("TestStreet");
		Profile.CarriagewayWidthCm = 600.0f;
		Profile.SidewalkWidthCm = 200.0f;
		Profile.CurbHeightCm = 15.0f;
		return Profile;
	}

	/** Sum of the areas of the triangles of a mesh that use one material slot, projected on the ground. */
	double ComputeGroundArea(const FChronoMeshData& Mesh, int32 MaterialId)
	{
		double Area = 0.0;
		for (int32 Triangle = 0; Triangle < Mesh.NumTriangles(); ++Triangle)
		{
			if (Mesh.MaterialIds[Triangle] == MaterialId)
			{
				Area += 0.5 * FMath::Abs(Mesh.GetTriangleWindingNormal(Triangle).Z);
			}
		}
		return Area;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoRoadTrimTest, "Chronopolis.Roads.Geometry.JunctionTrims",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoRoadTrimTest::RunTest(const FString& Parameters)
{
	const float HalfWidth = 500.0f;

	// Four roads of the same width meeting at right angles: the edges cross
	// exactly one half width away from the node.
	TArray<FChronoRoadArm> Cross;
	Cross.Add({ FVector2D(1.0, 0.0), HalfWidth });
	Cross.Add({ FVector2D(0.0, 1.0), HalfWidth });
	Cross.Add({ FVector2D(-1.0, 0.0), HalfWidth });
	Cross.Add({ FVector2D(0.0, -1.0), HalfWidth });

	TArray<float> Trims;
	float MinAngle = 0.0f;
	ChronoRoadGeometry::ComputeJunctionTrims(Cross, 100.0f, Trims, MinAngle);

	TestEqual(TEXT("One trim per arm"), Trims.Num(), 4);
	for (const float Trim : Trims)
	{
		TestTrue(TEXT("Right angle cross trims by one half width"), FMath::IsNearlyEqual(Trim, HalfWidth, 0.5f));
	}
	TestTrue(TEXT("Smallest angle of a cross is 90 degrees"), FMath::IsNearlyEqual(MinAngle, 90.0f, 0.01f));

	// A dead end is not trimmed.
	TArray<FChronoRoadArm> DeadEnd;
	DeadEnd.Add({ FVector2D(1.0, 0.0), HalfWidth });
	ChronoRoadGeometry::ComputeJunctionTrims(DeadEnd, 100.0f, Trims, MinAngle);
	TestTrue(TEXT("Dead end has no trim"), Trims.Num() == 1 && Trims[0] == 0.0f);

	// A straight joint only gets the minimum trim.
	TArray<FChronoRoadArm> Straight;
	Straight.Add({ FVector2D(1.0, 0.0), HalfWidth });
	Straight.Add({ FVector2D(-1.0, 0.0), HalfWidth });
	ChronoRoadGeometry::ComputeJunctionTrims(Straight, 100.0f, Trims, MinAngle);
	TestTrue(TEXT("Straight joint uses the minimum trim"), FMath::IsNearlyEqual(Trims[0], 100.0f, 0.01f) && FMath::IsNearlyEqual(Trims[1], 100.0f, 0.01f));

	// A sharper angle needs a longer trim.
	TArray<FChronoRoadArm> Sharp;
	Sharp.Add({ FVector2D(1.0, 0.0), HalfWidth });
	Sharp.Add({ FVector2D(FMath::Cos(FMath::DegreesToRadians(45.0)), FMath::Sin(FMath::DegreesToRadians(45.0))), HalfWidth });
	ChronoRoadGeometry::ComputeJunctionTrims(Sharp, 100.0f, Trims, MinAngle);
	TestTrue(TEXT("45 degree bend trims more than a right angle"), Trims[0] > HalfWidth * 2.0f);
	TestTrue(TEXT("Smallest angle of the bend is 45 degrees"), FMath::IsNearlyEqual(MinAngle, 45.0f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoRoadTriangulationTest, "Chronopolis.Roads.Geometry.Triangulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoRoadTriangulationTest::RunTest(const FString& Parameters)
{
	// Concave L shape, counterclockwise, with one duplicated point.
	TArray<FVector2D> Polygon;
	Polygon.Add(FVector2D(0.0, 0.0));
	Polygon.Add(FVector2D(200.0, 0.0));
	Polygon.Add(FVector2D(200.0, 100.0));
	Polygon.Add(FVector2D(200.0, 100.0));
	Polygon.Add(FVector2D(100.0, 100.0));
	Polygon.Add(FVector2D(100.0, 200.0));
	Polygon.Add(FVector2D(0.0, 200.0));

	TArray<int32> Triangles;
	TestTrue(TEXT("L shape is triangulated"), ChronoRoadGeometry::TriangulatePolygon(Polygon, Triangles));

	double TriangleArea = 0.0;
	for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
	{
		const FVector2D& A = Polygon[Triangles[Index]];
		const FVector2D& B = Polygon[Triangles[Index + 1]];
		const FVector2D& C = Polygon[Triangles[Index + 2]];
		const double Signed = 0.5 * ((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X));
		TestTrue(TEXT("Triangles keep the polygon orientation"), Signed > 0.0);
		TriangleArea += Signed;
	}

	// No overlap and no hole: the triangles cover exactly the polygon.
	TestTrue(TEXT("Triangles cover exactly the polygon"), FMath::IsNearlyEqual(TriangleArea, ChronoRoadGeometry::ComputePolygonArea(Polygon), 0.01));
	TestTrue(TEXT("L shape area is 30000 cm2"), FMath::IsNearlyEqual(TriangleArea, 30000.0, 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChronoRoadJunctionMeshTest, "Chronopolis.Roads.Mesh.JunctionFitsSegments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChronoRoadJunctionMeshTest::RunTest(const FString& Parameters)
{
	const FChronoRoadProfileData Profile = ChronoRoadTests::MakeStreetProfile();
	const double HalfWidth = Profile.GetHalfWidthCm();

	// T junction on a slope: a through road split in the middle, plus a branch.
	FChronoRoadGraph Graph;
	Graph.SetMinTrimCm(100.0f);
	const int32 West = Graph.AddNode(FVector(-6000.0, 0.0, 0.0));
	const int32 East = Graph.AddNode(FVector(6000.0, 0.0, 300.0));
	const int32 North = Graph.AddNode(FVector(0.0, 6000.0, 200.0));

	const FVector2D WestXY(-6000.0, 0.0);
	const FVector2D EastXY(6000.0, 0.0);
	const int32 Through = Graph.AddSegment(West, East, FChronoBezier2D::MakeLine(WestXY, EastXY), Profile);
	TestTrue(TEXT("Through road is created"), Through != INDEX_NONE);

	const int32 Center = Graph.SplitSegment(Through, 0.5);
	TestTrue(TEXT("Split creates a node"), Center != INDEX_NONE);
	TestEqual(TEXT("Split gives two segments"), Graph.GetSegments().Num(), 2);

	const FChronoRoadNode* CenterNode = Graph.FindNode(Center);
	TestTrue(TEXT("Split node sits on the road surface"), CenterNode && FMath::IsNearlyEqual(CenterNode->Position.Z, 150.0, 1.0));

	const int32 Branch = Graph.AddSegment(Center, North, FChronoBezier2D::MakeLine(FVector2D(0.0, 0.0), FVector2D(0.0, 6000.0)), Profile);
	TestTrue(TEXT("Branch is created"), Branch != INDEX_NONE);
	TestEqual(TEXT("Junction has three roads"), CenterNode ? Graph.FindNode(Center)->SegmentIds.Num() : 0, 3);
	TestTrue(TEXT("Smallest angle of a T junction is 90 degrees"), FMath::IsNearlyEqual(Graph.GetNodeMinAngleDegrees(Center), 90.0f, 0.1f));

	// The branch is cut back by the half width of the through road.
	const FChronoRoadSegment* BranchSegment = Graph.FindSegment(Branch);
	TestTrue(TEXT("Branch trim equals the half width of the crossed road"), BranchSegment && FMath::IsNearlyEqual(static_cast<double>(BranchSegment->TrimStart), HalfWidth, 1.0));

	FChronoRoadMeshSettings Settings;
	Settings.SampleSpacingCm = 200.0;
	Settings.SkirtDepthCm = 40.0f;

	// Junction outline: counterclockwise, with a positive area.
	TArray<FVector2D> Polygon;
	TArray<bool> EdgeIsMouth;
	TestTrue(TEXT("Junction outline is built"), ChronoRoadMeshBuilder::BuildNodePolygon(Graph, Center, Settings.SampleSpacingCm, Polygon, EdgeIsMouth));
	TestEqual(TEXT("Two outline points per road"), Polygon.Num(), 6);
	const double PolygonArea = ChronoRoadGeometry::ComputePolygonArea(Polygon);
	TestTrue(TEXT("Junction outline is counterclockwise"), PolygonArea > 0.0);

	// Junction mesh: its top surface covers exactly the outline and faces up.
	FChronoMeshData NodeMesh;
	ChronoRoadMeshBuilder::BuildNodeMesh(Graph, Center, Settings, FVector::ZeroVector, NodeMesh);
	TestFalse(TEXT("Junction mesh is not empty"), NodeMesh.IsEmpty());
	TestTrue(TEXT("Junction surface covers exactly its outline"), FMath::IsNearlyEqual(ChronoRoadTests::ComputeGroundArea(NodeMesh, ChronoRoadMaterialSlot::Carriageway), PolygonArea, 1.0));

	bool bAllTopFacesUp = true;
	for (int32 Triangle = 0; Triangle < NodeMesh.NumTriangles(); ++Triangle)
	{
		if (NodeMesh.MaterialIds[Triangle] == ChronoRoadMaterialSlot::Carriageway)
		{
			bAllTopFacesUp &= NodeMesh.GetTriangleWindingNormal(Triangle).Z > 0.0;
		}
	}
	TestTrue(TEXT("Junction surface faces up"), bAllTopFacesUp);

	// Segment mesh: the carriageway covers width x usable length (straight
	// road), which proves it stops exactly where the junction starts.
	FChronoMeshData BranchMesh;
	ChronoRoadMeshBuilder::BuildSegmentMesh(Graph, Branch, Settings, FVector::ZeroVector, BranchMesh);
	TestFalse(TEXT("Segment mesh is not empty"), BranchMesh.IsEmpty());
	if (BranchSegment)
	{
		const double ExpectedArea = Profile.CarriagewayWidthCm * BranchSegment->GetUsableLength();
		const double ActualArea = ChronoRoadTests::ComputeGroundArea(BranchMesh, ChronoRoadMaterialSlot::Carriageway);
		TestTrue(TEXT("Carriageway covers width times usable length"), FMath::IsNearlyEqual(ActualArea, ExpectedArea, ExpectedArea * 1.0e-4));
	}

	// Zero clipping contract at the junction: where a segment meets the
	// junction, both are at the height of the node.
	if (BranchSegment && CenterNode)
	{
		const double HeightAtMouth = Graph.GetSegmentHeightAtDistance(*BranchSegment, BranchSegment->TrimStart);
		TestTrue(TEXT("Segment and junction meet at the same height"), FMath::IsNearlyEqual(HeightAtMouth, Graph.FindNode(Center)->Position.Z, 0.01));
	}

	// Save and load: the same network comes back, with the same derived data.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Graph is saved"), Graph.Serialize(Writer));

	FChronoRoadGraph Loaded;
	Loaded.SetMinTrimCm(100.0f);
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Graph is loaded"), Loaded.Serialize(Reader));
	TestEqual(TEXT("Loaded graph has the same segments"), Loaded.GetSegments().Num(), Graph.GetSegments().Num());
	TestEqual(TEXT("Loaded graph has the same nodes"), Loaded.GetNodes().Num(), Graph.GetNodes().Num());
	const FChronoRoadSegment* LoadedBranch = Loaded.FindSegment(Branch);
	TestTrue(TEXT("Loaded segment keeps its trim"), LoadedBranch && BranchSegment && FMath::IsNearlyEqual(LoadedBranch->TrimStart, BranchSegment->TrimStart, 0.01f));
	TestTrue(TEXT("Loaded segment keeps its profile"), LoadedBranch && LoadedBranch->Profile.ProfileId == Profile.ProfileId);

	// Removing the branch turns the junction back into a simple joint.
	TestTrue(TEXT("Branch is removed"), Graph.RemoveSegment(Branch));
	TestTrue(TEXT("Orphan node is removed with its last road"), Graph.FindNode(North) == nullptr);
	TestEqual(TEXT("Joint has two roads"), Graph.FindNode(Center) ? Graph.FindNode(Center)->SegmentIds.Num() : 0, 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
