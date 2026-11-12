// Bridge between the plain triangle soup of the procedural builders and the
// engine dynamic mesh component. The only place that knows both formats.

#pragma once

#include "CoreMinimal.h"

class UDynamicMeshComponent;
struct FChronoMeshData;

namespace ChronoMeshConversion
{
	/**
	 * Replaces the mesh of a component by the content of MeshData. Material
	 * slot indices of MeshData become material ids of the component.
	 * @param bFlipWinding project-wide safety valve, see UChronoSettings::bFlipTerrainWinding.
	 */
	CHRONORENDER_API void ApplyToComponent(const FChronoMeshData& MeshData, bool bFlipWinding, UDynamicMeshComponent& Component);
}
