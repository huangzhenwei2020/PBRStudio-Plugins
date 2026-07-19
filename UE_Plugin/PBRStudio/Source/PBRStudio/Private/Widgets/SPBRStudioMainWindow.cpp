#include "Widgets/SPBRStudioMainWindow.h"
#include "Widgets/SPBRSideNav.h"
#include "Widgets/SPBRTextureSuiteTab.h"
#include "Widgets/SPBRDownloadLibraryTab.h"
#include "Widgets/SPBRTextureStreamingTab.h"
#include "Widgets/SPBRSpecialMaterialsTab.h"
#include "Widgets/SPBRSceneMaterialReplaceTab.h"
#include "Widgets/SPBRSceneLightConvertTab.h"
#include "Widgets/SPBRSceneCameraConvertTab.h"
#include "Widgets/SPBRSceneBatchAdjustTab.h"
#include "Widgets/SPBRViewportSyncTab.h"
#include "Widgets/SPBRSubstrateModeTab.h"
#include "Widgets/SPBRMagicOutlinerWindow.h"
#include "Widgets/SPBRDlssTab.h"
#include "MaterialVault/SMaterialVaultWindow.h"

#include "PBRStudioModule.h"
#include "Services/PBRSceneMaterialReplacer.h"
#include "Services/PBRWorkspaceTemplate.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SPBRStudioMainWindow"

namespace
{
TSharedRef<SWidget> MakePBRTopBarButton(const FText& Label, const FText& ToolTip, const FName IconName, const FOnClicked& OnClicked, const FName ButtonStyle = "FlatButton")
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), ButtonStyle)
		.ContentPadding(FMargin(10, 5))
		.ToolTipText(ToolTip)
		.OnClicked(OnClicked)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush(IconName))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.92f, 1.0f, 0.95f)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
		];
}
}

void SPBRStudioMainWindow::Construct(const FArguments& InArgs)
{
	auto OnCompactChanged = [this](bool bCompact)
	{
		bAnyContentCompactMode = bCompact;
	};

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0)
		[
			BuildTopBar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0)
			[
				SAssignNew(SideNavWidget, SPBRSideNav)
				.Visibility(this, &SPBRStudioMainWindow::GetSideNavVisibility)
				.OnTabChanged_Lambda([this](int32 Index)
				{
					if (ContentSwitcher.IsValid())
					{
						ContentSwitcher->SetActiveWidgetIndex(Index);
					}
				})
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0)
			[
				SAssignNew(ContentSwitcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(TextureSuiteTab, SPBRTextureSuiteTab)
					.OnCompactModeChanged_Lambda(OnCompactChanged)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SPBRDownloadLibraryTab)
						.OnLibrarySentToTextureSuite_Lambda([this]()
						{
							if (TextureSuiteTab.IsValid())
							{
								TextureSuiteTab->LoadExternalLibraryFolderAndScan();
							}
						})
						.OnLibrarySentToSpecialMaterials_Lambda([this]()
						{
							if (SpecialMaterialsTab.IsValid())
							{
								SpecialMaterialsTab->LoadExternalLibraryFolderAndScan();
							}
						})
					]
				]
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(MaterialVaultTab, SMaterialVaultWindow)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SPBRTextureStreamingTab)
					]
				]
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(SpecialMaterialsTab, SPBRSpecialMaterialsTab)
					.OnCompactModeChanged_Lambda(OnCompactChanged)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SPBRSceneMaterialReplaceTab)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SPBRSceneLightConvertTab)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SPBRSceneCameraConvertTab)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SPBRSceneBatchAdjustTab)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SPBRViewportSyncTab)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SPBRSubstrateModeTab)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SPBRMagicOutlinerWindow)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SPBRDlssTab)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0)
		[
			BuildStatusBar()
		]
	];
}

