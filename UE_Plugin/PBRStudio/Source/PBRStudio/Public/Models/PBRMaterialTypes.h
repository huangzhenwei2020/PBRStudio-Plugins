#pragma once

#include "CoreMinimal.h"
#include "PBRMaterialTypes.generated.h"

UENUM()
enum class EPBRMaterialType : uint8
{
	Standard,
	Wood,
	Stone,
	Tile,
	Fabric,
	Leather,
	Plastic,
	Metal,
	Transparent,
	Glass,
	Water,
	Emissive
};

struct PBRSTUDIO_API FPBRChannels
{
	static const FName BaseColor;
	static const FName Normal;
	static const FName NormalDX;
	static const FName NormalGL;
	static const FName Roughness;
	static const FName Glossiness;
	static const FName Metallic;
	static const FName Specular;
	static const FName AO;
	static const FName Opacity;
	static const FName Height;
	static const FName Displacement;
	static const FName Emissive;
	static const FName ORM;
	static const FName ARM;
	static const FName ClearCoat;
	static const FName ClearCoatRoughness;
	static const FName Anisotropy;
	static const FName Thickness;
};

struct PBRSTUDIO_API FPBRMaterialParameters
{
	static const FName BaseColorTexture;
	static const FName UseBaseColorTexture;
	static const FName BaseColorTint;
	static const FName BaseColorIntensity;
	static const FName NormalTexture;
	static const FName UseNormalTexture;
	static const FName NormalStrength;
	static const FName RoughnessTexture;
	static const FName UseRoughnessTexture;
	static const FName RoughnessValue;
	static const FName RoughnessMultiplier;
	static const FName MetallicTexture;
	static const FName UseMetallicTexture;
	static const FName MetallicValue;
	static const FName MetallicMultiplier;
	static const FName AOTexture;
	static const FName UseAOTexture;
	static const FName AOValue;
	static const FName AOMultiplier;
	static const FName OpacityTexture;
	static const FName UseOpacityTexture;
	static const FName Opacity;
	static const FName EmissiveTexture;
	static const FName UseEmissiveTexture;
	static const FName EmissiveColor;
	static const FName EmissiveIntensity;
	static const FName SpecularTexture;
	static const FName UseSpecularTexture;
	static const FName SpecularLevel;
	static const FName HeightTexture;
	static const FName UseHeightTexture;
	static const FName HeightStrength;
	static const FName PixelDepthOffsetStrength;
	static const FName ClearCoat;
	static const FName ClearCoatRoughness;
	static const FName ClearCoatTexture;
	static const FName UseClearCoatTexture;
	static const FName ClearCoatRoughnessTexture;
	static const FName UseClearCoatRoughnessTexture;
	static const FName Anisotropy;
	static const FName FabricFuzzColor;
	static const FName FabricFuzzStrength;
	static const FName RefractionAmount;
	static const FName WaterColor;
	static const FName WaterFlowSpeedU;
	static const FName WaterFlowSpeedV;
	static const FName WaterRippleScale;
	static const FName WaterRippleStrength;
	static const FName UVTiling;
	static const FName UVOffset;
	static const FName UVUTiling;
	static const FName UVVTiling;
	static const FName UVUOffset;
	static const FName UVVOffset;
	static const FName UVRotationDegrees;
};

USTRUCT()
struct PBRSTUDIO_API FPBRMaterialCreateOptions
{
	GENERATED_BODY()

	UPROPERTY()
	EPBRMaterialType MaterialType = EPBRMaterialType::Standard;

	UPROPERTY()
	FString PackageRoot = TEXT("/Game/PBRStudio/Materials");

	UPROPERTY()
	FString MaterialInstancePrefix = TEXT("MI_");

	UPROPERTY()
	bool bImportTextures = true;

	UPROPERTY()
	bool bCreateIsolatedMaterialFolder = true;

	UPROPERTY()
	FString NormalPreference = TEXT("DirectX");
};

USTRUCT()
struct PBRSTUDIO_API FPBRMaterialCreateResult
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> MaterialInstance;

	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> ParentMaterial;

	UPROPERTY()
	TMap<FString, TSoftObjectPtr<UTexture2D>> ImportedTextures;

	UPROPERTY()
	FString PackagePath;

	UPROPERTY()
	FString Message;

	UPROPERTY()
	bool bSkippedBecauseExists = false;
};
