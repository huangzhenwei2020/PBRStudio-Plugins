#include "PBRStudioModule.h"
#include "PBRStudioCommands.h"
#include "PBRStudioStyle.h"
#include "Services/PBRDataStore.h"
#include "Services/PBRHttpServer.h"
#include "Services/PBRLocalization.h"
#include "Services/PBRMaterialFunctionLibrary.h"
#include "Services/PBRSubstrateMaterialTemplateManager.h"
#include "Services/PBRMaterialTemplateManager.h"
#include "Widgets/SPBRMagicOutlinerWindow.h"
#include "Widgets/SPBRStudioMainWindow.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialFunctionInterface.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogPBRStudio, Log, All);

#define LOCTEXT_NAMESPACE "FPBRStudioModule"

static const FName MainTabName("PBRStudioMainTab");
static const FName MagicOutlinerTabName("PBRStudioMagicOutlinerTab");
TWeakPtr<SDockTab> FPBRStudioModule::MainTab;
TWeakPtr<SDockTab> FPBRStudioModule::MagicOutlinerTab;

static FText PBRText(const TCHAR* Key, const TCHAR* Chinese, const TCHAR* English)
{
	return FPBRLocalization::Text(Key, Chinese, English);
}

namespace
{
const TCHAR* MagicOutlinerIsolationShortcutConfigKey = TEXT("magic_outliner_isolation_shortcut");

static FString PBRActorLabel(AActor* Actor)
{
	return Actor ? Actor->GetActorLabel() : FString();
}

static bool PBRIsIsolationProtectedActor(AActor* Actor)
{
	if (!Actor || !Actor->GetClass())
	{
		return false;
	}
	const FString Label = PBRActorLabel(Actor);
	const FString ObjectName = Actor->GetName();
	const FString ClassName = Actor->GetClass()->GetName();
	return Label.Contains(TEXT("SunSky"), ESearchCase::IgnoreCase)
		|| ObjectName.Contains(TEXT("SunSky"), ESearchCase::IgnoreCase)
		|| ClassName.Contains(TEXT("SunSky"), ESearchCase::IgnoreCase);
}

static void PBRGatherSceneActors(TArray<AActor*>& OutActors)
{
	if (!GEditor)
	{
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}
	for (ULevel* Level : World->GetLevels())
	{
		if (!Level)
		{
			continue;
		}
		for (AActor* Actor : Level->Actors)
		{
			if (Actor && !Actor->IsPendingKillPending() && !Actor->IsTemplate())
			{
				OutActors.Add(Actor);
			}
		}
	}
}

static void PBRGetSelectedActors(TArray<AActor*>& OutActors)
{
	if (!GEditor)
	{
		return;
	}
	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection)
	{
		return;
	}
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			OutActors.Add(Actor);
		}
	}
}

static FString PBRShortcutToString(const FInputChord& Chord)
{
	if (!Chord.IsValidChord())
	{
		return FString();
	}
	TArray<FString> Parts;
	if (Chord.bCtrl) { Parts.Add(TEXT("Ctrl")); }
	if (Chord.bAlt) { Parts.Add(TEXT("Alt")); }
	if (Chord.bShift) { Parts.Add(TEXT("Shift")); }
	if (Chord.bCmd) { Parts.Add(TEXT("Cmd")); }
	Parts.Add(Chord.Key.GetFName().ToString());
	return FString::Join(Parts, TEXT("+"));
}

static bool PBRTryParseShortcut(const FString& Text, FInputChord& OutChord)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("+"), true);
	if (Parts.IsEmpty())
	{
		return false;
	}

	bool bCtrl = false;
	bool bAlt = false;
	bool bShift = false;
	bool bCmd = false;
	FKey Key;
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.Equals(TEXT("Ctrl"), ESearchCase::IgnoreCase) || Part.Equals(TEXT("Control"), ESearchCase::IgnoreCase))
		{
			bCtrl = true;
		}
		else if (Part.Equals(TEXT("Alt"), ESearchCase::IgnoreCase))
		{
			bAlt = true;
		}
		else if (Part.Equals(TEXT("Shift"), ESearchCase::IgnoreCase))
		{
			bShift = true;
		}
		else if (Part.Equals(TEXT("Cmd"), ESearchCase::IgnoreCase) || Part.Equals(TEXT("Command"), ESearchCase::IgnoreCase))
		{
			bCmd = true;
		}
		else
		{
			Key = FKey(*Part);
		}
	}

	OutChord = FInputChord(Key, bShift, bCtrl, bAlt, bCmd);
	return OutChord.IsValidChord();
}

