#include "Widgets/SPBRTextureSuiteTab.h"
#include "Services/PBRTextureScanner.h"
#include "Services/PBRMaterialFactory.h"
#include "Services/PBRDataStore.h"
#include "Services/PBRMaterialInstanceFactory.h"
#include "Services/PBRMaterialTemplateManager.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PrimitiveComponent.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SWindow.h"
#include "Misc/MessageDialog.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "SPBRTextureSuiteTab"

const FString SPBRTextureSuiteTab::ConfigFileName = TEXT("TextureSuiteConfig.json");
const FString SPBRTextureSuiteTab::CacheFileName = TEXT("TextureSuiteLastList.json");

static bool IsPBRTextureFile(const FString& FilePath)
{
	const FString Ext = FPaths::GetExtension(FilePath).ToLower();
	return Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg") || Ext == TEXT("tif") ||
		Ext == TEXT("tiff") || Ext == TEXT("exr") || Ext == TEXT("hdr") || Ext == TEXT("tga") ||
		Ext == TEXT("webp") || Ext == TEXT("bmp");
}

static void AddPBRManualFile(TArray<FString>& Files, const FString& FilePath)
{
	if (!FilePath.IsEmpty() && FPaths::FileExists(FilePath))
	{
		Files.AddUnique(FilePath);
	}
}

static FPBRMaterialCreateOptions BuildTextureSuiteExistingOptions(const FPBRMaterialSet& Set, const FPBRMaterialFactory::FCreateSettings& Settings)
{
	FPBRMaterialCreateOptions Options;
	Options.PackageRoot = Settings.PackagePath;
	Options.MaterialInstancePrefix = Settings.MaterialPrefix.StartsWith(TEXT("M_"))
		? Settings.MaterialPrefix.Replace(TEXT("M_"), TEXT("MI_"))
		: Settings.MaterialPrefix;
	Options.MaterialType = FPBRMaterialFactory::ResolveMaterialTypeForSet(Set, Settings.MaterialTypeMode);
	Options.bCreateIsolatedMaterialFolder = true;
	Options.NormalPreference = Settings.NormalPreference;
	return Options;
}

DECLARE_DELEGATE_TwoParams(FPBROnBoxSelectRange, const FVector2D&, const FVector2D&);

class SPBRBoxSelectListOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRBoxSelectListOverlay) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(bool, BoxSelectEnabled)
		SLATE_EVENT(FPBROnBoxSelectRange, OnBoxSelectRange)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		BoxSelectEnabled = InArgs._BoxSelectEnabled;
		OnBoxSelectRange = InArgs._OnBoxSelectRange;
		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!BoxSelectEnabled.Get(false) || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		bDragging = true;
		DragStart = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		DragEnd = DragStart;
		ScreenDragStart = MouseEvent.GetScreenSpacePosition();
		ScreenDragEnd = ScreenDragStart;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!bDragging)
		{
			return FReply::Unhandled();
		}

		DragEnd = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		ScreenDragEnd = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!bDragging || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		bDragging = false;
		DragEnd = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		ScreenDragEnd = MouseEvent.GetScreenSpacePosition();
		if (FMath::Abs(DragEnd.Y - DragStart.Y) > 4.0f && OnBoxSelectRange.IsBound())
		{
			OnBoxSelectRange.Execute(ScreenDragStart, ScreenDragEnd);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		bDragging = false;
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		const int32 ContentLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		if (bDragging)
		{
			const FVector2D TopLeft(0.0f, FMath::Min(DragStart.Y, DragEnd.Y));
			const FVector2D Size(AllottedGeometry.GetLocalSize().X, FMath::Abs(DragEnd.Y - DragStart.Y));
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				ContentLayer + 1,
				AllottedGeometry.ToPaintGeometry(TopLeft, Size),
				FAppStyle::GetBrush("FocusRectangle"),
				ESlateDrawEffect::None,
				FLinearColor(0.2f, 0.55f, 1.0f, 0.35f));
			return ContentLayer + 1;
		}
		return ContentLayer;
	}

private:
	TAttribute<bool> BoxSelectEnabled;
	FPBROnBoxSelectRange OnBoxSelectRange;
	bool bDragging = false;
	FVector2D DragStart = FVector2D::ZeroVector;
	FVector2D DragEnd = FVector2D::ZeroVector;
	FVector2D ScreenDragStart = FVector2D::ZeroVector;
	FVector2D ScreenDragEnd = FVector2D::ZeroVector;
};

void SPBRTextureSuiteTab::Construct(const FArguments& InArgs)
{
	OnCompactModeChanged = InArgs._OnCompactModeChanged;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	MaterialTypeOptions = {
		MakeShared<FString>(TEXT("自动")),
		MakeShared<FString>(TEXT("标准")),
		MakeShared<FString>(TEXT("木材")),
		MakeShared<FString>(TEXT("石材")),
		MakeShared<FString>(TEXT("瓷砖")),
		MakeShared<FString>(TEXT("布艺")),
		MakeShared<FString>(TEXT("皮革")),
		MakeShared<FString>(TEXT("塑料")),
		MakeShared<FString>(TEXT("金属")),
		MakeShared<FString>(TEXT("半透明")),
		MakeShared<FString>(TEXT("玻璃")),
		MakeShared<FString>(TEXT("水")),
		MakeShared<FString>(TEXT("自发光"))
	};
	SelectedMaterialTypeOption = MaterialTypeOptions[0];
	SelectedMaterialTypeMode = *SelectedMaterialTypeOption;
	NormalModeOptions = {
		MakeShared<FString>(TEXT("自动")),
		MakeShared<FString>(TEXT("DirectX")),
		MakeShared<FString>(TEXT("OpenGL"))
	};
	SelectedNormalModeOption = NormalModeOptions[0];
	SelectedNormalMode = *SelectedNormalModeOption;
	PBRChannelOptions.Reset();
	for (const FString& Channel : FPBRTextureScanner::ChannelDisplayOrder)
	{
		if (Channel != TEXT("Unknown"))
		{
			PBRChannelOptions.Add(MakeShared<FString>(Channel));
		}
	}
	PBRChannelOptions.Add(MakeShared<FString>(TEXT("不使用")));

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			// -- Title ----------------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 8, 8, 4)
			[
				SNew(STextBlock)
				.Visibility(this, &SPBRTextureSuiteTab::GetStandardControlsVisibility)
				.Text(LOCTEXT("SuiteTitle", "PBR 贴图套件 - 扫描文件夹、识别通道、创建材质"))
			]

			// -- Folder Row -----------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPBRTextureSuiteTab::GetStandardControlsVisibility)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("Folder", "文件夹:"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(FolderPathBox, SEditableTextBox)
					.HintText(LOCTEXT("FolderHint", "选择包含 PBR 贴图套件的文件夹..."))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Browse", "浏览..."))
					.OnClicked(this, &SPBRTextureSuiteTab::OnBrowseFolder)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenFolder", "打开"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnOpenFolder)
				]
			]

			// -- Scan Options ---------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPBRTextureSuiteTab::GetStandardControlsVisibility)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SAssignNew(RecursiveCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[ SNew(STextBlock).Text(LOCTEXT("Recursive", "递归扫描")) ]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SAssignNew(GroupByFolderCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[ SNew(STextBlock).Text(LOCTEXT("GroupByFolder", "按文件夹分组")) ]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Scan", "扫描文件夹"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.OnClicked(this, &SPBRTextureSuiteTab::OnScanFolder)
				]
			]

			// -- Material Options -----------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPBRTextureSuiteTab::GetStandardControlsVisibility)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("MaterialType", "母材质:"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SNew(SBox)
					.MinDesiredWidth(150)
					[
						SAssignNew(MaterialTypeComboBox, SComboBox<FStringOption>)
						.OptionsSource(&MaterialTypeOptions)
						.InitiallySelectedItem(SelectedMaterialTypeOption)
						.OnGenerateWidget(this, &SPBRTextureSuiteTab::GenerateMaterialTypeOption)
						.OnSelectionChanged(this, &SPBRTextureSuiteTab::OnMaterialTypeSelected)
						[
							SNew(STextBlock)
							.Text(this, &SPBRTextureSuiteTab::GetSelectedMaterialTypeText)
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 16, 0)
				[
					SAssignNew(AutoStandardCheck, SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bAutoStandardWhenChannelsUnused ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bAutoStandardWhenChannelsUnused = (NewState == ECheckBoxState::Checked);
						SavePersistedState();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("贴图位不够时自动转标准材质")))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("Prefix", "前缀:"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SAssignNew(PrefixBox, SEditableTextBox)
					.Text(FText::FromString(TEXT("M_PBR_")))
					.MinDesiredWidth(100)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("MaterialSlot", "槽位:"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SAssignNew(MaterialSlotBox, SEditableTextBox)
					.Text(FText::FromString(TEXT("0")))
					.MinDesiredWidth(48)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("Normal", "法线:"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SNew(SBox)
					.MinDesiredWidth(100)
					[
						SAssignNew(NormalModeComboBox, SComboBox<FStringOption>)
						.OptionsSource(&NormalModeOptions)
						.InitiallySelectedItem(SelectedNormalModeOption)
						.OnGenerateWidget(this, &SPBRTextureSuiteTab::GenerateNormalModeOption)
						.OnSelectionChanged(this, &SPBRTextureSuiteTab::OnNormalModeSelected)
						[
							SNew(STextBlock)
							.Text(this, &SPBRTextureSuiteTab::GetSelectedNormalModeText)
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 0, 8, 4)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPBRTextureSuiteTab::GetStandardControlsVisibility)
				+ SHorizontalBox::Slot()
				.FillWidth(0.42f)
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SAssignNew(CreateProgressBar, SProgressBar)
					.Percent_Lambda([this]() -> TOptional<float>
					{
						return CreateProgress;
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.58f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(CreateProgressText, STextBlock)
					.Text(LOCTEXT("CreateProgressReady", "创建进度：就绪"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 0, 8, 4)
			[
				SAssignNew(MaterialTypeHelpText, STextBlock)
				.Visibility(this, &SPBRTextureSuiteTab::GetStandardControlsVisibility)
				.Text(FText::FromString(GetMaterialTypeDescription(SelectedMaterialTypeMode)))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)))
			]

			// -- Create Buttons -------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPBRTextureSuiteTab::GetStandardControlsVisibility)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 10, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("CreateGroup", "创建:"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateSelected", "创建选中"))
					.OnClicked_Lambda([this]() { OnCreateMaterials(TEXT("selected")); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateChecked", "创建勾选"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
					.OnClicked_Lambda([this]() { OnCreateMaterials(TEXT("checked")); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 24, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateAll", "全部创建"))
					.OnClicked_Lambda([this]() { OnCreateMaterials(TEXT("all")); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 24, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyToSelection", "应用到选中对象"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.OnClicked(this, &SPBRTextureSuiteTab::OnApplySelectedMaterialToSelection)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 24, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ManualMapping", "手动通道"))
					.OnClicked_Lambda([this]()
					{
						OnManualMapping();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 24, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateAllParents", "一键创建母材质"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnCreateAllParentMaterials)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenCurrentParent", "定位母材质"))
					.ToolTipText(LOCTEXT("OpenCurrentParentTip", "在内容浏览器中定位当前母材质类型对应的母材质。自动模式会优先使用当前选中的套件判断类型。"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnOpenCurrentParentMaterial)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 24, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenCurrentExample", "定位示例材质"))
					.ToolTipText(LOCTEXT("OpenCurrentExampleTip", "在内容浏览器中定位当前母材质类型对应的示例材质实例。"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnOpenCurrentExampleMaterial)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 10, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("CheckGroup", "勾选:"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CheckAll", "全选"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnCheckAll)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CheckNone", "全不选"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnCheckNone)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("InvertChecked", "反选"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnInvertChecked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ListViewMode", "列表"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnShowListView)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("GridViewMode", "网格"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnShowGridView)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CheckSelectedRows", "勾选列表选择"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnCheckListSelection)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SPBRTextureSuiteTab::IsBoxSelectModeChecked)
					.OnCheckStateChanged(this, &SPBRTextureSuiteTab::OnBoxSelectModeChanged)
					[
						SNew(STextBlock).Text(LOCTEXT("BoxSelectMode", "框选模式"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8, 0)
				[
					SAssignNew(SetCountText, STextBlock)
					.Text(LOCTEXT("NoSets", "0 个套件"))
					.Visibility(this, &SPBRTextureSuiteTab::GetStandardControlsVisibility)
				]
			]

			// -- Tree View ------------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
					.Text_Lambda([this]()
					{
						return bCompactMode ? LOCTEXT("StandardMode", "标准模式") : LOCTEXT("CompactMode", "精简模式");
					})
					.OnClicked(this, &SPBRTextureSuiteTab::OnToggleCompactMode)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ListViewModeCompact", "列表"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnShowListView)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("GridViewModeCompact", "网格"))
					.OnClicked(this, &SPBRTextureSuiteTab::OnShowGridView)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("一键删除未使用")))
					.OnClicked(this, &SPBRTextureSuiteTab::OnDeleteUnusedCreatedAssets)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8, 0)
				[
					SAssignNew(SetCountText, STextBlock)
					.Text(LOCTEXT("NoSets", "0 个套件"))
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(8, 4)
			[
				SNew(SBox)
				.MinDesiredHeight(300)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SPBRBoxSelectListOverlay)
						.Visibility(this, &SPBRTextureSuiteTab::GetListViewVisibility)
						.BoxSelectEnabled_Lambda([this]() { return bBoxSelectMode; })
						.OnBoxSelectRange_Lambda([this](const FVector2D& ScreenStart, const FVector2D& ScreenEnd)
						{
							OnBoxSelectRange(ScreenStart, ScreenEnd);
						})
						[
							SAssignNew(TreeView, SListView<TSharedPtr<FPBRMaterialSet>>)
							.ListItemsSource(&MaterialSets)
							.OnGenerateRow(this, &SPBRTextureSuiteTab::OnGenerateRow)
							.OnSelectionChanged(this, &SPBRTextureSuiteTab::OnSelectionChanged)
							.OnContextMenuOpening(this, &SPBRTextureSuiteTab::MakeMaterialSetContextMenu)
							.SelectionMode(ESelectionMode::Multi)
							.ClearSelectionOnClick(false)
							.HeaderRow(BuildHeaderRow())
						]
					]
					+ SOverlay::Slot()
					[
						SAssignNew(TileView, STileView<TSharedPtr<FPBRMaterialSet>>)
						.Visibility(this, &SPBRTextureSuiteTab::GetGridViewVisibility)
						.ListItemsSource(&MaterialSets)
						.OnGenerateTile(this, &SPBRTextureSuiteTab::OnGenerateTile)
						.OnSelectionChanged(this, &SPBRTextureSuiteTab::OnSelectionChanged)
						.OnContextMenuOpening(this, &SPBRTextureSuiteTab::MakeMaterialSetContextMenu)
						.SelectionMode(ESelectionMode::Multi)
						.ClearSelectionOnClick(false)
						.ItemWidth(100.0f)
						.ItemHeight(112.0f)
					]
				]
			]
		]
	];

	LoadPersistedState();
}

FReply SPBRTextureSuiteTab::OnBrowseFolder()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) return FReply::Handled();

	FString Folder;
	bool bOk = Desktop->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select PBR Texture Folder"),
		FolderPathBox->GetText().ToString(),
		Folder
	);
	if (bOk && !Folder.IsEmpty())
	{
		FolderPathBox->SetText(FText::FromString(Folder));
		SavePersistedState();
	}
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnOpenFolder()
{
	if (FolderPathBox.IsValid())
	{
		const FString Folder = FolderPathBox->GetText().ToString();
		if (!Folder.IsEmpty())
		{
			FPlatformProcess::ExploreFolder(*Folder);
		}
	}
	return FReply::Handled();
}

