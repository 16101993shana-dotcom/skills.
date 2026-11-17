using UnrealBuildTool;

public class ChronoEconomy : ModuleRules
{
	public ChronoEconomy(ReadOnlyTargetRules Target) : base(Target)
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
