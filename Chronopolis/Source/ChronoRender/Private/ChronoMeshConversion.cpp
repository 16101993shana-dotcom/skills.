#include "ChronoMeshConversion.h"

#include "ChronoMeshData.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "IndexTypes.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, ChronoRender);

void ChronoMeshConversion::ApplyToComponent(const FChronoMeshData& MeshData, bool bFlipWinding, UDynamicMeshComponent& Component)
{
	UE::Geometry::FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();
	Mesh.Attributes()->EnableMaterialID();

	UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
	UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
	UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIds = Mesh.Attributes()->GetMaterialID();

	// Vertices, UVs and normals are appended in the same order, so the three
	// ids of a vertex are identical.
	for (int32 Index = 0; Index < MeshData.Positions.Num(); ++Index)
	{
		Mesh.AppendVertex(FVector3d(MeshData.Positions[Index]));
		UVOverlay->AppendElement(MeshData.UVs[Index]);
		NormalOverlay->AppendElement(MeshData.Normals[Index]);
	}

	for (int32 Triangle = 0; Triangle < MeshData.NumTriangles(); ++Triangle)
	{
		const int32 A = MeshData.Triangles[Triangle * 3];
		const int32 B = MeshData.Triangles[Triangle * 3 + 1];
		const int32 C = MeshData.Triangles[Triangle * 3 + 2];
		const UE::Geometry::FIndex3i Indices = bFlipWinding ? UE::Geometry::FIndex3i(A, C, B) : UE::Geometry::FIndex3i(A, B, C);

		const int32 TriangleId = Mesh.AppendTriangle(Indices.A, Indices.B, Indices.C);
		if (TriangleId >= 0)
		{
			UVOverlay->SetTriangle(TriangleId, Indices);
			NormalOverlay->SetTriangle(TriangleId, Indices);
			MaterialIds->SetValue(TriangleId, MeshData.MaterialIds[Triangle]);
		}
	}

	Component.SetMesh(MoveTemp(Mesh));
}
