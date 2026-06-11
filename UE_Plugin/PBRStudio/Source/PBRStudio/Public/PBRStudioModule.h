#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class FUICommandList;
class IInputProcessor;
class SDockTab;

class FPBRStudioModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void ToggleMainWindow();
	static void ToggleMagicOutlinerWindow();
	static void TogglePluginLanguage();
	static void RefreshOpenPluginWindows();
	static void RebuildTemplateMaterialsFromConsole();
	static void RebuildSubstrateTemplateMaterialsFromConsole();
	static void ScanMaterialFunctionUsageFromConsole();
	static void ScanSubstrateFunctionUsageFromConsole();

private:
	void RegisterMenus();
	void RegisterTabSpawner();
	void UnregisterTabSpawner();
	void RegisterConsoleCommands();
	void UnregisterConsoleCommands();
	void RegisterInputProcessor();
	void UnregisterInputProcessor();

	static TSharedRef<SDockTab> SpawnMainWindowTab(const FSpawnTabArgs& Args);
	static TSharedRef<SDockTab> SpawnMagicOutlinerTab(const FSpawnTabArgs& Args);

	TSharedPtr<FUICommandList> PluginCommands;
	TSharedPtr<class FPBRHttpServer> HttpServer;
	TSharedPtr<class FPBRDownloadManager> DownloadManager;
	TSharedPtr<IInputProcessor> InputProcessor;
	TArray<IConsoleObject*> ConsoleCommands;
	static TWeakPtr<SDockTab> MainTab;
	static TWeakPtr<SDockTab> MagicOutlinerTab;
};