EVisibility SPBRStudioMainWindow::GetSideNavVisibility() const
{
	return bAnyContentCompactMode ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<SWidget> SPBRStudioMainWindow::BuildTopBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.BorderBackgroundColor(FLinearColor(0.030f, 0.036f, 0.048f, 1.0f))
		.Padding(10, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 12, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("ClassIcon.Material"))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.24f, 1.0f)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 22, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "建筑可视化工作台"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.98f, 1.0f, 1.0f)))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				MakePBRTopBarButton(
					LOCTEXT("CreateWorkspaceTemplate", "工作模板"),
					LOCTEXT("CreateWorkspaceTemplateTip", "按当前设置一键创建 Content 下的项目工作目录，并可选生成基础场景环境。"),
					"Icons.Plus",
					FOnClicked::CreateSP(this, &SPBRStudioMainWindow::OnCreateWorkspaceTemplate),
					"FlatButton.Primary")
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				MakePBRTopBarButton(
					LOCTEXT("WorkspaceTemplateSettings", "设置"),
					LOCTEXT("WorkspaceTemplateSettingsTip", "设置工作模板的项目名称、英文目录名和场景环境选项。"),
					"Icons.Settings",
					FOnClicked::CreateSP(this, &SPBRStudioMainWindow::OnOpenWorkspaceTemplateSettings),
					"FlatButton.Primary")
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				MakePBRTopBarButton(
					LOCTEXT("OpenMagicOutliner", "魔法大纲"),
					LOCTEXT("OpenMagicOutlinerTip", "打开独立的魔法大纲窗口，用分类、组、材质、灯光类型等方式管理场景对象。"),
					"Icons.TreeItem",
					FOnClicked::CreateSP(this, &SPBRStudioMainWindow::OnOpenMagicOutliner))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				MakePBRTopBarButton(
					LOCTEXT("GlobalManualRefreshScene", "刷新场景"),
					LOCTEXT("GlobalManualRefreshSceneTip", "刷新当前关卡材质显示，替换材质后如果仍显示棋盘格可点这里。"),
					"Icons.Refresh",
					FOnClicked::CreateSP(this, &SPBRStudioMainWindow::OnGlobalManualRefreshScene))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				MakePBRTopBarButton(
					LOCTEXT("GlobalOpenProjectFolder", "项目文件夹"),
					LOCTEXT("GlobalOpenProjectFolderTip", "在电脑文件管理器中打开当前 UE 项目所在文件夹。"),
					"Icons.FolderOpen",
					FOnClicked::CreateSP(this, &SPBRStudioMainWindow::OnGlobalOpenProjectFolder))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				MakePBRTopBarButton(
					LOCTEXT("GlobalSelectSameMaterial", "选择同材质"),
					LOCTEXT("GlobalSelectSameMaterialTip", "选择所有使用当前选中物体同一材质的场景物体。"),
					"Icons.Search",
					FOnClicked::CreateSP(this, &SPBRStudioMainWindow::OnGlobalSelectSameMaterial))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
				.ContentPadding(FMargin(10, 5))
				.ToolTipText(LOCTEXT("GlobalCompactModeTip", "切换贴图套件、本地材质库和特殊材质的精简模式。"))
				.OnClicked(this, &SPBRStudioMainWindow::OnGlobalToggleCompactMode)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 6, 0)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.Layout"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.92f, 1.0f, 0.95f)))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SPBRStudioMainWindow::GetGlobalCompactModeText)
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					]
				]
			]
		];
}

FReply SPBRStudioMainWindow::OnCreateWorkspaceTemplate()
{
	FPBRWorkspaceTemplateResult Result;
	if (FPBRWorkspaceTemplate::CreateTemplate(FPBRWorkspaceTemplate::LoadSettings(), Result))
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::FromString(Result.Message));
		}
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
	}
	else
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::FromString(Result.Message));
		}
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
	}
	return FReply::Handled();
}

