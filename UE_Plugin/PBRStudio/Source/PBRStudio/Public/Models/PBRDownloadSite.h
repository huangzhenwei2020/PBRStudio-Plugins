#pragma once

#include "CoreMinimal.h"
#include "PBRDownloadSite.generated.h"

USTRUCT(BlueprintType)
struct PBRSTUDIO_API FPBRDownloadSite
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString License;

	UPROPERTY()
	FString URL;

	UPROPERTY()
	FString Note;
};
