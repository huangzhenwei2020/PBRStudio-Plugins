#include "Widgets/SPBRSceneMaterialReplaceTab.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SPBRSceneMaterialReplaceTab"

void SPBRSceneMaterialReplaceTab::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "场景材质替换"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Hint", "扫描当前关卡材质，保留基础颜色贴图，生成 PBR 套图并替换原材质。"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Scan", "扫描当前场景"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
				.OnClicked(this, &SPBRSceneMaterialReplaceTab::OnScanScene)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Replace", "一键生成并替换"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.OnClicked(this, &SPBRSceneMaterialReplaceTab::OnReplaceChecked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("CancelReplace", "取消转换"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Warning")
				.IsEnabled_Lambda([this]() { return bReplaceInProgress; })
				.OnClicked(this, &SPBRSceneMaterialReplaceTab::OnCancelReplace)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Undo", "撤回上次替换"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Danger")
				.OnClicked(this, &SPBRSceneMaterialReplaceTab::OnUndoReplacement)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("ManualRefresh", "手动更新场景"))
				.ToolTipText(LOCTEXT("ManualRefreshTip", "替换后视口仍显示棋盘格时，手动刷新当前关卡的材质显示。"))
				.OnClicked(this, &SPBRSceneMaterialReplaceTab::OnManualRefreshScene)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectAll", "全选"))
				.OnClicked(this, &SPBRSceneMaterialReplaceTab::OnSelectAll)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectNone", "全不选"))
				.OnClicked(this, &SPBRSceneMaterialReplaceTab::OnSelectNone)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(SummaryText, STextBlock)
				.Text(LOCTEXT("SummaryEmpty", "未扫描"))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("BatchRename", "批量命名"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("RenamePrefix", "前缀"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SAssignNew(RenamePrefixBox, SEditableTextBox)
				.MinDesiredWidth(90)
				.Text(FText::FromString(TEXT("MI_PBRSR_")))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("RenameBase", "名称"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SAssignNew(RenameBaseNameBox, SEditableTextBox)
				.MinDesiredWidth(120)
				.Text(FText::FromString(TEXT("Material")))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(LOCTEXT("RenameAll", "全部")).OnClicked(this, &SPBRSceneMaterialReplaceTab::OnBatchRenameAll)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(LOCTEXT("RenameSelected", "选择")).OnClicked(this, &SPBRSceneMaterialReplaceTab::OnBatchRenameSelected)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(LOCTEXT("RenameChecked", "打勾")).OnClicked(this, &SPBRSceneMaterialReplaceTab::OnBatchRenameChecked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(LOCTEXT("RenameReplaceable", "可替换")).OnClicked(this, &SPBRSceneMaterialReplaceTab::OnBatchRenameReplaceable)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(LOCTEXT("RenameMissingBase", "无基础色")).OnClicked(this, &SPBRSceneMaterialReplaceTab::OnBatchRenameMissingBaseColor)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("OutputRoot", "输出目录"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SAssignNew(OutputRootBox, SEditableTextBox)
				.Text(FText::FromString(TEXT("/Game/PBRStudio/SceneReplaced")))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
				[
					SAssignNew(SkipPBRStudioCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState) { RefreshList(); })
					[
						SNew(STextBlock).Text(LOCTEXT("SkipGenerated", "跳过PBRStudio材质"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
				[
					SAssignNew(ShowBPRReplacementCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState) { RefreshList(); })
					[
						SNew(STextBlock).Text(LOCTEXT("ShowBPRReplacement", "显示BPR替换"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
				[
					SAssignNew(ShowSimpleReplacementCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState) { RefreshList(); })
					[
						SNew(STextBlock).Text(LOCTEXT("ShowSimpleReplacement", "显示普通替换"))
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 6, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("ChannelList", "生成通道"))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SNew(SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					.IsEnabled(false)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateBaseColor", "基础色"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SAssignNew(GenerateNormalCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateNormal", "法线"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SAssignNew(GenerateRoughnessCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateRoughness", "粗糙度"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SAssignNew(GenerateMetallicCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateMetallic", "金属"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SAssignNew(GenerateAOCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Unchecked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateAO", "AO"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SAssignNew(GenerateSpecularCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Unchecked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateSpecular", "高光"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SAssignNew(GenerateOpacityCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateOpacity", "透明"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SAssignNew(GenerateEmissiveCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateEmissive", "自发光"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
				[
					SAssignNew(GenerateHeightCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Unchecked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateHeight", "高度"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(GenerateORMCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Unchecked)
					[
						SNew(STextBlock).Text(LOCTEXT("GenerateORM", "ORM"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("NormalStrength", "法线强度"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SNumericEntryBox<float>)
					.MinDesiredValueWidth(58)
					.MinValue(0.0f)
					.MaxValue(8.0f)
					.Value_Lambda([this]() { return NormalStrength; })
					.OnValueChanged_Lambda([this](float Value)
					{
						NormalStrength = FMath::Clamp(Value, 0.0f, 8.0f);
					})
					.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type)
					{
						NormalStrength = FMath::Clamp(Value, 0.0f, 8.0f);
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("MaxTextureSize", "最大贴图尺寸"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SNumericEntryBox<int32>)
					.MinDesiredValueWidth(70)
					.MinValue(1)
					.MaxValue(16384)
					.Value_Lambda([this]() { return MaxTextureSize; })
					.OnValueChanged_Lambda([this](int32 Value)
					{
						MaxTextureSize = FMath::Clamp(Value, 1, 16384);
					})
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type)
					{
						MaxTextureSize = FMath::Clamp(Value, 1, 16384);
					})
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8, 4)
		[
			SNew(SBox)
			.MinDesiredHeight(320)
			[
				SAssignNew(CandidateList, SListView<TSharedPtr<FPBRSceneMaterialCandidate>>)
				.ListItemsSource(&Candidates)
				.OnGenerateRow(this, &SPBRSceneMaterialReplaceTab::OnGenerateRow)
				.SelectionMode(ESelectionMode::Single)
				.HeaderRow(BuildHeaderRow())
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(ReplaceProgressBar, SProgressBar)
				.Percent_Lambda([this]() -> TOptional<float>
				{
					return ReplaceProgress;
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4, 0, 0)
			[
				SAssignNew(ProgressText, STextBlock)
				.Text(LOCTEXT("Ready", "准备就绪"))
			]
		]
	];
}

TSharedPtr<SHeaderRow> SPBRSceneMaterialReplaceTab::BuildHeaderRow() const
{
	return SNew(SHeaderRow)
		+ SHeaderRow::Column("Check").FixedWidth(42).DefaultLabel(LOCTEXT("Check", "选"))
		+ SHeaderRow::Column("Material").FillWidth(0.22f).DefaultLabel(LOCTEXT("Material", "原材质"))
		+ SHeaderRow::Column("OutputName").FillWidth(0.22f).DefaultLabel(LOCTEXT("OutputName", "输出名称"))
		+ SHeaderRow::Column("Mode").FixedWidth(92).DefaultLabel(LOCTEXT("Mode", "替换方式"))
		+ SHeaderRow::Column("Texture").FillWidth(0.22f).DefaultLabel(LOCTEXT("Texture", "基础颜色贴图"))
		+ SHeaderRow::Column("Slots").FixedWidth(64).DefaultLabel(LOCTEXT("Slots", "槽位"))
		+ SHeaderRow::Column("Status").FillWidth(0.24f).DefaultLabel(LOCTEXT("Status", "状态"));
}

TSharedRef<ITableRow> SPBRSceneMaterialReplaceTab::OnGenerateRow(TSharedPtr<FPBRSceneMaterialCandidate> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString TextureName = Item.IsValid() && Item->BaseColorTexture.IsValid()
		? Item->BaseColorTexture->GetName()
		: TEXT("-");
	return SNew(STableRow<TSharedPtr<FPBRSceneMaterialCandidate>>, OwnerTable)
		.Visibility(this, &SPBRSceneMaterialReplaceTab::GetCandidateVisibility, Item)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]() { return Item.IsValid() && Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.IsEnabled(Item.IsValid() && Item->bCanReplace)
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
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->MaterialName : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.24f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([Item]() { return FText::FromString(Item.IsValid() ? Item->OutputMaterialName : FString()); })
				.OnTextChanged_Lambda([Item](const FText& NewText)
				{
					if (Item.IsValid())
					{
						Item->OutputMaterialName = NewText.ToString();
					}
				})
				.OnTextCommitted_Lambda([Item](const FText& NewText, ETextCommit::Type)
				{
					if (Item.IsValid())
					{
						Item->OutputMaterialName = NewText.ToString();
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(SBox)
				.WidthOverride(88)
				[
					SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->ReplaceMode : FString()))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.24f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(TextureName))
			]
			+ SHorizontalBox::Slot().FillWidth(0.10f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::AsNumber(Item.IsValid() ? Item->Slots.Num() : 0))
			]
			+ SHorizontalBox::Slot().FillWidth(0.20f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->Status : FString()))
			]
		];
}

EVisibility SPBRSceneMaterialReplaceTab::GetCandidateVisibility(TSharedPtr<FPBRSceneMaterialCandidate> Item) const
{
	if (!Item.IsValid())
	{
		return EVisibility::Collapsed;
	}
	if (SkipPBRStudioCheck.IsValid() && SkipPBRStudioCheck->IsChecked() && Item->bIsPBRStudioMaterial)
	{
		return EVisibility::Collapsed;
	}
	const bool bShowBPR = !ShowBPRReplacementCheck.IsValid() || ShowBPRReplacementCheck->IsChecked();
	const bool bShowSimple = !ShowSimpleReplacementCheck.IsValid() || ShowSimpleReplacementCheck->IsChecked();
	if (Item->bCanReplace && Item->bUseBPRReplacement && !bShowBPR)
	{
		return EVisibility::Collapsed;
	}
	if (Item->bCanReplace && !Item->bUseBPRReplacement && !bShowSimple)
	{
		return EVisibility::Collapsed;
	}
	return EVisibility::Visible;
}

void SPBRSceneMaterialReplaceTab::RefreshList()
{
	if (CandidateList.IsValid())
	{
		CandidateList->RequestListRefresh();
	}
	UpdateSummary();
}

void SPBRSceneMaterialReplaceTab::UpdateSummary()
{
	int32 Replaceable = 0;
	int32 BPRReplace = 0;
	int32 SimpleReplace = 0;
	int32 NotReplaceable = 0;
	int32 Checked = 0;
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Item : Candidates)
	{
		if (!Item.IsValid())
		{
			continue;
		}

		if (Item->bCanReplace)
		{
			Replaceable++;
			if (Item->bUseBPRReplacement)
			{
				BPRReplace++;
			}
			else
			{
				SimpleReplace++;
			}
			if (Item->bChecked)
			{
				Checked++;
			}
		}
		else
		{
			NotReplaceable++;
		}
	}
	if (SummaryText.IsValid())
	{
		SummaryText->SetText(FText::FromString(FString::Printf(TEXT("%d 个材质，BPR %d，普通 %d，不可替换 %d，已选择 %d"), Candidates.Num(), BPRReplace, SimpleReplace, NotReplaceable, Checked)));
	}
}

FReply SPBRSceneMaterialReplaceTab::OnScanScene()
{
	FPBRSceneMaterialReplacer::ScanCurrentLevel(Candidates);
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("扫描完成: %d 个材质"), Candidates.Num())));
	}
	return FReply::Handled();
}

FPBRSceneReplaceSettings SPBRSceneMaterialReplaceTab::BuildSettings() const
{
	FPBRSceneReplaceSettings Settings;
	if (OutputRootBox.IsValid())
	{
		Settings.OutputRoot = OutputRootBox->GetText().ToString();
	}
	Settings.NormalStrength = FMath::Clamp(NormalStrength, 0.0f, 8.0f);
	Settings.OutputSize = FMath::Clamp(MaxTextureSize, 1, 16384);
	Settings.bGenerateHeight = GenerateHeightCheck.IsValid() && GenerateHeightCheck->IsChecked();
	Settings.bGenerateORM = GenerateORMCheck.IsValid() && GenerateORMCheck->IsChecked();
	Settings.bGenerateNormal = !GenerateNormalCheck.IsValid() || GenerateNormalCheck->IsChecked();
	Settings.bGenerateRoughness = !GenerateRoughnessCheck.IsValid() || GenerateRoughnessCheck->IsChecked();
	Settings.bGenerateMetallic = !GenerateMetallicCheck.IsValid() || GenerateMetallicCheck->IsChecked();
	Settings.bGenerateAO = !GenerateAOCheck.IsValid() || GenerateAOCheck->IsChecked();
	Settings.bGenerateSpecular = GenerateSpecularCheck.IsValid() && GenerateSpecularCheck->IsChecked();
	Settings.bGenerateOpacity = GenerateOpacityCheck.IsValid() && GenerateOpacityCheck->IsChecked();
	Settings.bGenerateEmissive = !GenerateEmissiveCheck.IsValid() || GenerateEmissiveCheck->IsChecked();
	return Settings;
}

void SPBRSceneMaterialReplaceTab::UpdateReplaceProgress(int32 Current, int32 Total, const FString& Status)
{
	ReplaceProgress = Total > 0 ? FMath::Clamp(static_cast<float>(Current) / static_cast<float>(Total), 0.0f, 1.0f) : 0.0f;
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(Status));
	}
	if (ReplaceProgressBar.IsValid())
	{
		ReplaceProgressBar->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SPBRSceneMaterialReplaceTab::AddBatchedReplacePackage(UPackage* Package)
{
	if (!Package)
	{
		return;
	}

	for (const TWeakObjectPtr<UPackage>& ExistingPackage : BatchedReplacePackagesToSave)
	{
		if (ExistingPackage.Get() == Package)
		{
			return;
		}
	}
	BatchedReplacePackagesToSave.Add(Package);
}

void SPBRSceneMaterialReplaceTab::SavePendingBatchedReplacePackages(const FString& StatusPrefix)
{
	if (ProgressText.IsValid() && !BatchedReplacePackagesToSave.IsEmpty())
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("%s，已生成 %d 个未保存材质包，请转换完成后使用保存所有"), *StatusPrefix, BatchedReplacePackagesToSave.Num())));
	}
	BatchedReplacePackagesToSave.Reset();
}

FReply SPBRSceneMaterialReplaceTab::OnReplaceChecked()
{
	if (bReplaceInProgress)
	{
		if (ProgressText.IsValid())
		{
			ProgressText->SetText(LOCTEXT("ReplaceAlreadyRunning", "正在分批转换，请等待当前批次完成"));
		}
		return FReply::Handled();
	}

	BatchedReplaceItems.Reset();
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Candidate : Candidates)
	{
		if (Candidate.IsValid() && Candidate->bCanReplace && Candidate->bChecked && !Candidate->bIsPBRStudioMaterial)
		{
			BatchedReplaceItems.Add(Candidate);
		}
	}

	if (BatchedReplaceItems.IsEmpty())
	{
		if (ProgressText.IsValid())
		{
			ProgressText->SetText(LOCTEXT("NoReplaceItems", "没有需要转换的材质"));
		}
		return FReply::Handled();
	}

	BatchedReplaceSettings = BuildSettings();
	BatchedReplaceSettings.bSaveGeneratedAssets = false;
	BatchedReplaceSettings.bBakeComplexMaterialChannels = true;
	BatchedReplaceSettings.GeneratedPackageCallback = [this](UPackage* Package)
	{
		AddBatchedReplacePackage(Package);
	};
	BatchedReplaceResult = FPBRSceneReplaceResult();
	BatchedReplaceIndex = 0;
	BatchedReplaceTotal = BatchedReplaceItems.Num();
	BatchedReplaceLastSaveIndex = 0;
	BatchedReplacePackagesToSave.Reset();
	bReplaceInProgress = true;
	bCancelReplaceRequested = false;
	ReplaceProgress = 0.0f;

	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("准备分批转换 %d 个材质"), BatchedReplaceTotal)));
	}
	RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateSP(this, &SPBRSceneMaterialReplaceTab::ProcessReplaceBatch));
	return FReply::Handled();
}

