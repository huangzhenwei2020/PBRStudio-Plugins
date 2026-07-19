#include "Widgets/SPBRDlssTab.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "IDesktopPlatform.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SPBRDlssTab"

namespace
{
constexpr const TCHAR* NvidiaDlssDownloadUrl = TEXT("https://developer.nvidia.com/rtx/dlss/get-started#ue-downloads");
constexpr const TCHAR* NvidiaDlss57PluginZipUrl = TEXT("https://developer.nvidia.com/downloads/assets/gameworks/downloads/secure/dlss/2026.05.21_ue5.7_dlss4.5plugin_v8.6.1.zip");
constexpr int64 DlssDownloadChunkSize = 8 * 1024 * 1024;

FLinearColor PBRCardColor()
{
	return FLinearColor(0.045f, 0.052f, 0.066f, 1.0f);
}

FLinearColor PBRTextMuted()
{
	return FLinearColor(0.62f, 0.68f, 0.76f, 1.0f);
}

FLinearColor PBRGood()
{
	return FLinearColor(0.28f, 0.82f, 0.45f, 1.0f);
}

FLinearColor PBRWarn()
{
	return FLinearColor(1.0f, 0.70f, 0.22f, 1.0f);
}

FLinearColor PBRBad()
{
	return FLinearColor(1.0f, 0.35f, 0.32f, 1.0f);
}

TSharedPtr<FJsonObject> FindJsonPluginEntry(const TArray<TSharedPtr<FJsonValue>>& PluginsArray, const FString& PluginName)
{
	for (const TSharedPtr<FJsonValue>& Value : PluginsArray)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Value.IsValid() && Value->TryGetObject(Object) && Object && Object->IsValid())
		{
			FString Name;
			if ((*Object)->TryGetStringField(TEXT("Name"), Name) && Name == PluginName)
			{
				return *Object;
			}
		}
	}
	return nullptr;
}

bool IsNvidiaRuntimePluginName(const FString& PluginName)
{
	const FString Upper = PluginName.ToUpper();
	if (Upper.Contains(TEXT("WINGDK")))
	{
		return false;
	}
	return Upper.Contains(TEXT("DLSS"))
		|| Upper.Contains(TEXT("STREAMLINE"))
		|| Upper.Contains(TEXT("REFLEX"))
		|| Upper.Contains(TEXT("NIS"));
}

bool IsSafeNvidiaPluginFilePath(const FString& PluginFile)
{
	FString Normalized = PluginFile;
	FPaths::NormalizeFilename(Normalized);
	const FString UpperPath = Normalized.ToUpper();
	return !UpperPath.Contains(TEXT("/PLATFORMS/"))
		&& !UpperPath.Contains(TEXT("/SAMPLES/"))
		&& IsNvidiaRuntimePluginName(FPaths::GetBaseFilename(PluginFile));
}

FString PowerShellQuote(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("'"), TEXT("''"));
	return TEXT("'") + Escaped + TEXT("'");
}
}

void SPBRDlssTab::Construct(const FArguments& InArgs)
{
	RefreshStatus();
	RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SPBRDlssTab::RefreshRuntimeStatus));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.BorderBackgroundColor(FLinearColor(0.025f, 0.030f, 0.040f, 1.0f))
		.Padding(18)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "NVIDIA DLSS"))
					.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.96f, 1.0f, 1.0f)))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Subtitle", "检测显卡配置，安装 NVIDIA DLSS Unreal Engine Plugin，并启用到当前项目。"))
					.Font(FAppStyle::GetFontStyle("NormalFont"))
					.ColorAndOpacity(FSlateColor(PBRTextMuted()))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					BuildStatusCard()
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					BuildActionCard()
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					BuildRuntimeCard()
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					BuildSettingsCard()
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					BuildRenderCard()
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					BuildHelpCard()
				]
			]
		]
	];
}

TSharedRef<SWidget> SPBRDlssTab::BuildStatusCard()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor(PBRCardColor())
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StatusHeader", "当前状态"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeInfoRow(LOCTEXT("GpuLabel", "显卡"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetGpuStatusText), TAttribute<FSlateColor>::CreateSP(this, &SPBRDlssTab::GetGpuStatusColor))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeInfoRow(LOCTEXT("DriverLabel", "驱动"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetDriverStatusText), TAttribute<FSlateColor>(FSlateColor(PBRTextMuted())))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeInfoRow(LOCTEXT("PluginLabel", "DLSS 插件"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetPluginStatusText), TAttribute<FSlateColor>::CreateSP(this, &SPBRDlssTab::GetPluginStatusColor))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeInfoRow(LOCTEXT("ProjectLabel", "项目"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetProjectStatusText), TAttribute<FSlateColor>(FSlateColor(PBRTextMuted())))
			]
		];
}

TSharedRef<SWidget> SPBRDlssTab::BuildActionCard()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor(PBRCardColor())
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ActionsHeader", "操作"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 8)
				[
					MakeActionButton(LOCTEXT("Refresh", "重新检测"), LOCTEXT("RefreshTip", "重新检测显卡、驱动和项目插件状态。"), "Icons.Refresh", FOnClicked::CreateSP(this, &SPBRDlssTab::OnRefreshClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 8)
				[
					MakeActionButton(LOCTEXT("AutoInstall", "自动下载并安装"), LOCTEXT("AutoInstallTip", "下载 NVIDIA DLSS UE 5.7 插件，解压后复制到当前项目 Plugins，并写入 .uproject 启用。"), "Icons.Download", FOnClicked::CreateSP(this, &SPBRDlssTab::OnAutoDownloadInstallClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 8)
				[
					MakeActionButton(LOCTEXT("Download", "打开 NVIDIA 下载页"), LOCTEXT("DownloadTip", "打开 NVIDIA 官方 DLSS Unreal Engine Plugin 下载页面。"), "Icons.Download", FOnClicked::CreateSP(this, &SPBRDlssTab::OnOpenNvidiaDownloadClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 8)
				[
					MakeActionButton(LOCTEXT("Install", "选择已下载插件目录"), LOCTEXT("InstallTip", "选择解压后的 DLSS 插件目录，复制到当前项目 Plugins 文件夹。"), "Icons.FolderOpen", FOnClicked::CreateSP(this, &SPBRDlssTab::OnInstallFromFolderClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 8)
				[
					MakeActionButton(LOCTEXT("Enable", "启用项目插件"), LOCTEXT("EnableTip", "把已安装的 DLSS/Streamline 插件写入当前 .uproject，重启 UE 后生效。"), "Icons.Check", FOnClicked::CreateSP(this, &SPBRDlssTab::OnEnableProjectPluginsClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 0, 8)
				[
					MakeActionButton(LOCTEXT("OpenPlugins", "打开项目 Plugins"), LOCTEXT("OpenPluginsTip", "打开当前项目 Plugins 文件夹。"), "Icons.FolderClosed", FOnClicked::CreateSP(this, &SPBRDlssTab::OnOpenProjectPluginsClicked))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(LOCTEXT("Ready", "准备就绪。"))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(PBRTextMuted()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
			[
				BuildDownloadPanel()
			]
		];
}

TSharedRef<SWidget> SPBRDlssTab::BuildDownloadPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor(FLinearColor(0.035f, 0.043f, 0.056f, 1.0f))
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SAssignNew(DownloadProgressText, STextBlock)
					.Text(TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetDownloadProgressText))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.94f, 1.0f, 1.0f)))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
				[
					MakeActionButton(LOCTEXT("PauseDownload", "暂停"), LOCTEXT("PauseDownloadTip", "暂停当前下载，保留已下载部分。"), "Icons.Pause", FOnClicked::CreateSP(this, &SPBRDlssTab::OnPauseDlssDownloadClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
				[
					MakeActionButton(LOCTEXT("ResumeDownload", "继续"), LOCTEXT("ResumeDownloadTip", "从已下载位置继续下载。"), "Icons.Play", FOnClicked::CreateSP(this, &SPBRDlssTab::OnResumeDlssDownloadClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
				[
					MakeActionButton(LOCTEXT("CancelDownload", "取消"), LOCTEXT("CancelDownloadTip", "取消当前下载。已下载的临时文件会保留，稍后可以继续。"), "Icons.X", FOnClicked::CreateSP(this, &SPBRDlssTab::OnCancelDlssDownloadClicked))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SAssignNew(DownloadProgressBar, SProgressBar)
				.Percent(TAttribute<TOptional<float>>::CreateSP(this, &SPBRDlssTab::GetDownloadProgressPercent))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(DownloadDetailText, STextBlock)
				.Text(TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetDownloadDetailText))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(PBRTextMuted()))
			]
		];
}

TSharedRef<SWidget> SPBRDlssTab::BuildRuntimeCard()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor(PBRCardColor())
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RuntimeHeader", "运行时生效诊断"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeInfoRow(LOCTEXT("RuntimeSupportLabel", "DLSS-SR 支持"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetDlssSupportStatusText), TAttribute<FSlateColor>::CreateSP(this, &SPBRDlssTab::GetDlssSupportStatusColor))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeInfoRow(LOCTEXT("RuntimeSessionLabel", "当前会话"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetDlssSessionStatusText), TAttribute<FSlateColor>::CreateSP(this, &SPBRDlssTab::GetDlssSessionStatusColor))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeInfoRow(LOCTEXT("RuntimeViewportLabel", "Editor / PIE 视口"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetViewportStatusText), TAttribute<FSlateColor>::CreateSP(this, &SPBRDlssTab::GetViewportStatusColor))
			]
		];
}

TSharedRef<SWidget> SPBRDlssTab::BuildSettingsCard()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor(PBRCardColor())
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SettingsHeader", "DLSS 设置"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SettingsDesc", "把常用 DLSS 默认项写入当前项目配置；如果插件已加载，会同时尝试应用到当前会话。"))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(PBRTextMuted()))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeActionButton(LOCTEXT("DlssQuality", "质量模式"), LOCTEXT("DlssQualityTip", "启用 DLSS，并把默认屏幕百分比设为 77。"), "Icons.Check", FOnClicked::CreateSP(this, &SPBRDlssTab::OnApplyDlssQualityClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeActionButton(LOCTEXT("DlssBalanced", "平衡模式"), LOCTEXT("DlssBalancedTip", "启用 DLSS，并把默认屏幕百分比设为 67。"), "Icons.Check", FOnClicked::CreateSP(this, &SPBRDlssTab::OnApplyDlssBalancedClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeActionButton(LOCTEXT("DlssPerformance", "性能模式"), LOCTEXT("DlssPerformanceTip", "启用 DLSS，并把默认屏幕百分比设为 58。"), "Icons.Check", FOnClicked::CreateSP(this, &SPBRDlssTab::OnApplyDlssPerformanceClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeActionButton(LOCTEXT("DlssDisable", "关闭默认 DLSS"), LOCTEXT("DlssDisableTip", "把项目默认 DLSS 开关设为关闭。"), "Icons.X", FOnClicked::CreateSP(this, &SPBRDlssTab::OnDisableDlssDefaultClicked))
				]
			]
		];
}

TSharedRef<SWidget> SPBRDlssTab::BuildRenderCard()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor(FLinearColor(0.035f, 0.040f, 0.052f, 1.0f))
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RenderHeader", "渲染与帧生成"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeInfoRow(LOCTEXT("MrqLabel", "Movie Render Queue"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetMrqStatusText), TAttribute<FSlateColor>::CreateSP(this, &SPBRDlssTab::GetMrqStatusColor))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeInfoRow(LOCTEXT("FrameGenLabel", "Frame Generation"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetFrameGenerationStatusText), TAttribute<FSlateColor>::CreateSP(this, &SPBRDlssTab::GetFrameGenerationStatusColor))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeInfoRow(LOCTEXT("FrameTimingLabel", "FG 实测 FPS"), TAttribute<FText>::CreateSP(this, &SPBRDlssTab::GetFrameTimingStatusText), TAttribute<FSlateColor>::CreateSP(this, &SPBRDlssTab::GetFrameTimingStatusColor))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeActionButton(LOCTEXT("DlssGOff", "FG 关闭"), LOCTEXT("DlssGOffTip", "关闭 DLSS Frame Generation。"), "Icons.X", FOnClicked::CreateSP(this, &SPBRDlssTab::OnSetDlssGOffClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeActionButton(LOCTEXT("DlssGAuto", "FG 自动"), LOCTEXT("DlssGAutoTip", "使用 NVIDIA 推荐的自动 DLSS Frame Generation 模式。"), "Icons.Check", FOnClicked::CreateSP(this, &SPBRDlssTab::OnSetDlssGAutoClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeActionButton(LOCTEXT("DlssG2X", "FG 2X"), LOCTEXT("DlssG2XTip", "每个渲染帧生成 1 个额外帧。"), "Icons.Plus", FOnClicked::CreateSP(this, &SPBRDlssTab::OnSetDlssG2XClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeActionButton(LOCTEXT("DlssG3X", "FG 3X"), LOCTEXT("DlssG3XTip", "每个渲染帧生成 2 个额外帧，需要硬件支持。"), "Icons.Plus", FOnClicked::CreateSP(this, &SPBRDlssTab::OnSetDlssG3XClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeActionButton(LOCTEXT("DlssG4X", "FG 4X"), LOCTEXT("DlssG4XTip", "每个渲染帧生成 3 个额外帧，需要硬件支持。"), "Icons.Plus", FOnClicked::CreateSP(this, &SPBRDlssTab::OnSetDlssG4XClicked))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RenderHelpText", "MRQ 需要在渲染队列配置中加入 DLSS/DLAA Setting；Frame Generation 不支持普通 Editor Viewport，请用 New Editor Window PIE、Standalone 或打包运行验证。UE 内置 FPS 统计可能不包含生成帧，建议用 NVIDIA 指示器或外部帧率工具确认。"))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(PBRTextMuted()))
			]
		];
}

