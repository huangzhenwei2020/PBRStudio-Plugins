#include "Widgets/SPBRSideNav.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SPBRSideNav"

namespace
{
struct FPBRNavItem
{
	FText Title;
	FName IconName;
	int32 TabIndex = 0;
};
}

void SPBRSideNav::Construct(const FArguments& InArgs)
{
	OnTabChanged = InArgs._OnTabChanged;

	TSharedRef<SVerticalBox> NavLayout = SNew(SVerticalBox);

	const TArray<TArray<FPBRNavItem>> Groups = {
		{
			{ LOCTEXT("DownloadLibrary", "PBR\u4E0B\u8F7D"), "Icons.Download", 1 },
			{ LOCTEXT("TextureSuite", "PBR\u81EA\u52A8\u751F\u6210"), "Icons.PlusCircle", 0 },
			{ LOCTEXT("LocalMaterialVault", "\u672C\u5730\u6750\u8D28\u5E93"), "ClassIcon.Material", 2 },
			{ LOCTEXT("SpecialMaterials", "\u7279\u6B8A\u6750\u8D28"), "ClassIcon.MaterialInstanceConstant", 4 },
			{ LOCTEXT("SubstrateMode", "Substrate\u6A21\u5F0F"), "Icons.Layers", 10 },
		},
		{
			{ LOCTEXT("UERules", "\u8D34\u56FE\u89C4\u8303\u5316"), "Icons.Settings", 3 },
			{ LOCTEXT("SceneReplace", "\u6750\u8D28\u8F6C\u6362"), "Icons.Convert", 5 },
			{ LOCTEXT("SceneLightConvert", "\u706F\u5149\u8F6C\u6362"), "ClassIcon.PointLightComponent", 6 },
			{ LOCTEXT("SceneCameraConvert", "\u76F8\u673A\u8F6C\u6362"), "ClassIcon.CameraComponent", 7 },
			{ LOCTEXT("MagicOutliner", "\u9B54\u6CD5\u5927\u7EB2"), "Icons.TreeItem", 11 },
			{ LOCTEXT("NvidiaDlss", "NVIDIA DLSS"), "Icons.Package", 12 },
		},
		{
			{ LOCTEXT("SceneBatchAdjust", "\u6279\u91CF\u8C03\u6574"), "Icons.Edit", 8 },
			{ LOCTEXT("ViewportSync", "MAX\u89C6\u53E3\u540C\u6B65"), "LevelEditor.Tabs.Viewports", 9 },
		},
	};

	for (const TArray<FPBRNavItem>& Group : Groups)
	{
		TArray<TSharedRef<SWidget>> GroupWidgets;
		for (const FPBRNavItem& Item : Group)
		{
			GroupWidgets.Add(MakeNavButton(Item.Title, Item.IconName, Item.TabIndex));
		}

		NavLayout->AddSlot()
			.AutoHeight()
			.Padding(10, 8)
			[
				MakeNavCard(GroupWidgets)
			];
	}

	NavLayout->AddSlot()
		.FillHeight(1.0f)
		[
			SNew(SSpacer)
		];

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(210.0f)
		.MaxDesiredWidth(210.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			.BorderBackgroundColor(FLinearColor(0.018f, 0.023f, 0.030f, 1.0f))
			.Padding(0)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					NavLayout
				]
			]
		]
	];
}

TSharedRef<SWidget> SPBRSideNav::MakeNavCard(const TArray<TSharedRef<SWidget>>& Items)
{
	TSharedRef<SVerticalBox> CardContent = SNew(SVerticalBox);

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		CardContent->AddSlot()
			.AutoHeight()
			[
				Items[ItemIndex]
			];

		if (ItemIndex < Items.Num() - 1)
		{
			CardContent->AddSlot()
				.AutoHeight()
				.Padding(12, 2)
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
					.Thickness(1.0f)
					.SeparatorImage(FAppStyle::Get().GetBrush("Brushes.White"))
					.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.055f))
				];
		}
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor(FLinearColor(0.045f, 0.052f, 0.066f, 1.0f))
		.Padding(6)
		[
			CardContent
		];
}

TSharedRef<SWidget> SPBRSideNav::MakeNavButton(const FText& Title, const FName IconName, int32 Index)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton")
		.ContentPadding(FMargin(8, 7))
		.ButtonColorAndOpacity_Lambda([this, Index]()
		{
			return ActiveIndex == Index
				? FSlateColor(FLinearColor(0.95f, 0.70f, 0.20f, 0.20f))
				: FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		})
		.OnClicked_Lambda([this, Index]()
		{
			SetActiveTab(Index);
			OnTabChanged.ExecuteIfBound(Index);
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush(IconName))
				.ColorAndOpacity_Lambda([this, Index]()
				{
					return ActiveIndex == Index
						? FSlateColor(FLinearColor(1.0f, 0.82f, 0.26f, 1.0f))
						: FSlateColor(FLinearColor(0.68f, 0.76f, 0.86f, 0.95f));
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.ColorAndOpacity_Lambda([this, Index]()
				{
					return ActiveIndex == Index
						? FSlateColor(FLinearColor(1.0f, 0.86f, 0.30f, 1.0f))
						: FSlateColor(FLinearColor(0.90f, 0.94f, 0.98f, 1.0f));
				})
			]
		];
}

void SPBRSideNav::SetActiveTab(int32 TabIndex)
{
	ActiveIndex = TabIndex;
}

#undef LOCTEXT_NAMESPACE
