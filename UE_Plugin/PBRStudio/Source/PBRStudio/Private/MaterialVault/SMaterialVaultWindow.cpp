#include "SMaterialVaultWindow.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "AssetThumbnail.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "MaterialVaultScanner.h"
#include "MaterialVaultSettings.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "ISettingsModule.h"
#include "Dom/JsonObject.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialFunctionInterface.h"
#include "MaterialExpressionIO.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/CommandLine.h"
#include "Misc/MessageDialog.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"

class SMaterialVaultDropTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialVaultDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnDrop, OnDrop)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnDropDelegate = InArgs._OnDrop;
		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		return OnDropDelegate.IsBound() ? OnDropDelegate.Execute(MyGeometry, DragDropEvent) : FReply::Unhandled();
	}

private:
	FOnDrop OnDropDelegate;
};

static void RefreshMaterialVaultFunctionCallsInExpressions(TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions)
{
	for (const TObjectPtr<UMaterialExpression>& ExpressionPtr : Expressions)
	{
		UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(ExpressionPtr.Get());
		if (!FunctionCall || !FunctionCall->MaterialFunction)
		{
			continue;
		}

		// Duplicated material-function call nodes keep transient input/output links.
		// Refresh them after object-reference replacement so SM6 does not compile
		// against stale or missing function resources.
		const bool bRecreateAndLinkNode = false;
		FunctionCall->UpdateFromFunctionResource(bRecreateAndLinkNode);
	}
}

static void RefreshMaterialVaultFunctionCalls(UObject* Asset)
{
	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		RefreshMaterialVaultFunctionCallsInExpressions(Material->GetExpressions());
		Material->MarkPackageDirty();
		return;
	}

	if (UMaterialFunctionInterface* Function = Cast<UMaterialFunctionInterface>(Asset))
	{
		Function->UpdateFromFunctionResource();
		RefreshMaterialVaultFunctionCallsInExpressions(Function->GetExpressions());
		Function->MarkPackageDirty();
	}
}

static bool RepairMaterialVaultMaterialExpressionsForPackaging(
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions,
	const TArray<UMaterialInstance*>& FamilyInstances)
{
	bool bChanged = false;
	for (const TObjectPtr<UMaterialExpression>& ExpressionPtr : Expressions)
	{
		UMaterialExpression* Expression = ExpressionPtr.Get();
		if (!Expression)
		{
			continue;
		}

		if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction)
			{
				FunctionCall->UpdateFromFunctionResource(false);
			}
			continue;
		}

		if (UMaterialExpressionTextureSampleParameter2D* TextureSample = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
		{
			if (!TextureSample->Texture && !TextureSample->TextureObject.Expression)
			{
				UTexture* ReplacementTexture = nullptr;
				if (!TextureSample->ParameterName.IsNone())
				{
					const FHashedMaterialParameterInfo ParameterInfo(TextureSample->ParameterName);
					for (UMaterialInstance* FamilyInstance : FamilyInstances)
					{
						if (FamilyInstance && FamilyInstance->GetTextureParameterValue(ParameterInfo, ReplacementTexture, true) && ReplacementTexture)
						{
							break;
						}
						ReplacementTexture = nullptr;
					}
				}

				if (ReplacementTexture)
				{
					TextureSample->Texture = ReplacementTexture;
				}
				else
				{
					TextureSample->SetDefaultTexture();
				}

				if (TextureSample->Texture)
				{
					bChanged = true;
				}
			}
			continue;
		}

		if (UMaterialExpressionStaticSwitchParameter* StaticSwitch = Cast<UMaterialExpressionStaticSwitchParameter>(Expression))
		{
			if (!StaticSwitch->A.Expression && StaticSwitch->B.Expression)
			{
				StaticSwitch->A = StaticSwitch->B;
				bChanged = true;
			}
			else if (!StaticSwitch->B.Expression && StaticSwitch->A.Expression)
			{
				StaticSwitch->B = StaticSwitch->A;
				bChanged = true;
			}
		}
	}
	return bChanged;
}

static bool RestoreMaterialVaultFunctionCallsFromSourceExpressions(
	TConstArrayView<TObjectPtr<UMaterialExpression>> SourceExpressions,
	TConstArrayView<TObjectPtr<UMaterialExpression>> DuplicatedExpressions,
	const TMap<UObject*, UObject*>& ReplacementMap)
{
	bool bChanged = false;
	const int32 ExpressionCount = FMath::Min(SourceExpressions.Num(), DuplicatedExpressions.Num());
	for (int32 Index = 0; Index < ExpressionCount; ++Index)
	{
		UMaterialExpressionMaterialFunctionCall* SourceFunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(SourceExpressions[Index].Get());
		UMaterialExpressionMaterialFunctionCall* DuplicatedFunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(DuplicatedExpressions[Index].Get());
		if (!SourceFunctionCall || !DuplicatedFunctionCall || DuplicatedFunctionCall->MaterialFunction || !SourceFunctionCall->MaterialFunction)
		{
			continue;
		}

		UObject* const* ReplacementFunction = ReplacementMap.Find(SourceFunctionCall->MaterialFunction);
		UMaterialFunctionInterface* DuplicatedFunction = ReplacementFunction ? Cast<UMaterialFunctionInterface>(*ReplacementFunction) : nullptr;
		if (!DuplicatedFunction)
		{
			continue;
		}

		DuplicatedFunctionCall->SetMaterialFunction(DuplicatedFunction);
		DuplicatedFunctionCall->UpdateFromFunctionResource(false);
		bChanged = true;
	}
	return bChanged;
}

static bool CopyMaterialVaultExpressionInputFromSource(
	const FExpressionInput& SourceInput,
	FExpressionInput& DuplicatedInput,
	const TMap<UMaterialExpression*, UMaterialExpression*>& ExpressionMap)
{
	if (!SourceInput.Expression)
	{
		return false;
	}

	UMaterialExpression* const* DuplicatedExpression = ExpressionMap.Find(SourceInput.Expression);
	if (!DuplicatedExpression || !*DuplicatedExpression)
	{
		return false;
	}

	if (DuplicatedInput.Expression == *DuplicatedExpression
		&& DuplicatedInput.OutputIndex == SourceInput.OutputIndex
		&& DuplicatedInput.InputName == SourceInput.InputName
		&& DuplicatedInput.Mask == SourceInput.Mask
		&& DuplicatedInput.MaskR == SourceInput.MaskR
		&& DuplicatedInput.MaskG == SourceInput.MaskG
		&& DuplicatedInput.MaskB == SourceInput.MaskB
		&& DuplicatedInput.MaskA == SourceInput.MaskA)
	{
		return false;
	}

	DuplicatedInput.Expression = *DuplicatedExpression;
	DuplicatedInput.OutputIndex = SourceInput.OutputIndex;
	DuplicatedInput.InputName = SourceInput.InputName;
	DuplicatedInput.Mask = SourceInput.Mask;
	DuplicatedInput.MaskR = SourceInput.MaskR;
	DuplicatedInput.MaskG = SourceInput.MaskG;
	DuplicatedInput.MaskB = SourceInput.MaskB;
	DuplicatedInput.MaskA = SourceInput.MaskA;
	return true;
}

static void BuildMaterialVaultExpressionReplacementMap(
	TConstArrayView<TObjectPtr<UMaterialExpression>> SourceExpressions,
	TConstArrayView<TObjectPtr<UMaterialExpression>> DuplicatedExpressions,
	TMap<UMaterialExpression*, UMaterialExpression*>& OutExpressionMap)
{
	const int32 ExpressionCount = FMath::Min(SourceExpressions.Num(), DuplicatedExpressions.Num());
	for (int32 Index = 0; Index < ExpressionCount; ++Index)
	{
		UMaterialExpression* SourceExpression = SourceExpressions[Index].Get();
		UMaterialExpression* DuplicatedExpression = DuplicatedExpressions[Index].Get();
		if (SourceExpression && DuplicatedExpression)
		{
			OutExpressionMap.Add(SourceExpression, DuplicatedExpression);
		}
	}
}

static bool RestoreMaterialVaultExpressionInputsFromSourceExpressions(
	TConstArrayView<TObjectPtr<UMaterialExpression>> SourceExpressions,
	TConstArrayView<TObjectPtr<UMaterialExpression>> DuplicatedExpressions,
	const TMap<UMaterialExpression*, UMaterialExpression*>& ExpressionMap)
{
	bool bChanged = false;
	const int32 ExpressionCount = FMath::Min(SourceExpressions.Num(), DuplicatedExpressions.Num());
	for (int32 Index = 0; Index < ExpressionCount; ++Index)
	{
		UMaterialExpression* SourceExpression = SourceExpressions[Index].Get();
		UMaterialExpression* DuplicatedExpression = DuplicatedExpressions[Index].Get();
		if (!SourceExpression || !DuplicatedExpression)
		{
			continue;
		}

		for (int32 InputIndex = 0;; ++InputIndex)
		{
			const FExpressionInput* SourceInput = SourceExpression->GetInput(InputIndex);
			FExpressionInput* DuplicatedInput = DuplicatedExpression->GetInput(InputIndex);
			if (!SourceInput || !DuplicatedInput)
			{
				break;
			}

			bChanged |= CopyMaterialVaultExpressionInputFromSource(*SourceInput, *DuplicatedInput, ExpressionMap);
		}
	}
	return bChanged;
}

static bool RestoreMaterialVaultMaterialPropertyInputsFromSource(
	UMaterial* SourceMaterial,
	UMaterial* DuplicatedMaterial,
	const TMap<UMaterialExpression*, UMaterialExpression*>& ExpressionMap)
{
	bool bChanged = false;
	for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
	{
		const EMaterialProperty Property = static_cast<EMaterialProperty>(PropertyIndex);
		const FExpressionInput* SourceInput = SourceMaterial->GetExpressionInputForProperty(Property);
		FExpressionInput* DuplicatedInput = DuplicatedMaterial->GetExpressionInputForProperty(Property);
		if (!SourceInput || !DuplicatedInput)
		{
			continue;
		}

		bChanged |= CopyMaterialVaultExpressionInputFromSource(*SourceInput, *DuplicatedInput, ExpressionMap);
	}
	return bChanged;
}

static void RestoreMaterialVaultGraphFromSource(
	UObject* SourceAsset,
	UObject* DuplicatedAsset,
	const TMap<UObject*, UObject*>& ReplacementMap)
{
	bool bChanged = false;
	if (UMaterial* SourceMaterial = Cast<UMaterial>(SourceAsset))
	{
		if (UMaterial* DuplicatedMaterial = Cast<UMaterial>(DuplicatedAsset))
		{
			TMap<UMaterialExpression*, UMaterialExpression*> ExpressionMap;
			BuildMaterialVaultExpressionReplacementMap(
				SourceMaterial->GetExpressions(),
				DuplicatedMaterial->GetExpressions(),
				ExpressionMap);
			bChanged = RestoreMaterialVaultFunctionCallsFromSourceExpressions(
				SourceMaterial->GetExpressions(),
				DuplicatedMaterial->GetExpressions(),
				ReplacementMap);
			bChanged |= RestoreMaterialVaultExpressionInputsFromSourceExpressions(
				SourceMaterial->GetExpressions(),
				DuplicatedMaterial->GetExpressions(),
				ExpressionMap);
			bChanged |= RestoreMaterialVaultMaterialPropertyInputsFromSource(
				SourceMaterial,
				DuplicatedMaterial,
				ExpressionMap);
		}
	}
	else if (UMaterialFunctionInterface* SourceFunction = Cast<UMaterialFunctionInterface>(SourceAsset))
	{
		if (UMaterialFunctionInterface* DuplicatedFunction = Cast<UMaterialFunctionInterface>(DuplicatedAsset))
		{
			TMap<UMaterialExpression*, UMaterialExpression*> ExpressionMap;
			BuildMaterialVaultExpressionReplacementMap(
				SourceFunction->GetExpressions(),
				DuplicatedFunction->GetExpressions(),
				ExpressionMap);
			bChanged = RestoreMaterialVaultFunctionCallsFromSourceExpressions(
				SourceFunction->GetExpressions(),
				DuplicatedFunction->GetExpressions(),
				ReplacementMap);
			bChanged |= RestoreMaterialVaultExpressionInputsFromSourceExpressions(
				SourceFunction->GetExpressions(),
				DuplicatedFunction->GetExpressions(),
				ExpressionMap);
		}
	}

	if (bChanged)
	{
		DuplicatedAsset->PostEditChange();
		DuplicatedAsset->MarkPackageDirty();
	}
}

static void RepairMaterialVaultMaterialForPackaging(UObject* Asset, const TArray<UMaterialInstance*>& FamilyInstances)
{
	bool bChanged = false;
	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		bChanged = RepairMaterialVaultMaterialExpressionsForPackaging(Material->GetExpressions(), FamilyInstances);
	}
	else if (UMaterialFunctionInterface* Function = Cast<UMaterialFunctionInterface>(Asset))
	{
		bChanged = RepairMaterialVaultMaterialExpressionsForPackaging(Function->GetExpressions(), FamilyInstances);
	}

	if (bChanged)
	{
		Asset->MarkPackageDirty();
	}
}

static bool SaveMaterialVaultStagingAssetQuietly(UObject* Asset, FText& OutError)
{
	if (!Asset)
	{
		OutError = FText::FromString(TEXT("暂存资产为空，无法保存。"));
		return false;
	}

	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		OutError = FText::Format(FText::FromString(TEXT("暂存资产没有有效包：\n{0}")), FText::FromString(Asset->GetPathName()));
		return false;
	}

	const FString PackageName = Package->GetName();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);
	Package->GetMetaData();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError | SAVE_FromAutosave;
	SaveArgs.bSlowTask = false;
	SaveArgs.bWarnOfLongFilename = false;
	SaveArgs.Error = GError;

	if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
	{
		OutError = FText::Format(FText::FromString(TEXT("保存暂存资产失败：\n{0}")), FText::FromString(PackageName));
		return false;
	}

	Package->SetDirtyFlag(false);
	return true;
}

class FMaterialVaultCategoryDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMaterialVaultCategoryDragDropOp, FDecoratedDragDropOp)

	FString CategoryPath;

	static TSharedRef<FMaterialVaultCategoryDragDropOp> New(const FString& InCategoryPath, const FString& InDisplayName)
	{
		TSharedRef<FMaterialVaultCategoryDragDropOp> Operation = MakeShared<FMaterialVaultCategoryDragDropOp>();
		Operation->CategoryPath = InCategoryPath;
		Operation->DefaultHoverText = FText::FromString(InDisplayName);
		Operation->Construct();
		return Operation;
	}
};

SMaterialVaultWindow::~SMaterialVaultWindow()
{
	SelectedPackBrush.Reset();
	OwnedThumbnailBrushes.Reset();
	for (const TWeakObjectPtr<UTexture2D>& WeakTexture : OwnedThumbnailTextures)
	{
		if (UTexture2D* Texture = WeakTexture.Get())
		{
			Texture->RemoveFromRoot();
		}
	}
	OwnedThumbnailTextures.Reset();
}

void SMaterialVaultWindow::Construct(const FArguments& InArgs)
{
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	SelectedCategory = TEXT("全部");
	RefreshLibraryItems();
	LoadPackItems(true);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bShowOptions = true;
	DetailsViewArgs.bHideSelectionTip = false;
	MaterialDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	MaterialDetailsView->SetVisibility(EVisibility::Collapsed);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SMaterialVaultWindow::GetStandardControlsVisibility)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("扫描本项目材质")))
				.OnClicked(this, &SMaterialVaultWindow::HandleScanClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("扫描外部项目材质")))
				.OnClicked(this, &SMaterialVaultWindow::HandleScanExternalProjectClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("选择材质库目录")))
				.OnClicked(this, &SMaterialVaultWindow::HandleChooseVaultRootClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("\u6253\u5f00\u6750\u8d28\u5e93\u76ee\u5f55")))
				.OnClicked(this, &SMaterialVaultWindow::HandleOpenVaultRootClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("刷新材质包")))
				.OnClicked(this, &SMaterialVaultWindow::HandleRefreshPackIndexClicked)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SMaterialVaultWindow::GetStatusText)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot()
			.Value_Lambda([this]() { return GetMainContentSplitterValue(); })
			[
				// Inner splitter: category tree | pack tile view
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.MinSize(136.0f)
				.Value_Lambda([this]() { return GetCategorySplitterValue(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f)
					[
						SAssignNew(LibraryComboBox, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&LibraryItems)
						.OnGenerateWidget(this, &SMaterialVaultWindow::GenerateLibraryComboItem)
						.OnSelectionChanged(this, &SMaterialVaultWindow::HandleLibrarySelectionChanged)
						[
							SNew(STextBlock)
							.Text(this, &SMaterialVaultWindow::GetActiveLibraryText)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("分类")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.Visibility(this, &SMaterialVaultWindow::GetStandardControlsVisibility)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("新建")))
								.ToolTipText(FText::FromString(TEXT("新建材质库。分类请在分类列表中右键新建。")))
								.OnClicked(this, &SMaterialVaultWindow::HandleCreateLibraryClicked)
							]
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(CategoryListView, STreeView<TSharedPtr<FCategoryTreeItem>>)
						.TreeItemsSource(&CategoryItems)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SMaterialVaultWindow::GenerateCategoryRow)
						.OnGetChildren(this, &SMaterialVaultWindow::GetCategoryChildren)
						.OnSelectionChanged(this, &SMaterialVaultWindow::HandleCategorySelectionChanged)
						.OnContextMenuOpening(this, &SMaterialVaultWindow::BuildCategoryContextMenu)
					]
				]
				+ SSplitter::Slot()
				.MinSize(360.0f)
				.Value_Lambda([this]() { return GetPackListSplitterValue(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.0f, 4.0f)
					[
						SNew(SHorizontalBox)
						.Visibility(this, &SMaterialVaultWindow::GetStandardControlsVisibility)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("材质包库")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("自动分类")))
							.OnClicked(this, &SMaterialVaultWindow::HandleAutoClassifyPacksClicked)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SComboButton)
							.HasDownArrow(true)
							.ButtonContent()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("修复缩略图")))
							]
							.OnGetMenuContent(this, &SMaterialVaultWindow::BuildRepairThumbnailsMenu)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("删除未使用暂存")))
							.OnClicked(this, &SMaterialVaultWindow::HandleCleanStagingClicked)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("暂存区设置")))
							.OnClicked(this, &SMaterialVaultWindow::HandleOpenStagingSettingsClicked)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("正式导入")))
							.OnClicked(this, &SMaterialVaultWindow::HandleImportSelectedPackClicked)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("加载到暂存区")))
							.OnClicked(this, &SMaterialVaultWindow::HandleInstallSelectedPackClicked)
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SMaterialVaultDropTarget)
						.OnDrop(this, &SMaterialVaultWindow::HandlePackLibraryDrop)
						[
							SAssignNew(PackListView, STileView<TSharedPtr<FMaterialVaultPackItem>>)
							.ListItemsSource(&FilteredPackItems)
							.SelectionMode(ESelectionMode::Multi)
							.OnGenerateTile(this, &SMaterialVaultWindow::GeneratePackRow)
							.OnSelectionChanged(this, &SMaterialVaultWindow::HandlePackSelectionChanged)
							.OnMouseButtonDoubleClick(this, &SMaterialVaultWindow::HandlePackDoubleClicked)
							.OnContextMenuOpening(this, &SMaterialVaultWindow::BuildPackContextMenu)
							.ItemWidth(140.0f)
							.ItemHeight(176.0f)
						]
					]
				]
			]
			+ SSplitter::Slot()
			.MinSize(0.0f)
			.Value_Lambda([this]() { return GetDetailsSplitterValue(); })
			[
				SNew(SBorder)
				.Padding(10.0f)
				.Visibility(this, &SMaterialVaultWindow::GetStandardControlsVisibility)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						SAssignNew(PreviewBox, SBox)
						.WidthOverride(280.0f)
						.HeightOverride(280.0f)
						[
							SNew(SBorder)
							.Padding(8.0f)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("选择材质包后显示预览")))
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("加载到暂存区")))
						.OnClicked(this, &SMaterialVaultWindow::HandleInstallSelectedPackClicked)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("正式导入 / 保留到项目材质库")))
						.OnClicked(this, &SMaterialVaultWindow::HandleImportSelectedPackClicked)
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SWidgetSwitcher)
						.WidgetIndex_Lambda([this]() -> int32 { return (MaterialDetailsView.IsValid() && MaterialDetailsView->GetVisibility() == EVisibility::Visible) ? 1 : 0; })
						+ SWidgetSwitcher::Slot()
						[
							SAssignNew(DetailsContainer, SBox)
							[
								BuildSelectedDetailsWidget()
							]
						]
						+ SWidgetSwitcher::Slot()
						[
							MaterialDetailsView.ToSharedRef()
						]
					]
				]
			]
		]
	];
}

void SMaterialVaultWindow::SetCompactModeFromGlobal(bool bInCompactMode)
{
	if (bCompactMode == bInCompactMode)
	{
		return;
	}

	bCompactMode = bInCompactMode;
	if (PackListView.IsValid())
	{
		PackListView->RequestListRefresh();
	}
	if (CategoryListView.IsValid())
	{
		CategoryListView->RequestTreeRefresh();
	}
}

EVisibility SMaterialVaultWindow::GetStandardControlsVisibility() const
{
	return bCompactMode ? EVisibility::Collapsed : EVisibility::Visible;
}

float SMaterialVaultWindow::GetMainContentSplitterValue() const
{
	return bCompactMode ? 1.0f : 0.74f;
}

float SMaterialVaultWindow::GetDetailsSplitterValue() const
{
	return bCompactMode ? 0.0f : 0.26f;
}

float SMaterialVaultWindow::GetCategorySplitterValue() const
{
	return bCompactMode ? 0.22f : 0.16f;
}

float SMaterialVaultWindow::GetPackListSplitterValue() const
{
	return bCompactMode ? 0.78f : 0.87f;
}

FReply SMaterialVaultWindow::HandleScanClicked()
{
	FScopedSlowTask SlowTask(1.0f, FText::FromString(TEXT("正在扫描本项目材质...")));
	SlowTask.MakeDialog(true);
	SlowTask.EnterProgressFrame(1.0f);

	FMaterialVaultScanner Scanner;
	FMaterialVaultScanStats ScanStats;
	Items = Scanner.ScanMaterials(TEXT("/Game"), &ScanStats);
	SlowTask.EnterProgressFrame(0.0f, FText::Format(
		FText::FromString(TEXT("\u626b\u63cf\u5b8c\u6210\uff1a\u6bcd\u6750\u8d28 {0}\uff0c\u6750\u8d28\u5b9e\u4f8b {1}\uff0c\u6750\u8d28\u5b9e\u4f8b\u5305 {2}\uff0c\u53ef\u751f\u6210 {3}")),
		FText::AsNumber(ScanStats.RawMaterialCount),
		FText::AsNumber(ScanStats.RawMaterialInstanceCount),
		FText::AsNumber(ScanStats.StandaloneMaterialInstanceCount),
		FText::AsNumber(ScanStats.GeneratedItemCount)));
	SelectedItem.Reset();
	RefreshPreview();
	OpenScanResultsWindow();

	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleChooseVaultRootClicked() const
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("无法打开目录选择窗口。")));
		return FReply::Handled();
	}

	FString SelectedDirectory;
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bSelected = DesktopPlatform->OpenDirectoryDialog(
		ParentWindowHandle,
		TEXT("选择材质库总目录"),
		GetDefault<UMaterialVaultSettings>()->ExternalVaultRoot.Path.IsEmpty()
			? FPaths::Combine(FPlatformProcess::UserDir(), TEXT("MaterialVault"))
			: ResolveVaultBaseRoot(),
		SelectedDirectory);

	if (!bSelected || SelectedDirectory.IsEmpty())
	{
		return FReply::Handled();
	}

	FString BaseDirectory = SelectedDirectory;
	FString ActiveLibraryName = GetDefault<UMaterialVaultSettings>()->ActiveLibraryName;
	if (IsVaultLibraryDirectory(SelectedDirectory) && !HasChildVaultLibraries(SelectedDirectory))
	{
		BaseDirectory = FPaths::GetPath(SelectedDirectory);
		ActiveLibraryName = FPaths::GetCleanFilename(SelectedDirectory);
	}

	UMaterialVaultSettings* Settings = GetMutableDefault<UMaterialVaultSettings>();
	Settings->ExternalVaultRoot.Path = BaseDirectory;
	if (!ActiveLibraryName.IsEmpty())
	{
		Settings->ActiveLibraryName = ActiveLibraryName;
	}
	Settings->SaveConfig();

	FText Error;
	if (!EnsureVaultLayout(ResolveVaultRoot(), Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	const_cast<SMaterialVaultWindow*>(this)->RefreshLibraryItems();
	if (CategoryListView.IsValid()) { CategoryListView->RequestTreeRefresh(); }
	if (PackListView.IsValid()) { PackListView->RequestListRefresh(); }
	const_cast<SMaterialVaultWindow*>(this)->LoadPackItems(true);

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			FText::FromString(TEXT("材质库总目录已设置为：\n{0}\n\n当前使用的子材质库：{1}")),
			FText::FromString(BaseDirectory),
			FText::FromString(GetDefault<UMaterialVaultSettings>()->ActiveLibraryName)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleOpenVaultRootClicked() const
{
	FText Error;
	const FString VaultRoot = ResolveVaultRoot();
	if (!EnsureVaultLayout(VaultRoot, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}

	FPlatformProcess::ExploreFolder(*VaultRoot);
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleCreateLibraryClicked()
{
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	const FString DefaultParent = ResolveVaultBaseRoot();

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("新建材质库")))
		.ClientSize(FVector2D(480.0f, 160.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedPtr<SEditableTextBox> ParentBox;
	TSharedPtr<SEditableTextBox> NameBox;
	Window->SetContent(
		SNew(SBorder).Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("材质库总目录")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SAssignNew(ParentBox, SEditableTextBox)
				.Text(FText::FromString(DefaultParent))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("新材质库名称")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SAssignNew(NameBox, SEditableTextBox)
				.Text(FText::FromString(TEXT("新材质库")))
				.SelectAllTextWhenFocused(true)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("创建并切换")))
				.OnClicked_Lambda([this, Window, ParentBox, NameBox]() -> FReply
				{
					FString Parent = ParentBox.IsValid() ? ParentBox->GetText().ToString() : FString();
					FString Name = NameBox.IsValid() ? NameBox->GetText().ToString() : FString();
					Parent.TrimStartAndEndInline();
					Name.TrimStartAndEndInline();
					const TCHAR* InvalidChars = TEXT("\\/:*?\"<>|");
					for (const TCHAR* It = InvalidChars; *It; ++It)
					{
						Name.ReplaceCharInline(*It, TEXT('_'));
					}
					if (Parent.IsEmpty() || Name.IsEmpty())
					{
						FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("总目录和材质库名称不能为空。")));
						return FReply::Handled();
					}

					const FString NewRoot = FPaths::Combine(Parent, Name);
					FText Error;
					if (!EnsureVaultLayout(NewRoot, Error))
					{
						FMessageDialog::Open(EAppMsgType::Ok, Error);
						return FReply::Handled();
					}

					UMaterialVaultSettings* Settings = GetMutableDefault<UMaterialVaultSettings>();
					Settings->ExternalVaultRoot.Path = Parent;
					Settings->ActiveLibraryName = Name;
					Settings->SaveConfig();
					RefreshLibraryItems();
					if (LibraryComboBox.IsValid()) { LibraryComboBox->RefreshOptions(); }
					LoadPackItems(true);
					if (PackListView.IsValid()) { PackListView->RequestListRefresh(); }
					Window->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]);
	FSlateApplication::Get().AddWindow(Window);
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleRefreshPackIndexClicked()
{
	FString IndexPath;
	int32 PackCount = 0;
	FText Error;
	if (!RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}
	LoadPackItems();
	if (PackListView.IsValid())
	{
		PackListView->RequestListRefresh();
	}

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			FText::FromString(TEXT("已刷新材质包索引。\n发现 {0} 个 .mvpack。\n索引文件：\n{1}")),
			FText::AsNumber(PackCount),
			FText::FromString(IndexPath)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleInstallSelectedPackClicked()
{
	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	FString LastInstallRoot;
	FText LastError;
	if (!InstallSelectedPacks(EMaterialVaultInstallMode::Staging, SuccessCount, FailedCount, LastInstallRoot, LastError))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LastError.IsEmpty() ? FText::FromString(TEXT("加载到暂存区失败。")) : LastError);
		return FReply::Handled();
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		FText::FromString(TEXT("加载到暂存区完成。\n成功：{0}\n失败：{1}\n\n最后位置：\n{2}\n\n暂存资产会自动清理，但项目里任何关卡或模型正在使用的资产不会删除。")),
		FText::AsNumber(SuccessCount),
		FailedCount == 0 ? FText::FromString(TEXT("0")) : FText::Format(FText::FromString(TEXT("{0}\n\n最后一个错误：\n{1}")), FText::AsNumber(FailedCount), LastError),
		FText::FromString(LastInstallRoot)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleImportSelectedPackClicked()
{
	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	FString LastInstallRoot;
	FText LastError;
	if (!InstallSelectedPacks(EMaterialVaultInstallMode::Library, SuccessCount, FailedCount, LastInstallRoot, LastError))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LastError.IsEmpty() ? FText::FromString(TEXT("正式导入失败。")) : LastError);
		return FReply::Handled();
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		FText::FromString(TEXT("正式导入完成。\n成功：{0}\n失败：{1}\n\n最后位置：\n{2}\n\n这些资产已经保留到项目材质库，不会被暂存区自动清理。")),
		FText::AsNumber(SuccessCount),
		FailedCount == 0 ? FText::FromString(TEXT("0")) : FText::Format(FText::FromString(TEXT("{0}\n\n最后一个错误：\n{1}")), FText::AsNumber(FailedCount), LastError),
		FText::FromString(LastInstallRoot)));
	return FReply::Handled();
}
FReply SMaterialVaultWindow::HandleAutoClassifyPacksClicked()
{
	AutoClassifyPacks();
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleCreateCategoryClicked()
{
	TSharedPtr<SEditableTextBox> CategoryTextBox;
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("新建分类")))
		.ClientSize(FVector2D(360.0f, 120.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);
	Window->SetContent(
		SNew(SBorder).Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[SNew(STextBlock).Text(FText::FromString(TEXT("分类名称，子分类用 / 分隔，例如：石材/大理石")))]
			+ SVerticalBox::Slot().AutoHeight()[SAssignNew(CategoryTextBox, SEditableTextBox).Text(FText::FromString(TEXT("新分类")))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f).HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("创建")))
				.OnClicked_Lambda([this, Window, CategoryTextBox]()
				{
					FString Category = CategoryTextBox.IsValid() ? CategoryTextBox->GetText().ToString() : FString();
					Category = NormalizeCategoryPath(Category);
					if (!Category.IsEmpty() && Category != TEXT("未分类"))
					{
						IFileManager::Get().MakeDirectory(*FPaths::Combine(ResolveVaultRoot(), TEXT("Packs"), Category), true);
						LoadPackItems();
						if (CategoryListView.IsValid()) { CategoryListView->RequestTreeRefresh(); }
					}
					Window->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]);
	FSlateApplication::Get().AddWindow(Window);
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleDeleteCategoryClicked()
{
	const FString CategoryToDelete = NormalizeCategoryPath(SelectedCategory);
	if (CategoryToDelete.IsEmpty() || CategoryToDelete == TEXT("全部") || CategoryToDelete == TEXT("未分类"))
	{
		return FReply::Handled();
	}

	const EAppReturnType::Type Result = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::Format(
			FText::FromString(TEXT("确定删除分类“{0}”吗？\n\n分类里的材质包会移动到“未分类”，不会删除材质包文件。")),
			FText::FromString(CategoryToDelete)));
	if (Result != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const FString VaultRoot = ResolveVaultRoot();
	const FString PacksRoot = FPaths::Combine(VaultRoot, TEXT("Packs"));
	const FString CategoryRoot = FPaths::Combine(PacksRoot, CategoryToDelete);
	if (!FPaths::DirectoryExists(CategoryRoot))
	{
		SelectedCategory = TEXT("全部");
		LoadPackItems();
		return FReply::Handled();
	}

	TArray<FString> PackFiles;
	IFileManager::Get().FindFilesRecursive(PackFiles, *CategoryRoot, TEXT("*.mvpack"), true, false);
	int32 MovedCount = 0;
	for (const FString& PackFile : PackFiles)
	{
		const FString BaseName = FPaths::GetBaseFilename(PackFile);
		const FString Extension = FPaths::GetExtension(PackFile, true);
		FString TargetPath = FPaths::Combine(PacksRoot, FPaths::GetCleanFilename(PackFile));
		int32 Suffix = 1;
		while (FPaths::FileExists(TargetPath))
		{
			TargetPath = FPaths::Combine(PacksRoot, FString::Printf(TEXT("%s_%03d%s"), *BaseName, Suffix++, *Extension));
		}
		if (IFileManager::Get().Move(*TargetPath, *PackFile, true, true))
		{
			++MovedCount;
		}
	}

	IFileManager::Get().DeleteDirectory(*CategoryRoot, false, true);

	UMaterialVaultSettings* Settings = GetMutableDefault<UMaterialVaultSettings>();
	Settings->CategoryOrder.RemoveAll([&CategoryToDelete](const FString& SavedCategory)
	{
		const FString NormalizedSavedCategory = SMaterialVaultWindow::NormalizeCategoryPath(SavedCategory);
		return NormalizedSavedCategory == CategoryToDelete || NormalizedSavedCategory.StartsWith(CategoryToDelete + TEXT("/"));
	});
	Settings->SaveConfig();

	SelectedCategory = TEXT("全部");
	LoadPackItems();
	FString IndexPath;
	int32 PackCount = 0;
	FText Error;
	RebuildPackIndex(VaultRoot, IndexPath, PackCount, Error);

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(FText::FromString(TEXT("已删除分类“{0}”，移动了 {1} 个材质包到未分类。")), FText::FromString(CategoryToDelete), FText::AsNumber(MovedCount)));
	return FReply::Handled();
}