class FPBRMagicOutlinerInputProcessor : public IInputProcessor
{
public:
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		FInputChord Shortcut(EKeys::Q, EModifierKey::Alt);
		TSharedPtr<FJsonObject> Config;
		if (FPBRDataStore::LoadConfig(Config) && Config.IsValid())
		{
			FString ShortcutText;
			if (Config->TryGetStringField(MagicOutlinerIsolationShortcutConfigKey, ShortcutText))
			{
				FInputChord ParsedShortcut;
				if (PBRTryParseShortcut(ShortcutText, ParsedShortcut))
				{
					Shortcut = ParsedShortcut;
				}
			}
		}

		const bool bMatches = Shortcut.IsValidChord()
			&& InKeyEvent.GetKey() == Shortcut.Key
			&& InKeyEvent.IsAltDown() == Shortcut.bAlt
			&& InKeyEvent.IsControlDown() == Shortcut.bCtrl
			&& InKeyEvent.IsShiftDown() == Shortcut.bShift
			&& InKeyEvent.IsCommandDown() == Shortcut.bCmd;
		if (!bMatches)
		{
			return false;
		}

		ToggleIsolationForEditorSelection();
		return true;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("PBRMagicOutlinerInputProcessor");
	}

private:
	void ToggleIsolationForEditorSelection()
	{
		if (bIsolationActive)
		{
			ExitIsolation();
		}
		else
		{
			IsolateSelection();
		}
	}

	void IsolateSelection()
	{
		TArray<AActor*> SelectedActors;
		PBRGetSelectedActors(SelectedActors);
		if (SelectedActors.IsEmpty())
		{
			return;
		}

		TSet<TWeakObjectPtr<AActor>> SelectedSet;
		for (AActor* Actor : SelectedActors)
		{
			if (Actor)
			{
				SelectedSet.Add(Actor);
			}
		}

		TArray<AActor*> SceneActors;
		PBRGatherSceneActors(SceneActors);
		HiddenStates.Reset();
		for (AActor* Actor : SceneActors)
		{
			if (!Actor)
			{
				continue;
			}
			HiddenStates.Add(Actor, Actor->IsTemporarilyHiddenInEditor());
			if (PBRIsIsolationProtectedActor(Actor))
			{
				Actor->SetIsTemporarilyHiddenInEditor(false);
				continue;
			}
			Actor->SetIsTemporarilyHiddenInEditor(!SelectedSet.Contains(Actor));
		}
		bIsolationActive = true;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}

	void ExitIsolation()
	{
		for (const TPair<TWeakObjectPtr<AActor>, bool>& Pair : HiddenStates)
		{
			if (AActor* Actor = Pair.Key.Get())
			{
				Actor->SetIsTemporarilyHiddenInEditor(Pair.Value);
			}
		}
		HiddenStates.Reset();
		bIsolationActive = false;
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}

	TMap<TWeakObjectPtr<AActor>, bool> HiddenStates;
	bool bIsolationActive = false;
};
}

