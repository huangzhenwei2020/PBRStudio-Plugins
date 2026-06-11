#pragma once

#include "CoreMinimal.h"

class SWidget;

class PBRSTUDIO_API FPBRLocalization
{
public:
	static FText Text(const TCHAR* Key, const TCHAR* Chinese, const TCHAR* English);
	static bool IsChinese();
	static FString GetLanguageCode();
	static FText GetLanguageButtonText();
	static FText GetLanguageButtonTooltip();
	static bool ToggleLanguage();
	static bool ApplySupportedEditorLanguage();

private:
	static FString ResolveEffectiveLanguageCode();
	static bool SetEditorLanguage(const FString& LanguageCode);
	static void RefreshEditorTextCaches();
};
