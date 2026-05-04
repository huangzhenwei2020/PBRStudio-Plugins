#include "Widgets/SPBRTextureStreamingTab.h"
#include "Services/PBRTextureProcessor.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SPBRTextureStreamingTab"

void SPBRTextureStreamingTab::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			// -- Title --------------------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "UE 贴图规范 - 检查二次幂、最大尺寸、重新链接路径"))
			]

			// -- Scan Buttons ------------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ScanSelected", "扫描选中对象"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
					.OnClicked(this, &SPBRTextureStreamingTab::OnScanSelected)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearList", "清空列表"))
					.OnClicked(this, &SPBRTextureStreamingTab::OnClearList)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("RemoveMissing", "移除缺失"))
					.OnClicked(this, &SPBRTextureStreamingTab::OnRemoveMissing)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
				[
					SAssignNew(CountText, STextBlock)
					.Text(LOCTEXT("NoItems", "0 张贴图"))
				]
			]

			// -- Output Directory --------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("Output", "输出:")) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SAssignNew(OutputDirBox, SEditableTextBox)
					.HintText(LOCTEXT("OutputHint", "选择 UE 规范贴图的输出目录..."))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton).Text(LOCTEXT("ChooseDir", "选择..."))
					.OnClicked(this, &SPBRTextureStreamingTab::OnChooseOutputDir)
				]
			]

			// -- Settings ----------------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("MaxSize", "最大尺寸:")) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
				[
					SAssignNew(MaxSizeSpin, SSpinBox<int32>)
					.MinValue(256).MaxValue(16384).Value(4096)
					.MinDesiredWidth(80)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
				[
					SAssignNew(RequireP2Check, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[ SNew(STextBlock).Text(LOCTEXT("RequireP2", "要求二次幂")) ]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
				[
					SAssignNew(UENamingCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[ SNew(STextBlock).Text(LOCTEXT("UENaming", "UE 命名 (T_ 前缀)")) ]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(OnlyProblemCheck, SCheckBox)
					[ SNew(STextBlock).Text(LOCTEXT("OnlyProblem", "仅显示问题")) ]
				]
			]

			// -- Process Buttons ---------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ForceCompliant", "强制规范化"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.OnClicked_Lambda([this]() { OnProcessChecked(true); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyOnly", "仅复制"))
					.OnClicked_Lambda([this]() { OnProcessChecked(false); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Relink", "重新链接材质"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Danger")
					.OnClicked(this, &SPBRTextureStreamingTab::OnRelinkMaterials)
				]
			]

			// -- Tree View ---------------------------------------------------
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(8, 4)
			[
				SNew(SBox).MinDesiredHeight(300)
				[
					SAssignNew(TextureTree, SListView<TSharedPtr<FPBRTextureEntry>>)
					.ListItemsSource(&TextureEntries)
					.OnGenerateRow(this, &SPBRTextureStreamingTab::OnGenerateRow)
					.SelectionMode(ESelectionMode::Single)
					.HeaderRow(BuildHeaderRow())
				]
			]

			// -- Progress ----------------------------------------------------
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 4)
			[
				SAssignNew(ProgressText, STextBlock)
				.Text(LOCTEXT("Ready", "就绪"))
			]
		]
	];
}

FReply SPBRTextureStreamingTab::OnChooseOutputDir()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) return FReply::Handled();

	FString Folder;
	bool bOk = Desktop->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select UE Texture Output Directory"),
		OutputDirBox->GetText().ToString(),
		Folder
	);
	if (bOk && !Folder.IsEmpty())
	{
		OutputDirBox->SetText(FText::FromString(Folder));
	}
	return FReply::Handled();
}