void SPBRTextureSuiteTab::LoadExternalLibraryFolderAndScan()
{
	TSharedPtr<FJsonObject> Config;
	if (FPBRDataStore::LoadConfig(Config) && Config.IsValid())
	{
		FString LastFolder;
		if (Config->TryGetStringField(TEXT("texture_suite_last_folder"), LastFolder) && FolderPathBox.IsValid())
		{
			FolderPathBox->SetText(FText::FromString(LastFolder));
		}
	}

	if (FolderPathBox.IsValid() && FPaths::DirectoryExists(FolderPathBox->GetText().ToString()))
	{
		OnScanFolder();
	}
}

void SPBRTextureSuiteTab::RefreshMaterialSetList()
{
	if (TreeView.IsValid())
	{
		TreeView->RequestListRefresh();
	}
	if (TileView.IsValid())
	{
		TileView->RequestListRefresh();
	}
	if (SetCountText.IsValid())
	{
		SetCountText->SetText(FText::AsNumber(MaterialSets.Num()));
	}
}

void SPBRTextureSuiteTab::LoadPersistedState()
{
	TSharedPtr<FJsonObject> Config;
	if (FPBRDataStore::LoadConfig(Config) && Config.IsValid())
	{
		FString LastFolder;
		if (Config->TryGetStringField(TEXT("texture_suite_last_folder"), LastFolder) && FolderPathBox.IsValid())
		{
			FolderPathBox->SetText(FText::FromString(LastFolder));
		}

		bool bRecursive = true;
		if (Config->TryGetBoolField(TEXT("texture_suite_recursive"), bRecursive) && RecursiveCheck.IsValid())
		{
			RecursiveCheck->SetIsChecked(bRecursive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
		}

		bool bGroupByFolder = true;
		if (Config->TryGetBoolField(TEXT("texture_suite_group_by_folder"), bGroupByFolder) && GroupByFolderCheck.IsValid())
		{
			GroupByFolderCheck->SetIsChecked(bGroupByFolder ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
		}

		FString Prefix;
		if (Config->TryGetStringField(TEXT("texture_suite_prefix"), Prefix) && PrefixBox.IsValid())
		{
			PrefixBox->SetText(FText::FromString(Prefix));
		}

		FString Slot;
		if (Config->TryGetStringField(TEXT("texture_suite_slot"), Slot) && MaterialSlotBox.IsValid())
		{
			MaterialSlotBox->SetText(FText::FromString(Slot));
		}

		bool bSavedGridViewMode = false;
		if (Config->TryGetBoolField(TEXT("texture_suite_grid_view"), bSavedGridViewMode))
		{
			bGridViewMode = bSavedGridViewMode;
		}

		bool bSavedAutoStandard = true;
		if (Config->TryGetBoolField(TEXT("texture_suite_auto_standard_when_channels_unused"), bSavedAutoStandard))
		{
			bAutoStandardWhenChannelsUnused = bSavedAutoStandard;
		}

		bool bSavedCompactMode = false;
		if (Config->TryGetBoolField(TEXT("texture_suite_compact_mode"), bSavedCompactMode))
		{
			bCompactMode = bSavedCompactMode;
			if (bCompactMode)
			{
				bGridViewMode = true;
			}
			if (OnCompactModeChanged.IsBound())
			{
				OnCompactModeChanged.Execute(bCompactMode);
			}
		}
	}

	TArray<FPBRMaterialSet> CachedSets;
	if (FPBRDataStore::LoadMaterialSets(CacheFileName, CachedSets))
	{
		MaterialSets.Empty();
		for (const FPBRMaterialSet& Set : CachedSets)
		{
			MaterialSets.Add(MakeShareable(new FPBRMaterialSet(Set)));
		}
		RefreshMaterialSetList();
	}
}

void SPBRTextureSuiteTab::SavePersistedState() const
{
	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		Config = MakeShareable(new FJsonObject);
	}

	Config->SetStringField(TEXT("texture_suite_last_folder"), FolderPathBox.IsValid() ? FolderPathBox->GetText().ToString() : FString());
	Config->SetBoolField(TEXT("texture_suite_recursive"), RecursiveCheck.IsValid() ? RecursiveCheck->IsChecked() : true);
	Config->SetBoolField(TEXT("texture_suite_group_by_folder"), GroupByFolderCheck.IsValid() ? GroupByFolderCheck->IsChecked() : true);
	Config->SetStringField(TEXT("texture_suite_prefix"), PrefixBox.IsValid() ? PrefixBox->GetText().ToString() : FString());
	Config->SetStringField(TEXT("texture_suite_slot"), MaterialSlotBox.IsValid() ? MaterialSlotBox->GetText().ToString() : FString());
	Config->SetBoolField(TEXT("texture_suite_grid_view"), bGridViewMode);
	Config->SetBoolField(TEXT("texture_suite_compact_mode"), bCompactMode);
	Config->SetBoolField(TEXT("texture_suite_auto_standard_when_channels_unused"), bAutoStandardWhenChannelsUnused);
	FPBRDataStore::SaveConfig(Config);

	TArray<FPBRMaterialSet> SetsToSave;
	for (const TSharedPtr<FPBRMaterialSet>& Set : MaterialSets)
	{
		if (Set.IsValid())
		{
			SetsToSave.Add(*Set);
		}
	}
	FPBRDataStore::SaveMaterialSets(CacheFileName, SetsToSave);
}

FReply SPBRTextureSuiteTab::OnScanFolder()
{
	FPBRTextureScanner::FScanSettings Settings;
	Settings.RootDir = FolderPathBox->GetText().ToString();
	Settings.bRecursive = RecursiveCheck->IsChecked();
	Settings.bGroupByFolder = GroupByFolderCheck->IsChecked();

	TArray<FPBRMaterialSet> Sets = FPBRTextureScanner::ScanPBRTextureSets(Settings);

	MaterialSets.Empty();
	for (const FPBRMaterialSet& S : Sets)
	{
		TSharedPtr<FPBRMaterialSet> NewSet = MakeShareable(new FPBRMaterialSet(S));

		FPBRMaterialFactory::FCreateSettings ExistingCreateSettings;
		if (PrefixBox.IsValid())
		{
			ExistingCreateSettings.MaterialPrefix = PrefixBox->GetText().ToString();
		}
		ExistingCreateSettings.MaterialTypeMode = SelectedMaterialTypeMode;
		ExistingCreateSettings.NormalPreference = SelectedNormalMode;
		FPBRMaterialCreateOptions ExistingOptions = BuildTextureSuiteExistingOptions(*NewSet, ExistingCreateSettings);

		FString ExistingPath;
		if (UMaterialInterface* ExistingMaterial = FPBRMaterialInstanceFactory::FindExistingMaterialInstance(*NewSet, ExistingOptions, ExistingPath))
		{
			NewSet->CreatedMaterial = ExistingMaterial;
			NewSet->Status = TEXT("使用已有材质实例");
		}

		MaterialSets.Add(NewSet);
	}

	SavePersistedState();

	RefreshMaterialSetList();

	if (SetCountText.IsValid())
	{
		SetCountText->SetText(FText::Format(LOCTEXT("SetCount", "{0} 个套件"), FText::AsNumber(MaterialSets.Num())));
	}
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnCheckAll()
{
	for (TSharedPtr<FPBRMaterialSet>& Set : MaterialSets)
	{
		if (Set.IsValid())
		{
			Set->bChecked = true;
		}
	}
	RefreshMaterialSetList();
	SavePersistedState();
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnCheckNone()
{
	for (TSharedPtr<FPBRMaterialSet>& Set : MaterialSets)
	{
		if (Set.IsValid())
		{
			Set->bChecked = false;
		}
	}
	RefreshMaterialSetList();
	SavePersistedState();
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnInvertChecked()
{
	for (TSharedPtr<FPBRMaterialSet>& Set : MaterialSets)
	{
		if (Set.IsValid())
		{
			Set->bChecked = !Set->bChecked;
		}
	}
	RefreshMaterialSetList();
	SavePersistedState();
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnShowListView()
{
	bGridViewMode = false;
	SavePersistedState();
	RefreshMaterialSetList();
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnShowGridView()
{
	bGridViewMode = true;
	SavePersistedState();
	RefreshMaterialSetList();
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnToggleCompactMode()
{
	bCompactMode = !bCompactMode;
	if (bCompactMode)
	{
		bGridViewMode = true;
	}
	if (OnCompactModeChanged.IsBound())
	{
		OnCompactModeChanged.Execute(bCompactMode);
	}
	SavePersistedState();
	RefreshMaterialSetList();
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::ToggleCompactModeFromGlobal()
{
	return OnToggleCompactMode();
}

void SPBRTextureSuiteTab::SetCompactModeFromGlobal(bool bInCompactMode)
{
	if (bCompactMode == bInCompactMode)
	{
		return;
	}
	bCompactMode = bInCompactMode;
	if (bCompactMode)
	{
		bGridViewMode = true;
	}
	if (OnCompactModeChanged.IsBound())
	{
		OnCompactModeChanged.Execute(bCompactMode);
	}
	SavePersistedState();
	RefreshMaterialSetList();
}

EVisibility SPBRTextureSuiteTab::GetStandardControlsVisibility() const
{
	return bCompactMode ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SPBRTextureSuiteTab::GetListViewVisibility() const
{
	return bGridViewMode ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SPBRTextureSuiteTab::GetGridViewVisibility() const
{
	return bGridViewMode ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SPBRTextureSuiteTab::OnCheckListSelection()
{
	if (!TreeView.IsValid())
	{
		return FReply::Handled();
	}

	TArray<TSharedPtr<FPBRMaterialSet>> SelectedItems = TreeView->GetSelectedItems();
	for (TSharedPtr<FPBRMaterialSet>& Set : MaterialSets)
	{
		if (Set.IsValid())
		{
			Set->bChecked = false;
		}
	}
	for (TSharedPtr<FPBRMaterialSet>& Set : SelectedItems)
	{
		if (Set.IsValid())
		{
			Set->bChecked = true;
		}
	}
	RefreshMaterialSetList();
	SavePersistedState();
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnApplySelectedMaterialToSelection()
{
	TSharedPtr<FPBRMaterialSet> SourceSet;
	UMaterialInterface* Material = GetMaterialForApply(SourceSet);
	if (!Material)
	{
		return FReply::Handled();
	}

	const int32 SlotIndex = GetTargetMaterialSlot();
	int32 AppliedCount = 0;

	if (GEditor)
	{
		USelection* ComponentSelection = GEditor->GetSelectedComponents();
		if (ComponentSelection)
		{
			for (FSelectionIterator It(*ComponentSelection); It; ++It)
			{
				if (UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(*It))
				{
					if (SlotIndex >= 0 && SlotIndex < Component->GetNumMaterials())
					{
						Component->Modify();
						Component->SetMaterial(SlotIndex, Material);
						AppliedCount++;
					}
				}
			}
		}

		if (AppliedCount == 0)
		{
			USelection* ActorSelection = GEditor->GetSelectedActors();
			if (ActorSelection)
			{
				for (FSelectionIterator It(*ActorSelection); It; ++It)
				{
					AActor* Actor = Cast<AActor>(*It);
					if (!Actor)
					{
						continue;
					}

					TArray<UPrimitiveComponent*> PrimitiveComponents;
					Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
					for (UPrimitiveComponent* Component : PrimitiveComponents)
					{
						if (Component && SlotIndex >= 0 && SlotIndex < Component->GetNumMaterials())
						{
							Component->Modify();
							Component->SetMaterial(SlotIndex, Material);
							AppliedCount++;
						}
					}
				}
			}
		}
	}

	if (SourceSet.IsValid())
	{
		SourceSet->Status = AppliedCount > 0
			? FString::Printf(TEXT("已应用到 %d 个槽位"), AppliedCount)
			: TEXT("未应用: 请先选择场景对象或检查槽位");
	}
	RefreshMaterialSetList();
	SavePersistedState();
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnCreateAllParentMaterials()
{
	TArray<FString> Messages;
	const int32 Count = FPBRMaterialTemplateManager::EnsureAllTemplateMaterials(Messages);
	const FString Summary = FString::Printf(TEXT("母材质: 已处理 %d 个"), Count);
	if (MaterialTypeHelpText.IsValid())
	{
		MaterialTypeHelpText->SetText(FText::FromString(Summary + TEXT("\n") + FString::Join(Messages, TEXT("\n"))));
	}
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnOpenCurrentParentMaterial()
{
	const EPBRMaterialType MaterialType = ResolveCurrentTemplateType();
	FString Message;
	FPBRMaterialTemplateManager::EnsureTemplateMaterial(MaterialType, Message);
	SyncBrowserToAsset(FPBRMaterialTemplateManager::GetTemplatePackagePath(MaterialType));
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnOpenCurrentExampleMaterial()
{
	const EPBRMaterialType MaterialType = ResolveCurrentTemplateType();
	FString Message;
	FPBRMaterialTemplateManager::EnsureExampleMaterialInstance(MaterialType, Message);
	SyncBrowserToAsset(FPBRMaterialTemplateManager::GetExampleMaterialInstancePackagePath(MaterialType));
	return FReply::Handled();
}

FReply SPBRTextureSuiteTab::OnCreateSpecialMaterials()
{
	TArray<FString> Messages;
	const int32 Count = FPBRMaterialTemplateManager::EnsureSpecialTemplateMaterials(Messages);
	const FString Summary = FString::Printf(TEXT("特殊材质: 已处理 %d 个"), Count);
	if (MaterialTypeHelpText.IsValid())
	{
		MaterialTypeHelpText->SetText(FText::FromString(Summary + TEXT("\n") + FString::Join(Messages, TEXT("\n"))));
	}
	return FReply::Handled();
}

static bool IsAssetUnderPath(const FString& AssetPath, const FString& RootPath)
{
	return AssetPath.StartsWith(RootPath);
}

static bool IsManagedPBRAssetClass(const FAssetData& Asset)
{
	const FName ClassName = Asset.AssetClassPath.GetAssetName();
	return ClassName == TEXT("MaterialInstanceConstant") ||
		ClassName == TEXT("Material") ||
		ClassName == TEXT("MaterialInstance") ||
		ClassName == TEXT("MaterialFunction") ||
		ClassName == TEXT("Texture2D");
}

static bool IsPBRStudioGeneratedAsset(const FAssetData& Asset)
{
	if (!IsManagedPBRAssetClass(Asset))
	{
		return false;
	}

	const FString PackageName = Asset.PackageName.ToString();
	const FString AssetName = Asset.AssetName.ToString();

	if (PackageName.StartsWith(TEXT("/Game/PBRStudio/SceneReplaced/")))
	{
		return true;
	}

	if (PackageName.StartsWith(TEXT("/Game/PBRStudio/Substrate/")))
	{
		return AssetName.StartsWith(TEXT("M_PBR_Substrate_")) ||
			AssetName.StartsWith(TEXT("MI_示例_Substrate_")) ||
			AssetName.StartsWith(TEXT("MF_PBRStudio_Substrate_")) ||
			AssetName.StartsWith(TEXT("T_"));
	}

	if (PackageName.StartsWith(TEXT("/Game/PBRStudio/Functions/")))
	{
		return AssetName.StartsWith(TEXT("MF_PBRStudio_"));
	}

	if (PackageName.StartsWith(TEXT("/Game/PBRStudio/Templates/")))
	{
		return AssetName.StartsWith(TEXT("M_PBR_")) ||
			AssetName.StartsWith(TEXT("MI_示例_")) ||
			AssetName.StartsWith(TEXT("T_Demo_"));
	}

	if (PackageName.StartsWith(TEXT("/Game/PBRStudio/SpecialMaterials/Examples/")))
	{
		return AssetName.StartsWith(TEXT("MI_"));
	}

	if (PackageName.StartsWith(TEXT("/Game/PBRStudio/SpecialMaterials/")))
	{
		return AssetName.StartsWith(TEXT("SM_")) || AssetName.StartsWith(TEXT("MI_")) || AssetName.StartsWith(TEXT("T_"));
	}

	if (PackageName.StartsWith(TEXT("/Game/Materials/PBR/")))
	{
		return AssetName.StartsWith(TEXT("M_PBR_")) ||
			AssetName.StartsWith(TEXT("MI_")) ||
			AssetName.StartsWith(TEXT("T_"));
	}

	return false;
}

static bool IsPBRStudioExampleAssetPackage(const FString& PackageName)
{
	return PackageName.StartsWith(TEXT("/Game/PBRStudio/SpecialMaterials/Examples/")) ||
		PackageName.StartsWith(TEXT("/Game/PBRStudio/Templates/Examples/")) ||
		PackageName.StartsWith(TEXT("/Game/PBRStudio/Substrate/Examples/"));
}

static void CollectMaterialReferencesFromWorld(UWorld* World, TSet<FString>& OutReferencedMaterialPaths)
{
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			if (!Component)
			{
				continue;
			}

			for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
			{
				if (UMaterialInterface* Material = Component->GetMaterial(Index))
				{
					OutReferencedMaterialPaths.Add(Material->GetPathName());
				}
			}
		}
	}
}

FReply SPBRTextureSuiteTab::OnDeleteUnusedCreatedAssets()
{
	const EAppReturnType::Type Confirm = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(TEXT("只会删除本插件生成的 UE 内容资源。当前关卡正在使用的材质、母材质和相关贴图会保留；没有用到的插件生成材质、示例材质、母材质和贴图会删除。手动放入同目录但不符合插件生成规则的资源不会删除，也不会删除电脑上的下载源文件。\n\n继续吗？")));
	if (Confirm != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const TArray<FString> ManagedRoots = {
		TEXT("/Game/Materials/PBR"),
		TEXT("/Game/PBRStudio/SceneReplaced"),
		TEXT("/Game/PBRStudio/Templates"),
		TEXT("/Game/PBRStudio/Functions"),
		TEXT("/Game/PBRStudio/SpecialMaterials"),
		TEXT("/Game/PBRStudio/Substrate")
	};

	TSet<FString> ReferencedMaterials;
	if (GEditor)
	{
		CollectMaterialReferencesFromWorld(GEditor->GetEditorWorldContext().World(), ReferencedMaterials);
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> ManagedAssets;
	TMap<FString, FAssetData> ManagedAssetsByPackage;
	for (const FString& Root : ManagedRoots)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*Root), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (!IsPBRStudioGeneratedAsset(Asset))
			{
				continue;
			}

			const FString PackageName = Asset.PackageName.ToString();
			ManagedAssets.Add(Asset);
			ManagedAssetsByPackage.Add(PackageName, Asset);
		}
	}

	FScopedSlowTask SlowTask(
		static_cast<float>(FMath::Max(ManagedAssets.Num() * 2, 1)),
		FText::FromString(TEXT("正在清理未使用的 PBRStudio 资源...")));
	SlowTask.MakeDialog(true);

	TSet<FString> PackagesToKeep;
	TArray<FString> PackagesToVisit;
	for (const FString& ReferencedMaterialPath : ReferencedMaterials)
	{
		const FString ReferencedPackage = FPackageName::ObjectPathToPackageName(ReferencedMaterialPath);
		if (ManagedAssetsByPackage.Contains(ReferencedPackage))
		{
			PackagesToKeep.Add(ReferencedPackage);
			PackagesToVisit.Add(ReferencedPackage);
		}
	}

	for (const FAssetData& Asset : ManagedAssets)
	{
		const FString PackageName = Asset.PackageName.ToString();
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(FName(*PackageName), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		for (const FName& Referencer : Referencers)
		{
			const FString ReferencerPackage = Referencer.ToString();
			if (ReferencerPackage == PackageName || ManagedAssetsByPackage.Contains(ReferencerPackage))
			{
				continue;
			}

			if (ReferencerPackage.StartsWith(TEXT("/Game/")) || ReferencerPackage.StartsWith(TEXT("/Plugin/")))
			{
				PackagesToKeep.Add(PackageName);
				PackagesToVisit.Add(PackageName);
				break;
			}
		}
	}

	while (PackagesToVisit.Num() > 0)
	{
		if (SlowTask.ShouldCancel())
		{
			return FReply::Handled();
		}

		const FString PackageName = PackagesToVisit.Pop(EAllowShrinking::No);
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("正在检查材质依赖...")));

		TArray<FName> Dependencies;
		AssetRegistry.GetDependencies(FName(*PackageName), Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		for (const FName& Dependency : Dependencies)
		{
			const FString DependencyPackage = Dependency.ToString();
			if (IsPBRStudioExampleAssetPackage(DependencyPackage))
			{
				continue;
			}
			if (ManagedAssetsByPackage.Contains(DependencyPackage) && !PackagesToKeep.Contains(DependencyPackage))
			{
				PackagesToKeep.Add(DependencyPackage);
				PackagesToVisit.Add(DependencyPackage);
			}
		}
	}

	TSet<FString> DeletedPackages;
	TArray<FString> DeletedAssets;
	TArray<FString> DeletedFolders;
	TArray<FString> EmptyFolderCandidates;
	for (const FAssetData& Asset : ManagedAssets)
	{
		if (SlowTask.ShouldCancel())
		{
			return FReply::Handled();
		}

		const FString PackageName = Asset.PackageName.ToString();
		if (PackagesToKeep.Contains(PackageName))
		{
			continue;
		}

		const FString AssetPath = Asset.GetObjectPathString();
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FString::Printf(TEXT("正在删除：%s"), *Asset.AssetName.ToString())));
		EmptyFolderCandidates.Add(FPackageName::GetLongPackagePath(PackageName));
		if (UEditorAssetLibrary::DeleteAsset(AssetPath))
		{
			DeletedPackages.Add(PackageName);
			DeletedAssets.Add(AssetPath);
		}
	}

	EmptyFolderCandidates.Sort();
	TArray<FString> UniqueFolderCandidates;
	for (const FString& Folder : EmptyFolderCandidates)
	{
		if (UniqueFolderCandidates.Num() == 0 || UniqueFolderCandidates.Last() != Folder)
		{
			UniqueFolderCandidates.Add(Folder);
		}
	}
	EmptyFolderCandidates = UniqueFolderCandidates;
	for (int32 Index = EmptyFolderCandidates.Num() - 1; Index >= 0; --Index)
	{
		if (SlowTask.ShouldCancel())
		{
			return FReply::Handled();
		}

		const FString& Folder = EmptyFolderCandidates[Index];
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FString::Printf(TEXT("正在检查空文件夹：%s"), *Folder)));
		TArray<FAssetData> RemainingAssets;
		AssetRegistry.GetAssetsByPath(FName(*Folder), RemainingAssets, true);
		if (RemainingAssets.Num() == 0 && UEditorAssetLibrary::DeleteDirectory(Folder))
		{
			DeletedFolders.Add(Folder);
		}
	}

	if (MaterialTypeHelpText.IsValid())
	{
		MaterialTypeHelpText->SetText(FText::FromString(FString::Printf(TEXT("已删除 %d 个未使用资源，%d 个空文件夹。"), DeletedAssets.Num(), DeletedFolders.Num())));
	}
	RefreshMaterialSetList();
	return FReply::Handled();
}

ECheckBoxState SPBRTextureSuiteTab::IsBoxSelectModeChecked() const
{
	return bBoxSelectMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SPBRTextureSuiteTab::OnBoxSelectModeChanged(ECheckBoxState NewState)
{
	bBoxSelectMode = (NewState == ECheckBoxState::Checked);
}

void SPBRTextureSuiteTab::OnCreateMaterials(const FString& Scope)
{
	if (MaterialSets.Num() == 0) return;

	FPBRMaterialFactory::FCreateSettings Settings;
	Settings.MaterialTypeMode = SelectedMaterialTypeMode;
	if (PrefixBox.IsValid()) Settings.MaterialPrefix = PrefixBox->GetText().ToString();
	Settings.NormalPreference = SelectedNormalMode;

	int32 Created = 0;
	int32 ExistingCount = 0;
	int32 FailedCount = 0;
	TArray<TSharedPtr<FPBRMaterialSet>> ToCreate;

	if (Scope == TEXT("selected"))
	{
		TArray<TSharedPtr<FPBRMaterialSet>> Selected = TreeView->GetSelectedItems();
		ToCreate = Selected;
	}
	else if (Scope == TEXT("checked"))
	{
		for (const TSharedPtr<FPBRMaterialSet>& Set : MaterialSets)
		{
			if (Set.IsValid() && Set->bChecked)
			{
				ToCreate.Add(Set);
			}
		}
	}
	else
	{
		ToCreate = MaterialSets;
	}

	FScopedSlowTask SlowTask(
		static_cast<float>(FMath::Max(ToCreate.Num(), 1)),
		FText::FromString(TEXT("正在创建贴图套件材质...")));
	SlowTask.MakeDialog(true);
	CreateProgress = 0.0f;
	if (CreateProgressText.IsValid())
	{
		CreateProgressText->SetText(FText::FromString(FString::Printf(TEXT("创建进度：0 / %d"), ToCreate.Num())));
	}

	for (int32 Index = 0; Index < ToCreate.Num(); ++Index)
	{
		TSharedPtr<FPBRMaterialSet>& Set = ToCreate[Index];
		if (SlowTask.ShouldCancel())
		{
			break;
		}
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(Set.IsValid()
			? FString::Printf(TEXT("正在处理：%s"), *Set->Name)
			: TEXT("正在处理贴图套件...")));
		CreateProgress = ToCreate.Num() > 0 ? static_cast<float>(Index) / static_cast<float>(ToCreate.Num()) : 0.0f;
		if (CreateProgressText.IsValid())
		{
			CreateProgressText->SetText(FText::FromString(FString::Printf(TEXT("创建进度：%d / %d"), Index, ToCreate.Num())));
		}
		if (CreateProgressBar.IsValid())
		{
			CreateProgressBar->Invalidate(EInvalidateWidgetReason::Paint);
		}
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}

		if (Set.IsValid() && Set->Channels.Num() > 0)
		{
			FString Notes;
			FPBRMaterialFactory::FCreateSettings EffectiveSettings = Settings;
			const EPBRMaterialType ResolvedType = FPBRMaterialFactory::ResolveMaterialTypeForSet(*Set, Settings.MaterialTypeMode);
			TArray<FString> UnusedChannels;
			FPBRMaterialFactory::GetUnusedChannelsForMaterialType(*Set, ResolvedType, UnusedChannels);
			const bool bHasOpacityChannel = Set->Channels.Contains(FPBRChannels::Opacity.ToString());
			if (ResolvedType != EPBRMaterialType::Standard && UnusedChannels.Num() > 0 && bAutoStandardWhenChannelsUnused && !bHasOpacityChannel)
			{
				EffectiveSettings.MaterialTypeMode = TEXT("Standard");
			}
			if (ResolvedType != EPBRMaterialType::Standard && UnusedChannels.Num() > 0 && !bAutoStandardWhenChannelsUnused && !bHasOpacityChannel)
			{
				const FString Prompt = FString::Printf(
					TEXT("%s 当前母材质可能不会使用这些贴图：%s\n\n是否改用标准母材质，尽量把贴图全部用上？"),
					*Set->Name,
					*FString::Join(UnusedChannels, TEXT(", ")));
				const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Prompt));
				if (Choice == EAppReturnType::Yes)
				{
					EffectiveSettings.MaterialTypeMode = TEXT("Standard");
				}
			}

			FPBRMaterialCreateOptions ExistingOptions = BuildTextureSuiteExistingOptions(*Set, EffectiveSettings);
			FString ExistingPath;
			if (UMaterialInterface* ExistingMaterial = FPBRMaterialInstanceFactory::FindExistingMaterialInstance(*Set, ExistingOptions, ExistingPath))
			{
				Set->CreatedMaterial = ExistingMaterial;
				Set->Status = TEXT("已存在，直接使用已有材质实例");
				ExistingCount++;
				continue;
			}

			UMaterialInterface* Mat = FPBRMaterialFactory::CreateMaterialFromPBRSet(*Set, EffectiveSettings, Notes);
			if (Mat)
			{
				Set->CreatedMaterial = Mat;
				Set->Status = Notes.StartsWith(TEXT("使用已有"))
					? Notes
					: TEXT("已创建");
				Created++;
			}
			else
			{
				Set->Status = Notes.StartsWith(TEXT("重复"))
					? Notes
					: FString::Printf(TEXT("失败: %s"), *Notes);
				FailedCount++;
			}
		}
		else if (Set.IsValid())
		{
			Set->Status = TEXT("跳过: 没有可用贴图通道");
			FailedCount++;
		}
	}
	CreateProgress = 1.0f;
	if (CreateProgressText.IsValid())
	{
		CreateProgressText->SetText(FText::FromString(FString::Printf(TEXT("创建完成：新建 %d，已存在 %d，失败/跳过 %d"), Created, ExistingCount, FailedCount)));
	}
	if (CreateProgressBar.IsValid())
	{
		CreateProgressBar->Invalidate(EInvalidateWidgetReason::Paint);
	}

	RefreshMaterialSetList();
	SavePersistedState();
}

void SPBRTextureSuiteTab::OnManualMapping()
{
	const TArray<TSharedPtr<FPBRMaterialSet>> Selected = bGridViewMode && TileView.IsValid()
		? TileView->GetSelectedItems()
		: (TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<TSharedPtr<FPBRMaterialSet>>());
	if (Selected.Num() == 0 || !Selected[0].IsValid())
	{
		return;
	}
	OnManualMappingForSet(Selected[0]);
}

TSharedRef<SWidget> SPBRTextureSuiteTab::GeneratePBRChannelOption(FStringOption Option) const
{
	return SNew(STextBlock).Text(FText::FromString(Option.IsValid() ? *Option : FString()));
}

TSharedRef<ITableRow> SPBRTextureSuiteTab::GenerateManualTextureRow(TSharedPtr<FPBRManualTextureChannelItem> TextureItem, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FPBRManualTextureChannelItem>>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.14f).Padding(4)
			[
				SNew(SBox)
				.WidthOverride(48.0f)
				.HeightOverride(48.0f)
				[
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit)
					.StretchDirection(EStretchDirection::DownOnly)
					[
						SNew(SImage)
						.Image_Lambda([this, TextureItem]()
						{
							return TextureItem.IsValid() ? GetManualTextureBrush(TextureItem->FilePath) : FAppStyle::GetBrush("WhiteBrush");
						})
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.52f).Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TextureItem.IsValid() ? FPaths::GetCleanFilename(TextureItem->FilePath) : FString()))
				.ToolTipText(FText::FromString(TextureItem.IsValid() ? TextureItem->FilePath : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.34f).Padding(4)
			[
				SNew(SComboBox<FStringOption>)
				.OptionsSource(&PBRChannelOptions)
				.OnGenerateWidget(this, &SPBRTextureSuiteTab::GeneratePBRChannelOption)
				.OnSelectionChanged(this, &SPBRTextureSuiteTab::OnPBRChannelSelected, TextureItem)
				[
					SNew(STextBlock)
					.Text_Lambda([TextureItem]() { return FText::FromString(TextureItem.IsValid() ? TextureItem->Channel : FString()); })
				]
			]
		];
}

void SPBRTextureSuiteTab::OnPBRChannelSelected(FStringOption Option, ESelectInfo::Type SelectInfo, TSharedPtr<FPBRManualTextureChannelItem> TextureItem)
{
	if (Option.IsValid() && TextureItem.IsValid())
	{
		TextureItem->Channel = *Option;
	}
}

void SPBRTextureSuiteTab::ApplyManualTextureChannels(TSharedPtr<FPBRMaterialSet> Set)
{
	if (!Set.IsValid())
	{
		return;
	}

	Set->Channels.Reset();
	Set->Duplicates.Reset();
	Set->Unknown.Reset();
	Set->PreviewPath.Empty();

	for (const TSharedPtr<FPBRManualTextureChannelItem>& Row : ManualTextureRows)
	{
		if (!Row.IsValid() || Row->FilePath.IsEmpty() || Row->Channel == TEXT("不使用"))
		{
			continue;
		}
		if (Row->Channel == TEXT("Preview"))
		{
			if (Set->PreviewPath.IsEmpty())
			{
				Set->PreviewPath = Row->FilePath;
			}
			else
			{
				Set->Duplicates.Add(Row->FilePath);
			}
			continue;
		}
		if (Row->Channel == TEXT("Unknown"))
		{
			Set->Unknown.Add(Row->FilePath);
			continue;
		}
		if (Set->Channels.Contains(Row->Channel))
		{
			Set->Duplicates.Add(Set->Channels[Row->Channel]);
		}
		Set->Channels.Add(Row->Channel, Row->FilePath);
	}

	if (Set->PreviewPath.IsEmpty())
	{
		if (const FString* BaseColorPath = Set->Channels.Find(TEXT("BaseColor")))
		{
			Set->PreviewPath = *BaseColorPath;
		}
	}
	Set->Status = TEXT("手动通道");
	SavePersistedState();
	RefreshMaterialSetList();
}

const FSlateBrush* SPBRTextureSuiteTab::GetManualTextureBrush(const FString& ImagePath)
{
	if (ImagePath.IsEmpty() || !FPaths::FileExists(ImagePath))
	{
		return FAppStyle::GetBrush("WhiteBrush");
	}

	if (!PreviewBrushCache.Contains(ImagePath))
	{
		PreviewBrushCache.Add(
			ImagePath,
			MakeShared<FSlateDynamicImageBrush>(FName(*ImagePath), GetPreviewImageSize(ImagePath)));
	}

	const TSharedPtr<FSlateDynamicImageBrush>* Brush = PreviewBrushCache.Find(ImagePath);
	return Brush && Brush->IsValid() ? Brush->Get() : FAppStyle::GetBrush("WhiteBrush");
}

FReply SPBRTextureSuiteTab::OnManualMappingForSet(TSharedPtr<FPBRMaterialSet> Set)
{
	if (!Set.IsValid())
	{
		return FReply::Handled();
	}

	TArray<FString> Files;
	if (!Set->Folder.IsEmpty() && FPaths::DirectoryExists(Set->Folder))
	{
		IFileManager::Get().FindFilesRecursive(Files, *Set->Folder, TEXT("*.*"), true, false);
		Files.RemoveAll([](const FString& File) { return !IsPBRTextureFile(File); });
	}
	for (const TPair<FString, FString>& Pair : Set->Channels)
	{
		AddPBRManualFile(Files, Pair.Value);
	}
	for (const FString& File : Set->Duplicates)
	{
		AddPBRManualFile(Files, File);
	}
	for (const FString& File : Set->Unknown)
	{
		AddPBRManualFile(Files, File);
	}
	AddPBRManualFile(Files, Set->PreviewPath);
	Files.Sort();

	ManualTextureRows.Reset();
	for (const FString& File : Files)
	{
		TSharedPtr<FPBRManualTextureChannelItem> Row = MakeShared<FPBRManualTextureChannelItem>();
		Row->FilePath = File;
		Row->Channel = TEXT("不使用");
		if (FPaths::IsSamePath(Set->PreviewPath, File))
		{
			Row->Channel = TEXT("Preview");
		}
		for (const TPair<FString, FString>& Pair : Set->Channels)
		{
			if (FPaths::IsSamePath(Pair.Value, File))
			{
				Row->Channel = Pair.Key;
				break;
			}
		}
		if (Row->Channel == TEXT("不使用") && Set->Unknown.ContainsByPredicate([&File](const FString& UnknownFile)
		{
			return FPaths::IsSamePath(UnknownFile, File);
		}))
		{
			Row->Channel = TEXT("Unknown");
		}
		if (Row->Channel == TEXT("不使用"))
		{
			Row->Channel = FPBRTextureScanner::IsProbablePBRPreviewImage(File)
				? TEXT("Preview")
				: FPBRTextureScanner::DetectPBRChannelFromFilename(File);
		}
		ManualTextureRows.Add(Row);
	}

	if (ManualTextureRows.Num() == 0)
	{
		return FReply::Handled();
	}

	TSharedPtr<SListView<TSharedPtr<FPBRManualTextureChannelItem>>> ManualListView;
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("手动修改 PBR 贴图通道")))
		.ClientSize(FVector2D(820, 520))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 6)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("材质: %s    文件夹: %s"), *Set->Name, *Set->Folder)))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(12, 4)
		[
			SAssignNew(ManualListView, SListView<TSharedPtr<FPBRManualTextureChannelItem>>)
			.ListItemsSource(&ManualTextureRows)
			.HeaderRow(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(TEXT("Preview")).DefaultLabel(LOCTEXT("PBRManualPreview", "预览")).FillWidth(0.14f)
				+ SHeaderRow::Column(TEXT("File")).DefaultLabel(LOCTEXT("PBRManualFile", "贴图文件")).FillWidth(0.66f)
				+ SHeaderRow::Column(TEXT("Channel")).DefaultLabel(LOCTEXT("PBRManualChannel", "通道")).FillWidth(0.34f))
			.OnGenerateRow(this, &SPBRTextureSuiteTab::GenerateManualTextureRow)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("PBRManualApply", "应用通道"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.OnClicked_Lambda([this, Set, Window]()
				{
					ApplyManualTextureChannels(Set);
					Window->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("PBRManualCancel", "取消"))
				.OnClicked_Lambda([Window]()
				{
					Window->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]);

	FSlateApplication::Get().AddWindow(Window);
	return FReply::Handled();
}

TSharedRef<SHeaderRow> SPBRTextureSuiteTab::BuildHeaderRow()
{
	return SNew(SHeaderRow)
		+ SHeaderRow::Column(TEXT("Check")).DefaultLabel(LOCTEXT("ColCheck", "选择")).FillWidth(0.07f)
		+ SHeaderRow::Column(TEXT("Preview")).DefaultLabel(LOCTEXT("ColPreview", "预览")).FillWidth(0.1f)
		+ SHeaderRow::Column(TEXT("Status")).DefaultLabel(LOCTEXT("ColStatus", "状态")).FillWidth(0.14f)
		+ SHeaderRow::Column(TEXT("Name")).DefaultLabel(LOCTEXT("ColName", "名称")).FillWidth(0.18f)
		+ SHeaderRow::Column(TEXT("Type")).DefaultLabel(LOCTEXT("ColType", "母材质")).FillWidth(0.12f)
		+ SHeaderRow::Column(TEXT("Channels")).DefaultLabel(LOCTEXT("ColChannels", "通道")).FillWidth(0.16f)
		+ SHeaderRow::Column(TEXT("ManualChannels")).DefaultLabel(LOCTEXT("ColManualChannels", "手动通道")).FillWidth(0.1f)
		+ SHeaderRow::Column(TEXT("Issues")).DefaultLabel(LOCTEXT("ColIssues", "问题")).FillWidth(0.13f);
}

TSharedRef<ITableRow> SPBRTextureSuiteTab::OnGenerateRow(
	TSharedPtr<FPBRMaterialSet> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FPBRMaterialSet>>, Owner)
		.OnDragDetected(this, &SPBRTextureSuiteTab::OnMaterialRowDragDetected, Item)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.07f).Padding(4)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]()
				{
					return Item.IsValid() && Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState NewState)
				{
					if (Item.IsValid())
					{
						Item->bChecked = (NewState == ECheckBoxState::Checked);
						SavePersistedState();
					}
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.1f).Padding(4)
			[
				SNew(SBox)
				.WidthOverride(54)
				.HeightOverride(54)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					BuildPreviewWidget(Item, FVector2D(54.0f, 54.0f))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.14f).Padding(4)
			[
				SNew(STextBlock)
				.Text_Lambda([Item]()
				{
					return FText::FromString(Item.IsValid() ? Item->Status : FString());
				})
				.ColorAndOpacity_Lambda([Item]()
				{
					const FString Status = Item.IsValid() ? Item->Status : FString();
					if (Status.StartsWith(TEXT("已创建")) || Status.StartsWith(TEXT("已应用")) ||
						Status.StartsWith(TEXT("完成")) || Status.StartsWith(TEXT("OK")))
					{
						return FSlateColor(FLinearColor(0.25f, 0.75f, 0.5f));
					}
					if (Status.StartsWith(TEXT("失败")) || Status.StartsWith(TEXT("跳过")) ||
						Status.StartsWith(TEXT("重复")) || Status.StartsWith(TEXT("未应用")) ||
						Status.StartsWith(TEXT("不能拖拽")) || Status.StartsWith(TEXT("Failed")) ||
						Status.StartsWith(TEXT("Skipped")))
					{
						return FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f));
					}
					if (Status.StartsWith(TEXT("拖拽中")))
					{
						return FSlateColor(FLinearColor(0.4f, 0.65f, 1.0f));
					}
					return FSlateColor(FLinearColor::Gray);
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.18f).Padding(4)
			[
				SNew(STextBlock).Text(FText::FromString(Item->Name))
			]
			+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(4)
			[
				SNew(STextBlock).Text(FText::FromString(DetectMaterialTypeLabel(*Item)))
			]
			+ SHorizontalBox::Slot().FillWidth(0.16f).Padding(4)
			[
				SNew(STextBlock).Text(FText::FromString(ChannelSummary(Item->Channels)))
			]
			+ SHorizontalBox::Slot().FillWidth(0.1f).Padding(4)
			[
				SNew(SButton)
				.Text(LOCTEXT("ManualChannelsRow", "通道..."))
				.OnClicked(this, &SPBRTextureSuiteTab::OnManualMappingForSet, Item)
			]
			+ SHorizontalBox::Slot().FillWidth(0.13f).Padding(4)
			[
				SNew(STextBlock).Text(FText::FromString(SetDisplayIssues(*Item)))
			]
		];
}