bool SMaterialVaultWindow::RepairMvpackThumbnailInCurrentProcess(const FString& PackPath, FText& OutError, bool bAllowRender)
{
	FMaterialVaultItem ManifestItem;
	if (!ExtractPackItemFromManifest(PackPath, ManifestItem))
	{
		OutError = FText::FromString(TEXT("材质包 manifest 读取失败，无法深度修复缩略图。"));
		return false;
	}

	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	FString StagingRelativeRoot = Settings->StagingMountRoot;
	StagingRelativeRoot.RemoveFromStart(TEXT("/Game/"));
	if (!StagingRelativeRoot.IsEmpty())
	{
		const FString StagingDirectory = FPaths::Combine(FPaths::ProjectContentDir(), StagingRelativeRoot);
		IFileManager::Get().DeleteDirectory(*StagingDirectory, false, true);
	}

	FString InstallRoot;
	if (!InstallMvpack(PackPath, InstallRoot, OutError, EMaterialVaultInstallMode::Staging))
	{
		return false;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	TArray<FString> PathsToScan;
	for (const FMaterialVaultAssetEntry& Entry : GetAllEntries(ManifestItem))
	{
		const FString ObjectPath = GetStagingObjectPath(Entry);
		if (!ObjectPath.IsEmpty())
		{
			PathsToScan.AddUnique(FPackageName::GetLongPackagePath(ObjectPath));
		}
	}
	if (!PathsToScan.IsEmpty())
	{
		AssetRegistry.ScanPathsSynchronous(PathsToScan, true);
		AssetRegistry.WaitForCompletion();
	}

	TArray<TPair<FString, FString>> ReplacementThumbnails;
	TSet<FString> UsedArchivePaths;
	auto MakeRepairArchivePath = [&UsedArchivePaths](const FMaterialVaultAssetEntry& Entry)
	{
		FString ArchivePath = Entry.ThumbnailPath;
		ArchivePath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (!ArchivePath.StartsWith(TEXT("thumbnails/")))
		{
			FString BaseName = FPaths::GetBaseFilename(Entry.PlannedPackageName);
			if (BaseName.IsEmpty())
			{
				BaseName = FPaths::GetBaseFilename(Entry.OriginalPackageName);
			}
			BaseName.ReplaceInline(TEXT("/"), TEXT("_"));
			BaseName.ReplaceInline(TEXT("\\"), TEXT("_"));
			BaseName.ReplaceInline(TEXT(":"), TEXT("_"));
			ArchivePath = FString::Printf(TEXT("thumbnails/%s.png"), *BaseName);
		}
		int32 Suffix = 2;
		const FString OriginalArchivePath = ArchivePath;
		while (UsedArchivePaths.Contains(ArchivePath))
		{
			ArchivePath = FString::Printf(TEXT("thumbnails/%s_%d.png"), *FPaths::GetBaseFilename(OriginalArchivePath), Suffix++);
		}
		UsedArchivePaths.Add(ArchivePath);
		return ArchivePath;
	};

	auto IsThumbnailRole = [](const FMaterialVaultAssetEntry& Entry)
	{
		return Entry.Role == EMaterialVaultAssetRole::RootMaterial ||
			Entry.Role == EMaterialVaultAssetRole::Material ||
			Entry.Role == EMaterialVaultAssetRole::MaterialInstance;
	};

	auto IsUtilityTextureName = [](const FString& TextureName)
	{
		return TextureName.Contains(TEXT("Normal"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("_N"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Rough"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Metallic"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Metalness"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Specular"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("AO"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("ARM"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("ORM"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Height"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Displace"), ESearchCase::IgnoreCase);
	};

	auto IsBaseColorTextureName = [](const FString& TextureName)
	{
		return TextureName.Contains(TEXT("BaseColor"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Base_Color"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Base Color"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Albedo"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Diffuse"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Color"), ESearchCase::IgnoreCase);
	};

	auto NormalizeMatchText = [](FString Value)
	{
		Value = Value.ToLower();
		Value.ReplaceInline(TEXT("mi_pbr_"), TEXT(""));
		Value.ReplaceInline(TEXT("mi_"), TEXT(""));
		Value.ReplaceInline(TEXT("m_"), TEXT(""));
		Value.ReplaceInline(TEXT("t_"), TEXT(""));
		Value.ReplaceInline(TEXT("basecolor"), TEXT(""));
		Value.ReplaceInline(TEXT("base_color"), TEXT(""));
		Value.ReplaceInline(TEXT("albedo"), TEXT(""));
		Value.ReplaceInline(TEXT("diffuse"), TEXT(""));
		Value.ReplaceInline(TEXT("color"), TEXT(""));
		for (TCHAR& Char : Value)
		{
			if (!FChar::IsAlnum(Char))
			{
				Char = TEXT(' ');
			}
		}
		return Value;
	};

	TArray<FMaterialVaultAssetEntry> AllRepairEntries = GetAllEntries(ManifestItem);
	auto FindBestTextureForEntry = [&AllRepairEntries, &IsUtilityTextureName, &IsBaseColorTextureName, &NormalizeMatchText](const FMaterialVaultAssetEntry& MaterialEntry)
	{
		FMaterialVaultAssetEntry BestTextureEntry;
		int32 BestScore = MIN_int32;
		const FString MaterialName = FPaths::GetBaseFilename(MaterialEntry.OriginalPackageName);
		const FString MaterialMatchText = NormalizeMatchText(MaterialName);
		TArray<FString> MaterialTokens;
		MaterialMatchText.ParseIntoArray(MaterialTokens, TEXT(" "), true);

		for (const FMaterialVaultAssetEntry& TextureEntry : AllRepairEntries)
		{
			if (TextureEntry.Role != EMaterialVaultAssetRole::Texture)
			{
				continue;
			}

			const FString TextureName = FPaths::GetBaseFilename(TextureEntry.OriginalPackageName);
			const FString TextureSearchText = NormalizeMatchText(TextureEntry.OriginalPackageName + TEXT(" ") + TextureEntry.PlannedPackageName);
			int32 Score = 0;
			if (IsBaseColorTextureName(TextureName))
			{
				Score += 1000;
			}
			if (IsUtilityTextureName(TextureName))
			{
				Score -= 500;
			}
			for (const FString& Token : MaterialTokens)
			{
				if (Token.Len() >= 3 && TextureSearchText.Contains(Token))
				{
					Score += 80;
				}
			}
			if (TextureSearchText.Contains(MaterialMatchText) && !MaterialMatchText.IsEmpty())
			{
				Score += 400;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				BestTextureEntry = TextureEntry;
			}
		}
		return BestScore > MIN_int32 ? BestTextureEntry : FMaterialVaultAssetEntry();
	};

	bool bHadReplacementThumbnail = false;
	auto GenerateReplacementThumbnail = [&](FMaterialVaultAssetEntry& ManifestEntry)
	{
		if (!IsThumbnailRole(ManifestEntry))
		{
			return true;
		}

		FMaterialVaultAssetEntry StagingEntry = ManifestEntry;
		StagingEntry.OriginalObjectPath = FSoftObjectPath(GetStagingObjectPath(ManifestEntry));
		StagingEntry.OriginalPackageName = GetStagingPackageName(ManifestEntry);
		if (StagingEntry.OriginalObjectPath.IsNull())
		{
			return false;
		}

		const FString ArchivePath = MakeRepairArchivePath(ManifestEntry);
		FString TempThumbnailPath;
		FText ThumbnailError;
		const FMaterialVaultAssetEntry RepresentativeTextureEntry = FindBestTextureForEntry(ManifestEntry);
		bool bCreatedThumbnail = false;
		if (ManifestEntry.Role == EMaterialVaultAssetRole::MaterialInstance && !RepresentativeTextureEntry.OriginalObjectPath.IsNull())
		{
			FMaterialVaultAssetEntry StagingTextureEntry = RepresentativeTextureEntry;
			StagingTextureEntry.OriginalObjectPath = FSoftObjectPath(GetStagingObjectPath(RepresentativeTextureEntry));
			StagingTextureEntry.OriginalPackageName = GetStagingPackageName(RepresentativeTextureEntry);
			bCreatedThumbnail = CreateAssetThumbnail(StagingTextureEntry, FPaths::GetCleanFilename(ArchivePath), TempThumbnailPath, ThumbnailError, bAllowRender);
		}
		if (!bCreatedThumbnail)
		{
			bCreatedThumbnail = CreateAssetThumbnail(StagingEntry, FPaths::GetCleanFilename(ArchivePath), TempThumbnailPath, ThumbnailError, bAllowRender);
		}
		if (!bCreatedThumbnail)
		{
			if (RepresentativeTextureEntry.OriginalObjectPath.IsNull())
			{
				OutError = ThumbnailError;
				return false;
			}

			FMaterialVaultAssetEntry StagingTextureEntry = RepresentativeTextureEntry;
			StagingTextureEntry.OriginalObjectPath = FSoftObjectPath(GetStagingObjectPath(RepresentativeTextureEntry));
			StagingTextureEntry.OriginalPackageName = GetStagingPackageName(RepresentativeTextureEntry);
			if (!CreateAssetThumbnail(StagingTextureEntry, FPaths::GetCleanFilename(ArchivePath), TempThumbnailPath, ThumbnailError, bAllowRender))
			{
				OutError = ThumbnailError;
				return false;
			}
		}

		ManifestEntry.ThumbnailPath = ArchivePath;
		ReplacementThumbnails.Add(TPair<FString, FString>(ArchivePath, TempThumbnailPath));
		bHadReplacementThumbnail = true;
		if (!ReplacementThumbnails.ContainsByPredicate([](const TPair<FString, FString>& Pair) { return Pair.Key == TEXT("thumbnail.png"); }))
		{
			ReplacementThumbnails.Add(TPair<FString, FString>(TEXT("thumbnail.png"), TempThumbnailPath));
		}
		return true;
	};

	GenerateReplacementThumbnail(ManifestItem.RootAsset);
	for (FMaterialVaultAssetEntry& Dependency : ManifestItem.Dependencies)
	{
		GenerateReplacementThumbnail(Dependency);
	}

	if (!bHadReplacementThumbnail)
	{
		if (OutError.IsEmpty())
		{
			OutError = FText::FromString(TEXT("安装并渲染后仍未得到有效材质球缩略图。"));
		}
		return false;
	}

	const FString PackRelativePath = MakeVaultRelativePath(PackPath, ResolveVaultRoot());
	const FString ManifestJson = BuildManifestJson(ManifestItem, ResolveVaultRoot(), PackRelativePath);
	const FString TempManifestPath = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("MaterialVault"), TEXT("Repair"), ManifestItem.Id + TEXT("_manifest.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TempManifestPath), true);
	if (!FFileHelper::SaveStringToFile(ManifestJson, *TempManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FText::FromString(TEXT("无法写入深度修复用的 manifest.json。"));
		return false;
	}
	ReplacementThumbnails.Add(TPair<FString, FString>(TEXT("manifest.json"), TempManifestPath));

	if (!RewritePackThumbnails(PackPath, ReplacementThumbnails, OutError))
	{
		return false;
	}

	ClearPackThumbnailCache(ManifestItem.Id);
	CollectGarbage(RF_NoFlags);
	if (!StagingRelativeRoot.IsEmpty())
	{
		const FString StagingDirectory = FPaths::Combine(FPaths::ProjectContentDir(), StagingRelativeRoot);
		IFileManager::Get().DeleteDirectory(*StagingDirectory, false, true);
	}
	return true;
}

bool SMaterialVaultWindow::RepairPackThumbnailsInternal(const TArray<TSharedPtr<FMaterialVaultPackItem>>& Targets, int32& OutSuccessCount, int32& OutFailedCount, FText& OutLastError)
{
	OutSuccessCount = 0;
	OutFailedCount = 0;
	OutLastError = FText::GetEmpty();

	TMap<FString, TSharedPtr<FMaterialVaultPackItem>> UniqueTargetsByPackPath;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : Targets)
	{
		if (PackItem.IsValid() && !PackItem->AbsolutePath.IsEmpty())
		{
			UniqueTargetsByPackPath.FindOrAdd(PackItem->AbsolutePath, PackItem);
		}
	}

	TArray<TSharedPtr<FMaterialVaultPackItem>> UniqueTargets;
	UniqueTargetsByPackPath.GenerateValueArray(UniqueTargets);

	FScopedSlowTask SlowTask(static_cast<float>(UniqueTargets.Num()), FText::FromString(TEXT("正在分批深度修复材质包缩略图...")));
	SlowTask.MakeDialog(true);

	int32 ProcessedCount = 0;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : UniqueTargets)
	{
		if (!PackItem.IsValid())
		{
			continue;
		}

		if (SlowTask.ShouldCancel())
		{
			break;
		}

		++ProcessedCount;
		SlowTask.EnterProgressFrame(1.0f, FText::Format(
			FText::FromString(TEXT("正在外部 UE 进程修复 {0}/{1}：{2}")),
			FText::AsNumber(ProcessedCount),
			FText::AsNumber(UniqueTargets.Num()),
			FText::FromString(PackItem->DisplayName)));

		FText Error;
		if (RunExternalPackThumbnailRepair(PackItem->AbsolutePath, Error))
		{
			const FString PackId = PackItem->SourcePackId.IsEmpty() ? PackItem->Id : PackItem->SourcePackId;
			ClearPackThumbnailCache(PackId);
			++OutSuccessCount;
			if (ShouldRunPeriodicGC(ProcessedCount))
			{
				CollectGarbage(RF_NoFlags);
			}
		}
		else
		{
			++OutFailedCount;
			OutLastError = Error;
		}
	}

	return OutFailedCount == 0;
}

bool SMaterialVaultWindow::ExtractPackThumbnailsInternal(const TArray<TSharedPtr<FMaterialVaultPackItem>>& Targets, int32& OutSuccessCount, int32& OutFailedCount, FText& OutLastError)
{
	OutSuccessCount = 0;
	OutFailedCount = 0;
	OutLastError = FText::GetEmpty();

	FScopedSlowTask SlowTask(static_cast<float>(Targets.Num()), FText::FromString(TEXT("\u6b63\u5728\u4ece .mvpack \u8bfb\u53d6\u7f29\u7565\u56fe...")));
	SlowTask.MakeDialog(true);

	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : Targets)
	{
		if (!PackItem.IsValid())
		{
			continue;
		}

		SlowTask.EnterProgressFrame(1.0f, FText::FromString(PackItem->DisplayName));

		FString CachedThumbnailPath;
		if (ExtractPackThumbnail(PackItem->AbsolutePath, PackItem->Id, CachedThumbnailPath))
		{
			PackItem->ThumbnailPath = CachedThumbnailPath;
			PackItem->ThumbnailBrush.Reset();
			++OutSuccessCount;
		}
		else
		{
			++OutFailedCount;
			OutLastError = FText::Format(
				FText::FromString(TEXT("\u65e0\u6cd5\u4ece\u6750\u8d28\u5305\u8bfb\u53d6\u6709\u6548\u7f29\u7565\u56fe\uff1a\n{0}")),
				FText::FromString(PackItem->AbsolutePath));
		}
	}

	return OutFailedCount == 0;
}

FReply SMaterialVaultWindow::HandleRepairPackThumbnailsClicked()
{
	if (PackItems.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("当前材质库里没有需要修复的材质族包。")));
		return FReply::Handled();
	}

	const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(
		EAppMsgType::OkCancel,
		FText::Format(
			FText::FromString(TEXT("将深度修复当前材质库里 {0} 个材质族包的缩略图。\n\n插件会逐包启动外部 UE 进程：安装 .mvpack、渲染材质球缩略图、写回 .mvpack。不会在当前编辑器里一口气渲染全部包；单个包失败也不会关闭当前 UE。是否继续？")),
			FText::AsNumber(PackItems.Num())));
	if (ConfirmResult != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	FText LastError;
	RepairPackThumbnailsInternal(PackItems, SuccessCount, FailedCount, LastError);

	FString IndexPath;
	int32 PackCount = 0;
	FText IndexError;
	RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, IndexError);
	LoadPackItems();

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			FText::FromString(TEXT("缩略图修复完成。\n成功：{0}\n失败：{1}")),
			FText::AsNumber(SuccessCount),
			FailedCount == 0
				? FText::FromString(TEXT("0"))
				: FText::Format(FText::FromString(TEXT("{0}\n\n最后一个错误：\n{1}")), FText::AsNumber(FailedCount), LastError)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleRepairSelectedPackThumbnails()
{
	TArray<TSharedPtr<FMaterialVaultPackItem>> SelectedPacks;
	if (PackListView.IsValid())
	{
		SelectedPacks = PackListView->GetSelectedItems();
	}
	if (SelectedPacks.IsEmpty() && SelectedPackItem.IsValid())
	{
		SelectedPacks.Add(SelectedPackItem);
	}
	if (SelectedPacks.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("请先在材质包库里选中一个或多个材质包。")));
		return FReply::Handled();
	}

	const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(
		EAppMsgType::OkCancel,
		FText::Format(
			FText::FromString(TEXT("将深度修复 {0} 个选中材质包的缩略图。\n\n插件会逐包启动外部 UE 进程安装并渲染，再写回 .mvpack。是否继续？")),
			FText::AsNumber(SelectedPacks.Num())));
	if (ConfirmResult != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	TArray<TSharedPtr<FMaterialVaultPackItem>> Targets = SelectedPacks;
	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	FText LastError;
	RepairPackThumbnailsInternal(Targets, SuccessCount, FailedCount, LastError);

	FString IndexPath;
	int32 PackCount = 0;
	FText IndexError;
	RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, IndexError);
	LoadPackItems();

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			FText::FromString(TEXT("缩略图修复完成。\n成功：{0}\n失败：{1}")),
			FText::AsNumber(SuccessCount),
			FailedCount == 0
				? FText::FromString(TEXT("0"))
				: FText::Format(FText::FromString(TEXT("{0}\n\n最后一个错误：\n{1}")), FText::AsNumber(FailedCount), LastError)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleRepairAllBrokenThumbnails()
{
	TArray<TSharedPtr<FMaterialVaultPackItem>> BrokenPacks;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : PackItems)
	{
		if (IsPackThumbnailBroken(PackItem))
		{
			BrokenPacks.Add(PackItem);
		}
	}

	if (BrokenPacks.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("所有材质包的缩略图都正常，无需修复。")));
		return FReply::Handled();
	}

	const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(
		EAppMsgType::OkCancel,
		FText::Format(
			FText::FromString(TEXT("检测到 {0} 个材质包的缩略图异常（缺失或显示为棋盘格）。\n将逐包启动外部 UE 进程安装并渲染缩略图，再写回 .mvpack。是否继续？")),
			FText::AsNumber(BrokenPacks.Num())));
	if (ConfirmResult != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	FText LastError;
	RepairPackThumbnailsInternal(BrokenPacks, SuccessCount, FailedCount, LastError);

	FString IndexPath;
	int32 PackCount = 0;
	FText IndexError;
	RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, IndexError);
	LoadPackItems();

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			FText::FromString(TEXT("缩略图修复完成。\n成功：{0}\n失败：{1}")),
			FText::AsNumber(SuccessCount),
			FailedCount == 0
				? FText::FromString(TEXT("0"))
				: FText::Format(FText::FromString(TEXT("{0}\n\n最后一个错误：\n{1}")), FText::AsNumber(FailedCount), LastError)));
	return FReply::Handled();
}

TSharedRef<SWidget> SMaterialVaultWindow::BuildRepairThumbnailsMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("正常修复所选缩略图")),
		FText::FromString(TEXT("从 .mvpack 读取内置缩略图，保存到材质库 Thumbnails 缓存")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleExtractSelectedPackThumbnails(); })));
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("正常修复显示异常的缩略图")),
		FText::FromString(TEXT("只从 .mvpack 里读取一次缩略图，不重新渲染")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleExtractAllBrokenThumbnails(); })));
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("正常修复全部缩略图")),
		FText::FromString(TEXT("从所有 .mvpack 读取缩略图并重建对应索引")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleExtractAllPackThumbnails(); })));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("深度修复所选缩略图")),
		FText::FromString(TEXT("逐包启动外部 UE 进程安装并渲染，写回 .mvpack")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleRepairSelectedPackThumbnails(); })));
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("深度修复显示异常的缩略图")),
		FText::FromString(TEXT("只对异常包逐包安装并渲染，失败不关闭当前 UE")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleRepairAllBrokenThumbnails(); })));
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("深度修复全部缩略图")),
		FText::FromString(TEXT("逐包外部渲染全部材质包缩略图并写回")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleRepairPackThumbnailsClicked(); })));
	return MenuBuilder.MakeWidget();
}

FReply SMaterialVaultWindow::HandleCleanStagingClicked() const
{
	const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(TEXT("将扫描整个项目的所有关卡、模型和资产引用，只删除没有被项目使用的暂存资产。\n\n材质包库和正式导入到项目材质库的资产不会被删除。是否继续？")));
	if (ConfirmResult != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	{
		FScopedSlowTask CountdownTask(3.0f, FText::FromString(TEXT("清理暂存区将在 3 秒后开始...")));
		CountdownTask.MakeDialog(true);
		for (int32 SecondsLeft = 3; SecondsLeft >= 1; --SecondsLeft)
		{
			if (CountdownTask.ShouldCancel())
			{
				return FReply::Handled();
			}
			CountdownTask.EnterProgressFrame(1.0f, FText::Format(
				FText::FromString(TEXT("{0} 秒后开始清理。只删除未使用暂存，已在项目中使用的资产会保留并继续可用。")),
				FText::AsNumber(SecondsLeft)));
			FPlatformProcess::Sleep(1.0f);
		}
	}

	FText Error;
	FMaterialVaultStagingCleanupStats Stats;
	if (!CleanupStagingArea(true, Error, &Stats))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			FText::FromString(TEXT("未使用暂存资产清理完成。\n扫描资产：{0}\n项目已使用并保留：{1}\n删除资产：{2}\n跳过资产：{3}\n删除空文件夹：{4}\n\n材质包库里的 .mvpack 不会被删除。")),
			FText::AsNumber(Stats.ScannedAssets),
			FText::AsNumber(Stats.UsedAssets),
			FText::AsNumber(Stats.DeletedAssets),
			FText::AsNumber(Stats.SkippedAssets),
			FText::AsNumber(Stats.DeletedFolders)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleExtractSelectedPackThumbnails()
{
	TArray<TSharedPtr<FMaterialVaultPackItem>> SelectedPacks;
	if (PackListView.IsValid())
	{
		SelectedPacks = PackListView->GetSelectedItems();
	}
	if (SelectedPacks.IsEmpty() && SelectedPackItem.IsValid())
	{
		SelectedPacks.Add(SelectedPackItem);
	}
	if (SelectedPacks.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("\u8bf7\u5148\u5728\u6750\u8d28\u5305\u5e93\u91cc\u9009\u4e2d\u4e00\u4e2a\u6216\u591a\u4e2a\u6750\u8d28\u5305\u3002")));
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	FText LastError;
	ExtractPackThumbnailsInternal(SelectedPacks, SuccessCount, FailedCount, LastError);

	FString IndexPath;
	int32 PackCount = 0;
	FText IndexError;
	RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, IndexError);
	LoadPackItems();

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			FText::FromString(TEXT("\u7f29\u7565\u56fe\u8bfb\u53d6\u5b8c\u6210\u3002\n\u6210\u529f\uff1a{0}\n\u5931\u8d25\uff1a{1}")),
			FText::AsNumber(SuccessCount),
			FailedCount == 0
				? FText::FromString(TEXT("0"))
				: FText::Format(FText::FromString(TEXT("{0}\n\n\u6700\u540e\u4e00\u4e2a\u9519\u8bef\uff1a\n{1}")), FText::AsNumber(FailedCount), LastError)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleExtractAllBrokenThumbnails()
{
	TArray<TSharedPtr<FMaterialVaultPackItem>> BrokenPacks;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : PackItems)
	{
		if (IsPackThumbnailBroken(PackItem))
		{
			BrokenPacks.Add(PackItem);
		}
	}

	if (BrokenPacks.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("\u6240\u6709\u6750\u8d28\u5305\u7684\u7f29\u7565\u56fe\u90fd\u5df2\u6709\u53ef\u7528\u7f13\u5b58\u3002")));
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	FText LastError;
	ExtractPackThumbnailsInternal(BrokenPacks, SuccessCount, FailedCount, LastError);

	FString IndexPath;
	int32 PackCount = 0;
	FText IndexError;
	RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, IndexError);
	LoadPackItems();

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(FText::FromString(TEXT("\u5f02\u5e38\u7f29\u7565\u56fe\u8bfb\u53d6\u5b8c\u6210\u3002\n\u6210\u529f\uff1a{0}\n\u5931\u8d25\uff1a{1}")), FText::AsNumber(SuccessCount), FText::AsNumber(FailedCount)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleExtractAllPackThumbnails()
{
	if (PackItems.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("\u5f53\u524d\u6750\u8d28\u5e93\u91cc\u6ca1\u6709\u6750\u8d28\u5305\u3002")));
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	FText LastError;
	ExtractPackThumbnailsInternal(PackItems, SuccessCount, FailedCount, LastError);

	FString IndexPath;
	int32 PackCount = 0;
	FText IndexError;
	RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, IndexError);
	LoadPackItems();

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(FText::FromString(TEXT("\u5168\u90e8\u7f29\u7565\u56fe\u8bfb\u53d6\u5b8c\u6210\u3002\n\u6210\u529f\uff1a{0}\n\u5931\u8d25\uff1a{1}")), FText::AsNumber(SuccessCount), FText::AsNumber(FailedCount)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleOpenStagingSettingsClicked() const
{
	UMaterialVaultSettings* Settings = GetMutableDefault<UMaterialVaultSettings>();
	TSharedRef<SWindow> SettingsWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("材质库设置")))
		.ClientSize(FVector2D(460.0f, 300.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	SettingsWindow->SetContent(
		SNew(SBorder)
		.Padding(14.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([]() { return FText::Format(FText::FromString(TEXT("材质库总目录：{0}")), FText::FromString(SMaterialVaultWindow::ResolveVaultBaseRoot())); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("选择材质库目录")))
				.OnClicked(this, &SMaterialVaultWindow::HandleChooseVaultRootClicked)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("暂存区最大容量 GB")))]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<float>).MinValue(0.5f).Value_Lambda([Settings]() { return static_cast<float>(Settings->StagingMaxSizeMB) / 1024.0f; }).OnValueCommitted_Lambda([Settings](float NewValue, ETextCommit::Type) { Settings->StagingMaxSizeMB = FMath::Max(512, FMath::RoundToInt(NewValue * 1024.0f)); Settings->SaveConfig(); })]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("暂存区保留天数")))]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<int32>).MinValue(0).Value_Lambda([Settings]() { return Settings->StagingKeepDays; }).OnValueCommitted_Lambda([Settings](int32 NewValue, ETextCommit::Type) { Settings->StagingKeepDays = FMath::Max(0, NewValue); Settings->SaveConfig(); })]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[SNew(SCheckBox).IsChecked_Lambda([Settings]() { return Settings->bCleanStagingAfterPack ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([Settings](ECheckBoxState State) { Settings->bCleanStagingAfterPack = State == ECheckBoxState::Checked; Settings->SaveConfig(); })[SNew(STextBlock).Text(FText::FromString(TEXT("生成材质包后自动删除未使用暂存资产")))]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[SNew(SCheckBox).IsChecked_Lambda([Settings]() { return Settings->bAutoCleanStagingWhenFull ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([Settings](ECheckBoxState State) { Settings->bAutoCleanStagingWhenFull = State == ECheckBoxState::Checked; Settings->SaveConfig(); })[SNew(STextBlock).Text(FText::FromString(TEXT("暂存区超过容量时只自动删除未使用资产")))]]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox).IsChecked_Lambda([Settings]() { return Settings->bSkipExistingPacks ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([Settings](ECheckBoxState State) { Settings->bSkipExistingPacks = State == ECheckBoxState::Checked; Settings->SaveConfig(); })[SNew(STextBlock).Text(FText::FromString(TEXT("生成时跳过已经存在的 .mvpack")))]]
		]
	);

	FSlateApplication::Get().AddWindow(SettingsWindow);
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleScanExternalProjectClicked() const
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("无法打开文件选择窗口。")));
		return FReply::Handled();
	}

	TArray<FString> SelectedFiles;
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bSelected = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("选择要扫描的 Unreal 项目"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Unreal Project (*.uproject)|*.uproject"),
		EFileDialogFlags::None,
		SelectedFiles);

	if (!bSelected || SelectedFiles.IsEmpty())
	{
		return FReply::Handled();
	}

	const FString ProjectFilePath = SelectedFiles[0];
	const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(
		EAppMsgType::OkCancel,
		FText::Format(
			FText::FromString(TEXT("将后台打开这个项目执行材质扫描：\n{0}\n\n扫描会临时加载当前插件，不会把插件复制到目标项目，也会跳过 MaterialVault 已生成的暂存/库内容。是否继续？")),
			FText::FromString(ProjectFilePath)));
	if (ConfirmResult != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	FString OutputPath;
	FText Error;
	{
		FScopedSlowTask SlowTask(3.0f, FText::FromString(TEXT("正在后台扫描外部项目...")));
		SlowTask.MakeDialog(true);
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("准备目标项目...")));
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("启动后台 Unreal Editor...")));
		if (!RunExternalProjectScan(ProjectFilePath, OutputPath, Error))
		{
			FMessageDialog::Open(EAppMsgType::Ok, Error);
			return FReply::Handled();
		}
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("写入扫描结果...")));
	}

	if (OutputPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("外部项目扫描没有生成结果文件。")));
		return FReply::Handled();
	}

	const_cast<SMaterialVaultWindow*>(this)->LoadPackItems();
	if (PackListView.IsValid())
	{
		PackListView->RequestListRefresh();
	}
	if (CategoryListView.IsValid())
	{
		CategoryListView->RequestTreeRefresh();
	}

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(
			FText::FromString(TEXT("外部项目扫描并生成材质包完成。\n结果文件：\n{0}\n\n材质包列表已刷新。")),
			FText::FromString(OutputPath)));
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleExportManifestClicked() const
{
	if (!ListView.IsValid())
	{
		return FReply::Handled();
	}

	TArray<TSharedPtr<FMaterialVaultItem>> SelectedItems = ListView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("请先选择一个或多个材质。")));
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	FString LastFilePath;
	FText LastError;
	for (const TSharedPtr<FMaterialVaultItem>& Item : SelectedItems)
	{
		if (!Item.IsValid())
		{
			continue;
		}

		FString FilePath;
		FText Error;
		if (ExportManifest(*Item, FilePath, Error))
		{
			++SuccessCount;
			LastFilePath = FilePath;
		}
		else
		{
			LastError = Error;
		}
	}

	if (SuccessCount > 0)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				FText::FromString(TEXT("已导出 {0} 个收录清单。\n最后一个文件：\n{1}")),
				FText::AsNumber(SuccessCount),
				FText::FromString(LastFilePath)));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, LastError.IsEmpty() ? FText::FromString(TEXT("导出失败。")) : LastError);
	}

	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleCollectToStagingClicked() const
{
	if (!ListView.IsValid())
	{
		return FReply::Handled();
	}

	const TArray<TSharedPtr<FMaterialVaultItem>> SelectedItems = ListView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("请先选择一个或多个材质。")));
		return FReply::Handled();
	}

	const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(
		EAppMsgType::OkCancel,
		FText::FromString(TEXT("将把选中材质和依赖复制到 /Game/__MaterialVaultStaging，并保存暂存资产。原始资产不会被修改。是否继续？")));
	if (ConfirmResult != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	FString LastRootPath;
	FText LastError;
	for (const TSharedPtr<FMaterialVaultItem>& Item : SelectedItems)
	{
		if (!Item.IsValid())
		{
			continue;
		}


		FString RootPath;
		FText Error;
		if (CollectToStaging(*Item, RootPath, Error))
		{
			++SuccessCount;
			LastRootPath = RootPath;

			FString ManifestPath;
			FText ManifestError;
			ExportManifest(*Item, ManifestPath, ManifestError);
		}
		else
		{
			LastError = Error;
		}
	}

	if (SuccessCount > 0)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				FText::FromString(TEXT("已收录 {0} 个材质到暂存区。\n最后一个根资产：\n{1}")),
				FText::AsNumber(SuccessCount),
				FText::FromString(LastRootPath)));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, LastError.IsEmpty() ? FText::FromString(TEXT("收录失败。")) : LastError);
	}

	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleCreateSelectedMvpackClicked() const
{
	TArray<TSharedPtr<FMaterialVaultItem>> SelectedItems;
	if (ScanResultsListView.IsValid())
	{
		SelectedItems = ScanResultsListView->GetSelectedItems();
	}

	if (SelectedItems.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("请先选择一个或多个材质。")));
		return FReply::Handled();
	}

	return CreateMvpackForItems(
		SelectedItems,
		FText::FromString(TEXT("将为选中的材质生成 .mvpack。插件会先收录到暂存区，再写入材质包。原始资产不会被修改。是否继续？")));
}

FReply SMaterialVaultWindow::HandleCreateAllMvpackClicked() const
{
	if (Items.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("当前没有可生成的材质，请先扫描项目。")));
		return FReply::Handled();
	}

	return CreateMvpackForItems(
		Items,
		FText::Format(
			FText::FromString(TEXT("将为当前列表中的全部 {0} 个材质生成 .mvpack。这个过程可能需要较长时间。原始资产不会被修改。是否继续？")),
			FText::AsNumber(Items.Num())));
}

FReply SMaterialVaultWindow::CreateMvpackForItems(const TArray<TSharedPtr<FMaterialVaultItem>>& ItemsToCreate, const FText& ConfirmText) const
{
	return CreateMvpackForItems(ItemsToCreate, ConfirmText, FString());
}

FReply SMaterialVaultWindow::CreateMvpackForItems(const TArray<TSharedPtr<FMaterialVaultItem>>& ItemsToCreate, const FText& ConfirmText, const FString& OverrideCategory) const
{
	if (ItemsToCreate.IsEmpty())
	{
		return FReply::Handled();
	}

	const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(EAppMsgType::OkCancel, ConfirmText);
	if (ConfirmResult != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	constexpr bool bUseBackgroundPackGeneration = false;
	if (bUseBackgroundPackGeneration && !IsRunningCommandlet() && OverrideCategory.IsEmpty())
	{
		TArray<FString> OnlyItemIds;
		OnlyItemIds.Reserve(ItemsToCreate.Num());
		for (const TSharedPtr<FMaterialVaultItem>& Item : ItemsToCreate)
		{
			if (Item.IsValid() && !Item->Id.IsEmpty())
			{
				OnlyItemIds.AddUnique(Item->Id);
			}
		}

		FString OutputPath;
		FText Error;
		if (!RunExternalProjectScan(FPaths::GetProjectFilePath(), OutputPath, Error, &OnlyItemIds))
		{
			FMessageDialog::Open(EAppMsgType::Ok, Error);
			return FReply::Handled();
		}

		FString IndexPath;
		int32 PackCount = 0;
		FText IndexError;
		RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, IndexError);
		const_cast<SMaterialVaultWindow*>(this)->LoadPackItems();
		if (PackListView.IsValid())
		{
			PackListView->RequestListRefresh();
		}
		if (CategoryListView.IsValid())
		{
			CategoryListView->RequestTreeRefresh();
		}

		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				FText::FromString(TEXT("后台生成材质包完成。\n请求生成：{0}\n结果文件：\n{1}\n\n材质包列表已刷新。")),
				FText::AsNumber(OnlyItemIds.Num()),
				FText::FromString(OutputPath)));
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	int32 SkippedCount = 0;
	FString LastPackPath;
	FText LastError;
	constexpr float CollectBudget = 3.0f;
	constexpr float PackBudget = 5.0f;
	constexpr float PerItemBudget = CollectBudget + PackBudget;
	const int32 ItemCount = ItemsToCreate.Num();
	FScopedSlowTask SlowTask(static_cast<float>(ItemCount) * PerItemBudget, FText::FromString(TEXT("正在生成材质包...")));
	SlowTask.MakeDialog(true);

	int32 ItemIndex = 0;
	int32 ItemsSinceLastGC = 0;
	for (const TSharedPtr<FMaterialVaultItem>& Item : ItemsToCreate)
	{
		++ItemIndex;
		if (!Item.IsValid())
		{
			SlowTask.EnterProgressFrame(PerItemBudget);
			continue;
		}

		const FString ExistingPackPath = GetPackPathForItem(*Item);
		if (GetDefault<UMaterialVaultSettings>()->bSkipExistingPacks && FPaths::FileExists(ExistingPackPath))
		{
			FText CleanupError;
			if (!CleanupStagingForItem(*Item, CleanupError, true))
			{
				UE_LOG(LogTemp, Warning, TEXT("Material Vault staging cleanup failed for existing pack: %s"), *CleanupError.ToString());
			}
			++SkippedCount;
			LastPackPath = ExistingPackPath;
			SlowTask.EnterProgressFrame(PerItemBudget, FText::Format(
				FText::FromString(TEXT("({0}/{1}) 已存在，跳过：{2}")),
				FText::AsNumber(ItemIndex), FText::AsNumber(ItemCount),
				FText::FromString(Item->DisplayName)));
			continue;
		}

		FString RootPath;
		FText Error;
		FMaterialVaultItem ItemToCreate = *Item;
		if (!OverrideCategory.IsEmpty())
		{
			ItemToCreate.Category = NormalizeCategoryPath(OverrideCategory);
			FMaterialVaultScanner CategoryScanner;
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			const FAssetData RootAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ItemToCreate.RootAsset.OriginalObjectPath);
			if (RootAssetData.IsValid())
			{
				ItemToCreate.RootAsset = CategoryScanner.BuildEntry(RootAssetData, ItemToCreate.Category, ItemToCreate.Id, true);
			}
		}
		FMaterialVaultScanner Scanner;
		Scanner.PopulateDependencies(ItemToCreate);

		SlowTask.EnterProgressFrame(0.0f, FText::Format(
			FText::FromString(TEXT("({0}/{1}) 收录到暂存区：{2}")),
			FText::AsNumber(ItemIndex), FText::AsNumber(ItemCount),
			FText::FromString(Item->DisplayName)));
		if (!CollectToStaging(ItemToCreate, RootPath, Error, &SlowTask, CollectBudget))
		{
			LastError = Error;
			SlowTask.EnterProgressFrame(PerItemBudget - CollectBudget);
			continue;
		}

		FString PackPath;
		SlowTask.EnterProgressFrame(0.0f, FText::Format(
			FText::FromString(TEXT("({0}/{1}) 写入 .mvpack：{2}")),
			FText::AsNumber(ItemIndex), FText::AsNumber(ItemCount),
			FText::FromString(Item->DisplayName)));
		if (CreateMvpack(ItemToCreate, PackPath, Error, &SlowTask, PackBudget))
		{
			FText CleanupError;
			if (!CleanupStagingForItem(ItemToCreate, CleanupError, true))
			{
				UE_LOG(LogTemp, Warning, TEXT("Material Vault staging cleanup failed after pack generation: %s"), *CleanupError.ToString());
			}
			++SuccessCount;
			LastPackPath = PackPath;
		}
		else
		{
			LastError = Error;
		}

		++ItemsSinceLastGC;
		if (ShouldRunPeriodicGC(ItemsSinceLastGC))
		{
			FAssetCompilingManager::Get().ProcessAsyncTasks(false);
			CollectGarbage(RF_NoFlags);
			FPlatformProcess::Sleep(0.08f);
			ItemsSinceLastGC = 0;
		}
	}

	FString IndexPath;
	int32 PackCount = 0;
	FText IndexError;
	RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, IndexError);
	const_cast<SMaterialVaultWindow*>(this)->LoadPackItems();
	if (PackListView.IsValid())
	{
		PackListView->RequestListRefresh();
	}
	if (CategoryListView.IsValid())
	{
		CategoryListView->RequestTreeRefresh();
	}
	FAssetCompilingManager::Get().ProcessAsyncTasks(false);

	if (SuccessCount > 0 || SkippedCount > 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			FText::FromString(TEXT("生成完成。\n新生成：{0}\n已存在跳过：{1}\n最后一个材质包：\n{2}")),
			FText::AsNumber(SuccessCount),
			FText::AsNumber(SkippedCount),
			FText::FromString(LastPackPath)));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, LastError.IsEmpty() ? FText::FromString(TEXT("生成材质包失败。")) : LastError);
	}

	return FReply::Handled();
}

bool SMaterialVaultWindow::GenerateMvpackForItem(const FMaterialVaultItem& Item, FString& OutPackPath, FText& OutError, bool bCleanupStaging)
{
	FMaterialVaultItem ItemToGenerate = Item;
	OutPackPath = GetPackPathForItem(ItemToGenerate);
	if (GetDefault<UMaterialVaultSettings>()->bSkipExistingPacks && FPaths::FileExists(OutPackPath))
	{
		if (bCleanupStaging)
		{
			FText CleanupError;
			if (!CleanupStagingForItem(ItemToGenerate, CleanupError, true))
			{
				UE_LOG(LogTemp, Warning, TEXT("Material Vault staging cleanup failed for existing pack: %s"), *CleanupError.ToString());
			}
		}
		return true;
	}

	FMaterialVaultScanner Scanner;
	Scanner.PopulateDependencies(ItemToGenerate);

	FString RootPath;
	if (!CollectToStaging(ItemToGenerate, RootPath, OutError))
	{
		return false;
	}

	const bool bCreated = CreateMvpack(ItemToGenerate, OutPackPath, OutError);
	if (bCreated && bCleanupStaging)
	{
		FText CleanupError;
		if (!CleanupStagingForItem(ItemToGenerate, CleanupError, true))
		{
			UE_LOG(LogTemp, Warning, TEXT("Material Vault staging cleanup failed after pack generation: %s"), *CleanupError.ToString());
		}
	}
	if (IsRunningCommandlet() || ShouldRunPeriodicGC(1))
	{
		FAssetCompilingManager::Get().ProcessAsyncTasks(false);
		CollectGarbage(RF_NoFlags);
		FPlatformProcess::Sleep(IsRunningCommandlet() ? 0.08f : 0.04f);
	}
	return bCreated;
}

FReply SMaterialVaultWindow::HandleInstallMvpackClicked() const
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("无法打开文件选择窗口。")));
		return FReply::Handled();
	}

	TArray<FString> SelectedFiles;
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bSelected = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("选择要加载到暂存区的材质包"),
		FPaths::Combine(ResolveVaultRoot(), TEXT("Packs")),
		TEXT(""),
		TEXT("Material Vault Pack (*.mvpack)|*.mvpack"),
		EFileDialogFlags::Multiple,
		SelectedFiles);

	if (!bSelected || SelectedFiles.IsEmpty())
	{
		return FReply::Handled();
	}

	int32 SuccessCount = 0;
	FString LastInstallRoot;
	FText LastError;
	FScopedSlowTask SlowTask(static_cast<float>(SelectedFiles.Num()), FText::FromString(TEXT("正在加载材质包到暂存区...")));
	SlowTask.MakeDialog(true);
	for (const FString& PackPath : SelectedFiles)
	{
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FPaths::GetCleanFilename(PackPath)));
		FString InstallRoot;
		FText Error;
		if (InstallMvpack(PackPath, InstallRoot, Error, EMaterialVaultInstallMode::Staging))
		{
			++SuccessCount;
			LastInstallRoot = InstallRoot;
		}
		else
		{
			LastError = Error;
		}
	}

	if (SuccessCount > 0)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				FText::FromString(TEXT("已加载 {0} 个材质包到暂存区。\n最后位置：\n{1}")),
				FText::AsNumber(SuccessCount),
				FText::FromString(LastInstallRoot)));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, LastError.IsEmpty() ? FText::FromString(TEXT("加载材质包失败。")) : LastError);
	}

	return FReply::Handled();
}

