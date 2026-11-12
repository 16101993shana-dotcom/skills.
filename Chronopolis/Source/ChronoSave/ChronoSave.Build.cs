using UnrealBuildTool;

public class ChronoSave : ModuleRules
{
	public ChronoSave(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ChronoCore",
			"ChronoTerrain",
			"ChronoRoads",
			"ChronoZoning",
			"ChronoBuildings"
		});
	}
}
