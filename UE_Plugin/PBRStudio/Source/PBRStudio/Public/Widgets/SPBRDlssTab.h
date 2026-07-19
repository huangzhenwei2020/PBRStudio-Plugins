#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Widgets/SCompoundWidget.h"

class SProgressBar;
class STextBlock;

class SPBRDlssTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRDlssTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FDlssSystemInfo
	{
		FString GpuName;
		FString DriverVersion;
		FString Message;
		bool bHasNvidiaGpu = false;
		bool bLikelyRtx = false;
	};

	struct FDlssRuntimeInfo
	{
		bool bStreamlineEnabled = false;
		bool bDlssMoviePipelineEnabled = false;
		bool bStreamlineDlssGEnabled = false;
		bool bDlssBlueprintLibraryLoaded = false;
		bool bDlssGBlueprintLibraryLoaded = false;
		bool bDlssSrSupportKnown = false;
		bool bDlssSrSupported = false;
		bool bDlssSrEnabledKnown = false;
		bool bDlssSrEnabled = false;
		bool bDlssGSupportKnown = false;
		bool bDlssGSupported = false;
		bool bDlssGFrameTimingKnown = false;
		float DlssGFrameRateInHertz = 0.0f;
		int32 DlssGFramesPresented = 0;
		int32 DlssEnableCVar = -1;
		int32 DlssGEnableCVar = -1;
		int32 DlssGFramesToGenerateCVar = -1;
		int32 TemporalAAUpscalerCVar = -1;
		int32 TemporalAAUpsamplingCVar = -1;
		float ScreenPercentageCVar = -1.0f;
		bool bProjectEditorViewportEnabled = false;
		bool bProjectPieViewportEnabled = false;
		bool bProjectDlssFGPieEnabled = false;
		FString UserEditorViewportOverride;
		FString UserPieViewportOverride;
		FString UserDlssFGPieOverride;
		bool bProjectConfigHasDlssDefaults = false;
	};

	TSharedRef<SWidget> BuildStatusCard();
	TSharedRef<SWidget> BuildActionCard();
	TSharedRef<SWidget> BuildDownloadPanel();
	TSharedRef<SWidget> BuildRuntimeCard();
	TSharedRef<SWidget> BuildSettingsCard();
	TSharedRef<SWidget> BuildRenderCard();
	TSharedRef<SWidget> BuildHelpCard();
	TSharedRef<SWidget> MakeInfoRow(const FText& Label, const TAttribute<FText>& Value, const TAttribute<FSlateColor>& Color) const;
	TSharedRef<SWidget> MakeActionButton(const FText& Label, const FText& ToolTip, const FName IconName, const FOnClicked& OnClicked) const;

	void RefreshStatus();
	EActiveTimerReturnType RefreshRuntimeStatus(double InCurrentTime, float InDeltaTime);
	FDlssSystemInfo QuerySystemInfo() const;
	FDlssRuntimeInfo QueryRuntimeInfo() const;
	bool TryRunNvidiaSmi(FString& OutStdout, FString& OutStderr) const;
	bool IsDlssPluginAvailable() const;
	bool IsDlssPluginEnabled() const;
	FString GetDlssPluginLocation() const;
	bool IsPluginEnabled(const FString& PluginName) const;
	bool TryCallDlssLibraryBool(const TCHAR* FunctionName, bool& OutValue) const;
	bool TryCallDlssGLibraryBool(const TCHAR* FunctionName, bool& OutValue) const;
	bool TryCallDlssGFrameTiming(float& OutFrameRateInHertz, int32& OutFramesPresented) const;
	bool TryCallDlssGSetMode(uint8 ModeValue) const;
	bool TryReadLoadedBool(const TCHAR* ClassPath, const TCHAR* PropertyName, bool& OutValue) const;
	bool TryReadLoadedEnumByte(const TCHAR* ClassPath, const TCHAR* PropertyName, uint8& OutValue) const;
	bool TryGetConsoleInt(const TCHAR* CVarName, int32& OutValue) const;
	bool TryGetConsoleFloat(const TCHAR* CVarName, float& OutValue) const;
	FText GetGpuStatusText() const;
	FText GetDriverStatusText() const;
	FText GetPluginStatusText() const;
	FText GetProjectStatusText() const;
	FText GetDlssSupportStatusText() const;
	FText GetDlssSessionStatusText() const;
	FText GetViewportStatusText() const;
	FText GetMrqStatusText() const;
	FText GetFrameGenerationStatusText() const;
	FText GetFrameTimingStatusText() const;
	FSlateColor GetGpuStatusColor() const;
	FSlateColor GetPluginStatusColor() const;
	FSlateColor GetDlssSupportStatusColor() const;
	FSlateColor GetDlssSessionStatusColor() const;
	FSlateColor GetViewportStatusColor() const;
	FSlateColor GetMrqStatusColor() const;
	FSlateColor GetFrameGenerationStatusColor() const;
	FSlateColor GetFrameTimingStatusColor() const;

	FReply OnSetDlssGOffClicked();
	FReply OnSetDlssGAutoClicked();
	FReply OnSetDlssG2XClicked();
	FReply OnSetDlssG3XClicked();
	FReply OnSetDlssG4XClicked();
	FReply OnRefreshClicked();
	FReply OnOpenNvidiaDownloadClicked();
	FReply OnOpenProjectPluginsClicked();
	FReply OnAutoDownloadInstallClicked();
	FReply OnPauseDlssDownloadClicked();
	FReply OnResumeDlssDownloadClicked();
	FReply OnCancelDlssDownloadClicked();
	FReply OnInstallFromFolderClicked();
	FReply OnEnableProjectPluginsClicked();
	FReply OnApplyDlssQualityClicked();
	FReply OnApplyDlssBalancedClicked();
	FReply OnApplyDlssPerformanceClicked();
	FReply OnDisableDlssDefaultClicked();

	bool DownloadAndExtractOfficialDlss(FString& OutExtractDirectory, FText& OutError) const;
	bool RunPowerShellCommand(const FString& Command, FString& OutStdout, FString& OutStderr, int32& OutReturnCode) const;
	void StartDlssDownload();
	void StartDlssHeadRequest();
	void OnDlssHeadRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	void RequestNextDlssChunk();
	void OnDlssChunkProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
	void OnDlssChunkRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	void FinishDlssDownloadAndInstall();
	void FailDlssDownload(const FText& Error);
	void UpdateDownloadStatusText();
	void ResetDlssDownloadState();
	FString FormatBytes(int64 Bytes) const;
	FText GetDownloadProgressText() const;
	FText GetDownloadDetailText() const;
	TOptional<float> GetDownloadProgressPercent() const;
	bool ResolveDlssPluginDirectory(const FString& SelectedDirectory, FString& OutPluginDirectory, FText& OutError) const;
	bool CopyDirectoryTree(const FString& SourceDirectory, const FString& DestinationDirectory, FText& OutError) const;
	bool EnableProjectPlugins(const TArray<FString>& PluginNames, FText& OutError) const;
	bool ApplyDlssProjectDefaults(bool bEnableDlss, int32 ScreenPercentage, FText& OutError) const;
	bool ApplyDlssGMode(uint8 ModeValue, FText& OutError) const;
	TArray<FString> FindPluginNamesInDirectory(const FString& Directory) const;

	FDlssSystemInfo SystemInfo;
	FDlssRuntimeInfo RuntimeInfo;
	FString DlssPluginLocation;
	bool bDlssAvailable = false;
	bool bDlssEnabled = false;
	bool bDownloadPauseRequested = false;
	bool bDownloadCancelRequested = false;
	bool bDownloadActive = false;
	bool bDownloadPaused = false;
	int64 DownloadTotalBytes = 0;
	int64 DownloadedBytes = 0;
	int64 CurrentChunkStartBytes = 0;
	uint64 CurrentChunkReceivedBytes = 0;
	double DownloadStartSeconds = 0.0;
	double LastProgressSeconds = 0.0;
	int64 LastProgressTotalBytes = 0;
	double DownloadSpeedBytesPerSecond = 0.0;
	FString DlssDownloadWorkRoot;
	FString DlssDownloadZipPath;
	FString DlssDownloadExtractDirectory;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveDownloadRequest;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> DownloadProgressText;
	TSharedPtr<STextBlock> DownloadDetailText;
	TSharedPtr<SProgressBar> DownloadProgressBar;
};