TSharedRef<ITableRow> SMaterialVaultWindow::GenerateRow(TSharedPtr<FMaterialVaultItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	const EMaterialVaultSourceState SourceState = GetItemSourceState(Item);
	const FLinearColor SourceColor = GetSourceColor(SourceState);
	int32 MaterialInstanceCount = 0;
	int32 MaterialCount = 0;
	if (Item.IsValid())
	{
		MaterialInstanceCount = Item->RootAsset.Role == EMaterialVaultAssetRole::MaterialInstance ? 1 : 0;
		MaterialCount = (Item->RootAsset.Role == EMaterialVaultAssetRole::Material || Item->RootAsset.Role == EMaterialVaultAssetRole::RootMaterial) ? 1 : 0;
		for (const FMaterialVaultAssetEntry& Entry : Item->Dependencies)
		{
			if (Entry.Role == EMaterialVaultAssetRole::MaterialInstance)
			{
				++MaterialInstanceCount;
			}
			else if (Entry.Role == EMaterialVaultAssetRole::Material || Entry.Role == EMaterialVaultAssetRole::RootMaterial)
			{
				++MaterialCount;
			}
		}
	}

	return SNew(STableRow<TSharedPtr<FMaterialVaultItem>>, OwnerTable)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item.IsValid() ? Item->DisplayName : TEXT("")))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(7.0f, 2.0f))
				.BorderBackgroundColor(SourceColor.CopyWithNewOpacity(0.22f))
				.ToolTipText(GetSourceTooltipText(SourceState))
				[
					SNew(STextBlock)
					.Text(GetSourceBadgeText(SourceState))
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity(FSlateColor(SourceColor))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::Format(FText::FromString(TEXT("实例 {0} / 母材质 {1} / 依赖 {2}")),
					FText::AsNumber(MaterialInstanceCount),
					FText::AsNumber(MaterialCount),
					FText::AsNumber(Item.IsValid() ? Item->Dependencies.Num() : 0)))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(90.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.IsValid() ? Item->Category : TEXT("")))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item.IsValid() ? Item->RootAsset.OriginalPackageName : TEXT("")))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
		]
	];
}
TSharedRef<ITableRow> SMaterialVaultWindow::GeneratePackRow(TSharedPtr<FMaterialVaultPackItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Title = Item.IsValid() ? Item->DisplayName : FString();
	const EMaterialVaultSourceState SourceState = GetPackSourceState(Item);
	const FLinearColor SourceColor = GetSourceColor(SourceState);
	if (Item.IsValid() && !Item->ThumbnailBrush.IsValid() && !Item->ThumbnailPath.IsEmpty())
	{
		Item->ThumbnailBrush = CreateThumbnailBrush(
			Item->ThumbnailPath,
			Item->Id + TEXT("_List"),
			FVector2D(256.0f, 256.0f),
			&OwnedThumbnailTextures);
		if (Item->ThumbnailBrush.IsValid())
		{
			OwnedThumbnailBrushes.Add(Item->ThumbnailBrush);
		}
		if (Item->ThumbnailBrush.IsValid() && IsThumbnailCheckerboard(Item->ThumbnailPath))
		{
			Item->ThumbnailBrush.Reset();
			Item->ThumbnailPath.Reset();
		}
	}
	const FSlateBrush* ThumbnailBrush = (Item.IsValid() && Item->ThumbnailBrush.IsValid())
		? Item->ThumbnailBrush.Get()
		: nullptr;
	TSharedRef<SWidget> ThumbnailContent = ThumbnailBrush
		? StaticCastSharedRef<SWidget>(
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			[
				SNew(SImage).Image(ThumbnailBrush)
			])
		: StaticCastSharedRef<SWidget>(
			SNew(SBorder)
			.Padding(4.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("无预览")))
				.Justification(ETextJustify::Center)
			]);

	return SNew(STableRow<TSharedPtr<FMaterialVaultPackItem>>, OwnerTable)
	.Padding(4.0f)
	.OnDragDetected(this, &SMaterialVaultWindow::HandlePackDragDetected, Item)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(112.0f)
			.HeightOverride(112.0f)
			[
				SNew(SBorder)
				.Padding(3.0f)
				.BorderBackgroundColor(SourceColor)
				.ToolTipText(GetSourceTooltipText(SourceState))
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						ThumbnailContent
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					.Padding(4.0f)
					[
						SNew(SBorder)
						.Padding(FMargin(5.0f, 1.0f))
						.BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.018f, 0.88f))
						[
							SNew(STextBlock)
							.Text(GetSourceBadgeText(SourceState))
							.Font(FAppStyle::GetFontStyle("SmallFontBold"))
							.ColorAndOpacity(FSlateColor(SourceColor))
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 6.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Title))
			.Justification(ETextJustify::Center)
			.AutoWrapText(true)
			.WrapTextAt(132.0f)
		]
	];
}

TSharedRef<ITableRow> SMaterialVaultWindow::GenerateCategoryRow(TSharedPtr<FCategoryTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	int32 Depth = 0;
	if (Item.IsValid() && !Item->FullPath.IsEmpty() && Item->FullPath != TEXT("全部"))
	{
		for (const TCHAR Ch : Item->FullPath)
		{
			if (Ch == TEXT('/')) { ++Depth; }
		}
	}
	const float LeftPad = Depth > 0 ? 8.0f + Depth * 10.0f : 4.0f;

	return SNew(STableRow<TSharedPtr<FCategoryTreeItem>>, OwnerTable)
	.OnDragDetected_Lambda([Item](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
	{
		if (!Item.IsValid() || Item->FullPath == TEXT("全部") || !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			return FReply::Unhandled();
		}
		return FReply::Handled().BeginDragDrop(FMaterialVaultCategoryDragDropOp::New(Item->FullPath, Item->DisplayName));
	})
	.OnAcceptDrop_Lambda([this](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FCategoryTreeItem> TargetItem) -> FReply
	{
		TSharedPtr<FMaterialVaultCategoryDragDropOp> Op = DragDropEvent.GetOperationAs<FMaterialVaultCategoryDragDropOp>();
		if (Op.IsValid())
		{
			if (!TargetItem.IsValid() || TargetItem->FullPath == TEXT("全部") || Op->CategoryPath == TargetItem->FullPath)
			{
				return FReply::Unhandled();
			}
			const_cast<SMaterialVaultWindow*>(this)->MoveCategoryBefore(Op->CategoryPath, TargetItem->FullPath);
			return FReply::Handled();
		}

		TSharedPtr<FAssetDragDropOp> AssetOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
		if (!AssetOp.IsValid() || !TargetItem.IsValid() || TargetItem->FullPath == TEXT("全部"))
		{
			return FReply::Unhandled();
		}

		TArray<TSharedPtr<FMaterialVaultItem>> DroppedItems;
		FMaterialVaultScanner Scanner;
		for (const FAssetData& AssetData : AssetOp->GetAssets())
		{
			UClass* AssetClass = AssetData.GetClass();
			if (!AssetClass || !AssetClass->IsChildOf(UMaterialInterface::StaticClass()))
			{
				continue;
			}

			TSharedPtr<FMaterialVaultItem> NewItem = Scanner.BuildItemLight(AssetData);
			if (NewItem.IsValid())
			{
				NewItem->Category = TargetItem->FullPath;
				NewItem->RootAsset = Scanner.BuildEntry(AssetData, NewItem->Category, NewItem->Id, true);
				DroppedItems.Add(NewItem);
			}
		}

		if (DroppedItems.IsEmpty())
		{
			return FReply::Unhandled();
		}

		return const_cast<SMaterialVaultWindow*>(this)->CreateMvpackForItems(
			DroppedItems,
			FText::Format(
				FText::FromString(TEXT("将把拖入的 {0} 个材质加入分类：\n{1}\n\n是否继续？")),
				FText::AsNumber(DroppedItems.Num()),
				FText::FromString(TargetItem->FullPath)),
			TargetItem->FullPath);
	})
	[
		SNew(STextBlock)
		.Text(FText::FromString(Item.IsValid()
			? FString::Printf(TEXT("%s  (%d)"), *Item->DisplayName, Item->Count)
			: TEXT("")))
		.Margin(FMargin(LeftPad, 5.0f, 6.0f, 5.0f))
	];
}

TSharedRef<SWidget> SMaterialVaultWindow::GenerateLibraryComboItem(TSharedPtr<FString> Item) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
}

void SMaterialVaultWindow::RefreshLibraryItems()
{
	LibraryItems.Reset();

	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	const FString Root = ResolveVaultBaseRoot();
	IFileManager::Get().MakeDirectory(*Root, true);

	TArray<FString> Children;
	IFileManager::Get().FindFiles(Children, *FPaths::Combine(Root, TEXT("*")), false, true);
	Children.Sort();

	for (const FString& Child : Children)
	{
		const FString ChildDirectory = FPaths::Combine(Root, Child);
		const bool bLooksLikeLegacyLibrary =
			Child.Contains(TEXT("材质库")) &&
			!Child.Contains(TEXT("backup"), ESearchCase::IgnoreCase) &&
			!Child.Contains(TEXT("备份"));
		if (IsVaultLibraryDirectory(ChildDirectory) || bLooksLikeLegacyLibrary)
		{
			LibraryItems.Add(MakeShared<FString>(Child));
		}
	}

	UMaterialVaultSettings* MutableSettings = GetMutableDefault<UMaterialVaultSettings>();
	if (MutableSettings->ActiveLibraryName.IsEmpty())
	{
		MutableSettings->ActiveLibraryName = TEXT("默认材质库");
		MutableSettings->SaveConfig();
	}

	bool bFoundActive = false;
	for (const TSharedPtr<FString>& Item : LibraryItems)
	{
		if (Item.IsValid() && *Item == MutableSettings->ActiveLibraryName)
		{
			bFoundActive = true;
			break;
		}
	}

	if (!bFoundActive)
	{
		LibraryItems.Add(MakeShared<FString>(MutableSettings->ActiveLibraryName));
	}

	FText Error;
	EnsureVaultLayout(ResolveVaultRoot(), Error);
}

void SMaterialVaultWindow::HandleLibrarySelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	UMaterialVaultSettings* Settings = GetMutableDefault<UMaterialVaultSettings>();
	Settings->ActiveLibraryName = *Item;
	Settings->SaveConfig();
	FText Error;
	EnsureVaultLayout(ResolveVaultRoot(), Error);
	LoadPackItems(true);
	if (PackListView.IsValid()) { PackListView->RequestListRefresh(); }
	if (CategoryListView.IsValid()) { CategoryListView->RequestTreeRefresh(); }
}

FText SMaterialVaultWindow::GetActiveLibraryText() const
{
	const FString Name = GetDefault<UMaterialVaultSettings>()->ActiveLibraryName;
	return FText::FromString(Name.IsEmpty() ? TEXT("默认材质库") : Name);
}

TSharedPtr<SWidget> SMaterialVaultWindow::BuildPackContextMenu()
{
	if (!SelectedPackItem.IsValid())
	{
		return nullptr;
	}

	FMenuBuilder MenuBuilder(true, nullptr);

	// Pack operations
	MenuBuilder.BeginSection(NAME_None, FText::FromString(TEXT("材质包操作")));
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("加载到暂存区")),
		FText::FromString(TEXT("把选中的材质包加载到 /Game/__MaterialVaultStaging，方便临时拖拽使用。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleInstallSelectedPackClicked(); })));
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("正式导入 / 保留到项目材质库")),
		FText::FromString(TEXT("把选中的材质包正式导入到项目材质库，后续不会被暂存区清理。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleImportSelectedPackClicked(); })));
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("批量重命名")),
		FText::FromString(TEXT("按前缀和分类名批量重命名选中或当前显示的材质包。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleRenamePack(); })));
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("删除材质包")),
		FText::FromString(TEXT("从材质库中移除选中的材质包。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleDeletePack(); })));
	MenuBuilder.EndSection();

	// Move to category
	MenuBuilder.BeginSection(NAME_None, FText::FromString(TEXT("移动到分类")));
	TArray<FString> Categories = {
		TEXT("母材质"),
		TEXT("布料"),
		TEXT("金属"),
		TEXT("木材"),
		TEXT("石材"),
		TEXT("混凝土"),
		TEXT("玻璃"),
		TEXT("墙漆"),
		TEXT("地面"),
		TEXT("皮革"),
		TEXT("塑料"),
		TEXT("贴花"),
		TEXT("污垢"),
		TEXT("自然"),
		TEXT("皮肤"),
		TEXT("自发光"),
		TEXT("科技"),
		TEXT("未分类")
	};
	TFunction<void(const TArray<TSharedPtr<FCategoryTreeItem>>&)> AddTreeCategories = [&Categories, &AddTreeCategories](const TArray<TSharedPtr<FCategoryTreeItem>>& ItemsToAdd)
	{
		for (const TSharedPtr<FCategoryTreeItem>& CategoryItem : ItemsToAdd)
		{
			if (CategoryItem.IsValid() && CategoryItem->FullPath != TEXT("全部"))
			{
				const FString NormalizedCategory = SMaterialVaultWindow::NormalizeCategoryPath(CategoryItem->FullPath);
				if (!NormalizedCategory.IsEmpty())
				{
					Categories.AddUnique(NormalizedCategory);
				}
				AddTreeCategories(CategoryItem->Children);
			}
		}
	};
	AddTreeCategories(CategoryItems);

	for (const FString& Category : Categories)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(Category),
			FText::FromString(TEXT("把选中的材质包移动到这个分类。")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &SMaterialVaultWindow::MoveSelectedPackToCategory, Category)));
	}
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SMaterialVaultWindow::BuildCategoryContextMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("新建分类")),
		FText::FromString(TEXT("在当前材质库中新建一个分类文件夹。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { HandleCreateCategoryClicked(); })));
	if (!SelectedCategory.IsEmpty() && SelectedCategory != TEXT("全部") && SelectedCategory != TEXT("未分类"))
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(TEXT("删除分类")),
			FText::FromString(TEXT("删除当前分类文件夹，材质包会移回未分类。")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this]() { HandleDeleteCategoryClicked(); })));
	}
	return MenuBuilder.MakeWidget();
}

FReply SMaterialVaultWindow::HandleRenamePack()
{
	TArray<TSharedPtr<FMaterialVaultPackItem>> SelectedPacks;
	if (PackListView.IsValid())
	{
		SelectedPacks = PackListView->GetSelectedItems();
	}
	if (SelectedPacks.IsEmpty() && SelectedPackItem.IsValid())
	{
		SelectedPacks.Add(SelectedPackItem);
	}
	if (SelectedPacks.IsEmpty())
	{
		return FReply::Handled();
	}

	TSharedRef<SWindow> InputWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("批量重命名材质包")))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedPtr<SEditableTextBox> PrefixBox;
	TSharedPtr<SCheckBox> IncludeCategoryCheckBox;
	TSharedPtr<SCheckBox> KeepOriginalNameCheckBox;
	TSharedPtr<SCheckBox> UseVisibleCheckBox;
	TSharedPtr<SMultiLineEditableTextBox> RenamePreviewBox;
	auto SanitizeName = [](FString Name) -> FString
	{
		Name.TrimStartAndEndInline();
		const TCHAR* InvalidChars = TEXT("\\/:*?\"<>|");
		for (const TCHAR* It = InvalidChars; *It; ++It)
		{
			Name.ReplaceCharInline(*It, TEXT('_'));
		}
		while (Name.ReplaceInline(TEXT("__"), TEXT("_")) > 0) {}
		Name.TrimStartAndEndInline();
		return Name.IsEmpty() ? TEXT("Material") : Name;
	};
	auto BuildTargets = [this, &SelectedPacks, &UseVisibleCheckBox]() -> TArray<TSharedPtr<FMaterialVaultPackItem>>
	{
		if (UseVisibleCheckBox.IsValid() && UseVisibleCheckBox->IsChecked())
		{
			return FilteredPackItems;
		}
		return SelectedPacks;
	};
	auto BuildNewName = [SanitizeName, &PrefixBox, &IncludeCategoryCheckBox, &KeepOriginalNameCheckBox](const TSharedPtr<FMaterialVaultPackItem>& PackItem) -> FString
	{
		FString Prefix = PrefixBox.IsValid() ? PrefixBox->GetText().ToString() : FString();
		Prefix.TrimStartAndEndInline();
		if (!Prefix.IsEmpty())
		{
			Prefix = SanitizeName(Prefix);
		}

		FString CategoryPrefix;
		if (IncludeCategoryCheckBox.IsValid() && IncludeCategoryCheckBox->IsChecked() && PackItem.IsValid())
		{
			CategoryPrefix = PackItem->Category;
			CategoryPrefix.ReplaceInline(TEXT("/"), TEXT("_"));
			CategoryPrefix = SanitizeName(CategoryPrefix);
			if (CategoryPrefix == TEXT("未分类") || CategoryPrefix == TEXT("Uncategorized"))
			{
				CategoryPrefix.Reset();
			}
		}

		FString BaseName = PackItem.IsValid() ? PackItem->DisplayName : TEXT("Material");
		BaseName = SanitizeName(BaseName);

		TArray<FString> Parts;
		if (!Prefix.IsEmpty()) { Parts.Add(Prefix); }
		if (!CategoryPrefix.IsEmpty()) { Parts.Add(CategoryPrefix); }
		if (!KeepOriginalNameCheckBox.IsValid() || KeepOriginalNameCheckBox->IsChecked())
		{
			Parts.Add(BaseName);
		}
		if (Parts.IsEmpty())
		{
			Parts.Add(TEXT("Material"));
		}
		return SanitizeName(FString::Join(Parts, TEXT("_")));
	};
	auto RefreshPreviewText = [&BuildTargets, &BuildNewName, &RenamePreviewBox]() -> void
	{
		if (!RenamePreviewBox.IsValid())
		{
			return;
		}
		TArray<TSharedPtr<FMaterialVaultPackItem>> Targets = BuildTargets();
		FString Preview;
		const int32 PreviewCount = FMath::Min(Targets.Num(), 20);
		for (int32 Index = 0; Index < PreviewCount; ++Index)
		{
			const TSharedPtr<FMaterialVaultPackItem>& PackItem = Targets[Index];
			if (!PackItem.IsValid())
			{
				continue;
			}
			Preview += FString::Printf(TEXT("%s  ->  %s\n"), *PackItem->DisplayName, *BuildNewName(PackItem));
		}
		if (Targets.Num() > PreviewCount)
		{
			Preview += FString::Printf(TEXT("... 还有 %d 个\n"), Targets.Num() - PreviewCount);
		}
		RenamePreviewBox->SetText(FText::FromString(Preview));
	};

	InputWindow->SetContent(
		SNew(SBox)
		.MinDesiredWidth(520.0f)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("已选择 %d 个材质包。"), SelectedPacks.Num())))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SAssignNew(PrefixBox, SEditableTextBox)
				.HintText(FText::FromString(TEXT("用户定义前缀，可留空")))
				.OnTextChanged_Lambda([RefreshPreviewText](const FText&) { RefreshPreviewText(); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SAssignNew(IncludeCategoryCheckBox, SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([RefreshPreviewText](ECheckBoxState) { RefreshPreviewText(); })
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("文件名带分类名")))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SAssignNew(KeepOriginalNameCheckBox, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				.OnCheckStateChanged_Lambda([RefreshPreviewText](ECheckBoxState) { RefreshPreviewText(); })
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("保留原名称")))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SAssignNew(UseVisibleCheckBox, SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([RefreshPreviewText](ECheckBoxState) { RefreshPreviewText(); })
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("应用到当前显示的全部材质包")))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SAssignNew(RenamePreviewBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AutoWrapText(false)
				.Text(FText::FromString(TEXT("")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("应用")))
					.OnClicked_Lambda([InputWindow, this, &BuildTargets, &BuildNewName]() -> FReply
					{
						TArray<TSharedPtr<FMaterialVaultPackItem>> Targets = BuildTargets();
						if (Targets.IsEmpty())
						{
							FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("没有可重命名的材质包。")));
							return FReply::Handled();
						}

						int32 RenamedCount = 0;
						FString LastError;
						TSet<FString> ReservedPaths;
						for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : Targets)
						{
							if (!PackItem.IsValid())
							{
								continue;
							}
							const FString OldPath = PackItem->AbsolutePath;
							const FString Directory = FPaths::GetPath(OldPath);
							const FString Extension = FPaths::GetExtension(OldPath, true);
							FString NewName = BuildNewName(PackItem);
							FString NewPath = FPaths::Combine(Directory, NewName + Extension);
							int32 Suffix = 1;
							while (ReservedPaths.Contains(FPaths::ConvertRelativePathToFull(NewPath)) ||
								(FPaths::FileExists(NewPath) && FPaths::ConvertRelativePathToFull(NewPath) != FPaths::ConvertRelativePathToFull(OldPath)))
							{
								NewName = FString::Printf(TEXT("%s_%03d"), *BuildNewName(PackItem), Suffix++);
								NewPath = FPaths::Combine(Directory, NewName + Extension);
							}
							ReservedPaths.Add(FPaths::ConvertRelativePathToFull(NewPath));

							if (FPaths::ConvertRelativePathToFull(NewPath) == FPaths::ConvertRelativePathToFull(OldPath))
							{
								continue;
							}

							if (IFileManager::Get().Move(*NewPath, *OldPath, true, true))
							{
								PackItem->DisplayName = NewName;
								PackItem->Id = NewName;
								PackItem->AbsolutePath = NewPath;
								PackItem->RelativePath = MakeVaultRelativePath(NewPath, ResolveVaultRoot());
								++RenamedCount;
							}
							else
							{
								LastError = OldPath;
							}
						}

						RefreshCategoryItems();
						RefreshPackFilter();
						RefreshPreview();
						if (PackListView.IsValid())
						{
							PackListView->RequestListRefresh();
						}
						FString IndexPath;
						int32 PackCount = 0;
						FText Error;
						RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, Error);
						InputWindow->RequestDestroyWindow();
						FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
							LastError.IsEmpty()
								? FString::Printf(TEXT("已重命名 %d 个材质包。"), RenamedCount)
								: FString::Printf(TEXT("已重命名 %d 个材质包。\n最后失败文件：\n%s"), RenamedCount, *LastError)));
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("取消")))
					.OnClicked_Lambda([InputWindow]() -> FReply
					{
						InputWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]
	);
	RefreshPreviewText();

	FSlateApplication::Get().AddModalWindow(InputWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
	return FReply::Handled();
}

FReply SMaterialVaultWindow::HandleDeletePack()
{
	TArray<TSharedPtr<FMaterialVaultPackItem>> SelectedPacks;
	if (PackListView.IsValid())
	{
		SelectedPacks = PackListView->GetSelectedItems();
	}
	if (SelectedPacks.IsEmpty() && SelectedPackItem.IsValid())
	{
		SelectedPacks.Add(SelectedPackItem);
	}
	if (SelectedPacks.IsEmpty())
	{
		return FReply::Handled();
	}

	const EAppReturnType::Type Result = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(FString::Printf(TEXT("确定要从材质库中删除选中的 %d 个材质包吗？此操作不可撤销。"), SelectedPacks.Num())));

	if (Result != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	int32 DeletedCount = 0;
	FString LastFailedPath;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : SelectedPacks)
	{
		if (!PackItem.IsValid())
		{
			continue;
		}

		const FString PackPath = PackItem->AbsolutePath;
		if (!IFileManager::Get().Delete(*PackPath, false, true))
		{
			LastFailedPath = PackPath;
			continue;
		}

		if (!PackItem->ThumbnailPath.IsEmpty() && FPaths::FileExists(PackItem->ThumbnailPath))
		{
			IFileManager::Get().Delete(*PackItem->ThumbnailPath, false, true);
		}

		PackItems.Remove(PackItem);
		FilteredPackItems.Remove(PackItem);
		if (SelectedPackItem == PackItem)
		{
			SelectedPackItem.Reset();
		}
		++DeletedCount;
	}

	RefreshCategoryItems();
	RefreshPackFilter();
	if (PackListView.IsValid())
	{
		PackListView->RequestListRefresh();
	}
	RefreshPreview();
	FString IndexPath;
	int32 PackCount = 0;
	FText Error;
	RebuildPackIndex(ResolveVaultRoot(), IndexPath, PackCount, Error);
	if (!LastFailedPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("已删除 %d 个材质包。\n最后失败文件：\n%s"), DeletedCount, *LastFailedPath)));
	}
	return FReply::Handled();
}

void SMaterialVaultWindow::HandleSelectionChanged(TSharedPtr<FMaterialVaultItem> Item, ESelectInfo::Type SelectInfo)
{
	SelectedItem = Item;
	if (Item.IsValid())
	{
		SelectedPackItem.Reset();
		if (PackListView.IsValid())
		{
			PackListView->ClearSelection();
		}
	}
	RefreshPreview();
	RefreshDetailsPanel();
	RefreshScanResultsDetailsPanel();
}

void SMaterialVaultWindow::HandlePackSelectionChanged(TSharedPtr<FMaterialVaultPackItem> Item, ESelectInfo::Type SelectInfo)
{
	SelectedPackItem = Item;
	if (Item.IsValid())
	{
		SelectedItem.Reset();
		if (ListView.IsValid())
		{
			ListView->ClearSelection();
		}
	}
	RefreshPreview();
	RefreshDetailsPanel();

	if (MaterialDetailsView.IsValid())
	{
		MaterialDetailsView->SetObject(nullptr);
		MaterialDetailsView->SetVisibility(EVisibility::Collapsed);
	}
	if (DetailsContainer.IsValid())
	{
		DetailsContainer->SetVisibility(EVisibility::Visible);
	}
}

bool SMaterialVaultWindow::ResolvePackAssetData(const TSharedPtr<FMaterialVaultPackItem>& Item, FAssetData& OutAssetData, FString& OutObjectPath, FText& OutError)
{
	OutAssetData = FAssetData();
	OutObjectPath.Reset();

	if (!Item.IsValid())
	{
		OutError = FText::FromString(TEXT("材质包无效。"));
		return false;
	}

	FString RootObjectPath = Item->RootObjectPath;
	FString AssetClass = Item->RootAssetClass;
	if (RootObjectPath.IsEmpty())
	{
		ExtractPackRootInfoFromManifest(Item->AbsolutePath, RootObjectPath, AssetClass);
	}
	if (RootObjectPath.IsEmpty())
	{
		OutError = FText::FromString(TEXT("这个材质包没有记录根材质路径。"));
		return false;
	}

	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	OutObjectPath = RootObjectPath;
	OutObjectPath.ReplaceInline(TEXT("/Game/_MaterialVaultStaging"), TEXT("/Game/__MaterialVaultStaging"));
	OutObjectPath.ReplaceInline(*Settings->LibraryMountRoot, *Settings->StagingMountRoot);
	OutObjectPath.ReplaceInline(TEXT("/Game/MaterialVault/Library"), *Settings->StagingMountRoot);
	OutObjectPath.ReplaceInline(TEXT("/Game/MaterialVault"), *Settings->StagingMountRoot);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	OutAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(OutObjectPath));
	if (!OutAssetData.IsValid())
	{
		const bool bExternalPack = GetPackSourceState(Item) == EMaterialVaultSourceState::External;
		FScopedSlowTask SlowTask(100.0f, FText::Format(
			FText::FromString(bExternalPack ? TEXT("正在从外部材质包载入：{0}") : TEXT("正在载入材质包：{0}")),
			FText::FromString(Item->DisplayName)));
		if (bExternalPack)
		{
			SlowTask.MakeDialog(false);
		}
		else
		{
			SlowTask.MakeDialogDelayed(0.15f, false);
		}

		SlowTask.EnterProgressFrame(4.0f, FText::FromString(TEXT("正在准备写入暂存区...")));
		FString InstallRoot;
		if (!InstallMvpack(Item->AbsolutePath, InstallRoot, OutError, EMaterialVaultInstallMode::Staging, &SlowTask, 70.0f))
		{
			return false;
		}

		SlowTask.EnterProgressFrame(18.0f, FText::FromString(TEXT("正在扫描新载入的材质...")));
		TArray<FString> PathsToScan;
		PathsToScan.Add(FPackageName::GetLongPackagePath(OutObjectPath));
		AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, true);
		AssetRegistryModule.Get().WaitForCompletion();

		SlowTask.EnterProgressFrame(8.0f, FText::FromString(TEXT("正在准备拖拽资产...")));
		OutAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(OutObjectPath));
	}

	if (!OutAssetData.IsValid())
	{
		OutError = FText::FromString(TEXT("材质包安装后未能找到材质资产。"));
		return false;
	}

	return true;
}

bool SMaterialVaultWindow::PlacePackMaterialInScene(const TSharedPtr<FMaterialVaultPackItem>& Item, int32 PlacementIndex, FText& OutError)
{
	FAssetData AssetData;
	FString ObjectPath;
	FScopedSlowTask SlowTask(2.0f, FText::Format(
		FText::FromString(TEXT("正在准备材质：{0}")),
		FText::FromString(Item.IsValid() ? Item->DisplayName : TEXT("材质"))));
	SlowTask.MakeDialogDelayed(0.15f, false);
	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("正在确认材质是否已载入...")));
	if (!ResolvePackAssetData(Item, AssetData, ObjectPath, OutError))
	{
		return false;
	}

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("正在加载材质对象...")));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
	if (!Material)
	{
		OutError = FText::FromString(TEXT("材质资产加载失败。"));
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutError = FText::FromString(TEXT("没有可用的编辑器场景。"));
		return false;
	}

	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!PlaneMesh)
	{
		OutError = FText::FromString(TEXT("无法加载引擎默认平面网格。"));
		return false;
	}

	const int32 Column = PlacementIndex % 5;
	const int32 Row = PlacementIndex / 5;
	const FVector Location(Column * 260.0f, Row * 260.0f, 0.0f);
	const FTransform Transform(FRotator::ZeroRotator, Location, FVector(2.0f, 2.0f, 2.0f));

	World->Modify();
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
	if (!Actor)
	{
		OutError = FText::FromString(TEXT("无法在当前场景创建材质预览平面。"));
		return false;
	}

	Actor->SetFlags(RF_Transactional);
	Actor->SetActorLabel(Item.IsValid() ? Item->DisplayName : TEXT("MaterialVaultMaterial"));
	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	if (MeshComponent)
	{
		MeshComponent->SetStaticMesh(PlaneMesh);
		MeshComponent->SetMaterial(0, Material);
		MeshComponent->SetMobility(EComponentMobility::Static);
		MeshComponent->Modify();
	}

	if (GEditor)
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(Actor, true, true);
	}
	Actor->MarkPackageDirty();
	return true;
}

bool SMaterialVaultWindow::InstallSelectedPacks(EMaterialVaultInstallMode Mode, int32& OutSuccessCount, int32& OutFailedCount, FString& OutLastInstallRoot, FText& OutLastError)
{
	OutSuccessCount = 0;
	OutFailedCount = 0;
	OutLastInstallRoot.Reset();
	OutLastError = FText::GetEmpty();

	const TArray<TSharedPtr<FMaterialVaultPackItem>> SelectedPacks = GetSelectedPackItems();
	if (SelectedPacks.IsEmpty())
	{
		OutLastError = FText::FromString(TEXT("请先在材质包库里选择一个或多个材质包。"));
		return false;
	}

	const FText TaskText = Mode == EMaterialVaultInstallMode::Library
		? FText::FromString(TEXT("正在正式导入到项目材质库..."))
		: FText::FromString(TEXT("正在加载到暂存区..."));
	FScopedSlowTask SlowTask(static_cast<float>(SelectedPacks.Num()), TaskText);
	SlowTask.MakeDialog(true);
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : SelectedPacks)
	{
		if (!PackItem.IsValid())
		{
			continue;
		}

		SlowTask.EnterProgressFrame(1.0f, FText::FromString(PackItem->DisplayName));
		FString InstallRoot;
		FText Error;
		if (InstallMvpack(PackItem->AbsolutePath, InstallRoot, Error, Mode))
		{
			++OutSuccessCount;
			OutLastInstallRoot = InstallRoot;
		}
		else
		{
			++OutFailedCount;
			OutLastError = Error;
		}
	}

	return OutSuccessCount > 0;
}

EMaterialVaultSourceState SMaterialVaultWindow::GetPackSourceState(const TSharedPtr<FMaterialVaultPackItem>& Item) const
{
	if (!Item.IsValid())
	{
		return EMaterialVaultSourceState::External;
	}

	FString RootObjectPath = Item->RootObjectPath;
	FString AssetClass = Item->RootAssetClass;
	if (RootObjectPath.IsEmpty())
	{
		ExtractPackRootInfoFromManifest(Item->AbsolutePath, RootObjectPath, AssetClass);
	}
	if (RootObjectPath.IsEmpty())
	{
		return EMaterialVaultSourceState::External;
	}

	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	FString StagingObjectPath = RootObjectPath;
	StagingObjectPath.ReplaceInline(TEXT("/Game/_MaterialVaultStaging"), TEXT("/Game/__MaterialVaultStaging"));
	StagingObjectPath.ReplaceInline(*Settings->LibraryMountRoot, *Settings->StagingMountRoot);
	StagingObjectPath.ReplaceInline(TEXT("/Game/MaterialVault/Library"), *Settings->StagingMountRoot);
	StagingObjectPath.ReplaceInline(TEXT("/Game/MaterialVault"), *Settings->StagingMountRoot);

	FString LibraryObjectPath = RootObjectPath;
	LibraryObjectPath.ReplaceInline(TEXT("/Game/_MaterialVaultStaging"), *Settings->LibraryMountRoot);
	LibraryObjectPath.ReplaceInline(TEXT("/Game/__MaterialVaultStaging"), *Settings->LibraryMountRoot);
	LibraryObjectPath.ReplaceInline(*Settings->StagingMountRoot, *Settings->LibraryMountRoot);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	if (!LibraryObjectPath.IsEmpty() && AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(LibraryObjectPath)).IsValid())
	{
		return EMaterialVaultSourceState::Library;
	}
	if (!StagingObjectPath.IsEmpty() && AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(StagingObjectPath)).IsValid())
	{
		return EMaterialVaultSourceState::Staging;
	}
	if (AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(RootObjectPath)).IsValid())
	{
		return EMaterialVaultSourceState::Project;
	}
	return EMaterialVaultSourceState::External;
}

EMaterialVaultSourceState SMaterialVaultWindow::GetItemSourceState(const TSharedPtr<FMaterialVaultItem>& Item) const
{
	if (!Item.IsValid())
	{
		return EMaterialVaultSourceState::Project;
	}

	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	FString PackageName = Item->RootAsset.OriginalPackageName;
	if (PackageName.IsEmpty())
	{
		PackageName = Item->RootAsset.OriginalObjectPath.GetLongPackageName();
	}
	PackageName.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (PackageName.StartsWith(Settings->StagingMountRoot) ||
		PackageName.StartsWith(TEXT("/Game/__MaterialVaultStaging")) ||
		PackageName.StartsWith(TEXT("/Game/_MaterialVaultStaging")))
	{
		return EMaterialVaultSourceState::Staging;
	}
	if (PackageName.StartsWith(Settings->LibraryMountRoot) ||
		PackageName.StartsWith(TEXT("/Game/MaterialVault/Library")))
	{
		return EMaterialVaultSourceState::Library;
	}
	return EMaterialVaultSourceState::Project;
}

FText SMaterialVaultWindow::GetSourceBadgeText(EMaterialVaultSourceState SourceState)
{
	switch (SourceState)
	{
	case EMaterialVaultSourceState::Library:
		return FText::FromString(TEXT("项目库"));
	case EMaterialVaultSourceState::Staging:
		return FText::FromString(TEXT("暂存"));
	case EMaterialVaultSourceState::External:
		return FText::FromString(TEXT("外部"));
	case EMaterialVaultSourceState::Project:
	default:
		return FText::FromString(TEXT("项目"));
	}
}

FText SMaterialVaultWindow::GetSourceTooltipText(EMaterialVaultSourceState SourceState)
{
	switch (SourceState)
	{
	case EMaterialVaultSourceState::Library:
		return FText::FromString(TEXT("已经正式导入到当前项目材质库，可直接使用。"));
	case EMaterialVaultSourceState::Staging:
		return FText::FromString(TEXT("已经载入当前项目暂存区，可拖拽使用；后续可正式导入项目材质库。"));
	case EMaterialVaultSourceState::External:
		return FText::FromString(TEXT("仅存在于外部 .mvpack；首次拖拽会先写入暂存区，期间会显示进度。"));
	case EMaterialVaultSourceState::Project:
	default:
		return FText::FromString(TEXT("当前项目内的材质资产，不属于材质库暂存区。"));
	}
}

FLinearColor SMaterialVaultWindow::GetSourceColor(EMaterialVaultSourceState SourceState)
{
	switch (SourceState)
	{
	case EMaterialVaultSourceState::Library:
		return FLinearColor(0.22f, 0.78f, 0.42f, 1.0f);
	case EMaterialVaultSourceState::Staging:
		return FLinearColor(1.0f, 0.66f, 0.18f, 1.0f);
	case EMaterialVaultSourceState::External:
		return FLinearColor(0.20f, 0.68f, 1.0f, 1.0f);
	case EMaterialVaultSourceState::Project:
	default:
		return FLinearColor(0.62f, 0.62f, 0.68f, 1.0f);
	}
}

FReply SMaterialVaultWindow::HandlePackDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent, TSharedPtr<FMaterialVaultPackItem> Item)
	{
		if (!Item.IsValid() || !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			return FReply::Unhandled();

		FAssetData AssetData;
		FString ObjectPath;
		FText Error;
		if (!ResolvePackAssetData(Item, AssetData, ObjectPath, Error))
			return FReply::Unhandled();
		if (!AssetData.IsValid())
			return FReply::Unhandled();

		return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(AssetData));
	}

FReply SMaterialVaultWindow::HandlePackLibraryDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FAssetDragDropOp> AssetDragDropOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!AssetDragDropOp.IsValid())
	{
		return FReply::Unhandled();
	}

	TArray<TSharedPtr<FMaterialVaultItem>> DroppedItems;
	FMaterialVaultScanner Scanner;
	for (const FAssetData& AssetData : AssetDragDropOp->GetAssets())
	{
		UClass* AssetClass = AssetData.GetClass();
		if (!AssetClass || !AssetClass->IsChildOf(UMaterialInterface::StaticClass()))
		{
			continue;
		}

		TSharedPtr<FMaterialVaultItem> Item = Scanner.BuildItemLight(AssetData);
		if (Item.IsValid())
		{
			DroppedItems.Add(Item);
		}
	}

	if (DroppedItems.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("请拖入材质或材质实例资产。")));
		return FReply::Handled();
	}

	const FString ManualCategory = (!SelectedCategory.IsEmpty() && SelectedCategory != TEXT("全部"))
		? NormalizeCategoryPath(SelectedCategory)
		: TEXT("未分类");
	for (const TSharedPtr<FMaterialVaultItem>& DroppedItem : DroppedItems)
	{
		if (DroppedItem.IsValid())
		{
			DroppedItem->Category = ManualCategory;
		}
	}

	return CreateMvpackForItems(
		DroppedItems,
		FText::Format(
			FText::FromString(TEXT("将把拖入的 {0} 个材质加入当前材质库分类：\n{1}\n\n手动入库不会按名称自动分类。是否继续？")),
			FText::AsNumber(DroppedItems.Num()),
			FText::FromString(ManualCategory)),
		ManualCategory);
}