static TSharedRef<SWidget> MakePBRStudioToolbarWidget()
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(8, 4))
		.ToolTipText_Lambda([]() { return PBRText(TEXT("PBRStudioToolbarTooltip"), TEXT("打开或关闭 AR Studio 建筑可视化工作台"), TEXT("Open or close AR Studio visualization tools")); })
		.OnClicked_Lambda([]()
		{
			FPBRStudioModule::ToggleMainWindow();
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(SImage)
				.Image(FPBRStudioStyle::Get().GetBrush("PBRStudio.OpenMainWindow.Small"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([]() { return PBRText(TEXT("PBRStudioToolbarWidgetLabel"), TEXT("AR Studio"), TEXT("AR Studio")); })
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
		];
}

static TSharedRef<SWidget> MakePBRMagicOutlinerToolbarWidget()
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(8, 4))
		.ToolTipText_Lambda([]() { return PBRText(TEXT("PBRMagicOutlinerToolbarTooltip"), TEXT("打开或关闭魔法大纲窗口"), TEXT("Open or close Magic Outliner")); })
		.OnClicked_Lambda([]()
		{
			FPBRStudioModule::ToggleMagicOutlinerWindow();
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(SImage)
				.Image(FPBRStudioStyle::Get().GetBrush("PBRStudio.MagicOutliner.Small"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([]() { return PBRText(TEXT("PBRMagicOutlinerToolbarWidgetLabel"), TEXT("魔法大纲"), TEXT("Magic Outliner")); })
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
		];
}

static TSharedRef<SWidget> MakePBRLanguageToolbarWidget()
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(8, 4))
		.ToolTipText_Lambda([]() { return FPBRLocalization::GetLanguageButtonTooltip(); })
		.OnClicked_Lambda([]()
		{
			FPBRStudioModule::TogglePluginLanguage();
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("文")))
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([]() { return FPBRLocalization::GetLanguageButtonText(); })
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
		];
}

void FPBRStudioModule::StartupModule()
{
	FPBRLocalization::ApplySupportedEditorLanguage();

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
	RegisterConsoleCommands();
	RegisterInputProcessor();
}

void FPBRStudioModule::ShutdownModule()
{
	UnregisterInputProcessor();
	UnregisterConsoleCommands();
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

void FPBRStudioModule::RegisterConsoleCommands()
{
	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PBRStudio.OpenMagicOutliner"),
		TEXT("Open the PBR Studio Magic Outliner window."),
		FConsoleCommandDelegate::CreateStatic(&FPBRStudioModule::ToggleMagicOutlinerWindow),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PBRStudio.RebuildParentTemplates"),
		TEXT("Rebuild PBR Studio material functions and parent templates only."),
		FConsoleCommandDelegate::CreateStatic(&FPBRStudioModule::RebuildParentTemplateMaterialsFromConsole),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PBRStudio.RebuildTemplates"),
		TEXT("Rebuild all PBR Studio template and special materials."),
		FConsoleCommandDelegate::CreateStatic(&FPBRStudioModule::RebuildTemplateMaterialsFromConsole),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PBRStudio.ScanFunctionUsage"),
		TEXT("Scan PBR Studio materials for material function call nodes."),
		FConsoleCommandDelegate::CreateStatic(&FPBRStudioModule::ScanMaterialFunctionUsageFromConsole),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PBRStudio.RebuildSubstrateTemplates"),
		TEXT("Rebuild PBR Studio Substrate-only templates under /Game/PBRStudio/Substrate."),
		FConsoleCommandDelegate::CreateStatic(&FPBRStudioModule::RebuildSubstrateTemplateMaterialsFromConsole),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PBRStudio.ScanSubstrateFunctionUsage"),
		TEXT("Scan PBR Studio Substrate-only materials for material function call nodes."),
		FConsoleCommandDelegate::CreateStatic(&FPBRStudioModule::ScanSubstrateFunctionUsageFromConsole),
		ECVF_Default));
}

void FPBRStudioModule::UnregisterConsoleCommands()
{
	for (IConsoleObject* Command : ConsoleCommands)
	{
		if (Command)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command);
		}
	}
	ConsoleCommands.Empty();
}

void FPBRStudioModule::RegisterInputProcessor()
{
	if (!InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		InputProcessor = MakeShared<FPBRMagicOutlinerInputProcessor>();
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}
}

void FPBRStudioModule::UnregisterInputProcessor()
{
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
		InputProcessor.Reset();
	}
}

