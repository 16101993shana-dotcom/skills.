using UnrealBuildTool;

public class ChronoCitizens : ModuleRules
{
	public ChronoCitizens(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"ChronoCore",
			"ChronoZoning",
			"ChronoBuildings"
		});
	}
}
