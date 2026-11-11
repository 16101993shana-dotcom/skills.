using UnrealBuildTool;

public class ChronoAudit : ModuleRules
{
	public ChronoAudit(ReadOnlyTargetRules Target) : base(Target)
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
	}
}
