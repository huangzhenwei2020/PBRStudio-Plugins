#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STileView.h"
#include "Models/PBRMaterialSet.h"
#include "Models/PBRMaterialTypes.h"

struct FPBRManualTextureChannelItem
{
	FString FilePath;
	FString Channel;
};

class SPBRTextureSuiteTab : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnCompactModeChanged, bool);

	SLATE_BEGIN_ARGS(SPBRTextureSuiteTab) {}
		SLATE_EVENT(FOnCompactModeChanged, OnCompactModeChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void LoadExternalLibraryFolderAndScan();
	FReply ToggleCompactModeFromGlobal();
	void SetCompactModeFromGlobal(bool bInCompactMode);
	bool IsCompactMode() const { return bCompactMode; }

private:
	using FStringOption = TSharedPtr<FString>;

	// Scanning
	FReply OnBrowseFolder();
	FReply OnOpenFolder();
	FReply OnScanFolder();
	void LoadPersistedState();
	void SavePersistedState() const;
	void RefreshMaterialSetList();
	FReply OnCheckAll();
	FReply OnCheckNone();
	FReply OnInvertChecked();
	FReply OnCheckListSelection();
	FReply OnApplySelectedMaterialToSelection();
	FReply OnCreateAllParentMaterials();
	FReply OnOpenCurrentParentMaterial();
	FReply OnOpenCurrentExampleMaterial();
	FReply OnCreateSpecialMaterials();
	FReply OnDeleteUnusedCreatedAssets();
	FReply OnShowListView();
	FReply OnShowGridView();
	FReply OnToggleCompactMode();
	EVisibility GetStandardControlsVisibility() const;
	EVisibility GetListViewVisibility() const;
	EVisibility GetGridViewVisibility() const;
	ECheckBoxState IsBoxSelectModeChecked() const;
	void OnBoxSelectModeChanged(ECheckBoxState NewState);
	void OnCreateMaterials(const FString& Scope);
	void OnManualMapping();
	FReply OnManualMappingForSet(TSharedPtr<FPBRMaterialSet> Set);

	// Tree
	TSharedRef<class SHeaderRow> BuildHeaderRow();
	TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<FPBRMaterialSet> Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<class ITableRow> OnGenerateTile(TSharedPtr<FPBRMaterialSet> Item, const TSharedRef<STableViewBase>& Owner);
	void OnSelectionChanged(TSharedPtr<FPBRMaterialSet> Item, ESelectInfo::Type SelectInfo);
	TSharedPtr<class SWidget> MakeMaterialSetContextMenu();
	void OnRenameSelectedMaterialSet();
	void OnOpenSelectedMaterialSetLocation();
	FReply OnMaterialRowDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent, TSharedPtr<FPBRMaterialSet> Item);
	void OnBoxSelectRange(const FVector2D& ScreenStart, const FVector2D& ScreenEnd);

	// Helpers
	FString ChannelSummary(const TMap<FString, FString>& Channels) const;
	FString SetDisplayIssues(const FPBRMaterialSet& Set) const;
	FString DetectMaterialTypeLabel(const FPBRMaterialSet& Set) const;
	FString GetMaterialTypeDescription(const FString& MaterialTypeMode) const;
	TSharedRef<class SWidget> GenerateMaterialTypeOption(FStringOption Option) const;
	void OnMaterialTypeSelected(FStringOption Option, ESelectInfo::Type SelectInfo);
	FText GetSelectedMaterialTypeText() const;
	TSharedRef<class SWidget> GenerateNormalModeOption(FStringOption Option) const;
	void OnNormalModeSelected(FStringOption Option, ESelectInfo::Type SelectInfo);
	FText GetSelectedNormalModeText() const;
	TSharedRef<class SWidget> GeneratePBRChannelOption(FStringOption Option) const;
	TSharedRef<class ITableRow> GenerateManualTextureRow(TSharedPtr<FPBRManualTextureChannelItem> TextureItem, const TSharedRef<class STableViewBase>& Owner);
	void OnPBRChannelSelected(FStringOption Option, ESelectInfo::Type SelectInfo, TSharedPtr<FPBRManualTextureChannelItem> TextureItem);
	void ApplyManualTextureChannels(TSharedPtr<FPBRMaterialSet> Set);
	const FSlateBrush* GetManualTextureBrush(const FString& ImagePath);
	TSharedRef<class SWidget> BuildPreviewWidget(TSharedPtr<FPBRMaterialSet> Item, const FVector2D& Size);
	class UMaterialInterface* GetPreviewFallbackMaterial(TSharedPtr<FPBRMaterialSet> Item) const;
	const FSlateBrush* GetPreviewBrush(const FPBRMaterialSet& Set);
	FVector2D GetPreviewImageSize(const FString& ImagePath) const;
	UMaterialInterface* GetMaterialForApply(TSharedPtr<FPBRMaterialSet>& OutSourceSet) const;
	int32 GetTargetMaterialSlot() const;
	EPBRMaterialType ResolveCurrentTemplateType() const;
	void SyncBrowserToAsset(const FString& AssetPath) const;

	TSharedPtr<class SEditableTextBox> FolderPathBox;
	TSharedPtr<class SCheckBox> RecursiveCheck;
	TSharedPtr<class SCheckBox> GroupByFolderCheck;
	TSharedPtr<class SComboBox<FStringOption>> MaterialTypeComboBox;
	TSharedPtr<class STextBlock> MaterialTypeHelpText;
	TSharedPtr<class SCheckBox> AutoStandardCheck;
	TSharedPtr<class SEditableTextBox> PrefixBox;
	TSharedPtr<class SEditableTextBox> MaterialSlotBox;
	TSharedPtr<class SComboBox<FStringOption>> NormalModeComboBox;
	TSharedPtr<SListView<TSharedPtr<FPBRMaterialSet>>> TreeView;
	TSharedPtr<STileView<TSharedPtr<FPBRMaterialSet>>> TileView;
	TSharedPtr<class STextBlock> SetCountText;
	TSharedPtr<class STextBlock> CreateProgressText;
	TSharedPtr<class SProgressBar> CreateProgressBar;

	TArray<TSharedPtr<FPBRMaterialSet>> MaterialSets;
	TArray<FStringOption> MaterialTypeOptions;
	TArray<FStringOption> NormalModeOptions;
	TArray<FStringOption> PBRChannelOptions;
	TArray<TSharedPtr<FPBRManualTextureChannelItem>> ManualTextureRows;
	FStringOption SelectedMaterialTypeOption;
	FStringOption SelectedNormalModeOption;
	TMap<FString, TSharedPtr<struct FSlateDynamicImageBrush>> PreviewBrushCache;
	TSharedPtr<class FAssetThumbnailPool> ThumbnailPool;
	TMap<FString, TSharedPtr<class FAssetThumbnail>> MaterialThumbnailCache;
	FString SelectedMaterialTypeMode = TEXT("自动");
	FString SelectedNormalMode = TEXT("自动");
	bool bBoxSelectMode = false;
	bool bGridViewMode = false;
	bool bCompactMode = false;
	bool bAutoStandardWhenChannelsUnused = true;
	float CreateProgress = 0.0f;
	FOnCompactModeChanged OnCompactModeChanged;
	static const FString ConfigFileName;
	static const FString CacheFileName;
};