void SMaterialVaultWindow::HandlePackDoubleClicked(TSharedPtr<FMaterialVaultPackItem> Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	FString RootObjectPath = Item->RootObjectPath;
	FString AssetClass = Item->RootAssetClass;
	if (RootObjectPath.IsEmpty())
	{
		ExtractPackRootInfoFromManifest(Item->AbsolutePath, RootObjectPath, AssetClass);
	}

	if (RootObjectPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("这个材质包没有记录根材质路径。")));
		return;
	}

	// Step 1: Auto-install into staging if not already installed.
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	FString InstalledObjectPath = RootObjectPath;
	InstalledObjectPath.ReplaceInline(TEXT("/Game/_MaterialVaultStaging"), TEXT("/Game/__MaterialVaultStaging"));
	InstalledObjectPath.ReplaceInline(*Settings->LibraryMountRoot, *Settings->StagingMountRoot);
	InstalledObjectPath.ReplaceInline(TEXT("/Game/MaterialVault/Library"), *Settings->StagingMountRoot);
	InstalledObjectPath.ReplaceInline(TEXT("/Game/MaterialVault"), *Settings->StagingMountRoot);
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(InstalledObjectPath));
	if (!AssetData.IsValid())
	{
		FScopedSlowTask SlowTask(100.0f, FText::Format(
			FText::FromString(TEXT("正在载入材质包：{0}")),
			FText::FromString(Item->DisplayName)));
		SlowTask.MakeDialogDelayed(0.15f, false);

		SlowTask.EnterProgressFrame(4.0f, FText::FromString(TEXT("正在准备写入暂存区...")));
		FString InstallRoot;
		FText InstallError;
		if (!InstallMvpack(Item->AbsolutePath, InstallRoot, InstallError, EMaterialVaultInstallMode::Staging, &SlowTask, 70.0f))
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
				FText::FromString(TEXT("材质包加载到暂存区失败：\n{0}")), InstallError));
			return;
		}

		// Re-scan asset registry
		SlowTask.EnterProgressFrame(18.0f, FText::FromString(TEXT("正在扫描新载入的材质...")));
		TArray<FString> PathsToScan;
		PathsToScan.Add(FPackageName::GetLongPackagePath(InstalledObjectPath));
		AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, true);
		AssetRegistryModule.Get().WaitForCompletion();
		SlowTask.EnterProgressFrame(8.0f, FText::FromString(TEXT("正在打开材质...")));
		AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(InstalledObjectPath));
	}

	if (AssetData.IsValid())
	{
		UMaterialInstance* Material = LoadObject<UMaterialInstance>(nullptr, *InstalledObjectPath);
		if (Material)
		{
			if (GEditor)
			{
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				if (AssetEditorSubsystem)
					AssetEditorSubsystem->OpenEditorForAsset(Material);
			}
			if (DetailsContainer.IsValid())
				DetailsContainer->SetVisibility(EVisibility::Collapsed);
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("材质包已安装，但无法加载材质实例用于参数编辑。")));
		}
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("材质包安装后未能找到材质资产。")));
	}
}
void SMaterialVaultWindow::HandleCategorySelectionChanged(TSharedPtr<FCategoryTreeItem> Item, ESelectInfo::Type SelectInfo)
{
	SelectedCategory = Item.IsValid() ? Item->FullPath : TEXT("全部");
	RefreshPackFilter();
}

void SMaterialVaultWindow::OpenScanResultsWindow()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::Format(FText::FromString(TEXT("扫描结果 - {0} 个材质")), FText::AsNumber(Items.Num())))
		.ClientSize(FVector2D(760.0f, 520.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false);

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("选择要生成材质包的材质。分类已根据材质名自动推断，可生成后在材质包库右键调整。")))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("生成选中材质")))
				.OnClicked(this, &SMaterialVaultWindow::HandleCreateSelectedMvpackClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("全部生成材质")))
				.OnClicked(this, &SMaterialVaultWindow::HandleCreateAllMvpackClicked)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(10.0f, 0.0f, 10.0f, 10.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot()
			.Value(0.58f)
			[
				SAssignNew(ScanResultsListView, SListView<TSharedPtr<FMaterialVaultItem>>)
				.ListItemsSource(&Items)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SMaterialVaultWindow::GenerateRow)
				.OnSelectionChanged(this, &SMaterialVaultWindow::HandleSelectionChanged)
			]
			+ SSplitter::Slot()
			.Value(0.42f)
			[
				SAssignNew(ScanResultsDetailsBox, SBox)
				[
					BuildScanResultDetailsWidget()
				]
			]
		]);

	FSlateApplication::Get().AddWindow(Window);
}

void SMaterialVaultWindow::RefreshScanResultsDetailsPanel()
{
	if (ScanResultsDetailsBox.IsValid())
	{
		ScanResultThumbnails.Reset();
		ScanResultsDetailsBox->SetContent(BuildScanResultDetailsWidget());
	}
}

TSharedRef<SWidget> SMaterialVaultWindow::BuildScanResultDetailsWidget() const
{
	if (!SelectedItem.IsValid())
	{
		return SNew(SBorder)
			.Padding(10.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(TEXT("选择左侧材质后，在这里查看材质簇内容。列表本身保持轻量，避免扫描结果窗口一次性加载大量实例和缩略图。")))
			];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SelectedItem->DisplayName))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(FText::FromString(TEXT("分类：{0}    依赖：{1}")),
				FText::FromString(SelectedItem->Category),
				FText::AsNumber(SelectedItem->Dependencies.Num())))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(TEXT("内部扫描结果不实时渲染缩略图，避免占用显存。生成材质包后会从包内或缓存缩略图显示。")))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.62f, 0.38f)))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				BuildMaterialFamilyTable(*SelectedItem, false)
			]
		];
}

void SMaterialVaultWindow::RefreshPreview()
{
	if (!PreviewBox.IsValid())
	{
		return;
	}

	if (!SelectedItem.IsValid())
	{
		if (SelectedPackItem.IsValid())
		{
			SelectedThumbnail.Reset();
			SelectedPackBrush.Reset();
			SelectedPackBrush = CreateThumbnailBrush(
				SelectedPackItem->ThumbnailPath,
				SelectedPackItem->Id + TEXT("_Preview"),
				FVector2D(220.0f, 220.0f),
				&OwnedThumbnailTextures);
			if (SelectedPackBrush.IsValid())
			{
				OwnedThumbnailBrushes.Add(SelectedPackBrush);
			}
			if (SelectedPackBrush.IsValid())
			{
				PreviewBox->SetContent(
					SNew(SBox)
					.WidthOverride(220.0f)
					.HeightOverride(220.0f)
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						[
							SNew(SImage).Image(SelectedPackBrush.Get())
						]
					]);
				return;
			}

			PreviewBox->SetContent(
				SNew(SBorder)
				.Padding(8.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT(".mvpack 内没有预览图")))
				]);
			return;
		}

		SelectedThumbnail.Reset();
		SelectedPackBrush.Reset();
		PreviewBox->SetContent(
			SNew(SBorder)
			.Padding(8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("选择材质后显示预览")))
			]);
		return;
	}

	SelectedThumbnail.Reset();
	SelectedPackBrush.Reset();
	PreviewBox->SetContent(
		SNew(SBorder)
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(TEXT("扫描结果预览已禁用实时渲染，避免扫描大量材质时占用显存。生成材质包后在材质库中查看缩略图。")))
		]);
}

void SMaterialVaultWindow::RefreshDetailsPanel()
{
	PackDetailThumbnails.Reset();
	if (DetailsContainer.IsValid())
	{
		DetailsContainer->SetContent(BuildSelectedDetailsWidget());
	}
}

TSharedRef<SWidget> SMaterialVaultWindow::BuildSelectedDetailsWidget()
{
	TSharedRef<SVerticalBox> DetailsBox = SNew(SVerticalBox);

	DetailsBox->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.Text(this, &SMaterialVaultWindow::GetSelectedDetailsText)
	];

	if (SelectedPackItem.IsValid())
	{
		if (SelectedPackItem->bHasFamilyItem)
		{
			DetailsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\u6750\u8d28\u7c07\u5185\u5bb9")))
			];

			DetailsBox->AddSlot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					BuildMaterialFamilyTable(SelectedPackItem->FamilyItem, false)
				]
			];
		}
		return DetailsBox;
	}

	if (SelectedPackItem.IsValid())
	{
		FMaterialVaultItem FamilyItem;
		FText Error;
		if (PreparePackFamilyPreviewItem(SelectedPackItem, FamilyItem, Error))
		{
			DetailsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("材质族内容")))
			];

			DetailsBox->AddSlot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					BuildMaterialFamilyTable(FamilyItem, true)
				]
			];
		}
		else if (!Error.IsEmpty())
		{
			DetailsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.45f, 0.18f)))
				.Text(Error)
			];
		}
	}

	return DetailsBox;
}

TSharedRef<SWidget> SMaterialVaultWindow::BuildMaterialFamilyTable(const FMaterialVaultItem& Item, bool bUseStagingAssets) const
{
	TSharedRef<SVerticalBox> RowsBox = SNew(SVerticalBox);

	auto MakeEntrySelectionKey = [](const FMaterialVaultAssetEntry& Entry)
	{
		const FString ObjectPath = Entry.OriginalObjectPath.ToString();
		if (!ObjectPath.IsEmpty())
		{
			return ObjectPath;
		}
		return Entry.PlannedPackageName;
	};

	auto ResolveObjectPath = [bUseStagingAssets](const FMaterialVaultAssetEntry& Entry) -> FSoftObjectPath
	{
		if (bUseStagingAssets)
		{
			const FString StagingObjectPath = GetStagingObjectPath(Entry);
			return FSoftObjectPath(StagingObjectPath.IsEmpty() ? Entry.OriginalObjectPath.ToString() : StagingObjectPath);
		}
		return Entry.OriginalObjectPath;
	};

	auto AddAssetRow = [this, &RowsBox, &ResolveObjectPath, &MakeEntrySelectionKey, &Item, bUseStagingAssets](const FMaterialVaultAssetEntry& Entry, const FString& RoleLabel)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FSoftObjectPath ObjectPath = ResolveObjectPath(Entry);
		const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ObjectPath);

		TSharedRef<SWidget> ThumbnailContent = SNew(STextBlock).Text(FText::FromString(TEXT("--")));
		bool bHasCachedThumbnailContent = false;
		if (!Entry.ThumbnailPath.IsEmpty() && FPaths::FileExists(Entry.ThumbnailPath))
		{
			TSharedPtr<FSlateDynamicImageBrush> CachedBrush = CreateThumbnailBrush(
				Entry.ThumbnailPath,
				FString::Printf(TEXT("%s_%u_Family"), *Item.Id, GetTypeHash(Entry.ThumbnailPath)),
				FVector2D(54.0f, 54.0f),
				&OwnedThumbnailTextures);
			if (CachedBrush.IsValid())
			{
				OwnedThumbnailBrushes.Add(CachedBrush);
				bHasCachedThumbnailContent = true;
				ThumbnailContent = SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit)
					[
						SNew(SImage).Image(CachedBrush.Get())
					];
			}
		}
		if (!bHasCachedThumbnailContent && AssetData.IsValid() && bUseStagingAssets && !IsRunningCommandlet())
		{
			TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(AssetData, 54, 54, ThumbnailPool);
			if (bUseStagingAssets)
			{
				PackDetailThumbnails.Add(Thumbnail);
			}
			else
			{
				ScanResultThumbnails.Add(Thumbnail);
			}
			ThumbnailContent = Thumbnail->MakeThumbnailWidget();
		}

		TSharedRef<SWidget> ThumbnailWidget = SNew(SBox)
			.WidthOverride(54.0f)
			.HeightOverride(54.0f)
			[
				SNew(SBorder).Padding(2.0f)
				[
					ThumbnailContent
				]
			];

		const FString PackageName = bUseStagingAssets ? GetStagingPackageName(Entry) : Entry.OriginalPackageName;
		const FString EntrySelectionKey = MakeEntrySelectionKey(Entry);
		const bool bIsSelectedEntry = !EntrySelectionKey.IsEmpty() && EntrySelectionKey == SelectedFamilyEntryKey;
		RowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SBorder)
			.Padding(4.0f)
			.BorderBackgroundColor(bIsSelectedEntry ? FLinearColor(0.08f, 0.32f, 0.68f, 1.0f) : FLinearColor(0.02f, 0.02f, 0.02f, 0.35f))
			.OnMouseButtonDown_Lambda([this, Entry](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
			{
				if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
				{
					return FReply::Unhandled();
				}
				const_cast<SMaterialVaultWindow*>(this)->HandleFamilyEntryClicked(Entry);
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					ThumbnailWidget
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(76.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(RoleLabel))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.34f)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FPaths::GetBaseFilename(PackageName)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.66f)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(PackageName))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
				]
			]
		];
	};

	AddAssetRow(Item.RootAsset, Item.RootAsset.Role == EMaterialVaultAssetRole::MaterialInstance ? TEXT("材质实例") : TEXT("母材质"));
	for (const FMaterialVaultAssetEntry& Dependency : Item.Dependencies)
	{
		if (Dependency.Role == EMaterialVaultAssetRole::Material || Dependency.Role == EMaterialVaultAssetRole::RootMaterial)
		{
			AddAssetRow(Dependency, TEXT("母材质"));
		}
	}
	for (const FMaterialVaultAssetEntry& Dependency : Item.Dependencies)
	{
		if (Dependency.Role == EMaterialVaultAssetRole::MaterialInstance)
		{
			AddAssetRow(Dependency, TEXT("材质实例"));
		}
	}

	return RowsBox;
}

void SMaterialVaultWindow::HandleFamilyEntryClicked(const FMaterialVaultAssetEntry& Entry)
{
	SelectedFamilyEntryKey = Entry.OriginalObjectPath.ToString();
	if (SelectedFamilyEntryKey.IsEmpty())
	{
		SelectedFamilyEntryKey = Entry.PlannedPackageName;
	}

	auto EntryMatches = [&Entry](const FMaterialVaultAssetEntry& Candidate)
	{
		const FString EntryObjectPath = Entry.OriginalObjectPath.ToString();
		const FString CandidateObjectPath = Candidate.OriginalObjectPath.ToString();
		if (!EntryObjectPath.IsEmpty() && EntryObjectPath == CandidateObjectPath)
		{
			return true;
		}
		if (!Entry.OriginalPackageName.IsEmpty() && Entry.OriginalPackageName == Candidate.OriginalPackageName)
		{
			return true;
		}
		if (!Entry.PlannedPackageName.IsEmpty() && Entry.PlannedPackageName == Candidate.PlannedPackageName)
		{
			return true;
		}
		return false;
	};
	auto GetEntryBaseName = [](const FMaterialVaultAssetEntry& InEntry)
	{
		FString Name = FPaths::GetBaseFilename(InEntry.PlannedPackageName);
		if (Name.IsEmpty())
		{
			Name = FPaths::GetBaseFilename(InEntry.OriginalPackageName);
		}
		if (Name.IsEmpty())
		{
			Name = FPackageName::ObjectPathToObjectName(InEntry.PlannedObjectPath);
		}
		if (Name.IsEmpty())
		{
			Name = FPackageName::ObjectPathToObjectName(InEntry.OriginalObjectPath.ToString());
		}
		return Name;
	};
	const FString EntryBaseName = GetEntryBaseName(Entry);
	const FString EntryOriginalObjectPath = Entry.OriginalObjectPath.ToString();

	TSharedPtr<FMaterialVaultPackItem> MatchingPack;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : PackItems)
	{
		if (!PackItem.IsValid())
		{
			continue;
		}
		if (PackItem->bIsFamilyEntry)
		{
			if ((!Entry.PlannedObjectPath.IsEmpty() && PackItem->RootObjectPath == Entry.PlannedObjectPath) ||
				(!Entry.PlannedPackageName.IsEmpty() && PackItem->RootObjectPath.StartsWith(Entry.PlannedPackageName + TEXT("."))) ||
				(!EntryOriginalObjectPath.IsEmpty() && PackItem->RootObjectPath == EntryOriginalObjectPath) ||
				(!EntryBaseName.IsEmpty() && PackItem->DisplayName == EntryBaseName && PackItem->EntryRole == Entry.Role))
			{
				MatchingPack = PackItem;
				break;
			}
		}
	}
	if (!MatchingPack.IsValid())
	{
		for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : PackItems)
		{
			if (!PackItem.IsValid())
			{
				continue;
			}
		if (PackItem->bHasFamilyItem)
		{
			if (EntryMatches(PackItem->FamilyItem.RootAsset))
			{
				MatchingPack = PackItem;
				break;
			}
			for (const FMaterialVaultAssetEntry& Dependency : PackItem->FamilyItem.Dependencies)
			{
				if (EntryMatches(Dependency))
				{
					MatchingPack = PackItem;
					break;
				}
			}
			if (MatchingPack.IsValid())
			{
				break;
			}
		}
		if (!PackItem->RootObjectPath.IsEmpty() && Entry.OriginalObjectPath.ToString() == PackItem->RootObjectPath)
		{
			MatchingPack = PackItem;
			break;
		}
		}
	}

	if (MatchingPack.IsValid() && PackListView.IsValid())
	{
		if (!MatchingPack->Category.IsEmpty() && SelectedCategory != MatchingPack->Category)
		{
			SelectedCategory = MatchingPack->Category;
			if (CategoryListView.IsValid())
			{
				TSharedPtr<FCategoryTreeItem> MatchingCategory;
				TFunction<void(const TArray<TSharedPtr<FCategoryTreeItem>>&)> FindCategory =
					[&](const TArray<TSharedPtr<FCategoryTreeItem>>& ItemsToSearch)
					{
						for (const TSharedPtr<FCategoryTreeItem>& CategoryItem : ItemsToSearch)
						{
							if (!CategoryItem.IsValid() || MatchingCategory.IsValid())
							{
								continue;
							}
							if (CategoryItem->FullPath == MatchingPack->Category)
							{
								MatchingCategory = CategoryItem;
								return;
							}
							FindCategory(CategoryItem->Children);
						}
					};
				FindCategory(CategoryItems);
				if (MatchingCategory.IsValid())
				{
					CategoryListView->SetSelection(MatchingCategory);
					CategoryListView->RequestScrollIntoView(MatchingCategory);
				}
			}
			RefreshPackFilter();
		}
		if (!FilteredPackItems.Contains(MatchingPack))
		{
			SelectedCategory = TEXT("全部");
			RefreshPackFilter();
		}
		PackListView->SetSelection(MatchingPack);
		PackListView->RequestScrollIntoView(MatchingPack);
	}
	else
	{
		RefreshDetailsPanel();
	}
}

bool SMaterialVaultWindow::PreparePackFamilyPreviewItem(const TSharedPtr<FMaterialVaultPackItem>& PackItem, FMaterialVaultItem& OutItem, FText& OutError)
{
	OutItem = FMaterialVaultItem();
	OutError = FText::GetEmpty();
	if (!PackItem.IsValid())
	{
		return false;
	}

	if (!ExtractPackItemFromManifest(PackItem->AbsolutePath, OutItem))
	{
		OutError = FText::FromString(TEXT("这个材质族包没有可读取的 manifest，无法展开母材质和实例列表。"));
		return false;
	}

	auto CacheManifestThumbnail = [PackItem, &OutItem](FMaterialVaultAssetEntry& Entry)
	{
		if (Entry.ThumbnailPath.IsEmpty())
		{
			return;
		}
		FString CachedThumbnailPath;
		if (ExtractPackStoredThumbnail(PackItem->AbsolutePath, OutItem.Id, Entry.ThumbnailPath, CachedThumbnailPath))
		{
			Entry.ThumbnailPath = CachedThumbnailPath;
		}
	};
	CacheManifestThumbnail(OutItem.RootAsset);
	for (FMaterialVaultAssetEntry& Dependency : OutItem.Dependencies)
	{
		CacheManifestThumbnail(Dependency);
	}
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	auto HasStagingAsset = [&AssetRegistryModule](const FMaterialVaultAssetEntry& Entry) -> bool
	{
		const FString ObjectPath = GetStagingObjectPath(Entry);
		return !ObjectPath.IsEmpty() && AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
	};

	if (!HasStagingAsset(OutItem.RootAsset))
	{
		FString InstallRoot;
		if (!InstallMvpack(PackItem->AbsolutePath, InstallRoot, OutError, EMaterialVaultInstallMode::Staging))
		{
			return false;
		}
	}

	TArray<FString> PathsToScan;
	for (const FMaterialVaultAssetEntry& Entry : GetAllEntries(OutItem))
	{
		const FString ObjectPath = GetStagingObjectPath(Entry);
		if (!ObjectPath.IsEmpty())
		{
			PathsToScan.AddUnique(FPackageName::GetLongPackagePath(ObjectPath));
		}
	}
	if (!PathsToScan.IsEmpty())
	{
		AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, true);
	}

	if (!HasStagingAsset(OutItem.RootAsset))
	{
		OutError = FText::FromString(TEXT("材质族包已尝试加载到暂存区，但仍未找到根材质资产。"));
		return false;
	}

	return true;
}

FText SMaterialVaultWindow::GetSelectedDetailsText() const
{
	if (SelectedPackItem.IsValid())
	{
		return GetSelectedPackDetailsText();
	}

	if (!SelectedItem.IsValid())
	{
		return FText::FromString(TEXT("请选择扫描结果或材质包。材质包会按 Packs/分类/*.mvpack 自动分类显示。"));
	}

	FString Details;
	Details += FString::Printf(TEXT("名称：%s\n"), *SelectedItem->DisplayName);
	Details += FString::Printf(TEXT("库编号：%s\n"), *SelectedItem->Id);
	Details += FString::Printf(TEXT("收录引擎版本：%s\n\n"), *SelectedItem->SourceEngineVersion);
	if (NormalizeCategoryPath(SelectedItem->Category) == TEXT("母材质"))
	{
		Details += TEXT("提示：这是母材质，主要用于参数和实例继承，不建议直接赋给物体。请优先使用对应的材质实例或实例族材质包。\n\n");
	}
	Details += FString::Printf(TEXT("原始根资产：\n%s\n\n"), *SelectedItem->RootAsset.OriginalPackageName);
	Details += FString::Printf(TEXT("统一目标路径：\n%s\n\n"), *SelectedItem->RootAsset.PlannedPackageName);
	Details += FString::Printf(TEXT("依赖数量：%d\n"), SelectedItem->Dependencies.Num());

	for (const FMaterialVaultAssetEntry& Dependency : SelectedItem->Dependencies)
	{
		Details += FString::Printf(TEXT("\n%s\n-> %s"), *Dependency.OriginalPackageName, *Dependency.PlannedPackageName);
	}

	return FText::FromString(Details);
}

FText SMaterialVaultWindow::GetSelectedPackDetailsText() const
{
	if (!SelectedPackItem.IsValid())
	{
		return FText::GetEmpty();
	}

	FString Details;
	if (SelectedPackItem->bIsFamilyEntry)
	{
		Details += FString::Printf(TEXT("材质：%s\n"), *SelectedPackItem->DisplayName);
		Details += FString::Printf(TEXT("所属材质包：%s\n"), *SelectedPackItem->PackDisplayName);
	}
	else
	{
		Details += FString::Printf(TEXT("材质包：%s\n"), *SelectedPackItem->DisplayName);
	}
	Details += FString::Printf(TEXT("分类：%s\n"), *SelectedPackItem->Category);
	if (NormalizeCategoryPath(SelectedPackItem->Category) == TEXT("母材质") ||
		(SelectedPackItem->RootAssetClass.Contains(TEXT("Material")) && !SelectedPackItem->RootAssetClass.Contains(TEXT("MaterialInstance"))))
	{
		Details += TEXT("提示：这是母材质族包，主要用于实例继承，不建议直接赋给物体。请优先使用材质族里的材质实例。\n");
	}
	Details += FString::Printf(TEXT("相对路径：%s\n"), *SelectedPackItem->RelativePath);
	Details += FString::Printf(TEXT("文件大小：%.2f GB\n\n"), static_cast<double>(SelectedPackItem->SizeBytes) / 1024.0 / 1024.0 / 1024.0);
	Details += TEXT("使用方式：点击“加载到暂存区”用于临时拖拽；点击“正式导入 / 保留到项目材质库”会导入到项目材质库并长期保留。\n\n");
	Details += SelectedPackItem->ThumbnailPath.IsEmpty()
		? TEXT("这个 .mvpack 里没有 thumbnail.png，可能是旧版本生成的包。")
		: TEXT("这个 .mvpack 已读取内置 thumbnail.png。");
	return FText::FromString(Details);
}

FText SMaterialVaultWindow::GetStatusText() const
{
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	const int64 StagingBytes = GetDirectorySizeBytes(GetStagingRootDirectory());
	return FText::Format(
		FText::FromString(TEXT("已扫描 {0} 个本项目材质，材质库条目 {1} 个。导入根目录：{2}。材质库：{3}。暂存区：{4} / {5} GB，保留 {6} 天")),
		FText::AsNumber(Items.Num()),
		FText::AsNumber(PackItems.Num()),
		FText::FromString(Settings->LibraryMountRoot),
		FText::FromString(ResolveVaultRoot()),
		FText::FromString(FormatBytes(StagingBytes)),
		FText::FromString(FString::Printf(TEXT("%.1f"), static_cast<float>(Settings->StagingMaxSizeMB) / 1024.0f)),
		FText::AsNumber(Settings->StagingKeepDays));
}

TArray<TSharedPtr<FMaterialVaultPackItem>> SMaterialVaultWindow::GetSelectedPackItems() const
{
	TArray<TSharedPtr<FMaterialVaultPackItem>> SelectedPacks;
	if (PackListView.IsValid())
	{
		SelectedPacks = PackListView->GetSelectedItems();
	}
	if (SelectedPacks.IsEmpty() && SelectedPackItem.IsValid())
	{
		SelectedPacks.Add(SelectedPackItem);
	}
	return SelectedPacks;
}

void SMaterialVaultWindow::LoadPackItems(bool bShowProgress)
{
	if (PackListView.IsValid())
	{
		PackListView->ClearSelection();
	}
	SelectedPackItem.Reset();
	SelectedPackBrush.Reset();

	PackItems.Reset();
	FilteredPackItems.Reset();

	FText Error;
	const FString VaultRoot = ResolveVaultRoot();
	if (!EnsureVaultLayout(VaultRoot, Error))
	{
		return;
	}

	const FString PacksRoot = FPaths::Combine(VaultRoot, TEXT("Packs"));
	TArray<FString> PackFiles;
	IFileManager::Get().FindFilesRecursive(PackFiles, *PacksRoot, TEXT("*.mvpack"), true, false);
	PackFiles.RemoveAll([](const FString& PackFile)
	{
		const FString BaseName = FPaths::GetBaseFilename(PackFile);
		return BaseName.StartsWith(TEXT("MV___MaterialVaultStaging_")) ||
			BaseName.StartsWith(TEXT("__MaterialVaultStaging_")) ||
			BaseName.StartsWith(TEXT("_MaterialVaultStaging_"));
	});
	PackFiles.Sort();

	TMap<FString, FString> LatestPackFilesById;
	for (const FString& PackFile : PackFiles)
	{
		const FString PackId = FPaths::GetBaseFilename(PackFile);
		const FString* ExistingPackFile = LatestPackFilesById.Find(PackId);
		if (!ExistingPackFile ||
			IFileManager::Get().GetTimeStamp(*PackFile) > IFileManager::Get().GetTimeStamp(**ExistingPackFile))
		{
			LatestPackFilesById.Add(PackId, PackFile);
		}
	}

	PackFiles.Reset();
	LatestPackFilesById.GenerateValueArray(PackFiles);
	PackFiles.Sort([](const FString& Left, const FString& Right)
	{
		return FPaths::GetBaseFilename(Left) < FPaths::GetBaseFilename(Right);
	});

	if (LoadPackItemsFromIndex(VaultRoot, PackFiles, PackItems))
	{
		RefreshCategoryItems();
		RefreshPackFilter();
		return;
	}

	FScopedSlowTask SlowTask(static_cast<float>(PackFiles.Num()), FText::FromString(TEXT("正在读取材质包索引...")));
	if (bShowProgress && PackFiles.Num() > 80)
	{
		SlowTask.MakeDialog(true);
	}

	for (const FString& PackFile : PackFiles)
	{
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FPaths::GetCleanFilename(PackFile)));
		TSharedPtr<FMaterialVaultPackItem> PackItem = MakeShared<FMaterialVaultPackItem>();
		PackItem->Id = FPaths::GetBaseFilename(PackFile);
		PackItem->DisplayName = PackItem->Id;

		FPackedMeta Meta;
		if (!ExtractPackMeta(PackFile, PackItem->Id, VaultRoot, Meta))
		{
			continue;
		}
		if (!Meta.RootAssetClass.Contains(TEXT("Material")))
		{
			continue;
		}

		PackItem->RootObjectPath = Meta.RootObjectPath;
		PackItem->RootAssetClass = Meta.RootAssetClass;
		PackItem->ThumbnailPath = Meta.ThumbnailPath;

		if (!Meta.Category.IsEmpty())
		{
			PackItem->Category = NormalizeCategoryPath(Meta.Category);
		}
		else
		{
			PackItem->Category = NormalizeCategoryPath(DetectPackCategory(PackFile, VaultRoot));
			if (PackItem->Category == TEXT("未分类"))
			{
				PackItem->Category = InferCategoryFromName(PackItem->DisplayName);
			}
		}
		PackItem->RelativePath = MakeVaultRelativePath(PackFile, VaultRoot);
		PackItem->AbsolutePath = PackFile;
		PackItem->SizeBytes = IFileManager::Get().FileSize(*PackFile);
		PackItems.Add(PackItem);
	}

	RefreshCategoryItems();
	RefreshPackFilter();
}

bool SMaterialVaultWindow::LoadPackItemsFromIndex(const FString& VaultRoot, const TArray<FString>& PackFiles, TArray<TSharedPtr<FMaterialVaultPackItem>>& OutPackItems)
{
	OutPackItems.Reset();
	const FString IndexPath = FPaths::Combine(VaultRoot, TEXT("Index.json"));
	if (!FPaths::FileExists(IndexPath))
	{
		return false;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *IndexPath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> IndexObject;
	const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(JsonReader, IndexObject) || !IndexObject.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* PacksArray = nullptr;
	if (!IndexObject->TryGetArrayField(TEXT("packs"), PacksArray) || !PacksArray)
	{
		return false;
	}

	TMap<FString, FString> PackPathById;
	TMap<FString, int64> PackSizeById;
	for (const FString& PackFile : PackFiles)
	{
		const FString PackId = FPaths::GetBaseFilename(PackFile);
		PackPathById.Add(PackId, PackFile);
		PackSizeById.Add(PackId, IFileManager::Get().FileSize(*PackFile));
	}

	auto ParseRole = [](const FString& RoleText)
	{
		if (RoleText == TEXT("RootMaterial")) return EMaterialVaultAssetRole::RootMaterial;
		if (RoleText == TEXT("Material")) return EMaterialVaultAssetRole::Material;
		if (RoleText == TEXT("MaterialInstance")) return EMaterialVaultAssetRole::MaterialInstance;
		if (RoleText == TEXT("Texture")) return EMaterialVaultAssetRole::Texture;
		if (RoleText == TEXT("MaterialFunction")) return EMaterialVaultAssetRole::MaterialFunction;
		return EMaterialVaultAssetRole::Other;
	};
	auto ParseFamilyEntry = [&ParseRole](const TSharedPtr<FJsonObject>& EntryObject, FMaterialVaultAssetEntry& OutEntry)
	{
		if (!EntryObject.IsValid())
		{
			return false;
		}

		FString OriginalObjectPath;
		FString RoleText;
		EntryObject->TryGetStringField(TEXT("role"), RoleText);
		EntryObject->TryGetStringField(TEXT("assetClass"), OutEntry.AssetClass);
		EntryObject->TryGetStringField(TEXT("originalObjectPath"), OriginalObjectPath);
		EntryObject->TryGetStringField(TEXT("originalPackageName"), OutEntry.OriginalPackageName);
		EntryObject->TryGetStringField(TEXT("plannedPackageName"), OutEntry.PlannedPackageName);
		EntryObject->TryGetStringField(TEXT("plannedObjectPath"), OutEntry.PlannedObjectPath);
		EntryObject->TryGetStringField(TEXT("thumbnailPath"), OutEntry.ThumbnailPath);
		OutEntry.OriginalObjectPath = FSoftObjectPath(OriginalObjectPath);
		OutEntry.Role = ParseRole(RoleText);
		return !OriginalObjectPath.IsEmpty() || !OutEntry.PlannedPackageName.IsEmpty();
	};
	auto IsUsableThumbnailPath = [](const FString& ThumbnailPath)
	{
		return !ThumbnailPath.IsEmpty() &&
			FPaths::FileExists(ThumbnailPath) &&
			!IsThumbnailCheckerboard(ThumbnailPath) &&
			!IsThumbnailMostlyEmpty(ThumbnailPath);
	};

	TSet<FString> AddedPackIds;
	TSet<FString> AddedViewIds;
	OutPackItems.Reserve(PackFiles.Num() * 4);
	auto IsVisibleFamilyRole = [](EMaterialVaultAssetRole Role)
	{
		return Role == EMaterialVaultAssetRole::RootMaterial ||
			Role == EMaterialVaultAssetRole::Material ||
			Role == EMaterialVaultAssetRole::MaterialInstance;
	};
	auto MakeEntryDisplayName = [](const FMaterialVaultAssetEntry& Entry)
	{
		FString Name = FPaths::GetBaseFilename(Entry.PlannedPackageName);
		if (Name.IsEmpty())
		{
			Name = FPaths::GetBaseFilename(Entry.OriginalPackageName);
		}
		if (Name.IsEmpty())
		{
			Name = FPackageName::ObjectPathToObjectName(Entry.PlannedObjectPath);
		}
		if (Name.IsEmpty())
		{
			Name = FPackageName::ObjectPathToObjectName(Entry.OriginalObjectPath.ToString());
		}
		return Name;
	};
	auto MakeEntryObjectPath = [&MakeEntryDisplayName](const FMaterialVaultAssetEntry& Entry)
	{
		if (!Entry.PlannedObjectPath.IsEmpty())
		{
			return Entry.PlannedObjectPath;
		}
		if (!Entry.PlannedPackageName.IsEmpty())
		{
			const FString Name = MakeEntryDisplayName(Entry);
			if (!Name.IsEmpty())
			{
				return Entry.PlannedPackageName + TEXT(".") + Name;
			}
		}
		return Entry.OriginalObjectPath.ToString();
	};
	for (const TSharedPtr<FJsonValue>& PackValue : *PacksArray)
	{
		if (!PackValue.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> PackObject = PackValue->AsObject();
		if (!PackObject.IsValid())
		{
			continue;
		}

		FString PackId;
		FString RelativePath;
		if (!PackObject->TryGetStringField(TEXT("id"), PackId) ||
			!PackObject->TryGetStringField(TEXT("relativePath"), RelativePath) ||
			PackId.IsEmpty())
		{
			continue;
		}

		FString* AbsolutePath = PackPathById.Find(PackId);
		if (!AbsolutePath)
		{
			FString NormalizedRelativePath = RelativePath;
			NormalizedRelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));
			const FString CandidatePath = FPaths::Combine(VaultRoot, NormalizedRelativePath);
			if (!FPaths::FileExists(CandidatePath))
			{
				continue;
			}
			PackPathById.Add(PackId, CandidatePath);
			PackSizeById.Add(PackId, IFileManager::Get().FileSize(*CandidatePath));
			AbsolutePath = PackPathById.Find(PackId);
		}
		if (!AbsolutePath || AddedPackIds.Contains(PackId))
		{
			continue;
		}

		FString DisplayName;
		FString Category;
		FString RootObjectPath;
		FString RootAssetClass;
		FString ThumbnailPath;
		PackObject->TryGetStringField(TEXT("displayName"), DisplayName);
		PackObject->TryGetStringField(TEXT("category"), Category);
		PackObject->TryGetStringField(TEXT("rootObjectPath"), RootObjectPath);
		PackObject->TryGetStringField(TEXT("rootAssetClass"), RootAssetClass);
		PackObject->TryGetStringField(TEXT("thumbnailPath"), ThumbnailPath);

		TSharedPtr<FMaterialVaultPackItem> PackItem = MakeShared<FMaterialVaultPackItem>();
		PackItem->Id = PackId;
		const FString FileDisplayName = FPaths::GetBaseFilename(*AbsolutePath);
		PackItem->DisplayName = FileDisplayName.IsEmpty() ? (DisplayName.IsEmpty() ? PackId : DisplayName) : FileDisplayName;
		PackItem->Category = NormalizeCategoryPath(Category);
		if (PackItem->Category == TEXT("未分类"))
		{
			const FString PathCategory = DetectPackCategory(*AbsolutePath, VaultRoot);
			PackItem->Category = PathCategory == TEXT("未分类") ? InferCategoryFromName(PackItem->DisplayName) : PathCategory;
		}
		PackItem->RelativePath = MakeVaultRelativePath(*AbsolutePath, VaultRoot);
		PackItem->AbsolutePath = *AbsolutePath;
		PackItem->SizeBytes = PackSizeById.FindRef(PackId);
		PackItem->RootObjectPath = RootObjectPath;
		PackItem->RootAssetClass = RootAssetClass;
		PackItem->ThumbnailPath = ThumbnailPath;
		const TArray<TSharedPtr<FJsonValue>>* FamilyArray = nullptr;
		if (PackObject->TryGetArrayField(TEXT("family"), FamilyArray) && FamilyArray && FamilyArray->Num() > 0)
		{
			PackItem->FamilyItem.Id = PackItem->Id;
			PackItem->FamilyItem.DisplayName = PackItem->DisplayName;
			PackItem->FamilyItem.Category = PackItem->Category;
			for (const TSharedPtr<FJsonValue>& EntryValue : *FamilyArray)
			{
				FMaterialVaultAssetEntry Entry;
				if (ParseFamilyEntry(EntryValue.IsValid() ? EntryValue->AsObject() : nullptr, Entry))
				{
					if (PackItem->FamilyItem.RootAsset.OriginalObjectPath.IsNull() && Entry.Role != EMaterialVaultAssetRole::Texture && Entry.Role != EMaterialVaultAssetRole::MaterialFunction)
					{
						PackItem->FamilyItem.RootAsset = Entry;
					}
					else
					{
						PackItem->FamilyItem.Dependencies.Add(Entry);
					}
				}
			}
			PackItem->bHasFamilyItem = !PackItem->FamilyItem.RootAsset.OriginalObjectPath.IsNull() || PackItem->FamilyItem.Dependencies.Num() > 0;
		}
		if (PackItem->bHasFamilyItem)
		{
			const FString PackDisplayName = PackItem->DisplayName;
			const FString PackCategory = PackItem->Category;
			int32 AddedForPack = 0;
			for (const FMaterialVaultAssetEntry& Entry : GetAllEntries(PackItem->FamilyItem))
			{
				if (!IsVisibleFamilyRole(Entry.Role))
				{
					continue;
				}

				const FString EntryName = MakeEntryDisplayName(Entry);
				if (EntryName.IsEmpty())
				{
					continue;
				}

				FString ViewId = PackId + TEXT("::") + EntryName;
				if (!Entry.PlannedPackageName.IsEmpty())
				{
					ViewId = PackId + TEXT("::") + Entry.PlannedPackageName;
				}
				if (AddedViewIds.Contains(ViewId))
				{
					continue;
				}

				TSharedPtr<FMaterialVaultPackItem> ViewItem = MakeShared<FMaterialVaultPackItem>();
				ViewItem->Id = ViewId;
				ViewItem->DisplayName = EntryName;
				ViewItem->PackDisplayName = PackDisplayName;
				ViewItem->SourcePackId = PackId;
				ViewItem->Category = (Entry.Role == EMaterialVaultAssetRole::RootMaterial || Entry.Role == EMaterialVaultAssetRole::Material)
					? TEXT("母材质")
					: NormalizeCategoryPath(PackCategory);
				ViewItem->RelativePath = PackItem->RelativePath;
				ViewItem->AbsolutePath = PackItem->AbsolutePath;
				ViewItem->SizeBytes = PackItem->SizeBytes;
				ViewItem->RootObjectPath = MakeEntryObjectPath(Entry);
				ViewItem->RootAssetClass = Entry.AssetClass;
				if (IsUsableThumbnailPath(Entry.ThumbnailPath))
				{
					ViewItem->ThumbnailPath = Entry.ThumbnailPath;
				}
				else
				{
					FString CachedThumbnailPath;
					ViewItem->ThumbnailPath =
						!Entry.ThumbnailPath.IsEmpty() && ExtractPackStoredThumbnail(PackItem->AbsolutePath, PackId, Entry.ThumbnailPath, CachedThumbnailPath)
						? CachedThumbnailPath
						: PackItem->ThumbnailPath;
				}
				if ((Entry.Role == EMaterialVaultAssetRole::RootMaterial || Entry.Role == EMaterialVaultAssetRole::Material) &&
					!IsUsableThumbnailPath(ViewItem->ThumbnailPath))
				{
					for (const FMaterialVaultAssetEntry& ThumbnailEntry : GetAllEntries(PackItem->FamilyItem))
					{
						if (ThumbnailEntry.Role != EMaterialVaultAssetRole::MaterialInstance)
						{
							continue;
						}
						if (IsUsableThumbnailPath(ThumbnailEntry.ThumbnailPath))
						{
							ViewItem->ThumbnailPath = ThumbnailEntry.ThumbnailPath;
							break;
						}
						FString CachedInstanceThumbnailPath;
						if (!ThumbnailEntry.ThumbnailPath.IsEmpty() &&
							ExtractPackStoredThumbnail(PackItem->AbsolutePath, PackId, ThumbnailEntry.ThumbnailPath, CachedInstanceThumbnailPath) &&
							IsUsableThumbnailPath(CachedInstanceThumbnailPath))
						{
							ViewItem->ThumbnailPath = CachedInstanceThumbnailPath;
							break;
						}
					}
					if (!IsUsableThumbnailPath(ViewItem->ThumbnailPath))
					{
						ViewItem->ThumbnailPath = PackItem->ThumbnailPath;
					}
				}
				ViewItem->FamilyItem = PackItem->FamilyItem;
				ViewItem->bHasFamilyItem = true;
				ViewItem->bIsFamilyEntry = true;
				ViewItem->EntryRole = Entry.Role;
				OutPackItems.Add(ViewItem);
				AddedViewIds.Add(ViewId);
				++AddedForPack;
			}
			if (AddedForPack > 0)
			{
				AddedPackIds.Add(PackId);
				continue;
			}
		}

		PackItem->PackDisplayName = PackItem->DisplayName;
		PackItem->SourcePackId = PackId;
		OutPackItems.Add(PackItem);
		AddedPackIds.Add(PackId);
	}

	if (OutPackItems.Num() == 0 && PackFiles.Num() > 0)
	{
		return false;
	}

	for (const FString& PackFile : PackFiles)
	{
		const FString PackId = FPaths::GetBaseFilename(PackFile);
		if (AddedPackIds.Contains(PackId))
		{
			continue;
		}

		TSharedPtr<FMaterialVaultPackItem> PackItem = MakeShared<FMaterialVaultPackItem>();
		PackItem->Id = PackId;
		PackItem->DisplayName = PackId;
		PackItem->PackDisplayName = PackId;
		PackItem->SourcePackId = PackId;
		PackItem->Category = NormalizeCategoryPath(DetectPackCategory(PackFile, VaultRoot));
		PackItem->RelativePath = MakeVaultRelativePath(PackFile, VaultRoot);
		PackItem->AbsolutePath = PackFile;
		PackItem->SizeBytes = IFileManager::Get().FileSize(*PackFile);
		OutPackItems.Add(PackItem);
		AddedPackIds.Add(PackId);
	}

	OutPackItems.Sort([](const TSharedPtr<FMaterialVaultPackItem>& Left, const TSharedPtr<FMaterialVaultPackItem>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid())
		{
			return Left.IsValid();
		}
		const FString LeftCategory = NormalizeCategoryPath(Left->Category);
		const FString RightCategory = NormalizeCategoryPath(Right->Category);
		if (LeftCategory != RightCategory)
		{
			if (LeftCategory == TEXT("母材质")) return true;
			if (RightCategory == TEXT("母材质")) return false;
			return LeftCategory < RightCategory;
		}
		if (Left->PackDisplayName != Right->PackDisplayName)
		{
			return Left->PackDisplayName < Right->PackDisplayName;
		}
		return Left->DisplayName < Right->DisplayName;
	});
	return true;
}