void SPBRTextureSuiteTab::OnSelectionChanged(TSharedPtr<FPBRMaterialSet> Item, ESelectInfo::Type SelectInfo)
{
}

TSharedPtr<SWidget> SPBRTextureSuiteTab::MakeMaterialSetContextMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameSetBeforeCreate", "重命名材质"),
		LOCTEXT("RenameSetBeforeCreateTip", "创建材质前修改套件名称，生成材质会使用新名称"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPBRTextureSuiteTab::OnRenameSelectedMaterialSet)));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ManualMappingContext", "手动修改贴图通道"),
		LOCTEXT("ManualMappingContextTip", "逐张指定当前 PBR 套件里的贴图通道"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { OnManualMapping(); })));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenSetLocation", "打开文件所在位置"),
		LOCTEXT("OpenSetLocationTip", "打开当前贴图套件所在文件夹"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPBRTextureSuiteTab::OnOpenSelectedMaterialSetLocation)));
	return MenuBuilder.MakeWidget();
}

void SPBRTextureSuiteTab::OnRenameSelectedMaterialSet()
{
	TArray<TSharedPtr<FPBRMaterialSet>> Selected = bGridViewMode && TileView.IsValid()
		? TileView->GetSelectedItems()
		: (TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<TSharedPtr<FPBRMaterialSet>>());
	if (Selected.Num() == 0 || !Selected[0].IsValid())
	{
		return;
	}

	TSharedPtr<FPBRMaterialSet> Item = Selected[0];
	TSharedPtr<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("RenameSetWindow", "重命名材质"))
		.ClientSize(FVector2D(360, 112))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedPtr<SEditableTextBox> NameBox;
	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 12, 12, 6)
		[
			SAssignNew(NameBox, SEditableTextBox)
			.Text(FText::FromString(Item->Name))
			.SelectAllTextWhenFocused(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RenameSetOk", "确定"))
				.OnClicked_Lambda([this, Window, NameBox, Item]()
				{
					FString NewName = NameBox.IsValid() ? NameBox->GetText().ToString().TrimStartAndEnd() : FString();
					if (!NewName.IsEmpty())
					{
						Item->Name = NewName;
						SavePersistedState();
						RefreshMaterialSetList();
					}
					Window->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RenameSetCancel", "取消"))
				.OnClicked_Lambda([Window]() { Window->RequestDestroyWindow(); return FReply::Handled(); })
			]
		]);
	FSlateApplication::Get().AddWindow(Window.ToSharedRef());
}

