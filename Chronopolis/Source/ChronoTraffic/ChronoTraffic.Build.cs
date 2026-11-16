using UnrealBuildTool;

public class ChronoTraffic : ModuleRules
{
	public ChronoTraffic(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"ChronoCore",
			"ChronoRoads",
			"ChronoZoning",
			"ChronoBuildings",
			"ChronoCitizens"
		});
	}
}
