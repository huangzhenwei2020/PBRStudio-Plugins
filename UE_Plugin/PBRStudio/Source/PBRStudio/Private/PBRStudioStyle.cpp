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
