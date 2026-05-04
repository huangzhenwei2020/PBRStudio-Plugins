#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class PBRSTUDIO_API FPBRDataStore
{
public:
	static FString GetDataDirectory();

	// Download sites
	static bool LoadDownloadSites(TArray<struct FPBRDownloadSite>& OutSites);
	static bool SaveDownloadSites(const TArray<struct FPBRDownloadSite>& Sites);

	// Plugin config
	static bool LoadConfig(TSharedPtr<FJsonObject>& OutConfig);
	static bool SaveConfig(const TSharedPtr<FJsonObject>& Config);

	// Slot learning: key "TargetMode|Channel" -> property name
	static bool LoadSlotLearning(TMap<FString, FString>& OutSlots);
	static bool SaveSlotLearning(const TMap<FString, FString>& Slots);

	// Material sets cache
	static bool LoadMaterialSets(const FString& FileName, TArray<struct FPBRMaterialSet>& OutSets);
	static bool SaveMaterialSets(const FString& FileName, const TArray<struct FPBRMaterialSet>& Sets);

private:
	static bool ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutObject);
	static bool WriteJsonFile(const FString& Path, const TSharedPtr<FJsonObject>& Object);
	static FString GetFilePath(const FString& FileName);
};
