using UnrealBuildTool;

public class ChronoBuildings : ModuleRules
{
	public ChronoBuildings(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"ChronoCore",
			"ChronoTerrain",
			"ChronoRoads",
			"ChronoZoning"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GeometryFramework",
			"ChronoRender"
		});
	}
}
