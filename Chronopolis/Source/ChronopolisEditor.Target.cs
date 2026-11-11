using UnrealBuildTool;

public class ChronopolisEditorTarget : TargetRules
{
	public ChronopolisEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ChronoCore", "ChronoTerrain", "ChronoRoads", "ChronoAudit", "ChronoSave", "ChronoGame" });
	}
}
