using UnrealBuildTool;

public class ChronopolisEditorTarget : TargetRules
{
	public ChronopolisEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ChronoCore", "ChronoRender", "ChronoTerrain", "ChronoRoads", "ChronoZoning", "ChronoBuildings", "ChronoAudit", "ChronoSave", "ChronoGame" });
	}
}
