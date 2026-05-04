#pragma once

#include "CoreMinimal.h"
#include "PBRMaterialSet.generated.h"

USTRUCT(BlueprintType)
struct PBRSTUDIO_API FPBRMaterialSet
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Folder;

	// Channel -> full file path (e.g. "BaseColor" -> "D:/textures/diffuse.png")
	UPROPERTY()
	TMap<FString, FString> Channels;

	UPROPERTY()
	TArray<FString> Duplicates;

	UPROPERTY()
	TArray<FString> Unknown;

	UPROPERTY()
	FString PreviewPath;

	UPROPERTY()
	FString Status;

	UPROPERTY()
	FString SlotOverridesKey;

	UPROPERTY()
	FString CreatedSignature;

	// Soft reference to created UMaterial
	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> CreatedMaterial;

	UPROPERTY()
	bool bChecked = true;

	bool IsBasicComplete() const
	{
		bool bHasBaseColor = Channels.Contains(TEXT("BaseColor"));
		bool bHasNormal = Channels.Contains(TEXT("Normal"))
			|| Channels.Contains(TEXT("NormalDX"))
			|| Channels.Contains(TEXT("NormalGL"));
		bool bHasRoughness = Channels.Contains(TEXT("Roughness"))
			|| Channels.Contains(TEXT("Glossiness"));
		return bHasBaseColor && bHasNormal && bHasRoughness;
	}
};
