#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnNavTabChanged, int32);

class SPBRSideNav : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRSideNav) {}
		SLATE_EVENT(FOnNavTabChanged, OnTabChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetActiveTab(int32 TabIndex);

private:
	TSharedRef<SWidget> MakeNavButton(const FString& Icon, const FString& Title, int32 Index);

	FOnNavTabChanged OnTabChanged;
	TArray<TSharedPtr<SButton>> NavButtons;
	int32 ActiveIndex = 0;
};