void SMaterialVaultWindow::GetCategoryChildren(TSharedPtr<FCategoryTreeItem> Item, TArray<TSharedPtr<FCategoryTreeItem>>& OutChildren) const
	{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
	}


void SMaterialVaultWindow::RefreshCategoryItems()
	{
		const FString PreviousCategory = SelectedCategory.IsEmpty() ? TEXT("全部") : SelectedCategory;
		CategoryItems.Reset();

		// Root node
		{
			TSharedPtr<FCategoryTreeItem> RootItem = MakeShared<FCategoryTreeItem>();
			RootItem->DisplayName = TEXT("全部");
			RootItem->FullPath = TEXT("全部");
			CategoryItems.Add(RootItem);
		}

		// Collect category paths
		TSet<FString> SeenPaths;
		for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : PackItems)
		{
			if (!PackItem.IsValid()) continue;
			const FString Cat = NormalizeCategoryPath(PackItem->Category);
			SeenPaths.Add(Cat);
		}

		const FString PacksRoot = FPaths::Combine(ResolveVaultRoot(), TEXT("Packs"));
		TArray<FString> CategoryDirectories;
		IFileManager::Get().FindFilesRecursive(CategoryDirectories, *PacksRoot, TEXT("*"), false, true);
		for (FString CategoryDirectory : CategoryDirectories)
		{
			FPaths::NormalizeFilename(CategoryDirectory);
			FString RelativeCategory = CategoryDirectory;
			if (FPaths::MakePathRelativeTo(RelativeCategory, *PacksRoot))
			{
				RelativeCategory.ReplaceInline(TEXT("\\"), TEXT("/"));
				RelativeCategory.RemoveFromStart(TEXT("/"));
				RelativeCategory.RemoveFromEnd(TEXT("/"));
				RelativeCategory = NormalizeCategoryPath(RelativeCategory);
				if (!RelativeCategory.IsEmpty())
				{
					SeenPaths.Add(RelativeCategory);
				}
			}
		}

		// Build tree from "Parent/Child" paths
		TMap<FString, TSharedPtr<FCategoryTreeItem>> NodeMap;
		for (const FString& FullPath : SeenPaths)
		{
			TArray<FString> Parts;
			FullPath.ParseIntoArray(Parts, TEXT("/"), true);
			if (Parts.IsEmpty()) continue;

			FString CurrentPath;
			TSharedPtr<FCategoryTreeItem> Parent;
			for (int32 i = 0; i < Parts.Num(); ++i)
			{
				CurrentPath = CurrentPath.IsEmpty() ? Parts[i] : CurrentPath + TEXT("/") + Parts[i];
				TSharedPtr<FCategoryTreeItem>* Existing = NodeMap.Find(CurrentPath);
				if (!Existing)
				{
					TSharedPtr<FCategoryTreeItem> N = MakeShared<FCategoryTreeItem>();
					N->DisplayName = Parts[i];
					N->FullPath = CurrentPath;
					if (Parent.IsValid())
						Parent->Children.Add(N);
					else
						CategoryItems.Add(N);
					NodeMap.Add(CurrentPath, N);
				}
				Parent = NodeMap[CurrentPath];
			}
		}

		if (CategoryItems.Num() > 0 && CategoryItems[0].IsValid() && CategoryItems[0]->FullPath == TEXT("全部"))
		{
			CategoryItems[0]->Count = PackItems.Num();
		}
		for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : PackItems)
		{
			if (!PackItem.IsValid())
			{
				continue;
			}
			const FString Cat = NormalizeCategoryPath(PackItem->Category);
			TArray<FString> Parts;
			Cat.ParseIntoArray(Parts, TEXT("/"), true);
			FString CurrentPath;
			for (const FString& Part : Parts)
			{
				CurrentPath = CurrentPath.IsEmpty() ? Part : CurrentPath + TEXT("/") + Part;
				if (TSharedPtr<FCategoryTreeItem>* Node = NodeMap.Find(CurrentPath))
				{
					(*Node)->Count++;
				}
			}
		}

		// Sort
		TMap<FString, int32> CategoryOrder;
		const TArray<FString>& SavedOrder = GetDefault<UMaterialVaultSettings>()->CategoryOrder;
		for (int32 Index = 0; Index < SavedOrder.Num(); ++Index)
		{
			CategoryOrder.Add(SavedOrder[Index], Index);
		}
		auto GetCategoryRank = [](const TSharedPtr<FCategoryTreeItem>& Item) -> int32
		{
			if (!Item.IsValid())
			{
				return 1;
			}
			if (Item->FullPath == TEXT("母材质"))
			{
				return 0;
			}
			if (Item->FullPath == TEXT("未分类"))
			{
				return 2;
			}
			return 1;
		};
		auto SortCategories = [&CategoryOrder, &GetCategoryRank](const TSharedPtr<FCategoryTreeItem>& A, const TSharedPtr<FCategoryTreeItem>& B) {
			const int32 ARank = GetCategoryRank(A);
			const int32 BRank = GetCategoryRank(B);
			if (ARank != BRank) return ARank < BRank;
			const int32* AOrder = A.IsValid() ? CategoryOrder.Find(A->FullPath) : nullptr;
			const int32* BOrder = B.IsValid() ? CategoryOrder.Find(B->FullPath) : nullptr;
			if (AOrder && BOrder) return *AOrder < *BOrder;
			if (AOrder) return true;
			if (BOrder) return false;
			return A->DisplayName < B->DisplayName;
		};
		TFunction<void(TArray<TSharedPtr<FCategoryTreeItem>>&)> SortTree = [&SortCategories, &SortTree](TArray<TSharedPtr<FCategoryTreeItem>>& ItemsToSort)
		{
			for (TSharedPtr<FCategoryTreeItem>& Child : ItemsToSort)
			{
				SortTree(Child->Children);
			}
			ItemsToSort.Sort(SortCategories);
		};
		SortTree(CategoryItems);
		CategoryItems.Sort([&CategoryOrder, &SortCategories](const auto& A, const auto& B) {
			if (A->FullPath == TEXT("全部")) return true;
			if (B->FullPath == TEXT("全部")) return false;
			return SortCategories(A, B);
		});

		// Restore previous selection
		SelectedCategory = TEXT("全部");
		if (TSharedPtr<FCategoryTreeItem>* Found = NodeMap.Find(PreviousCategory))
		{
			SelectedCategory = PreviousCategory;
			if (CategoryListView.IsValid())
			{
				CategoryListView->RequestTreeRefresh();
				CategoryListView->SetSelection(*Found);
				return;
			}
		}

		if (CategoryListView.IsValid())
			CategoryListView->RequestTreeRefresh();
	}


void SMaterialVaultWindow::RefreshPackFilter()
{
	FilteredPackItems.Reset();
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : PackItems)
	{
		if (!PackItem.IsValid())
		{
			continue;
		}

			const FString PackCat = NormalizeCategoryPath(PackItem->Category);
			if (SelectedCategory == TEXT("全部") || PackCat == SelectedCategory || PackCat.StartsWith(SelectedCategory + TEXT("/")))
		{
			FilteredPackItems.Add(PackItem);
		}
	}

	if (PackListView.IsValid())
	{
		PackListView->RequestListRefresh();
	}
}

void SMaterialVaultWindow::MoveCategoryBefore(const FString& SourceCategory, const FString& TargetCategory)
{
	if (SourceCategory.IsEmpty() || TargetCategory.IsEmpty() || SourceCategory == TargetCategory)
	{
		return;
	}

	UMaterialVaultSettings* Settings = GetMutableDefault<UMaterialVaultSettings>();
	TArray<FString>& Order = Settings->CategoryOrder;

	auto AddKnownCategories = [&Order](const TArray<TSharedPtr<FCategoryTreeItem>>& ItemsToAdd, auto& AddKnownCategoriesRef) -> void
	{
		for (const TSharedPtr<FCategoryTreeItem>& Item : ItemsToAdd)
		{
			if (Item.IsValid() && Item->FullPath != TEXT("全部"))
			{
				Order.AddUnique(Item->FullPath);
				AddKnownCategoriesRef(Item->Children, AddKnownCategoriesRef);
			}
		}
	};
	AddKnownCategories(CategoryItems, AddKnownCategories);

	Order.Remove(SourceCategory);
	const int32 TargetIndex = Order.Find(TargetCategory);
	if (TargetIndex == INDEX_NONE)
	{
		Order.Add(SourceCategory);
	}
	else
	{
		Order.Insert(SourceCategory, TargetIndex);
	}
	Settings->SaveConfig();

	RefreshCategoryItems();
	if (CategoryListView.IsValid())
	{
		CategoryListView->RequestTreeRefresh();
	}
}

void SMaterialVaultWindow::MoveSelectedPackToCategory(FString Category)
{
	TArray<TSharedPtr<FMaterialVaultPackItem>> SelectedPacks = GetSelectedPackItems();
	if (SelectedPacks.IsEmpty() && SelectedPackItem.IsValid())
	{
		SelectedPacks.Add(SelectedPackItem);
	}
	if (SelectedPacks.IsEmpty())
	{
		return;
	}

	const FString VaultRoot = ResolveVaultRoot();
	const FString SafeCategory = NormalizeCategoryPath(Category);
	if (SafeCategory == TEXT("母材质"))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("“母材质”是按材质族内容生成的虚拟分类，不会作为真实文件夹分类。请选择石材、木材、布料等实际分类。")));
		return;
	}
	const FString TargetDir = SafeCategory == TEXT("未分类")
		? FPaths::Combine(VaultRoot, TEXT("Packs"))
		: FPaths::Combine(VaultRoot, TEXT("Packs"), SafeCategory);
	IFileManager::Get().MakeDirectory(*TargetDir, true);

	FScopedSlowTask SlowTask(static_cast<float>(SelectedPacks.Num()), FText::Format(
		FText::FromString(TEXT("正在移动到分类：{0}")),
		FText::FromString(SafeCategory)));
	SlowTask.MakeDialog(true);

	int32 MovedCount = 0;
	int32 FailedCount = 0;
	TArray<FString> OldDirectories;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : SelectedPacks)
	{
		if (!PackItem.IsValid() || PackItem->AbsolutePath.IsEmpty())
		{
			continue;
		}

		SlowTask.EnterProgressFrame(1.0f, FText::FromString(PackItem->DisplayName));
		const FString OldDirectory = FPaths::GetPath(PackItem->AbsolutePath);
		OldDirectories.AddUnique(OldDirectory);

		FString TargetPath = FPaths::Combine(TargetDir, FPaths::GetCleanFilename(PackItem->AbsolutePath));
		const FString BaseName = FPaths::GetBaseFilename(PackItem->AbsolutePath);
		const FString Extension = FPaths::GetExtension(PackItem->AbsolutePath, true);
		int32 Suffix = 2;
		while (FPaths::FileExists(TargetPath) &&
			FPaths::ConvertRelativePathToFull(TargetPath) != FPaths::ConvertRelativePathToFull(PackItem->AbsolutePath))
		{
			TargetPath = FPaths::Combine(TargetDir, FString::Printf(TEXT("%s_%02d%s"), *BaseName, Suffix++, *Extension));
		}

		if (FPaths::ConvertRelativePathToFull(TargetPath) == FPaths::ConvertRelativePathToFull(PackItem->AbsolutePath))
		{
			continue;
		}

		if (IFileManager::Get().Move(*TargetPath, *PackItem->AbsolutePath, true, true))
		{
			++MovedCount;
		}
		else
		{
			++FailedCount;
		}
	}

	const FString PacksRoot = FPaths::Combine(VaultRoot, TEXT("Packs"));
	for (const FString& OldDirectory : OldDirectories)
	{
		if (OldDirectory.StartsWith(PacksRoot) && OldDirectory != PacksRoot)
		{
			TArray<FString> Children;
			IFileManager::Get().FindFiles(Children, *FPaths::Combine(OldDirectory, TEXT("*")), true, true);
			if (Children.IsEmpty())
			{
				IFileManager::Get().DeleteDirectory(*OldDirectory, false, true);
			}
		}
	}

	SelectedPackItem.Reset();
	LoadPackItems();
	if (PackListView.IsValid())
	{
		PackListView->RequestListRefresh();
	}
	if (CategoryListView.IsValid())
	{
		CategoryListView->RequestTreeRefresh();
	}

	FString IndexPath;
	int32 PackCount = 0;
	FText Error;
	RebuildPackIndex(VaultRoot, IndexPath, PackCount, Error);

	if (FailedCount > 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			FText::FromString(TEXT("手动分类完成。\n移动成功：{0}\n失败：{1}")),
			FText::AsNumber(MovedCount),
			FText::AsNumber(FailedCount)));
	}
}

void SMaterialVaultWindow::AutoClassifyPacks()
{
	const FString VaultRoot = ResolveVaultRoot();
	FText Error;
	if (!EnsureVaultLayout(VaultRoot, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return;
	}

	LoadPackItems();
	int32 MovedCount = 0;
	TMap<FString, TSharedPtr<FMaterialVaultPackItem>> UniquePacksByPath;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : PackItems)
	{
		if (!PackItem.IsValid() || PackItem->AbsolutePath.IsEmpty())
		{
			continue;
		}
		UniquePacksByPath.FindOrAdd(FPaths::ConvertRelativePathToFull(PackItem->AbsolutePath), PackItem);
	}

	TArray<TSharedPtr<FMaterialVaultPackItem>> UniquePacks;
	UniquePacksByPath.GenerateValueArray(UniquePacks);

	FScopedSlowTask SlowTask(static_cast<float>(UniquePacks.Num()), FText::FromString(TEXT("正在自动分类材质包...")));
	SlowTask.MakeDialog(true);

	int32 ProcessedCount = 0;
	for (const TSharedPtr<FMaterialVaultPackItem>& PackItem : UniquePacks)
	{
		if (!PackItem.IsValid())
		{
			continue;
		}
		if (SlowTask.ShouldCancel())
		{
			break;
		}

		++ProcessedCount;
		SlowTask.EnterProgressFrame(1.0f, FText::Format(
			FText::FromString(TEXT("正在自动分类 {0}/{1}：{2}")),
			FText::AsNumber(ProcessedCount),
			FText::AsNumber(UniquePacks.Num()),
			FText::FromString(PackItem->DisplayName)));
		FString Category;
		FMaterialVaultItem ManifestItem;
		if (ExtractPackItemFromManifest(PackItem->AbsolutePath, ManifestItem))
		{
			Category = InferCategoryFromItem(ManifestItem);
		}
		if (Category.IsEmpty() || Category == TEXT("未分类"))
		{
			const FString PackName = !PackItem->PackDisplayName.IsEmpty()
				? PackItem->PackDisplayName
				: FPaths::GetBaseFilename(PackItem->AbsolutePath);
			Category = InferCategoryFromName(PackName);
		}
		Category = NormalizeCategoryPath(Category);
		const FString TargetDir = Category == TEXT("未分类")
			? FPaths::Combine(VaultRoot, TEXT("Packs"))
			: FPaths::Combine(VaultRoot, TEXT("Packs"), Category);
		IFileManager::Get().MakeDirectory(*TargetDir, true);

		const FString TargetPath = FPaths::Combine(TargetDir, FPaths::GetCleanFilename(PackItem->AbsolutePath));
		if (FPaths::ConvertRelativePathToFull(TargetPath) == FPaths::ConvertRelativePathToFull(PackItem->AbsolutePath))
		{
			continue;
		}

		if (IFileManager::Get().Move(*TargetPath, *PackItem->AbsolutePath, true, true))
		{
			++MovedCount;
		}
	}

	SlowTask.EnterProgressFrame(0.0f, FText::FromString(TEXT("正在刷新材质包索引...")));
	LoadPackItems();
	if (PackListView.IsValid())
	{
		PackListView->RequestListRefresh();
	}
	if (CategoryListView.IsValid())
	{
		CategoryListView->RequestTreeRefresh();
	}

	TArray<FString> CategoryDirectories;
	IFileManager::Get().FindFilesRecursive(CategoryDirectories, *FPaths::Combine(VaultRoot, TEXT("Packs")), TEXT("*"), false, true);
	CategoryDirectories.Sort([](const FString& Left, const FString& Right)
	{
		return Left.Len() > Right.Len();
	});
	for (const FString& CategoryDirectory : CategoryDirectories)
	{
		TArray<FString> Children;
		IFileManager::Get().FindFiles(Children, *FPaths::Combine(CategoryDirectory, TEXT("*")), true, true);
		if (Children.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*CategoryDirectory, false, false);
		}
	}
	LoadPackItems();

	FString IndexPath;
	int32 PackCount = 0;
	RebuildPackIndex(VaultRoot, IndexPath, PackCount, Error);

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::Format(FText::FromString(TEXT("自动分类完成，移动了 {0} 个材质包。")), FText::AsNumber(MovedCount)));
}

