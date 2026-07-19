#pragma once

#include "CoreMinimal.h"
#include "MaterialVaultTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Views/STreeView.h"

class SButton;
class SBox;
class STableViewBase;
class FAssetThumbnail;
class FAssetThumbnailPool;
class IAssetRegistry;
class UTexture2D;
class IDetailsView;
struct FAssetData;
struct FScopedSlowTask;
struct FSlateDynamicImageBrush;

struct FMaterialVaultPackItem
{
	FString Id;
	FString DisplayName;
	FString Category;
	FString RelativePath;
	FString AbsolutePath;
	FString ThumbnailPath;
	FString RootObjectPath;
	FString RootAssetClass;
	FString PackDisplayName;
	FString SourcePackId;
	FMaterialVaultItem FamilyItem;
	bool bHasFamilyItem = false;
	bool bIsFamilyEntry = false;
	EMaterialVaultAssetRole EntryRole = EMaterialVaultAssetRole::Other;
	TSharedPtr<FSlateDynamicImageBrush> ThumbnailBrush;
	int64 SizeBytes = 0;
};

struct FPackedMeta
{
	FString RootObjectPath;
	FString RootAssetClass;
	FString Category;
	FString ThumbnailPath;
};

enum class EMaterialVaultInstallMode : uint8
{
	Staging,
	Library
};

enum class EMaterialVaultSourceState : uint8
{
	Project,
	Library,
	Staging,
	External
};

struct FMaterialVaultStagingCleanupStats
{
	int32 ScannedAssets = 0;
	int32 UsedAssets = 0;
	int32 DeletedAssets = 0;
	int32 SkippedAssets = 0;
	int32 DeletedFolders = 0;
};

struct FCategoryTreeItem
{
	FString DisplayName;
	FString FullPath;
	int32 Count = 0;
	TArray<TSharedPtr<FCategoryTreeItem>> Children;
};

class SMaterialVaultWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialVaultWindow) {}
	SLATE_END_ARGS()

	~SMaterialVaultWindow();
	void Construct(const FArguments& InArgs);
	void SetCompactModeFromGlobal(bool bInCompactMode);
	bool IsCompactMode() const { return bCompactMode; }
	static bool GenerateMvpackForItem(const FMaterialVaultItem& Item, FString& OutPackPath, FText& OutError, bool bCleanupStaging = true);
	static bool RebuildPackIndex(const FString& VaultRoot, FString& OutIndexPath, int32& OutPackCount, FText& OutError);
	static bool InstallMvpack(const FString& PackPath, FString& OutInstallRoot, FText& OutError, EMaterialVaultInstallMode Mode = EMaterialVaultInstallMode::Staging, FScopedSlowTask* ParentTask = nullptr, float ParentBudget = 0.0f);
	static bool RepairMvpackThumbnailInCurrentProcess(const FString& PackPath, FText& OutError, bool bAllowRender);