FReply SPBRSceneMaterialReplaceTab::OnCancelReplace()
{
	if (bReplaceInProgress)
	{
		bCancelReplaceRequested = true;
		if (ProgressText.IsValid())
		{
			ProgressText->SetText(LOCTEXT("ReplaceCancelRequested", "已请求取消，当前材质处理完成后停止"));
		}
	}
	return FReply::Handled();
}

EActiveTimerReturnType SPBRSceneMaterialReplaceTab::ProcessReplaceBatch(double InCurrentTime, float InDeltaTime)
{
	if (!bReplaceInProgress)
	{
		return EActiveTimerReturnType::Stop;
	}

	if (bCancelReplaceRequested)
	{
		const int32 UnsavedPackageCount = BatchedReplacePackagesToSave.Num();
		SavePendingBatchedReplacePackages(TEXT("正在取消转换"));
		bReplaceInProgress = false;
		bCancelReplaceRequested = false;
		BatchedReplaceItems.Reset();
		if (ProgressText.IsValid())
		{
			ProgressText->SetText(FText::FromString(FString::Printf(TEXT("已取消转换，已处理 %d/%d，%d 个材质包未自动保存"), BatchedReplaceIndex, BatchedReplaceTotal, UnsavedPackageCount)));
		}
		RefreshList();
		return EActiveTimerReturnType::Stop;
	}

	if (!BatchedReplaceItems.IsValidIndex(BatchedReplaceIndex))
	{
		const int32 UnsavedPackageCount = BatchedReplacePackagesToSave.Num();
		SavePendingBatchedReplacePackages(TEXT("分批转换完成"));
		bReplaceInProgress = false;
		bCancelReplaceRequested = false;
		ReplaceProgress = 1.0f;
		BatchedReplaceItems.Reset();
		RefreshList();
		if (ProgressText.IsValid())
		{
			ProgressText->SetText(FText::FromString(FString::Printf(TEXT("分批转换完成，已生成 %d 个材质包，请使用保存所有写入磁盘"), UnsavedPackageCount)));
		}
		return EActiveTimerReturnType::Stop;
	}

	TSharedPtr<FPBRSceneMaterialCandidate> CurrentItem = BatchedReplaceItems[BatchedReplaceIndex];
	const int32 DisplayIndex = BatchedReplaceIndex + 1;
	const FString CurrentName = CurrentItem.IsValid() ? CurrentItem->MaterialName : FString(TEXT("无效材质"));
	UpdateReplaceProgress(BatchedReplaceIndex, BatchedReplaceTotal, FString::Printf(TEXT("正在转换 %d/%d: %s"), DisplayIndex, BatchedReplaceTotal, *CurrentName));
	UE_LOG(LogTemp, Display, TEXT("PBRStudio: Scene material replace batch %d/%d: %s"), DisplayIndex, BatchedReplaceTotal, *CurrentName);

	TArray<TSharedPtr<FPBRSceneMaterialCandidate>> SingleItemBatch;
	SingleItemBatch.Add(CurrentItem);
	FPBRSceneReplaceResult BatchResult;
	FPBRSceneReplaceSettings SingleSettings = BatchedReplaceSettings;
	SingleSettings.ProgressCallback = nullptr;
	FPBRSceneMaterialReplacer::ReplaceCandidates(SingleItemBatch, SingleSettings, BatchResult);

	BatchedReplaceResult.ScannedMaterials += BatchResult.ScannedMaterials;
	BatchedReplaceResult.ReplaceableMaterials += BatchResult.ReplaceableMaterials;
	BatchedReplaceResult.ReplacedMaterials += BatchResult.ReplacedMaterials;
	BatchedReplaceResult.ReplacedSlots += BatchResult.ReplacedSlots;
	BatchedReplaceResult.Messages.Append(BatchResult.Messages);

	++BatchedReplaceIndex;
	UpdateReplaceProgress(BatchedReplaceIndex, BatchedReplaceTotal, FString::Printf(TEXT("已处理 %d/%d: %s"), BatchedReplaceIndex, BatchedReplaceTotal, *CurrentName));
	return EActiveTimerReturnType::Continue;
}

