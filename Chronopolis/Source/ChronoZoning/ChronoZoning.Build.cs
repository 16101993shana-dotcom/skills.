using UnrealBuildTool;

public class ChronoZoning : ModuleRules
{
	public ChronoZoning(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ChronoCore",
			"ChronoTerrain",
			"ChronoRoads"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GeometryFramework",
			"ChronoRender"
		});
	}
}
