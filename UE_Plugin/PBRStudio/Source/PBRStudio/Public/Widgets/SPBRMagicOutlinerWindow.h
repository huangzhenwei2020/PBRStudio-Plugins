#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/InputChord.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class APostProcessVolume;
class UCameraComponent;
class ULightComponent;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UMeshComponent;
class UPrimitiveComponent;
class UStaticMesh;
class UTexture;
class FAssetThumbnail;
class FAssetThumbnailPool;
class STableViewBase;
struct FSlateDynamicImageBrush;
struct FPostProcessSettings;
struct FGeometry;
struct FKeyEvent;
class FDragDropEvent;
struct FAssetData;
struct FPBRMagicDynamicMaterialParameter;
template<typename ItemType> class SListView;
template<typename ItemType> class STreeView;
enum class EPBRMaterialType : uint8;

enum class EPBRMagicOutlinerCategory : uint8
{
	Models,
	Materials,
	Lights,
	Cameras,
	Blueprints,
	Levels,
	All
};

enum class EPBRMagicOutlinerMode : uint8
{
	Group,
	Type,
	Material,
	Reference,
	State
};

struct FPBRMaterialSlotReference
{
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UMeshComponent> MeshComponent;
	int32 SlotIndex = INDEX_NONE;
};

struct FPBRMagicOutlinerItem
{
	FString DisplayName;
	FString TypeText;
	FString DetailText;
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UMeshComponent> MeshComponent;
	TWeakObjectPtr<UMaterialInterface> Material;
	TArray<FPBRMaterialSlotReference> MaterialSlots;
	TArray<TSharedPtr<FPBRMagicOutlinerItem>> Children;
	bool bGroup = false;
	bool bChecked = false;
	bool bExpandedByDefault = false;
	bool bExpanded = false;
	int32 ActorCount = 0;
};

struct FPBRCheckedActorListItem
{
	TWeakObjectPtr<AActor> Actor;
	FString DisplayName;
	FString TypeText;
	FString DetailText;
};

struct FPBRNameCheckListItem
{
	FString Name;
	int32 MatchCount = 0;
};

class SPBRMagicOutlinerWindow : public SCompoundWidget
{
	friend class SPBRMagicOutlinerRow;
	friend class SPBRMagicOutlinerPaintSurface;
	friend class SPBRMagicMaterialParameterPopupSurface;

public:
	SLATE_BEGIN_ARGS(SPBRMagicOutlinerWindow) {}
	SLATE_END_ARGS()