FReply SPBRSceneMaterialReplaceTab::OnManualRefreshScene()
{
	ReplaceProgress = 0.0f;
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(LOCTEXT("ManualRefreshing", "正在手动更新场景材质显示..."));
	}
	FPBRSceneMaterialReplacer::RefreshCurrentLevelMaterialAssignments();
	ReplaceProgress = 1.0f;
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(LOCTEXT("ManualRefreshDone", "已手动更新场景材质显示"));
	}
	return FReply::Handled();
}

FReply SPBRSceneMaterialReplaceTab::OnUndoReplacement()
{
	FPBRSceneReplaceResult Result;
	FPBRSceneMaterialReplacer::UndoLastReplacement(Result);
	FPBRSceneMaterialReplacer::ScanCurrentLevel(Candidates);
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(Result.Messages.Num() > 0 ? Result.Messages.Last() : TEXT("已撤回")));
	}
	return FReply::Handled();
}

FReply SPBRSceneMaterialReplaceTab::OnSelectAll()
{
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Item : Candidates)
	{
		if (Item.IsValid() && Item->bCanReplace)
		{
			Item->bChecked = true;
		}
	}
	RefreshList();
	return FReply::Handled();
}

FReply SPBRSceneMaterialReplaceTab::OnSelectNone()
{
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Item : Candidates)
	{
		if (Item.IsValid())
		{
			Item->bChecked = false;
		}
	}
	RefreshList();
	return FReply::Handled();
}

