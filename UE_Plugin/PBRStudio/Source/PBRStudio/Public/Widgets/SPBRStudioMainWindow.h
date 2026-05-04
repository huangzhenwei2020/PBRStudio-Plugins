#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SPBRStudioMainWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRStudioMainWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildStatusBar();
	EVisibility GetSideNavVisibility() const;

	TSharedPtr<class SPBRSideNav> SideNavWidget;
	TSharedPtr<class SWidgetSwitcher> ContentSwitcher;
	TSharedPtr<class STextBlock> StatusText;
	bool bTextureSuiteCompactMode = false;
};
