#pragma once

#include "CoreMinimal.h"
#include "Services/PBRSubstrateMaterialTemplateManager.h"
#include "Widgets/SCompoundWidget.h"

using FPBRSubstrateTemplateTypeOption = TSharedPtr<EPBRSubstrateTemplateType>;

class SPBRSubstrateModeTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRSubstrateModeTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnRebuildSubstrateAssets();
	FReply OnOpenTemplateAsset();
	FReply OnOpenExampleAsset();
	TSharedRef<SWidget> GenerateTemplateTypeOption(FPBRSubstrateTemplateTypeOption Option) const;
	void OnTemplateTypeSelected(FPBRSubstrateTemplateTypeOption NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedTemplateTypeText() const;
	FText GetStatusText() const;

	void SyncBrowserToAsset(const FString& AssetPath) const;
	EPBRSubstrateTemplateType GetSelectedTemplateType() const;

	TArray<FPBRSubstrateTemplateTypeOption> TemplateTypeOptions;
	FPBRSubstrateTemplateTypeOption SelectedTemplateTypeOption;
	FString Status;
};