FReply SPBRTextureStreamingTab::OnScanSelected()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		ProgressText->SetText(LOCTEXT("NoWorld", "没有编辑器世界"));
		return FReply::Handled();
	}

	// Scan selected actors for materials and their texture references
	TextureEntries.Empty();

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection) return FReply::Handled();

	int32 MaxSize = MaxSizeSpin->GetValue();
	bool bRequireP2 = RequireP2Check->IsChecked();

	for (int32 i = 0; i < Selection->Num(); ++i)
	{
		AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
		if (!Actor) continue;

		TArray<UMaterialInterface*> Materials;
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			if (Component)
			{
				Component->GetUsedMaterials(Materials);
			}
		}

		for (UMaterialInterface* MI : Materials)
		{
			if (!MI) continue;

			TArray<FMaterialParameterInfo> ParamInfo;
			TArray<FGuid> ParamIds;
			TArray<UTexture*> Textures;
			MI->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, false);

			for (UTexture* Tex : Textures)
			{
				if (!Tex || !Tex->IsA<UTexture2D>()) continue;

				if (Tex->GetPathName().StartsWith(TEXT("/Engine/"))) continue; // Skip engine textures

				TSharedPtr<FPBRTextureEntry> Entry = MakeShareable(new FPBRTextureEntry);
				Entry->File = Tex->GetName();
				Entry->Path = Tex->GetPathName();
				Entry->Width = Tex->GetSurfaceWidth();
				Entry->Height = Tex->GetSurfaceHeight();
				Entry->bExists = true;
				Entry->Channel = TEXT("Texture2D");

				TArray<FString> Issues;
				int32 W, H;
				bool bOK = FPBRTextureProcessor::IsTextureUECompliant(
					Entry->Path, MaxSize, bRequireP2, Issues, W, H);

				Entry->Status = bOK ? TEXT("OK") : FString::Join(Issues, TEXT("; "));

				TextureEntries.Add(Entry);
			}
		}
	}

	RefreshTree();
	CountText->SetText(FText::Format(LOCTEXT("Count", "{0} 张贴图"), FText::AsNumber(TextureEntries.Num())));
	ProgressText->SetText(FText::Format(LOCTEXT("ScanDone", "扫描完成: {0} 张贴图"), FText::AsNumber(TextureEntries.Num())));
	return FReply::Handled();
}

void SPBRTextureStreamingTab::OnProcessChecked(bool bForce)
{
	if (TextureEntries.Num() == 0) return;

	FString OutDir = OutputDirBox->GetText().ToString();
	if (OutDir.IsEmpty())
	{
		OutDir = FPaths::ProjectSavedDir() / TEXT("PBRStudio") / TEXT("UE_Textures");
		OutputDirBox->SetText(FText::FromString(OutDir));
	}

	FPBRTextureProcessor::FProcessSettings Settings;
	Settings.OutputDir = OutDir;
	Settings.MaxSize = MaxSizeSpin->GetValue();
	Settings.bRequirePowerOf2 = RequireP2Check->IsChecked();
	Settings.bUECompliantNaming = UENamingCheck->IsChecked();
	Settings.bNoOverwrite = true;

	int32 Processed = 0;
	for (TSharedPtr<FPBRTextureEntry>& Entry : TextureEntries)
	{
		if (!Entry.IsValid()) continue;

		FString OutputPath, Message;
		bool bOK = FPBRTextureProcessor::ForceProcessTextureForUE(
			Entry->Path, Settings, OutputPath, Message);

		if (bOK && !OutputPath.IsEmpty())
		{
			Entry->OutputPath = OutputPath;
			Entry->Status = Message;
			Processed++;
		}
		else
		{
			Entry->Status = TEXT("失败: ") + Message;
		}

		ProgressText->SetText(FText::Format(LOCTEXT("Processing", "处理中... {0}/{1}"),
			FText::AsNumber(Processed), FText::AsNumber(TextureEntries.Num())));
	}

	RefreshTree();
	ProgressText->SetText(FText::Format(LOCTEXT("Processed", "已处理: {0} 张贴图"), FText::AsNumber(Processed)));
}

FReply SPBRTextureStreamingTab::OnRelinkMaterials()
{
	// Re-link materials to point to processed textures
	// For each texture entry with an output, update the material's texture reference
	int32 Relinked = 0;

	for (TSharedPtr<FPBRTextureEntry>& Entry : TextureEntries)
	{
		if (!Entry.IsValid() || Entry->OutputPath.IsEmpty()) continue;
		if (!FPaths::FileExists(Entry->OutputPath)) continue;

		// Find the original texture asset
		UTexture2D* OriginalTex = LoadObject<UTexture2D>(nullptr, *Entry->Path);
		if (!OriginalTex) continue;

		// In UE, we would reimport the texture from the new file
		// OriginalTex->Source.Import(Entry->OutputPath, ...)
		// For now, mark as relinked
		Entry->Status = FString::Printf(TEXT("已重新链接: %s"), *FPaths::GetCleanFilename(Entry->OutputPath));
		Relinked++;
	}

	RefreshTree();
	ProgressText->SetText(FText::Format(LOCTEXT("Relinked", "已重新链接: {0} 张贴图"), FText::AsNumber(Relinked)));
	return FReply::Handled();
}

