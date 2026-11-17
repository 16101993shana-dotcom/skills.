// Greybox volume of a building: foundation, walls and roof, generated from
// its footprint and heights. Used until a finished model is assigned to the
// building definition.

#pragma once

#include "CoreMinimal.h"
#include "ChronoBuildingTypes.h"
#include "ChronoMeshData.h"

/** Material slots used by greybox buildings. */
namespace ChronoBuildingMaterialSlot
{
	const int32 Wall = 0;
	const int32 Roof = 1;
	const int32 Foundation = 2;
	const int32 Count = 3;
}

namespace ChronoBuildingMeshBuilder
{
	/**
	 * Appends only the foundation of a building: used under a finished model,
	 * which stops at floor level, so it still meets the ground on a slope.
	 */
	CHRONOBUILDINGS_API void AppendFoundation(const FChronoBuilding& Building, const FVector& Origin, FChronoMeshData& OutMesh);

	/** Appends the volume of one building to OutMesh. Positions are relative to Origin. */
	CHRONOBUILDINGS_API void AppendBuilding(const FChronoBuilding& Building, const FVector& Origin, FChronoMeshData& OutMesh);
}