TSharedRef<SWidget> SPBRDlssTab::BuildHelpCard()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor(FLinearColor(0.035f, 0.040f, 0.052f, 1.0f))
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("HelpTitle", "说明"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("HelpText", "DLSS 需要 NVIDIA RTX 显卡。官方插件通常需要在 NVIDIA Developer 页面登录下载；下载后先解压，再用这里的“选择已下载插件目录”复制到项目。启用插件后通常需要重启 Unreal Editor。"))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(PBRTextMuted()))
			]
		];
}

TSharedRef<SWidget> SPBRDlssTab::MakeInfoRow(const FText& Label, const TAttribute<FText>& Value, const TAttribute<FSlateColor>& Color) const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(120.0f)
			[
				SNew(STextBlock).Text(Label).ColorAndOpacity(FSlateColor(PBRTextMuted()))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Value).ColorAndOpacity(Color).AutoWrapText(true)
		];
}

TSharedRef<SWidget> SPBRDlssTab::MakeActionButton(const FText& Label, const FText& ToolTip, const FName IconName, const FOnClicked& OnClicked) const
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton")
		.ContentPadding(FMargin(10, 7))
		.ToolTipText(ToolTip)
		.OnClicked(OnClicked)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush(IconName))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.50f, 0.90f, 0.46f, 1.0f)))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
		];
}

void SPBRDlssTab::RefreshStatus()
{
	SystemInfo = QuerySystemInfo();
	bDlssAvailable = IsDlssPluginAvailable();
	bDlssEnabled = IsDlssPluginEnabled();
	DlssPluginLocation = GetDlssPluginLocation();
	RuntimeInfo = QueryRuntimeInfo();
}

EActiveTimerReturnType SPBRDlssTab::RefreshRuntimeStatus(double InCurrentTime, float InDeltaTime)
{
	RuntimeInfo = QueryRuntimeInfo();
	Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

SPBRDlssTab::FDlssSystemInfo SPBRDlssTab::QuerySystemInfo() const
{
	FDlssSystemInfo Info;
	FString Stdout;
	FString Stderr;
	if (!TryRunNvidiaSmi(Stdout, Stderr))
	{
		Info.Message = TEXT("未检测到 nvidia-smi，无法自动判断显卡。");
		return Info;
	}

	TArray<FString> Lines;
	Stdout.ParseIntoArrayLines(Lines, true);
	if (Lines.IsEmpty())
	{
		Info.Message = TEXT("nvidia-smi 没有返回显卡信息。");
		return Info;
	}

	FString FirstLine = Lines[0];
	TArray<FString> Parts;
	FirstLine.ParseIntoArray(Parts, TEXT(","), true);
	Info.GpuName = Parts.Num() > 0 ? Parts[0].TrimStartAndEnd() : FirstLine.TrimStartAndEnd();
	Info.DriverVersion = Parts.Num() > 1 ? Parts[1].TrimStartAndEnd() : FString();
	Info.bHasNvidiaGpu = !Info.GpuName.IsEmpty();
	const FString UpperName = Info.GpuName.ToUpper();
	Info.bLikelyRtx = UpperName.Contains(TEXT("RTX")) || UpperName.Contains(TEXT("NVIDIA RTX")) || UpperName.Contains(TEXT("QUADRO RTX"));
	Info.Message = Info.bLikelyRtx ? TEXT("检测到 RTX 显卡，适合使用 DLSS。") : TEXT("检测到 NVIDIA 显卡，但未确认是 RTX。");
	return Info;
}

SPBRDlssTab::FDlssRuntimeInfo SPBRDlssTab::QueryRuntimeInfo() const
{
	FDlssRuntimeInfo Info;
	Info.bStreamlineEnabled = IsPluginEnabled(TEXT("StreamlineCore"));
	Info.bDlssMoviePipelineEnabled = IsPluginEnabled(TEXT("DLSSMoviePipelineSupport"));
	Info.bStreamlineDlssGEnabled = IsPluginEnabled(TEXT("StreamlineDLSSG"));
	Info.bDlssBlueprintLibraryLoaded = FindObject<UClass>(nullptr, TEXT("/Script/DLSSBlueprint.DLSSLibrary")) != nullptr;
	Info.bDlssGBlueprintLibraryLoaded = FindObject<UClass>(nullptr, TEXT("/Script/StreamlineDLSSGBlueprint.StreamlineLibraryDLSSG")) != nullptr;
	Info.bDlssSrSupportKnown = TryCallDlssLibraryBool(TEXT("IsDLSSSupported"), Info.bDlssSrSupported);
	Info.bDlssSrEnabledKnown = TryCallDlssLibraryBool(TEXT("IsDLSSEnabled"), Info.bDlssSrEnabled);
	Info.bDlssGSupportKnown = TryCallDlssGLibraryBool(TEXT("IsDLSSGSupported"), Info.bDlssGSupported);
	Info.bDlssGFrameTimingKnown = TryCallDlssGFrameTiming(Info.DlssGFrameRateInHertz, Info.DlssGFramesPresented);
	TryGetConsoleInt(TEXT("r.NGX.DLSS.Enable"), Info.DlssEnableCVar);
	TryGetConsoleInt(TEXT("r.Streamline.DLSSG.Enable"), Info.DlssGEnableCVar);
	TryGetConsoleInt(TEXT("r.Streamline.DLSSG.FramesToGenerate"), Info.DlssGFramesToGenerateCVar);
	TryGetConsoleInt(TEXT("r.TemporalAA.Upscaler"), Info.TemporalAAUpscalerCVar);
	TryGetConsoleInt(TEXT("r.TemporalAA.Upsampling"), Info.TemporalAAUpsamplingCVar);
	TryGetConsoleFloat(TEXT("r.ScreenPercentage"), Info.ScreenPercentageCVar);

	const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini"));
	const TCHAR* DlssSettingsSection = TEXT("/Script/DLSS.DLSSSettings");
	const TCHAR* StreamlineSettingsSection = TEXT("/Script/StreamlineRHI.StreamlineSettings");
	bool bConfigBool = false;
	FString ConfigDlssEnableValue;
	Info.bProjectEditorViewportEnabled = (GConfig && GConfig->GetBool(DlssSettingsSection, TEXT("bEnableDLSSInEditorViewports"), bConfigBool, ConfigPath)) ? bConfigBool : false;
	Info.bProjectPieViewportEnabled = (GConfig && GConfig->GetBool(DlssSettingsSection, TEXT("bEnableDLSSInPlayInEditorViewports"), bConfigBool, ConfigPath)) ? bConfigBool : false;
	Info.bProjectDlssFGPieEnabled = (GConfig && GConfig->GetBool(StreamlineSettingsSection, TEXT("bEnableDLSSFGInPlayInEditorViewports"), bConfigBool, ConfigPath)) ? bConfigBool : false;
	Info.bProjectConfigHasDlssDefaults = FPaths::FileExists(ConfigPath)
		&& GConfig
		&& GConfig->GetString(TEXT("SystemSettings"), TEXT("r.NGX.DLSS.Enable"), ConfigDlssEnableValue, ConfigPath);

	bool bLoadedBool = false;
	if (TryReadLoadedBool(TEXT("/Script/DLSS.DLSSSettings"), TEXT("bEnableDLSSInEditorViewports"), bLoadedBool))
	{
		Info.bProjectEditorViewportEnabled = bLoadedBool;
	}
	if (TryReadLoadedBool(TEXT("/Script/DLSS.DLSSSettings"), TEXT("bEnableDLSSInPlayInEditorViewports"), bLoadedBool))
	{
		Info.bProjectPieViewportEnabled = bLoadedBool;
	}
	if (TryReadLoadedBool(TEXT("/Script/StreamlineRHI.StreamlineSettings"), TEXT("bEnableDLSSFGInPlayInEditorViewports"), bLoadedBool))
	{
		Info.bProjectDlssFGPieEnabled = bLoadedBool;
	}

	Info.UserEditorViewportOverride.Reset();
	Info.UserPieViewportOverride.Reset();
	Info.UserDlssFGPieOverride.Reset();
	const FString UserConfigPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), FPlatformProperties::PlatformName(), TEXT("Engine.ini"));
	const TCHAR* DlssOverrideSection = TEXT("/Script/DLSS.DLSSOverrideSettings");
	const TCHAR* StreamlineOverrideSection = TEXT("/Script/StreamlineRHI.StreamlineOverrideSettings");
	if (GConfig)
	{
		GConfig->GetString(DlssOverrideSection, TEXT("EnableDLSSInEditorViewportsOverride"), Info.UserEditorViewportOverride, UserConfigPath);
		GConfig->GetString(DlssOverrideSection, TEXT("EnableDLSSInPlayInEditorViewportsOverride"), Info.UserPieViewportOverride, UserConfigPath);
		GConfig->GetString(StreamlineOverrideSection, TEXT("EnableDLSSFGInPlayInEditorViewportsOverride"), Info.UserDlssFGPieOverride, UserConfigPath);
	}

	uint8 LoadedOverride = 2;
	if (TryReadLoadedEnumByte(TEXT("/Script/StreamlineRHI.StreamlineOverrideSettings"), TEXT("EnableDLSSFGInPlayInEditorViewportsOverride"), LoadedOverride))
	{
		Info.UserDlssFGPieOverride = LoadedOverride == 0 ? TEXT("Enabled") : (LoadedOverride == 1 ? TEXT("Disabled") : TEXT("UseProjectSettings"));
	}
	return Info;
}

