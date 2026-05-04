#pragma once

#include "CoreMinimal.h"
#include "PBRTextureEntry.generated.h"

USTRUCT(BlueprintType)
struct PBRSTUDIO_API FPBRTextureEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString File;

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	UPROPERTY()
	bool bExists = false;

	UPROPERTY()
	FString Reader;

	UPROPERTY()
	FString OutputPath;

	UPROPERTY()
	int32 OutputWidth = 0;

	UPROPERTY()
	int32 OutputHeight = 0;

	UPROPERTY()
	FString Channel;

	UPROPERTY()
	FString Status;

	UPROPERTY()
	TArray<FString> OwnerNodeNames;

	UPROPERTY()
	int32 TexMapCount = 0;

	bool IsOutputReady() const
	{
		return !OutputPath.IsEmpty() && FPaths::FileExists(OutputPath);
	}
};
