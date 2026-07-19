#pragma once

#include "CoreMinimal.h"

struct PBRSTUDIO_API FPBRWorkspaceTemplateSettings
{
	FString ProjectDisplayName = TEXT("新项目");
	FString ProjectFolderName;
	bool bCreateSceneEnvironment = true;
};

struct PBRSTUDIO_API FPBRWorkspaceTemplateResult
{
	FString PackageRoot;
	FString ContentRootDirectory;
	TArray<FString> CreatedDirectories;
	bool bCreatedSceneEnvironment = false;
	FString Message;
};

class PBRSTUDIO_API FPBRWorkspaceTemplate
{
public:
	static const TArray<FString>& GetTemplateFolders();
	static FPBRWorkspaceTemplateSettings LoadSettings();
	static void SaveSettings(const FPBRWorkspaceTemplateSettings& Settings);
	static FString MakeSafeEnglishProjectFolderName(const FString& DisplayName);
	static bool CreateTemplate(const FPBRWorkspaceTemplateSettings& Settings, FPBRWorkspaceTemplateResult& OutResult);

private:
	static bool CreateSceneEnvironment(FString& OutMessage);
};