bool SMaterialVaultWindow::ExportManifest(const FMaterialVaultItem& Item, FString& OutFilePath, FText& OutError)
{
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	const FString VaultRoot = ResolveVaultRoot();
	if (!EnsureVaultLayout(VaultRoot, OutError))
	{
		return false;
	}

	const FString ManifestDir = FPaths::Combine(VaultRoot, TEXT("Manifests"));
	IFileManager::Get().MakeDirectory(*ManifestDir, true);
	OutFilePath = FPaths::Combine(ManifestDir, Item.Id + TEXT(".json"));
	const FString ManifestRelativePath = MakeVaultRelativePath(OutFilePath, VaultRoot);
	const FString Json = BuildManifestJson(Item, VaultRoot, ManifestRelativePath);

	if (!FFileHelper::SaveStringToFile(Json, *OutFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FText::Format(FText::FromString(TEXT("无法写入清单文件：\n{0}")), FText::FromString(OutFilePath));
		return false;
	}

	return true;
}

bool SMaterialVaultWindow::CreateMvpack(const FMaterialVaultItem& Item, FString& OutPackPath, FText& OutError, FScopedSlowTask* ParentTask, float ParentBudget)
{
	const FString VaultRoot = ResolveVaultRoot();
	if (!EnsureVaultLayout(VaultRoot, OutError))
	{
		return false;
	}

	OutPackPath = GetPackPathForItem(Item);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPackPath), true);

	const float ThumbBudget = ParentTask ? ParentBudget * 0.2f : 0.0f;
	const float WriteBudget = ParentTask ? ParentBudget - ThumbBudget : 0.0f;

	if (ParentTask && ThumbBudget > 0.0f)
	{
		ParentTask->EnterProgressFrame(ThumbBudget, FText::Format(
			FText::FromString(TEXT("[{0}] 生成材质球缩略图...")),
			FText::FromString(Item.DisplayName)));
	}

	TArray<TPair<FString, FString>> FilesToPack;
	auto GetStagingPriority = [](EMaterialVaultAssetRole Role)
	{
		switch (Role)
		{
		case EMaterialVaultAssetRole::Texture:
		case EMaterialVaultAssetRole::MaterialFunction:
			return 0;
		case EMaterialVaultAssetRole::Material:
		case EMaterialVaultAssetRole::RootMaterial:
			return 1;
		case EMaterialVaultAssetRole::MaterialInstance:
			return 2;
		default:
			return 1;
		}
	};

	TArray<FMaterialVaultAssetEntry> Entries = GetAllEntries(Item);
	Entries.Sort([&GetStagingPriority](const FMaterialVaultAssetEntry& Left, const FMaterialVaultAssetEntry& Right)
	{
		const int32 LeftPriority = GetStagingPriority(Left.Role);
		const int32 RightPriority = GetStagingPriority(Right.Role);
		if (LeftPriority != RightPriority)
		{
			return LeftPriority < RightPriority;
		}
		return Left.OriginalObjectPath.ToString() < Right.OriginalObjectPath.ToString();
	});
	for (const FMaterialVaultAssetEntry& Entry : Entries)
	{
		const FString StagingPackageName = GetStagingPackageName(Entry);
		AddPackageFiles(StagingPackageName, TEXT("Content"), FilesToPack);
	}

	FMaterialVaultItem ManifestItem = Item;

	auto IsThumbnailRole = [](const FMaterialVaultAssetEntry& Entry)
	{
		return Entry.Role == EMaterialVaultAssetRole::RootMaterial ||
			Entry.Role == EMaterialVaultAssetRole::Material ||
			Entry.Role == EMaterialVaultAssetRole::MaterialInstance;
	};

	TSet<FString> UsedThumbnailArchivePaths;
	auto MakeThumbnailArchivePath = [&UsedThumbnailArchivePaths](const FMaterialVaultAssetEntry& Entry)
	{
		FString BaseName = FPaths::GetBaseFilename(Entry.PlannedPackageName);
		if (BaseName.IsEmpty())
		{
			BaseName = FPaths::GetBaseFilename(Entry.OriginalPackageName);
		}
		BaseName.ReplaceInline(TEXT("/"), TEXT("_"));
		BaseName.ReplaceInline(TEXT("\\"), TEXT("_"));
		BaseName.ReplaceInline(TEXT(":"), TEXT("_"));
		FString ArchivePath = FString::Printf(TEXT("thumbnails/%s.png"), *BaseName);
		int32 Suffix = 2;
		while (UsedThumbnailArchivePaths.Contains(ArchivePath))
		{
			ArchivePath = FString::Printf(TEXT("thumbnails/%s_%d.png"), *BaseName, Suffix++);
		}
		UsedThumbnailArchivePaths.Add(ArchivePath);
		return ArchivePath;
	};

	auto IsUtilityTextureName = [](const FString& TextureName)
	{
		return TextureName.Contains(TEXT("Normal"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("_N"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Rough"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Metallic"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Metalness"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Specular"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("AO"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("ARM"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("ORM"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Height"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Displace"), ESearchCase::IgnoreCase);
	};

	auto IsBaseColorTextureName = [](const FString& TextureName)
	{
		return TextureName.Contains(TEXT("BaseColor"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Base_Color"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Base Color"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Albedo"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Diffuse"), ESearchCase::IgnoreCase) ||
			TextureName.Contains(TEXT("Color"), ESearchCase::IgnoreCase);
	};

	auto NormalizeMatchText = [](FString Value)
	{
		Value = Value.ToLower();
		Value.ReplaceInline(TEXT("mi_pbr_"), TEXT(""));
		Value.ReplaceInline(TEXT("mi_"), TEXT(""));
		Value.ReplaceInline(TEXT("m_"), TEXT(""));
		Value.ReplaceInline(TEXT("t_"), TEXT(""));
		Value.ReplaceInline(TEXT("basecolor"), TEXT(""));
		Value.ReplaceInline(TEXT("base_color"), TEXT(""));
		Value.ReplaceInline(TEXT("albedo"), TEXT(""));
		Value.ReplaceInline(TEXT("diffuse"), TEXT(""));
		Value.ReplaceInline(TEXT("color"), TEXT(""));
		for (TCHAR& Char : Value)
		{
			if (!FChar::IsAlnum(Char))
			{
				Char = TEXT(' ');
			}
		}
		return Value;
	};

	auto FindBestTextureForEntry = [&Entries, &IsUtilityTextureName, &IsBaseColorTextureName, &NormalizeMatchText](const FMaterialVaultAssetEntry& MaterialEntry)
	{
		FMaterialVaultAssetEntry BestTextureEntry;
		int32 BestScore = MIN_int32;
		const FString MaterialName = FPaths::GetBaseFilename(MaterialEntry.OriginalPackageName);
		const FString MaterialMatchText = NormalizeMatchText(MaterialName);
		TArray<FString> MaterialTokens;
		MaterialMatchText.ParseIntoArray(MaterialTokens, TEXT(" "), true);

		for (const FMaterialVaultAssetEntry& TextureEntry : Entries)
		{
			if (TextureEntry.Role != EMaterialVaultAssetRole::Texture)
			{
				continue;
			}

			const FString TextureName = FPaths::GetBaseFilename(TextureEntry.OriginalPackageName);
			const FString TextureSearchText = NormalizeMatchText(TextureEntry.OriginalPackageName + TEXT(" ") + TextureEntry.PlannedPackageName);
			int32 Score = 0;
			if (IsBaseColorTextureName(TextureName))
			{
				Score += 1000;
			}
			if (IsUtilityTextureName(TextureName))
			{
				Score -= 500;
			}
			for (const FString& Token : MaterialTokens)
			{
				if (Token.Len() >= 3 && TextureSearchText.Contains(Token))
				{
					Score += 80;
				}
			}
			if (TextureSearchText.Contains(MaterialMatchText) && !MaterialMatchText.IsEmpty())
			{
				Score += 400;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				BestTextureEntry = TextureEntry;
			}
		}
		return BestScore > MIN_int32 ? BestTextureEntry : FMaterialVaultAssetEntry();
	};

	FString CoverThumbnailPath;
	auto GenerateEntryThumbnail = [&](FMaterialVaultAssetEntry& ManifestEntry)
	{
		if (!IsThumbnailRole(ManifestEntry))
		{
			return;
		}

		const FString ArchivePath = MakeThumbnailArchivePath(ManifestEntry);
		FString TempThumbnailPath;
		FText ThumbnailError;
		const FMaterialVaultAssetEntry RepresentativeTextureEntry = FindBestTextureForEntry(ManifestEntry);
		bool bCreatedThumbnail = false;
		if (ManifestEntry.Role == EMaterialVaultAssetRole::MaterialInstance && !RepresentativeTextureEntry.OriginalObjectPath.IsNull())
		{
			bCreatedThumbnail = CreateAssetThumbnail(RepresentativeTextureEntry, FPaths::GetCleanFilename(ArchivePath), TempThumbnailPath, ThumbnailError, true);
		}
		if (!bCreatedThumbnail)
		{
			bCreatedThumbnail = CreateAssetThumbnail(ManifestEntry, FPaths::GetCleanFilename(ArchivePath), TempThumbnailPath, ThumbnailError, true);
		}
		if (bCreatedThumbnail)
		{
			ManifestEntry.ThumbnailPath = ArchivePath;
			FilesToPack.Add(TPair<FString, FString>(ArchivePath, TempThumbnailPath));
			if (CoverThumbnailPath.IsEmpty())
			{
				CoverThumbnailPath = TempThumbnailPath;
			}
		}
		else if (!RepresentativeTextureEntry.OriginalObjectPath.IsNull() &&
			CreateAssetThumbnail(RepresentativeTextureEntry, FPaths::GetCleanFilename(ArchivePath), TempThumbnailPath, ThumbnailError, true))
		{
			ManifestEntry.ThumbnailPath = ArchivePath;
			FilesToPack.Add(TPair<FString, FString>(ArchivePath, TempThumbnailPath));
			if (CoverThumbnailPath.IsEmpty())
			{
				CoverThumbnailPath = TempThumbnailPath;
			}
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Material Vault thumbnail skipped for %s: %s"), *ManifestEntry.OriginalObjectPath.ToString(), *ThumbnailError.ToString());
		}
	};

	GenerateEntryThumbnail(ManifestItem.RootAsset);
	for (int32 DependencyIndex = 0; DependencyIndex < ManifestItem.Dependencies.Num(); ++DependencyIndex)
	{
		GenerateEntryThumbnail(ManifestItem.Dependencies[DependencyIndex]);
	}

	if (!CoverThumbnailPath.IsEmpty())
	{
		FilesToPack.Add(TPair<FString, FString>(TEXT("thumbnail.png"), CoverThumbnailPath));
	}

	const FString PackRelativePath = MakeVaultRelativePath(OutPackPath, VaultRoot);
	const FString ManifestJson = BuildManifestJson(ManifestItem, VaultRoot, PackRelativePath);
	const FString TempManifestPath = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("MaterialVault"), Item.Id + TEXT("_manifest.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TempManifestPath), true);
	if (!FFileHelper::SaveStringToFile(ManifestJson, *TempManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FText::FromString(TEXT("\u65e0\u6cd5\u751f\u6210\u4e34\u65f6 manifest.json\u3002"));
		return false;
	}
	FilesToPack.Add(TPair<FString, FString>(TEXT("manifest.json"), TempManifestPath));

	if (ParentTask && WriteBudget > 0.0f)
	{
		ParentTask->EnterProgressFrame(WriteBudget, FText::Format(
			FText::FromString(TEXT("[{0}] \u5199\u5165 {1} \u4e2a\u6587\u4ef6\u5230 .mvpack...")),
			FText::FromString(Item.DisplayName),
			FText::AsNumber(FilesToPack.Num())));
	}

	if (!WriteMvpackFile(OutPackPath, FilesToPack, OutError))
	{
		return false;
	}

	return true;
}
bool SMaterialVaultWindow::CreatePackThumbnail(const FMaterialVaultItem& Item, FString& OutThumbnailPath, FText& OutError)
{
	return CreateAssetThumbnail(Item.RootAsset, Item.Id + TEXT(".png"), OutThumbnailPath, OutError, true);
}

bool SMaterialVaultWindow::CreateAssetThumbnail(const FMaterialVaultAssetEntry& Entry, const FString& OutputFileName, FString& OutThumbnailPath, FText& OutError, bool bAllowRender)
{
	OutThumbnailPath.Reset();

	const int32 Width = 512;
	const int32 Height = 512;
	const FString ThumbnailDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("MaterialVault"), TEXT("Thumbnails"));
	IFileManager::Get().MakeDirectory(*ThumbnailDir, true);
	FString SafeFileName = OutputFileName.IsEmpty() ? (FPaths::GetBaseFilename(Entry.OriginalPackageName) + TEXT(".png")) : OutputFileName;
	SafeFileName.ReplaceInline(TEXT("/"), TEXT("_"));
	SafeFileName.ReplaceInline(TEXT("\\"), TEXT("_"));
	SafeFileName.ReplaceInline(TEXT(":"), TEXT("_"));
	if (!SafeFileName.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
	{
		SafeFileName += TEXT(".png");
	}
	const FString CandidateThumbnailPath = FPaths::Combine(ThumbnailDir, SafeFileName);

	auto SaveObjectThumbnailToFile = [&CandidateThumbnailPath, &OutError](const FObjectThumbnail& ObjectThumbnail) -> bool
	{
		const TArray<uint8>& RawBytes = ObjectThumbnail.GetUncompressedImageData();
		if (ObjectThumbnail.IsEmpty() || RawBytes.Num() == 0)
		{
			return false;
		}

		TArray<uint8> PngBytes = RawBytes;
		for (int64 ByteIndex = 3; ByteIndex < PngBytes.Num(); ByteIndex += 4)
		{
			PngBytes[ByteIndex] = 255;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!ImageWrapper.IsValid() ||
			!ImageWrapper->SetRaw(
				PngBytes.GetData(),
				PngBytes.Num(),
				ObjectThumbnail.GetImageWidth(),
				ObjectThumbnail.GetImageHeight(),
				ERGBFormat::BGRA,
				8))
		{
			OutError = FText::FromString(TEXT("无法生成材质包缩略图。"));
			return false;
		}

		if (!FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(), *CandidateThumbnailPath))
		{
			OutError = FText::FromString(TEXT("无法保存临时缩略图。"));
			return false;
		}

		return true;
	};

	auto SaveTextureSourceToFile = [&CandidateThumbnailPath, Width, Height, &OutError](UTexture2D* Texture) -> bool
	{
		if (!Texture || !Texture->Source.IsValid())
		{
			return false;
		}

		FImage SourceImage;
		if (!Texture->Source.GetMipImage(SourceImage, 0))
		{
			return false;
		}

		FImage OutputImage;
		if (SourceImage.SizeX > Width || SourceImage.SizeY > Height)
		{
			OutputImage = FImage(Width, Height, ERawImageFormat::BGRA8, Texture->SRGB ? EGammaSpace::sRGB : EGammaSpace::Linear);
			FImageCore::ResizeImage(SourceImage, OutputImage, FImageCore::EResizeImageFilter::Default);
		}
		else
		{
			OutputImage = FImage(SourceImage.SizeX, SourceImage.SizeY, ERawImageFormat::BGRA8, Texture->SRGB ? EGammaSpace::sRGB : EGammaSpace::Linear);
			FImageCore::CopyImage(SourceImage, OutputImage);
		}
		FImageCore::SetAlphaOpaque(OutputImage);

		if (!FImageUtils::SaveImageByExtension(*CandidateThumbnailPath, OutputImage, 90))
		{
			OutError = FText::FromString(TEXT("无法从贴图源数据保存缩略图。"));
			return false;
		}

		return true;
	};

	auto RenderObjectThumbnailToFile = [&SaveObjectThumbnailToFile, Width, Height](UObject* ThumbnailObject) -> bool
	{
		if (!ThumbnailObject)
		{
			return false;
		}

		FObjectThumbnail ObjectThumbnail;
		ThumbnailTools::RenderThumbnail(
			ThumbnailObject,
			Width,
			Height,
			ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush,
			nullptr,
			&ObjectThumbnail);

		return SaveObjectThumbnailToFile(ObjectThumbnail);
	};

	if (bAllowRender)
	{
		UObject* RootAsset = Entry.OriginalObjectPath.TryLoad();
		if (!RootAsset)
		{
			OutError = FText::Format(
				FText::FromString(TEXT("\u65e0\u6cd5\u52a0\u8f7d\u8d44\u4ea7\u7528\u4e8e\u751f\u6210\u7f29\u7565\u56fe\uff1a\n{0}")),
				FText::FromString(Entry.OriginalObjectPath.ToString()));
			return false;
		}

		const bool bAllowCommandletRender = FParse::Param(FCommandLine::Get(), TEXT("MaterialVaultRenderThumbnails"));
		const bool bCanRenderThumbnails = !IsRunningCommandlet() || bAllowCommandletRender;
		UMaterialInterface* Material = Cast<UMaterialInterface>(RootAsset);
		if (!Material && bCanRenderThumbnails)
		{
			if (UTexture2D* Texture2D = Cast<UTexture2D>(RootAsset))
			{
				if (SaveTextureSourceToFile(Texture2D) &&
					!IsThumbnailCheckerboard(CandidateThumbnailPath) &&
					!IsThumbnailMostlyEmpty(CandidateThumbnailPath))
				{
					OutThumbnailPath = CandidateThumbnailPath;
					return true;
				}
				IFileManager::Get().Delete(*CandidateThumbnailPath, false, true);
			}

			if (RenderObjectThumbnailToFile(RootAsset) &&
				!IsThumbnailCheckerboard(CandidateThumbnailPath) &&
				!IsThumbnailMostlyEmpty(CandidateThumbnailPath))
			{
				OutThumbnailPath = CandidateThumbnailPath;
				return true;
			}
			IFileManager::Get().Delete(*CandidateThumbnailPath, false, true);
		}
		if (bCanRenderThumbnails)
		{
			if (!Material)
			{
				OutError = FText::Format(
					FText::FromString(TEXT("\u8d44\u4ea7\u6e32\u67d3\u540e\u4ecd\u50cf\u5360\u4f4d\u56fe\uff1a\n{0}")),
					FText::FromString(Entry.OriginalObjectPath.ToString()));
				return false;
			}

			TArray<UObject*> ObjectsToFinish;
			ObjectsToFinish.Add(Material);
			TArray<UTexture*> TexturesToStream;
			UTexture* RepresentativeTexture = nullptr;
			const bool bLightweightCommandletRender = IsRunningCommandlet() && bAllowCommandletRender;

			TArray<FMaterialParameterInfo> TextureParameterInfos;
			TArray<FGuid> TextureParameterIds;
			Material->GetAllTextureParameterInfo(TextureParameterInfos, TextureParameterIds);
			for (const FMaterialParameterInfo& ParameterInfo : TextureParameterInfos)
			{
				UTexture* Texture = nullptr;
				if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Texture, true) && Texture)
				{
					const FString ParameterName = ParameterInfo.Name.ToString();
					const bool bLooksLikeBaseColor =
						ParameterName.Contains(TEXT("BaseColor"), ESearchCase::IgnoreCase) ||
						ParameterName.Contains(TEXT("Base Color"), ESearchCase::IgnoreCase) ||
						ParameterName.Contains(TEXT("Albedo"), ESearchCase::IgnoreCase) ||
						ParameterName.Contains(TEXT("Diffuse"), ESearchCase::IgnoreCase) ||
						ParameterName.Contains(TEXT("Color"), ESearchCase::IgnoreCase);
					if (!RepresentativeTexture || bLooksLikeBaseColor)
					{
						RepresentativeTexture = Texture;
					}
					if (!bLightweightCommandletRender)
					{
						ObjectsToFinish.Add(Texture);
						TexturesToStream.AddUnique(Texture);
					}
				}
			}
			if (!RepresentativeTexture)
			{
				TArray<UTexture*> UsedTextures;
				Material->GetUsedTextures(UsedTextures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, false);
				for (UTexture* UsedTexture : UsedTextures)
				{
					if (!UsedTexture)
					{
						continue;
					}

					const FString TextureName = UsedTexture->GetName();
					const bool bLooksLikeUtility =
						TextureName.Contains(TEXT("Normal"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("_N"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("Rough"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("Metal"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("Specular"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("AO"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("ARM"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("ORM"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("Height"), ESearchCase::IgnoreCase) ||
						TextureName.Contains(TEXT("Displace"), ESearchCase::IgnoreCase);
					if (!bLooksLikeUtility)
					{
						RepresentativeTexture = UsedTexture;
						break;
					}
				}
				if (!RepresentativeTexture && UsedTextures.Num() > 0)
				{
					RepresentativeTexture = UsedTextures[0];
				}
				if (RepresentativeTexture && !bLightweightCommandletRender)
				{
					ObjectsToFinish.Add(RepresentativeTexture);
					TexturesToStream.AddUnique(RepresentativeTexture);
				}
			}

			auto PrepareMaterialForThumbnail = [&ObjectsToFinish, &TexturesToStream, Material, bLightweightCommandletRender]()
			{
				if (UPackage* Package = Material->GetOutermost())
				{
					const FString PackageName = Package->GetName();
					FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
					AssetRegistryModule.Get().ScanModifiedAssetFiles({ PackageName });
				}

				FAssetCompilingManager::Get().FinishCompilationForObjects(ObjectsToFinish);
				Material->EnsureIsComplete();
				if (!bLightweightCommandletRender)
				{
					Material->SetForceMipLevelsToBeResident(true, true, 30.0f, 0, true);
					for (UTexture* Texture : TexturesToStream)
					{
						if (Texture)
						{
							Texture->SetForceMipLevelsToBeResident(30.0f);
							Texture->WaitForStreaming();
						}
					}
					FlushRenderingCommands();
				}
			};

			constexpr int32 MaxAttempts = 8;
			for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
			{
				PrepareMaterialForThumbnail();
				if (RenderObjectThumbnailToFile(Material) &&
					!IsThumbnailCheckerboard(CandidateThumbnailPath) &&
					!IsThumbnailMostlyEmpty(CandidateThumbnailPath))
				{
					OutThumbnailPath = CandidateThumbnailPath;
					return true;
				}

				FAssetCompilingManager::Get().ProcessAsyncTasks(false);
				FPlatformProcess::Sleep(0.15f);
			}

			if (RepresentativeTexture)
			{
				if (!bLightweightCommandletRender)
				{
					RepresentativeTexture->SetForceMipLevelsToBeResident(30.0f);
					RepresentativeTexture->WaitForStreaming();
					FlushRenderingCommands();
				}
				if (RenderObjectThumbnailToFile(RepresentativeTexture) &&
					!IsThumbnailCheckerboard(CandidateThumbnailPath) &&
					!IsThumbnailMostlyEmpty(CandidateThumbnailPath))
				{
					OutThumbnailPath = CandidateThumbnailPath;
					return true;
				}
				IFileManager::Get().Delete(*CandidateThumbnailPath, false, true);
			}
		}
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(Entry.OriginalObjectPath);
	if (AssetData.IsValid())
	{
		if (const FObjectThumbnail* CachedThumbnail = ThumbnailTools::FindCachedThumbnail(AssetData.GetFullName()))
		{
			if (SaveObjectThumbnailToFile(*CachedThumbnail) &&
				!IsThumbnailCheckerboard(CandidateThumbnailPath) &&
				!IsThumbnailMostlyEmpty(CandidateThumbnailPath))
			{
				OutThumbnailPath = CandidateThumbnailPath;
				return true;
			}
			IFileManager::Get().Delete(*CandidateThumbnailPath, false, true);
		}

		FObjectThumbnail StoredThumbnail;
		if (ThumbnailTools::LoadThumbnailFromPackage(AssetData, StoredThumbnail) &&
			SaveObjectThumbnailToFile(StoredThumbnail) &&
			!IsThumbnailCheckerboard(CandidateThumbnailPath) &&
			!IsThumbnailMostlyEmpty(CandidateThumbnailPath))
		{
			OutThumbnailPath = CandidateThumbnailPath;
			return true;
		}
		IFileManager::Get().Delete(*CandidateThumbnailPath, false, true);
	}

	OutError = bAllowRender
		? FText::FromString(TEXT("\u7f29\u7565\u56fe\u6e32\u67d3\u540e\u4ecd\u50cf\u5360\u4f4d\u56fe\u6216\u9ed1\u56fe\uff0c\u5df2\u8df3\u8fc7\u4fdd\u5b58\uff0c\u907f\u514d\u7528\u8d34\u56fe\u5047\u5192\u6750\u8d28\u7403\u7f29\u7565\u56fe\u3002"))
		: FText::FromString(TEXT("未找到当前项目已有缩略图，已跳过渲染以避免大贴图 DDC 导致内存崩溃。"));
	IFileManager::Get().Delete(*CandidateThumbnailPath, false, true);
	return false;
}
void SMaterialVaultWindow::ClearPackThumbnailCache(const FString& PackId)
{
	if (PackId.IsEmpty())
	{
		return;
	}

	const FString ThumbnailRoot = FPaths::Combine(ResolveVaultRoot(), TEXT("Thumbnails"));
	TArray<FString> CachedThumbnails;
	IFileManager::Get().FindFiles(CachedThumbnails, *FPaths::Combine(ThumbnailRoot, PackId + TEXT("_*.png")), true, false);
	for (const FString& CachedThumbnail : CachedThumbnails)
	{
		IFileManager::Get().Delete(*FPaths::Combine(ThumbnailRoot, CachedThumbnail), false, true);
	}
}


bool SMaterialVaultWindow::ExtractPackThumbnail(const FString& PackPath, const FString& PackId, FString& OutThumbnailPath)
{
	OutThumbnailPath.Reset();
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		return false;
	}

	uint8 Magic[8] = {};
	Reader->Serialize(Magic, UE_ARRAY_COUNT(Magic));
	const uint8 ExpectedMagic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	if (FMemory::Memcmp(Magic, ExpectedMagic, UE_ARRAY_COUNT(ExpectedMagic)) != 0)
	{
		return false;
	}

	int32 Version = 0;
	int32 FileCount = 0;
	*Reader << Version;
	*Reader << FileCount;
	if (Version != 1 || FileCount < 0)
	{
		return false;
	}

	for (int32 Index = 0; Index < FileCount; ++Index)
	{
		FString StoredPath;
		int64 Size = 0;
		*Reader << StoredPath;
		*Reader << Size;

		if (Size < 0 || Size > 256LL * 1024LL * 1024LL)
		{
			return false;
		}
		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (StoredPath == TEXT("thumbnail.png"))
		{
			const FString CachePath = GetPackThumbnailCachePath(PackId, PackPath);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);
			if (FPaths::FileExists(CachePath))
			{
				Reader->Seek(Reader->Tell() + Size);
				if (IsThumbnailCheckerboard(CachePath) || IsThumbnailMostlyEmpty(CachePath))
				{
					IFileManager::Get().Delete(*CachePath, false, true);
					return false;
				}

				OutThumbnailPath = CachePath;
				return true;
			}

			TArray<uint8> Bytes;
			Bytes.SetNumUninitialized(static_cast<int32>(Size));
			if (Size > 0)
			{
				Reader->Serialize(Bytes.GetData(), Size);
			}
			if (FFileHelper::SaveArrayToFile(Bytes, *CachePath))
			{
				if (IsThumbnailCheckerboard(CachePath) || IsThumbnailMostlyEmpty(CachePath))
				{
					IFileManager::Get().Delete(*CachePath, false, true);
					return false;
				}

				OutThumbnailPath = CachePath;
				return true;
			}
			return false;
		}

		Reader->Seek(Reader->Tell() + Size);
	}

	return false;
}

bool SMaterialVaultWindow::ExtractPackStoredThumbnail(const FString& PackPath, const FString& PackId, const FString& StoredThumbnailPath, FString& OutThumbnailPath)
{
	OutThumbnailPath.Reset();
	if (StoredThumbnailPath.IsEmpty())
	{
		return false;
	}

	FString NormalizedStoredPath = StoredThumbnailPath;
	NormalizedStoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		return false;
	}

	uint8 Magic[8] = {};
	Reader->Serialize(Magic, UE_ARRAY_COUNT(Magic));
	const uint8 ExpectedMagic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	if (FMemory::Memcmp(Magic, ExpectedMagic, UE_ARRAY_COUNT(ExpectedMagic)) != 0)
	{
		return false;
	}

	int32 Version = 0;
	int32 FileCount = 0;
	*Reader << Version;
	*Reader << FileCount;
	if (Version != 1 || FileCount < 0)
	{
		return false;
	}

	FString CacheFileName = PackId + TEXT("_") + NormalizedStoredPath;
	CacheFileName.ReplaceInline(TEXT("/"), TEXT("_"));
	CacheFileName.ReplaceInline(TEXT("\\"), TEXT("_"));
	CacheFileName.ReplaceInline(TEXT(":"), TEXT("_"));
	const FString CachePath = FPaths::Combine(ResolveVaultRoot(), TEXT("Thumbnails"), CacheFileName);

	for (int32 Index = 0; Index < FileCount; ++Index)
	{
		FString StoredPath;
		int64 Size = 0;
		*Reader << StoredPath;
		*Reader << Size;
		if (Size < 0 || Size > 256LL * 1024LL * 1024LL)
		{
			return false;
		}
		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (StoredPath != NormalizedStoredPath)
		{
			Reader->Seek(Reader->Tell() + Size);
			continue;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);
		if (FPaths::FileExists(CachePath))
		{
			Reader->Seek(Reader->Tell() + Size);
			if (IsThumbnailCheckerboard(CachePath) || IsThumbnailMostlyEmpty(CachePath))
			{
				IFileManager::Get().Delete(*CachePath, false, true);
				return false;
			}
			OutThumbnailPath = CachePath;
			return true;
		}

		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(static_cast<int32>(Size));
		if (Size > 0)
		{
			Reader->Serialize(Bytes.GetData(), Size);
		}
		if (!FFileHelper::SaveArrayToFile(Bytes, *CachePath))
		{
			return false;
		}
		if (IsThumbnailCheckerboard(CachePath) || IsThumbnailMostlyEmpty(CachePath))
		{
			IFileManager::Get().Delete(*CachePath, false, true);
			return false;
		}
		OutThumbnailPath = CachePath;
		return true;
	}

	return false;
}
TSharedPtr<FSlateDynamicImageBrush> SMaterialVaultWindow::CreateThumbnailBrush(const FString& ThumbnailPath, const FString& BrushId, const FVector2D& ImageSize, TArray<TWeakObjectPtr<UTexture2D>>* OutOwnedTextures)
{
	if (ThumbnailPath.IsEmpty() || !FPaths::FileExists(ThumbnailPath))
	{
		return nullptr;
	}

	TArray<uint8> CompressedBytes;
	if (!FFileHelper::LoadFileToArray(CompressedBytes, *ThumbnailPath))
	{
		return nullptr;
	}

	UTexture2D* ThumbnailTexture = FImageUtils::ImportBufferAsTexture2D(CompressedBytes);
	if (!ThumbnailTexture)
	{
		return nullptr;
	}
	ThumbnailTexture->MipGenSettings = TMGS_NoMipmaps;
	ThumbnailTexture->CompressionSettings = TC_EditorIcon;
	ThumbnailTexture->LODGroup = TEXTUREGROUP_UI;
	ThumbnailTexture->UpdateResource();
	ThumbnailTexture->AddToRoot();
	if (OutOwnedTextures)
	{
		OutOwnedTextures->Add(ThumbnailTexture);
	}

	return MakeShared<FSlateDynamicImageBrush>(
		ThumbnailTexture,
		ImageSize,
		FName(*FString::Printf(TEXT("MaterialVault_%s_%s"), *BrushId, *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
}

bool SMaterialVaultWindow::IsThumbnailCheckerboard(const FString& ThumbnailPath)
{
	if (ThumbnailPath.IsEmpty() || !FPaths::FileExists(ThumbnailPath))
	{
		return false;
	}

	TArray<uint8> CompressedBytes;
	if (!FFileHelper::LoadFileToArray(CompressedBytes, *ThumbnailPath))
	{
		return false;
	}

	const int64 FileSize = CompressedBytes.Num();
	// Very small files are almost certainly placeholder thumbnails
	if (FileSize < 1024)
	{
		return true;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedBytes.GetData(), CompressedBytes.Num()))
	{
		return false;
	}

	TArray64<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		return false;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();

	// Very small images are placeholder thumbnails
	if (Width < 64 || Height < 64)
	{
		return true;
	}

	// Divide image into blocks and check for two-color checkerboard pattern
	const int32 BlockSize = 8;
	const int32 GridW = Width / BlockSize;
	const int32 GridH = Height / BlockSize;
	if (GridW < 3 || GridH < 3)
	{
		return false;
	}

	// Compute average color for each block (quantized to reduce noise)
	TArray<uint32> BlockColors;
	BlockColors.SetNumZeroed(GridW * GridH);
	for (int32 Gy = 0; Gy < GridH; ++Gy)
	{
		for (int32 Gx = 0; Gx < GridW; ++Gx)
		{
			int64 SumR = 0, SumG = 0, SumB = 0;
			int32 Count = 0;
			for (int32 Dy = 0; Dy < BlockSize; ++Dy)
			{
				for (int32 Dx = 0; Dx < BlockSize; ++Dx)
				{
					const int32 Px = Gx * BlockSize + Dx;
					const int32 Py = Gy * BlockSize + Dy;
					const int64 Index = (static_cast<int64>(Py) * Width + Px) * 4;
					if (Index + 3 >= RawData.Num()) { continue; }
					SumB += RawData[Index];
					SumG += RawData[Index + 1];
					SumR += RawData[Index + 2];
					++Count;
				}
			}
			// Quantize to 16 levels per channel to group similar colors
			const uint8 R = static_cast<uint8>((SumR / Count) / 16 * 16);
			const uint8 G = static_cast<uint8>((SumG / Count) / 16 * 16);
			const uint8 B = static_cast<uint8>((SumB / Count) / 16 * 16);
			BlockColors[Gy * GridW + Gx] = (static_cast<uint32>(R) << 16) | (static_cast<uint32>(G) << 8) | B;
		}
	}

	// Find the two most common block colors
	TMap<uint32, int32> ColorCount;
	for (int32 I = 0; I < BlockColors.Num(); ++I)
	{
		ColorCount.FindOrAdd(BlockColors[I])++;
	}

	// Need at least 2 colors for a checkerboard; allow up to 16 for edge artifacts
	if (ColorCount.Num() < 2 || ColorCount.Num() > 16)
	{
		return false;
	}

	TArray<TPair<uint32, int32>> Sorted;
	for (const auto& Pair : ColorCount)
	{
		Sorted.Add(Pair);
	}
	Sorted.Sort([](const TPair<uint32, int32>& A, const TPair<uint32, int32>& B) { return A.Value > B.Value; });

	const uint32 ColorA = Sorted[0].Key;
	const uint32 ColorB = Sorted[1].Key;
	const float TopTwoRatio = static_cast<float>(Sorted[0].Value + Sorted[1].Value) / (GridW * GridH);
	if (TopTwoRatio < 0.55f)
	{
		return false;
	}

	// The two colors must be significantly different (high contrast checkerboard)
	const int32 Rdiff = static_cast<int32>((ColorA >> 16) & 0xFF) - static_cast<int32>((ColorB >> 16) & 0xFF);
	const int32 Gdiff = static_cast<int32>((ColorA >> 8) & 0xFF) - static_cast<int32>((ColorB >> 8) & 0xFF);
	const int32 Bdiff = static_cast<int32>(ColorA & 0xFF) - static_cast<int32>(ColorB & 0xFF);
	const int32 ColorDist = Rdiff * Rdiff + Gdiff * Gdiff + Bdiff * Bdiff;
	if (ColorDist < 2000)
	{
		return false;
	}

	// Check that horizontally and vertically adjacent blocks alternate between A and B
	int32 AlternatingCount = 0;
	int32 NeighborPairs = 0;

	for (int32 Gy = 0; Gy < GridH; ++Gy)
	{
		for (int32 Gx = 0; Gx < GridW - 1; ++Gx)
		{
			const uint32 Left = BlockColors[Gy * GridW + Gx];
			const uint32 Right = BlockColors[Gy * GridW + Gx + 1];
			if ((Left == ColorA && Right == ColorB) || (Left == ColorB && Right == ColorA))
			{
				++AlternatingCount;
			}
			++NeighborPairs;
		}
	}
	for (int32 Gy = 0; Gy < GridH - 1; ++Gy)
	{
		for (int32 Gx = 0; Gx < GridW; ++Gx)
		{
			const uint32 Top = BlockColors[Gy * GridW + Gx];
			const uint32 Bottom = BlockColors[(Gy + 1) * GridW + Gx];
			if ((Top == ColorA && Bottom == ColorB) || (Top == ColorB && Bottom == ColorA))
			{
				++AlternatingCount;
			}
			++NeighborPairs;
		}
	}

	if (NeighborPairs < 12)
	{
		return false;
	}
	return static_cast<float>(AlternatingCount) / NeighborPairs >= 0.55f;
}

bool SMaterialVaultWindow::IsThumbnailMostlyEmpty(const FString& ThumbnailPath)
{
	if (ThumbnailPath.IsEmpty() || !FPaths::FileExists(ThumbnailPath))
	{
		return false;
	}

	TArray<uint8> CompressedBytes;
	if (!FFileHelper::LoadFileToArray(CompressedBytes, *ThumbnailPath))
	{
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedBytes.GetData(), CompressedBytes.Num()))
	{
		return false;
	}

	TArray64<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		return false;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();
	const int64 PixelCount = static_cast<int64>(Width) * Height;
	if (PixelCount <= 0 || RawData.Num() < PixelCount * 4)
	{
		return false;
	}

	int64 TransparentPixels = 0;
	int64 NearBlackPixels = 0;
	int64 BrightnessSum = 0;
	int64 CenterPixels = 0;
	int64 CenterBrightnessSum = 0;
	int64 CenterSaturationSum = 0;
	int64 CenterGrayPixels = 0;
	const float CenterX = (static_cast<float>(Width) - 1.0f) * 0.5f;
	const float CenterY = (static_cast<float>(Height) - 1.0f) * 0.5f;
	const float SphereRadius = static_cast<float>(FMath::Min(Width, Height)) * 0.38f;
	const float SphereRadiusSq = SphereRadius * SphereRadius;
	for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
	{
		const int64 ByteIndex = PixelIndex * 4;
		const uint8 B = RawData[ByteIndex];
		const uint8 G = RawData[ByteIndex + 1];
		const uint8 R = RawData[ByteIndex + 2];
		const uint8 A = RawData[ByteIndex + 3];
		const int32 Brightness = (static_cast<int32>(R) + static_cast<int32>(G) + static_cast<int32>(B)) / 3;
		const int32 MaxChannel = FMath::Max3(static_cast<int32>(R), static_cast<int32>(G), static_cast<int32>(B));
		const int32 MinChannel = FMath::Min3(static_cast<int32>(R), static_cast<int32>(G), static_cast<int32>(B));
		const int32 Saturation = MaxChannel - MinChannel;
		BrightnessSum += Brightness;
		if (A < 8)
		{
			++TransparentPixels;
		}
		if (Brightness < 8)
		{
			++NearBlackPixels;
		}

		const int32 X = static_cast<int32>(PixelIndex % Width);
		const int32 Y = static_cast<int32>(PixelIndex / Width);
		const float Dx = static_cast<float>(X) - CenterX;
		const float Dy = static_cast<float>(Y) - CenterY;
		if (Dx * Dx + Dy * Dy <= SphereRadiusSq && Brightness > 24)
		{
			++CenterPixels;
			CenterBrightnessSum += Brightness;
			CenterSaturationSum += Saturation;
			if (Saturation <= 4)
			{
				++CenterGrayPixels;
			}
		}
	}

	const float TransparentRatio = static_cast<float>(TransparentPixels) / static_cast<float>(PixelCount);
	const float NearBlackRatio = static_cast<float>(NearBlackPixels) / static_cast<float>(PixelCount);
	const float AverageBrightness = static_cast<float>(BrightnessSum) / static_cast<float>(PixelCount);
	const bool bLooksLikeDefaultGrayMaterial =
		CenterPixels > 512 &&
		static_cast<float>(CenterGrayPixels) / static_cast<float>(CenterPixels) > 0.92f &&
		static_cast<float>(CenterSaturationSum) / static_cast<float>(CenterPixels) < 2.5f &&
		static_cast<float>(CenterBrightnessSum) / static_cast<float>(CenterPixels) > 45.0f &&
		static_cast<float>(CenterBrightnessSum) / static_cast<float>(CenterPixels) < 115.0f;
	return TransparentRatio > 0.95f || (NearBlackRatio > 0.98f && AverageBrightness < 4.0f) || bLooksLikeDefaultGrayMaterial;
}

bool SMaterialVaultWindow::IsPackThumbnailBroken(const TSharedPtr<FMaterialVaultPackItem>& PackItem)
{
	if (!PackItem.IsValid())
	{
		return true;
	}
	if (PackItem->ThumbnailPath.IsEmpty() || !FPaths::FileExists(PackItem->ThumbnailPath))
	{
		return true;
	}
	return IsThumbnailCheckerboard(PackItem->ThumbnailPath) || IsThumbnailMostlyEmpty(PackItem->ThumbnailPath);
}

bool SMaterialVaultWindow::ExtractPackMeta(const FString& PackPath, const FString& PackId, const FString& VaultRoot, FPackedMeta& OutMeta)
{
	OutMeta = FPackedMeta();
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		return false;
	}

	uint8 Magic[8] = {};
	Reader->Serialize(Magic, UE_ARRAY_COUNT(Magic));
	const uint8 ExpectedMagic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	if (FMemory::Memcmp(Magic, ExpectedMagic, UE_ARRAY_COUNT(ExpectedMagic)) != 0)
	{
		return false;
	}

	int32 Version = 0;
	int32 FileCount = 0;
	*Reader << Version;
	*Reader << FileCount;
	if (Version != 1 || FileCount < 0)
	{
		return false;
	}

	bool bFoundManifest = false;
	for (int32 Index = 0; Index < FileCount; ++Index)
	{
		FString StoredPath;
		int64 Size = 0;
		*Reader << StoredPath;
		*Reader << Size;

		if (Size < 0 || Size > 256LL * 1024LL * 1024LL)
		{
			return false;
		}

		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (StoredPath == TEXT("manifest.json"))
		{
			TArray<uint8> Bytes;
			Bytes.SetNumUninitialized(static_cast<int32>(Size));
			if (Size > 0)
			{
				Reader->Serialize(Bytes.GetData(), Size);
			}
			FString ManifestJson;
			FFileHelper::BufferToString(ManifestJson, Bytes.GetData(), Bytes.Num());
			TSharedPtr<FJsonObject> ManifestObject;
			const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ManifestJson);
			if (FJsonSerializer::Deserialize(JsonReader, ManifestObject) && ManifestObject.IsValid())
			{
				const TSharedPtr<FJsonObject>* RootAsset;
				if (ManifestObject->TryGetObjectField(TEXT("rootAsset"), RootAsset) && (*RootAsset).IsValid())
				{
					(*RootAsset)->TryGetStringField(TEXT("stagingObjectPath"), OutMeta.RootObjectPath);
					(*RootAsset)->TryGetStringField(TEXT("assetClass"), OutMeta.RootAssetClass);
				}
				ManifestObject->TryGetStringField(TEXT("category"), OutMeta.Category);
			}
			bFoundManifest = true;
		}
		else if (StoredPath == TEXT("thumbnail.png"))
		{
			const FString CachePath = GetPackThumbnailCachePath(PackId, PackPath);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);
			if (FPaths::FileExists(CachePath))
			{
				Reader->Seek(Reader->Tell() + Size);
				if (IsThumbnailCheckerboard(CachePath) || IsThumbnailMostlyEmpty(CachePath))
				{
					IFileManager::Get().Delete(*CachePath, false, true);
				}
				else
				{
					OutMeta.ThumbnailPath = CachePath;
				}
				continue;
			}

			TArray<uint8> Bytes;
			Bytes.SetNumUninitialized(static_cast<int32>(Size));
			if (Size > 0)
			{
				Reader->Serialize(Bytes.GetData(), Size);
			}
			if (FFileHelper::SaveArrayToFile(Bytes, *CachePath))
			{
				if (IsThumbnailCheckerboard(CachePath) || IsThumbnailMostlyEmpty(CachePath))
				{
					IFileManager::Get().Delete(*CachePath, false, true);
				}
				else
				{
					OutMeta.ThumbnailPath = CachePath;
				}
			}
		}
		else
		{
			Reader->Seek(Reader->Tell() + Size);
		}
	}

	return bFoundManifest;
}

bool SMaterialVaultWindow::ExtractPackCategoryFromManifest(const FString& PackPath, FString& OutCategory)
{
	OutCategory.Reset();
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		return false;
	}

	uint8 Magic[8] = {};
	Reader->Serialize(Magic, UE_ARRAY_COUNT(Magic));
	const uint8 ExpectedMagic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	if (FMemory::Memcmp(Magic, ExpectedMagic, UE_ARRAY_COUNT(ExpectedMagic)) != 0)
	{
		return false;
	}

	int32 Version = 0;
	int32 FileCount = 0;
	*Reader << Version;
	*Reader << FileCount;
	if (Version != 1 || FileCount < 0)
	{
		return false;
	}

	for (int32 Index = 0; Index < FileCount; ++Index)
	{
		FString StoredPath;
		int64 Size = 0;
		*Reader << StoredPath;
		*Reader << Size;

		if (Size < 0 || Size > 256LL * 1024LL * 1024LL)
		{
			return false;
		}
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(static_cast<int32>(Size));
		if (Size > 0)
		{
			Reader->Serialize(Bytes.GetData(), Size);
		}

		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (StoredPath != TEXT("manifest.json"))
		{
			continue;
		}

		FString ManifestJson;
		FFileHelper::BufferToString(ManifestJson, Bytes.GetData(), Bytes.Num());
		TSharedPtr<FJsonObject> ManifestObject;
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ManifestJson);
		if (FJsonSerializer::Deserialize(JsonReader, ManifestObject) && ManifestObject.IsValid())
		{
			return ManifestObject->TryGetStringField(TEXT("category"), OutCategory);
		}
		return false;
	}

	return false;
}

bool SMaterialVaultWindow::ExtractPackRootInfoFromManifest(const FString& PackPath, FString& OutRootObjectPath, FString& OutAssetClass)
{
	OutRootObjectPath.Reset();
	OutAssetClass.Reset();
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		return false;
	}

	uint8 Magic[8] = {};
	Reader->Serialize(Magic, UE_ARRAY_COUNT(Magic));
	const uint8 ExpectedMagic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	if (FMemory::Memcmp(Magic, ExpectedMagic, UE_ARRAY_COUNT(ExpectedMagic)) != 0)
	{
		return false;
	}

	int32 Version = 0;
	int32 FileCount = 0;
	*Reader << Version;
	*Reader << FileCount;
	if (Version != 1 || FileCount < 0)
	{
		return false;
	}

	for (int32 Index = 0; Index < FileCount; ++Index)
	{
		FString StoredPath;
		int64 Size = 0;
		*Reader << StoredPath;
		*Reader << Size;

		if (Size < 0 || Size > 256LL * 1024LL * 1024LL)
		{
			return false;
		}
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(static_cast<int32>(Size));
		if (Size > 0)
		{
			Reader->Serialize(Bytes.GetData(), Size);
		}

		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (StoredPath != TEXT("manifest.json"))
		{
			continue;
		}

		FString JsonText;
		FFileHelper::BufferToString(JsonText, Bytes.GetData(), Bytes.Num());

		TSharedPtr<FJsonObject> ManifestObject;
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(JsonReader, ManifestObject) || !ManifestObject.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* RootAssetObject = nullptr;
		if (!ManifestObject->TryGetObjectField(TEXT("rootAsset"), RootAssetObject) || !RootAssetObject || !RootAssetObject->IsValid())
		{
			return false;
		}

		(*RootAssetObject)->TryGetStringField(TEXT("assetClass"), OutAssetClass);
		return (*RootAssetObject)->TryGetStringField(TEXT("stagingObjectPath"), OutRootObjectPath) ||
			(*RootAssetObject)->TryGetStringField(TEXT("plannedObjectPath"), OutRootObjectPath);
	}

	return false;
}


bool SMaterialVaultWindow::ExtractPackItemFromManifest(const FString& PackPath, FMaterialVaultItem& OutItem)
{
	OutItem = FMaterialVaultItem();
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		return false;
	}

	uint8 Magic[8] = {};
	Reader->Serialize(Magic, UE_ARRAY_COUNT(Magic));
	const uint8 ExpectedMagic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	if (FMemory::Memcmp(Magic, ExpectedMagic, UE_ARRAY_COUNT(ExpectedMagic)) != 0)
	{
		return false;
	}

	int32 Version = 0;
	int32 FileCount = 0;
	*Reader << Version;
	*Reader << FileCount;
	if (Version != 1 || FileCount < 0)
	{
		return false;
	}

	auto ParseRole = [](const FString& RoleText)
	{
		if (RoleText == TEXT("RootMaterial")) return EMaterialVaultAssetRole::RootMaterial;
		if (RoleText == TEXT("Material")) return EMaterialVaultAssetRole::Material;
		if (RoleText == TEXT("MaterialInstance")) return EMaterialVaultAssetRole::MaterialInstance;
		if (RoleText == TEXT("Texture")) return EMaterialVaultAssetRole::Texture;
		if (RoleText == TEXT("MaterialFunction")) return EMaterialVaultAssetRole::MaterialFunction;
		return EMaterialVaultAssetRole::Other;
	};

	auto ParseEntry = [&ParseRole](const TSharedPtr<FJsonObject>& Object, FMaterialVaultAssetEntry& OutEntry)
	{
		if (!Object.IsValid())
		{
			return false;
		}
		FString RoleText;
		FString OriginalObjectPath;
		Object->TryGetStringField(TEXT("role"), RoleText);
		Object->TryGetStringField(TEXT("assetClass"), OutEntry.AssetClass);
		Object->TryGetStringField(TEXT("originalObjectPath"), OriginalObjectPath);
		Object->TryGetStringField(TEXT("originalPackageName"), OutEntry.OriginalPackageName);
		Object->TryGetStringField(TEXT("plannedPackageName"), OutEntry.PlannedPackageName);
		Object->TryGetStringField(TEXT("plannedObjectPath"), OutEntry.PlannedObjectPath);
		Object->TryGetStringField(TEXT("thumbnailPath"), OutEntry.ThumbnailPath);
		OutEntry.OriginalObjectPath = FSoftObjectPath(OriginalObjectPath);
		OutEntry.Role = ParseRole(RoleText);
		return !OriginalObjectPath.IsEmpty();
	};

	for (int32 Index = 0; Index < FileCount; ++Index)
	{
		FString StoredPath;
		int64 Size = 0;
		*Reader << StoredPath;
		*Reader << Size;
		if (Size < 0 || Size > 256LL * 1024LL * 1024LL)
		{
			return false;
		}
		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (StoredPath != TEXT("manifest.json"))
		{
			Reader->Seek(Reader->Tell() + Size);
			continue;
		}

		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(static_cast<int32>(Size));
		if (Size > 0)
		{
			Reader->Serialize(Bytes.GetData(), Size);
		}
		FString ManifestJson;
		FFileHelper::BufferToString(ManifestJson, Bytes.GetData(), Bytes.Num());
		TSharedPtr<FJsonObject> ManifestObject;
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ManifestJson);
		if (!FJsonSerializer::Deserialize(JsonReader, ManifestObject) || !ManifestObject.IsValid())
		{
			return false;
		}

		ManifestObject->TryGetStringField(TEXT("id"), OutItem.Id);
		ManifestObject->TryGetStringField(TEXT("displayName"), OutItem.DisplayName);
		ManifestObject->TryGetStringField(TEXT("category"), OutItem.Category);
		ManifestObject->TryGetStringField(TEXT("sourceEngineVersion"), OutItem.SourceEngineVersion);

		const TSharedPtr<FJsonObject>* RootObject = nullptr;
		if (!ManifestObject->TryGetObjectField(TEXT("rootAsset"), RootObject) || !RootObject || !RootObject->IsValid() || !ParseEntry(*RootObject, OutItem.RootAsset))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Dependencies = nullptr;
		if (ManifestObject->TryGetArrayField(TEXT("dependencies"), Dependencies) && Dependencies)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Dependencies)
			{
				FMaterialVaultAssetEntry Dependency;
				if (ParseEntry(Value->AsObject(), Dependency))
				{
					OutItem.Dependencies.Add(Dependency);
				}
			}
		}
		return true;
	}

	return false;
}
bool SMaterialVaultWindow::RewritePackThumbnail(const FString& PackPath, const FString& ThumbnailPath, FText& OutError)
{
	TArray<TPair<FString, FString>> Replacements;
	Replacements.Add(TPair<FString, FString>(TEXT("thumbnail.png"), ThumbnailPath));
	return RewritePackThumbnails(PackPath, Replacements, OutError);
}

bool SMaterialVaultWindow::RewritePackThumbnails(const FString& PackPath, const TArray<TPair<FString, FString>>& ReplacementFiles, FText& OutError)
{
	if (!FPaths::FileExists(PackPath) || ReplacementFiles.IsEmpty())
	{
		OutError = FText::FromString(TEXT("材质包或缩略图文件不存在。"));
		return false;
	}
	TMap<FString, FString> ReplacementByStoredPath;
	for (const TPair<FString, FString>& Replacement : ReplacementFiles)
	{
		FString StoredPath = Replacement.Key;
		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (StoredPath.IsEmpty() || !FPaths::FileExists(Replacement.Value))
		{
			OutError = FText::Format(FText::FromString(TEXT("缩略图替换文件不存在：\n{0}")), FText::FromString(Replacement.Value));
			return false;
		}
		ReplacementByStoredPath.Add(StoredPath, Replacement.Value);
	}

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		OutError = FText::Format(FText::FromString(TEXT("无法读取材质包：\n{0}")), FText::FromString(PackPath));
		return false;
	}

	uint8 Magic[8] = {};
	Reader->Serialize(Magic, UE_ARRAY_COUNT(Magic));
	const uint8 ExpectedMagic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	if (FMemory::Memcmp(Magic, ExpectedMagic, UE_ARRAY_COUNT(ExpectedMagic)) != 0)
	{
		OutError = FText::FromString(TEXT("材质包格式不正确。"));
		return false;
	}

	int32 Version = 0;
	int32 FileCount = 0;
	*Reader << Version;
	*Reader << FileCount;
	if (Version != 1 || FileCount < 0)
	{
		OutError = FText::FromString(TEXT("材质包版本不支持。"));
		return false;
	}

	const FString TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("MaterialVault"), TEXT("Repair"), FPaths::GetBaseFilename(PackPath));
	IFileManager::Get().DeleteDirectory(*TempDir, false, true);
	IFileManager::Get().MakeDirectory(*TempDir, true);

	TArray<TPair<FString, FString>> Files;
	TSet<FString> WrittenStoredPaths;
	for (int32 Index = 0; Index < FileCount; ++Index)
	{
		FString StoredPath;
		int64 Size = 0;
		*Reader << StoredPath;
		*Reader << Size;
		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (Size < 0 || Size > 256LL * 1024LL * 1024LL)
		{
			OutError = FText::FromString(TEXT("材质包内文件尺寸异常，包可能已损坏。"));
			return false;
		}
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(static_cast<int32>(Size));
		if (Size > 0)
		{
			Reader->Serialize(Bytes.GetData(), Size);
		}

		if (const FString* ReplacementPath = ReplacementByStoredPath.Find(StoredPath))
		{
			Files.Add(TPair<FString, FString>(StoredPath, *ReplacementPath));
			WrittenStoredPaths.Add(StoredPath);
			continue;
		}

		const FString TempFilePath = FPaths::Combine(TempDir, StoredPath);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(TempFilePath), true);
		if (!FFileHelper::SaveArrayToFile(Bytes, *TempFilePath))
		{
			OutError = FText::Format(FText::FromString(TEXT("无法写入临时文件：\n{0}")), FText::FromString(TempFilePath));
			return false;
		}

		Files.Add(TPair<FString, FString>(StoredPath, TempFilePath));
	}
	Reader->Close();

	for (const TPair<FString, FString>& Replacement : ReplacementByStoredPath)
	{
		if (!WrittenStoredPaths.Contains(Replacement.Key))
		{
			Files.Add(TPair<FString, FString>(Replacement.Key, Replacement.Value));
		}
	}

	const FString TempPackPath = PackPath + TEXT(".repairing");
	if (!WriteMvpackFile(TempPackPath, Files, OutError))
	{
		return false;
	}

	if (!IFileManager::Get().Move(*PackPath, *TempPackPath, true, true))
	{
		OutError = FText::Format(FText::FromString(TEXT("无法替换材质包：\n{0}")), FText::FromString(PackPath));
		return false;
	}

	return true;
}

FString SMaterialVaultWindow::GetPackThumbnailCachePath(const FString& PackId, const FString& PackPath)
{
	const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*PackPath);
	const FString Stamp = Timestamp.ToString(TEXT("%Y%m%d%H%M%S"));
	FString NormalizedPackPath = FPaths::ConvertRelativePathToFull(PackPath).ToLower();
	NormalizedPackPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	const FString PathHash = FString::Printf(TEXT("%08x"), GetTypeHash(NormalizedPackPath));
	return FPaths::Combine(ResolveVaultRoot(), TEXT("Thumbnails"), PackId + TEXT("_") + PathHash + TEXT("_") + Stamp + TEXT(".png"));
}

