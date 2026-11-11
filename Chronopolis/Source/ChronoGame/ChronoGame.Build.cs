using UnrealBuildTool;

public class ChronoGame : ModuleRules
{
	public ChronoGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ChronoCore",
			"ChronoTerrain",
			"ChronoRoads"
		});
	}
}
