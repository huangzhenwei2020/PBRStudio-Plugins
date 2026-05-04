#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Views/SListView.h"
#include "Models/PBRTextureEntry.h"

class SPBRTextureStreamingTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRTextureStreamingTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnChooseOutputDir();
	FReply OnScanSelected();
	void OnProcessChecked(bool bForce);
	FReply OnRelinkMaterials();
	FReply OnClearList();
	FReply OnRemoveMissing();

	TSharedRef<class SHeaderRow> BuildHeaderRow();
	TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<FPBRTextureEntry> Item, const TSharedRef<STableViewBase>& Owner);
	void RefreshTree();

	TSharedPtr<class SEditableTextBox> OutputDirBox;
	TSharedPtr<SSpinBox<int32>> MaxSizeSpin;
	TSharedPtr<class SCheckBox> RequireP2Check;
	TSharedPtr<class SCheckBox> UENamingCheck;
	TSharedPtr<class SCheckBox> OnlyProblemCheck;
	TSharedPtr<SListView<TSharedPtr<FPBRTextureEntry>>> TextureTree;
	TSharedPtr<class STextBlock> CountText;
	TSharedPtr<class STextBlock> ProgressText;

	TArray<TSharedPtr<FPBRTextureEntry>> TextureEntries;
};