bool SMaterialVaultWindow::InstallMvpack(const FString& PackPath, FString& OutInstallRoot, FText& OutError, EMaterialVaultInstallMode Mode, FScopedSlowTask* ParentTask, float ParentBudget)
{
	OutInstallRoot.Reset();
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		OutError = FText::Format(FText::FromString(TEXT("无法读取材质包：\n{0}")), FText::FromString(PackPath));
		return false;
	}

	uint8 Magic[8] = {};
	Reader->Serialize(Magic, UE_ARRAY_COUNT(Magic));
	const uint8 ExpectedMagic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	if (FMemory::Memcmp(Magic, ExpectedMagic, UE_ARRAY_COUNT(ExpectedMagic)) != 0)
	{
		OutError = FText::FromString(TEXT("材质包格式不正确。"));
		return false;
	}

	int32 Version = 0;
	int32 FileCount = 0;
	*Reader << Version;
	*Reader << FileCount;
	if (Version != 1 || FileCount < 0)
	{
		OutError = FText::FromString(TEXT("材质包版本不支持。"));
		return false;
	}

	const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString ContentRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	bool bInstalledAnyContent = false;
	TArray<FString> InstalledAssetFiles;
	TArray<FString> InstalledPackageNames;
	TArray<uint8> CopyBuffer;
	CopyBuffer.SetNumUninitialized(4 * 1024 * 1024);
	const float PerFileBudget = ParentTask && FileCount > 0 && ParentBudget > 0.0f
		? ParentBudget / static_cast<float>(FileCount)
		: 0.0f;
	auto AdvanceInstallProgress = [ParentTask](float Amount, const FString& Message)
	{
		if (ParentTask && Amount > 0.0f)
		{
			ParentTask->EnterProgressFrame(Amount, FText::FromString(Message));
		}
	};
	auto SkipPayload = [&Reader, &CopyBuffer](int64 SizeToSkip)
	{
		if (SizeToSkip <= 0)
		{
			return;
		}
		Reader->Seek(Reader->Tell() + SizeToSkip);
	};

	for (int32 Index = 0; Index < FileCount; ++Index)
	{
		FString StoredPath;
		int64 Size = 0;
		*Reader << StoredPath;
		*Reader << Size;

		StoredPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (StoredPath.Contains(TEXT("..")) || FPaths::IsDrive(StoredPath) || StoredPath.StartsWith(TEXT("/")))
		{
			OutError = FText::Format(FText::FromString(TEXT("材质包中包含不安全路径：\n{0}")), FText::FromString(StoredPath));
			return false;
		}

		if (Size < 0 || Size > 256LL * 1024LL * 1024LL)
		{
			OutError = FText::FromString(TEXT("材质包内文件尺寸异常，包可能已损坏。"));
			return false;
		}

		if (StoredPath == TEXT("manifest.json"))
		{
			SkipPayload(Size);
			AdvanceInstallProgress(PerFileBudget, FString::Printf(TEXT("跳过清单 %d/%d"), Index + 1, FileCount));
			continue;
		}

		if (!StoredPath.StartsWith(TEXT("Content/")))
		{
			SkipPayload(Size);
			AdvanceInstallProgress(PerFileBudget, FString::Printf(TEXT("跳过非 Content 文件 %d/%d"), Index + 1, FileCount));
			continue;
		}

		FString NormalizedStoredPath = StoredPath;
		NormalizedStoredPath.ReplaceInline(TEXT("Content/_MaterialVaultStaging/"), TEXT("Content/__MaterialVaultStaging/"));
		if (Mode == EMaterialVaultInstallMode::Library)
		{
			const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
			FString LibraryRelativeRoot = Settings->LibraryMountRoot;
			LibraryRelativeRoot.RemoveFromStart(TEXT("/Game/"));
			if (LibraryRelativeRoot.IsEmpty())
			{
				LibraryRelativeRoot = TEXT("MaterialVault/Library");
			}

			FString StagingRelativeRoot = Settings->StagingMountRoot;
			StagingRelativeRoot.RemoveFromStart(TEXT("/Game/"));
			if (StagingRelativeRoot.IsEmpty())
			{
				StagingRelativeRoot = TEXT("__MaterialVaultStaging");
			}

			NormalizedStoredPath.ReplaceInline(
				*(TEXT("Content/") + StagingRelativeRoot + TEXT("/")),
				*(TEXT("Content/") + LibraryRelativeRoot + TEXT("/")));
		}

		const FString DestinationPath = FPaths::Combine(ProjectRoot, NormalizedStoredPath);
		const FString DestinationDir = FPaths::GetPath(DestinationPath);
		const bool bIsStagingFile = NormalizedStoredPath.StartsWith(TEXT("Content/__MaterialVaultStaging/"));
		if (!IFileManager::Get().MakeDirectory(*DestinationDir, true))
		{
			OutError = FText::Format(FText::FromString(TEXT("无法创建安装目录：\n{0}")), FText::FromString(DestinationDir));
			return false;
		}
		if (FPaths::FileExists(DestinationPath))
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*DestinationPath, false);
			if (!IFileManager::Get().Delete(*DestinationPath, false, true, true))
			{
				if (bIsStagingFile)
				{
					bInstalledAnyContent = true;
					OutInstallRoot = FPaths::GetPath(DestinationPath);
					SkipPayload(Size);
					AdvanceInstallProgress(PerFileBudget, FString::Printf(TEXT("已存在暂存文件 %d/%d：%s"), Index + 1, FileCount, *FPaths::GetCleanFilename(DestinationPath)));
					continue;
				}

				OutError = FText::Format(FText::FromString(TEXT("无法替换已有安装文件，请确认文件没有被只读或外部程序占用：\n{0}")), FText::FromString(DestinationPath));
				return false;
			}
		}

		TUniquePtr<FArchive> DestinationWriter(IFileManager::Get().CreateFileWriter(*DestinationPath));
		if (!DestinationWriter)
		{
			if (bIsStagingFile && FPaths::FileExists(DestinationPath))
			{
				bInstalledAnyContent = true;
				OutInstallRoot = FPaths::GetPath(DestinationPath);
				SkipPayload(Size);
				AdvanceInstallProgress(PerFileBudget, FString::Printf(TEXT("沿用暂存文件 %d/%d：%s"), Index + 1, FileCount, *FPaths::GetCleanFilename(DestinationPath)));
				continue;
			}

			OutError = FText::Format(FText::FromString(TEXT("无法写入安装文件：\n{0}")), FText::FromString(DestinationPath));
			return false;
		}

		int64 RemainingBytes = Size;
		if (RemainingBytes <= 0)
		{
			AdvanceInstallProgress(PerFileBudget, FString::Printf(TEXT("写入暂存区 %d/%d：%s"), Index + 1, FileCount, *FPaths::GetCleanFilename(DestinationPath)));
		}
		while (RemainingBytes > 0)
		{
			const int64 ChunkSize64 = FMath::Min<int64>(RemainingBytes, CopyBuffer.Num());
			const int32 ChunkSize = static_cast<int32>(ChunkSize64);
			Reader->Serialize(CopyBuffer.GetData(), ChunkSize);
			if (Reader->IsError())
			{
				OutError = FText::Format(FText::FromString(TEXT("读取材质包内容失败：\n{0}")), FText::FromString(StoredPath));
				return false;
			}
			DestinationWriter->Serialize(CopyBuffer.GetData(), ChunkSize);
			if (DestinationWriter->IsError())
			{
				OutError = FText::Format(FText::FromString(TEXT("写入安装文件失败：\n{0}")), FText::FromString(DestinationPath));
				return false;
			}
			RemainingBytes -= ChunkSize64;
			const float ChunkBudget = Size > 0
				? PerFileBudget * static_cast<float>(ChunkSize64) / static_cast<float>(Size)
				: 0.0f;
			AdvanceInstallProgress(ChunkBudget, FString::Printf(TEXT("写入暂存区 %d/%d：%s"), Index + 1, FileCount, *FPaths::GetCleanFilename(DestinationPath)));
		}
		DestinationWriter->Close();

		bInstalledAnyContent = true;
		OutInstallRoot = FPaths::GetPath(DestinationPath);

		const FString Extension = FPaths::GetExtension(DestinationPath, true);
		if (Extension.Equals(TEXT(".uasset"), ESearchCase::IgnoreCase) || Extension.Equals(TEXT(".umap"), ESearchCase::IgnoreCase))
		{
			FString PackagePath = NormalizedStoredPath;
			PackagePath.RemoveFromStart(TEXT("Content/"));
			PackagePath = FPaths::ChangeExtension(PackagePath, TEXT(""));
			PackagePath.ReplaceInline(TEXT("\\"), TEXT("/"));
			const FString PackageName = TEXT("/Game/") + PackagePath;
			InstalledAssetFiles.Add(DestinationPath);
			InstalledPackageNames.Add(PackageName);
		}
	}

	Reader->Close();

	if (!bInstalledAnyContent)
	{
		OutError = FText::FromString(TEXT("材质包里没有可安装的 Content 文件。"));
		return false;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FString> PathsToScan;
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	PathsToScan.Add(Settings->LibraryMountRoot);
	PathsToScan.Add(Settings->StagingMountRoot);
	if (InstalledAssetFiles.Num() > 0)
	{
		AssetRegistryModule.Get().ScanFilesSynchronous(InstalledAssetFiles, true);
	}
	AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, true);

	auto GetInstallLoadPriority = [](const FString& PackageName)
	{
		if (PackageName.Contains(TEXT("/Functions/")) || PackageName.Contains(TEXT("/Textures/")))
		{
			return 0;
		}
		if (PackageName.Contains(TEXT("/Materials/")))
		{
			return 1;
		}
		if (PackageName.Contains(TEXT("/Instances/")))
		{
			return 2;
		}
		return 1;
	};

	InstalledPackageNames.Sort([&GetInstallLoadPriority](const FString& Left, const FString& Right)
	{
		const int32 LeftPriority = GetInstallLoadPriority(Left);
		const int32 RightPriority = GetInstallLoadPriority(Right);
		if (LeftPriority != RightPriority)
		{
			return LeftPriority < RightPriority;
		}
		return Left < Right;
	});

	if (GEditor)
	{
		for (const FString& PackageName : InstalledPackageNames)
		{
			LoadPackage(nullptr, *PackageName, LOAD_None);
		}
	}

	return true;
}

FString SMaterialVaultWindow::GetPackPathForItem(const FMaterialVaultItem& Item)
{
	const FString VaultRoot = ResolveVaultRoot();
	const FString SafeCategory = NormalizeCategoryPath(Item.Category);
	return FPaths::Combine(VaultRoot, TEXT("Packs"), SafeCategory, Item.Id + TEXT(".mvpack"));
}

FString SMaterialVaultWindow::GetStagingRootDirectory()
{
	FString RelativePath = GetDefault<UMaterialVaultSettings>()->StagingMountRoot;
	RelativePath.RemoveFromStart(TEXT("/Game/"));
	return FPaths::Combine(FPaths::ProjectContentDir(), RelativePath);
}

FString SMaterialVaultWindow::GetStagingItemDirectory(const FMaterialVaultItem& Item)
{
	return FPaths::Combine(GetStagingRootDirectory(), NormalizeCategoryPath(Item.Category), Item.Id);
}

int64 SMaterialVaultWindow::GetDirectorySizeBytes(const FString& Directory)
{
	int64 TotalBytes = 0;
	IFileManager::Get().IterateDirectoryRecursively(*Directory, [&TotalBytes](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (!bIsDirectory)
		{
			TotalBytes += IFileManager::Get().FileSize(FilenameOrDirectory);
		}
		return true;
	});
	return TotalBytes;
}

FString SMaterialVaultWindow::FormatBytes(int64 Bytes)
{
	return FString::Printf(TEXT("%.2f GB"), static_cast<double>(Bytes) / 1024.0 / 1024.0 / 1024.0);
}

bool SMaterialVaultWindow::ShouldRunPeriodicGC(int32 ItemsSinceLastGC)
{
	if (ItemsSinceLastGC >= 2)
	{
		return true;
	}
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	const int64 RssBytes = static_cast<int64>(Stats.UsedPhysical);
	const int64 ThresholdBytes = 4LL * 1024LL * 1024LL * 1024LL;
	return RssBytes > ThresholdBytes;
}

bool SMaterialVaultWindow::CleanupStagingForItem(const FMaterialVaultItem& Item, FText& OutError, bool bForce)
{
	if (!bForce && !GetDefault<UMaterialVaultSettings>()->bCleanStagingAfterPack)
	{
		return true;
	}

	const FString ItemDirectory = GetStagingItemDirectory(Item);
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	FString StagingAssetDirectory = FPaths::Combine(Settings->StagingMountRoot, NormalizeCategoryPath(Item.Category), Item.Id);
	StagingAssetDirectory.ReplaceInline(TEXT("\\"), TEXT("/"));
	StagingAssetDirectory.RemoveFromEnd(TEXT("/"));

	TSet<FString> StagingAssetDirectories;
	TSet<FString> StagingAssetRootDirectories;
	auto AddStagingDirectory = [&StagingAssetDirectories, &StagingAssetRootDirectories, Settings](FString Directory)
	{
		Directory.ReplaceInline(TEXT("\\"), TEXT("/"));
		Directory.RemoveFromEnd(TEXT("/"));
		if (Directory.IsEmpty())
		{
			return;
		}

		StagingAssetDirectories.Add(Directory);

		FString MountRoot = Settings->StagingMountRoot;
		MountRoot.ReplaceInline(TEXT("\\"), TEXT("/"));
		MountRoot.RemoveFromEnd(TEXT("/"));
		FString RelativeDirectory = Directory;
		if (RelativeDirectory.RemoveFromStart(MountRoot + TEXT("/")))
		{
			TArray<FString> Segments;
			RelativeDirectory.ParseIntoArray(Segments, TEXT("/"), true);
			if (Segments.Num() >= 2)
			{
				StagingAssetRootDirectories.Add(MountRoot / Segments[0] / Segments[1]);
			}
		}
	};

	StagingAssetDirectories.Add(StagingAssetDirectory);
	AddStagingDirectory(StagingAssetDirectory);
	for (const FMaterialVaultAssetEntry& Entry : GetAllEntries(Item))
	{
		const FString EntryPackageName = GetStagingPackageName(Entry);
		if (!EntryPackageName.IsEmpty())
		{
			FString EntryDirectory = FPackageName::GetLongPackagePath(EntryPackageName);
			AddStagingDirectory(EntryDirectory);
		}
	}
	StagingAssetDirectories.Append(StagingAssetRootDirectories);

	if (GEditor && !IsRunningCommandlet())
	{
		if (UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
			TArray<FString> PathsToScan = StagingAssetDirectories.Array();
			AssetRegistry.ScanPathsSynchronous(PathsToScan, true);

			TArray<FAssetData> AssetsToDelete;
			for (const FString& Directory : StagingAssetDirectories)
			{
				TArray<FAssetData> DirectoryAssets;
				AssetRegistry.GetAssetsByPath(*Directory, DirectoryAssets, true, false);
				AssetsToDelete.Append(DirectoryAssets);
			}

			if (!AssetsToDelete.IsEmpty())
			{
				ObjectTools::DeleteAssets(AssetsToDelete, false);
			}

			for (const FString& Directory : StagingAssetDirectories)
			{
				if (AssetSubsystem->DoesDirectoryExist(Directory))
				{
					AssetSubsystem->DeleteDirectory(Directory);
				}
			}
			AssetRegistry.ScanPathsSynchronous(PathsToScan, true);
		}
	}

	TArray<FString> PhysicalDirectories;
	PhysicalDirectories.Add(ItemDirectory);
	FString StagingMountRelative = Settings->StagingMountRoot;
	StagingMountRelative.RemoveFromStart(TEXT("/Game/"));
	for (FString AssetRootDirectory : StagingAssetRootDirectories)
	{
		AssetRootDirectory.RemoveFromStart(Settings->StagingMountRoot);
		AssetRootDirectory.RemoveFromStart(TEXT("/"));
		PhysicalDirectories.Add(FPaths::Combine(FPaths::ProjectContentDir(), StagingMountRelative, AssetRootDirectory));
	}
	PhysicalDirectories.Sort([](const FString& Left, const FString& Right)
	{
		return Left.Len() > Right.Len();
	});

	bool bDeletedAnyDirectory = false;
	bool bFailedToDeleteDirectory = false;
	for (const FString& Directory : PhysicalDirectories)
	{
		if (!FPaths::DirectoryExists(Directory))
		{
			continue;
		}

		if (IFileManager::Get().DeleteDirectory(*Directory, false, true))
		{
			bDeletedAnyDirectory = true;
			continue;
		}

		bFailedToDeleteDirectory = true;
		OutError = FText::Format(FText::FromString(TEXT("\u65e0\u6cd5\u5220\u9664\u5f53\u524d\u6682\u5b58\u76ee\u5f55\uff1a\n{0}")), FText::FromString(Directory));
	}

	if (!bDeletedAnyDirectory && !bFailedToDeleteDirectory)
	{
		return true;
	}

	if (bFailedToDeleteDirectory)
	{
		return false;
	}

	return true;
}

bool SMaterialVaultWindow::CleanupStagingArea(bool bForce, FText& OutError, FMaterialVaultStagingCleanupStats* OutStats)
{
	const FString StagingRoot = GetStagingRootDirectory();
	if (!FPaths::DirectoryExists(StagingRoot))
	{
		if (OutStats)
		{
			*OutStats = FMaterialVaultStagingCleanupStats();
		}
		return true;
	}

	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	const int64 MaxBytes = static_cast<int64>(Settings->StagingMaxSizeMB) * 1024LL * 1024LL;
	const bool bOverLimit = MaxBytes > 0 && GetDirectorySizeBytes(StagingRoot) > MaxBytes;
	if (!bForce && (!Settings->bAutoCleanStagingWhenFull || !bOverLimit))
	{
		if (OutStats)
		{
			*OutStats = FMaterialVaultStagingCleanupStats();
		}
		return true;
	}

	FMaterialVaultStagingCleanupStats Stats;
	const bool bResult = CleanupUnusedStagingAssets(bForce || bOverLimit, OutError, Stats);
	if (OutStats)
	{
		*OutStats = Stats;
	}
	return bResult;
}

bool SMaterialVaultWindow::CleanupUnusedStagingAssets(bool bForce, FText& OutError, FMaterialVaultStagingCleanupStats& OutStats)
{
	OutStats = FMaterialVaultStagingCleanupStats();

	const FString StagingRoot = GetStagingRootDirectory();
	if (!FPaths::DirectoryExists(StagingRoot))
	{
		return true;
	}

	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	FString StagingMountRoot = Settings->StagingMountRoot;
	if (StagingMountRoot.IsEmpty())
	{
		StagingMountRoot = TEXT("/Game/__MaterialVaultStaging");
	}
	StagingMountRoot.ReplaceInline(TEXT("\\"), TEXT("/"));
	StagingMountRoot.RemoveFromEnd(TEXT("/"));

	FScopedSlowTask SlowTask(100.0f, FText::FromString(TEXT("正在删除未使用暂存资产...")));
	SlowTask.MakeDialogDelayed(0.5f, true);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	const int32 KeepDays = Settings->StagingKeepDays;
	const FDateTime ExpireBefore = KeepDays > 0 ? FDateTime::Now() - FTimespan::FromDays(KeepDays) : FDateTime::MaxValue();

	SlowTask.EnterProgressFrame(8.0f, FText::FromString(TEXT("读取暂存区资产...")));
	AssetRegistry.ScanPathsSynchronous({ StagingMountRoot }, true);
	TArray<FAssetData> StagingAssets;
	AssetRegistry.GetAssetsByPath(*StagingMountRoot, StagingAssets, true, false);
	OutStats.ScannedAssets = StagingAssets.Num();
	if (StagingAssets.IsEmpty())
	{
		DeleteEmptyStagingFolders(OutStats);
		return true;
	}

	SlowTask.EnterProgressFrame(12.0f, FText::FromString(TEXT("扫描整个项目的资产引用...")));
	AssetRegistry.ScanPathsSynchronous({ TEXT("/Game") }, false);

	TSet<FName> StagingPackages;
	StagingPackages.Reserve(StagingAssets.Num());
	for (const FAssetData& AssetData : StagingAssets)
	{
		if (AssetData.IsValid())
		{
			StagingPackages.Add(AssetData.PackageName);
		}
	}

	TSet<FName> ProtectedPackages;
	const float ReferenceBudget = 50.0f;
	const float PerAssetBudget = StagingAssets.Num() > 0 ? ReferenceBudget / static_cast<float>(StagingAssets.Num()) : 0.0f;
	for (const FAssetData& AssetData : StagingAssets)
	{
		if (!AssetData.IsValid())
		{
			++OutStats.SkippedAssets;
			continue;
		}

		if (PerAssetBudget > 0.0f)
		{
			SlowTask.EnterProgressFrame(PerAssetBudget, FText::Format(
				FText::FromString(TEXT("检查引用：{0}")),
				FText::FromName(AssetData.AssetName)));
		}

		bool bUsedByProject = IsStagingPackageUsedByProject(AssetData.PackageName, StagingMountRoot, StagingPackages, AssetRegistry);
		if (!bUsedByProject)
		{
			if (UObject* LoadedAsset = AssetData.FastGetAsset(false))
			{
				UObject* AssetForReferenceCheck = LoadedAsset;
				FReferencerInformationList MemoryReferences;
				if (GEditor && GEditor->Trans)
				{
					GEditor->Trans->DisableObjectSerialization();
				}
				IsReferenced(AssetForReferenceCheck, GARBAGE_COLLECTION_KEEPFLAGS, EInternalObjectFlags_GarbageCollectionKeepFlags, true, &MemoryReferences);
				if (GEditor && GEditor->Trans)
				{
					GEditor->Trans->EnableObjectSerialization();
				}

				auto HasProjectReference = [&StagingPackages, &AssetData](const TArray<FReferencerInformation>& References)
				{
					for (const FReferencerInformation& ReferenceInfo : References)
					{
						UObject* Referencer = ReferenceInfo.Referencer;
						if (!Referencer)
						{
							continue;
						}
						UPackage* ReferencerPackage = Referencer->GetPackage();
						if (!ReferencerPackage)
						{
							continue;
						}
						const FName ReferencerPackageName = ReferencerPackage->GetFName();
						const FString ReferencerPackageString = ReferencerPackageName.ToString();
						if (ReferencerPackageName == AssetData.PackageName || StagingPackages.Contains(ReferencerPackageName))
						{
							continue;
						}
						if (ReferencerPackageString.StartsWith(TEXT("/Game")))
						{
							return true;
						}
					}
					return false;
				};

				bUsedByProject =
					HasProjectReference(MemoryReferences.InternalReferences) ||
					HasProjectReference(MemoryReferences.ExternalReferences);
			}
		}

		if (!bUsedByProject)
		{
			continue;
		}

		ProtectedPackages.Add(AssetData.PackageName);
		TArray<FName> PendingDependencies;
		PendingDependencies.Add(AssetData.PackageName);
		while (!PendingDependencies.IsEmpty())
		{
			const FName CurrentPackage = PendingDependencies.Pop(EAllowShrinking::No);
			TArray<FName> Dependencies;
			AssetRegistry.GetDependencies(
				CurrentPackage,
				Dependencies,
				UE::AssetRegistry::EDependencyCategory::Package,
				UE::AssetRegistry::FDependencyQuery());

			for (const FName Dependency : Dependencies)
			{
				if (!StagingPackages.Contains(Dependency) || ProtectedPackages.Contains(Dependency))
				{
					continue;
				}
				ProtectedPackages.Add(Dependency);
				PendingDependencies.Add(Dependency);
			}
		}
	}

	TArray<FAssetData> AssetsToDelete;
	AssetsToDelete.Reserve(StagingAssets.Num());
	for (const FAssetData& AssetData : StagingAssets)
	{
		if (!AssetData.IsValid())
		{
			continue;
		}

		if (ProtectedPackages.Contains(AssetData.PackageName))
		{
			++OutStats.UsedAssets;
			continue;
		}

		if (!bForce && KeepDays > 0)
		{
			FString AssetPackageFilename;
			if (FPackageName::TryConvertLongPackageNameToFilename(AssetData.PackageName.ToString(), AssetPackageFilename, FPackageName::GetAssetPackageExtension()))
			{
				const FFileStatData FileStatData = IFileManager::Get().GetStatData(*AssetPackageFilename);
				if (FileStatData.bIsValid && FileStatData.ModificationTime > ExpireBefore)
				{
					++OutStats.SkippedAssets;
					continue;
				}
			}
		}

		AssetsToDelete.Add(AssetData);
	}

	if (AssetsToDelete.IsEmpty())
	{
		DeleteEmptyStagingFolders(OutStats);
		return true;
	}

	SlowTask.EnterProgressFrame(20.0f, FText::FromString(TEXT("删除未使用暂存资产...")));
	constexpr int32 DeleteBatchSize = 128;
	for (int32 StartIndex = 0; StartIndex < AssetsToDelete.Num(); StartIndex += DeleteBatchSize)
	{
		TArray<FAssetData> Batch;
		const int32 BatchCount = FMath::Min(DeleteBatchSize, AssetsToDelete.Num() - StartIndex);
		Batch.Reserve(BatchCount);
		for (int32 BatchIndex = 0; BatchIndex < BatchCount; ++BatchIndex)
		{
			Batch.Add(AssetsToDelete[StartIndex + BatchIndex]);
		}
		const int32 DeletedInBatch = ObjectTools::DeleteAssets(Batch, false);
		OutStats.DeletedAssets += DeletedInBatch;
		OutStats.SkippedAssets += FMath::Max(0, Batch.Num() - DeletedInBatch);
	}

	SlowTask.EnterProgressFrame(10.0f, FText::FromString(TEXT("清理空文件夹...")));
	DeleteEmptyStagingFolders(OutStats);
	if (!IsRunningCommandlet())
	{
		CollectGarbage(RF_NoFlags);
	}
	return true;
}

bool SMaterialVaultWindow::IsStagingPackageUsedByProject(const FName PackageName, const FString& StagingMountRoot, const TSet<FName>& StagingPackages, IAssetRegistry& AssetRegistry)
{
	TArray<FName> Referencers;
	AssetRegistry.GetReferencers(
		PackageName,
		Referencers,
		UE::AssetRegistry::EDependencyCategory::Package,
		UE::AssetRegistry::FDependencyQuery());

	for (const FName Referencer : Referencers)
	{
		if (Referencer == PackageName || StagingPackages.Contains(Referencer))
		{
			continue;
		}

		const FString ReferencerString = Referencer.ToString();
		if (!ReferencerString.StartsWith(TEXT("/Game")))
		{
			continue;
		}
		if (ReferencerString == StagingMountRoot || ReferencerString.StartsWith(StagingMountRoot + TEXT("/")))
		{
			continue;
		}
		return true;
	}

	return false;
}

void SMaterialVaultWindow::DeleteEmptyStagingFolders(FMaterialVaultStagingCleanupStats& InOutStats)
{
	const FString StagingRoot = GetStagingRootDirectory();
	if (!FPaths::DirectoryExists(StagingRoot))
	{
		return;
	}

	TArray<FString> Directories;
	IFileManager::Get().IterateDirectoryRecursively(*StagingRoot, [&Directories](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory)
		{
			Directories.Add(FilenameOrDirectory);
		}
		return true;
	});

	Directories.Sort([](const FString& Left, const FString& Right)
	{
		return Left.Len() > Right.Len();
	});

	for (const FString& Directory : Directories)
	{
		bool bIsEmpty = true;
		IFileManager::Get().IterateDirectory(*Directory, [&bIsEmpty](const TCHAR*, bool)
		{
			bIsEmpty = false;
			return false;
		});

		if (bIsEmpty && IFileManager::Get().DeleteDirectory(*Directory, false, false))
		{
			++InOutStats.DeletedFolders;
		}
	}
}

bool SMaterialVaultWindow::WriteMvpackFile(const FString& PackPath, const TArray<TPair<FString, FString>>& Files, FText& OutError)
{
	TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*PackPath));
	if (!Writer)
	{
		OutError = FText::Format(FText::FromString(TEXT("无法创建材质包文件：\n{0}")), FText::FromString(PackPath));
		return false;
	}

	const uint8 Magic[8] = { 'M', 'V', 'P', 'A', 'C', 'K', '0', '1' };
	Writer->Serialize((void*)Magic, UE_ARRAY_COUNT(Magic));

	int32 Version = 1;
	int32 FileCount = Files.Num();
	*Writer << Version;
	*Writer << FileCount;

	TArray<uint8> CopyBuffer;
	CopyBuffer.SetNumUninitialized(4 * 1024 * 1024);
	constexpr int64 YieldAfterBytes = 64LL * 1024LL * 1024LL;
	int64 BytesSinceYield = 0;
	for (const TPair<FString, FString>& FilePair : Files)
	{
		const FString ArchivePath = FilePair.Key;
		const FString SourcePath = FilePair.Value;

		TUniquePtr<FArchive> SourceReader(IFileManager::Get().CreateFileReader(*SourcePath));
		if (!SourceReader)
		{
			OutError = FText::Format(FText::FromString(TEXT("无法读取要打包的文件：\n{0}")), FText::FromString(SourcePath));
			return false;
		}

		FString StoredPath = ArchivePath;
		int64 Size = SourceReader->TotalSize();
		*Writer << StoredPath;
		*Writer << Size;

		int64 RemainingBytes = Size;
		while (RemainingBytes > 0)
		{
			const int64 ChunkSize64 = FMath::Min<int64>(RemainingBytes, CopyBuffer.Num());
			const int32 ChunkSize = static_cast<int32>(ChunkSize64);
			SourceReader->Serialize(CopyBuffer.GetData(), ChunkSize);
			if (SourceReader->IsError())
			{
				OutError = FText::Format(FText::FromString(TEXT("读取打包文件时失败：\n{0}")), FText::FromString(SourcePath));
				return false;
			}
			Writer->Serialize(CopyBuffer.GetData(), ChunkSize);
			if (Writer->IsError())
			{
				OutError = FText::Format(FText::FromString(TEXT("写入材质包时失败：\n{0}")), FText::FromString(ArchivePath));
				return false;
			}
			RemainingBytes -= ChunkSize64;
			BytesSinceYield += ChunkSize64;
			if (BytesSinceYield >= YieldAfterBytes)
			{
				FPlatformProcess::Sleep(IsRunningCommandlet() ? 0.04f : 0.02f);
				BytesSinceYield = 0;
			}
		}
	}

	Writer->Close();
	return true;
}

void SMaterialVaultWindow::AddPackageFiles(const FString& PackageName, const FString& ArchiveRoot, TArray<TPair<FString, FString>>& InOutFiles)
{
	const FString AssetFile = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	const FString BaseFile = FPaths::ChangeExtension(AssetFile, TEXT(""));
	const FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

	TArray<FString> CandidateFiles;
	CandidateFiles.Add(AssetFile);
	CandidateFiles.Add(BaseFile + TEXT(".uexp"));
	CandidateFiles.Add(BaseFile + TEXT(".ubulk"));
	CandidateFiles.Add(BaseFile + TEXT(".uptnl"));

	for (const FString& CandidateFile : CandidateFiles)
	{
		if (!FPaths::FileExists(CandidateFile))
		{
			continue;
		}

		FString RelativeFile = FPaths::ConvertRelativePathToFull(CandidateFile);
		if (!FPaths::MakePathRelativeTo(RelativeFile, *ContentDir))
		{
			RelativeFile = FPaths::GetCleanFilename(CandidateFile);
		}

		RelativeFile.ReplaceInline(TEXT("\\"), TEXT("/"));
		const FString ArchivePath = ArchiveRoot / RelativeFile;
		InOutFiles.AddUnique(TPair<FString, FString>(ArchivePath, CandidateFile));
	}
}

FString SMaterialVaultWindow::BuildManifestJson(const FMaterialVaultItem& Item, const FString& VaultRoot, const FString& ManifestRelativePath)
{
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();

	auto AppendEntry = [](FString& Json, const FMaterialVaultAssetEntry& Entry, const FString& Indent)
	{
		Json += Indent + TEXT("{\n");
		Json += Indent + FString::Printf(TEXT("  \"role\": \"%s\",\n"), *RoleToString(Entry.Role));
		Json += Indent + FString::Printf(TEXT("  \"assetClass\": \"%s\",\n"), *JsonEscape(Entry.AssetClass));
		Json += Indent + FString::Printf(TEXT("  \"originalObjectPath\": \"%s\",\n"), *JsonEscape(Entry.OriginalObjectPath.ToString()));
		Json += Indent + FString::Printf(TEXT("  \"originalPackageName\": \"%s\",\n"), *JsonEscape(Entry.OriginalPackageName));
		Json += Indent + FString::Printf(TEXT("  \"plannedPackageName\": \"%s\",\n"), *JsonEscape(Entry.PlannedPackageName));
		Json += Indent + FString::Printf(TEXT("  \"plannedObjectPath\": \"%s\",\n"), *JsonEscape(Entry.PlannedObjectPath));
		Json += Indent + FString::Printf(TEXT("  \"thumbnailPath\": \"%s\",\n"), *JsonEscape(Entry.ThumbnailPath));
		Json += Indent + FString::Printf(TEXT("  \"stagingPackageName\": \"%s\",\n"), *JsonEscape(GetStagingPackageName(Entry)));
		Json += Indent + FString::Printf(TEXT("  \"stagingObjectPath\": \"%s\"\n"), *JsonEscape(GetStagingObjectPath(Entry)));
		Json += Indent + TEXT("}");
	};

	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"schemaVersion\": 1,\n"));
	Json += FString::Printf(TEXT("  \"packFormat\": \"mvpack-raw-v1\",\n"));
	Json += FString::Printf(TEXT("  \"id\": \"%s\",\n"), *JsonEscape(Item.Id));
	Json += FString::Printf(TEXT("  \"displayName\": \"%s\",\n"), *JsonEscape(Item.DisplayName));
	Json += FString::Printf(TEXT("  \"category\": \"%s\",\n"), *JsonEscape(Item.Category));
	Json += FString::Printf(TEXT("  \"sourceEngineVersion\": \"%s\",\n"), *JsonEscape(Item.SourceEngineVersion));
	Json += FString::Printf(TEXT("  \"libraryMountRoot\": \"%s\",\n"), *JsonEscape(Settings->LibraryMountRoot));
	Json += FString::Printf(TEXT("  \"vaultRelativePath\": \"%s\",\n"), *JsonEscape(ManifestRelativePath));
	Json += TEXT("  \"rootAsset\": ");
	AppendEntry(Json, Item.RootAsset, TEXT("  "));
	Json += TEXT(",\n");
	Json += TEXT("  \"dependencies\": [\n");
	for (int32 Index = 0; Index < Item.Dependencies.Num(); ++Index)
	{
		Json += TEXT("    ");
		AppendEntry(Json, Item.Dependencies[Index], TEXT("    "));
		Json += Index + 1 < Item.Dependencies.Num() ? TEXT(",\n") : TEXT("\n");
	}
	Json += TEXT("  ]\n");
	Json += TEXT("}\n");
	return Json;
}

bool SMaterialVaultWindow::CollectToStaging(const FMaterialVaultItem& Item, FString& OutRootPath, FText& OutError, FScopedSlowTask* ParentTask, float ParentBudget)
{
	if (!GEditor)
	{
		OutError = FText::FromString(TEXT("没有可用的编辑器实例。"));
		return false;
	}

	UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (!AssetSubsystem)
	{
		OutError = FText::FromString(TEXT("无法获取 EditorAssetSubsystem。"));
		return false;
	}

	auto GetStagingPriority = [](EMaterialVaultAssetRole Role)
	{
		switch (Role)
		{
		case EMaterialVaultAssetRole::Texture:
		case EMaterialVaultAssetRole::MaterialFunction:
			return 0;
		case EMaterialVaultAssetRole::Material:
		case EMaterialVaultAssetRole::RootMaterial:
			return 1;
		case EMaterialVaultAssetRole::MaterialInstance:
			return 2;
		default:
			return 1;
		}
	};

	TArray<FMaterialVaultAssetEntry> Entries = GetAllEntries(Item);
	Entries.Sort([&GetStagingPriority](const FMaterialVaultAssetEntry& Left, const FMaterialVaultAssetEntry& Right)
	{
		const int32 LeftPriority = GetStagingPriority(Left.Role);
		const int32 RightPriority = GetStagingPriority(Right.Role);
		if (LeftPriority != RightPriority)
		{
			return LeftPriority < RightPriority;
		}
		return Left.OriginalObjectPath.ToString() < Right.OriginalObjectPath.ToString();
	});
	const int32 EntryCount = Entries.Num();
	const float PerEntryBudget = (ParentTask && EntryCount > 0) ? (ParentBudget / static_cast<float>(EntryCount)) : 0.0f;

	TMap<UObject*, UObject*> ReplacementMap;
	TArray<UObject*> DuplicatedAssets;
	TArray<TPair<UObject*, UObject*>> DuplicatedAssetPairs;
	TArray<TPair<UObject*, EMaterialVaultAssetRole>> DuplicatedAssetsWithRoles;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const FMaterialVaultAssetEntry& Entry = Entries[EntryIndex];
		const FString SourcePath = Entry.OriginalObjectPath.ToString();
		const FString DestinationPath = GetStagingObjectPath(Entry);
		const FString DestinationDirectory = FPackageName::GetLongPackagePath(DestinationPath);

		if (ParentTask && PerEntryBudget > 0.0f)
		{
			ParentTask->EnterProgressFrame(PerEntryBudget, FText::Format(
				FText::FromString(TEXT("[{0}] 收录 {1}/{2}：{3}")),
				FText::FromString(Item.DisplayName),
				FText::AsNumber(EntryIndex + 1),
				FText::AsNumber(EntryCount),
				FText::FromString(FPaths::GetBaseFilename(SourcePath))));
		}

		AssetSubsystem->MakeDirectory(DestinationDirectory);

		UObject* SourceAsset = AssetSubsystem->LoadAsset(SourcePath);
		if (!SourceAsset)
		{
			OutError = FText::Format(FText::FromString(TEXT("无法加载源资产：\n{0}")), FText::FromString(SourcePath));
			return false;
		}

		UObject* DuplicatedAsset = nullptr;
		if (AssetSubsystem->DoesAssetExist(DestinationPath))
		{
			DuplicatedAsset = AssetSubsystem->LoadAsset(DestinationPath);
		}
		if (!DuplicatedAsset)
		{
			DuplicatedAsset = AssetSubsystem->DuplicateAsset(SourcePath, DestinationPath);
		}
		if (!DuplicatedAsset)
		{
			AssetRegistryModule.Get().ScanPathsSynchronous({ DestinationDirectory }, true);
			const FAssetData ExistingDestinationAsset = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(DestinationPath));
			DuplicatedAsset = ExistingDestinationAsset.IsValid() ? ExistingDestinationAsset.GetAsset() : nullptr;
		}

		if (!DuplicatedAsset)
		{
			OutError = FText::Format(FText::FromString(TEXT("无法复制到暂存路径：\n{0}")), FText::FromString(DestinationPath));
			return false;
		}

		ReplacementMap.Add(SourceAsset, DuplicatedAsset);
		DuplicatedAssets.Add(DuplicatedAsset);
		DuplicatedAssetPairs.Add(TPair<UObject*, UObject*>(SourceAsset, DuplicatedAsset));
		DuplicatedAssetsWithRoles.Add(TPair<UObject*, EMaterialVaultAssetRole>(DuplicatedAsset, Entry.Role));
	}

	for (UObject* DuplicatedAsset : DuplicatedAssets)
	{
		if (!DuplicatedAsset)
		{
			continue;
		}

		FArchiveReplaceObjectRef<UObject> ReplaceReferences(
			DuplicatedAsset,
			ReplacementMap,
			EArchiveReplaceObjectFlags::IgnoreOuterRef);
		DuplicatedAsset->MarkPackageDirty();
	}

	for (const TPair<UObject*, UObject*>& AssetPair : DuplicatedAssetPairs)
	{
		if (AssetPair.Key && AssetPair.Value)
		{
			RestoreMaterialVaultGraphFromSource(AssetPair.Key, AssetPair.Value, ReplacementMap);
		}
	}

	for (const TPair<UObject*, UObject*>& AssetPair : DuplicatedAssetPairs)
	{
		UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(AssetPair.Key);
		UMaterialInstance* DuplicatedInstance = Cast<UMaterialInstance>(AssetPair.Value);
		if (!SourceInstance || !DuplicatedInstance || !SourceInstance->Parent)
		{
			continue;
		}

		if (UObject** ReplacementParent = ReplacementMap.Find(SourceInstance->Parent))
		{
			if (UMaterialInterface* DuplicatedParent = Cast<UMaterialInterface>(*ReplacementParent))
			{
				DuplicatedInstance->Parent = DuplicatedParent;
				DuplicatedInstance->MarkPackageDirty();
			}
		}
	}

	TArray<UMaterialInstance*> DuplicatedFamilyInstances;
	for (UObject* DuplicatedAsset : DuplicatedAssets)
	{
		if (UMaterialInstance* DuplicatedInstance = Cast<UMaterialInstance>(DuplicatedAsset))
		{
			DuplicatedFamilyInstances.Add(DuplicatedInstance);
		}
	}

	for (UObject* DuplicatedAsset : DuplicatedAssets)
	{
		if (DuplicatedAsset)
		{
			RepairMaterialVaultMaterialForPackaging(DuplicatedAsset, DuplicatedFamilyInstances);
		}
	}

	for (UObject* DuplicatedAsset : DuplicatedAssets)
	{
		if (DuplicatedAsset)
		{
			RefreshMaterialVaultFunctionCalls(DuplicatedAsset);
		}
	}

	for (const TPair<UObject*, UObject*>& AssetPair : DuplicatedAssetPairs)
	{
		if (AssetPair.Key && AssetPair.Value)
		{
			RestoreMaterialVaultGraphFromSource(AssetPair.Key, AssetPair.Value, ReplacementMap);
		}
	}

	auto GetSavePriority = [&GetStagingPriority](EMaterialVaultAssetRole Role)
	{
		return GetStagingPriority(Role);
	};
	DuplicatedAssetsWithRoles.Sort([&GetSavePriority](const TPair<UObject*, EMaterialVaultAssetRole>& Left, const TPair<UObject*, EMaterialVaultAssetRole>& Right)
	{
		return GetSavePriority(Left.Value) < GetSavePriority(Right.Value);
	});
	bool bSavedAllAssets = true;
	for (int32 SavePriority = 0; SavePriority <= 2; ++SavePriority)
	{
		TArray<UObject*> AssetsToSave;
		for (const TPair<UObject*, EMaterialVaultAssetRole>& AssetWithRole : DuplicatedAssetsWithRoles)
		{
			if (AssetWithRole.Key && GetSavePriority(AssetWithRole.Value) == SavePriority)
			{
				AssetsToSave.Add(AssetWithRole.Key);
			}
		}

		for (UObject* AssetToSave : AssetsToSave)
		{
			if (!SaveMaterialVaultStagingAssetQuietly(AssetToSave, OutError))
			{
				bSavedAllAssets = false;
				break;
			}
		}
		if (!bSavedAllAssets)
		{
			break;
		}
	}

	if (!bSavedAllAssets)
	{
		OutError = FText::FromString(TEXT("暂存资产已创建，但保存时失败。请检查是否有只读文件或源码控制锁。"));
		return false;
	}

	for (UObject* DuplicatedAsset : DuplicatedAssets)
	{
		if (!DuplicatedAsset)
		{
			continue;
		}
		DuplicatedAsset->ClearFlags(RF_Standalone);
		if (UPackage* Package = DuplicatedAsset->GetOutermost())
		{
			Package->ClearFlags(RF_Standalone);
		}
	}

	OutRootPath = GetStagingObjectPath(Item.RootAsset);
	return true;
}

