#include "Widgets/SPBRDownloadLibraryTab.h"
#include "Services/PBRDownloadManager.h"
#include "Services/PBRDataStore.h"
#include "Services/PBRHttpServer.h"

#include "DesktopPlatformModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDesktopPlatform.h"
#include "Input/DragAndDrop.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SPBRDownloadLibraryTab"

void SPBRDownloadLibraryTab::Construct(const FArguments& InArgs)
{
	OnLibrarySentToTextureSuite = InArgs._OnLibrarySentToTextureSuite;
	OnLibrarySentToSpecialMaterials = InArgs._OnLibrarySentToSpecialMaterials;
	DownloadManager = MakeShareable(new FPBRDownloadManager);
	HttpServer = MakeShareable(new FPBRHttpServer);
	LoadLibraryHistory();

	TSharedPtr<FJsonObject> Config;
	if (FPBRDataStore::LoadConfig(Config) && Config.IsValid())
	{
		FString LastFolder;
		if (Config->TryGetStringField(TEXT("texture_suite_last_folder"), LastFolder) && !LastFolder.IsEmpty())
		{
			DownloadManager->SetMaterialLibraryDir(LastFolder);
		}
	}

	HttpServer->OnPBRPush.BindLambda([this](const TArray<FString>& URLs, bool bAutoStart)
	{
		for (const FString& URL : URLs)
		{
			DownloadManager->AddToQueue(URL, FString(), TEXT("Chrome推送"));
		}
		RefreshQueue();
		if (bAutoStart)
		{
			DownloadManager->DownloadAllPending();
			RefreshQueue();
		}
	});

	DownloadManager->OnQueueChanged.BindRaw(this, &SPBRDownloadLibraryTab::RefreshQueue);
	FPlatformApplicationMisc::ClipboardPaste(LastClipboardText);

	RefreshSites();
	RefreshQueue();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "PBR 下载库 - 下载、解压、整理材质库，不直接导入 UE"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[ SNew(STextBlock).Text(LOCTEXT("Lib", "材质库")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SAssignNew(LibraryPathBox, SEditableTextBox)
				.Text(FText::FromString(DownloadManager->GetMaterialLibraryDir()))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SAssignNew(LibraryHistoryComboBox, SComboBox<FStringOption>)
				.OptionsSource(&LibraryHistoryOptions)
				.OnGenerateWidget(this, &SPBRDownloadLibraryTab::GenerateLibraryHistoryOption)
				.OnSelectionChanged(this, &SPBRDownloadLibraryTab::OnLibraryHistorySelected)
				[
					SNew(STextBlock).Text(LOCTEXT("LibraryHistory", "历史"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton).Text(LOCTEXT("BrowseLib", "选择"))
				.OnClicked(this, &SPBRDownloadLibraryTab::OnBrowseLibrary)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton).Text(LOCTEXT("OpenLib", "打开"))
				.OnClicked(this, &SPBRDownloadLibraryTab::OnOpenLibrary)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton).Text(LOCTEXT("ScanToPBRSuite", "用于 PBR 套件"))
				.OnClicked(this, &SPBRDownloadLibraryTab::OnScanLibraryToPBRSuite)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton).Text(LOCTEXT("ScanToSpecialMaterials", "用于特殊材质"))
				.OnClicked(this, &SPBRDownloadLibraryTab::OnScanLibraryToSpecialMaterials)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[ SNew(STextBlock).Text(LOCTEXT("BridgeTitle", "Chrome 推送到 UE")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[ SNew(STextBlock).Text(LOCTEXT("Port", "端口")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SAssignNew(PortSpin, SSpinBox<int32>)
				.MinValue(1024)
				.MaxValue(65535)
				.Value(19528)
				.MinDesiredWidth(80)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Start", "启动"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.OnClicked(this, &SPBRDownloadLibraryTab::OnStartServer)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Stop", "停止"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Danger")
				.OnClicked(this, &SPBRDownloadLibraryTab::OnStopServer)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(ServerStatusText, STextBlock)
				.Text(this, &SPBRDownloadLibraryTab::GetServerStatus)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 4)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
			.PhysicalSplitterHandleSize(10.0f)
			+ SSplitter::Slot().Value(0.34f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[ SNew(STextBlock).Text(LOCTEXT("SitesSection", "下载站点")) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton).Text(LOCTEXT("AddSite", "添加"))
						.OnClicked_Lambda([this]() { OnAddSite(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton).Text(LOCTEXT("RemoveSite", "删除"))
						.OnClicked_Lambda([this]() { OnRemoveSite(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).Text(LOCTEXT("OpenSite", "打开网站"))
						.OnClicked_Lambda([this]() { OnOpenSite(); return FReply::Handled(); })
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).MinHeight(120.0f)
				[
					SAssignNew(SiteTree, SListView<TSharedPtr<FPBRDownloadSite>>)
					.ListItemsSource(&SiteRows)
					.OnGenerateRow(this, &SPBRDownloadLibraryTab::OnGenerateSiteRow)
					.SelectionMode(ESelectionMode::Single)
					.HeaderRow(BuildSiteHeader())
				]
			]
			+ SSplitter::Slot().Value(0.66f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[ SNew(STextBlock).Text(LOCTEXT("QueueSection", "下载队列")) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton).Text(LOCTEXT("PasteClipboard", "手动检测剪贴板"))
						.OnClicked_Lambda([this]() { OnAddUrlFromClipboard(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton).Text(LOCTEXT("DownloadSelected", "下载选中"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
						.OnClicked_Lambda([this]() { OnDownloadSelected(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton).Text(LOCTEXT("DownloadAll", "全部下载"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
						.OnClicked_Lambda([this]() { OnDownloadAll(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton).Text(LOCTEXT("ClearQueue", "清空"))
						.OnClicked_Lambda([this]() { OnClearQueue(); return FReply::Handled(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
					[
						SAssignNew(AutoExtractCheck, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						[ SNew(STextBlock).Text(LOCTEXT("AutoExtract", "下载后自动解压")) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
					[
						SAssignNew(DeleteNonImageFilesCheck, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							DownloadManager->SetDeleteNonImageFilesAfterExtract(NewState == ECheckBoxState::Checked);
						})
						[ SNew(STextBlock).Text(LOCTEXT("DeleteNonImageFiles", "解压后删除图片之外的文件")) ]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[ SNew(STextBlock).Text(LOCTEXT("ManualClipboardHint", "复制链接后点手动检测，不再自动闪烁检测。")) ]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).MinHeight(180.0f)
				[
					SAssignNew(QueueTree, SListView<TSharedPtr<FPBRDownloadEntry>>)
					.ListItemsSource(&QueueRows)
					.OnGenerateRow(this, &SPBRDownloadLibraryTab::OnGenerateQueueRow)
					.OnContextMenuOpening(this, &SPBRDownloadLibraryTab::MakeQueueContextMenu)
					.SelectionMode(ESelectionMode::Multi)
					.HeaderRow(BuildQueueHeader())
				]
			]
		]
	];
}

FReply SPBRDownloadLibraryTab::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return HasSupportedArchiveDrag(DragDropEvent) ? FReply::Handled() : FReply::Unhandled();
}

FReply SPBRDownloadLibraryTab::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FExternalDragOperation> ExternalDrag = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (!ExternalDrag.IsValid() || !ExternalDrag->HasFiles())
	{
		return FReply::Unhandled();
	}

	DownloadManager->SetMaterialLibraryDir(LibraryPathBox->GetText().ToString());
	bool bHandledAny = false;
	for (const FString& FilePath : ExternalDrag->GetFiles())
	{
		if (!DownloadManager->IsArchiveFile(FilePath))
		{
			continue;
		}
		FString LibraryFolder;
		FString Message;
		DownloadManager->ImportLocalArchiveToLibrary(FilePath, LibraryFolder, Message);
		bHandledAny = true;
	}

	if (bHandledAny)
	{
		RefreshQueue();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

bool SPBRDownloadLibraryTab::HasSupportedArchiveDrag(const FDragDropEvent& DragDropEvent) const
{
	TSharedPtr<FExternalDragOperation> ExternalDrag = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (!ExternalDrag.IsValid() || !ExternalDrag->HasFiles() || !DownloadManager.IsValid())
	{
		return false;
	}

	for (const FString& FilePath : ExternalDrag->GetFiles())
	{
		if (DownloadManager->IsArchiveFile(FilePath))
		{
			return true;
		}
	}
	return false;
}

FReply SPBRDownloadLibraryTab::OnBrowseLibrary()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop)
	{
		return FReply::Handled();
	}

	FString Folder;
	const bool bOk = Desktop->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("选择 PBR 材质库文件夹"),
		LibraryPathBox->GetText().ToString(),
		Folder);

	if (bOk && !Folder.IsEmpty())
	{
		SetCurrentLibraryFolder(Folder, true);
	}
	return FReply::Handled();
}

FReply SPBRDownloadLibraryTab::OnOpenLibrary()
{
	DownloadManager->SetMaterialLibraryDir(LibraryPathBox->GetText().ToString());
	FPlatformProcess::ExploreFolder(*DownloadManager->GetMaterialLibraryDir());
	return FReply::Handled();
}

FReply SPBRDownloadLibraryTab::OnScanLibraryToPBRSuite()
{
	SetCurrentLibraryFolder(LibraryPathBox->GetText().ToString(), false);
	DownloadManager->PublishLibraryPathToTextureSuite();
	if (OnLibrarySentToTextureSuite.IsBound())
	{
		OnLibrarySentToTextureSuite.Execute();
	}
	return FReply::Handled();
}

FReply SPBRDownloadLibraryTab::OnScanLibraryToSpecialMaterials()
{
	SetCurrentLibraryFolder(LibraryPathBox->GetText().ToString(), false);
	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		Config = MakeShareable(new FJsonObject);
	}
	Config->SetStringField(TEXT("special_materials_last_folder"), DownloadManager->GetMaterialLibraryDir());
	FPBRDataStore::SaveConfig(Config);
	if (OnLibrarySentToSpecialMaterials.IsBound())
	{
		OnLibrarySentToSpecialMaterials.Execute();
	}
	return FReply::Handled();
}

void SPBRDownloadLibraryTab::SetCurrentLibraryFolder(const FString& Folder, bool bUpdateText)
{
	const FString CleanFolder = Folder.TrimStartAndEnd();
	if (CleanFolder.IsEmpty())
	{
		return;
	}
	if (bUpdateText && LibraryPathBox.IsValid())
	{
		LibraryPathBox->SetText(FText::FromString(CleanFolder));
	}
	DownloadManager->SetMaterialLibraryDir(CleanFolder);
	SaveLibraryHistory(CleanFolder);
}

void SPBRDownloadLibraryTab::LoadLibraryHistory()
{
	LibraryHistoryOptions.Reset();
	TSharedPtr<FJsonObject> Config;
	if (FPBRDataStore::LoadConfig(Config) && Config.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* HistoryArray = nullptr;
		if (Config->TryGetArrayField(TEXT("material_library_history"), HistoryArray))
		{
			for (const TSharedPtr<FJsonValue>& Value : *HistoryArray)
			{
				const FString Folder = Value.IsValid() ? Value->AsString() : FString();
				if (!Folder.IsEmpty())
				{
					LibraryHistoryOptions.Add(MakeShared<FString>(Folder));
				}
			}
		}
	}
}

void SPBRDownloadLibraryTab::SaveLibraryHistory(const FString& Folder)
{
	if (Folder.IsEmpty())
	{
		return;
	}
	LibraryHistoryOptions.RemoveAll([&Folder](const FStringOption& Option)
	{
		return Option.IsValid() && FPaths::IsSamePath(*Option, Folder);
	});
	LibraryHistoryOptions.Insert(MakeShared<FString>(Folder), 0);
	while (LibraryHistoryOptions.Num() > 12)
	{
		LibraryHistoryOptions.RemoveAt(LibraryHistoryOptions.Num() - 1);
	}

	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		Config = MakeShareable(new FJsonObject);
	}
	Config->SetStringField(TEXT("texture_suite_last_folder"), Folder);
	TArray<TSharedPtr<FJsonValue>> HistoryValues;
	for (const FStringOption& Option : LibraryHistoryOptions)
	{
		if (Option.IsValid() && !Option->IsEmpty())
		{
			HistoryValues.Add(MakeShared<FJsonValueString>(*Option));
		}
	}
	Config->SetArrayField(TEXT("material_library_history"), HistoryValues);
	FPBRDataStore::SaveConfig(Config);
	if (LibraryHistoryComboBox.IsValid())
	{
		LibraryHistoryComboBox->RefreshOptions();
	}
}

void SPBRDownloadLibraryTab::OnLibraryHistorySelected(FStringOption Option, ESelectInfo::Type SelectInfo)
{
	if (Option.IsValid())
	{
		SetCurrentLibraryFolder(*Option, true);
	}
}

TSharedRef<SWidget> SPBRDownloadLibraryTab::GenerateLibraryHistoryOption(FStringOption Option) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(Option.IsValid() ? *Option : FString()));
}

TSharedRef<SHeaderRow> SPBRDownloadLibraryTab::BuildSiteHeader()
{
	return SNew(SHeaderRow)
		+ SHeaderRow::Column(TEXT("Name")).DefaultLabel(LOCTEXT("SName", "名称")).FillWidth(0.2f)
		+ SHeaderRow::Column(TEXT("License")).DefaultLabel(LOCTEXT("SLicense", "授权")).FillWidth(0.12f)
		+ SHeaderRow::Column(TEXT("URL")).DefaultLabel(LOCTEXT("SUrl", "网址")).FillWidth(0.38f)
		+ SHeaderRow::Column(TEXT("Note")).DefaultLabel(LOCTEXT("SNote", "备注")).FillWidth(0.3f);
}

TSharedRef<ITableRow> SPBRDownloadLibraryTab::OnGenerateSiteRow(TSharedPtr<FPBRDownloadSite> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FPBRDownloadSite>>, Owner)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.2f).Padding(4)
		[ SNew(STextBlock).Text(FText::FromString(Item->Name)) ]
		+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(4)
		[ SNew(STextBlock).Text(FText::FromString(Item->License)) ]
		+ SHorizontalBox::Slot().FillWidth(0.38f).Padding(4)
		[ SNew(STextBlock).Text(FText::FromString(Item->URL)).AutoWrapText(true) ]
		+ SHorizontalBox::Slot().FillWidth(0.3f).Padding(4)
		[ SNew(STextBlock).Text(FText::FromString(Item->Note)).AutoWrapText(true) ]
	];
}

void SPBRDownloadLibraryTab::CommitSiteEdit(TSharedPtr<FPBRDownloadSite> Item, const FString& Field, const FText& NewText)
{
}

void SPBRDownloadLibraryTab::OnAddSite()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("AddSiteWindowTitle", "添加下载站点"))
		.ClientSize(FVector2D(520, 220))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedPtr<SEditableTextBox> UrlBox;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> LicenseBox;
	TSharedPtr<SEditableTextBox> NoteBox;

	Window->SetContent(
		SNew(SBorder).Padding(12)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[ SNew(STextBlock).Text(LOCTEXT("AddSiteHint", "至少填写网站地址；名称可留空，插件会按域名生成。")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("AddSiteURL", "网站")) ]
				+ SHorizontalBox::Slot()
				[ SAssignNew(UrlBox, SEditableTextBox).Text(FText::FromString(TEXT("https://"))) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("AddSiteName", "名称")) ]
				+ SHorizontalBox::Slot()
				[ SAssignNew(NameBox, SEditableTextBox) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("AddSiteLicense", "授权")) ]
				+ SHorizontalBox::Slot()
				[ SAssignNew(LicenseBox, SEditableTextBox).Text(FText::FromString(TEXT("Free"))) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("AddSiteNote", "备注")) ]
				+ SHorizontalBox::Slot()
				[ SAssignNew(NoteBox, SEditableTextBox) ]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddSiteOK", "添加"))
					.OnClicked_Lambda([this, Window, UrlBox, NameBox, LicenseBox, NoteBox]()
					{
						AddSiteFromDialog(
							UrlBox.IsValid() ? UrlBox->GetText().ToString() : FString(),
							NameBox.IsValid() ? NameBox->GetText().ToString() : FString(),
							LicenseBox.IsValid() ? LicenseBox->GetText().ToString() : FString(),
							NoteBox.IsValid() ? NoteBox->GetText().ToString() : FString());
						FSlateApplication::Get().RequestDestroyWindow(Window);
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddSiteCancel", "取消"))
					.OnClicked_Lambda([Window]()
					{
						FSlateApplication::Get().RequestDestroyWindow(Window);
						return FReply::Handled();
					})
				]
			]
		]);

	FSlateApplication::Get().AddWindow(Window);
}

void SPBRDownloadLibraryTab::AddSiteFromDialog(const FString& URL, const FString& Name, const FString& License, const FString& Note)
{
	FString CleanURL = URL.TrimStartAndEnd();
	if (CleanURL.IsEmpty() || CleanURL == TEXT("https://") || CleanURL == TEXT("http://"))
	{
		return;
	}
	if (!CleanURL.StartsWith(TEXT("http://")) && !CleanURL.StartsWith(TEXT("https://")))
	{
		CleanURL = TEXT("https://") + CleanURL;
	}

	FString AutoName = Name.TrimStartAndEnd();
	if (AutoName.IsEmpty())
	{
		FString Host = CleanURL;
		Host.RemoveFromStart(TEXT("https://"));
		Host.RemoveFromStart(TEXT("http://"));
		int32 SlashIndex = INDEX_NONE;
		if (Host.FindChar('/', SlashIndex))
		{
			Host = Host.Left(SlashIndex);
		}
		AutoName = Host;
	}

	FPBRDownloadSite Site;
	Site.Name = AutoName;
	Site.License = License.TrimStartAndEnd().IsEmpty() ? TEXT("Free") : License.TrimStartAndEnd();
	Site.URL = CleanURL;
	Site.Note = Note.TrimStartAndEnd();
	DownloadManager->AddSite(Site);
	RefreshSites();
}

void SPBRDownloadLibraryTab::OnRemoveSite()
{
	TArray<TSharedPtr<FPBRDownloadSite>> Selected = SiteTree->GetSelectedItems();
	for (const TSharedPtr<FPBRDownloadSite>& Sel : Selected)
	{
		const int32 Idx = SiteRows.Find(Sel);
		if (Idx != INDEX_NONE)
		{
			DownloadManager->RemoveSite(Idx);
		}
	}
	RefreshSites();
}

void SPBRDownloadLibraryTab::OnOpenSite()
{
	TArray<TSharedPtr<FPBRDownloadSite>> Selected = SiteTree->GetSelectedItems();
	if (Selected.Num() > 0 && Selected[0].IsValid())
	{
		FPlatformProcess::LaunchURL(*Selected[0]->URL, nullptr, nullptr);
	}
}

void SPBRDownloadLibraryTab::RefreshSites()
{
	SiteRows.Empty();
	for (FPBRDownloadSite& S : DownloadManager->GetSites())
	{
		SiteRows.Add(MakeShareable(new FPBRDownloadSite(S)));
	}
	if (SiteTree.IsValid())
	{
		SiteTree->RequestListRefresh();
	}
}

FReply SPBRDownloadLibraryTab::OnStartServer()
{
	HttpServer->Start(PortSpin->GetValue());
	return FReply::Handled();
}

FReply SPBRDownloadLibraryTab::OnStopServer()
{
	HttpServer->Stop();
	return FReply::Handled();
}

FText SPBRDownloadLibraryTab::GetServerStatus() const
{
	if (HttpServer->IsRunning())
	{
		return FText::Format(LOCTEXT("ServerRunning", "运行中，端口 {0}"), FText::AsNumber(HttpServer->GetPort()));
	}
	return LOCTEXT("ServerStopped", "已停止");
}

TSharedRef<SHeaderRow> SPBRDownloadLibraryTab::BuildQueueHeader()
{
	return SNew(SHeaderRow)
		+ SHeaderRow::Column(TEXT("Status")).DefaultLabel(LOCTEXT("QStatus", "状态")).FillWidth(0.11f)
		+ SHeaderRow::Column(TEXT("Progress")).DefaultLabel(LOCTEXT("QProgress", "进度")).FillWidth(0.15f)
		+ SHeaderRow::Column(TEXT("Name")).DefaultLabel(LOCTEXT("QName", "名称")).FillWidth(0.19f)
		+ SHeaderRow::Column(TEXT("Source")).DefaultLabel(LOCTEXT("QSource", "来源")).FillWidth(0.1f)
		+ SHeaderRow::Column(TEXT("URL")).DefaultLabel(LOCTEXT("QUrl", "地址/文件")).FillWidth(0.25f)
		+ SHeaderRow::Column(TEXT("PBR")).DefaultLabel(LOCTEXT("QPBR", "分析")).FillWidth(0.2f);
}

TSharedRef<ITableRow> SPBRDownloadLibraryTab::OnGenerateQueueRow(TSharedPtr<FPBRDownloadEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
	FLinearColor StatusColor = FLinearColor::Gray;
	if (Item->Status.Contains(TEXT("完成")) || Item->Status.Contains(TEXT("解压")) || Item->Status.Contains(TEXT("整理")))
	{
		StatusColor = FLinearColor(0.25f, 0.75f, 0.5f);
	}
	else if (Item->Status.Contains(TEXT("失败")))
	{
		StatusColor = FLinearColor(1.0f, 0.3f, 0.3f);
	}
	else if (Item->Status.Contains(TEXT("中")))
	{
		StatusColor = FLinearColor(0.3f, 0.5f, 1.0f);
	}

	return SNew(STableRow<TSharedPtr<FPBRDownloadEntry>>, Owner)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.11f).Padding(4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->Status))
			.ColorAndOpacity(FSlateColor(StatusColor))
		]
		+ SHorizontalBox::Slot().FillWidth(0.15f).Padding(4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SProgressBar)
				.Percent(Item->Progress > 0.0f ? TOptional<float>(Item->Progress) : TOptional<float>())
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->DetailStatus.IsEmpty() ? TEXT("-") : Item->DetailStatus))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(0.19f).Padding(4)
		[ SNew(STextBlock).Text(FText::FromString(Item->Name)).AutoWrapText(true) ]
		+ SHorizontalBox::Slot().FillWidth(0.1f).Padding(4)
		[ SNew(STextBlock).Text(FText::FromString(Item->Source)) ]
		+ SHorizontalBox::Slot().FillWidth(0.25f).Padding(4)
		[ SNew(STextBlock).Text(FText::FromString(Item->URL)).AutoWrapText(true) ]
		+ SHorizontalBox::Slot().FillWidth(0.2f).Padding(4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->PbrAnalysisMessage.IsEmpty() ? TEXT("-") : Item->PbrAnalysisMessage))
			.AutoWrapText(true)
		]
	];
}

