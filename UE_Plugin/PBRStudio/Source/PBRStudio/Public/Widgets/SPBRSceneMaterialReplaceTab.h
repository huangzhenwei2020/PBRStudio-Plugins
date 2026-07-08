#pragma once

#include "CoreMinimal.h"
#include "Services/PBRSceneMaterialReplacer.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SEditableTextBox;
class SListViewBase;
class SProgressBar;
class STableViewBase;
class STextBlock;
template <typename ItemType> class SListView;

class SPBRSceneMaterialReplaceTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRSceneMaterialReplaceTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TArray<TSharedPtr<FPBRSceneMaterialCandidate>> Candidates;
	TSharedPtr<SListView<TSharedPtr<FPBRSceneMaterialCandidate>>> CandidateList;
	TSharedPtr<STextBlock> SummaryText;
	TSharedPtr<STextBlock> ProgressText;
	TSharedPtr<SProgressBar> ReplaceProgressBar;
	TSharedPtr<SEditableTextBox> OutputRootBox;
	TSharedPtr<SEditableTextBox> RenamePrefixBox;
	TSharedPtr<SEditableTextBox> RenameBaseNameBox;
	TSharedPtr<SCheckBox> SkipPBRStudioCheck;
	TSharedPtr<SCheckBox> ShowBPRReplacementCheck;
	TSharedPtr<SCheckBox> ShowSimpleReplacementCheck;
	TSharedPtr<SCheckBox> GenerateNormalCheck;
	TSharedPtr<SCheckBox> GenerateRoughnessCheck;
	TSharedPtr<SCheckBox> GenerateMetallicCheck;
	TSharedPtr<SCheckBox> GenerateAOCheck;
	TSharedPtr<SCheckBox> GenerateSpecularCheck;
	TSharedPtr<SCheckBox> GenerateOpacityCheck;
	TSharedPtr<SCheckBox> GenerateEmissiveCheck;
	TSharedPtr<SCheckBox> GenerateHeightCheck;
	TSharedPtr<SCheckBox> GenerateORMCheck;
	float ReplaceProgress = 0.0f;
	float NormalStrength = 2.0f;
	int32 MaxTextureSize = 2048;
	bool bReplaceInProgress = false;
	bool bCancelReplaceRequested = false;
	int32 BatchedReplaceIndex = 0;
	int32 BatchedReplaceTotal = 0;
	FPBRSceneReplaceSettings BatchedReplaceSettings;
	FPBRSceneReplaceResult BatchedReplaceResult;
	TArray<TSharedPtr<FPBRSceneMaterialCandidate>> BatchedReplaceItems;

	FReply OnScanScene();
	FReply OnReplaceChecked();
	FReply OnCancelReplace();
	FReply OnUndoReplacement();
	FReply OnManualRefreshScene();
	FReply OnSelectAll();
	FReply OnSelectNone();
	FReply OnBatchRenameAll();
	FReply OnBatchRenameSelected();
	FReply OnBatchRenameChecked();
	FReply OnBatchRenameReplaceable();
	FReply OnBatchRenameMissingBaseColor();
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FPBRSceneMaterialCandidate> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedPtr<SHeaderRow> BuildHeaderRow() const;
	void BatchRename(const TArray<TSharedPtr<FPBRSceneMaterialCandidate>>& Items);
	void RefreshList();
	void UpdateSummary();
	void UpdateReplaceProgress(int32 Current, int32 Total, const FString& Status);
	EActiveTimerReturnType ProcessReplaceBatch(double InCurrentTime, float InDeltaTime);
	FPBRSceneReplaceSettings BuildSettings() const;
	EVisibility GetCandidateVisibility(TSharedPtr<FPBRSceneMaterialCandidate> Item) const;
};
