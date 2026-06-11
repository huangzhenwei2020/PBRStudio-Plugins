#include "Widgets/SPBRSubstrateModeTab.h"

#include "EditorAssetLibrary.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SPBRSubstrateModeTab"

void SPBRSubstrateModeTab::Construct(const FArguments& InArgs)
{
	TemplateTypeOptions.Reset();
	for (EPBRSubstrateTemplateType TemplateType : FPBRSubstrateMaterialTemplateManager::GetAllTemplateTypes())
	{
		TemplateTypeOptions.Add(MakeShared<EPBRSubstrateTemplateType>(TemplateType));
	}
	SelectedTemplateTypeOption = TemplateTypeOptions.Num() > 0 ? TemplateTypeOptions[0] : nullptr;
	Status = TEXT("准备就绪。Substrate 资源会单独生成到 /Game/PBRStudio/Substrate，不会修改兼容模式母材质。");

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(10, 10, 10, 4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "Substrate 模式"))
				.Font(FAppStyle::GetFontStyle("HeadingMedium"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(10, 2, 10, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Intro", "这里是独立的 Substrate 材质模式。它使用自己的代码、资源路径和中文参数命名，和现有兼容模式分开维护。"))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(10, 4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("TemplateType", "模板类型:"))
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SNew(SBox)
					.MinDesiredWidth(170)
					[
						SNew(SComboBox<FPBRSubstrateTemplateTypeOption>)
						.OptionsSource(&TemplateTypeOptions)
						.InitiallySelectedItem(SelectedTemplateTypeOption)
						.OnGenerateWidget(this, &SPBRSubstrateModeTab::GenerateTemplateTypeOption)
						.OnSelectionChanged(this, &SPBRSubstrateModeTab::OnTemplateTypeSelected)
						[
							SNew(STextBlock)
							.Text(this, &SPBRSubstrateModeTab::GetSelectedTemplateTypeText)
						]
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
					.Text(LOCTEXT("Rebuild", "生成/重建全部模板"))
					.ToolTipText(LOCTEXT("RebuildTip", "生成独立的 Substrate 母材质和示例材质实例。"))
					.OnClicked(this, &SPBRSubstrateModeTab::OnRebuildSubstrateAssets)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenTemplate", "定位母材质"))
					.OnClicked(this, &SPBRSubstrateModeTab::OnOpenTemplateAsset)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenExample", "定位示例材质"))
					.OnClicked(this, &SPBRSubstrateModeTab::OnOpenExampleAsset)
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(10, 8)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(10, 4)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(10)
				[
					SNew(STextBlock)
					.Text(this, &SPBRSubstrateModeTab::GetStatusText)
					.AutoWrapText(true)
				]
			]
		]
	];
}

FReply SPBRSubstrateModeTab::OnRebuildSubstrateAssets()
{
	TArray<FString> Messages;
	const int32 Count = FPBRSubstrateMaterialTemplateManager::EnsureAllTemplateMaterials(Messages);
	Status = FString::Printf(TEXT("已处理 %d 个 Substrate 母材质。\n%s"), Count, *FString::Join(Messages, TEXT("\n")));
	return FReply::Handled();
}

FReply SPBRSubstrateModeTab::OnOpenTemplateAsset()
{
	const EPBRSubstrateTemplateType TemplateType = GetSelectedTemplateType();
	FString Message;
	FPBRSubstrateMaterialTemplateManager::EnsureTemplateMaterial(TemplateType, Message);
	SyncBrowserToAsset(FPBRSubstrateMaterialTemplateManager::GetTemplatePackagePath(TemplateType));
	Status = FPBRSubstrateMaterialTemplateManager::GetTemplateDisplayName(TemplateType) + TEXT(": ") + Message;
	return FReply::Handled();
}

FReply SPBRSubstrateModeTab::OnOpenExampleAsset()
{
	const EPBRSubstrateTemplateType TemplateType = GetSelectedTemplateType();
	FString Message;
	FPBRSubstrateMaterialTemplateManager::EnsureExampleMaterialInstance(TemplateType, Message);
	SyncBrowserToAsset(FPBRSubstrateMaterialTemplateManager::GetExampleMaterialInstancePackagePath(TemplateType));
	Status = Message;
	return FReply::Handled();
}

TSharedRef<SWidget> SPBRSubstrateModeTab::GenerateTemplateTypeOption(FPBRSubstrateTemplateTypeOption Option) const
{
	const FString Name = Option.IsValid()
		? FPBRSubstrateMaterialTemplateManager::GetTemplateDisplayName(*Option)
		: TEXT("标准 Slab");
	return SNew(STextBlock).Text(FText::FromString(Name));
}

void SPBRSubstrateModeTab::OnTemplateTypeSelected(FPBRSubstrateTemplateTypeOption NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		SelectedTemplateTypeOption = NewSelection;
	}
}

FText SPBRSubstrateModeTab::GetSelectedTemplateTypeText() const
{
	return FText::FromString(FPBRSubstrateMaterialTemplateManager::GetTemplateDisplayName(GetSelectedTemplateType()));
}

FText SPBRSubstrateModeTab::GetStatusText() const
{
	return FText::FromString(Status);
}

void SPBRSubstrateModeTab::SyncBrowserToAsset(const FString& AssetPath) const
{
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		UEditorAssetLibrary::SyncBrowserToObjects({ AssetPath });
	}
}

EPBRSubstrateTemplateType SPBRSubstrateModeTab::GetSelectedTemplateType() const
{
	return SelectedTemplateTypeOption.IsValid()
		? *SelectedTemplateTypeOption
		: EPBRSubstrateTemplateType::Standard;
}

#undef LOCTEXT_NAMESPACE
