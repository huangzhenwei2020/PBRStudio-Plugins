#include "Widgets/SPBRSceneCameraConvertTab.h"

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

#define LOCTEXT_NAMESPACE "SPBRSceneCameraConvertTab"

void SPBRSceneCameraConvertTab::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(8)
		[
			SNew(STextBlock).Text(LOCTEXT("Title", "\u76f8\u673a\u8f6c\u6362"))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Hint", "\u626b\u63cf\u5bfc\u5165\u573a\u666f\u4e2d\u7684 V-Ray/Corona/Max \u76f8\u673a\uff0c\u8f6c\u4e3a UE \u7535\u5f71\u76f8\u673a\uff0c\u5e76\u6279\u91cf\u8c03\u8282\u7126\u8ddd\u3001\u5149\u5708\u3001\u66dd\u5149\u3001\u8272\u6e29\u7b49\u5e38\u7528\u53c2\u6570\u3002"))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Scan", "\u626b\u63cf\u573a\u666f\u76f8\u673a"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
				.OnClicked(this, &SPBRSceneCameraConvertTab::OnScanScene)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Convert", "\u8f6c\u6362\u52fe\u9009\u76f8\u673a"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.OnClicked(this, &SPBRSceneCameraConvertTab::OnConvertChecked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Undo", "\u64a4\u56de\u4e0a\u6b21\u8f6c\u6362"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Danger")
				.OnClicked(this, &SPBRSceneCameraConvertTab::OnUndoConversion)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton).Text(LOCTEXT("SelectAll", "\u5168\u9009")).OnClicked(this, &SPBRSceneCameraConvertTab::OnSelectAll)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton).Text(LOCTEXT("SelectNone", "\u5168\u4e0d\u9009")).OnClicked(this, &SPBRSceneCameraConvertTab::OnSelectNone)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(SummaryText, STextBlock).Text(LOCTEXT("SummaryEmpty", "\u672a\u626b\u63cf"))
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
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
				SAssignNew(HideOriginalCheck, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				[
					SNew(STextBlock).Text(LOCTEXT("HideOriginal", "\u8f6c\u6362\u540e\u9690\u85cf\u539f\u5bf9\u8c61"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
			[
				SAssignNew(SkipPBRStudioCheck, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				[
					SNew(STextBlock).Text(LOCTEXT("SkipPBRStudio", "\u8df3\u8fc7 PBRStudio \u76f8\u673a"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SAssignNew(ResetDefaultsCheck, SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				[
					SNew(STextBlock).Text(LOCTEXT("ResetDefaults", "\u8f6c\u6362\u65f6\u5957\u7528\u9ed8\u8ba4\u53c2\u6570"))
				]
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("DefaultParams", "\u9ed8\u8ba4\u53c2\u6570"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("Sensor", "\u80f6\u7247"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(1.0f).Value_Lambda([this]() { return SensorWidth; }).OnValueChanged_Lambda([this](float V) { SensorWidth = FMath::Max(1.0f, V); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("SensorX", "x"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			[
				SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(1.0f).Value_Lambda([this]() { return SensorHeight; }).OnValueChanged_Lambda([this](float V) { SensorHeight = FMath::Max(1.0f, V); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("Focal", "\u7126\u8ddd"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			[
				SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(1.0f).Value_Lambda([this]() { return FocalLength; }).OnValueChanged_Lambda([this](float V) { FocalLength = FMath::Max(1.0f, V); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("Aperture", "\u5149\u5708"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			[
				SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(0.1f).Value_Lambda([this]() { return Aperture; }).OnValueChanged_Lambda([this](float V) { Aperture = FMath::Max(0.1f, V); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("Focus", "\u5bf9\u7126cm"))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SNumericEntryBox<float>).MinDesiredValueWidth(70).MinValue(1.0f).Value_Lambda([this]() { return ManualFocusDistance; }).OnValueChanged_Lambda([this](float V) { ManualFocusDistance = FMath::Max(1.0f, V); })
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
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
				SAssignNew(RenamePrefixBox, SEditableTextBox).MinDesiredWidth(86).Text(FText::FromString(TEXT("PBR_Cine_")))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("RenameBase", "\u540d\u79f0"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SAssignNew(RenameBaseNameBox, SEditableTextBox).MinDesiredWidth(120).Text(FText::FromString(TEXT("Camera")))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(LOCTEXT("RenameAll", "\u5168\u90e8")).OnClicked(this, &SPBRSceneCameraConvertTab::OnBatchRenameAll)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(LOCTEXT("RenameSelected", "\u9009\u4e2d")).OnClicked(this, &SPBRSceneCameraConvertTab::OnBatchRenameSelected)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(LOCTEXT("RenameChecked", "\u52fe\u9009")).OnClicked(this, &SPBRSceneCameraConvertTab::OnBatchRenameChecked)
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("FolderPath", "\u5927\u7eb2\u6587\u4ef6\u5939"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SAssignNew(FolderPathBox, SEditableTextBox).Text(FText::FromString(TEXT("PBRStudio/ConvertedCameras")))
			]
		]

		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 4)
		[
			SNew(SBox)
			.MinDesiredHeight(320)
			[
				SAssignNew(CandidateList, SListView<TSharedPtr<FPBRSceneCameraCandidate>>)
				.ListItemsSource(&Candidates)
				.OnGenerateRow(this, &SPBRSceneCameraConvertTab::OnGenerateRow)
				.SelectionMode(ESelectionMode::Multi)
				.HeaderRow(BuildHeaderRow())
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SAssignNew(ProgressText, STextBlock).Text(LOCTEXT("Ready", "\u51c6\u5907\u5c31\u7eea"))
		]
	];
}

TSharedPtr<SHeaderRow> SPBRSceneCameraConvertTab::BuildHeaderRow() const
{
	return SNew(SHeaderRow)
		+ SHeaderRow::Column("Check").FixedWidth(42).DefaultLabel(LOCTEXT("Check", "\u9009"))
		+ SHeaderRow::Column("Source").FillWidth(0.24f).DefaultLabel(LOCTEXT("Source", "\u539f\u5bf9\u8c61"))
		+ SHeaderRow::Column("OutputName").FillWidth(0.26f).DefaultLabel(LOCTEXT("OutputName", "\u65b0\u76f8\u673a\u540d"))
		+ SHeaderRow::Column("SourceType").FillWidth(0.22f).DefaultLabel(LOCTEXT("SourceType", "\u8bc6\u522b\u7c7b\u578b"))
		+ SHeaderRow::Column("Status").FillWidth(0.18f).DefaultLabel(LOCTEXT("Status", "\u72b6\u6001"));
}

TSharedRef<ITableRow> SPBRSceneCameraConvertTab::OnGenerateRow(TSharedPtr<FPBRSceneCameraCandidate> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FPBRSceneCameraCandidate>>, OwnerTable)
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
			+ SHorizontalBox::Slot().FillWidth(0.24f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->SourceName : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.26f).VAlign(VAlign_Center).Padding(4, 1)
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
			+ SHorizontalBox::Slot().FillWidth(0.22f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->SourceTypeLabel : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->Status : FString()))
			]
		];
}

FPBRSceneCameraConvertSettings SPBRSceneCameraConvertTab::BuildConvertSettings() const
{
	FPBRSceneCameraConvertSettings Settings;
	Settings.bOnlySelectedActors = OnlySelectedCheck.IsValid() && OnlySelectedCheck->IsChecked();
	Settings.bHideOriginalActors = !HideOriginalCheck.IsValid() || HideOriginalCheck->IsChecked();
	Settings.bSkipPBRStudioCameras = !SkipPBRStudioCheck.IsValid() || SkipPBRStudioCheck->IsChecked();
	Settings.bResetToDefaults = !ResetDefaultsCheck.IsValid() || ResetDefaultsCheck->IsChecked();
	Settings.SensorWidth = FMath::Max(1.0f, SensorWidth);
	Settings.SensorHeight = FMath::Max(1.0f, SensorHeight);
	Settings.FocalLength = FMath::Max(1.0f, FocalLength);
	Settings.Aperture = FMath::Max(0.1f, Aperture);
	Settings.ManualFocusDistance = FMath::Max(1.0f, ManualFocusDistance);
	Settings.ExposureCompensation = ExposureCompensation;
	Settings.WhiteTemp = WhiteTemp;
	Settings.MotionBlurAmount = MotionBlurAmount;
	Settings.VignetteIntensity = VignetteIntensity;
	if (FolderPathBox.IsValid())
	{
		Settings.FolderPath = FName(*FolderPathBox->GetText().ToString());
	}
	return Settings;
}

void SPBRSceneCameraConvertTab::RefreshList()
{
	if (CandidateList.IsValid())
	{
		CandidateList->RequestListRefresh();
	}
	UpdateSummary();
}

void SPBRSceneCameraConvertTab::UpdateSummary()
{
	int32 Checked = 0;
	int32 Cine = 0;
	int32 Imported = 0;
	for (const TSharedPtr<FPBRSceneCameraCandidate>& Item : Candidates)
	{
		if (!Item.IsValid())
		{
			continue;
		}
		if (Item->bChecked)
		{
			Checked++;
		}
		if (Item->SourceType == EPBRSceneCameraSourceType::CineCamera)
		{
			Cine++;
		}
		if (Item->SourceType == EPBRSceneCameraSourceType::ImportedVRayCorona)
		{
			Imported++;
		}
	}
	if (SummaryText.IsValid())
	{
		SummaryText->SetText(FText::FromString(FString::Printf(
			TEXT("%d \u4e2a\u5019\u9009\uff0c\u7535\u5f71\u76f8\u673a %d\uff0c\u5bfc\u5165\u76f8\u673a %d\uff0c\u5df2\u9009 %d"),
			Candidates.Num(), Cine, Imported, Checked)));
	}
}

FReply SPBRSceneCameraConvertTab::OnScanScene()
{
	FPBRSceneCameraConverter::ScanCurrentLevel(Candidates, BuildConvertSettings());
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("\u626b\u63cf\u5b8c\u6210: %d \u4e2a\u76f8\u673a\u5019\u9009"), Candidates.Num())));
	}
	return FReply::Handled();
}

FReply SPBRSceneCameraConvertTab::OnConvertChecked()
{
	FPBRSceneCameraConvertResult Result;
	FPBRSceneCameraConverter::ConvertCandidates(Candidates, BuildConvertSettings(), Result);
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(Result.Messages.Num() > 0 ? Result.Messages.Last() : TEXT("\u8f6c\u6362\u5b8c\u6210")));
	}
	return FReply::Handled();
}

FReply SPBRSceneCameraConvertTab::OnUndoConversion()
{
	FPBRSceneCameraConvertResult Result;
	FPBRSceneCameraConverter::UndoLastConversion(Result);
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(Result.Messages.Num() > 0 ? Result.Messages.Last() : TEXT("\u5df2\u64a4\u56de")));
	}
	return FReply::Handled();
}

FReply SPBRSceneCameraConvertTab::OnSelectAll()
{
	for (const TSharedPtr<FPBRSceneCameraCandidate>& Item : Candidates)
	{
		if (Item.IsValid() && Item->bCanConvert)
		{
			Item->bChecked = true;
		}
	}
	RefreshList();
	return FReply::Handled();
}

FReply SPBRSceneCameraConvertTab::OnSelectNone()
{
	for (const TSharedPtr<FPBRSceneCameraCandidate>& Item : Candidates)
	{
		if (Item.IsValid())
		{
			Item->bChecked = false;
		}
	}
	RefreshList();
	return FReply::Handled();
}

void SPBRSceneCameraConvertTab::BatchRename(const TArray<TSharedPtr<FPBRSceneCameraCandidate>>& Items)
{
	const FString Prefix = RenamePrefixBox.IsValid() ? RenamePrefixBox->GetText().ToString() : TEXT("PBR_Cine_");
	const FString BaseName = RenameBaseNameBox.IsValid() ? RenameBaseNameBox->GetText().ToString() : TEXT("Camera");
	int32 Index = 1;
	for (const TSharedPtr<FPBRSceneCameraCandidate>& Item : Items)
	{
		if (Item.IsValid())
		{
			Item->OutputName = FString::Printf(TEXT("%s%s_%03d"), *Prefix, *BaseName, Index++);
		}
	}
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("\u5df2\u6279\u91cf\u547d\u540d %d \u4e2a\u76f8\u673a"), Index - 1)));
	}
}

FReply SPBRSceneCameraConvertTab::OnBatchRenameAll()
{
	BatchRename(Candidates);
	return FReply::Handled();
}

FReply SPBRSceneCameraConvertTab::OnBatchRenameSelected()
{
	TArray<TSharedPtr<FPBRSceneCameraCandidate>> Selected = CandidateList.IsValid()
		? CandidateList->GetSelectedItems()
		: TArray<TSharedPtr<FPBRSceneCameraCandidate>>();
	BatchRename(Selected);
	return FReply::Handled();
}

FReply SPBRSceneCameraConvertTab::OnBatchRenameChecked()
{
	TArray<TSharedPtr<FPBRSceneCameraCandidate>> Items;
	for (const TSharedPtr<FPBRSceneCameraCandidate>& Item : Candidates)
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
