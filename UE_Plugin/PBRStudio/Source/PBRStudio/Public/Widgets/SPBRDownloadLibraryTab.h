#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Views/SListView.h"
#include "Models/PBRDownloadEntry.h"
#include "Models/PBRDownloadSite.h"

class FPBRDownloadManager;
class FPBRHttpServer;

class SPBRDownloadLibraryTab : public SCompoundWidget
{
public:
	DECLARE_DELEGATE(FOnLibrarySentToTextureSuite);
	DECLARE_DELEGATE(FOnLibrarySentToSpecialMaterials);

	SLATE_BEGIN_ARGS(SPBRDownloadLibraryTab) {}
		SLATE_EVENT(FOnLibrarySentToTextureSuite, OnLibrarySentToTextureSuite)
		SLATE_EVENT(FOnLibrarySentToSpecialMaterials, OnLibrarySentToSpecialMaterials)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// Library
	FReply OnBrowseLibrary();
	FReply OnOpenLibrary();
	FReply OnScanLibraryToPBRSuite();
	FReply OnScanLibraryToSpecialMaterials();
	bool HasSupportedArchiveDrag(const FDragDropEvent& DragDropEvent) const;
	void SetCurrentLibraryFolder(const FString& Folder, bool bUpdateText);
	void LoadLibraryHistory();
	void SaveLibraryHistory(const FString& Folder);
	void OnLibraryHistorySelected(TSharedPtr<FString> Option, ESelectInfo::Type SelectInfo);
	TSharedRef<class SWidget> GenerateLibraryHistoryOption(TSharedPtr<FString> Option) const;

	// Sites
	TSharedRef<class SHeaderRow> BuildSiteHeader();
	TSharedRef<class ITableRow> OnGenerateSiteRow(TSharedPtr<FPBRDownloadSite> Item, const TSharedRef<STableViewBase>& Owner);
	void OnAddSite();
	void OnRemoveSite();
	void OnOpenSite();
	void CommitSiteEdit(TSharedPtr<FPBRDownloadSite> Item, const FString& Field, const FText& NewText);
	void AddSiteFromDialog(const FString& URL, const FString& Name, const FString& License, const FString& Note);
	void RefreshSites();

	// HTTP Server
	FReply OnStartServer();
	FReply OnStopServer();
	FText GetServerStatus() const;

	// Download Queue
	TSharedRef<SHeaderRow> BuildQueueHeader();
	TSharedRef<class ITableRow> OnGenerateQueueRow(TSharedPtr<FPBRDownloadEntry> Item, const TSharedRef<STableViewBase>& Owner);
	void OnDownloadSelected();
	void OnDownloadAll();
	void OnClearQueue();
	TSharedPtr<class SWidget> MakeQueueContextMenu();
	void OnRenameSelectedQueueEntry();
	void OnOpenSelectedQueueEntryLocation();
	void OnAddUrlFromClipboard();
	void AddUrlsFromText(const FString& Text, const FString& Source);
	void RefreshQueue();

	using FStringOption = TSharedPtr<FString>;
	TSharedPtr<class SEditableTextBox> LibraryPathBox;
	TSharedPtr<class SComboBox<FStringOption>> LibraryHistoryComboBox;
	TSharedPtr<SSpinBox<int32>> PortSpin;
	TSharedPtr<class STextBlock> ServerStatusText;
	TSharedPtr<SListView<TSharedPtr<FPBRDownloadSite>>> SiteTree;
	TSharedPtr<SListView<TSharedPtr<FPBRDownloadEntry>>> QueueTree;
	TSharedPtr<class SCheckBox> AutoExtractCheck;
	TSharedPtr<class SCheckBox> DeleteNonImageFilesCheck;
	TSharedPtr<class SCheckBox> WatchClipboardCheck;

	TArray<TSharedPtr<FPBRDownloadSite>> SiteRows;
	TArray<TSharedPtr<FPBRDownloadEntry>> QueueRows;
	TSharedPtr<FPBRDownloadManager> DownloadManager;
	TSharedPtr<FPBRHttpServer> HttpServer;
	FString LastClipboardText;
	TArray<FStringOption> LibraryHistoryOptions;
	FOnLibrarySentToTextureSuite OnLibrarySentToTextureSuite;
	FOnLibrarySentToSpecialMaterials OnLibrarySentToSpecialMaterials;
};