bool SPBRDlssTab::TryRunNvidiaSmi(FString& OutStdout, FString& OutStderr) const
{
	const FString Args = TEXT("--query-gpu=name,driver_version --format=csv,noheader");
	int32 ReturnCode = -1;
	if (FPlatformProcess::ExecProcess(TEXT("nvidia-smi"), *Args, &ReturnCode, &OutStdout, &OutStderr) && ReturnCode == 0)
	{
		return true;
	}

	const FString DefaultPath = TEXT("C:/Program Files/NVIDIA Corporation/NVSMI/nvidia-smi.exe");
	if (FPaths::FileExists(DefaultPath))
	{
		return FPlatformProcess::ExecProcess(*DefaultPath, *Args, &ReturnCode, &OutStdout, &OutStderr) && ReturnCode == 0;
	}
	return false;
}

bool SPBRDlssTab::IsDlssPluginAvailable() const
{
	return IPluginManager::Get().FindPlugin(TEXT("DLSS")).IsValid();
}

bool SPBRDlssTab::IsDlssPluginEnabled() const
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DLSS"));
	return Plugin.IsValid() && Plugin->IsEnabled();
}

FString SPBRDlssTab::GetDlssPluginLocation() const
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DLSS"));
	return Plugin.IsValid() ? Plugin->GetBaseDir() : FString();
}

bool SPBRDlssTab::IsPluginEnabled(const FString& PluginName) const
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
	return Plugin.IsValid() && Plugin->IsEnabled();
}

bool SPBRDlssTab::TryCallDlssLibraryBool(const TCHAR* FunctionName, bool& OutValue) const
{
	UClass* LibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/DLSSBlueprint.DLSSLibrary"));
	if (!LibraryClass)
	{
		return false;
	}
	UFunction* Function = LibraryClass->FindFunctionByName(FName(FunctionName));
	UObject* DefaultObject = LibraryClass->GetDefaultObject();
	if (!Function || !DefaultObject)
	{
		return false;
	}

	struct FBoolReturnParams
	{
		bool ReturnValue = false;
	};

	FBoolReturnParams Params;
	DefaultObject->ProcessEvent(Function, &Params);
	OutValue = Params.ReturnValue;
	return true;
}

bool SPBRDlssTab::TryCallDlssGLibraryBool(const TCHAR* FunctionName, bool& OutValue) const
{
	UClass* LibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/StreamlineDLSSGBlueprint.StreamlineLibraryDLSSG"));
	if (!LibraryClass)
	{
		return false;
	}
	UFunction* Function = LibraryClass->FindFunctionByName(FName(FunctionName));
	UObject* DefaultObject = LibraryClass->GetDefaultObject();
	if (!Function || !DefaultObject)
	{
		return false;
	}

	struct FBoolReturnParams
	{
		bool ReturnValue = false;
	};

	FBoolReturnParams Params;
	DefaultObject->ProcessEvent(Function, &Params);
	OutValue = Params.ReturnValue;
	return true;
}

bool SPBRDlssTab::TryCallDlssGFrameTiming(float& OutFrameRateInHertz, int32& OutFramesPresented) const
{
	UClass* LibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/StreamlineDLSSGBlueprint.StreamlineLibraryDLSSG"));
	if (!LibraryClass)
	{
		return false;
	}
	UFunction* Function = LibraryClass->FindFunctionByName(TEXT("GetDLSSGFrameTiming"));
	UObject* DefaultObject = LibraryClass->GetDefaultObject();
	if (!Function || !DefaultObject)
	{
		return false;
	}

	struct FFrameTimingParams
	{
		float FrameRateInHertz = 0.0f;
		int32 FramesPresented = 0;
	};

	FFrameTimingParams Params;
	DefaultObject->ProcessEvent(Function, &Params);
	OutFrameRateInHertz = Params.FrameRateInHertz;
	OutFramesPresented = Params.FramesPresented;
	return true;
}

bool SPBRDlssTab::TryCallDlssGSetMode(uint8 ModeValue) const
{
	UClass* LibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/StreamlineDLSSGBlueprint.StreamlineLibraryDLSSG"));
	if (!LibraryClass)
	{
		return false;
	}
	UFunction* Function = LibraryClass->FindFunctionByName(TEXT("SetDLSSGMode"));
	UObject* DefaultObject = LibraryClass->GetDefaultObject();
	if (!Function || !DefaultObject)
	{
		return false;
	}

	struct FSetDlssGModeParams
	{
		uint8 DLSSGMode = 0;
	};

	FSetDlssGModeParams Params;
	Params.DLSSGMode = ModeValue;
	DefaultObject->ProcessEvent(Function, &Params);
	return true;
}

bool SPBRDlssTab::TryReadLoadedBool(const TCHAR* ClassPath, const TCHAR* PropertyName, bool& OutValue) const
{
	if (UClass* Class = FindObject<UClass>(nullptr, ClassPath))
	{
		if (UObject* DefaultObject = Class->GetDefaultObject())
		{
			if (FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Class, PropertyName))
			{
				OutValue = BoolProperty->GetPropertyValue_InContainer(DefaultObject);
				return true;
			}
		}
	}
	return false;
}

bool SPBRDlssTab::TryReadLoadedEnumByte(const TCHAR* ClassPath, const TCHAR* PropertyName, uint8& OutValue) const
{
	if (UClass* Class = FindObject<UClass>(nullptr, ClassPath))
	{
		if (UObject* DefaultObject = Class->GetDefaultObject())
		{
			if (FEnumProperty* EnumProperty = FindFProperty<FEnumProperty>(Class, PropertyName))
			{
				OutValue = static_cast<uint8>(EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(DefaultObject)));
				return true;
			}
			if (FByteProperty* ByteProperty = FindFProperty<FByteProperty>(Class, PropertyName))
			{
				OutValue = ByteProperty->GetPropertyValue_InContainer(DefaultObject);
				return true;
			}
		}
	}
	return false;
}

bool SPBRDlssTab::TryGetConsoleInt(const TCHAR* CVarName, int32& OutValue) const
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName))
	{
		OutValue = CVar->GetInt();
		return true;
	}
	return false;
}

bool SPBRDlssTab::TryGetConsoleFloat(const TCHAR* CVarName, float& OutValue) const
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName))
	{
		OutValue = CVar->GetFloat();
		return true;
	}
	return false;
}

FText SPBRDlssTab::GetGpuStatusText() const
{
	if (!SystemInfo.bHasNvidiaGpu)
	{
		return FText::FromString(SystemInfo.Message);
	}
	return FText::FromString(FString::Printf(TEXT("%s - %s"), *SystemInfo.GpuName, *SystemInfo.Message));
}

FText SPBRDlssTab::GetDriverStatusText() const
{
	return SystemInfo.DriverVersion.IsEmpty()
		? LOCTEXT("DriverUnknown", "未知")
		: FText::FromString(SystemInfo.DriverVersion);
}