void SPBRTextureSuiteTab::OnOpenSelectedMaterialSetLocation()
{
	TArray<TSharedPtr<FPBRMaterialSet>> Selected = bGridViewMode && TileView.IsValid()
		? TileView->GetSelectedItems()
		: (TreeView.IsValid() ? TreeView->GetSelectedItems() : TArray<TSharedPtr<FPBRMaterialSet>>());
	if (Selected.Num() == 0 || !Selected[0].IsValid())
	{
		return;
	}

	FString Folder = Selected[0]->Folder;
	if (Folder.IsEmpty())
	{
		for (const auto& Pair : Selected[0]->Channels)
		{
			Folder = FPaths::GetPath(Pair.Value);
			break;
		}
	}
	if (!Folder.IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*Folder);
	}
}

TSharedRef<ITableRow> SPBRTextureSuiteTab::OnGenerateTile(
	TSharedPtr<FPBRMaterialSet> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FPBRMaterialSet>>, Owner)
		.OnDragDetected(this, &SPBRTextureSuiteTab::OnMaterialRowDragDetected, Item)
		[
			SNew(SBorder)
			.Padding(4)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0, 3)
				[
					SNew(SBox)
					.WidthOverride(86)
					.HeightOverride(86)
					[
						BuildPreviewWidget(Item, FVector2D(86.0f, 86.0f))
					]
				]
			]
		];
}

