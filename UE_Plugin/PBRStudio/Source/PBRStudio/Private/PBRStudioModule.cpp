#include "PBRStudioModule.h"
#include "PBRStudioCommands.h"
#include "PBRStudioStyle.h"
#include "Services/PBRHttpServer.h"
#include "Widgets/SPBRStudioMainWindow.h"
#include "Framework/Docking/TabManager.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"

DEFINE_LOG_CATEGORY_STATIC(LogPBRStudio, Log, All);

#define LOCTEXT_NAMESPACE "FPBRStudioModule"

static const FName MainTabName("PBRStudioMainTab");
TWeakPtr<SDockTab> FPBRStudioModule::MainTab;

void FPBRStudioModule::StartupModule()
{
	FPBRStudioStyle::Initialize();
	FPBRStudioStyle::ReloadTextures();
	FPBRStudioCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FPBRStudioCommands::Get().OpenMainWindow,
		FExecuteAction::CreateStatic(&FPBRStudioModule::ToggleMainWindow),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPBRStudioModule::RegisterMenus));

	RegisterTabSpawner();
}

void FPBRStudioModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	UnregisterTabSpawner();

	if (HttpServer.IsValid())
	{
		HttpServer->Stop();
		HttpServer.Reset();
	}
	DownloadManager.Reset();

	FPBRStudioCommands::Unregister();
	FPBRStudioStyle::Shutdown();
}

void FPBRStudioModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
	Section.AddMenuEntryWithCommandList(
		FPBRStudioCommands::Get().OpenMainWindow,
		PluginCommands,
		LOCTEXT("PBRStudioMenuLabel", "PBR 工作室"),
		LOCTEXT("PBRStudioMenuTooltip", "打开 PBR 工作室工具窗口"),
		FSlateIcon()
	);
}

void FPBRStudioModule::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		MainTabName,
		FOnSpawnTab::CreateStatic(&FPBRStudioModule::SpawnMainWindowTab))
		.SetDisplayName(LOCTEXT("PBRStudioTabTitle", "PBR 工作室"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FPBRStudioStyle::GetStyleSetName(), "PBRStudio.OpenMainWindow"));
}

void FPBRStudioModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MainTabName);
}

TSharedRef<SDockTab> FPBRStudioModule::SpawnMainWindowTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> NewTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("PBRStudioTabLabel", "PBR 工作室"))
		[
			SNew(SPBRStudioMainWindow)
		];

	MainTab = NewTab;
	return NewTab;
}

void FPBRStudioModule::ToggleMainWindow()
{
	TSharedPtr<SDockTab> Existing = MainTab.Pin();
	if (Existing.IsValid() && Existing->GetParentWindow().IsValid())
	{
		if (Existing->IsForeground())
		{
			Existing->RequestCloseTab();
		}
		else
		{
			Existing->GetParentWindow()->BringToFront();
			Existing->ActivateInParent(ETabActivationCause::SetDirectly);
		}
	}
	else
	{
		FGlobalTabmanager::Get()->TryInvokeTab(MainTabName);
	}
}

IMPLEMENT_MODULE(FPBRStudioModule, PBRStudio)

#undef LOCTEXT_NAMESPACE
