using UnrealBuildTool;

public class ChronopolisTarget : TargetRules
{
	public ChronopolisTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ChronoCore", "ChronoTerrain", "ChronoRoads", "ChronoGame" });
	}
}