FText SPBRDlssTab::GetPluginStatusText() const
{
	if (!bDlssAvailable)
	{
		return LOCTEXT("PluginMissing", "未安装到当前引擎或项目。");
	}
	if (bDlssEnabled)
	{
		return FText::FromString(TEXT("已启用：") + DlssPluginLocation);
	}
	return FText::FromString(TEXT("已找到但当前会话未启用：") + DlssPluginLocation);
}

FText SPBRDlssTab::GetProjectStatusText() const
{
	return FText::FromString(FPaths::GetProjectFilePath());
}

FText SPBRDlssTab::GetDlssSupportStatusText() const
{
	if (!bDlssEnabled)
	{
		return LOCTEXT("DlssSupportPluginNotLoaded", "DLSS 插件当前会话未加载，重启 UE 后才能查询硬件支持。");
	}
	if (!RuntimeInfo.bDlssBlueprintLibraryLoaded)
	{
		return LOCTEXT("DlssSupportLibraryMissing", "DLSS Blueprint 库未加载，无法调用官方支持检测。");
	}
	if (!RuntimeInfo.bDlssSrSupportKnown)
	{
		return LOCTEXT("DlssSupportUnknown", "无法读取官方 Is DLSS-SR Supported 状态。");
	}
	return RuntimeInfo.bDlssSrSupported
		? LOCTEXT("DlssSupportYes", "支持。RTX、驱动和 RHI 已通过官方 DLSS-SR 检测。")
		: LOCTEXT("DlssSupportNo", "不支持。请检查 RTX 显卡、驱动、D3D12/Vulkan 或是否有 RenderDoc 等捕获工具。");
}

FText SPBRDlssTab::GetDlssSessionStatusText() const
{
	const FString DlssEnable = RuntimeInfo.DlssEnableCVar >= 0 ? FString::FromInt(RuntimeInfo.DlssEnableCVar) : TEXT("未注册");
	const FString TemporalUpscaler = RuntimeInfo.TemporalAAUpscalerCVar >= 0 ? FString::FromInt(RuntimeInfo.TemporalAAUpscalerCVar) : TEXT("未注册");
	const FString TemporalUpsampling = RuntimeInfo.TemporalAAUpsamplingCVar >= 0 ? FString::FromInt(RuntimeInfo.TemporalAAUpsamplingCVar) : TEXT("未注册");
	const FString ScreenPercentage = RuntimeInfo.ScreenPercentageCVar >= 0.0f ? FString::Printf(TEXT("%.1f"), RuntimeInfo.ScreenPercentageCVar) : TEXT("未注册");

	if (!RuntimeInfo.bDlssSrEnabledKnown)
	{
		return FText::FromString(FString::Printf(TEXT("未确认。r.NGX.DLSS.Enable=%s, r.TemporalAA.Upscaler=%s, r.TemporalAA.Upsampling=%s, r.ScreenPercentage=%s。"), *DlssEnable, *TemporalUpscaler, *TemporalUpsampling, *ScreenPercentage));
	}

	const TCHAR* EnabledText = RuntimeInfo.bDlssSrEnabled ? TEXT("官方库显示已启用") : TEXT("官方库显示未启用");
	return FText::FromString(FString::Printf(TEXT("%s。r.NGX.DLSS.Enable=%s, r.TemporalAA.Upscaler=%s, r.TemporalAA.Upsampling=%s, r.ScreenPercentage=%s。"), EnabledText, *DlssEnable, *TemporalUpscaler, *TemporalUpsampling, *ScreenPercentage));
}

FText SPBRDlssTab::GetViewportStatusText() const
{
	const FString EditorOverride = RuntimeInfo.UserEditorViewportOverride.IsEmpty() ? TEXT("UseProjectSettings/未写入") : RuntimeInfo.UserEditorViewportOverride;
	const FString PieOverride = RuntimeInfo.UserPieViewportOverride.IsEmpty() ? TEXT("UseProjectSettings/未写入") : RuntimeInfo.UserPieViewportOverride;
	return FText::FromString(FString::Printf(
		TEXT("项目 Editor=%s, PIE=%s；用户覆盖 Editor=%s, PIE=%s。Selected Viewport 仍可能受编辑器 Screen Percentage 覆盖，PIE/Standalone 更适合验证。"),
		RuntimeInfo.bProjectEditorViewportEnabled ? TEXT("允许") : TEXT("未允许"),
		RuntimeInfo.bProjectPieViewportEnabled ? TEXT("允许") : TEXT("未允许"),
		*EditorOverride,
		*PieOverride));
}

FText SPBRDlssTab::GetMrqStatusText() const
{
	return RuntimeInfo.bDlssMoviePipelineEnabled
		? LOCTEXT("MrqEnabled", "DLSSMoviePipelineSupport 已加载。MRQ 需要在任务配置里加入 DLSS/DLAA Setting，选择质量模式后才会用于渲染。")
		: LOCTEXT("MrqDisabled", "DLSSMoviePipelineSupport 未加载。Movie Render Queue 无法显示 NVIDIA DLSS/DLAA Setting。");
}

FText SPBRDlssTab::GetFrameGenerationStatusText() const
{
	if (!RuntimeInfo.bStreamlineEnabled)
	{
		return LOCTEXT("StreamlineMissing", "StreamlineCore 未加载，Frame Generation/Reflex 路径不可用。");
	}
	if (!RuntimeInfo.bStreamlineDlssGEnabled)
	{
		return LOCTEXT("DlssGMissing", "Streamline 已加载，但 StreamlineDLSSG 未加载。需要启用 DLSS Frame Generation 插件。");
	}
	const FString Support = RuntimeInfo.bDlssGSupportKnown ? (RuntimeInfo.bDlssGSupported ? TEXT("Supported") : TEXT("Not Supported")) : TEXT("未确认");
	const FString Mode = RuntimeInfo.DlssGEnableCVar >= 0 ? FString::FromInt(RuntimeInfo.DlssGEnableCVar) : TEXT("未注册");
	const FString Frames = RuntimeInfo.DlssGFramesToGenerateCVar >= 0 ? FString::FromInt(RuntimeInfo.DlssGFramesToGenerateCVar) : TEXT("未注册");
	const FString PieOverride = RuntimeInfo.UserDlssFGPieOverride.IsEmpty() ? TEXT("UseProjectSettings/未写入") : RuntimeInfo.UserDlssFGPieOverride;
	return FText::FromString(FString::Printf(
		TEXT("DLSS-FG %s。r.Streamline.DLSSG.Enable=%s, FramesToGenerate=%s；New Editor Window PIE=%s，用户覆盖=%s。普通 Editor Viewport 不支持 FG。"),
		*Support,
		*Mode,
		*Frames,
		RuntimeInfo.bProjectDlssFGPieEnabled ? TEXT("允许") : TEXT("未允许"),
		*PieOverride));
}

FText SPBRDlssTab::GetFrameTimingStatusText() const
{
	if (!RuntimeInfo.bDlssGFrameTimingKnown)
	{
		return LOCTEXT("FrameTimingUnknown", "未读取到 NVIDIA FG Timing。需要 StreamlineDLSSGBlueprint 已加载。");
	}
	if (RuntimeInfo.DlssGFrameRateInHertz <= 0.01f && RuntimeInfo.DlssGFramesPresented <= 0)
	{
		return LOCTEXT("FrameTimingIdle", "0 FPS / 0 Presented。当前可能没有运行 New Editor Window PIE、Standalone 或打包游戏。");
	}
	return FText::FromString(FString::Printf(
		TEXT("%.1f FPS，Presented Frames: %d。这个数值来自 NVIDIA GetDLSSGFrameTiming，不依赖 UE 内置 FPS。"),
		RuntimeInfo.DlssGFrameRateInHertz,
		RuntimeInfo.DlssGFramesPresented));
}

FSlateColor SPBRDlssTab::GetGpuStatusColor() const
{
	if (SystemInfo.bLikelyRtx)
	{
		return FSlateColor(PBRGood());
	}
	return SystemInfo.bHasNvidiaGpu ? FSlateColor(PBRWarn()) : FSlateColor(PBRBad());
}

FSlateColor SPBRDlssTab::GetPluginStatusColor() const
{
	return bDlssEnabled ? FSlateColor(PBRGood()) : (bDlssAvailable ? FSlateColor(PBRWarn()) : FSlateColor(PBRBad()));
}

FSlateColor SPBRDlssTab::GetDlssSupportStatusColor() const
{
	return (RuntimeInfo.bDlssSrSupportKnown && RuntimeInfo.bDlssSrSupported) ? FSlateColor(PBRGood()) : FSlateColor(PBRWarn());
}

FSlateColor SPBRDlssTab::GetDlssSessionStatusColor() const
{
	const bool bLooksEnabled = RuntimeInfo.bDlssSrEnabled
		|| (RuntimeInfo.DlssEnableCVar == 1 && RuntimeInfo.TemporalAAUpscalerCVar == 1 && RuntimeInfo.ScreenPercentageCVar > 0.0f && RuntimeInfo.ScreenPercentageCVar <= 100.0f);
	return bLooksEnabled ? FSlateColor(PBRGood()) : FSlateColor(PBRWarn());
}

FSlateColor SPBRDlssTab::GetViewportStatusColor() const
{
	const bool bEditorAllowed = RuntimeInfo.bProjectEditorViewportEnabled || RuntimeInfo.UserEditorViewportOverride == TEXT("Enabled");
	const bool bPieAllowed = RuntimeInfo.bProjectPieViewportEnabled || RuntimeInfo.UserPieViewportOverride == TEXT("Enabled");
	return (bEditorAllowed && bPieAllowed) ? FSlateColor(PBRGood()) : FSlateColor(PBRWarn());
}

FSlateColor SPBRDlssTab::GetMrqStatusColor() const
{
	return RuntimeInfo.bDlssMoviePipelineEnabled ? FSlateColor(PBRGood()) : FSlateColor(PBRWarn());
}

FSlateColor SPBRDlssTab::GetFrameGenerationStatusColor() const
{
	const bool bLooksEnabled = RuntimeInfo.bStreamlineEnabled
		&& RuntimeInfo.bStreamlineDlssGEnabled
		&& RuntimeInfo.bDlssGSupported
		&& RuntimeInfo.DlssGEnableCVar > 0;
	return bLooksEnabled ? FSlateColor(PBRGood()) : FSlateColor(PBRWarn());
}

