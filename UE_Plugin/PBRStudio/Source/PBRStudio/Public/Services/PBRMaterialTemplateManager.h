#pragma once

#include "CoreMinimal.h"
#include "Models/PBRMaterialTypes.h"

class UMaterial;
class UMaterialInstanceConstant;

class PBRSTUDIO_API FPBRMaterialTemplateManager
{
public:
	static FString GetTemplatePackagePath(EPBRMaterialType MaterialType);
	static UMaterial* EnsureTemplateMaterial(EPBRMaterialType MaterialType, FString& OutMessage);
	static int32 EnsureAllTemplateMaterials(TArray<FString>& OutMessages);
	static int32 EnsureSpecialTemplateMaterials(TArray<FString>& OutMessages);
	static UMaterialInstanceConstant* EnsureExampleMaterialInstance(EPBRMaterialType MaterialType, FString& OutMessage);

private:
	static FString GetTemplateAssetName(EPBRMaterialType MaterialType);
	static UMaterial* CreateStandardTemplate(const FString& PackagePath, const FString& AssetName, EPBRMaterialType MaterialType, FString& OutMessage);
	static void SaveMaterial(UMaterial* Material);
};
