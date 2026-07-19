#pragma once

#include "CoreMinimal.h"
#include "Services/PBRSceneCameraConverter.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SEditableTextBox;
class SHeaderRow;
class STableViewBase;
class STextBlock;
template <typename ItemType> class SListView;

class SPBRSceneCameraConvertTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRSceneCameraConvertTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TArray<TSharedPtr<FPBRSceneCameraCandidate>> Candidates;
	TSharedPtr<SListView<TSharedPtr<FPBRSceneCameraCandidate>>> CandidateList;
	TSharedPtr<STextBlock> SummaryText;
	TSharedPtr<STextBlock> ProgressText;
	TSharedPtr<SCheckBox> OnlySelectedCheck;
	TSharedPtr<SCheckBox> HideOriginalCheck;
	TSharedPtr<SCheckBox> SkipPBRStudioCheck;
	TSharedPtr<SCheckBox> ResetDefaultsCheck;
	TSharedPtr<SEditableTextBox> RenamePrefixBox;
	TSharedPtr<SEditableTextBox> RenameBaseNameBox;
	TSharedPtr<SEditableTextBox> FolderPathBox;

	float SensorWidth = 36.0f;
	float SensorHeight = 20.25f;
	float FocalLength = 35.0f;
	float Aperture = 5.6f;
	float ManualFocusDistance = 1000.0f;
	float ExposureCompensation = 0.0f;
	float WhiteTemp = 6500.0f;
	float MotionBlurAmount = 0.0f;
	float VignetteIntensity = 0.0f;

	FReply OnScanScene();
	FReply OnConvertChecked();
	FReply OnUndoConversion();
	FReply OnSelectAll();
	FReply OnSelectNone();
	FReply OnBatchRenameAll();
	FReply OnBatchRenameSelected();
	FReply OnBatchRenameChecked();
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FPBRSceneCameraCandidate> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedPtr<SHeaderRow> BuildHeaderRow() const;
	FPBRSceneCameraConvertSettings BuildConvertSettings() const;
	void BatchRename(const TArray<TSharedPtr<FPBRSceneCameraCandidate>>& Items);
	void RefreshList();
	void UpdateSummary();
};
