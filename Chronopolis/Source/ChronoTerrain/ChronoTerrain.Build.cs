using UnrealBuildTool;

public class ChronoTerrain : ModuleRules
{
	public ChronoTerrain(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ChronoCore"
		});

		// GeometryCore: FDynamicMesh3. GeometryFramework: UDynamicMeshComponent.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GeometryCore",
			"GeometryFramework"
		});
	}
}
