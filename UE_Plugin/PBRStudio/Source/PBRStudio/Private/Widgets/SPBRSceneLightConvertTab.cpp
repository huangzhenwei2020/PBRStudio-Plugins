#include "Widgets/SPBRSceneLightConvertTab.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SPBRSceneLightConvertTab"

void SPBRSceneLightConvertTab::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "\u706f\u5149\u8f6c\u6362"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Hint", "\u626b\u63cf\u5bfc\u5165\u573a\u666f\u4e2d\u7684 V-Ray/Corona/Max \u706f\u5149\u75d5\u8ff9\u548c\u53d1\u5149\u9762\uff0c\u8f6c\u4e3a UE \u539f\u751f Rect/Point/Spot Light\u3002"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Scan", "\u626b\u63cf\u573a\u666f\u706f\u5149"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
				.OnClicked(this, &SPBRSceneLightConvertTab::OnScanScene)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Convert", "\u8f6c\u6362\u52fe\u9009\u706f\u5149"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.OnClicked(this, &SPBRSceneLightConvertTab::OnConvertChecked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Undo", "\u64a4\u56de\u4e0a\u6b21\u8f6c\u6362"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Danger")
				.OnClicked(this, &SPBRSceneLightConvertTab::OnUndoConversion)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectAll", "\u5168\u9009"))
				.OnClicked(this, &SPBRSceneLightConvertTab::OnSelectAll)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectNone", "\u5168\u4e0d\u9009"))
				.OnClicked(this, &SPBRSceneLightConvertTab::OnSelectNone)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(SummaryText, STextBlock)
				.Text(LOCTEXT("SummaryEmpty", "\u672a\u626b\u63cf"))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
			[
				SAssignNew(OnlySelectedCheck, SCheckBox)
				[
					SNew(STextBlock).Text(LOCTEXT("OnlySelected", "\u53ea\u626b\u63cf\u9009\u4e2d\u5bf9\u8c61"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
			[
				SAssignNew(MovableLightCheck, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				[
					SNew(STextBlock).Text(LOCTEXT("MovableLight", "\u521b\u5efa\u53ef\u79fb\u52a8\u706f\u5149"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
			[
				SAssignNew(SkipPBRStudioCheck, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				[
					SNew(STextBlock).Text(LOCTEXT("SkipPBRStudio", "\u8df3\u8fc7 PBRStudio \u706f\u5149"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
			[
				SAssignNew(ClearProjectionCheck, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				[
					SNew(STextBlock).Text(LOCTEXT("ClearProjection", "\u6e05\u7406\u6295\u5f71/IES/Preview"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("IntensityMultiplier", "\u5f3a\u5ea6\u500d\u7387"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 20, 0)
			[
				SNew(SNumericEntryBox<float>)
				.MinDesiredValueWidth(72)
				.MinValue(0.05f)
				.MaxValue(20.0f)
				.Value_Lambda([this]() { return IntensityMultiplier; })
				.OnValueChanged_Lambda([this](float NewValue) { IntensityMultiplier = FMath::Max(0.05f, NewValue); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("FolderPath", "\u5927\u7eb2\u6587\u4ef6\u5939"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SAssignNew(FolderPathBox, SEditableTextBox)
				.Text(FText::FromString(TEXT("PBRStudio/ConvertedLights")))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("BatchRename", "\u6279\u91cf\u547d\u540d"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("RenamePrefix", "\u524d\u7f00"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SAssignNew(RenamePrefixBox, SEditableTextBox)
				.MinDesiredWidth(86)
				.Text(FText::FromString(TEXT("PBR_")))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("RenameBase", "\u540d\u79f0"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SAssignNew(RenameBaseNameBox, SEditableTextBox)
				.MinDesiredWidth(120)
				.Text(FText::FromString(TEXT("Light")))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(LOCTEXT("RenameAll", "\u5168\u90e8")).OnClicked(this, &SPBRSceneLightConvertTab::OnBatchRenameAll)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(LOCTEXT("RenameSelected", "\u9009\u4e2d")).OnClicked(this, &SPBRSceneLightConvertTab::OnBatchRenameSelected)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(LOCTEXT("RenameChecked", "\u52fe\u9009")).OnClicked(this, &SPBRSceneLightConvertTab::OnBatchRenameChecked)
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8, 4)
		[
			SNew(SBox)
			.MinDesiredHeight(320)
			[
				SAssignNew(CandidateList, SListView<TSharedPtr<FPBRSceneLightCandidate>>)
				.ListItemsSource(&Candidates)
				.OnGenerateRow(this, &SPBRSceneLightConvertTab::OnGenerateRow)
				.SelectionMode(ESelectionMode::Multi)
				.HeaderRow(BuildHeaderRow())
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SAssignNew(ProgressText, STextBlock)
			.Text(LOCTEXT("Ready", "\u51c6\u5907\u5c31\u7eea"))
		]
	];
}

TSharedPtr<SHeaderRow> SPBRSceneLightConvertTab::BuildHeaderRow() const
{
	return SNew(SHeaderRow)
		+ SHeaderRow::Column("Check").FixedWidth(42).DefaultLabel(LOCTEXT("Check", "\u9009"))
		+ SHeaderRow::Column("Source").FillWidth(0.22f).DefaultLabel(LOCTEXT("Source", "\u539f\u5bf9\u8c61"))
		+ SHeaderRow::Column("OutputName").FillWidth(0.22f).DefaultLabel(LOCTEXT("OutputName", "\u65b0\u706f\u5149\u540d"))
		+ SHeaderRow::Column("SourceType").FillWidth(0.18f).DefaultLabel(LOCTEXT("SourceType", "\u8bc6\u522b\u7c7b\u578b"))
		+ SHeaderRow::Column("TargetType").FixedWidth(86).DefaultLabel(LOCTEXT("TargetType", "UE \u7c7b\u578b"))
		+ SHeaderRow::Column("Size").FillWidth(0.16f).DefaultLabel(LOCTEXT("Size", "\u5c3a\u5bf8/\u8303\u56f4"))
		+ SHeaderRow::Column("Status").FillWidth(0.16f).DefaultLabel(LOCTEXT("Status", "\u72b6\u6001"));
}

TSharedRef<ITableRow> SPBRSceneLightConvertTab::OnGenerateRow(TSharedPtr<FPBRSceneLightCandidate> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FPBRSceneLightCandidate>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]() { return Item.IsValid() && Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.IsEnabled(Item.IsValid() && Item->bCanConvert)
				.OnCheckStateChanged_Lambda([Item](ECheckBoxState State)
				{
					if (Item.IsValid())
					{
						Item->bChecked = State == ECheckBoxState::Checked;
					}
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.22f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->SourceName : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.22f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([Item]() { return FText::FromString(Item.IsValid() ? Item->OutputName : FString()); })
				.OnTextChanged_Lambda([Item](const FText& NewText)
				{
					if (Item.IsValid())
					{
						Item->OutputName = NewText.ToString();
					}
				})
				.OnTextCommitted_Lambda([Item](const FText& NewText, ETextCommit::Type)
				{
					if (Item.IsValid())
					{
						Item->OutputName = NewText.ToString();
					}
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->SourceTypeLabel : FString()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(SBox)
				.WidthOverride(82)
				[
					SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->TargetTypeLabel : FString()))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.16f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->SizeLabel : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.16f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->Status : FString()))
			]
		];
}

FPBRSceneLightConvertSettings SPBRSceneLightConvertTab::BuildSettings() const
{
	FPBRSceneLightConvertSettings Settings;
	Settings.bOnlySelectedActors = OnlySelectedCheck.IsValid() && OnlySelectedCheck->IsChecked();
	Settings.bUseMovableLights = !MovableLightCheck.IsValid() || MovableLightCheck->IsChecked();
	Settings.bSkipPBRStudioLights = !SkipPBRStudioCheck.IsValid() || SkipPBRStudioCheck->IsChecked();
	Settings.bClearProjectionSettings = !ClearProjectionCheck.IsValid() || ClearProjectionCheck->IsChecked();
	Settings.IntensityMultiplier = FMath::Max(0.05f, IntensityMultiplier);
	if (FolderPathBox.IsValid())
	{
		Settings.FolderPath = FName(*FolderPathBox->GetText().ToString());
	}
	return Settings;
}

void SPBRSceneLightConvertTab::RefreshList()
{
	if (CandidateList.IsValid())
	{
		CandidateList->RequestListRefresh();
	}
	UpdateSummary();
}

void SPBRSceneLightConvertTab::UpdateSummary()
{
	int32 RectCount = 0;
	int32 PointCount = 0;
	int32 SpotCount = 0;
	int32 Checked = 0;
	for (const TSharedPtr<FPBRSceneLightCandidate>& Item : Candidates)
	{
		if (!Item.IsValid())
		{
			continue;
		}
		if (Item->TargetType == EPBRSceneLightTargetType::Rect)
		{
			RectCount++;
		}
		else if (Item->TargetType == EPBRSceneLightTargetType::Point)
		{
			PointCount++;
		}
		else
		{
			SpotCount++;
		}
		if (Item->bChecked)
		{
			Checked++;
		}
	}

	if (SummaryText.IsValid())
	{
		SummaryText->SetText(FText::FromString(FString::Printf(
			TEXT("%d \u4e2a\u5019\u9009\uff0c\u77e9\u5f62 %d\uff0c\u70b9\u5149 %d\uff0c\u5c04\u706f %d\uff0c\u5df2\u9009 %d"),
			Candidates.Num(), RectCount, PointCount, SpotCount, Checked)));
	}
}

FReply SPBRSceneLightConvertTab::OnScanScene()
{
	FPBRSceneLightConverter::ScanCurrentLevel(Candidates, BuildSettings());
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("\u626b\u63cf\u5b8c\u6210: %d \u4e2a\u706f\u5149\u5019\u9009"), Candidates.Num())));
	}
	return FReply::Handled();
}

FReply SPBRSceneLightConvertTab::OnConvertChecked()
{
	FPBRSceneLightConvertResult Result;
	FPBRSceneLightConverter::ConvertCandidates(Candidates, BuildSettings(), Result);
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(Result.Messages.Num() > 0 ? Result.Messages.Last() : TEXT("\u8f6c\u6362\u5b8c\u6210")));
	}
	return FReply::Handled();
}

FReply SPBRSceneLightConvertTab::OnUndoConversion()
{
	FPBRSceneLightConvertResult Result;
	FPBRSceneLightConverter::UndoLastConversion(Result);
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(Result.Messages.Num() > 0 ? Result.Messages.Last() : TEXT("\u5df2\u64a4\u56de")));
	}
	return FReply::Handled();
}

FReply SPBRSceneLightConvertTab::OnSelectAll()
{
	for (const TSharedPtr<FPBRSceneLightCandidate>& Item : Candidates)
	{
		if (Item.IsValid() && Item->bCanConvert)
		{
			Item->bChecked = true;
		}
	}
	RefreshList();
	return FReply::Handled();
}

FReply SPBRSceneLightConvertTab::OnSelectNone()
{
	for (const TSharedPtr<FPBRSceneLightCandidate>& Item : Candidates)
	{
		if (Item.IsValid())
		{
			Item->bChecked = false;
		}
	}
	RefreshList();
	return FReply::Handled();
}

void SPBRSceneLightConvertTab::BatchRename(const TArray<TSharedPtr<FPBRSceneLightCandidate>>& Items)
{
	const FString Prefix = RenamePrefixBox.IsValid() ? RenamePrefixBox->GetText().ToString() : TEXT("PBR_");
	const FString BaseName = RenameBaseNameBox.IsValid() ? RenameBaseNameBox->GetText().ToString() : TEXT("Light");
	int32 Index = 1;
	for (const TSharedPtr<FPBRSceneLightCandidate>& Item : Items)
	{
		if (!Item.IsValid())
		{
			continue;
		}
		Item->OutputName = FString::Printf(TEXT("%s%s_%03d"), *Prefix, *BaseName, Index++);
	}
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("\u5df2\u6279\u91cf\u547d\u540d %d \u4e2a\u706f\u5149"), Index - 1)));
	}
}

FReply SPBRSceneLightConvertTab::OnBatchRenameAll()
{
	BatchRename(Candidates);
	return FReply::Handled();
}

FReply SPBRSceneLightConvertTab::OnBatchRenameSelected()
{
	TArray<TSharedPtr<FPBRSceneLightCandidate>> Selected = CandidateList.IsValid()
		? CandidateList->GetSelectedItems()
		: TArray<TSharedPtr<FPBRSceneLightCandidate>>();
	BatchRename(Selected);
	return FReply::Handled();
}

FReply SPBRSceneLightConvertTab::OnBatchRenameChecked()
{
	TArray<TSharedPtr<FPBRSceneLightCandidate>> Items;
	for (const TSharedPtr<FPBRSceneLightCandidate>& Item : Candidates)
	{
		if (Item.IsValid() && Item->bChecked)
		{
			Items.Add(Item);
		}
	}
	BatchRename(Items);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