void SPBRDownloadLibraryTab::OnDownloadSelected()
{
	TArray<TSharedPtr<FPBRDownloadEntry>> Selected = QueueTree->GetSelectedItems();
	for (const TSharedPtr<FPBRDownloadEntry>& Sel : Selected)
	{
		const int32 Idx = QueueRows.Find(Sel);
		if (Idx != INDEX_NONE)
		{
			DownloadManager->DownloadEntry(Idx);
		}
	}
	RefreshQueue();
}

void SPBRDownloadLibraryTab::OnDownloadAll()
{
	DownloadManager->DownloadAllPending();
	RefreshQueue();
}

void SPBRDownloadLibraryTab::OnClearQueue()
{
	DownloadManager->ClearQueue();
	RefreshQueue();
}

TSharedPtr<SWidget> SPBRDownloadLibraryTab::MakeQueueContextMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameMaterial", "重命名材质"),
		LOCTEXT("RenameMaterialTip", "修改队列名称，并重命名已经整理出来的材质文件夹"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPBRDownloadLibraryTab::OnRenameSelectedQueueEntry)));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenEntryLocation", "打开文件所在位置"),
		LOCTEXT("OpenEntryLocationTip", "打开下载或解压后的材质文件夹"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPBRDownloadLibraryTab::OnOpenSelectedQueueEntryLocation)));
	return MenuBuilder.MakeWidget();
}