FSlateColor SPBRDlssTab::GetFrameTimingStatusColor() const
{
	return (RuntimeInfo.bDlssGFrameTimingKnown && RuntimeInfo.DlssGFrameRateInHertz > 0.01f) ? FSlateColor(PBRGood()) : FSlateColor(PBRWarn());
}

FReply SPBRDlssTab::OnSetDlssGOffClicked()
{
	FText Error;
	if (!ApplyDlssGMode(0, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("DlssGOffApplied", "已关闭 DLSS Frame Generation。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnSetDlssGAutoClicked()
{
	FText Error;
	if (!ApplyDlssGMode(251, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("DlssGAutoApplied", "已设置 DLSS Frame Generation 自动模式。请用 New Editor Window PIE / Standalone 验证。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnSetDlssG2XClicked()
{
	FText Error;
	if (!ApplyDlssGMode(17, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("DlssG2XApplied", "已设置 DLSS Frame Generation 2X。请用 New Editor Window PIE / Standalone 验证。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnSetDlssG3XClicked()
{
	FText Error;
	if (!ApplyDlssGMode(23, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("DlssG3XApplied", "已设置 DLSS Frame Generation 3X。需要硬件支持，请用外部帧率工具确认。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnSetDlssG4XClicked()
{
	FText Error;
	if (!ApplyDlssGMode(31, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("DlssG4XApplied", "已设置 DLSS Frame Generation 4X。需要硬件支持，请用外部帧率工具确认。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnRefreshClicked()
{
	RefreshStatus();
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("Refreshed", "状态已刷新。"));
	}
	return FReply::Handled();
}

FReply SPBRDlssTab::OnOpenNvidiaDownloadClicked()
{
	FPlatformProcess::LaunchURL(NvidiaDlssDownloadUrl, nullptr, nullptr);
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("OpenedDownload", "已打开 NVIDIA 官方 DLSS Unreal Engine Plugin 下载页面。"));
	}
	return FReply::Handled();
}

FReply SPBRDlssTab::OnOpenProjectPluginsClicked()
{
	const FString PluginsDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins"));
	IFileManager::Get().MakeDirectory(*PluginsDir, true);
	FPlatformProcess::ExploreFolder(*PluginsDir);
	return FReply::Handled();
}

FReply SPBRDlssTab::OnAutoDownloadInstallClicked()
{
	if (bDownloadActive)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("DownloadAlreadyRunning", "DLSS 插件正在下载中。"));
		}
		return FReply::Handled();
	}

	StartDlssDownload();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnPauseDlssDownloadClicked()
{
	if (bDownloadActive && ActiveDownloadRequest.IsValid())
	{
		bDownloadPauseRequested = true;
		ActiveDownloadRequest->CancelRequest();
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("PauseRequested", "正在暂停下载..."));
		}
	}
	return FReply::Handled();
}

FReply SPBRDlssTab::OnResumeDlssDownloadClicked()
{
	if (bDownloadPaused && !bDownloadActive)
	{
		bDownloadPaused = false;
		bDownloadCancelRequested = false;
		bDownloadPauseRequested = false;
		bDownloadActive = true;
		DownloadStartSeconds = FPlatformTime::Seconds();
		LastProgressSeconds = DownloadStartSeconds;
		LastProgressTotalBytes = DownloadedBytes;
		RequestNextDlssChunk();
	}
	return FReply::Handled();
}

FReply SPBRDlssTab::OnCancelDlssDownloadClicked()
{
	if (bDownloadActive && ActiveDownloadRequest.IsValid())
	{
		bDownloadCancelRequested = true;
		ActiveDownloadRequest->CancelRequest();
	}
	else if (bDownloadPaused)
	{
		bDownloadPaused = false;
		bDownloadCancelRequested = true;
		bDownloadActive = false;
		UpdateDownloadStatusText();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("CancelRequested", "已取消 DLSS 下载。临时文件保留，可再次点击自动安装继续。"));
	}
	return FReply::Handled();
}

FReply SPBRDlssTab::OnInstallFromFolderClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	void* ParentWindowHandle = nullptr;
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid())
	{
		ParentWindowHandle = ParentWindow->GetNativeWindow()->GetOSWindowHandle();
	}

	FString SelectedDirectory;
	if (!DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, TEXT("选择已解压的 NVIDIA DLSS 插件目录"), FPaths::ProjectDir(), SelectedDirectory))
	{
		return FReply::Handled();
	}

	FString PluginDirectory;
	FText Error;
	if (!ResolveDlssPluginDirectory(SelectedDirectory, PluginDirectory, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}

	const FString DestinationDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins"), FPaths::GetCleanFilename(PluginDirectory));
	if (FPaths::DirectoryExists(DestinationDirectory))
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			LOCTEXT("OverwriteConfirm", "项目里已经存在：\n{0}\n\n是否覆盖复制？"),
			FText::FromString(DestinationDirectory)));
		if (Result != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}
		IFileManager::Get().DeleteDirectory(*DestinationDirectory, false, true);
	}

	if (!CopyDirectoryTree(PluginDirectory, DestinationDirectory, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}

	const TArray<FString> PluginNames = FindPluginNamesInDirectory(DestinationDirectory);
	if (!EnableProjectPlugins(PluginNames, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
	}

	RefreshStatus();
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(LOCTEXT("Installed", "已复制 DLSS 插件到项目：{0}\n请重启 Unreal Editor。"), FText::FromString(DestinationDirectory)));
	}
	return FReply::Handled();
}

FReply SPBRDlssTab::OnEnableProjectPluginsClicked()
{
	TArray<FString> PluginNames;
	const FString ProjectPluginsDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins"));
	PluginNames = FindPluginNamesInDirectory(ProjectPluginsDir);
	PluginNames.RemoveAll([](const FString& Name)
	{
		return !IsNvidiaRuntimePluginName(Name);
	});

	if (PluginNames.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoPluginsToEnable", "当前项目 Plugins 目录中没有找到 DLSS/Streamline 相关插件。"));
		return FReply::Handled();
	}

	FText Error;
	if (!EnableProjectPlugins(PluginNames, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("Enabled", "已写入 .uproject。请重启 Unreal Editor，让 DLSS 插件生效。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnApplyDlssQualityClicked()
{
	FText Error;
	if (!ApplyDlssProjectDefaults(true, 77, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("AppliedQuality", "已强制启用 DLSS 质量模式，并允许 Editor/PIE 视口使用。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnApplyDlssBalancedClicked()
{
	FText Error;
	if (!ApplyDlssProjectDefaults(true, 67, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("AppliedBalanced", "已强制启用 DLSS 平衡模式，并允许 Editor/PIE 视口使用。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnApplyDlssPerformanceClicked()
{
	FText Error;
	if (!ApplyDlssProjectDefaults(true, 58, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("AppliedPerformance", "已强制启用 DLSS 性能模式，并允许 Editor/PIE 视口使用。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

FReply SPBRDlssTab::OnDisableDlssDefaultClicked()
{
	FText Error;
	if (!ApplyDlssProjectDefaults(false, 100, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("DisabledDefault", "已关闭项目默认 DLSS 配置。"));
	}
	RefreshStatus();
	return FReply::Handled();
}

void SPBRDlssTab::StartDlssDownload()
{
	ResetDlssDownloadState();

	DlssDownloadWorkRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("PBRStudio"), TEXT("DLSS")));
	DlssDownloadZipPath = FPaths::Combine(DlssDownloadWorkRoot, TEXT("NVIDIA_DLSS_UE57.zip"));
	DlssDownloadExtractDirectory = FPaths::Combine(DlssDownloadWorkRoot, TEXT("Extracted"));
	IFileManager::Get().MakeDirectory(*DlssDownloadWorkRoot, true);

	if (FPaths::FileExists(DlssDownloadZipPath))
	{
		DownloadedBytes = IFileManager::Get().FileSize(*DlssDownloadZipPath);
	}

	bDownloadActive = true;
	DownloadStartSeconds = FPlatformTime::Seconds();
	LastProgressSeconds = DownloadStartSeconds;
	LastProgressTotalBytes = DownloadedBytes;
	StartDlssHeadRequest();
	UpdateDownloadStatusText();
}

void SPBRDlssTab::StartDlssHeadRequest()
{
	ActiveDownloadRequest = FHttpModule::Get().CreateRequest();
	ActiveDownloadRequest->SetURL(NvidiaDlss57PluginZipUrl);
	ActiveDownloadRequest->SetVerb(TEXT("HEAD"));
	ActiveDownloadRequest->SetHeader(TEXT("User-Agent"), TEXT("PBRStudio-DLSS-Installer"));
	ActiveDownloadRequest->OnProcessRequestComplete().BindSP(this, &SPBRDlssTab::OnDlssHeadRequestComplete);
	ActiveDownloadRequest->ProcessRequest();
}

void SPBRDlssTab::OnDlssHeadRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
	ActiveDownloadRequest.Reset();
	if (bDownloadCancelRequested)
	{
		bDownloadActive = false;
		UpdateDownloadStatusText();
		return;
	}
	if (!bSucceeded || !Response.IsValid() || !EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		FailDlssDownload(FText::FromString(TEXT("无法连接 NVIDIA DLSS 下载地址，HTTP 状态：") + (Response.IsValid() ? FString::FromInt(Response->GetResponseCode()) : TEXT("无响应"))));
		return;
	}

	DownloadTotalBytes = static_cast<int64>(Response->GetContentLength());
	if (DownloadTotalBytes <= 0)
	{
		const FString ContentLength = Response->GetHeader(TEXT("Content-Length"));
		LexFromString(DownloadTotalBytes, *ContentLength);
	}
	if (DownloadTotalBytes <= 0)
	{
		FailDlssDownload(LOCTEXT("NoDownloadSize", "无法获取 DLSS 插件包大小，下载已停止。"));
		return;
	}

	if (DownloadedBytes > DownloadTotalBytes)
	{
		IFileManager::Get().Delete(*DlssDownloadZipPath);
		DownloadedBytes = 0;
	}
	if (DownloadedBytes == DownloadTotalBytes)
	{
		FinishDlssDownloadAndInstall();
		return;
	}

	RequestNextDlssChunk();
}

void SPBRDlssTab::RequestNextDlssChunk()
{
	if (bDownloadCancelRequested)
	{
		bDownloadActive = false;
		UpdateDownloadStatusText();
		return;
	}
	if (bDownloadPauseRequested)
	{
		bDownloadActive = false;
		bDownloadPaused = true;
		bDownloadPauseRequested = false;
		UpdateDownloadStatusText();
		return;
	}
	if (DownloadTotalBytes > 0 && DownloadedBytes >= DownloadTotalBytes)
	{
		FinishDlssDownloadAndInstall();
		return;
	}

	CurrentChunkStartBytes = DownloadedBytes;
	CurrentChunkReceivedBytes = 0;
	const int64 ChunkEndBytes = FMath::Min(CurrentChunkStartBytes + DlssDownloadChunkSize - 1, DownloadTotalBytes - 1);

	ActiveDownloadRequest = FHttpModule::Get().CreateRequest();
	ActiveDownloadRequest->SetURL(NvidiaDlss57PluginZipUrl);
	ActiveDownloadRequest->SetVerb(TEXT("GET"));
	ActiveDownloadRequest->SetHeader(TEXT("User-Agent"), TEXT("PBRStudio-DLSS-Installer"));
	ActiveDownloadRequest->SetHeader(TEXT("Range"), FString::Printf(TEXT("bytes=%lld-%lld"), CurrentChunkStartBytes, ChunkEndBytes));
	ActiveDownloadRequest->OnRequestProgress64().BindSP(this, &SPBRDlssTab::OnDlssChunkProgress);
	ActiveDownloadRequest->OnProcessRequestComplete().BindSP(this, &SPBRDlssTab::OnDlssChunkRequestComplete);
	ActiveDownloadRequest->ProcessRequest();
	UpdateDownloadStatusText();
}

void SPBRDlssTab::OnDlssChunkProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
	CurrentChunkReceivedBytes = BytesReceived;
	const double Now = FPlatformTime::Seconds();
	const double DeltaSeconds = Now - LastProgressSeconds;
	const int64 CurrentTotalBytes = CurrentChunkStartBytes + static_cast<int64>(CurrentChunkReceivedBytes);
	if (DeltaSeconds >= 0.25)
	{
		const int64 DeltaBytes = CurrentTotalBytes - LastProgressTotalBytes;
		DownloadSpeedBytesPerSecond = DeltaSeconds > 0.0 ? static_cast<double>(DeltaBytes) / DeltaSeconds : 0.0;
		LastProgressSeconds = Now;
		LastProgressTotalBytes = CurrentTotalBytes;
	}
	UpdateDownloadStatusText();
}

void SPBRDlssTab::OnDlssChunkRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
	ActiveDownloadRequest.Reset();

	if (bDownloadCancelRequested)
	{
		bDownloadActive = false;
		UpdateDownloadStatusText();
		return;
	}
	if (bDownloadPauseRequested)
	{
		bDownloadActive = false;
		bDownloadPaused = true;
		bDownloadPauseRequested = false;
		UpdateDownloadStatusText();
		return;
	}
	if (!bSucceeded || !Response.IsValid())
	{
		FailDlssDownload(LOCTEXT("ChunkFailed", "DLSS 插件包下载失败，网络请求没有返回有效响应。"));
		return;
	}

	const int32 ResponseCode = Response->GetResponseCode();
	if (ResponseCode != EHttpResponseCodes::PartialContent)
	{
		FailDlssDownload(FText::FromString(TEXT("服务器没有按分段下载返回数据，HTTP 状态：") + FString::FromInt(ResponseCode)));
		return;
	}

	const TArray<uint8>& Content = Response->GetContent();
	if (Content.IsEmpty())
	{
		FailDlssDownload(LOCTEXT("ChunkEmpty", "DLSS 插件包下载失败，当前分段为空。"));
		return;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenWrite(*DlssDownloadZipPath, true, true));
	if (!FileHandle.IsValid() || !FileHandle->Write(Content.GetData(), Content.Num()))
	{
		FailDlssDownload(FText::FromString(TEXT("无法写入下载文件：") + DlssDownloadZipPath));
		return;
	}

	DownloadedBytes += Content.Num();
	CurrentChunkReceivedBytes = 0;
	UpdateDownloadStatusText();
	RequestNextDlssChunk();
}

void SPBRDlssTab::FinishDlssDownloadAndInstall()
{
	bDownloadActive = false;
	bDownloadPaused = false;
	DownloadSpeedBytesPerSecond = 0.0;
	UpdateDownloadStatusText();

	FText Error;
	FString ExtractDirectory;
	if (!DownloadAndExtractOfficialDlss(ExtractDirectory, Error))
	{
		FailDlssDownload(Error);
		return;
	}

	TArray<FString> PluginFiles;
	const FString MainPluginRoot = FPaths::Combine(ExtractDirectory, TEXT("Plugins"));
	IFileManager::Get().FindFilesRecursive(PluginFiles, *MainPluginRoot, TEXT("*.uplugin"), true, false);

	TArray<FString> SourcePluginDirectories;
	TArray<FString> PluginNames;
	for (const FString& PluginFile : PluginFiles)
	{
		const FString PluginName = FPaths::GetBaseFilename(PluginFile);
		if (IsSafeNvidiaPluginFilePath(PluginFile))
		{
			SourcePluginDirectories.AddUnique(FPaths::GetPath(PluginFile));
			PluginNames.AddUnique(PluginName);
		}
	}

	if (SourcePluginDirectories.IsEmpty())
	{
		FailDlssDownload(LOCTEXT("AutoInstallNoPlugin", "下载包已解压，但没有找到 DLSS/Streamline/Reflex/NIS 相关 .uplugin。请确认 NVIDIA 下载链接返回的是 UE 插件 zip。"));
		return;
	}

	const FString ProjectPluginsDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins"));
	IFileManager::Get().MakeDirectory(*ProjectPluginsDir, true);
	for (const FString& SourceDirectory : SourcePluginDirectories)
	{
		const FString DestinationDirectory = FPaths::Combine(ProjectPluginsDir, FPaths::GetCleanFilename(SourceDirectory));
		if (FPaths::DirectoryExists(DestinationDirectory))
		{
			IFileManager::Get().DeleteDirectory(*DestinationDirectory, false, true);
		}
		if (!CopyDirectoryTree(SourceDirectory, DestinationDirectory, Error))
		{
			FailDlssDownload(Error);
			return;
		}
	}

	if (!EnableProjectPlugins(PluginNames, Error))
	{
		FailDlssDownload(Error);
		return;
	}

	RefreshStatus();
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(
			LOCTEXT("AutoInstalled", "已安装并启用 {0} 个 NVIDIA 插件。请重启 Unreal Editor 后使用 DLSS。"),
			FText::AsNumber(PluginNames.Num())));
	}
	FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("AutoInstalledDialog", "DLSS 插件已复制到当前项目并写入 .uproject。\n\n请重启 Unreal Editor，让插件完成加载。"));
}

void SPBRDlssTab::FailDlssDownload(const FText& Error)
{
	bDownloadActive = false;
	bDownloadPaused = false;
	bDownloadPauseRequested = false;
	bDownloadCancelRequested = false;
	ActiveDownloadRequest.Reset();
	if (StatusText.IsValid())
	{
		StatusText->SetText(Error);
	}
	UpdateDownloadStatusText();
}

void SPBRDlssTab::UpdateDownloadStatusText()
{
	if (DownloadProgressText.IsValid())
	{
		DownloadProgressText->SetText(GetDownloadProgressText());
	}
	if (DownloadDetailText.IsValid())
	{
		DownloadDetailText->SetText(GetDownloadDetailText());
	}
}

void SPBRDlssTab::ResetDlssDownloadState()
{
	bDownloadPauseRequested = false;
	bDownloadCancelRequested = false;
	bDownloadActive = false;
	bDownloadPaused = false;
	DownloadTotalBytes = 0;
	DownloadedBytes = 0;
	CurrentChunkStartBytes = 0;
	CurrentChunkReceivedBytes = 0;
	DownloadStartSeconds = 0.0;
	LastProgressSeconds = 0.0;
	LastProgressTotalBytes = 0;
	DownloadSpeedBytesPerSecond = 0.0;
	ActiveDownloadRequest.Reset();
}

FString SPBRDlssTab::FormatBytes(int64 Bytes) const
{
	const double Value = static_cast<double>(Bytes);
	if (Bytes >= 1024LL * 1024LL * 1024LL)
	{
		return FString::Printf(TEXT("%.2f GB"), Value / (1024.0 * 1024.0 * 1024.0));
	}
	if (Bytes >= 1024LL * 1024LL)
	{
		return FString::Printf(TEXT("%.1f MB"), Value / (1024.0 * 1024.0));
	}
	if (Bytes >= 1024LL)
	{
		return FString::Printf(TEXT("%.1f KB"), Value / 1024.0);
	}
	return FString::Printf(TEXT("%lld B"), Bytes);
}

FText SPBRDlssTab::GetDownloadProgressText() const
{
	if (bDownloadPaused)
	{
		return LOCTEXT("DownloadPaused", "下载已暂停");
	}
	if (bDownloadActive)
	{
		return LOCTEXT("DownloadActive", "正在下载 NVIDIA DLSS UE 5.7 插件");
	}
	if (DownloadTotalBytes > 0 && DownloadedBytes >= DownloadTotalBytes)
	{
		return LOCTEXT("DownloadComplete", "下载完成");
	}
	return LOCTEXT("DownloadIdle", "下载未开始");
}

FText SPBRDlssTab::GetDownloadDetailText() const
{
	const int64 VisibleDownloadedBytes = DownloadedBytes + static_cast<int64>(CurrentChunkReceivedBytes);
	const FString ProgressPart = DownloadTotalBytes > 0
		? FString::Printf(TEXT("%s / %s"), *FormatBytes(VisibleDownloadedBytes), *FormatBytes(DownloadTotalBytes))
		: FormatBytes(VisibleDownloadedBytes);
	const FString SpeedPart = DownloadSpeedBytesPerSecond > 0.0
		? FString::Printf(TEXT(" · %s/s"), *FormatBytes(static_cast<int64>(DownloadSpeedBytesPerSecond)))
		: FString();
	const FString StatePart = bDownloadPaused ? TEXT(" · 已暂停，可继续") : (bDownloadActive ? TEXT(" · 可暂停/取消") : TEXT(" · 可开始下载"));
	return FText::FromString(ProgressPart + SpeedPart + StatePart);
}

TOptional<float> SPBRDlssTab::GetDownloadProgressPercent() const
{
	if (DownloadTotalBytes <= 0)
	{
		return TOptional<float>(0.0f);
	}
	const int64 VisibleDownloadedBytes = DownloadedBytes + static_cast<int64>(CurrentChunkReceivedBytes);
	return TOptional<float>(FMath::Clamp(static_cast<float>(static_cast<double>(VisibleDownloadedBytes) / static_cast<double>(DownloadTotalBytes)), 0.0f, 1.0f));
}

bool SPBRDlssTab::DownloadAndExtractOfficialDlss(FString& OutExtractDirectory, FText& OutError) const
{
	OutExtractDirectory.Reset();

	const FString WorkRoot = DlssDownloadWorkRoot.IsEmpty()
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("PBRStudio"), TEXT("DLSS")))
		: DlssDownloadWorkRoot;
	const FString ZipPath = DlssDownloadZipPath.IsEmpty()
		? FPaths::Combine(WorkRoot, TEXT("NVIDIA_DLSS_UE57.zip"))
		: DlssDownloadZipPath;
	const FString ExtractDirectory = DlssDownloadExtractDirectory.IsEmpty()
		? FPaths::Combine(WorkRoot, TEXT("Extracted"))
		: DlssDownloadExtractDirectory;

	IFileManager::Get().MakeDirectory(*WorkRoot, true);
	if (!FPaths::FileExists(ZipPath))
	{
		OutError = FText::FromString(TEXT("DLSS 插件包不存在，无法解压：") + ZipPath);
		return false;
	}

	const FString Command = FString::Printf(
		TEXT("$ErrorActionPreference='Stop'; ")
		TEXT("New-Item -ItemType Directory -Force -Path %s | Out-Null; ")
		TEXT("if (Test-Path -LiteralPath %s) { Remove-Item -LiteralPath %s -Recurse -Force; } ")
		TEXT("Expand-Archive -LiteralPath %s -DestinationPath %s -Force; "),
		*PowerShellQuote(WorkRoot),
		*PowerShellQuote(ExtractDirectory),
		*PowerShellQuote(ExtractDirectory),
		*PowerShellQuote(ZipPath),
		*PowerShellQuote(ExtractDirectory));

	FString Stdout;
	FString Stderr;
	int32 ReturnCode = -1;
	if (!RunPowerShellCommand(Command, Stdout, Stderr, ReturnCode) || ReturnCode != 0 || !FPaths::DirectoryExists(ExtractDirectory))
	{
		FString Details = Stderr.IsEmpty() ? Stdout : Stderr;
		if (Details.IsEmpty())
		{
			Details = FString::Printf(TEXT("PowerShell 返回码：%d"), ReturnCode);
		}
		OutError = FText::FromString(TEXT("DLSS 插件包解压失败。\n\n错误信息：\n") + Details + TEXT("\n\n可以删除临时 zip 后重新下载，或使用“选择已下载插件目录”作为兜底。"));
		return false;
	}

	OutExtractDirectory = ExtractDirectory;
	return true;
}

bool SPBRDlssTab::RunPowerShellCommand(const FString& Command, FString& OutStdout, FString& OutStderr, int32& OutReturnCode) const
{
	FString EscapedCommand = Command;
	EscapedCommand.ReplaceInline(TEXT("\""), TEXT("\\\""));
	const FString Args = TEXT("-NoProfile -ExecutionPolicy Bypass -Command \"") + EscapedCommand + TEXT("\"");
	return FPlatformProcess::ExecProcess(TEXT("powershell.exe"), *Args, &OutReturnCode, &OutStdout, &OutStderr);
}

bool SPBRDlssTab::ResolveDlssPluginDirectory(const FString& SelectedDirectory, FString& OutPluginDirectory, FText& OutError) const
{
	OutPluginDirectory.Reset();
	TArray<FString> PluginFiles;
	IFileManager::Get().FindFilesRecursive(PluginFiles, *SelectedDirectory, TEXT("*.uplugin"), true, false);
	for (const FString& PluginFile : PluginFiles)
	{
		if (IsSafeNvidiaPluginFilePath(PluginFile))
		{
			OutPluginDirectory = FPaths::GetPath(PluginFile);
			return true;
		}
	}

	OutError = LOCTEXT("NoDlssPlugin", "选择的目录里没有找到 DLSS 或 Streamline 的 .uplugin 文件。请先解压 NVIDIA DLSS Unreal Engine Plugin。");
	return false;
}

bool SPBRDlssTab::CopyDirectoryTree(const FString& SourceDirectory, const FString& DestinationDirectory, FText& OutError) const
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*SourceDirectory))
	{
		OutError = FText::FromString(TEXT("源目录不存在：") + SourceDirectory);
		return false;
	}
	if (!PlatformFile.CreateDirectoryTree(*DestinationDirectory))
	{
		OutError = FText::FromString(TEXT("无法创建目标目录：") + DestinationDirectory);
		return false;
	}
	if (!PlatformFile.CopyDirectoryTree(*DestinationDirectory, *SourceDirectory, true))
	{
		OutError = FText::FromString(TEXT("复制插件目录失败。"));
		return false;
	}
	return true;
}

bool SPBRDlssTab::EnableProjectPlugins(const TArray<FString>& PluginNames, FText& OutError) const
{
	if (PluginNames.IsEmpty())
	{
		OutError = LOCTEXT("NoPluginNames", "没有可启用的插件名称。");
		return false;
	}

	const FString ProjectFile = FPaths::GetProjectFilePath();
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ProjectFile))
	{
		OutError = FText::FromString(TEXT("无法读取项目文件：") + ProjectFile);
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		OutError = FText::FromString(TEXT("项目文件 JSON 解析失败。"));
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> PluginsArray;
	if (RootObject->HasTypedField<EJson::Array>(TEXT("Plugins")))
	{
		PluginsArray = RootObject->GetArrayField(TEXT("Plugins"));
	}

	for (const FString& PluginName : PluginNames)
	{
		if (PluginName.IsEmpty())
		{
			continue;
		}
		if (TSharedPtr<FJsonObject> ExistingPluginObject = FindJsonPluginEntry(PluginsArray, PluginName))
		{
			ExistingPluginObject->SetBoolField(TEXT("Enabled"), true);
			continue;
		}
		TSharedPtr<FJsonObject> PluginObject = MakeShared<FJsonObject>();
		PluginObject->SetStringField(TEXT("Name"), PluginName);
		PluginObject->SetBoolField(TEXT("Enabled"), true);
		PluginsArray.Add(MakeShared<FJsonValueObject>(PluginObject));
	}
	RootObject->SetArrayField(TEXT("Plugins"), PluginsArray);

	FString OutputText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputText);
	if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
	{
		OutError = FText::FromString(TEXT("项目文件 JSON 写入失败。"));
		return false;
	}
	if (!FFileHelper::SaveStringToFile(OutputText, *ProjectFile))
	{
		OutError = FText::FromString(TEXT("无法保存项目文件：") + ProjectFile);
		return false;
	}
	return true;
}

bool SPBRDlssTab::ApplyDlssProjectDefaults(bool bEnableDlss, int32 ScreenPercentage, FText& OutError) const
{
	const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);

	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.NGX.Enable"), TEXT("1"), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.NGX.DLSS.Enable"), bEnableDlss ? TEXT("1") : TEXT("0"), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.NGX.DLSS.DenoiserMode"), TEXT("0"), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.TemporalAA.Upscaler"), bEnableDlss ? TEXT("1") : TEXT("0"), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.TemporalAA.Upsampling"), bEnableDlss ? TEXT("1") : TEXT("0"), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.ScreenPercentage"), *FString::FromInt(ScreenPercentage), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.Streamline.InitializePlugin"), TEXT("1"), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.Streamline.Load.DLSSG"), bEnableDlss ? TEXT("1") : TEXT("0"), ConfigPath);

	const TCHAR* DlssSettingsSection = TEXT("/Script/DLSS.DLSSSettings");
	GConfig->SetBool(DlssSettingsSection, TEXT("bEnableDLSSD3D12"), true, ConfigPath);
	GConfig->SetBool(DlssSettingsSection, TEXT("bEnableDLSSD3D11"), true, ConfigPath);
	GConfig->SetBool(DlssSettingsSection, TEXT("bEnableDLSSVulkan"), true, ConfigPath);
	GConfig->SetBool(DlssSettingsSection, TEXT("bEnableDLSSInEditorViewports"), bEnableDlss, ConfigPath);
	GConfig->SetBool(DlssSettingsSection, TEXT("bEnableDLSSInPlayInEditorViewports"), bEnableDlss, ConfigPath);
	GConfig->SetBool(DlssSettingsSection, TEXT("bShowDLSSSDebugOnScreenMessages"), true, ConfigPath);

	const FString UserConfigPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), FPlatformProperties::PlatformName(), TEXT("Engine.ini"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(UserConfigPath), true);
	const TCHAR* DlssOverrideSection = TEXT("/Script/DLSS.DLSSOverrideSettings");
	GConfig->SetString(DlssOverrideSection, TEXT("EnableDLSSInEditorViewportsOverride"), bEnableDlss ? TEXT("Enabled") : TEXT("Disabled"), UserConfigPath);
	GConfig->SetString(DlssOverrideSection, TEXT("EnableDLSSInPlayInEditorViewportsOverride"), bEnableDlss ? TEXT("Enabled") : TEXT("Disabled"), UserConfigPath);
	GConfig->SetString(DlssOverrideSection, TEXT("ShowDLSSSDebugOnScreenMessages"), TEXT("Enabled"), UserConfigPath);
	GConfig->Flush(false, ConfigPath);
	GConfig->Flush(false, UserConfigPath);

	if (!FPaths::FileExists(ConfigPath))
	{
		OutError = FText::FromString(TEXT("无法写入项目配置：") + ConfigPath);
		return false;
	}

	auto SetLoadedBool = [](const TCHAR* ClassPath, const TCHAR* PropertyName, bool bValue)
	{
		if (UClass* Class = FindObject<UClass>(nullptr, ClassPath))
		{
			if (UObject* DefaultObject = Class->GetDefaultObject())
			{
				if (FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Class, PropertyName))
				{
					BoolProperty->SetPropertyValue_InContainer(DefaultObject, bValue);
				}
			}
		}
	};

	auto SetLoadedEnumByte = [](const TCHAR* ClassPath, const TCHAR* PropertyName, uint8 Value)
	{
		if (UClass* Class = FindObject<UClass>(nullptr, ClassPath))
		{
			if (UObject* DefaultObject = Class->GetDefaultObject())
			{
				if (FEnumProperty* EnumProperty = FindFProperty<FEnumProperty>(Class, PropertyName))
				{
					EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(DefaultObject), static_cast<int64>(Value));
				}
				else if (FByteProperty* ByteProperty = FindFProperty<FByteProperty>(Class, PropertyName))
				{
					ByteProperty->SetPropertyValue_InContainer(DefaultObject, Value);
				}
			}
		}
	};

	SetLoadedBool(TEXT("/Script/DLSS.DLSSSettings"), TEXT("bEnableDLSSD3D12"), true);
	SetLoadedBool(TEXT("/Script/DLSS.DLSSSettings"), TEXT("bEnableDLSSD3D11"), true);
	SetLoadedBool(TEXT("/Script/DLSS.DLSSSettings"), TEXT("bEnableDLSSVulkan"), true);
	SetLoadedBool(TEXT("/Script/DLSS.DLSSSettings"), TEXT("bEnableDLSSInEditorViewports"), bEnableDlss);
	SetLoadedBool(TEXT("/Script/DLSS.DLSSSettings"), TEXT("bEnableDLSSInPlayInEditorViewports"), bEnableDlss);
	SetLoadedBool(TEXT("/Script/DLSS.DLSSSettings"), TEXT("bShowDLSSSDebugOnScreenMessages"), true);
	SetLoadedEnumByte(TEXT("/Script/DLSS.DLSSOverrideSettings"), TEXT("EnableDLSSInEditorViewportsOverride"), bEnableDlss ? 0 : 1);
	SetLoadedEnumByte(TEXT("/Script/DLSS.DLSSOverrideSettings"), TEXT("EnableDLSSInPlayInEditorViewportsOverride"), bEnableDlss ? 0 : 1);
	SetLoadedEnumByte(TEXT("/Script/DLSS.DLSSOverrideSettings"), TEXT("ShowDLSSSDebugOnScreenMessages"), 0);

	if (IConsoleVariable* NgxEnableCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.Enable")))
	{
		NgxEnableCVar->Set(1, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* DlssEnableCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.Enable")))
	{
		DlssEnableCVar->Set(bEnableDlss ? 1 : 0, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* DlssDenoiserModeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.DenoiserMode")))
	{
		DlssDenoiserModeCVar->Set(0, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* TemporalAAUpscalerCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TemporalAA.Upscaler")))
	{
		TemporalAAUpscalerCVar->Set(bEnableDlss ? 1 : 0, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* TemporalAAUpsamplingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TemporalAA.Upsampling")))
	{
		TemporalAAUpsamplingCVar->Set(bEnableDlss ? 1 : 0, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* ScreenPercentageCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		ScreenPercentageCVar->Set(ScreenPercentage, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* StreamlineInitializeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streamline.InitializePlugin")))
	{
		StreamlineInitializeCVar->Set(1, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* StreamlineDlssGCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streamline.Load.DLSSG")))
	{
		StreamlineDlssGCVar->Set(bEnableDlss ? 1 : 0, ECVF_SetByCommandline);
	}

	return true;
}

bool SPBRDlssTab::ApplyDlssGMode(uint8 ModeValue, FText& OutError) const
{
	const bool bEnableFrameGeneration = ModeValue != 0;
	const int32 DlssGEnable = ModeValue == 0 ? 0 : (ModeValue == 251 ? 2 : (ModeValue == 241 ? 3 : 1));
	int32 FramesToGenerate = 1;
	switch (ModeValue)
	{
	case 23:
		FramesToGenerate = 2;
		break;
	case 31:
		FramesToGenerate = 3;
		break;
	case 37:
		FramesToGenerate = 4;
		break;
	case 41:
		FramesToGenerate = 5;
		break;
	default:
		FramesToGenerate = 1;
		break;
	}

	const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.Streamline.InitializePlugin"), TEXT("1"), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.Streamline.Load.DLSSG"), bEnableFrameGeneration ? TEXT("1") : TEXT("0"), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.Streamline.DLSSG.Enable"), *FString::FromInt(DlssGEnable), ConfigPath);
	GConfig->SetString(TEXT("SystemSettings"), TEXT("r.Streamline.DLSSG.FramesToGenerate"), *FString::FromInt(FramesToGenerate), ConfigPath);

	const TCHAR* StreamlineSettingsSection = TEXT("/Script/StreamlineRHI.StreamlineSettings");
	GConfig->SetBool(StreamlineSettingsSection, TEXT("bEnableStreamlineD3D12"), true, ConfigPath);
	GConfig->SetBool(StreamlineSettingsSection, TEXT("bEnableDLSSFGInPlayInEditorViewports"), bEnableFrameGeneration, ConfigPath);
	GConfig->SetBool(StreamlineSettingsSection, TEXT("bLoadDebugOverlay"), bEnableFrameGeneration, ConfigPath);
	GConfig->SetBool(StreamlineSettingsSection, TEXT("bAllowOTAUpdate"), true, ConfigPath);

	const FString UserConfigPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), FPlatformProperties::PlatformName(), TEXT("Engine.ini"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(UserConfigPath), true);
	const TCHAR* StreamlineOverrideSection = TEXT("/Script/StreamlineRHI.StreamlineOverrideSettings");
	GConfig->SetString(StreamlineOverrideSection, TEXT("EnableDLSSFGInPlayInEditorViewportsOverride"), bEnableFrameGeneration ? TEXT("Enabled") : TEXT("Disabled"), UserConfigPath);
	GConfig->SetString(StreamlineOverrideSection, TEXT("LoadDebugOverlayOverride"), bEnableFrameGeneration ? TEXT("Enabled") : TEXT("Disabled"), UserConfigPath);
	GConfig->Flush(false, ConfigPath);
	GConfig->Flush(false, UserConfigPath);

	if (!FPaths::FileExists(ConfigPath))
	{
		OutError = FText::FromString(TEXT("无法写入项目配置：") + ConfigPath);
		return false;
	}

	auto SetLoadedBool = [](const TCHAR* ClassPath, const TCHAR* PropertyName, bool bValue)
	{
		if (UClass* Class = FindObject<UClass>(nullptr, ClassPath))
		{
			if (UObject* DefaultObject = Class->GetDefaultObject())
			{
				if (FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Class, PropertyName))
				{
					BoolProperty->SetPropertyValue_InContainer(DefaultObject, bValue);
				}
			}
		}
	};

	auto SetLoadedEnumByte = [](const TCHAR* ClassPath, const TCHAR* PropertyName, uint8 Value)
	{
		if (UClass* Class = FindObject<UClass>(nullptr, ClassPath))
		{
			if (UObject* DefaultObject = Class->GetDefaultObject())
			{
				if (FEnumProperty* EnumProperty = FindFProperty<FEnumProperty>(Class, PropertyName))
				{
					EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(DefaultObject), static_cast<int64>(Value));
				}
				else if (FByteProperty* ByteProperty = FindFProperty<FByteProperty>(Class, PropertyName))
				{
					ByteProperty->SetPropertyValue_InContainer(DefaultObject, Value);
				}
			}
		}
	};

	SetLoadedBool(TEXT("/Script/StreamlineRHI.StreamlineSettings"), TEXT("bEnableStreamlineD3D12"), true);
	SetLoadedBool(TEXT("/Script/StreamlineRHI.StreamlineSettings"), TEXT("bEnableDLSSFGInPlayInEditorViewports"), bEnableFrameGeneration);
	SetLoadedBool(TEXT("/Script/StreamlineRHI.StreamlineSettings"), TEXT("bLoadDebugOverlay"), bEnableFrameGeneration);
	SetLoadedEnumByte(TEXT("/Script/StreamlineRHI.StreamlineOverrideSettings"), TEXT("EnableDLSSFGInPlayInEditorViewportsOverride"), bEnableFrameGeneration ? 0 : 1);
	SetLoadedEnumByte(TEXT("/Script/StreamlineRHI.StreamlineOverrideSettings"), TEXT("LoadDebugOverlayOverride"), bEnableFrameGeneration ? 0 : 1);

	if (IConsoleVariable* StreamlineInitializeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streamline.InitializePlugin")))
	{
		StreamlineInitializeCVar->Set(1, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* StreamlineLoadDlssGCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streamline.Load.DLSSG")))
	{
		StreamlineLoadDlssGCVar->Set(bEnableFrameGeneration ? 1 : 0, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* DlssGEnableCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streamline.DLSSG.Enable")))
	{
		DlssGEnableCVar->Set(DlssGEnable, ECVF_SetByCommandline);
	}
	if (IConsoleVariable* FramesToGenerateCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streamline.DLSSG.FramesToGenerate")))
	{
		FramesToGenerateCVar->Set(FramesToGenerate, ECVF_SetByCommandline);
	}

	TryCallDlssGSetMode(ModeValue);
	return true;
}

TArray<FString> SPBRDlssTab::FindPluginNamesInDirectory(const FString& Directory) const
{
	TArray<FString> PluginFiles;
	IFileManager::Get().FindFilesRecursive(PluginFiles, *Directory, TEXT("*.uplugin"), true, false);
	TArray<FString> Names;
	for (const FString& PluginFile : PluginFiles)
	{
		Names.AddUnique(FPaths::GetBaseFilename(PluginFile));
	}
	return Names;
}

#undef LOCTEXT_NAMESPACE
