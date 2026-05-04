#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class FUICommandList;
class SDockTab;

class FPBRStudioModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void ToggleMainWindow();

private:
	void RegisterMenus();
	void RegisterTabSpawner();
	void UnregisterTabSpawner();

	static TSharedRef<SDockTab> SpawnMainWindowTab(const FSpawnTabArgs& Args);

	TSharedPtr<FUICommandList> PluginCommands;
	TSharedPtr<class FPBRHttpServer> HttpServer;
	TSharedPtr<class FPBRDownloadManager> DownloadManager;
	static TWeakPtr<SDockTab> MainTab;
};