FReply SPBRTextureSuiteTab::OnMaterialRowDragDetected(
	const FGeometry& Geometry,
	const FPointerEvent& MouseEvent,
	TSharedPtr<FPBRMaterialSet> Item)
{
	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !Item.IsValid())
	{
		return FReply::Unhandled();
	}

	UMaterialInterface* Material = Item->CreatedMaterial.LoadSynchronous();
	if (!Material)
	{
		Item->Status = TEXT("不能拖拽: 请先创建材质");
		RefreshMaterialSetList();
		return FReply::Unhandled();
	}

	Item->Status = TEXT("拖拽中: 可放到场景物体或材质槽");
	RefreshMaterialSetList();

	return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(FAssetData(Material)));
}

void SPBRTextureSuiteTab::OnBoxSelectRange(const FVector2D& ScreenStart, const FVector2D& ScreenEnd)
{
	if (!TreeView.IsValid() || MaterialSets.Num() == 0)
	{
		return;
	}

	const float MinY = FMath::Min(ScreenStart.Y, ScreenEnd.Y);
	const float MaxY = FMath::Max(ScreenStart.Y, ScreenEnd.Y);
	const float MinX = FMath::Min(ScreenStart.X, ScreenEnd.X);
	const float MaxX = FMath::Max(ScreenStart.X, ScreenEnd.X);

	TreeView->ClearSelection();
	const int32 FirstVisibleIndex = FMath::Max(0, FMath::FloorToInt(TreeView->GetScrollOffset()));
	for (int32 GeneratedIndex = 0; GeneratedIndex < TreeView->GetNumGeneratedChildren(); ++GeneratedIndex)
	{
		TSharedPtr<SWidget> RowWidget = TreeView->GetGeneratedChildAt(GeneratedIndex);
		if (!RowWidget.IsValid())
		{
			continue;
		}

		const FGeometry RowGeometry = RowWidget->GetTickSpaceGeometry();
		const FVector2D RowTopLeft = RowGeometry.GetAbsolutePosition();
		const FVector2D RowBottomRight = RowTopLeft + RowGeometry.GetAbsoluteSize();
		const bool bIntersectsY = RowBottomRight.Y >= MinY && RowTopLeft.Y <= MaxY;
		const bool bIntersectsX = RowBottomRight.X >= MinX && RowTopLeft.X <= MaxX;
		if (!bIntersectsY || !bIntersectsX)
		{
			continue;
		}

		const int32 MaterialSetIndex = FirstVisibleIndex + GeneratedIndex;
		if (MaterialSetIndex < MaterialSets.Num() && MaterialSets[MaterialSetIndex].IsValid())
		{
			TreeView->SetItemSelection(MaterialSets[MaterialSetIndex], true, ESelectInfo::Direct);
		}
	}

	TArray<TSharedPtr<FPBRMaterialSet>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.Num() == 0)
	{
		for (TSharedPtr<FPBRMaterialSet>& Set : MaterialSets)
		{
			if (Set.IsValid())
			{
				Set->Status = TEXT("框选: 当前可见行未命中，请从行内容区域拖动");
				break;
			}
		}
	}
	RefreshMaterialSetList();
}

