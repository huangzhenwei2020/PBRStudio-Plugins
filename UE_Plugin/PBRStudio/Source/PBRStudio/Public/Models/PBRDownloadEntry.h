#pragma once

#include "CoreMinimal.h"
#include "PBRDownloadEntry.generated.h"

USTRUCT(BlueprintType)
struct PBRSTUDIO_API FPBRDownloadEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString URL;

	UPROPERTY()
	FString Status;

	UPROPERTY()
	FString DetailStatus;

	UPROPERTY()
	float Progress = 0.0f;

	UPROPERTY()
	FString TargetDirectory;

	UPROPERTY()
	FString DownloadedFile;

	UPROPERTY()
	FString Source;

	UPROPERTY()
	bool bAutoStartDownload = false;

	UPROPERTY()
	FString LocalFile;

	// PBR analysis after download
	UPROPERTY()
	bool bPbrOk = false;

	UPROPERTY()
	bool bPbrComplete = false;

	UPROPERTY()
	FString PbrAnalysisMessage;
};
