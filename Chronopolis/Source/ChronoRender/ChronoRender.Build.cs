using UnrealBuildTool;

public class ChronoRender : ModuleRules
{
	public ChronoRender(ReadOnlyTargetRules Target) : base(Target)
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
