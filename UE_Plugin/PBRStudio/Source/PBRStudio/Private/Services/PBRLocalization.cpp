#include "Services/PBRLocalization.h"

#include "EdGraph/EdGraphSchema.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "InternationalizationSettingsModel.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectIterator.h"

namespace
{
const TCHAR* PBRLanguageChinese = TEXT("zh");
const TCHAR* PBRLanguageEnglish = TEXT("en");
const TCHAR* PBRUECultureChinese = TEXT("zh-Hans");
const TCHAR* PBRUECultureEnglish = TEXT("en");

static bool IsChineseCultureCode(const FString& CultureName)
{
	return CultureName.StartsWith(TEXT("zh"), ESearchCase::IgnoreCase)
		|| CultureName.StartsWith(TEXT("cn"), ESearchCase::IgnoreCase);
}
}

FText FPBRLocalization::Text(const TCHAR* Key, const TCHAR* Chinese, const TCHAR* English)
{
	return FText::FromString(IsChinese() ? FString(Chinese) : FString(English));
}

bool FPBRLocalization::IsChinese()
{
	return GetLanguageCode() == PBRLanguageChinese;
}

FString FPBRLocalization::GetLanguageCode()
{
	return ResolveEffectiveLanguageCode();
}

FText FPBRLocalization::GetLanguageButtonText()
{
	return IsChinese()
		? FText::FromString(TEXT("中 / EN"))
		: FText::FromString(TEXT("EN / 中"));
}

FText FPBRLocalization::GetLanguageButtonTooltip()
{
	return IsChinese()
		? FText::FromString(TEXT("Switch Unreal Editor language to English"))
		: FText::FromString(TEXT("将 Unreal Editor 语言切换为中文"));
}

bool FPBRLocalization::ToggleLanguage()
{
	return SetEditorLanguage(IsChinese() ? PBRLanguageEnglish : PBRLanguageChinese);
}

bool FPBRLocalization::ApplySupportedEditorLanguage()
{
	const FString CurrentLanguage = FInternationalization::Get().GetCurrentLanguage()->GetName();
	if (IsChineseCultureCode(CurrentLanguage))
	{
		return SetEditorLanguage(PBRLanguageChinese);
	}
	if (!CurrentLanguage.Equals(PBRUECultureEnglish, ESearchCase::IgnoreCase))
	{
		return SetEditorLanguage(PBRLanguageEnglish);
	}
	return true;
}

FString FPBRLocalization::ResolveEffectiveLanguageCode()
{
	const FString CultureName = FInternationalization::Get().GetCurrentLanguage()->GetName();
	return IsChineseCultureCode(CultureName) ? PBRLanguageChinese : PBRLanguageEnglish;
}

bool FPBRLocalization::SetEditorLanguage(const FString& LanguageCode)
{
	const FString TargetCulture = LanguageCode.Equals(PBRLanguageChinese, ESearchCase::IgnoreCase)
		? PBRUECultureChinese
		: PBRUECultureEnglish;

	UInternationalizationSettingsModel* SettingsModel = GetMutableDefault<UInternationalizationSettingsModel>();
	if (SettingsModel)
	{
		SettingsModel->SetEditorLanguage(TargetCulture);
		SettingsModel->SetEditorLocale(TargetCulture);
	}
	else
	{
		GConfig->SetString(TEXT("Internationalization"), TEXT("Language"), *TargetCulture, GEditorSettingsIni);
		GConfig->SetString(TEXT("Internationalization"), TEXT("Locale"), *TargetCulture, GEditorSettingsIni);
		GConfig->SetString(TEXT("Internationalization"), TEXT("Culture"), TEXT(""), GEditorSettingsIni);
		GConfig->Flush(false, GEditorSettingsIni);
	}

	FInternationalization& I18N = FInternationalization::Get();
	const bool bLanguageChanged = I18N.SetCurrentLanguage(TargetCulture);
	const bool bLocaleChanged = I18N.SetCurrentLocale(TargetCulture);
	RefreshEditorTextCaches();
	return bLanguageChanged || bLocaleChanged || I18N.GetCurrentLanguage()->GetName().Equals(TargetCulture, ESearchCase::IgnoreCase);
}

void FPBRLocalization::RefreshEditorTextCaches()
{
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		if (UEdGraphSchema* Schema = Cast<UEdGraphSchema>(ClassIt->GetDefaultObject()))
		{
			Schema->ForceVisualizationCacheClear();
		}
	}
}