void FPBRStudioModule::RebuildParentTemplateMaterialsFromConsole()
{
	TArray<FString> TemplateMessages;
	TArray<FString> FunctionMessages;
	const int32 FunctionCount = FPBRMaterialFunctionLibrary::EnsureAllMaterialFunctions(FunctionMessages);
	const int32 TemplateCount = FPBRMaterialTemplateManager::EnsureAllTemplateMaterials(TemplateMessages);
	UE_LOG(LogPBRStudio, Display, TEXT("PBRStudio parent templates rebuilt. Functions: %d, Templates: %d"), FunctionCount, TemplateCount);
	for (const FString& Message : FunctionMessages)
	{
		UE_LOG(LogPBRStudio, Display, TEXT("%s"), *Message);
	}
	for (const FString& Message : TemplateMessages)
	{
		UE_LOG(LogPBRStudio, Display, TEXT("%s"), *Message);
	}
}

void FPBRStudioModule::RebuildTemplateMaterialsFromConsole()
{
	TArray<FString> TemplateMessages;
	TArray<FString> SpecialMessages;
	TArray<FString> FunctionMessages;
	const int32 FunctionCount = FPBRMaterialFunctionLibrary::EnsureAllMaterialFunctions(FunctionMessages);
	const int32 TemplateCount = FPBRMaterialTemplateManager::EnsureAllTemplateMaterials(TemplateMessages);
	const int32 SpecialCount = FPBRMaterialTemplateManager::EnsureSpecialTemplateMaterials(SpecialMessages);
	UE_LOG(LogPBRStudio, Display, TEXT("PBRStudio templates rebuilt. Functions: %d, Templates: %d, Special: %d"), FunctionCount, TemplateCount, SpecialCount);
	for (const FString& Message : FunctionMessages)
	{
		UE_LOG(LogPBRStudio, Display, TEXT("%s"), *Message);
	}
	for (const FString& Message : TemplateMessages)
	{
		UE_LOG(LogPBRStudio, Display, TEXT("%s"), *Message);
	}
	for (const FString& Message : SpecialMessages)
	{
		UE_LOG(LogPBRStudio, Display, TEXT("%s"), *Message);
	}
}

void FPBRStudioModule::RebuildSubstrateTemplateMaterialsFromConsole()
{
	TArray<FString> Messages;
	const int32 TemplateCount = FPBRSubstrateMaterialTemplateManager::EnsureAllTemplateMaterials(Messages);
	UE_LOG(LogPBRStudio, Display, TEXT("PBRStudio Substrate templates rebuilt. Templates: %d"), TemplateCount);
	for (const FString& Message : Messages)
	{
		UE_LOG(LogPBRStudio, Display, TEXT("%s"), *Message);
	}
}

void FPBRStudioModule::ScanMaterialFunctionUsageFromConsole()
{
	const TArray<FString> Roots = {
		TEXT("/Game/PBRStudio/Templates"),
		TEXT("/Game/PBRStudio/SpecialMaterials")
	};

	int32 TotalMaterials = 0;
	int32 TotalFunctionCalls = 0;
	for (const FString& Root : Roots)
	{
		const TArray<FString> Assets = UEditorAssetLibrary::ListAssets(Root, true, false);
		for (const FString& AssetPath : Assets)
		{
			UMaterial* Material = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(AssetPath));
			if (!Material)
			{
				continue;
			}

			++TotalMaterials;
			int32 CallsInMaterial = 0;
			TArray<FString> FunctionNames;
			for (UMaterialExpression* Expression : Material->GetExpressions())
			{
				UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
				if (!FunctionCall || !FunctionCall->MaterialFunction)
				{
					continue;
				}

				++CallsInMaterial;
				++TotalFunctionCalls;
				FunctionNames.Add(FunctionCall->MaterialFunction->GetPathName());
			}

			UE_LOG(LogPBRStudio, Display, TEXT("Function scan: %s -> %d call(s): %s"),
				*Material->GetPathName(),
				CallsInMaterial,
				*FString::Join(FunctionNames, TEXT(", ")));
		}
	}

	UE_LOG(LogPBRStudio, Display, TEXT("Function scan summary: %d material(s), %d material function call node(s)."), TotalMaterials, TotalFunctionCalls);
}