	~SPBRMagicOutlinerWindow();

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void RebuildRootContent();
	TSharedRef<SWidget> BuildPaintRoot();
	TSharedRef<SWidget> BuildClassicRoot();
	TSharedRef<SWidget> BuildTopTabs();
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildThemeSelector();
	TSharedRef<SWidget> BuildThemeMenu();
	TSharedRef<SWidget> BuildSceneTreePanel();
	TSharedRef<SWidget> BuildStatusBar();
	TSharedRef<SWidget> BuildDetailsPanel();
	TSharedRef<SWidget> BuildNameCheckPanel();
	TSharedRef<SWidget> BuildCheckedListPanel();
	TSharedRef<SWidget> BuildSelectedMaterialPanel();
	TSharedRef<SWidget> BuildMaterialParameterPopupContent();
	TSharedRef<SWidget> BuildEditableMaterialTypeMenu();
	TSharedRef<SWidget> BuildMaterialParameterControl(const struct FPBRMagicEditableMaterialParameter& Parameter);
	TArray<FPBRMagicDynamicMaterialParameter> CollectEditableDynamicMaterialParameters() const;
	bool IsDynamicMaterialParameterVisible(const FPBRMagicDynamicMaterialParameter& Parameter) const;
	TSharedRef<SWidget> BuildDynamicMaterialParameterGroup(const FString& GroupName, const TArray<FPBRMagicDynamicMaterialParameter>& Parameters);
	TSharedRef<SWidget> BuildDynamicMaterialParameterControl(const FPBRMagicDynamicMaterialParameter& Parameter);
	TSharedRef<SWidget> BuildMaterialScalarControl(const FText& Label, const FName& ParameterName, float MinValue, float MaxValue, float DefaultValue, float StepValue = 0.05f);
	TSharedRef<SWidget> BuildMaterialVectorControl(const FText& Label, const FName& ParameterName, const FLinearColor& DefaultValue);
	TSharedRef<SWidget> BuildMaterialSwitchControl(const FText& Label, const FName& ParameterName, bool bDefaultValue);
	TSharedRef<SWidget> BuildMaterialTextureControl(const FText& Label, const FName& ParameterName);
	TSharedRef<SWidget> BuildModelToolsPanel();
	TSharedRef<SWidget> BuildLightAdjustPanel();
	TSharedRef<SWidget> BuildCameraPostProcessPanel();
	TSharedRef<SWidget> BuildMaterialThumbnail(TSharedPtr<FPBRMagicOutlinerItem> Item, const FVector2D& Size);
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FPBRMagicOutlinerItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> GenerateSelectedMaterialRow(TSharedPtr<FPBRMagicOutlinerItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> GenerateNameCheckRow(TSharedPtr<FPBRNameCheckListItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> GenerateCheckedActorRow(TSharedPtr<FPBRCheckedActorListItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void GetItemChildren(TSharedPtr<FPBRMagicOutlinerItem> Item, TArray<TSharedPtr<FPBRMagicOutlinerItem>>& OutChildren) const;
	void OnTreeSelectionChanged(TSharedPtr<FPBRMagicOutlinerItem> Item, ESelectInfo::Type SelectInfo);
	void HandleTreeItemClicked(TSharedPtr<FPBRMagicOutlinerItem> Item, bool bAddToSelection);
	void OnTreeItemDoubleClicked(TSharedPtr<FPBRMagicOutlinerItem> Item);
	void OnCheckedActorSelectionChanged(TSharedPtr<FPBRCheckedActorListItem> Item, ESelectInfo::Type SelectInfo);
	EActiveTimerReturnType SyncMaterialSelectionTimer(double InCurrentTime, float InDeltaTime);
	void HandleMaterialThumbnailUpdated(const FAssetData& AssetData);
	void RefreshSelectedMaterialItems(const TSet<TWeakObjectPtr<UMaterialInterface>>& SelectedMaterials);
	void RebuildItems();
	void RebuildMaterialItems(const TArray<AActor*>& Actors);
	void RefreshNameCheckList();
	void RefreshCheckedList();
	void LoadMagicOutlinerSettings();
	void SaveMagicOutlinerSettings() const;
	TSet<TWeakObjectPtr<AActor>>& GetActiveCheckedActors();
	const TSet<TWeakObjectPtr<AActor>>& GetActiveCheckedActors() const;
	void GatherActors(TArray<AActor*>& OutActors) const;
	void GetDisplayedActors(TArray<AActor*>& OutActors) const;
	void CollectItemActors(TSharedPtr<FPBRMagicOutlinerItem> Item, TArray<AActor*>& OutActors, TSet<AActor*>& AddedActors) const;
	TSharedPtr<FPBRMagicOutlinerItem> FindFirstItemForActor(AActor* Actor, TArray<TSharedPtr<FPBRMagicOutlinerItem>>* OutAncestors = nullptr) const;
	void FocusFirstEditorSelectedActor(const TArray<AActor*>& SelectedActors);
	void SyncAutoSelectCheckedActors();
	bool PassesCategory(AActor* Actor) const;
	bool PassesClassicSearch(AActor* Actor) const;
	bool PassesClassicMaterialSearch(AActor* Actor, UMaterialInterface* Material, UMeshComponent* MeshComponent, int32 SlotIndex) const;
	FText GetModeLabel(EPBRMagicOutlinerMode Mode) const;
	FString GetModeKey(AActor* Actor) const;
	FString GetGroupKey(AActor* Actor) const;
	FString GetTypeKey(AActor* Actor) const;
	FString GetLightTypeKey(AActor* Actor) const;
	FString GetCameraTypeKey(AActor* Actor) const;
	FString GetMaterialKey(AActor* Actor) const;
	FString GetPBRParentMaterialKey(AActor* Actor) const;
	FString GetReferenceKey(AActor* Actor) const;
	FString GetStateKey(AActor* Actor) const;
	FString GetActorTypeText(AActor* Actor) const;
	FString GetActorDetailText(AActor* Actor) const;
	void AddActorToGroup(const FString& GroupKey, AActor* Actor, TMap<FString, TSharedPtr<FPBRMagicOutlinerItem>>& GroupMap);
	void AddModelMeshChildren(TSharedPtr<FPBRMagicOutlinerItem> ActorItem, AActor* Actor);
	void AddMaterialSlotToList(UMaterialInterface* Material, AActor* Actor, UMeshComponent* MeshComponent, int32 SlotIndex, TMap<UMaterialInterface*, TSharedPtr<FPBRMagicOutlinerItem>>& MaterialMap);
	FString MakeNameCheckKey(AActor* Actor) const;
	ECheckBoxState GetNameCheckState(const FString& Name) const;
	void SetNameChecked(const FString& Name, bool bChecked);
	void SetItemChecked(TSharedPtr<FPBRMagicOutlinerItem> Item, bool bChecked);
	void SetActorChecked(AActor* Actor, bool bChecked);
	ECheckBoxState GetItemCheckState(TSharedPtr<FPBRMagicOutlinerItem> Item) const;
	void CollectCheckedActors(TSharedPtr<FPBRMagicOutlinerItem> Item, TArray<AActor*>& OutActors) const;
	void CollectCheckedLightComponents(TArray<ULightComponent*>& OutLightComponents) const;
	int32 UpdateCachedCheckedCount();
	void SelectActors(const TArray<AActor*>& Actors, bool bAddToSelection) const;
	void SelectItemActors(TSharedPtr<FPBRMagicOutlinerItem> Item, bool bAddToSelection) const;
	void GetModelToolTargetActors(TArray<AActor*>& OutActors) const;
	bool IsActorSelectedInEditor(AActor* Actor) const;
	bool IsItemRepresentedInEditorSelection(TSharedPtr<FPBRMagicOutlinerItem> Item) const;
	void SyncMaterialListSelectionFromEditor();
	void CollectSelectedActorMaterials(TSet<TWeakObjectPtr<UMaterialInterface>>& OutMaterials) const;
	void GetEditorSelectedActors(TArray<AActor*>& OutActors) const;
	void HandlePostUndoRedo();
	FString BuildMaterialFocusSignature(const TSet<TWeakObjectPtr<UMaterialInterface>>& Materials) const;
	void FocusMaterialItemsFromSet(const TSet<TWeakObjectPtr<UMaterialInterface>>& Materials, bool bForceScroll);
	TSharedPtr<FAssetThumbnail> GetOrCreateMaterialThumbnail(UMaterialInterface* Material, const FVector2D& Size);
	TSharedPtr<FSlateDynamicImageBrush> GetOrCreateMaterialThumbnailBrush(UMaterialInterface* Material, const FVector2D& Size);
	TSharedPtr<FSlateDynamicImageBrush> GetOrCreateTextureThumbnailBrush(UTexture* Texture, const FVector2D& Size);
	UMaterialInterface* GetDraggedMaterial(const FDragDropEvent& DragDropEvent) const;
	UStaticMesh* GetDraggedStaticMesh(const FDragDropEvent& DragDropEvent) const;
	FReply OnMaterialItemDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, TSharedPtr<FPBRMagicOutlinerItem> Item);
	FReply OnMaterialSlotDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, TSharedPtr<FPBRMagicOutlinerItem> Item, int32 MaterialSlotIndex);
	FReply OnModelReplacementDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, TSharedPtr<FPBRMagicOutlinerItem> Item);
	FReply OnEditSelectedMaterialSlot(TSharedPtr<FPBRMagicOutlinerItem> Item, int32 MaterialSlotIndex = INDEX_NONE);
	void OpenEditableMaterialParameterWindow();
	void OpenMaterialEditor(TSharedPtr<FPBRMagicOutlinerItem> Item);
	bool ResolveEditableMaterialSlot(UPrimitiveComponent*& OutComponent, int32& OutSlotIndex) const;
	UMaterialInstanceConstant* GetEditableMaterialInstance() const;
	EPBRMaterialType GetEditableMaterialType() const;
	FText GetEditableMaterialNameText() const;
	FText GetEditableMaterialSlotText() const;
	FText GetEditableMaterialTypeText() const;
	TOptional<float> GetEditableMaterialScalar(const FName& ParameterName, float DefaultValue) const;
	TOptional<float> GetEditableMaterialVectorChannel(const FName& ParameterName, int32 ChannelIndex, const FLinearColor& DefaultValue) const;
	bool GetEditableMaterialSwitch(const FName& ParameterName, bool bDefaultValue) const;
	UTexture* GetEditableMaterialTexture(const FName& ParameterName) const;
	void CommitEditableMaterialScalar(const FName& ParameterName, float Value, float MinValue, float MaxValue);
	void CommitEditableMaterialVector(const FName& ParameterName, const FLinearColor& Value);
	void CommitEditableMaterialVectorChannel(const FName& ParameterName, int32 ChannelIndex, float Value, const FLinearColor& DefaultValue);
	void CommitEditableMaterialSwitch(const FName& ParameterName, bool bValue);
	void CommitEditableMaterialTexture(const FName& ParameterName, UTexture* Texture);
	void OpenEditableMaterialColorPicker(const FName& ParameterName, const FLinearColor& DefaultValue);
	void StepEditableMaterialScalar(const FName& ParameterName, float DeltaValue, float MinValue, float MaxValue, float DefaultValue);
	void SelectEditableMaterialType(EPBRMaterialType MaterialType);
	void CycleEditableMaterialType(int32 Direction);
	int32 ReplaceMaterialItem(TSharedPtr<FPBRMagicOutlinerItem> Item, UMaterialInterface* NewMaterial);
	int32 ReplaceMaterialSlot(TSharedPtr<FPBRMagicOutlinerItem> Item, int32 MaterialSlotIndex, UMaterialInterface* NewMaterial);
	int32 ReplaceModelActorsMesh(const TArray<AActor*>& Actors, UStaticMesh* NewMesh);
	FReply OnModelBatchRenameClicked();
	FReply OnModelReplaceActorsClicked();
	FReply OnModelGroupToFolderClicked();
	void ApplyModelBatchRename(const FString& Prefix, int32 StartIndex, bool bKeepOriginalName, bool bMoveToFolder, const FString& FolderName, bool bMoveToNewActor, const FString& NewActorName);
	void MoveModelTargetsToFolder(const FString& FolderName);
	int32 MoveActorsToFolder(const TArray<AActor*>& Actors, const FString& FolderName, bool bUseAttachmentRoot);
	int32 MoveActorsToNewParentActor(const TArray<AActor*>& Actors, const FString& NewActorName);
	bool IsIsolationProtectedActor(AActor* Actor) const;
	bool DoesKeyEventMatchShortcut(const FKeyEvent& InKeyEvent) const;
	FString ShortcutToString(const FInputChord& Chord) const;
	bool TryParseShortcut(const FString& Text, FInputChord& OutChord) const;
	FString BuildShortcutConflictText(const FInputChord& Chord) const;
	TSharedRef<SWidget> BuildShortcutSettingsContent();
	FReply OnRefreshClicked();
	FReply OnSelectCheckedClicked();
	FReply OnClearCheckedClicked();
	FReply OnToggleSelectedCheckedClicked();
	FReply OnInvertCheckedClicked();
	FReply OnToggleIsolationClicked();
	FReply OnIsolateSelectionClicked();
	FReply OnExitIsolationClicked();
	FReply OnToggleCheckedVisibilityClicked();
	FReply OnShortcutSettingsClicked();
	FReply OnToggleCompactModeClicked();
	FReply OnThemeSelected(FName ThemeId);
	FReply OnToggleClassicSkinClicked();
	FReply OnSelectCheckedListClicked();
	FReply OnRemoveCheckedListSelectionClicked();
	FReply OnLightColorBlockClicked();
	void OnLightColorCommitted(FLinearColor NewColor);
	void ApplyLightAdjustmentsRealtime();
	void RefreshLightAdjustmentBaselines();
	AActor* GetDetailsActor() const;
	APostProcessVolume* GetDetailsPostProcessVolume() const;
	UCameraComponent* GetDetailsCameraComponent() const;
	FPostProcessSettings* GetDetailsPostProcessSettings() const;
	void NotifyPostProcessSettingsChanged();
	void ApplySuggestedPostProcessSettings();
	FText GetStatusText() const;
	FText GetScenePanelTitleText() const;
	FText GetScenePanelSummaryText() const;
	FSlateColor GetCategoryColor(EPBRMagicOutlinerCategory Category) const;
	EVisibility GetLightAdjustVisibility() const;
	EVisibility GetCameraPostProcessVisibility() const;
	EVisibility GetCheckedActionsVisibility() const;
	EVisibility GetSelectedMaterialVisibility() const;
	EVisibility GetEditableMaterialPanelVisibility() const;
	EVisibility GetStandardControlsVisibility() const;
	EVisibility GetDetailsPanelVisibility() const;
	FText GetCheckedSummaryText() const;
	FText GetSelectedMaterialSummaryText() const;
	FText GetCompactModeText() const;
	FText GetThemeButtonText() const;
	FLinearColor GetThemeColor(FName ColorName) const;

