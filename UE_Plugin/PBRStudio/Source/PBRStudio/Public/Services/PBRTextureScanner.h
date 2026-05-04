#pragma once

#include "CoreMinimal.h"
#include "Models/PBRMaterialSet.h"

class PBRSTUDIO_API FPBRTextureScanner
{
public:
	struct FScanSettings
	{
		FString RootDir;
		bool bRecursive = true;
		bool bGroupByFolder = true;
	};

	static TArray<FPBRMaterialSet> ScanPBRTextureSets(const FScanSettings& Settings);

	static FString DetectPBRChannelFromFilename(const FString& Path);

	static bool IsProbablePBRPreviewImage(const FString& Path);

	static bool IsResolutionToken(const FString& Token);

	static const TArray<FString> ChannelDisplayOrder;

	static const TMap<FString, TArray<FString>> ChannelTokens;

private:
	static void SplitTextureNameTokens(const FString& Path, FString& OutBase, FString& OutCompact, TArray<FString>& OutTokens);

	static FString ChooseBetterMap(const FString& Existing, const FString& Candidate);
};
