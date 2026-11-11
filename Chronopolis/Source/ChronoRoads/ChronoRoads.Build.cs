using UnrealBuildTool;

public class ChronoRoads : ModuleRules
{
	public ChronoRoads(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ChronoCore",
			"ChronoTerrain"
		});

		// GeometryCore: FDynamicMesh3. GeometryFramework: UDynamicMeshComponent.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GeometryCore",
			"GeometryFramework"
		});
	}
}