FString SPBRTextureSuiteTab::ChannelSummary(const TMap<FString, FString>& Channels) const
{
	FString Summary;
	for (const FString& Ch : FPBRTextureScanner::ChannelDisplayOrder)
	{
		if (Channels.Contains(Ch))
		{
			if (!Summary.IsEmpty()) Summary += TEXT(", ");
			Summary += Ch;
		}
	}
	if (Summary.IsEmpty()) Summary = TEXT("无");
	return Summary;
}

FString SPBRTextureSuiteTab::SetDisplayIssues(const FPBRMaterialSet& Set) const
{
	TArray<FString> Issues;

	if (!Set.Channels.Contains(TEXT("BaseColor")))
	{
		Issues.Add(TEXT("无基础色，使用母材质默认"));
	}
	if (!Set.Channels.Contains(TEXT("Normal")) && !Set.Channels.Contains(TEXT("NormalDX")) && !Set.Channels.Contains(TEXT("NormalGL")))
	{
		Issues.Add(TEXT("无法线，使用母材质默认"));
	}
	if (!Set.Channels.Contains(TEXT("Roughness")) && !Set.Channels.Contains(TEXT("Glossiness")) && !Set.Channels.Contains(TEXT("ORM")) && !Set.Channels.Contains(TEXT("ARM")))
	{
		Issues.Add(TEXT("无粗糙度，使用母材质默认"));
	}
	if (Set.Unknown.Num() > 0)
	{
		Issues.Add(FString::Printf(TEXT("%d 个未识别"), Set.Unknown.Num()));
	}

	return Issues.Num() > 0 ? FString::Join(Issues, TEXT("; ")) : TEXT("OK");
}