void SPBRDownloadLibraryTab::OnRenameSelectedQueueEntry()
{
	TArray<TSharedPtr<FPBRDownloadEntry>> Selected = QueueTree->GetSelectedItems();
	if (Selected.Num() == 0 || !Selected[0].IsValid())
	{
		return;
	}

	TSharedPtr<FPBRDownloadEntry> Item = Selected[0];
	TSharedPtr<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("RenameMaterialWindow", "重命名材质"))
		.ClientSize(FVector2D(360, 112))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedPtr<SEditableTextBox> NameBox;
	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 12, 12, 6)
		[
			SAssignNew(NameBox, SEditableTextBox)
			.Text(FText::FromString(Item->Name))
			.SelectAllTextWhenFocused(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RenameOk", "确定"))
				.OnClicked_Lambda([this, Window, NameBox, Item]()
				{
					const int32 Index = QueueRows.Find(Item);
					FString Message;
					if (DownloadManager->RenameQueueEntry(Index, NameBox.IsValid() ? NameBox->GetText().ToString() : FString(), Message))
					{
						RefreshQueue();
					}
					Window->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RenameCancel", "取消"))
				.OnClicked_Lambda([Window]() { Window->RequestDestroyWindow(); return FReply::Handled(); })
			]
		]);
	FSlateApplication::Get().AddWindow(Window.ToSharedRef());
}

