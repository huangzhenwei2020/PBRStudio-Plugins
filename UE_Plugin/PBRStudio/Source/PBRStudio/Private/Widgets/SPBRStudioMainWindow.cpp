#include "Widgets/SPBRStudioMainWindow.h"
#include "Widgets/SPBRSideNav.h"
#include "Widgets/SPBRTextureSuiteTab.h"
#include "Widgets/SPBRDownloadLibraryTab.h"
#include "Widgets/SPBRTextureStreamingTab.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SPBRStudioMainWindow"

void SPBRStudioMainWindow::Construct(const FArguments& InArgs)
{
	TSharedPtr<SPBRTextureSuiteTab> TextureSuiteTab;

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0)
		[
			BuildTopBar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0)
			[
				SAssignNew(SideNavWidget, SPBRSideNav)
				.Visibility(this, &SPBRStudioMainWindow::GetSideNavVisibility)
				.OnTabChanged_Lambda([this](int32 Index)
				{
					if (ContentSwitcher.IsValid())
					{
						ContentSwitcher->SetActiveWidgetIndex(Index);
					}
				})
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0)
			[
				SAssignNew(ContentSwitcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(TextureSuiteTab, SPBRTextureSuiteTab)
					.OnCompactModeChanged_Lambda([this](bool bCompact)
					{
						bTextureSuiteCompactMode = bCompact;
					})
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SPBRDownloadLibraryTab)
						.OnLibrarySentToTextureSuite_Lambda([TextureSuiteTab]()
						{
							if (TextureSuiteTab.IsValid())
							{
								TextureSuiteTab->LoadExternalLibraryFolderAndScan();
							}
						})
					]
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SPBRTextureStreamingTab)
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0)
		[
			BuildStatusBar()
		]
	];
}

EVisibility SPBRStudioMainWindow::GetSideNavVisibility() const
{
	return bTextureSuiteCompactMode ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<SWidget> SPBRStudioMainWindow::BuildTopBar()
{
	return SNew(SBox)
		.MinDesiredHeight(32.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "PBR Studio"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
		];
}

TSharedRef<SWidget> SPBRStudioMainWindow::BuildStatusBar()
{
	return SNew(SBox)
		.HeightOverride(24.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8, 0)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(LOCTEXT("StatusReady", "Ready"))
			]
		];
}

#undef LOCTEXT_NAMESPACE