private:
	FReply HandleScanClicked();
	FReply HandleChooseVaultRootClicked() const;
	FReply HandleOpenVaultRootClicked() const;
	FReply HandleOpenBuiltinLibraryClicked() const;
	FReply HandleCreateLibraryClicked();
	FReply HandleRefreshPackIndexClicked();
	FReply HandleCreateSelectedMvpackClicked() const;
	FReply HandleCreateAllMvpackClicked() const;
	FReply HandleInstallMvpackClicked() const;
	FReply HandleInstallSelectedPackClicked();
	FReply HandleImportSelectedPackClicked();
	FReply HandleAutoClassifyPacksClicked();
	FReply HandleCreateCategoryClicked();
	FReply HandleDeleteCategoryClicked();
	FReply HandleRepairPackThumbnailsClicked();
	FReply HandleRepairSelectedPackThumbnails();
	FReply HandleRepairAllBrokenThumbnails();
	FReply HandleExtractSelectedPackThumbnails();
	FReply HandleExtractAllBrokenThumbnails();
	FReply HandleExtractAllPackThumbnails();
	bool ExtractPackThumbnailsInternal(const TArray<TSharedPtr<FMaterialVaultPackItem>>& Targets, int32& OutSuccessCount, int32& OutFailedCount, FText& OutLastError);
	bool RepairPackThumbnailsInternal(const TArray<TSharedPtr<FMaterialVaultPackItem>>& Targets, int32& OutSuccessCount, int32& OutFailedCount, FText& OutLastError);
	TSharedRef<SWidget> BuildRepairThumbnailsMenu();
	static bool IsThumbnailCheckerboard(const FString& ThumbnailPath);
	static bool IsThumbnailMostlyEmpty(const FString& ThumbnailPath);
	static bool IsPackThumbnailBroken(const TSharedPtr<FMaterialVaultPackItem>& PackItem);
	FReply HandleCleanStagingClicked() const;
	FReply HandleOpenStagingSettingsClicked() const;
	FReply HandleExportManifestClicked() const;
	FReply HandleCollectToStagingClicked() const;
	FReply HandleScanExternalProjectClicked() const;
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FMaterialVaultItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const;
	TSharedRef<ITableRow> GeneratePackRow(TSharedPtr<FMaterialVaultPackItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> GenerateCategoryRow(TSharedPtr<FCategoryTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const;
	TSharedRef<SWidget> GenerateLibraryComboItem(TSharedPtr<FString> Item) const;
	void GetCategoryChildren(TSharedPtr<FCategoryTreeItem> Item, TArray<TSharedPtr<FCategoryTreeItem>>& OutChildren) const;
	void RefreshLibraryItems();
	void HandleLibrarySelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);
	FText GetActiveLibraryText() const;
	TSharedPtr<SWidget> BuildPackContextMenu();
	TSharedPtr<SWidget> BuildCategoryContextMenu();
	void HandleSelectionChanged(TSharedPtr<FMaterialVaultItem> Item, ESelectInfo::Type SelectInfo);
	void HandlePackSelectionChanged(TSharedPtr<FMaterialVaultPackItem> Item, ESelectInfo::Type SelectInfo);
	void HandlePackDoubleClicked(TSharedPtr<FMaterialVaultPackItem> Item);
	void HandleFamilyEntryClicked(const FMaterialVaultAssetEntry& Entry);
	void HandleCategorySelectionChanged(TSharedPtr<FCategoryTreeItem> Item, ESelectInfo::Type SelectInfo);
	void OpenScanResultsWindow();
	void RefreshScanResultsDetailsPanel();
	TSharedRef<SWidget> BuildScanResultDetailsWidget() const;
	void RefreshPreview();
	void RefreshDetailsPanel();
	TSharedRef<SWidget> BuildSelectedDetailsWidget();
	TSharedRef<SWidget> BuildMaterialFamilyTable(const FMaterialVaultItem& Item, bool bUseStagingAssets) const;
	bool PreparePackFamilyPreviewItem(const TSharedPtr<FMaterialVaultPackItem>& PackItem, FMaterialVaultItem& OutItem, FText& OutError);
	FText GetSelectedDetailsText() const;
	FText GetSelectedPackDetailsText() const;
	FText GetStatusText() const;
	EVisibility GetStandardControlsVisibility() const;
	float GetMainContentSplitterValue() const;
	float GetDetailsSplitterValue() const;
	float GetCategorySplitterValue() const;
	float GetPackListSplitterValue() const;
	TArray<TSharedPtr<FMaterialVaultPackItem>> GetSelectedPackItems() const;
	void LoadPackItems(bool bShowProgress = false);
	static bool LoadPackItemsFromIndex(const FString& VaultRoot, const TArray<FString>& PackFiles, TArray<TSharedPtr<FMaterialVaultPackItem>>& OutPackItems);
	void RefreshCategoryItems();
	void RefreshPackFilter();
	void MoveCategoryBefore(const FString& SourceCategory, const FString& TargetCategory);
	void MoveSelectedPackToCategory(FString Category);
	FReply HandleRenamePack();
	FReply HandleDeletePack();
	void AutoClassifyPacks();
	FReply HandlePackDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent, TSharedPtr<FMaterialVaultPackItem> Item);
	FReply HandlePackLibraryDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent);
	bool ResolvePackAssetData(const TSharedPtr<FMaterialVaultPackItem>& Item, FAssetData& OutAssetData, FString& OutObjectPath, FText& OutError);
	bool PlacePackMaterialInScene(const TSharedPtr<FMaterialVaultPackItem>& Item, int32 PlacementIndex, FText& OutError);
	bool InstallSelectedPacks(EMaterialVaultInstallMode Mode, int32& OutSuccessCount, int32& OutFailedCount, FString& OutLastInstallRoot, FText& OutLastError);
	EMaterialVaultSourceState GetPackSourceState(const TSharedPtr<FMaterialVaultPackItem>& Item) const;
	EMaterialVaultSourceState GetItemSourceState(const TSharedPtr<FMaterialVaultItem>& Item) const;
	static FText GetSourceBadgeText(EMaterialVaultSourceState SourceState);
	static FText GetSourceTooltipText(EMaterialVaultSourceState SourceState);
	static FLinearColor GetSourceColor(EMaterialVaultSourceState SourceState);
	FReply CreateMvpackForItems(const TArray<TSharedPtr<FMaterialVaultItem>>& ItemsToCreate, const FText& ConfirmText) const;
	FReply CreateMvpackForItems(const TArray<TSharedPtr<FMaterialVaultItem>>& ItemsToCreate, const FText& ConfirmText, const FString& OverrideCategory) const;
	static bool ExportManifest(const FMaterialVaultItem& Item, FString& OutFilePath, FText& OutError);
	static bool CollectToStaging(const FMaterialVaultItem& Item, FString& OutRootPath, FText& OutError, FScopedSlowTask* ParentTask = nullptr, float ParentBudget = 0.0f);
	static bool CreateMvpack(const FMaterialVaultItem& Item, FString& OutPackPath, FText& OutError, FScopedSlowTask* ParentTask = nullptr, float ParentBudget = 0.0f);