FReply SPBRStudioMainWindow::OnOpenWorkspaceTemplateSettings()
{
	FPBRWorkspaceTemplateSettings InitialSettings = FPBRWorkspaceTemplate::LoadSettings();
	TSharedPtr<SEditableTextBox> ProjectNameBox;
	TSharedPtr<SEditableTextBox> FolderNameBox;
	TSharedPtr<SCheckBox> CreateEnvironmentCheck;
	TSharedPtr<SWindow> Window;

	Window = SNew(SWindow)
		.Title(LOCTEXT("WorkspaceTemplateSettingsTitle", "工作模板设置"))
		.ClientSize(FVector2D(460.0f, 260.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SWidget> FolderNameEditor =
		SAssignNew(FolderNameBox, SEditableTextBox)
		.Text(FText::FromString(InitialSettings.ProjectFolderName))
		.ToolTipText(LOCTEXT("WorkspaceFolderNameTip", "UE 内容目录只使用英文、数字和下划线。中文项目名会自动转换成安全英文目录。"));

	Window->SetContent(
		SNew(SBorder)
		.Padding(16.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WorkspaceProjectNameLabel", "项目名称"))
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SAssignNew(ProjectNameBox, SEditableTextBox)
				.Text(FText::FromString(InitialSettings.ProjectDisplayName))
				.OnTextChanged_Lambda([FolderNameBox](const FText& NewText)
				{
					if (FolderNameBox.IsValid())
					{
						FolderNameBox->SetText(FText::FromString(FPBRWorkspaceTemplate::MakeSafeEnglishProjectFolderName(NewText.ToString())));
					}
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WorkspaceFolderNameLabel", "生成英文目录名"))
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				FolderNameEditor
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 14.0f)
			[
				SAssignNew(CreateEnvironmentCheck, SCheckBox)
				.IsChecked(InitialSettings.bCreateSceneEnvironment ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("WorkspaceCreateEnvironment", "创建场景环境（SunSky 或基础天空灯光）"))
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSpacer)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("WorkspaceSaveSettings", "保存"))
					.OnClicked_Lambda([Window, ProjectNameBox, FolderNameBox, CreateEnvironmentCheck]()
					{
						FPBRWorkspaceTemplateSettings Settings;
						Settings.ProjectDisplayName = ProjectNameBox.IsValid() ? ProjectNameBox->GetText().ToString() : TEXT("新项目");
						Settings.ProjectFolderName = FolderNameBox.IsValid() ? FolderNameBox->GetText().ToString() : FString();
						Settings.ProjectFolderName = FPBRWorkspaceTemplate::MakeSafeEnglishProjectFolderName(Settings.ProjectFolderName.IsEmpty() ? Settings.ProjectDisplayName : Settings.ProjectFolderName);
						Settings.bCreateSceneEnvironment = CreateEnvironmentCheck.IsValid() && CreateEnvironmentCheck->IsChecked();
						FPBRWorkspaceTemplate::SaveSettings(Settings);
						if (Window.IsValid())
						{
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("WorkspaceCancelSettings", "取消"))
					.OnClicked_Lambda([Window]()
					{
						if (Window.IsValid())
						{
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		]);

	FSlateApplication::Get().AddWindow(Window.ToSharedRef());
	return FReply::Handled();
}

FReply SPBRStudioMainWindow::OnGlobalManualRefreshScene()
{
	FPBRSceneMaterialReplacer::RefreshCurrentLevelMaterialAssignments();
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("GlobalRefreshDone", "已手动更新场景材质显示"));
	}
	return FReply::Handled();
}

FReply SPBRStudioMainWindow::OnGlobalOpenProjectFolder()
{
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPlatformProcess::ExploreFolder(*ProjectDir);
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("已打开项目文件夹: %s"), *ProjectDir)));
	}
	return FReply::Handled();
}

FReply SPBRStudioMainWindow::OnGlobalSelectSameMaterial()
{
	if (GUnrealEd)
	{
		GUnrealEd->edactSelectMatchingMaterial();
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("GlobalSelectSameMaterialDone", "已选择使用相同材质的场景物体"));
		}
	}
	else if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("GlobalSelectSameMaterialFailed", "当前编辑器环境不可用，无法选择相同材质"));
	}
	return FReply::Handled();
}

FReply SPBRStudioMainWindow::OnOpenMagicOutliner()
{
	FPBRStudioModule::ToggleMagicOutlinerWindow();
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("MagicOutlinerOpened", "已打开魔法大纲窗口"));
	}
	return FReply::Handled();
}

FReply SPBRStudioMainWindow::OnGlobalToggleCompactMode()
{
	const bool bNewCompactMode = !bAnyContentCompactMode;
	bAnyContentCompactMode = bNewCompactMode;
	if (TextureSuiteTab.IsValid())
	{
		TextureSuiteTab->SetCompactModeFromGlobal(bNewCompactMode);
	}
	if (SpecialMaterialsTab.IsValid())
	{
		SpecialMaterialsTab->SetCompactModeFromGlobal(bNewCompactMode);
	}
	if (MaterialVaultTab.IsValid())
	{
		MaterialVaultTab->SetCompactModeFromGlobal(bNewCompactMode);
	}
	return FReply::Handled();
}

FText SPBRStudioMainWindow::GetGlobalCompactModeText() const
{
	return bAnyContentCompactMode ? LOCTEXT("GlobalStandardMode", "标准模式") : LOCTEXT("GlobalCompactMode", "精简模式");
}

TSharedRef<SWidget> SPBRStudioMainWindow::BuildStatusBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.BorderBackgroundColor(FLinearColor(0.030f, 0.036f, 0.048f, 1.0f))
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(LOCTEXT("StatusReady", "准备就绪"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.86f, 1.0f)))
			]
		];
}

#undef LOCTEXT_NAMESPACE