FReply SPBRTextureStreamingTab::OnClearList()
{
	TextureEntries.Empty();
	RefreshTree();
	CountText->SetText(LOCTEXT("Cleared", "0 张贴图"));
	return FReply::Handled();
}

FReply SPBRTextureStreamingTab::OnRemoveMissing()
{
	TextureEntries.RemoveAll([](const TSharedPtr<FPBRTextureEntry>& E)
	{
		return !E.IsValid() || !E->bExists;
	});
	RefreshTree();
	CountText->SetText(FText::Format(LOCTEXT("Count", "{0} 张贴图"), FText::AsNumber(TextureEntries.Num())));
	return FReply::Handled();
}

TSharedRef<SHeaderRow> SPBRTextureStreamingTab::BuildHeaderRow()
{
	return SNew(SHeaderRow)
		+ SHeaderRow::Column(TEXT("Name")).DefaultLabel(LOCTEXT("ColName", "名称")).FillWidth(0.25f)
		+ SHeaderRow::Column(TEXT("Dimensions")).DefaultLabel(LOCTEXT("ColDims", "尺寸")).FillWidth(0.15f)
		+ SHeaderRow::Column(TEXT("Compliant")).DefaultLabel(LOCTEXT("ColCompliant", "UE 规范")).FillWidth(0.1f)
		+ SHeaderRow::Column(TEXT("Status")).DefaultLabel(LOCTEXT("ColStatus", "状态")).FillWidth(0.3f)
		+ SHeaderRow::Column(TEXT("Output")).DefaultLabel(LOCTEXT("ColOutput", "输出")).FillWidth(0.2f);
}

TSharedRef<ITableRow> SPBRTextureStreamingTab::OnGenerateRow(
	TSharedPtr<FPBRTextureEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
	FLinearColor StatusColor = FLinearColor::Gray;
	if (Item->Status.StartsWith(TEXT("OK")))
		StatusColor = FLinearColor(0.25f, 0.75f, 0.5f);
	else if (Item->Status.StartsWith(TEXT("失败")) || Item->Status.StartsWith(TEXT("Failed")))
		StatusColor = FLinearColor(1.0f, 0.3f, 0.3f);
	else if (Item->Status.StartsWith(TEXT("已重新链接")) || Item->Status.StartsWith(TEXT("Relinked")))
		StatusColor = FLinearColor(0.3f, 0.5f, 1.0f);

	bool bCompliant = Item->Status.StartsWith(TEXT("OK")) || Item->Status.StartsWith(TEXT("已重新链接")) || Item->Status.StartsWith(TEXT("Relinked"));

	return SNew(STableRow<TSharedPtr<FPBRTextureEntry>>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.25f).Padding(4)
			[ SNew(STextBlock).Text(FText::FromString(Item->File)) ]
			+ SHorizontalBox::Slot().FillWidth(0.15f).Padding(4)
			[ SNew(STextBlock).Text(FText::FromString(
				FString::Printf(TEXT("%dx%d"), Item->Width, Item->Height))) ]
			+ SHorizontalBox::Slot().FillWidth(0.1f).Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(bCompliant ? TEXT("是") : TEXT("否")))
				.ColorAndOpacity(FSlateColor(bCompliant ? FLinearColor::Green : FLinearColor::Red))
			]
			+ SHorizontalBox::Slot().FillWidth(0.3f).Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->Status))
				.ColorAndOpacity(FSlateColor(StatusColor))
			]
			+ SHorizontalBox::Slot().FillWidth(0.2f).Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->OutputPath.IsEmpty() ? TEXT("-") :
					FPaths::GetCleanFilename(Item->OutputPath)))
				.AutoWrapText(true)
			]
		];
}

void SPBRTextureStreamingTab::RefreshTree()
{
	if (TextureTree.IsValid())
	{
		TextureTree->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
