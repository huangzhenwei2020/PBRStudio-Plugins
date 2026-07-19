#include "PBRStudioStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FPBRStudioStyle::StyleInstance = nullptr;

void FPBRStudioStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FPBRStudioStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		StyleInstance.Reset();
	}
}

FName FPBRStudioStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("PBRStudioStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FPBRStudioStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("PBRStudioStyle"));

	Style->SetContentRoot(IPluginManager::Get().FindPlugin("PBRStudio")->GetBaseDir() / TEXT("Resources"));

	Style->Set("PBRStudio.OpenMainWindow", new FSlateImageBrush(
		Style->RootToContentDir(TEXT("PBRStudioIcon_128"), TEXT(".png")),
		FVector2D(40.0f, 40.0f)));

	Style->Set("PBRStudio.OpenMainWindow.Small", new FSlateImageBrush(
		Style->RootToContentDir(TEXT("PBRStudioIcon_40"), TEXT(".png")),
		FVector2D(20.0f, 20.0f)));

	Style->Set("PBRStudio.OpenMainWindow.Toolbar", new FSlateImageBrush(
		Style->RootToContentDir(TEXT("PBRStudioIcon_40"), TEXT(".png")),
		FVector2D(24.0f, 24.0f)));

	Style->Set("PBRStudio.MagicOutliner", new FSlateImageBrush(
		Style->RootToContentDir(TEXT("PBRMagicOutliner_128"), TEXT(".png")),
		FVector2D(40.0f, 40.0f)));

	Style->Set("PBRStudio.MagicOutliner.Small", new FSlateImageBrush(
		Style->RootToContentDir(TEXT("PBRMagicOutliner_40"), TEXT(".png")),
		FVector2D(20.0f, 20.0f)));

	Style->Set("PBRStudio.MagicOutliner.Toolbar", new FSlateImageBrush(
		Style->RootToContentDir(TEXT("PBRMagicOutliner_40"), TEXT(".png")),
		FVector2D(24.0f, 24.0f)));

	Style->Set("PBRStudio.Icon.MagicOutliner", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/magic-outliner"), TEXT(".svg")),
		FVector2D(30.0f, 30.0f)));

	Style->Set("PBRStudio.Icon.MaterialVault", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/material-vault"), TEXT(".svg")),
		FVector2D(30.0f, 30.0f)));

	Style->Set("PBRStudio.Icon.CameraPost", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/camera-post"), TEXT(".svg")),
		FVector2D(30.0f, 30.0f)));

	Style->Set("PBRStudio.Icon.TextureSuite", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/texture-suite"), TEXT(".svg")),
		FVector2D(30.0f, 30.0f)));

	Style->Set("PBRStudio.Icon.BatchAdjust", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/batch-adjust"), TEXT(".svg")),
		FVector2D(30.0f, 30.0f)));

	Style->Set("PBRStudio.Toolbar.Refresh", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-refresh"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	Style->Set("PBRStudio.Toolbar.Select", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-select"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	Style->Set("PBRStudio.Toolbar.Add", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-add"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	Style->Set("PBRStudio.Toolbar.Show", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-show"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	Style->Set("PBRStudio.Toolbar.Hide", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-hide"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	Style->Set("PBRStudio.Toolbar.Settings", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-settings"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	Style->Set("PBRStudio.Toolbar.Filter", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-filter"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	Style->Set("PBRStudio.Toolbar.Clear", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-clear"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	Style->Set("PBRStudio.Toolbar.Open", new FSlateVectorImageBrush(
		Style->RootToContentDir(TEXT("Icons/toolbar-open"), TEXT(".svg")),
		FVector2D(18.0f, 18.0f)));

	return Style;
}

void FPBRStudioStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FPBRStudioStyle::Get()
{
	return *StyleInstance;
}