	TSharedPtr<STreeView<TSharedPtr<FPBRMagicOutlinerItem>>> TreeView;
	TSharedPtr<SListView<TSharedPtr<FPBRNameCheckListItem>>> NameCheckListView;
	TSharedPtr<SListView<TSharedPtr<FPBRCheckedActorListItem>>> CheckedListView;
	TSharedPtr<SListView<TSharedPtr<FPBRMagicOutlinerItem>>> SelectedMaterialListView;
	TSharedPtr<class SEditableTextBox> ShortcutSettingsEditBox;
	TSharedPtr<class STextBlock> ShortcutConflictTextBlock;
	TArray<TSharedPtr<FPBRMagicOutlinerItem>> RootItems;
	TArray<TSharedPtr<FPBRNameCheckListItem>> NameCheckListItems;
	TArray<TSharedPtr<FPBRCheckedActorListItem>> CheckedListItems;
	TArray<TSharedPtr<FPBRMagicOutlinerItem>> SelectedMaterialItems;
	TSharedPtr<FPBRMagicOutlinerItem> ActiveTreeItem;
	TWeakObjectPtr<UPrimitiveComponent> EditableMaterialComponent;
	int32 EditableMaterialSlotIndex = INDEX_NONE;
	TSet<TWeakObjectPtr<AActor>> PaintSelectedCheckedActors;
	TMap<EPBRMagicOutlinerCategory, TSet<TWeakObjectPtr<AActor>>> CheckedActorsByCategory;
	TMap<TWeakObjectPtr<AActor>, bool> IsolateHiddenStates;
	TMap<TWeakObjectPtr<ULightComponent>, float> LightBaseIntensities;
	TMap<TWeakObjectPtr<UMaterialInterface>, TSharedPtr<FPBRMagicOutlinerItem>> MaterialItemsByMaterial;
	TSharedPtr<FAssetThumbnailPool> MaterialThumbnailPool;
	TWeakPtr<class SWindow> MaterialParameterWindow;
	TMap<FString, TSharedPtr<FAssetThumbnail>> MaterialThumbnailCache;
	TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> MaterialThumbnailBrushCache;
	TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> TextureThumbnailBrushCache;
	TWeakObjectPtr<AActor> DetailsActor;
	EPBRMagicOutlinerCategory ActiveCategory = EPBRMagicOutlinerCategory::All;
	EPBRMagicOutlinerMode ActiveMode = EPBRMagicOutlinerMode::Type;
	FString StatusMessage;
	FString ClassicSearchText;
	FString LastEditorSelectionSignature;
	int32 CachedActorCount = 0;
	int32 CachedCheckedCount = 0;
	bool bIsolationActive = false;
	bool bAutoSelectCheckedActors = false;
	bool bCheckedActorsHidden = false;
	bool bCompactMode = false;
	bool bUseClassicSkin = false;
	FInputChord IsolationShortcut;
	FName ActiveThemeId = TEXT("graphite_cyan");
	FString LastMaterialFocusSignature;
	float LightIntensityMultiplier = 1.0f;
	float LightTemperature = 6500.0f;
	FLinearColor LightColor = FLinearColor::White;
};
