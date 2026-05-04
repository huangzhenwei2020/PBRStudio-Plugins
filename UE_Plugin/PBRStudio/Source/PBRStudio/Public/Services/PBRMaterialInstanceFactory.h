#pragma once

#include "CoreMinimal.h"
#include "Models/PBRMaterialSet.h"
#include "Models/PBRMaterialTypes.h"

class UMaterialInstanceConstant;
class UTexture2D;

class PBRSTUDIO_API FPBRMaterialInstanceFactory
{
public:
	static bool CreateInstanceFromSet(
		const FPBRMaterialSet& Set,
		const FPBRMaterialCreateOptions& Options,
		FPBRMaterialCreateResult& OutResult);

	static UMaterialInterface* FindExistingMaterialInstance(const FPBRMaterialSet& Set, const FPBRMaterialCreateOptions& Options, FString& OutInstancePath);
	static UTexture2D* ImportTextureToAsset(const FString& FilePath, const FString& PackagePath, const FString& AssetName);
	static UTexture2D* ImportTextureToAsset(const FString& FilePath, const FString& PackagePath, const FString& AssetName, const FString& Channel);
	static FString SanitizeAssetName(const FString& InName);

private:
	static FString BuildSetPackagePath(const FPBRMaterialSet& Set, const FPBRMaterialCreateOptions& Options);
	static FName GetTextureParameterForChannel(const FString& Channel);
	static void ConfigureTextureForChannel(UTexture2D* Texture, const FString& Channel);
	static void ApplyTextureParameters(UMaterialInstanceConstant* Instance, const TMap<FString, UTexture2D*>& Textures, EPBRMaterialType MaterialType, const FPBRMaterialSet* SourceSet = nullptr);
	static void SavePackages(const TArray<UPackage*>& Packages);
};
