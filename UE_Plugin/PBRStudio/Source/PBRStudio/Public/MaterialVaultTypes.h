#pragma once

#include "CoreMinimal.h"
#include "MaterialVaultTypes.generated.h"

UENUM()
enum class EMaterialVaultAssetRole : uint8
{
	RootMaterial,
	Material,
	MaterialInstance,
	Texture,
	MaterialFunction,
	Other
};

USTRUCT()
struct PBRSTUDIO_API FMaterialVaultAssetEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath OriginalObjectPath;

	UPROPERTY()
	FString OriginalPackageName;

	UPROPERTY()
	FString PlannedPackageName;

	UPROPERTY()
	FString PlannedObjectPath;

	UPROPERTY()
	FString AssetClass;

	UPROPERTY()
	FString ThumbnailPath;

	UPROPERTY()
	EMaterialVaultAssetRole Role = EMaterialVaultAssetRole::Other;
};

USTRUCT()
struct PBRSTUDIO_API FMaterialVaultItem
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	UPROPERTY()
	FString DisplayName;

	UPROPERTY()
	FString Category;

	UPROPERTY()
	FString SourceEngineVersion;

	UPROPERTY()
	FMaterialVaultAssetEntry RootAsset;

	UPROPERTY()
	TArray<FMaterialVaultAssetEntry> Dependencies;
};