TArray<FMaterialVaultAssetEntry> SMaterialVaultWindow::GetAllEntries(const FMaterialVaultItem& Item)
{
	TArray<FMaterialVaultAssetEntry> Entries;
	Entries.Add(Item.RootAsset);
	for (const FMaterialVaultAssetEntry& Dependency : Item.Dependencies)
	{
		bool bAlreadyAdded = false;
		for (const FMaterialVaultAssetEntry& ExistingEntry : Entries)
		{
			if (ExistingEntry.OriginalPackageName == Dependency.OriginalPackageName)
			{
				bAlreadyAdded = true;
				break;
			}
		}
		if (!bAlreadyAdded)
		{
			Entries.Add(Dependency);
		}
	}
	return Entries;
}

FString SMaterialVaultWindow::GetStagingObjectPath(const FMaterialVaultAssetEntry& Entry)
{
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	FString StagingPath = Entry.PlannedObjectPath;
	StagingPath.ReplaceInline(*Settings->LibraryMountRoot, *Settings->StagingMountRoot, ESearchCase::CaseSensitive);
	return StagingPath;
}

FString SMaterialVaultWindow::GetStagingPackageName(const FMaterialVaultAssetEntry& Entry)
{
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	FString StagingPath = Entry.PlannedPackageName;
	StagingPath.ReplaceInline(*Settings->LibraryMountRoot, *Settings->StagingMountRoot, ESearchCase::CaseSensitive);
	return StagingPath;
}

bool SMaterialVaultWindow::InstallPluginIntoProject(const FString& ProjectFilePath, FText& OutError)
{
	const TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(TEXT("PBRStudio"));
	if (!ThisPlugin.IsValid())
	{
		OutError = FText::FromString(TEXT("无法找到当前 PBRStudio 插件目录。"));
		return false;
	}

	const FString ProjectPluginsDir = FPaths::Combine(FPaths::GetPath(ProjectFilePath), TEXT("Plugins"));
	const FString TargetPluginDir = FPaths::Combine(ProjectPluginsDir, TEXT("PBRStudio"));
	const FString TargetPluginFile = FPaths::Combine(TargetPluginDir, TEXT("PBRStudio.uplugin"));
	if (FPaths::FileExists(TargetPluginFile))
	{
		return true;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*ProjectPluginsDir);

	if (!PlatformFile.CopyDirectoryTree(*TargetPluginDir, *ThisPlugin->GetBaseDir(), true))
	{
		OutError = FText::Format(
			FText::FromString(TEXT("无法把插件复制到目标项目：\n{0}")),
			FText::FromString(TargetPluginDir));
		return false;
	}

	return true;
}

bool SMaterialVaultWindow::RunExternalPackThumbnailRepair(const FString& PackPath, FText& OutError)
{
	const FString ProjectFilePath = FPaths::GetProjectFilePath();
	if (ProjectFilePath.IsEmpty() || !FPaths::FileExists(ProjectFilePath))
	{
		OutError = FText::FromString(TEXT("当前项目没有可用的 .uproject 路径，无法启动外部深度修复进程。"));
		return false;
	}

	const FString EditorCmdPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
	if (!FPaths::FileExists(EditorCmdPath))
	{
		OutError = FText::Format(FText::FromString(TEXT("找不到 UnrealEditor-Cmd.exe：\n{0}")), FText::FromString(EditorCmdPath));
		return false;
	}

	const TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(TEXT("PBRStudio"));
	if (!ThisPlugin.IsValid())
	{
		OutError = FText::FromString(TEXT("无法找到当前 PBRStudio 插件目录。"));
		return false;
	}
	const FString PluginFilePath = FPaths::Combine(ThisPlugin->GetBaseDir(), TEXT("PBRStudio.uplugin"));
	if (!FPaths::FileExists(PluginFilePath))
	{
		OutError = FText::Format(FText::FromString(TEXT("找不到当前插件描述文件：\n{0}")), FText::FromString(PluginFilePath));
		return false;
	}

	FString SafeBaseRoot = ResolveVaultBaseRoot();
	SafeBaseRoot.TrimEndInline();
	while (SafeBaseRoot.EndsWith(TEXT("\\")) || SafeBaseRoot.EndsWith(TEXT("/")))
	{
		SafeBaseRoot.LeftChopInline(1);
	}
	const FString ActiveLibraryName = GetDefault<UMaterialVaultSettings>()->ActiveLibraryName.IsEmpty()
		? TEXT("默认材质库")
		: GetDefault<UMaterialVaultSettings>()->ActiveLibraryName;

	const FString RepairDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MaterialVault"), TEXT("RepairLogs"));
	IFileManager::Get().MakeDirectory(*RepairDir, true);
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString PackBaseName = FPaths::GetBaseFilename(PackPath);
	PackBaseName.ReplaceInline(TEXT(" "), TEXT("_"));
	const FString OutputPath = FPaths::Combine(RepairDir, PackBaseName + TEXT("_") + Stamp + TEXT(".json"));
	const FString RepairLogPath = FPaths::Combine(RepairDir, PackBaseName + TEXT("_") + Stamp + TEXT(".log"));

	const FString Params = FString::Printf(
		TEXT("\"%s\" -PLUGIN=\"%s\" -EnablePlugins=PBRStudio -run=MaterialVaultScan -MaterialVaultOutput=\"%s\" -MaterialVaultRepairPackThumbnail=\"%s\" -MaterialVaultRenderThumbnails -MaterialVaultVaultRoot=\"%s\" -MaterialVaultActiveLibraryName=\"%s\" -stdout -FullStdOutLogOutput -unattended -nop4 -nosplash"),
		*ProjectFilePath,
		*PluginFilePath,
		*OutputPath,
		*PackPath,
		*SafeBaseRoot,
		*ActiveLibraryName);

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		OutError = FText::FromString(TEXT("无法创建外部深度修复输出管道。"));
		return false;
	}

	uint32 ProcessId = 0;
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(*EditorCmdPath, *Params, false, true, true, &ProcessId, 0, nullptr, WritePipe, nullptr, WritePipe);
	FPlatformProcess::ClosePipe(nullptr, WritePipe);
	WritePipe = nullptr;
	if (!ProcHandle.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, nullptr);
		OutError = FText::FromString(TEXT("无法启动外部 UE 深度修复进程。"));
		return false;
	}

	FString FullOutput;
	while (FPlatformProcess::IsProcRunning(ProcHandle))
	{
		const FString Chunk = FPlatformProcess::ReadPipe(ReadPipe);
		if (!Chunk.IsEmpty())
		{
			FullOutput += Chunk;
		}
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}
		FPlatformProcess::Sleep(0.1f);
	}

	FullOutput += FPlatformProcess::ReadPipe(ReadPipe);
	int32 ReturnCode = -1;
	FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
	FPlatformProcess::CloseProc(ProcHandle);
	FPlatformProcess::ClosePipe(ReadPipe, nullptr);
	FFileHelper::SaveStringToFile(FullOutput, *RepairLogPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (ReturnCode != 0)
	{
		OutError = FText::Format(
			FText::FromString(TEXT("外部深度修复失败，返回码 {0}。\n日志：\n{1}")),
			FText::AsNumber(ReturnCode),
			FText::FromString(RepairLogPath));
		return false;
	}

	return true;
}

bool SMaterialVaultWindow::RunExternalProjectScan(const FString& ProjectFilePath, FString& OutOutputPath, FText& OutError, const TArray<FString>* OnlyItemIds)
{
	if (!FPaths::FileExists(ProjectFilePath))
	{
		OutError = FText::Format(FText::FromString(TEXT("项目文件不存在：\n{0}")), FText::FromString(ProjectFilePath));
		return false;
	}

	FScopedSlowTask SlowTask(100.0f, FText::FromString(TEXT("正在后台扫描外部项目...")));
	SlowTask.MakeDialog(true);
	SlowTask.EnterProgressFrame(5.0f, FText::FromString(TEXT("准备目标项目...")));

	const TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(TEXT("PBRStudio"));
	if (!ThisPlugin.IsValid())
	{
		OutError = FText::FromString(TEXT("无法找到当前 PBRStudio 插件目录。"));
		return false;
	}
	const FString PluginFilePath = FPaths::Combine(ThisPlugin->GetBaseDir(), TEXT("PBRStudio.uplugin"));
	if (!FPaths::FileExists(PluginFilePath))
	{
		OutError = FText::Format(FText::FromString(TEXT("找不到当前插件描述文件：\n{0}")), FText::FromString(PluginFilePath));
		return false;
	}

	const FString EditorCmdPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
	if (!FPaths::FileExists(EditorCmdPath))
	{
		OutError = FText::Format(FText::FromString(TEXT("找不到 UnrealEditor-Cmd.exe：\n{0}")), FText::FromString(EditorCmdPath));
		return false;
	}

	const FString VaultRoot = ResolveVaultRoot();
	if (!EnsureVaultLayout(VaultRoot, OutError))
	{
		return false;
	}

	const FString ExternalScanDir = FPaths::Combine(VaultRoot, TEXT("ExternalScans"));
	IFileManager::Get().MakeDirectory(*ExternalScanDir, true);
	const FString ProjectName = FPaths::GetBaseFilename(ProjectFilePath);
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	OutOutputPath = FPaths::Combine(ExternalScanDir, ProjectName + TEXT("_") + Stamp + TEXT(".json"));

	FString SafeBaseRoot = ResolveVaultBaseRoot();
	SafeBaseRoot.TrimEndInline();
	while (SafeBaseRoot.EndsWith(TEXT("\\")) || SafeBaseRoot.EndsWith(TEXT("/")))
	{
		SafeBaseRoot.LeftChopInline(1);
	}
	const FString ActiveLibraryName = GetDefault<UMaterialVaultSettings>()->ActiveLibraryName.IsEmpty()
		? TEXT("默认材质库")
		: GetDefault<UMaterialVaultSettings>()->ActiveLibraryName;

	FString OnlyIdsParam;
	if (OnlyItemIds && OnlyItemIds->Num() > 0)
	{
		FString JoinedIds;
		for (const FString& ItemId : *OnlyItemIds)
		{
			if (ItemId.IsEmpty())
			{
				continue;
			}
			if (!JoinedIds.IsEmpty())
			{
				JoinedIds += TEXT(";");
			}
			JoinedIds += ItemId;
		}
		if (!JoinedIds.IsEmpty())
		{
			OnlyIdsParam = FString::Printf(TEXT(" -MaterialVaultOnlyIds=\"%s\""), *JoinedIds);
		}
	}

	const FString Params = FString::Printf(
		TEXT("\"%s\" -PLUGIN=\"%s\" -EnablePlugins=PBRStudio -run=MaterialVaultScan -MaterialVaultOutput=\"%s\" -MaterialVaultRoot=/Game -MaterialVaultVaultRoot=\"%s\" -MaterialVaultActiveLibraryName=\"%s\" -MaterialVaultGeneratePacks%s -stdout -unattended -nop4 -nosplash"),
		*ProjectFilePath,
		*PluginFilePath,
		*OutOutputPath,
		*SafeBaseRoot,
		*ActiveLibraryName,
		*OnlyIdsParam);

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		OutError = FText::FromString(TEXT("无法创建后台扫描输出管道。"));
		return false;
	}

	uint32 ProcessId = 0;
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(*EditorCmdPath, *Params, false, true, true, &ProcessId, 0, nullptr, WritePipe, nullptr, WritePipe);
	FPlatformProcess::ClosePipe(nullptr, WritePipe);
	WritePipe = nullptr;
	if (!ProcHandle.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, nullptr);
		OutError = FText::FromString(TEXT("无法启动后台 UE 扫描进程。"));
		return false;
	}

	float DisplayProgress = 5.0f;
	int32 LastPackIndex = 0;
	int32 TotalPacks = 0;
	FString BufferedOutput;
	FString FullOutput;
	SlowTask.EnterProgressFrame(5.0f, FText::FromString(TEXT("启动后台 Unreal Editor...")));
	DisplayProgress += 5.0f;

	while (FPlatformProcess::IsProcRunning(ProcHandle))
	{
		const FString Chunk = FPlatformProcess::ReadPipe(ReadPipe);
		if (!Chunk.IsEmpty())
		{
			FullOutput += Chunk;
			BufferedOutput += Chunk;
			TArray<FString> Lines;
			BufferedOutput.ParseIntoArrayLines(Lines, false);
			if (!BufferedOutput.EndsWith(TEXT("\n")) && Lines.Num() > 0)
			{
				BufferedOutput = Lines.Last();
				Lines.RemoveAt(Lines.Num() - 1);
			}
			else
			{
				BufferedOutput.Reset();
			}

			for (const FString& Line : Lines)
			{
				if (Line.Contains(TEXT("MV_PROGRESS:SCAN_START")))
				{
					const float Target = 20.0f;
					SlowTask.EnterProgressFrame(FMath::Max(0.0f, Target - DisplayProgress), FText::FromString(TEXT("正在扫描材质资产...")));
					DisplayProgress = Target;
				}
				else if (Line.Contains(TEXT("MV_PROGRESS:SCAN_DONE:")))
				{
					FString Marker, Remainder, TotalText, MaterialText, InstanceText, StandaloneText, SkippedText;
					Line.Split(TEXT("MV_PROGRESS:SCAN_DONE:"), &Marker, &Remainder);
					Remainder.Split(TEXT(":"), &TotalText, &Remainder);
					Remainder.Split(TEXT(":"), &MaterialText, &Remainder);
					Remainder.Split(TEXT(":"), &InstanceText, &Remainder);
					Remainder.Split(TEXT(":"), &StandaloneText, &SkippedText);
					const float Target = 35.0f;
					SlowTask.EnterProgressFrame(FMath::Max(0.0f, Target - DisplayProgress), FText::Format(
						FText::FromString(TEXT("\u626b\u63cf\u5b8c\u6210\uff1a\u6bcd\u6750\u8d28 {0}\uff0c\u6750\u8d28\u5b9e\u4f8b {1}\uff0c\u72ec\u7acb\u5b9e\u4f8b {2}\uff0c\u8df3\u8fc7 {3}\uff0c\u5f00\u59cb\u751f\u6210 {4} \u4e2a\u6750\u8d28\u65cf\u5305...")),
						FText::AsNumber(FCString::Atoi(*MaterialText)),
						FText::AsNumber(FCString::Atoi(*InstanceText)),
						FText::AsNumber(FCString::Atoi(*StandaloneText)),
						FText::AsNumber(FCString::Atoi(*SkippedText)),
						FText::AsNumber(FCString::Atoi(*TotalText))));
					DisplayProgress = Target;
				}
				else if (Line.Contains(TEXT("MV_PROGRESS:PACK:")))
				{
					FString Marker, IndexText, Remainder, TotalText, NameText;
					Line.Split(TEXT("MV_PROGRESS:PACK:"), &Marker, &Remainder);
					Remainder.Split(TEXT(":"), &IndexText, &Remainder);
					Remainder.Split(TEXT(":"), &TotalText, &NameText);
					LastPackIndex = FCString::Atoi(*IndexText);
					TotalPacks = FCString::Atoi(*TotalText);
					const float Target = TotalPacks > 0 ? 35.0f + 60.0f * static_cast<float>(LastPackIndex) / static_cast<float>(TotalPacks) : DisplayProgress + 1.0f;
					SlowTask.EnterProgressFrame(FMath::Max(0.0f, Target - DisplayProgress), FText::Format(FText::FromString(TEXT("正在生成材质包 {0}/{1}：{2}")), FText::AsNumber(LastPackIndex), FText::AsNumber(TotalPacks), FText::FromString(NameText)));
					DisplayProgress = Target;
				}
			}
		}
		FPlatformProcess::Sleep(0.1f);
	}

	FullOutput += FPlatformProcess::ReadPipe(ReadPipe);
	int32 ReturnCode = 0;
	FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
	FPlatformProcess::CloseProc(ProcHandle);
	FPlatformProcess::ClosePipe(ReadPipe, nullptr);
	SlowTask.EnterProgressFrame(FMath::Max(0.0f, 100.0f - DisplayProgress), FText::FromString(TEXT("写入扫描结果...")));

	if (ReturnCode != 0)
	{
		OutError = FText::Format(FText::FromString(TEXT("外部项目扫描失败，返回码：{0}\n\n{1}")), FText::AsNumber(ReturnCode), FText::FromString(FullOutput));
		return false;
	}
	if (!FPaths::FileExists(OutOutputPath))
	{
		OutError = FText::FromString(TEXT("扫描进程已结束，但没有生成结果文件。"));
		return false;
	}
	return true;
}

FString SMaterialVaultWindow::ResolveVaultBaseRoot()
{
	const FString ConfiguredRoot = GetDefault<UMaterialVaultSettings>()->ExternalVaultRoot.Path;
	FString BaseRoot = ConfiguredRoot.IsEmpty()
		? FPaths::Combine(FPlatformProcess::UserDir(), TEXT("MaterialVault"))
		: ConfiguredRoot;
	BaseRoot = FPaths::ConvertRelativePathToFull(BaseRoot);
	FPaths::NormalizeDirectoryName(BaseRoot);

	if (IsVaultLibraryDirectory(BaseRoot) && !HasChildVaultLibraries(BaseRoot))
	{
		BaseRoot = FPaths::GetPath(BaseRoot);
		FPaths::NormalizeDirectoryName(BaseRoot);
	}

	return BaseRoot;
}

FString SMaterialVaultWindow::ResolveVaultRoot()
{
	const FString BaseRoot = ResolveVaultBaseRoot();
	const FString ActiveLibraryName = GetDefault<UMaterialVaultSettings>()->ActiveLibraryName.IsEmpty()
		? TEXT("默认材质库")
		: GetDefault<UMaterialVaultSettings>()->ActiveLibraryName;
	return FPaths::Combine(BaseRoot, ActiveLibraryName);
}

bool SMaterialVaultWindow::IsVaultLibraryDirectory(const FString& Directory)
{
	if (Directory.IsEmpty() || !FPaths::DirectoryExists(Directory))
	{
		return false;
	}

	const FString DirectoryName = FPaths::GetCleanFilename(Directory);
	const bool bLegacyNamedLibrary =
		DirectoryName.Contains(TEXT("材质库")) &&
		!DirectoryName.Contains(TEXT("backup"), ESearchCase::IgnoreCase) &&
		!DirectoryName.Contains(TEXT("备份"));
	if (bLegacyNamedLibrary)
	{
		return true;
	}

	return FPaths::FileExists(FPaths::Combine(Directory, TEXT("MaterialVaultLibrary.json"))) ||
		FPaths::DirectoryExists(FPaths::Combine(Directory, TEXT("Packs"))) ||
		FPaths::DirectoryExists(FPaths::Combine(Directory, TEXT("Thumbnails"))) ||
		FPaths::FileExists(FPaths::Combine(Directory, TEXT("Index.json")));
}

bool SMaterialVaultWindow::HasChildVaultLibraries(const FString& Directory)
{
	if (Directory.IsEmpty() || !FPaths::DirectoryExists(Directory))
	{
		return false;
	}

	TArray<FString> Children;
	IFileManager::Get().FindFiles(Children, *FPaths::Combine(Directory, TEXT("*")), false, true);
	for (const FString& Child : Children)
	{
		if (IsVaultLibraryDirectory(FPaths::Combine(Directory, Child)))
		{
			return true;
		}
	}

	return false;
}

bool SMaterialVaultWindow::EnsureVaultLayout(const FString& VaultRoot, FText& OutError)
{
	if (VaultRoot.IsEmpty())
	{
		OutError = FText::FromString(TEXT("材质库位置为空。"));
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.MakeDirectory(*VaultRoot, true) ||
		!FileManager.MakeDirectory(*FPaths::Combine(VaultRoot, TEXT("Assets")), true) ||
		!FileManager.MakeDirectory(*FPaths::Combine(VaultRoot, TEXT("Packs")), true) ||
		!FileManager.MakeDirectory(*FPaths::Combine(VaultRoot, TEXT("Manifests")), true) ||
		!FileManager.MakeDirectory(*FPaths::Combine(VaultRoot, TEXT("ExternalScans")), true) ||
		!FileManager.MakeDirectory(*FPaths::Combine(VaultRoot, TEXT("Thumbnails")), true))
	{
		OutError = FText::Format(FText::FromString(TEXT("无法创建材质库目录：\n{0}")), FText::FromString(VaultRoot));
		return false;
	}

	const FString LibraryFile = FPaths::Combine(VaultRoot, TEXT("MaterialVaultLibrary.json"));
	if (!FPaths::FileExists(LibraryFile))
	{
		const FString LibraryId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		FString Json;
		Json += TEXT("{\n");
		Json += TEXT("  \"schemaVersion\": 1,\n");
		Json += FString::Printf(TEXT("  \"libraryId\": \"%s\",\n"), *LibraryId);
		Json += FString::Printf(TEXT("  \"displayName\": \"%s\",\n"), *JsonEscape(FPaths::GetCleanFilename(VaultRoot)));
		Json += TEXT("  \"folders\": {\n");
		Json += TEXT("    \"assets\": \"Assets\",\n");
		Json += TEXT("    \"packs\": \"Packs\",\n");
		Json += TEXT("    \"manifests\": \"Manifests\",\n");
		Json += TEXT("    \"externalScans\": \"ExternalScans\",\n");
		Json += TEXT("    \"thumbnails\": \"Thumbnails\"\n");
		Json += TEXT("  },\n");
		Json += TEXT("  \"portable\": true\n");
		Json += TEXT("}\n");

		if (!FFileHelper::SaveStringToFile(Json, *LibraryFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FText::Format(FText::FromString(TEXT("无法写入材质库配置文件：\n{0}")), FText::FromString(LibraryFile));
			return false;
		}
	}

	return true;
}

bool SMaterialVaultWindow::RebuildPackIndex(const FString& VaultRoot, FString& OutIndexPath, int32& OutPackCount, FText& OutError)
{
	OutPackCount = 0;
	if (!EnsureVaultLayout(VaultRoot, OutError))
	{
		return false;
	}

	const FString PacksRoot = FPaths::Combine(VaultRoot, TEXT("Packs"));
	TArray<FString> PackFiles;
	IFileManager::Get().FindFilesRecursive(PackFiles, *PacksRoot, TEXT("*.mvpack"), true, false);
	PackFiles.Sort();

	OutIndexPath = FPaths::Combine(VaultRoot, TEXT("Index.json"));

	FString Json;
	Json += TEXT("{\n");
	Json += TEXT("  \"schemaVersion\": 1,\n");
	Json += FString::Printf(TEXT("  \"generatedAt\": \"%s\",\n"), *FDateTime::UtcNow().ToIso8601());
	Json += TEXT("  \"packsRoot\": \"Packs\",\n");
	Json += FString::Printf(TEXT("  \"packCount\": %d,\n"), PackFiles.Num());
	Json += TEXT("  \"packs\": [\n");

	for (int32 Index = 0; Index < PackFiles.Num(); ++Index)
	{
		const FString& PackFile = PackFiles[Index];
		const FString RelativePath = MakeVaultRelativePath(PackFile, VaultRoot);
		const FString Category = NormalizeCategoryPath(DetectPackCategory(PackFile, VaultRoot));
		const FString PackId = FPaths::GetBaseFilename(PackFile);
		const int64 FileSize = IFileManager::Get().FileSize(*PackFile);
		const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*PackFile);
		FPackedMeta Meta;
		ExtractPackMeta(PackFile, PackId, VaultRoot, Meta);
		FMaterialVaultItem ManifestItem;
		const bool bHasManifestItem = ExtractPackItemFromManifest(PackFile, ManifestItem);
		auto BuildIndexEntryJson = [PackFile, PackId](FMaterialVaultAssetEntry Entry, const FString& Indent)
		{
			if (!Entry.ThumbnailPath.IsEmpty())
			{
				FString CachedThumbnailPath;
				if (ExtractPackStoredThumbnail(PackFile, PackId, Entry.ThumbnailPath, CachedThumbnailPath))
				{
					Entry.ThumbnailPath = CachedThumbnailPath;
				}
				else
				{
					Entry.ThumbnailPath.Reset();
				}
			}

			FString EntryJson;
			EntryJson += Indent + TEXT("{\n");
			EntryJson += Indent + FString::Printf(TEXT("  \"role\": \"%s\",\n"), *RoleToString(Entry.Role));
			EntryJson += Indent + FString::Printf(TEXT("  \"assetClass\": \"%s\",\n"), *JsonEscape(Entry.AssetClass));
			EntryJson += Indent + FString::Printf(TEXT("  \"originalObjectPath\": \"%s\",\n"), *JsonEscape(Entry.OriginalObjectPath.ToString()));
			EntryJson += Indent + FString::Printf(TEXT("  \"originalPackageName\": \"%s\",\n"), *JsonEscape(Entry.OriginalPackageName));
			EntryJson += Indent + FString::Printf(TEXT("  \"plannedPackageName\": \"%s\",\n"), *JsonEscape(Entry.PlannedPackageName));
			EntryJson += Indent + FString::Printf(TEXT("  \"plannedObjectPath\": \"%s\",\n"), *JsonEscape(Entry.PlannedObjectPath));
			EntryJson += Indent + FString::Printf(TEXT("  \"thumbnailPath\": \"%s\"\n"), *JsonEscape(Entry.ThumbnailPath));
			EntryJson += Indent + TEXT("}");
			return EntryJson;
		};

		Json += TEXT("    {\n");
		Json += FString::Printf(TEXT("      \"id\": \"%s\",\n"), *JsonEscape(PackId));
		Json += FString::Printf(TEXT("      \"displayName\": \"%s\",\n"), *JsonEscape(FPaths::GetBaseFilename(PackFile)));
		Json += FString::Printf(TEXT("      \"category\": \"%s\",\n"), *JsonEscape(Category));
		Json += FString::Printf(TEXT("      \"relativePath\": \"%s\",\n"), *JsonEscape(RelativePath));
		Json += FString::Printf(TEXT("      \"rootObjectPath\": \"%s\",\n"), *JsonEscape(Meta.RootObjectPath));
		Json += FString::Printf(TEXT("      \"rootAssetClass\": \"%s\",\n"), *JsonEscape(Meta.RootAssetClass));
		Json += FString::Printf(TEXT("      \"thumbnailPath\": \"%s\",\n"), *JsonEscape(Meta.ThumbnailPath));
		Json += FString::Printf(TEXT("      \"sizeBytes\": %lld,\n"), FileSize);
		Json += FString::Printf(TEXT("      \"modifiedAt\": \"%s\",\n"), *Timestamp.ToIso8601());
		Json += TEXT("      \"family\": [\n");
		if (bHasManifestItem)
		{
			TArray<FMaterialVaultAssetEntry> FamilyEntries;
			FamilyEntries.Add(ManifestItem.RootAsset);
			for (const FMaterialVaultAssetEntry& Dependency : ManifestItem.Dependencies)
			{
				if (Dependency.Role == EMaterialVaultAssetRole::Material || Dependency.Role == EMaterialVaultAssetRole::RootMaterial || Dependency.Role == EMaterialVaultAssetRole::MaterialInstance)
				{
					FamilyEntries.Add(Dependency);
				}
			}
			for (int32 FamilyIndex = 0; FamilyIndex < FamilyEntries.Num(); ++FamilyIndex)
			{
				Json += BuildIndexEntryJson(FamilyEntries[FamilyIndex], TEXT("        "));
				Json += FamilyIndex + 1 < FamilyEntries.Num() ? TEXT(",\n") : TEXT("\n");
			}
		}
		Json += TEXT("      ]\n");
		Json += Index + 1 < PackFiles.Num() ? TEXT("    },\n") : TEXT("    }\n");
	}

	Json += TEXT("  ]\n");
	Json += TEXT("}\n");

	if (!FFileHelper::SaveStringToFile(Json, *OutIndexPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FText::Format(FText::FromString(TEXT("无法写入材质包索引：\n{0}")), FText::FromString(OutIndexPath));
		return false;
	}

	OutPackCount = PackFiles.Num();
	return true;
}

FString SMaterialVaultWindow::DetectPackCategory(const FString& PackFilePath, const FString& VaultRoot)
{
	FString RelativePath = MakeVaultRelativePath(PackFilePath, VaultRoot);
	RelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));

	const FString Prefix = TEXT("Packs/");
	if (!RelativePath.StartsWith(Prefix))
	{
		const FString EmbeddedPrefix = TEXT("/Packs/");
		int32 EmbeddedPacksIndex = INDEX_NONE;
		if (RelativePath.FindChar(TEXT('/'), EmbeddedPacksIndex) &&
			RelativePath.Find(EmbeddedPrefix, ESearchCase::IgnoreCase, ESearchDir::FromStart, EmbeddedPacksIndex) != INDEX_NONE)
		{
			RelativePath = RelativePath.RightChop(RelativePath.Find(EmbeddedPrefix, ESearchCase::IgnoreCase) + EmbeddedPrefix.Len());
		}
		else
		{
			return TEXT("未分类");
		}
	}
	else
	{
		RelativePath.RightChopInline(Prefix.Len());
	}

	FString UnderPacks = RelativePath;
	const FString FileName = FPaths::GetCleanFilename(UnderPacks);
	if (!FileName.IsEmpty())
	{
		UnderPacks.RemoveFromEnd(FileName);
		UnderPacks.RemoveFromEnd(TEXT("/"));
		UnderPacks.RemoveFromEnd(TEXT("\\"));
	}

	return NormalizeCategoryPath(UnderPacks);
}

FString SMaterialVaultWindow::NormalizeCategoryPath(FString CategoryPath)
{
	CategoryPath.TrimStartAndEndInline();
	CategoryPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (CategoryPath.StartsWith(TEXT("/")))
	{
		CategoryPath.RightChopInline(1);
	}
	while (CategoryPath.EndsWith(TEXT("/")))
	{
		CategoryPath.LeftChopInline(1);
	}

	if (CategoryPath.IsEmpty() || CategoryPath == TEXT("Uncategorized"))
	{
		return TEXT("未分类");
	}

	if (CategoryPath.Equals(TEXT("Uncategorized"), ESearchCase::IgnoreCase))
	{
		return TEXT("未分类");
	}

	const FString PacksPrefix = TEXT("Packs/");
	if (CategoryPath == TEXT("Packs"))
	{
		return TEXT("未分类");
	}
	if (CategoryPath.StartsWith(PacksPrefix, ESearchCase::IgnoreCase))
	{
		CategoryPath.RightChopInline(PacksPrefix.Len());
	}

	const FString EmbeddedPacks = TEXT("/Packs/");
	const int32 PacksIndex = CategoryPath.Find(EmbeddedPacks, ESearchCase::IgnoreCase);
	if (PacksIndex != INDEX_NONE)
	{
		CategoryPath = CategoryPath.RightChop(PacksIndex + EmbeddedPacks.Len());
	}

	CategoryPath.TrimStartAndEndInline();
	CategoryPath.RemoveFromStart(TEXT("/"));
	CategoryPath.RemoveFromEnd(TEXT("/"));
	if (CategoryPath.Equals(TEXT("Uncategorized"), ESearchCase::IgnoreCase))
	{
		return TEXT("未分类");
	}
	if (CategoryPath == TEXT("布艺"))
	{
		return TEXT("布料");
	}
	if (CategoryPath.StartsWith(TEXT("布艺/")))
	{
		CategoryPath = TEXT("布料/") + CategoryPath.RightChop(3);
	}
	return CategoryPath.IsEmpty() ? TEXT("未分类") : CategoryPath;
}

FString SMaterialVaultWindow::InferCategoryFromName(const FString& Name)
{
	return FMaterialVaultScanner::InferCategory(Name);
}

FString SMaterialVaultWindow::InferCategoryFromItem(const FMaterialVaultItem& Item)
{
	auto TryInfer = [](const FString& Text)
	{
		const FString Category = NormalizeCategoryPath(InferCategoryFromName(Text));
		return Category == TEXT("未分类") ? FString() : Category;
	};

	TArray<FString> Candidates;
	Candidates.Add(Item.DisplayName);
	Candidates.Add(Item.Id);
	Candidates.Add(Item.RootAsset.OriginalPackageName);
	Candidates.Add(Item.RootAsset.OriginalObjectPath.ToString());
	Candidates.Add(Item.RootAsset.PlannedPackageName);
	Candidates.Add(Item.RootAsset.PlannedObjectPath);

	for (const FMaterialVaultAssetEntry& Entry : Item.Dependencies)
	{
		if (Entry.Role != EMaterialVaultAssetRole::Material &&
			Entry.Role != EMaterialVaultAssetRole::RootMaterial &&
			Entry.Role != EMaterialVaultAssetRole::MaterialInstance)
		{
			continue;
		}
		Candidates.Add(Entry.OriginalPackageName);
		Candidates.Add(Entry.OriginalObjectPath.ToString());
		Candidates.Add(Entry.PlannedPackageName);
		Candidates.Add(Entry.PlannedObjectPath);
	}

	for (const FString& Candidate : Candidates)
	{
		if (Candidate.IsEmpty())
		{
			continue;
		}
		const FString Category = TryInfer(Candidate);
		if (!Category.IsEmpty())
		{
			return Category;
		}
	}

	return TEXT("未分类");
}

FString SMaterialVaultWindow::MakeVaultRelativePath(const FString& AbsolutePath, const FString& VaultRoot)
{
	FString RelativePath = FPaths::ConvertRelativePathToFull(AbsolutePath);
	FString AbsoluteRoot = FPaths::ConvertRelativePathToFull(VaultRoot);
	FPaths::NormalizeDirectoryName(AbsoluteRoot);
	FPaths::NormalizeFilename(RelativePath);

	if (FPaths::MakePathRelativeTo(RelativePath, *AbsoluteRoot))
	{
		return RelativePath;
	}

	return AbsolutePath;
}

FString SMaterialVaultWindow::RoleToString(EMaterialVaultAssetRole Role)
{
	switch (Role)
	{
	case EMaterialVaultAssetRole::RootMaterial:
		return TEXT("RootMaterial");
	case EMaterialVaultAssetRole::Material:
		return TEXT("Material");
	case EMaterialVaultAssetRole::MaterialInstance:
		return TEXT("MaterialInstance");
	case EMaterialVaultAssetRole::Texture:
		return TEXT("Texture");
	case EMaterialVaultAssetRole::MaterialFunction:
		return TEXT("MaterialFunction");
	default:
		return TEXT("Other");
	}
}

FString SMaterialVaultWindow::JsonEscape(const FString& Value)
{
	FString Result;
	Result.Reserve(Value.Len());
	for (const TCHAR Character : Value)
	{
		switch (Character)
		{
		case TEXT('\\'):
			Result += TEXT("\\\\");
			break;
		case TEXT('"'):
			Result += TEXT("\\\"");
			break;
		case TEXT('\n'):
			Result += TEXT("\\n");
			break;
		case TEXT('\r'):
			Result += TEXT("\\r");
			break;
		case TEXT('\t'):
			Result += TEXT("\\t");
			break;
		default:
			if (Character < 0x20)
			{
				Result += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Character));
			}
			else
			{
				Result.AppendChar(Character);
			}
			break;
		}
	}
	return Result;
}
