using UnrealBuildTool;

public class PBRStudio : ModuleRules
{
	public PBRStudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"ImageWrapper",
			"ImageWriteQueue",
			"MaterialEditor",
			"PropertyEditor",
			"EditorScriptingUtilities",
			"WorkspaceMenuStructure",
			"InputCore",
			"Projects",
			"DesktopPlatform",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"EditorStyle",
			"ToolMenus",
		});
	}
}
