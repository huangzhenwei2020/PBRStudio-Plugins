#pragma once

#include "CoreMinimal.h"
#include "Services/PBRSceneLightConverter.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SEditableTextBox;
class SHeaderRow;
class STableViewBase;
class STextBlock;
template <typename ItemType> class SListView;

class SPBRSceneLightConvertTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRSceneLightConvertTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TArray<TSharedPtr<FPBRSceneLightCandidate>> Candidates;
	TSharedPtr<SListView<TSharedPtr<FPBRSceneLightCandidate>>> CandidateList;
	TSharedPtr<STextBlock> SummaryText;
	TSharedPtr<STextBlock> ProgressText;
	TSharedPtr<SCheckBox> OnlySelectedCheck;
	TSharedPtr<SCheckBox> MovableLightCheck;
	TSharedPtr<SCheckBox> SkipPBRStudioCheck;
	TSharedPtr<SCheckBox> ClearProjectionCheck;
	TSharedPtr<SEditableTextBox> RenamePrefixBox;
	TSharedPtr<SEditableTextBox> RenameBaseNameBox;
	TSharedPtr<SEditableTextBox> FolderPathBox;
	float IntensityMultiplier = 1.0f;

	FReply OnScanScene();
	FReply OnConvertChecked();
	FReply OnUndoConversion();
	FReply OnSelectAll();
	FReply OnSelectNone();
	FReply OnBatchRenameAll();
	FReply OnBatchRenameSelected();
	FReply OnBatchRenameChecked();
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FPBRSceneLightCandidate> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedPtr<SHeaderRow> BuildHeaderRow() const;
	FPBRSceneLightConvertSettings BuildSettings() const;
	void BatchRename(const TArray<TSharedPtr<FPBRSceneLightCandidate>>& Items);
	void RefreshList();
	void UpdateSummary();
};