void SPBRDownloadLibraryTab::OnOpenSelectedQueueEntryLocation()
{
	TArray<TSharedPtr<FPBRDownloadEntry>> Selected = QueueTree->GetSelectedItems();
	if (Selected.Num() == 0 || !Selected[0].IsValid())
	{
		return;
	}

	const FPBRDownloadEntry& Item = *Selected[0];
	FString Folder = Item.TargetDirectory;
	if (!Folder.IsEmpty() && !FPaths::DirectoryExists(Folder))
	{
		Folder = FPaths::GetPath(Item.DownloadedFile);
	}
	if (!Folder.IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*Folder);
	}
}

void SPBRDownloadLibraryTab::AddUrlsFromText(const FString& Text, const FString& Source)
{
	TArray<FString> Tokens;
	Text.ParseIntoArray(Tokens, TEXT(" "), true);

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	Tokens.Append(Lines);

	bool bAdded = false;
	for (const FString& Token : Tokens)
	{
		FString Trimmed = Token.TrimStartAndEnd().TrimQuotes();
		if (Trimmed.StartsWith(TEXT("http://")) || Trimmed.StartsWith(TEXT("https://")))
		{
			if (DownloadManager->AddToQueue(Trimmed, FString(), Source) != INDEX_NONE)
			{
				bAdded = true;
			}
		}
	}

	if (bAdded)
	{
		RefreshQueue();
	}
}

void SPBRDownloadLibraryTab::OnAddUrlFromClipboard()
{
	FString Clipboard;
	FPlatformApplicationMisc::ClipboardPaste(Clipboard);
	LastClipboardText = Clipboard;
	AddUrlsFromText(Clipboard, TEXT("剪贴板"));
}

void SPBRDownloadLibraryTab::RefreshQueue()
{
	QueueRows.Empty();
	for (FPBRDownloadEntry& E : DownloadManager->GetQueue())
	{
		QueueRows.Add(MakeShareable(new FPBRDownloadEntry(E)));
	}
	if (QueueTree.IsValid())
	{
		QueueTree->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
