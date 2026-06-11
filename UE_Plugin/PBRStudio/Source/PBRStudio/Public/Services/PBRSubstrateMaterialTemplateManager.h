#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceConstant;

enum class EPBRSubstrateTemplateType : uint8
{
	Standard,
	Wood,
	Stone,
	Tile,
	Leather,
	Plastic,
	Metal,
	Transparent,
	Water,
	Glass,
	CarPaint,
	Leaf,
	Fabric,
	Emissive
};

class PBRSTUDIO_API FPBRSubstrateMaterialTemplateManager
{
public:
	static const TArray<EPBRSubstrateTemplateType>& GetAllTemplateTypes();
	static FString GetTemplateDisplayName(EPBRSubstrateTemplateType TemplateType);
	static FString GetTemplatePackagePath(EPBRSubstrateTemplateType TemplateType);
	static FString GetExampleMaterialInstancePackagePath(EPBRSubstrateTemplateType TemplateType);

	static FString GetStandardTemplatePackagePath();
	static UMaterial* EnsureTemplateMaterial(EPBRSubstrateTemplateType TemplateType, FString& OutMessage);
	static UMaterialInstanceConstant* EnsureExampleMaterialInstance(EPBRSubstrateTemplateType TemplateType, FString& OutMessage);
	static UMaterial* EnsureStandardTemplateMaterial(FString& OutMessage);
	static UMaterialInstanceConstant* EnsureStandardExampleMaterialInstance(FString& OutMessage);
	static int32 EnsureAllTemplateMaterials(TArray<FString>& OutMessages);

private:
	static UMaterial* CreateOrRebuildTemplate(EPBRSubstrateTemplateType TemplateType, const FString& PackagePath, const FString& AssetName, FString& OutMessage);
	static void SaveMaterial(UMaterial* Material);
};
