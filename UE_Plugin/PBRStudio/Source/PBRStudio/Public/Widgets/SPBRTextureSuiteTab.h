#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STileView.h"
#include "Models/PBRMaterialSet.h"

class SPBRTextureSuiteTab : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnCompactModeChanged, bool);

	SLATE_BEGIN_ARGS(SPBRTextureSuiteTab) {}
		SLATE_EVENT(FOnCompactModeChanged, OnCompactModeChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void LoadExternalLibraryFolderAndScan();

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

	// Tree
	TSharedRef<class SHeaderRow> BuildHeaderRow();
	TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<FPBRMaterialSet> Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<class ITableRow> OnGenerateTile(TSharedPtr<FPBRMaterialSet> Item, const TSharedRef<STableViewBase>& Owner);
	void OnSelectionChanged(TSharedPtr<FPBRMaterialSet> Item, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> OnMaterialSetContextMenuOpening();
	void OnRenameMaterialSet();
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
	const FSlateBrush* GetPreviewBrush(const FPBRMaterialSet& Set);
	FVector2D GetPreviewImageSize(const FString& ImagePath) const;
	UMaterialInterface* GetMaterialForApply(TSharedPtr<FPBRMaterialSet>& OutSourceSet) const;
	int32 GetTargetMaterialSlot() const;
	bool IsCompactMode() const { return bCompactMode; }

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

	TArray<TSharedPtr<FPBRMaterialSet>> MaterialSets;
	TArray<FStringOption> MaterialTypeOptions;
	TArray<FStringOption> NormalModeOptions;
	FStringOption SelectedMaterialTypeOption;
	FStringOption SelectedNormalModeOption;
	TMap<FString, TSharedPtr<struct FSlateDynamicImageBrush>> PreviewBrushCache;
	FString SelectedMaterialTypeMode = TEXT("自动");
	FString SelectedNormalMode = TEXT("自动");
	bool bBoxSelectMode = false;
	bool bGridViewMode = false;
	bool bCompactMode = false;
	bool bAutoStandardWhenChannelsUnused = true;
	FOnCompactModeChanged OnCompactModeChanged;
	static const FString ConfigFileName;
	static const FString CacheFileName;
};
