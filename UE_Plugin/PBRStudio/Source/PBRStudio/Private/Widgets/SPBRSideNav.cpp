#include "Widgets/SPBRSideNav.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"

void SPBRSideNav::Construct(const FArguments& InArgs)
{
	OnTabChanged = InArgs._OnTabChanged;

	TSharedRef<SVerticalBox> NavLayout = SNew(SVerticalBox);

	struct FNavItem { FString Icon; FString Title; };
	TArray<FNavItem> Items = {
		{ TEXT("\U0001F9E9"), TEXT("贴图套件") },
		{ TEXT("\U0001F310"), TEXT("下载库") },
		{ TEXT("\U0001F680"), TEXT("UE 规范") },
	};

	for (int32 i = 0; i < Items.Num(); ++i)
	{
		NavLayout->AddSlot()
			.AutoHeight()
			.Padding(4, 3)
			[
				MakeNavButton(Items[i].Icon, Items[i].Title, i)
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
		.MinDesiredWidth(140.0f)
		.MaxDesiredWidth(140.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				NavLayout
			]
		]
	];
}

TSharedRef<SWidget> SPBRSideNav::MakeNavButton(const FString& Icon, const FString& Title, int32 Index)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton")
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
			.Padding(8, 4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Icon))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4, 4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Title))
			]
		];
}

void SPBRSideNav::SetActiveTab(int32 TabIndex)
{
	ActiveIndex = TabIndex;
}