void FPBRStudioModule::ScanSubstrateFunctionUsageFromConsole()
{
	const TArray<FString> Assets = UEditorAssetLibrary::ListAssets(TEXT("/Game/PBRStudio/Substrate"), true, false);

	int32 TotalMaterials = 0;
	int32 TotalFunctionCalls = 0;
	int32 TotalStaticSwitches = 0;
	int32 TotalLerps = 0;
	for (const FString& AssetPath : Assets)
	{
		UMaterial* Material = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (!Material)
		{
			continue;
		}

		++TotalMaterials;
		int32 CallsInMaterial = 0;
		int32 StaticSwitchesInMaterial = 0;
		int32 LerpsInMaterial = 0;
		TArray<FString> FunctionNames;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (Cast<UMaterialExpressionStaticSwitchParameter>(Expression))
			{
				++StaticSwitchesInMaterial;
				++TotalStaticSwitches;
			}
			if (Cast<UMaterialExpressionLinearInterpolate>(Expression))
			{
				++LerpsInMaterial;
				++TotalLerps;
			}

			UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
			if (!FunctionCall || !FunctionCall->MaterialFunction)
			{
				continue;
			}

			++CallsInMaterial;
			++TotalFunctionCalls;
			FunctionNames.Add(FunctionCall->MaterialFunction->GetPathName());
		}

		UE_LOG(LogPBRStudio, Display, TEXT("Substrate function scan: %s -> %d call(s), %d static switch(es), %d lerp(s): %s"),
			*Material->GetPathName(),
			CallsInMaterial,
			StaticSwitchesInMaterial,
			LerpsInMaterial,
			*FString::Join(FunctionNames, TEXT(", ")));
	}

	UE_LOG(LogPBRStudio, Display, TEXT("Substrate function scan summary: %d material(s), %d material function call node(s), %d static switch node(s), %d lerp node(s)."), TotalMaterials, TotalFunctionCalls, TotalStaticSwitches, TotalLerps);
}

void FPBRStudioModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
	Section.AddMenuEntryWithCommandList(
		FPBRStudioCommands::Get().OpenMainWindow,
		PluginCommands,
		PBRText(TEXT("PBRStudioMenuLabel"), TEXT("建筑可视化工作台"), TEXT("Architectural Visualization Tools")),
		PBRText(TEXT("PBRStudioMenuTooltip"), TEXT("打开 AR Studio 工具窗口"), TEXT("Open AR Studio tools")),
		FSlateIcon(FPBRStudioStyle::GetStyleSetName(), "PBRStudio.OpenMainWindow.Small")
	);
	Section.AddMenuEntry(
		"PBRStudioMagicOutlinerMenu",
		PBRText(TEXT("PBRMagicOutlinerMenuLabel"), TEXT("魔法大纲"), TEXT("Magic Outliner")),
		PBRText(TEXT("PBRMagicOutlinerMenuTooltip"), TEXT("打开 AR Studio 魔法大纲窗口"), TEXT("Open AR Studio Magic Outliner")),
		FSlateIcon(FPBRStudioStyle::GetStyleSetName(), "PBRStudio.MagicOutliner.Small"),
		FUIAction(FExecuteAction::CreateStatic(&FPBRStudioModule::ToggleMagicOutlinerWindow)));

	auto AddToolbarWidget = [](const char* MenuName, const FName SectionName)
	{
		if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(MenuName))
		{
			FToolMenuSection& ToolbarSection = Toolbar->FindOrAddSection(SectionName);
			FToolMenuEntry ToolbarEntry = FToolMenuEntry::InitWidget(
				"PBRStudioOpenToolbarWidget",
				MakePBRStudioToolbarWidget(),
				PBRText(TEXT("PBRStudioToolbarLabel"), TEXT("AR Studio"), TEXT("AR Studio")),
				true);
			ToolbarEntry.StyleNameOverride = "AssetEditorToolbar";
			ToolbarSection.AddEntry(ToolbarEntry);

			FToolMenuEntry MagicOutlinerEntry = FToolMenuEntry::InitWidget(
				"PBRStudioMagicOutlinerToolbarWidget",
				MakePBRMagicOutlinerToolbarWidget(),
				PBRText(TEXT("PBRMagicOutlinerToolbarLabel"), TEXT("魔法大纲"), TEXT("Magic Outliner")),
				true);
			MagicOutlinerEntry.StyleNameOverride = "AssetEditorToolbar";
			ToolbarSection.AddEntry(MagicOutlinerEntry);

			FToolMenuEntry LanguageEntry = FToolMenuEntry::InitWidget(
				"PBRStudioLanguageToolbarWidget",
				MakePBRLanguageToolbarWidget(),
				PBRText(TEXT("PBRLanguageToolbarLabel"), TEXT("语言"), TEXT("Language")),
				true);
			LanguageEntry.StyleNameOverride = "AssetEditorToolbar";
			ToolbarSection.AddEntry(LanguageEntry);
		}
	};

	AddToolbarWidget("LevelEditor.LevelEditorToolBar.User", "PBRStudio");
	AddToolbarWidget("AssetEditor.MaterialEditor.ToolBar", "PBRStudio");
	AddToolbarWidget("AssetEditor.BlueprintEditor.ToolBar", "PBRStudio");
}