void SPBRSceneMaterialReplaceTab::BatchRename(const TArray<TSharedPtr<FPBRSceneMaterialCandidate>>& Items)
{
	const FString Prefix = RenamePrefixBox.IsValid() ? RenamePrefixBox->GetText().ToString() : TEXT("MI_PBRSR_");
	const FString BaseName = RenameBaseNameBox.IsValid() ? RenameBaseNameBox->GetText().ToString() : TEXT("Material");
	int32 Index = 1;
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Item : Items)
	{
		if (!Item.IsValid())
		{
			continue;
		}
		Item->OutputMaterialName = FString::Printf(TEXT("%s%s_%03d"), *Prefix, *BaseName, Index++);
	}
	RefreshList();
	if (ProgressText.IsValid())
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("已批量命名 %d 个材质"), Index - 1)));
	}
}

FReply SPBRSceneMaterialReplaceTab::OnBatchRenameAll()
{
	BatchRename(Candidates);
	return FReply::Handled();
}

FReply SPBRSceneMaterialReplaceTab::OnBatchRenameSelected()
{
	TArray<TSharedPtr<FPBRSceneMaterialCandidate>> Selected = CandidateList.IsValid()
		? CandidateList->GetSelectedItems()
		: TArray<TSharedPtr<FPBRSceneMaterialCandidate>>();
	BatchRename(Selected);
	return FReply::Handled();
}

FReply SPBRSceneMaterialReplaceTab::OnBatchRenameChecked()
{
	TArray<TSharedPtr<FPBRSceneMaterialCandidate>> Items;
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Item : Candidates)
	{
		if (Item.IsValid() && Item->bChecked)
		{
			Items.Add(Item);
		}
	}
	BatchRename(Items);
	return FReply::Handled();
}

FReply SPBRSceneMaterialReplaceTab::OnBatchRenameReplaceable()
{
	TArray<TSharedPtr<FPBRSceneMaterialCandidate>> Items;
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Item : Candidates)
	{
		if (Item.IsValid() && Item->bCanReplace)
		{
			Items.Add(Item);
		}
	}
	BatchRename(Items);
	return FReply::Handled();
}

FReply SPBRSceneMaterialReplaceTab::OnBatchRenameMissingBaseColor()
{
	TArray<TSharedPtr<FPBRSceneMaterialCandidate>> Items;
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Item : Candidates)
	{
		if (Item.IsValid() && !Item->BaseColorTexture.IsValid())
		{
			Items.Add(Item);
		}
	}
	BatchRename(Items);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
