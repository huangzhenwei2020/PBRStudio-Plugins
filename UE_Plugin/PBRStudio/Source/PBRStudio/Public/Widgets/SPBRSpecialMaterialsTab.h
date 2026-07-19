#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STileView.h"

struct FPBRSpecialMaterialItem
{
	FString AssetName;
	FString DisplayName;
	FString Category;
	FString Description;
	FString Status;
	FString SourceFolder;
	FString PreviewPath;
	TMap<FString, FString> SpecialTextures;
	TMap<FString, FString> SpecialTextureRoles;
	bool bChecked = true;
	TSoftObjectPtr<class UMaterialInterface> CreatedMaterial;
};

struct FPBRSpecialTextureChannelItem
{
	FString FilePath;
	FString Role;
};

class SPBRSpecialMaterialsTab : public SCompoundWidget
{
public:
	using FStringOption = TSharedPtr<FString>;
	DECLARE_DELEGATE_OneParam(FOnCompactModeChanged, bool);

	SLATE_BEGIN_ARGS(SPBRSpecialMaterialsTab) {}
		SLATE_EVENT(FOnCompactModeChanged, OnCompactModeChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void LoadExternalLibraryFolderAndScan();
	FReply ToggleCompactModeFromGlobal();
	void SetCompactModeFromGlobal(bool bInCompactMode);
	bool IsCompactMode() const { return bCompactMode; }

private:
	void RebuildItems();
	FReply OnBrowseFolder();
	FReply OnOpenFolder();
	FReply OnScanFolder();
	FReply OnCreateSpecialTemplates();
	FReply OnCreateSelected();
	FReply OnCreateChecked();
	FReply OnCreateAll();
	FReply OnCheckAll();
	FReply OnCheckNone();
	FReply OnShowListView();
	FReply OnShowGridView();
	FReply OnToggleCompactMode();
	FReply OnApplySelectedToSelection();
	FReply OnOpenSelectedLocation();
	FReply OnManualTextureChannels();
	FReply OnManualTextureChannelsForItem(TSharedPtr<FPBRSpecialMaterialItem> Item);
	void CreateMaterials(const FString& Scope);
	void RefreshViews();
	bool HasScannedItems() const;
	EVisibility GetScannedActionVisibility() const;
	EVisibility GetStandardControlsVisibility() const;
	void OnSelectionChanged(TSharedPtr<FPBRSpecialMaterialItem> Item, ESelectInfo::Type SelectInfo);
	TSharedRef<class SWidget> GenerateSpecialTemplateOption(FStringOption Option) const;
	void OnSpecialTemplateSelected(FStringOption Option, ESelectInfo::Type SelectInfo, TSharedPtr<FPBRSpecialMaterialItem> Item);
	FText GetSpecialTemplateText(TSharedPtr<FPBRSpecialMaterialItem> Item) const;
	void ApplyManualSpecialTemplate(TSharedPtr<FPBRSpecialMaterialItem> Item, const FString& AssetName);
	TSharedRef<class SWidget> GenerateTextureRoleOption(FStringOption Option) const;
	TSharedRef<class ITableRow> GenerateManualTextureRow(TSharedPtr<FPBRSpecialTextureChannelItem> TextureItem, const TSharedRef<class STableViewBase>& Owner);
	void OnTextureRoleSelected(FStringOption Option, ESelectInfo::Type SelectInfo, TSharedPtr<FPBRSpecialTextureChannelItem> TextureItem);
	void ApplyManualTextureChannels(TSharedPtr<FPBRSpecialMaterialItem> Item);
	const FSlateBrush* GetManualTextureBrush(const FString& ImagePath);
	TSharedRef<class SWidget> BuildPreviewWidget(TSharedPtr<FPBRSpecialMaterialItem> Item, const FVector2D& Size);
	class UMaterialInterface* GetPreviewFallbackMaterial(TSharedPtr<FPBRSpecialMaterialItem> Item) const;
	const FSlateBrush* GetPreviewBrush(const FPBRSpecialMaterialItem& Item);
	FVector2D GetPreviewImageSize(const FString& ImagePath) const;
	TSharedRef<class SHeaderRow> BuildHeaderRow();
	TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<FPBRSpecialMaterialItem> Item, const TSharedRef<class STableViewBase>& Owner);
	TSharedRef<class ITableRow> OnGenerateTile(TSharedPtr<FPBRSpecialMaterialItem> Item, const TSharedRef<class STableViewBase>& Owner);
	TSharedPtr<class SWidget> MakeContextMenu();
	FReply OnMaterialDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent, TSharedPtr<FPBRSpecialMaterialItem> Item);
	FText GetStatusText() const;
	EVisibility GetListVisibility() const;
	EVisibility GetGridVisibility() const;

	TSharedPtr<class STextBlock> StatusText;
	TSharedPtr<class STextBlock> CountText;
	TSharedPtr<class SEditableTextBox> FolderPathBox;
	TSharedPtr<class SCheckBox> RecursiveCheck;
	TSharedPtr<SListView<TSharedPtr<FPBRSpecialMaterialItem>>> ListView;
	TSharedPtr<STileView<TSharedPtr<FPBRSpecialMaterialItem>>> TileView;
	TArray<TSharedPtr<FPBRSpecialMaterialItem>> Items;
	TArray<FStringOption> SpecialTemplateOptions;
	TArray<FStringOption> TextureRoleOptions;
	TArray<TSharedPtr<FPBRSpecialTextureChannelItem>> ManualTextureRows;
	TArray<FString> LastMessages;
	TMap<FString, TSharedPtr<struct FSlateDynamicImageBrush>> PreviewBrushCache;
	TSharedPtr<class FAssetThumbnailPool> ThumbnailPool;
	TMap<FString, TSharedPtr<class FAssetThumbnail>> MaterialThumbnailCache;
	int32 LastCreatedCount = 0;
	bool bGridViewMode = false;
	bool bCompactMode = false;
	FOnCompactModeChanged OnCompactModeChanged;
};