void FPBRStudioModule::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		MainTabName,
		FOnSpawnTab::CreateStatic(&FPBRStudioModule::SpawnMainWindowTab))
		.SetDisplayName(PBRText(TEXT("PBRStudioTabTitle"), TEXT("建筑可视化工作台"), TEXT("Architectural Visualization Tools")))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FPBRStudioStyle::GetStyleSetName(), "PBRStudio.OpenMainWindow"));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		MagicOutlinerTabName,
		FOnSpawnTab::CreateStatic(&FPBRStudioModule::SpawnMagicOutlinerTab))
		.SetDisplayName(PBRText(TEXT("PBRMagicOutlinerTabTitle"), TEXT("魔法大纲"), TEXT("Magic Outliner")))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FPBRStudioStyle::GetStyleSetName(), "PBRStudio.MagicOutliner"));
}

void FPBRStudioModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MainTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MagicOutlinerTabName);
}

TSharedRef<SDockTab> FPBRStudioModule::SpawnMainWindowTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> NewTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(PBRText(TEXT("PBRStudioTabLabel"), TEXT("AR Studio"), TEXT("AR Studio")))
		[
			SNew(SPBRStudioMainWindow)
		];

	MainTab = NewTab;
	return NewTab;
}

TSharedRef<SDockTab> FPBRStudioModule::SpawnMagicOutlinerTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> NewTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(PBRText(TEXT("PBRMagicOutlinerTabLabel"), TEXT("魔法大纲"), TEXT("Magic Outliner")))
		[
			SNew(SPBRMagicOutlinerWindow)
		];

	MagicOutlinerTab = NewTab;
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

void FPBRStudioModule::ToggleMagicOutlinerWindow()
{
	TSharedPtr<SDockTab> Existing = MagicOutlinerTab.Pin();
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
		FGlobalTabmanager::Get()->TryInvokeTab(MagicOutlinerTabName);
	}
}

void FPBRStudioModule::TogglePluginLanguage()
{
	FPBRLocalization::ToggleLanguage();
	RefreshOpenPluginWindows();
	UToolMenus::Get()->RefreshAllWidgets();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}
}

void FPBRStudioModule::RefreshOpenPluginWindows()
{
	if (TSharedPtr<SDockTab> ExistingMainTab = MainTab.Pin())
	{
		ExistingMainTab->SetLabel(PBRText(TEXT("PBRStudioTabLabel"), TEXT("AR Studio"), TEXT("AR Studio")));
		ExistingMainTab->SetContent(SNew(SPBRStudioMainWindow));
	}
	if (TSharedPtr<SDockTab> ExistingMagicTab = MagicOutlinerTab.Pin())
	{
		ExistingMagicTab->SetLabel(PBRText(TEXT("PBRMagicOutlinerTabLabel"), TEXT("魔法大纲"), TEXT("Magic Outliner")));
		ExistingMagicTab->SetContent(SNew(SPBRMagicOutlinerWindow));
	}
}

IMPLEMENT_MODULE(FPBRStudioModule, PBRStudio)

#undef LOCTEXT_NAMESPACE
