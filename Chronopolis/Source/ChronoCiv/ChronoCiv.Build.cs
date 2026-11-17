using UnrealBuildTool;

public class ChronoCiv : ModuleRules
{
	public ChronoCiv(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"ChronoCore",
			"ChronoBuildings",
			"ChronoCitizens",
			"ChronoEconomy"
		});

		// Only the automation tests use these: they build small towns to check renewal.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ChronoTerrain",
			"ChronoRoads",
			"ChronoZoning",
			"ChronoAudit"
		});
	}
}
