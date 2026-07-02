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
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"HTTP",
			"HTTPServer",
			"InternationalizationSettings",
			"Json",
			"JsonUtilities",
			"ImageWrapper",
			"ImageCore",
			"ImageWriteQueue",
			"MaterialEditor",
			"CinematicCamera",
			"DatasmithContent",
			"PropertyEditor",
			"EditorScriptingUtilities",
			"WorkspaceMenuStructure",
			"InputCore",
			"Projects",
			"DesktopPlatform",
			"Networking",
			"Sockets",
			"LevelEditor",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AppFramework",
			"ContentBrowser",
			"DeveloperSettings",
			"EditorFramework",
			"MaterialBaking",
			"MaterialUtilities",
			"Settings",
			"ToolMenus",
		});
	}
}