FString SPBRTextureSuiteTab::DetectMaterialTypeLabel(const FPBRMaterialSet& Set) const
{
	if (!SelectedMaterialTypeMode.IsEmpty())
	{
		const FString Mode = SelectedMaterialTypeMode.TrimStartAndEnd();
		if (!Mode.IsEmpty() && Mode != TEXT("自动") && !Mode.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			return Mode;
		}
	}

	FString Text = (Set.Name + TEXT(" ") + Set.Folder).ToLower();
	for (const TPair<FString, FString>& ChannelPair : Set.Channels)
	{
		Text += TEXT(" ") + ChannelPair.Key.ToLower();
		Text += TEXT(" ") + FPaths::GetBaseFilename(ChannelPair.Value).ToLower();
	}

	if (Text.Contains(TEXT("water")) || Text.Contains(TEXT("pool")) || Text.Contains(TEXT("ocean")) || Text.Contains(TEXT("水")))
	{
		return TEXT("水");
	}
	if (Text.Contains(TEXT("glass")) || Text.Contains(TEXT("window")) || Text.Contains(TEXT("mirror")) || Text.Contains(TEXT("玻璃")))
	{
		return TEXT("玻璃");
	}
	if (Text.Contains(TEXT("transparent")) || Text.Contains(TEXT("opacity")) || Text.Contains(TEXT("alpha")) ||
		Text.Contains(TEXT("半透明")) || Text.Contains(TEXT("透明")) || Set.Channels.Contains(TEXT("Opacity")))
	{
		return TEXT("半透明");
	}
	if (Text.Contains(TEXT("emissive")) || Text.Contains(TEXT("glow")) || Text.Contains(TEXT("neon")) ||
		Text.Contains(TEXT("自发光")) || Text.Contains(TEXT("发光")) || Set.Channels.Contains(TEXT("Emissive")))
	{
		return TEXT("自发光");
	}
	if (Text.Contains(TEXT("metal")) || Text.Contains(TEXT("steel")) || Text.Contains(TEXT("iron")) ||
		Text.Contains(TEXT("copper")) || Text.Contains(TEXT("金属")) || Set.Channels.Contains(TEXT("Metallic")))
	{
		return TEXT("金属");
	}
	if (Text.Contains(TEXT("fabric")) || Text.Contains(TEXT("cloth")) || Text.Contains(TEXT("linen")) ||
		Text.Contains(TEXT("cotton")) || Text.Contains(TEXT("wool")) || Text.Contains(TEXT("carpet")) ||
		Text.Contains(TEXT("rug")) || Text.Contains(TEXT("curtain")) || Text.Contains(TEXT("布")) ||
		Text.Contains(TEXT("织物")) || Text.Contains(TEXT("地毯")))
	{
		return TEXT("布艺");
	}
	if (Text.Contains(TEXT("leather")) || Text.Contains(TEXT("皮革")) || Text.Contains(TEXT("皮")))
	{
		return TEXT("皮革");
	}
	if (Text.Contains(TEXT("wood")) || Text.Contains(TEXT("timber")) || Text.Contains(TEXT("floor")) ||
		Text.Contains(TEXT("木")) || Text.Contains(TEXT("木地板")))
	{
		return TEXT("木材");
	}
	if (Text.Contains(TEXT("tile")) || Text.Contains(TEXT("ceramic")) || Text.Contains(TEXT("brick")) ||
		Text.Contains(TEXT("瓷砖")) || Text.Contains(TEXT("砖")))
	{
		return TEXT("瓷砖");
	}
	if (Text.Contains(TEXT("stone")) || Text.Contains(TEXT("rock")) || Text.Contains(TEXT("marble")) ||
		Text.Contains(TEXT("granite")) || Text.Contains(TEXT("石")) || Text.Contains(TEXT("大理石")))
	{
		return TEXT("石材");
	}
	if (Text.Contains(TEXT("plastic")) || Text.Contains(TEXT("pvc")) || Text.Contains(TEXT("塑料")))
	{
		return TEXT("塑料");
	}
	return TEXT("标准");
}