private:
	static FString GetPackPathForItem(const FMaterialVaultItem& Item);
	static FString GetStagingRootDirectory();
	static FString GetStagingItemDirectory(const FMaterialVaultItem& Item);
	static int64 GetDirectorySizeBytes(const FString& Directory);
	static FString FormatBytes(int64 Bytes);
	static bool CleanupStagingArea(bool bForce, FText& OutError, FMaterialVaultStagingCleanupStats* OutStats = nullptr);
	static bool CleanupStagingForItem(const FMaterialVaultItem& Item, FText& OutError, bool bForce = false);
	static bool CleanupUnusedStagingAssets(bool bForce, FText& OutError, FMaterialVaultStagingCleanupStats& OutStats);
	static bool IsStagingPackageUsedByProject(const FName PackageName, const FString& StagingMountRoot, const TSet<FName>& StagingPackages, IAssetRegistry& AssetRegistry);
	static void DeleteEmptyStagingFolders(FMaterialVaultStagingCleanupStats& InOutStats);
	static bool ShouldRunPeriodicGC(int32 ItemsSinceLastGC);
	static bool WriteMvpackFile(const FString& PackPath, const TArray<TPair<FString, FString>>& Files, FText& OutError);
	static bool CreatePackThumbnail(const FMaterialVaultItem& Item, FString& OutThumbnailPath, FText& OutError);
	static bool CreateAssetThumbnail(const FMaterialVaultAssetEntry& Entry, const FString& OutputFileName, FString& OutThumbnailPath, FText& OutError, bool bAllowRender = false);
	static bool ExtractPackThumbnail(const FString& PackPath, const FString& PackId, FString& OutThumbnailPath);
	static bool ExtractPackStoredThumbnail(const FString& PackPath, const FString& PackId, const FString& StoredThumbnailPath, FString& OutThumbnailPath);
	static TSharedPtr<FSlateDynamicImageBrush> CreateThumbnailBrush(const FString& ThumbnailPath, const FString& BrushId, const FVector2D& ImageSize, TArray<TWeakObjectPtr<UTexture2D>>* OutOwnedTextures = nullptr);
	static bool ExtractPackCategoryFromManifest(const FString& PackPath, FString& OutCategory);
	static bool ExtractPackRootInfoFromManifest(const FString& PackPath, FString& OutRootObjectPath, FString& OutAssetClass);
	static bool ExtractPackItemFromManifest(const FString& PackPath, FMaterialVaultItem& OutItem);
	static FString GetPackThumbnailCachePath(const FString& PackId, const FString& PackPath);
	static void ClearPackThumbnailCache(const FString& PackId);
	static bool ExtractPackMeta(const FString& PackPath, const FString& PackId, const FString& VaultRoot, FPackedMeta& OutMeta);
	static bool RewritePackThumbnail(const FString& PackPath, const FString& ThumbnailPath, FText& OutError);
	static bool RewritePackThumbnails(const FString& PackPath, const TArray<TPair<FString, FString>>& ReplacementFiles, FText& OutError);
	static void AddPackageFiles(const FString& PackageName, const FString& ArchiveRoot, TArray<TPair<FString, FString>>& InOutFiles);
	static FString BuildManifestJson(const FMaterialVaultItem& Item, const FString& VaultRoot, const FString& ManifestRelativePath);
	static TArray<FMaterialVaultAssetEntry> GetAllEntries(const FMaterialVaultItem& Item);
	static FString GetStagingObjectPath(const FMaterialVaultAssetEntry& Entry);
	static FString GetStagingPackageName(const FMaterialVaultAssetEntry& Entry);
	static bool InstallPluginIntoProject(const FString& ProjectFilePath, FText& OutError);
	static bool RunExternalPackThumbnailRepair(const FString& PackPath, FText& OutError);
	static bool RunExternalProjectScan(const FString& ProjectFilePath, FString& OutOutputPath, FText& OutError, const TArray<FString>* OnlyItemIds = nullptr);
	static FString ResolveVaultBaseRoot();
	static FString ResolveVaultRoot();
	static bool IsVaultLibraryDirectory(const FString& Directory);
	static bool HasChildVaultLibraries(const FString& Directory);
	static bool EnsureVaultLayout(const FString& VaultRoot, FText& OutError);
	static FString DetectPackCategory(const FString& PackFilePath, const FString& VaultRoot);
	static FString NormalizeCategoryPath(FString CategoryPath);
	static FString InferCategoryFromName(const FString& Name);
	static FString InferCategoryFromItem(const FMaterialVaultItem& Item);
	static FString MakeVaultRelativePath(const FString& AbsolutePath, const FString& VaultRoot);
	static FString RoleToString(EMaterialVaultAssetRole Role);
	static FString JsonEscape(const FString& Value);

	TArray<TSharedPtr<FMaterialVaultItem>> Items;
	TArray<TSharedPtr<FMaterialVaultPackItem>> PackItems;
	TArray<TSharedPtr<FMaterialVaultPackItem>> FilteredPackItems;
	TArray<TSharedPtr<FCategoryTreeItem>> CategoryItems;
	TSharedPtr<FMaterialVaultItem> SelectedItem;
	TSharedPtr<FMaterialVaultPackItem> SelectedPackItem;
	FString SelectedFamilyEntryKey;
	FString SelectedCategory;
	TSharedPtr<SListView<TSharedPtr<FMaterialVaultItem>>> ListView;
	TSharedPtr<SListView<TSharedPtr<FMaterialVaultItem>>> ScanResultsListView;
	TSharedPtr<SBox> ScanResultsDetailsBox;
	TSharedPtr<STileView<TSharedPtr<FMaterialVaultPackItem>>> PackListView;
	TSharedPtr<STreeView<TSharedPtr<FCategoryTreeItem>>> CategoryListView;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> LibraryComboBox;
	TSharedPtr<SBox> PreviewBox;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<FAssetThumbnail> SelectedThumbnail;
	mutable TArray<TSharedPtr<FAssetThumbnail>> ScanResultThumbnails;
	mutable TArray<TSharedPtr<FAssetThumbnail>> PackDetailThumbnails;
	TSharedPtr<FSlateDynamicImageBrush> SelectedPackBrush;
	mutable TArray<TSharedPtr<FSlateDynamicImageBrush>> OwnedThumbnailBrushes;

	mutable FDateTime StatusCacheTime;
	mutable FText CachedStatusText;
	mutable TArray<TWeakObjectPtr<UTexture2D>> OwnedThumbnailTextures;
	TArray<TSharedPtr<FString>> LibraryItems;
	TSharedPtr<IDetailsView> MaterialDetailsView;
	TSharedPtr<SBox> DetailsContainer;
	bool bCompactMode = false;
};