FString SPBRTextureSuiteTab::GetMaterialTypeDescription(const FString& MaterialTypeMode) const
{
	const FString Mode = MaterialTypeMode.TrimStartAndEnd().ToLower();
	if (Mode.Contains(TEXT("木")) || Mode.Contains(TEXT("wood")))
	{
		return TEXT("木材母材质: 用于木地板、木饰面、木纹贴图，默认非金属，保留粗糙度、AO、法线和统一 UV 调整。");
	}
	if (Mode.Contains(TEXT("石")) || Mode.Contains(TEXT("stone")) || Mode.Contains(TEXT("marble")))
	{
		return TEXT("石材母材质: 用于石材、大理石、岩板，默认非金属，重点保留粗糙度、AO、法线和 UV 调整。");
	}
	if (Mode.Contains(TEXT("砖")) || Mode.Contains(TEXT("瓷砖")) || Mode.Contains(TEXT("tile")))
	{
		return TEXT("瓷砖母材质: 用于瓷砖、砖墙、地砖，默认非金属，适合重复纹理和角度旋转调整。");
	}
	if (Mode.Contains(TEXT("布")) || Mode.Contains(TEXT("fabric")) || Mode.Contains(TEXT("cloth")))
	{
		return TEXT("布艺母材质: 用于布料、窗帘、地毯、织物，默认非金属，后续会增加织物绒毛和布纹控制。");
	}
	if (Mode.Contains(TEXT("皮")) || Mode.Contains(TEXT("leather")))
	{
		return TEXT("皮革母材质: 用于沙发、座椅、皮革软包，默认非金属，保留粗糙度、AO、法线和统一 UV 调整。");
	}
	if (Mode.Contains(TEXT("塑料")) || Mode.Contains(TEXT("plastic")))
	{
		return TEXT("塑料母材质: 用于塑料、PVC、亚克力等普通不透明材质，默认非金属，保留金属度倍率但默认关闭。");
	}
	if (Mode.Contains(TEXT("金属")) || Mode.Contains(TEXT("metal")))
	{
		return TEXT("金属母材质: 用于不锈钢、铁、铜、铝等金属，默认金属度为 1，有贴图时可继续由金属度贴图控制。");
	}
	if (Mode.Contains(TEXT("半透明")) || Mode.Contains(TEXT("透明")) || Mode.Contains(TEXT("transparent")) || Mode.Contains(TEXT("opacity")))
	{
		return TEXT("半透明母材质: 用于窗帘、树叶、镂空贴图和带透明通道的材质，重点处理透明度和遮罩。");
	}
	if (Mode.Contains(TEXT("玻璃")) || Mode.Contains(TEXT("glass")))
	{
		return TEXT("玻璃母材质: 用于玻璃、镜面玻璃、磨砂玻璃，后续会加入透明、折射、粗糙玻璃等参数。");
	}
	if (Mode.Contains(TEXT("水")) || Mode.Contains(TEXT("water")))
	{
		return TEXT("水母材质: 用于水面、泳池、水景，后续会加入法线流动、波纹、深浅颜色等参数。");
	}
	if (Mode.Contains(TEXT("自发光")) || Mode.Contains(TEXT("发光")) || Mode.Contains(TEXT("emissive")))
	{
		return TEXT("自发光母材质: 用于灯带、屏幕、发光标识，只保留基础色、自发光和 UV 调整，避免普通 PBR 多余节点参与输出。");
	}
	if (Mode.IsEmpty() || Mode == TEXT("自动") || Mode == TEXT("auto"))
	{
		return TEXT("自动母材质: 根据套件名、文件夹名、贴图名和通道判断木材、石材、瓷砖、布艺、皮革、塑料、金属、半透明、玻璃、水、自发光或标准。判断不准时可手动选择。");
	}
	return TEXT("标准母材质: 用于木材、墙面、石材、塑料、普通不透明材质，默认非金属。");
}

TSharedRef<SWidget> SPBRTextureSuiteTab::GenerateMaterialTypeOption(FStringOption Option) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(Option.IsValid() ? *Option : TEXT("标准")));
}

void SPBRTextureSuiteTab::OnMaterialTypeSelected(FStringOption Option, ESelectInfo::Type SelectInfo)
{
	if (!Option.IsValid())
	{
		return;
	}

	SelectedMaterialTypeOption = Option;
	SelectedMaterialTypeMode = *Option;
	if (MaterialTypeHelpText.IsValid())
	{
		MaterialTypeHelpText->SetText(FText::FromString(GetMaterialTypeDescription(SelectedMaterialTypeMode)));
	}
	RefreshMaterialSetList();
}

FText SPBRTextureSuiteTab::GetSelectedMaterialTypeText() const
{
	return FText::FromString(SelectedMaterialTypeOption.IsValid() ? *SelectedMaterialTypeOption : SelectedMaterialTypeMode);
}

TSharedRef<SWidget> SPBRTextureSuiteTab::GenerateNormalModeOption(FStringOption Option) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(Option.IsValid() ? *Option : TEXT("自动")));
}

void SPBRTextureSuiteTab::OnNormalModeSelected(FStringOption Option, ESelectInfo::Type SelectInfo)
{
	if (!Option.IsValid())
	{
		return;
	}

	SelectedNormalModeOption = Option;
	SelectedNormalMode = *Option;
}

FText SPBRTextureSuiteTab::GetSelectedNormalModeText() const
{
	return FText::FromString(SelectedNormalModeOption.IsValid() ? *SelectedNormalModeOption : SelectedNormalMode);
}

TSharedRef<SWidget> SPBRTextureSuiteTab::BuildPreviewWidget(TSharedPtr<FPBRMaterialSet> Item, const FVector2D& Size)
{
	if (Item.IsValid() && !Item->PreviewPath.IsEmpty() && FPaths::FileExists(Item->PreviewPath))
	{
		return SNew(SImage)
			.Image(GetPreviewBrush(*Item))
			.DesiredSizeOverride(Size);
	}

	UMaterialInterface* Material = GetPreviewFallbackMaterial(Item);
	if (!ThumbnailPool.IsValid() || !Material)
	{
		return SNew(SImage)
			.Image(FAppStyle::GetBrush("ClassThumbnail.Material"))
			.DesiredSizeOverride(Size);
	}

	const FString CacheKey = Material->GetPathName() + FString::Printf(TEXT("_%dx%d"), FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y));
	TSharedPtr<FAssetThumbnail>& Thumbnail = MaterialThumbnailCache.FindOrAdd(CacheKey);
	if (!Thumbnail.IsValid())
	{
		Thumbnail = MakeShared<FAssetThumbnail>(Material, static_cast<uint32>(Size.X), static_cast<uint32>(Size.Y), ThumbnailPool);
	}
	return Thumbnail->MakeThumbnailWidget();
}

UMaterialInterface* SPBRTextureSuiteTab::GetPreviewFallbackMaterial(TSharedPtr<FPBRMaterialSet> Item) const
{
	if (Item.IsValid())
	{
		if (UMaterialInterface* Created = Item->CreatedMaterial.LoadSynchronous())
		{
			return Created;
		}
	}
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
}

const FSlateBrush* SPBRTextureSuiteTab::GetPreviewBrush(const FPBRMaterialSet& Set)
{
	if (Set.PreviewPath.IsEmpty() || !FPaths::FileExists(Set.PreviewPath))
	{
		return FAppStyle::GetBrush("WhiteBrush");
	}

	if (!PreviewBrushCache.Contains(Set.PreviewPath))
	{
		PreviewBrushCache.Add(
			Set.PreviewPath,
			MakeShared<FSlateDynamicImageBrush>(FName(*Set.PreviewPath), GetPreviewImageSize(Set.PreviewPath)));
	}

	const TSharedPtr<FSlateDynamicImageBrush>* Brush = PreviewBrushCache.Find(Set.PreviewPath);
	return Brush && Brush->IsValid() ? Brush->Get() : FAppStyle::GetBrush("WhiteBrush");
}

FVector2D SPBRTextureSuiteTab::GetPreviewImageSize(const FString& ImagePath) const
{
	constexpr float MaxSize = 54.0f;

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *ImagePath) || FileData.Num() == 0)
	{
		return FVector2D(MaxSize, MaxSize);
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		return FVector2D(MaxSize, MaxSize);
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat, *ImagePath);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		return FVector2D(MaxSize, MaxSize);
	}

	const float Width = static_cast<float>(ImageWrapper->GetWidth());
	const float Height = static_cast<float>(ImageWrapper->GetHeight());
	if (Width <= 0.0f || Height <= 0.0f)
	{
		return FVector2D(MaxSize, MaxSize);
	}

	const float Scale = MaxSize / FMath::Max(Width, Height);
	return FVector2D(FMath::Max(1.0f, Width * Scale), FMath::Max(1.0f, Height * Scale));
}

UMaterialInterface* SPBRTextureSuiteTab::GetMaterialForApply(TSharedPtr<FPBRMaterialSet>& OutSourceSet) const
{
	OutSourceSet.Reset();
	if (!TreeView.IsValid())
	{
		return nullptr;
	}

	TArray<TSharedPtr<FPBRMaterialSet>> SelectedItems = TreeView->GetSelectedItems();
	for (const TSharedPtr<FPBRMaterialSet>& Set : SelectedItems)
	{
		if (Set.IsValid())
		{
			if (UMaterialInterface* Material = Set->CreatedMaterial.LoadSynchronous())
			{
				OutSourceSet = Set;
				return Material;
			}
		}
	}

	for (const TSharedPtr<FPBRMaterialSet>& Set : MaterialSets)
	{
		if (Set.IsValid() && Set->bChecked)
		{
			if (UMaterialInterface* Material = Set->CreatedMaterial.LoadSynchronous())
			{
				OutSourceSet = Set;
				return Material;
			}
		}
	}
	return nullptr;
}

int32 SPBRTextureSuiteTab::GetTargetMaterialSlot() const
{
	int32 SlotIndex = 0;
	if (MaterialSlotBox.IsValid())
	{
		LexTryParseString(SlotIndex, *MaterialSlotBox->GetText().ToString());
	}
	return FMath::Max(0, SlotIndex);
}

EPBRMaterialType SPBRTextureSuiteTab::ResolveCurrentTemplateType() const
{
	FPBRMaterialSet ReferenceSet;
	if (TreeView.IsValid())
	{
		const TArray<TSharedPtr<FPBRMaterialSet>> SelectedItems = TreeView->GetSelectedItems();
		for (const TSharedPtr<FPBRMaterialSet>& Item : SelectedItems)
		{
			if (Item.IsValid())
			{
				ReferenceSet = *Item;
				break;
			}
		}
	}

	if (ReferenceSet.Name.IsEmpty() && TileView.IsValid())
	{
		const TArray<TSharedPtr<FPBRMaterialSet>> SelectedItems = TileView->GetSelectedItems();
		for (const TSharedPtr<FPBRMaterialSet>& Item : SelectedItems)
		{
			if (Item.IsValid())
			{
				ReferenceSet = *Item;
				break;
			}
		}
	}

	if (ReferenceSet.Name.IsEmpty() && ReferenceSet.Folder.IsEmpty() && ReferenceSet.Channels.Num() == 0)
	{
		for (const TSharedPtr<FPBRMaterialSet>& Item : MaterialSets)
		{
			if (Item.IsValid() && Item->bChecked)
			{
				ReferenceSet = *Item;
				break;
			}
		}
	}

	return FPBRMaterialFactory::ResolveMaterialTypeForSet(ReferenceSet, SelectedMaterialTypeMode);
}

void SPBRTextureSuiteTab::SyncBrowserToAsset(const FString& AssetPath) const
{
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		UEditorAssetLibrary::SyncBrowserToObjects({ AssetPath });
	}
}

#undef LOCTEXT_NAMESPACE
