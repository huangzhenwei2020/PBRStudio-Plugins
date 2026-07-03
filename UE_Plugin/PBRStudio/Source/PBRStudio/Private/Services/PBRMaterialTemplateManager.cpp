#include "Services/PBRMaterialTemplateManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/TextureFactory.h"
#include "FileHelpers.h"
#include "Services/PBRMaterialFunctionLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDistanceFieldGradient.h"
#include "Materials/MaterialExpressionDistanceToNearestSurface.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionRayTracingQualitySwitch.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionRotator.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionShadowReplace.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionLightVector.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

static float Hash01(int32 X, int32 Y, int32 Seed)
{
	uint32 N = static_cast<uint32>(X * 374761393 + Y * 668265263 + Seed * 1442695041);
	N = (N ^ (N >> 13)) * 1274126177;
	return static_cast<float>(N ^ (N >> 16)) / static_cast<float>(MAX_uint32);
}

static FColor LerpColor(const FLinearColor& A, const FLinearColor& B, float Alpha)
{
	return FLinearColor::LerpUsingHSV(A, B, FMath::Clamp(Alpha, 0.0f, 1.0f)).ToFColor(true);
}

static FString GetTemplateChineseName(EPBRMaterialType MaterialType);
static float GetTemplateDefaultRoughness(EPBRMaterialType MaterialType);
static float GetTemplateDefaultOpacity(EPBRMaterialType MaterialType);
static float GetTemplateDefaultMetallic(EPBRMaterialType MaterialType);
static UObject* LoadAssetIfExistsQuietly(const FString& PackagePath);
static UTexture2D* GetDemoTextureForParameter(const FName& ParameterName);

static constexpr float PBRARMDefaultWaterFlowSpeedU = 0.18f;
static constexpr float PBRARMDefaultWaterFlowSpeedV = 0.09f;
static constexpr float PBRARMDefaultWaterRippleScale = 18.0f;
static constexpr float PBRARMDefaultWaterRippleStrength = 0.8f;
static constexpr const TCHAR* PBRStudioPluginTemplateRoot = TEXT("/PBRStudio/Templates/");
static constexpr const TCHAR* PBRStudioProjectTemplateRoot = TEXT("/Game/PBRStudio/Templates/");
static constexpr const TCHAR* PBRStudioPluginFunctionRoot = TEXT("/PBRStudio/Functions/");
static constexpr const TCHAR* PBRStudioProjectFunctionRoot = TEXT("/Game/PBRStudio/Functions/");

static FString GetDemoSourceSetName(EPBRMaterialType MaterialType)
{
	switch (MaterialType)
	{
	case EPBRMaterialType::Wood:
		return TEXT("Wood");
	case EPBRMaterialType::Stone:
		return TEXT("Stone");
	case EPBRMaterialType::Tile:
		return TEXT("Tile");
	case EPBRMaterialType::Fabric:
		return TEXT("Fabric");
	case EPBRMaterialType::Leather:
		return TEXT("Leather");
	case EPBRMaterialType::Plastic:
		return TEXT("Plastic");
	case EPBRMaterialType::Metal:
		return TEXT("Metal");
	case EPBRMaterialType::Transparent:
	case EPBRMaterialType::Glass:
		return TEXT("Glass");
	case EPBRMaterialType::Water:
		return TEXT("Water");
	case EPBRMaterialType::Emissive:
		return TEXT("Emissive");
	case EPBRMaterialType::Standard:
	default:
		return TEXT("Standard");
	}
}

static FString GetDemoSourceAssetId(const FString& SourceSetName)
{
	if (SourceSetName == TEXT("Wood"))
	{
		return TEXT("Wood095");
	}
	if (SourceSetName == TEXT("Fabric"))
	{
		return TEXT("Fabric081C");
	}
	if (SourceSetName == TEXT("Leather"))
	{
		return TEXT("Leather037");
	}
	if (SourceSetName == TEXT("Plastic"))
	{
		return TEXT("Plastic010");
	}
	if (SourceSetName == TEXT("Metal"))
	{
		return TEXT("Metal063");
	}
	if (SourceSetName == TEXT("Water"))
	{
		return TEXT("Ice003");
	}
	if (SourceSetName == TEXT("Glass"))
	{
		return TEXT("Facade001");
	}
	if (SourceSetName == TEXT("Emissive"))
	{
		return TEXT("Onyx013");
	}
	if (SourceSetName == TEXT("Stone"))
	{
		return TEXT("Concrete048");
	}
	if (SourceSetName == TEXT("Tile"))
	{
		return TEXT("PavingStones150");
	}
	return TEXT("Marble012");
}

static FString GetDemoChannelName(const FName& ParameterName)
{
	if (ParameterName == FPBRMaterialParameters::BaseColorTexture || ParameterName == FPBRMaterialParameters::EmissiveTexture)
	{
		return TEXT("BaseColor");
	}
	if (ParameterName == FPBRMaterialParameters::NormalTexture)
	{
		return TEXT("NormalDX");
	}
	if (ParameterName == FPBRMaterialParameters::WaterRippleTexture ||
		ParameterName == FPBRMaterialParameters::GlassDirtTexture ||
		ParameterName == FPBRMaterialParameters::GlassDistortionTexture ||
		ParameterName == FPBRMaterialParameters::GlassFrostedTexture)
	{
		return TEXT("Roughness");
	}
	if (ParameterName == FPBRMaterialParameters::RoughnessTexture)
	{
		return TEXT("Roughness");
	}
	if (ParameterName == FPBRMaterialParameters::MetallicTexture)
	{
		return TEXT("Metallic");
	}
	if (ParameterName == FPBRMaterialParameters::AOTexture)
	{
		return TEXT("AO");
	}
	if (ParameterName == FPBRMaterialParameters::OpacityTexture)
	{
		return TEXT("Opacity");
	}
	if (ParameterName == FPBRMaterialParameters::SpecularTexture)
	{
		return TEXT("Specular");
	}
	if (ParameterName == FPBRMaterialParameters::HeightTexture)
	{
		return TEXT("Height");
	}
	if (ParameterName == FPBRMaterialParameters::ClearCoatTexture)
	{
		return TEXT("ClearCoat");
	}
	if (ParameterName == FPBRMaterialParameters::ClearCoatRoughnessTexture)
	{
		return TEXT("ClearCoatRoughness");
	}
	return TEXT("BaseColor");
}

static bool IsColorTextureParameter(const FName& ParameterName)
{
	return ParameterName == FPBRMaterialParameters::BaseColorTexture ||
		ParameterName == FPBRMaterialParameters::EmissiveTexture;
}

static FString GetDemoSourceFilePath(EPBRMaterialType MaterialType, const FName& ParameterName)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PBRStudio"));
	if (!Plugin.IsValid())
	{
		return FString();
	}

	const FString SpecialRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("SpecialExamples"));
	if (MaterialType == EPBRMaterialType::Water)
	{
		const FString WaterDir = FPaths::Combine(SpecialRoot, TEXT("Water"));
		if (ParameterName == FPBRMaterialParameters::BaseColorTexture)
		{
			const FString Candidate = FPaths::Combine(WaterDir, TEXT("Water_001_COLOR.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::NormalTexture)
		{
			const FString Candidate = FPaths::Combine(WaterDir, TEXT("Water_001_NORM.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::WaterRippleTexture)
		{
			const FString Candidate = FPaths::Combine(WaterDir, TEXT("Water_001_DISP.png"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::AOTexture)
		{
			const FString Candidate = FPaths::Combine(WaterDir, TEXT("Water_001_OCC.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::SpecularTexture)
		{
			const FString Candidate = FPaths::Combine(WaterDir, TEXT("Water_001_SPEC.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::HeightTexture)
		{
			const FString Candidate = FPaths::Combine(WaterDir, TEXT("Water_001_DISP.png"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
	}
	if (MaterialType == EPBRMaterialType::Standard && ParameterName == FPBRMaterialParameters::HeightTexture)
	{
		const FString StoneDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("AmbientCG"), TEXT("Stone"), TEXT("Concrete048_Height.jpg"));
		if (FPaths::FileExists(StoneDir)) { return StoneDir; }
		const FString TileDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("AmbientCG"), TEXT("Tile"), TEXT("PavingStones150_Height.jpg"));
		if (FPaths::FileExists(TileDir)) { return TileDir; }
	}
	if (MaterialType == EPBRMaterialType::Glass || MaterialType == EPBRMaterialType::Transparent)
	{
		const FString GlassDir = FPaths::Combine(SpecialRoot, TEXT("Glass"));
		if (ParameterName == FPBRMaterialParameters::BaseColorTexture)
		{
			const FString Candidate = FPaths::Combine(GlassDir, TEXT("Water_Droplets_001_basecolor.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::NormalTexture)
		{
			const FString Candidate = FPaths::Combine(GlassDir, TEXT("Water_Droplets_001_normal.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::RoughnessTexture)
		{
			const FString Candidate = FPaths::Combine(GlassDir, TEXT("Water_Droplets_001_roughness.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::AOTexture)
		{
			const FString Candidate = FPaths::Combine(GlassDir, TEXT("Water_Droplets_001_ambientOcclusion.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::OpacityTexture)
		{
			const FString Candidate = FPaths::Combine(GlassDir, TEXT("Water_Droplets_001_mask.jpg"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
		if (ParameterName == FPBRMaterialParameters::HeightTexture)
		{
			const FString Candidate = FPaths::Combine(GlassDir, TEXT("Water_Droplets_001_height.png"));
			if (FPaths::FileExists(Candidate)) { return Candidate; }
		}
	}

	const FString SourceSetName = GetDemoSourceSetName(MaterialType);
	const FString AssetId = GetDemoSourceAssetId(SourceSetName);
	const FString ChannelName = GetDemoChannelName(ParameterName);
	const FString Candidate = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("AmbientCG"), SourceSetName, AssetId + TEXT("_") + ChannelName + TEXT(".jpg"));
	if (FPaths::FileExists(Candidate))
	{
		return Candidate;
	}

	if (ChannelName == TEXT("Metallic"))
	{
		return FString();
	}
	if (ChannelName == TEXT("AO"))
	{
		const FString RoughnessFallback = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("AmbientCG"), SourceSetName, AssetId + TEXT("_Roughness.jpg"));
		return FPaths::FileExists(RoughnessFallback) ? RoughnessFallback : FString();
	}
	if (ChannelName == TEXT("Opacity"))
	{
		const FString BaseColorFallback = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("AmbientCG"), SourceSetName, AssetId + TEXT("_BaseColor.jpg"));
		return FPaths::FileExists(BaseColorFallback) ? BaseColorFallback : FString();
	}
	return FString();
}

static FString GetDemoTextureAssetName(EPBRMaterialType MaterialType, const FName& ParameterName)
{
	const FString SourceSetName = GetDemoSourceSetName(MaterialType);
	const FString AssetId = GetDemoSourceAssetId(SourceSetName);
	return TEXT("T_Demo_") + SourceSetName + TEXT("_") + AssetId + TEXT("_") + GetDemoChannelName(ParameterName);
}

enum class EPBRPaintExampleType : uint8
{
	LatexPaint,
	Microcement
};

static FString GetPaintExampleAssetId(EPBRPaintExampleType PaintType)
{
	return PaintType == EPBRPaintExampleType::Microcement ? TEXT("Microcement") : TEXT("LatexPaint");
}

static FString GetPaintExampleChineseName(EPBRPaintExampleType PaintType)
{
	return PaintType == EPBRPaintExampleType::Microcement ? TEXT("微水泥") : TEXT("乳胶漆");
}

static FString GetPaintExampleSourceFilePath(EPBRPaintExampleType PaintType, const FName& ParameterName)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PBRStudio"));
	if (!Plugin.IsValid())
	{
		return FString();
	}

	const FString AssetId = GetPaintExampleAssetId(PaintType);
	const FString ChannelName = GetDemoChannelName(ParameterName);
	const FString Candidate = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("AmbientCG"),
		TEXT("Paint"),
		AssetId,
		AssetId + TEXT("_") + ChannelName + TEXT(".jpg"));
	return FPaths::FileExists(Candidate) ? Candidate : FString();
}

static UTexture2D* CreatePaintDemoTextureAsset(EPBRPaintExampleType PaintType, const FName& ParameterName)
{
	const FString AssetId = GetPaintExampleAssetId(PaintType);
	const FString AssetName = TEXT("T_Demo_Paint_") + AssetId + TEXT("_") + GetDemoChannelName(ParameterName);
	const FString AssetPath = TEXT("/Game/PBRStudio/Templates/DemoTextures/") + AssetName;
	if (UTexture2D* Existing = Cast<UTexture2D>(LoadAssetIfExistsQuietly(AssetPath)))
	{
		return Existing;
	}
	if (UTexture2D* Existing = FindObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName)))
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return GetDemoTextureForParameter(ParameterName);
	}
	if (UTexture2D* Existing = FindObject<UTexture2D>(Package, *AssetName))
	{
		return Existing;
	}

	const FString SourceFilePath = GetPaintExampleSourceFilePath(PaintType, ParameterName);
	if (!SourceFilePath.IsEmpty())
	{
		UTextureFactory* Factory = NewObject<UTextureFactory>();
		Factory->AddToRoot();
		Factory->SuppressImportOverwriteDialog();
		Factory->ColorSpaceMode = IsColorTextureParameter(ParameterName) ? ETextureSourceColorSpace::SRGB : ETextureSourceColorSpace::Linear;
		Factory->bCreateMaterial = false;
		Factory->bEditorImport = true;

		bool bCancelled = false;
		UObject* Imported = Factory->FactoryCreateFile(
			UTexture2D::StaticClass(),
			Package,
			FName(*AssetName),
			RF_Public | RF_Standalone,
			SourceFilePath,
			TEXT(""),
			GWarn,
			bCancelled);

		Factory->RemoveFromRoot();

		if (UTexture2D* ImportedTexture = Cast<UTexture2D>(Imported))
		{
			ImportedTexture->SRGB = IsColorTextureParameter(ParameterName);
			if (ParameterName == FPBRMaterialParameters::NormalTexture)
			{
				ImportedTexture->CompressionSettings = TextureCompressionSettings::TC_Normalmap;
				ImportedTexture->bFlipGreenChannel = false;
			}
			else if (!IsColorTextureParameter(ParameterName))
			{
				ImportedTexture->CompressionSettings = TextureCompressionSettings::TC_Masks;
			}
			ImportedTexture->PostEditChange();
			FAssetRegistryModule::AssetCreated(ImportedTexture);
			Package->SetDirtyFlag(true);
			UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
			return ImportedTexture;
		}
	}

	return GetDemoTextureForParameter(ParameterName);
}

static EPBRMaterialType GetMaterialTypeFromAssetName(const FString& AssetName)
{
	if (AssetName.Contains(TEXT("Wood")))
	{
		return EPBRMaterialType::Wood;
	}
	if (AssetName.Contains(TEXT("Stone")))
	{
		return EPBRMaterialType::Stone;
	}
	if (AssetName.Contains(TEXT("Tile")))
	{
		return EPBRMaterialType::Tile;
	}
	if (AssetName.Contains(TEXT("Fabric")))
	{
		return EPBRMaterialType::Fabric;
	}
	if (AssetName.Contains(TEXT("Leather")))
	{
		return EPBRMaterialType::Leather;
	}
	if (AssetName.Contains(TEXT("Plastic")))
	{
		return EPBRMaterialType::Plastic;
	}
	if (AssetName.Contains(TEXT("Metal")))
	{
		return EPBRMaterialType::Metal;
	}
	if (AssetName.Contains(TEXT("Transparent")))
	{
		return EPBRMaterialType::Transparent;
	}
	if (AssetName.Contains(TEXT("Glass")))
	{
		return EPBRMaterialType::Glass;
	}
	if (AssetName.Contains(TEXT("Water")))
	{
		return EPBRMaterialType::Water;
	}
	if (AssetName.Contains(TEXT("Emissive")))
	{
		return EPBRMaterialType::Emissive;
	}
	return EPBRMaterialType::Standard;
}

static UTexture2D* LoadTexture(const TCHAR* AssetPath)
{
	return LoadObject<UTexture2D>(nullptr, AssetPath);
}

static UTexture2D* GetDefaultTextureForSampler(EMaterialSamplerType SamplerType)
{
	switch (SamplerType)
	{
	case EMaterialSamplerType::SAMPLERTYPE_Normal:
		return LoadTexture(TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
	case EMaterialSamplerType::SAMPLERTYPE_Masks:
	case EMaterialSamplerType::SAMPLERTYPE_LinearColor:
		return LoadTexture(TEXT("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks.DefaultDiffuse_TC_Masks"));
	case EMaterialSamplerType::SAMPLERTYPE_Color:
	default:
		return LoadTexture(TEXT("/Engine/EngineMaterials/T_Default_BaseColor.T_Default_BaseColor"));
	}
}

static UTexture2D* GetDemoTextureForParameter(const FName& ParameterName)
{
	if (ParameterName == FPBRMaterialParameters::NormalTexture)
	{
		return GetDefaultTextureForSampler(EMaterialSamplerType::SAMPLERTYPE_Normal);
	}
	if (ParameterName == FPBRMaterialParameters::RoughnessTexture ||
		ParameterName == FPBRMaterialParameters::MetallicTexture ||
		ParameterName == FPBRMaterialParameters::AOTexture ||
		ParameterName == FPBRMaterialParameters::OpacityTexture ||
		ParameterName == FPBRMaterialParameters::SpecularTexture ||
		ParameterName == FPBRMaterialParameters::HeightTexture ||
		ParameterName == FPBRMaterialParameters::ClearCoatTexture ||
		ParameterName == FPBRMaterialParameters::ClearCoatRoughnessTexture)
	{
		return GetDefaultTextureForSampler(EMaterialSamplerType::SAMPLERTYPE_Masks);
	}
	return GetDefaultTextureForSampler(EMaterialSamplerType::SAMPLERTYPE_Color);
}

static FString SanitizeDemoAssetName(const FString& Name)
{
	FString Result = Name;
	const TCHAR* BadChars = TEXT(" /\\:;,.[]{}()+-=*?\"'<>|");
	for (const TCHAR* It = BadChars; *It; ++It)
	{
		Result.ReplaceCharInline(*It, TCHAR('_'));
	}
	return Result;
}

static bool TryFindDownloadedSpecialExampleFile(const FString& AssetFolder, const TArray<FString>& Tokens, FString& OutFilePath)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PBRStudio"));
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString Root = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("SpecialExamples"), TEXT("Downloaded"), AssetFolder);
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.*"), true, false);
	for (const FString& File : Files)
	{
		const FString Extension = FPaths::GetExtension(File).ToLower();
		if (Extension != TEXT("jpg") && Extension != TEXT("jpeg") && Extension != TEXT("png"))
		{
			continue;
		}

		const FString FileName = FPaths::GetCleanFilename(File).ToLower();
		bool bMatchesAll = true;
		for (const FString& Token : Tokens)
		{
			if (!FileName.Contains(Token.ToLower()))
			{
				bMatchesAll = false;
				break;
			}
		}
		if (bMatchesAll)
		{
			OutFilePath = File;
			return true;
		}
	}
	return false;
}

static FString GetSpecialDemoSourceFilePath(const FString& Kind)
{
	FString FilePath;
	if (Kind.Contains(TEXT("Leaf")) && TryFindDownloadedSpecialExampleFile(TEXT("LeafSet030_1K-JPG"), { TEXT("color") }, FilePath))
	{
		return FilePath;
	}
	if (Kind.Contains(TEXT("WindowScreen")) && TryFindDownloadedSpecialExampleFile(TEXT("Fence007A_1K-JPG"), { TEXT("color") }, FilePath))
	{
		return FilePath;
	}
	if (Kind.Contains(TEXT("Lampshade")) && TryFindDownloadedSpecialExampleFile(TEXT("Fabric080_1K-JPG"), { TEXT("color") }, FilePath))
	{
		return FilePath;
	}
	if (Kind.Contains(TEXT("CarPaint")) && TryFindDownloadedSpecialExampleFile(TEXT("Metal032_1K-JPG"), { TEXT("color") }, FilePath))
	{
		return FilePath;
	}
	if ((Kind.Contains(TEXT("Decal")) || Kind.Contains(TEXT("Grime")) || Kind.Contains(TEXT("Damage"))) &&
		TryFindDownloadedSpecialExampleFile(TEXT("AsphaltDamage001_1K-JPG"), { TEXT("color") }, FilePath))
	{
		return FilePath;
	}
	return FString();
}

static FColor GenerateSpecialDemoPixel(const FString& Kind, int32 X, int32 Y, int32 Size)
{
	const float U = static_cast<float>(X) / static_cast<float>(Size - 1);
	const float V = static_cast<float>(Y) / static_cast<float>(Size - 1);
	const float CenterX = U - 0.5f;
	const float CenterY = V - 0.5f;
	const float Radius = FMath::Sqrt(CenterX * CenterX + CenterY * CenterY);
	const float Noise = Hash01(X / 4, Y / 4, 701);

	if (Kind.Contains(TEXT("Crack")))
	{
		const float MainCrack = FMath::Abs(V - (0.52f + 0.08f * FMath::Sin(U * 28.0f)));
		const float Branch = FMath::Abs((V - 0.25f) - (U - 0.35f) * 0.9f);
		const uint8 Alpha = (MainCrack < 0.018f || Branch < 0.012f) ? 255 : static_cast<uint8>(FMath::Max(0.0f, 80.0f - Radius * 120.0f));
		return FColor(18, 18, 16, Alpha);
	}
	if (Kind.Contains(TEXT("Grime")) || Kind.Contains(TEXT("Damage")))
	{
		const float Blob = FMath::Clamp(1.0f - Radius * 2.4f + Noise * 0.65f, 0.0f, 1.0f);
		const uint8 Alpha = static_cast<uint8>(Blob * 230.0f);
		return FColor(static_cast<uint8>(35 + Noise * 35), static_cast<uint8>(31 + Noise * 28), static_cast<uint8>(25 + Noise * 20), Alpha);
	}
	if (Kind.Contains(TEXT("Smoke")))
	{
		const float Puff = FMath::Clamp(1.0f - Radius * 1.9f + Noise * 0.45f + FMath::Sin(V * 18.0f + Noise * 3.0f) * 0.08f, 0.0f, 1.0f);
		const uint8 Value = static_cast<uint8>(120 + Puff * 120);
		return FColor(Value, Value, Value, static_cast<uint8>(Puff * 210));
	}
	if (Kind.Contains(TEXT("Fire")))
	{
		const float Flame = FMath::Clamp((1.0f - V) * 1.25f - FMath::Abs(CenterX) * 1.8f + Noise * 0.55f, 0.0f, 1.0f);
		return FColor(255, static_cast<uint8>(70 + Flame * 160), static_cast<uint8>(Flame * 45), static_cast<uint8>(Flame * 240));
	}
	if (Kind.Contains(TEXT("Lightning")))
	{
		const float BoltX = 0.5f + 0.08f * FMath::Sin(V * 42.0f) + 0.03f * FMath::Sin(V * 103.0f);
		const float Dist = FMath::Abs(U - BoltX);
		const float Glow = FMath::Clamp(1.0f - Dist * 28.0f, 0.0f, 1.0f);
		return FColor(static_cast<uint8>(90 + Glow * 165), static_cast<uint8>(170 + Glow * 85), 255, static_cast<uint8>(Glow * 255));
	}
	if (Kind.Contains(TEXT("Hologram")))
	{
		const bool Line = (Y % 18) < 3;
		const float Edge = FMath::Clamp(1.0f - Radius * 1.6f, 0.0f, 1.0f);
		const uint8 Alpha = static_cast<uint8>((Line ? 0.85f : 0.25f) * Edge * 220.0f);
		return FColor(32, 210, 255, Alpha);
	}
	if (Kind.Contains(TEXT("CarPaint")))
	{
		const float Flake = Hash01(X, Y, 819) > 0.975f ? 1.0f : 0.0f;
		const FColor Base = LerpColor(FLinearColor(0.02f, 0.025f, 0.035f), FLinearColor(0.75f, 0.02f, 0.015f), 0.8f + Noise * 0.1f);
		return Flake > 0.0f ? FColor(255, 210, 160, 255) : Base;
	}
	if (Kind.Contains(TEXT("Abyss")))
	{
		const float Angle = FMath::Atan2(CenterY, CenterX);
		const float Spiral = 0.5f + 0.5f * FMath::Sin(Angle * 5.0f + Radius * 42.0f);
		const float Stars = Hash01(X, Y, 947) > 0.992f ? 1.0f : 0.0f;
		const float Glow = FMath::Clamp((1.0f - Radius * 1.8f) * Spiral + Stars, 0.0f, 1.0f);
		return FColor(static_cast<uint8>(8 + Glow * 55), static_cast<uint8>(10 + Glow * 75), static_cast<uint8>(18 + Glow * 150), static_cast<uint8>(220 + Glow * 35));
	}
	if (Kind.Contains(TEXT("Leaf")))
	{
		const float LeafShape = FMath::Clamp(1.0f - FMath::Pow(FMath::Abs(CenterX) * 1.35f, 2.0f) - FMath::Pow(FMath::Abs(CenterY) * 2.05f, 2.0f), 0.0f, 1.0f);
		const float Vein = FMath::Abs(CenterX) < 0.018f || FMath::Abs(CenterY - FMath::Abs(CenterX) * 0.75f) < 0.012f ? 1.0f : 0.0f;
		const uint8 Alpha = LeafShape > 0.08f ? 255 : 0;
		const FColor Green = LerpColor(FLinearColor(0.05f, 0.22f, 0.05f), FLinearColor(0.38f, 0.68f, 0.18f), LeafShape * 0.8f + Noise * 0.18f);
		return Vein > 0.0f ? FColor(170, 210, 90, Alpha) : FColor(Green.R, Green.G, Green.B, Alpha);
	}
	if (Kind.Contains(TEXT("WindowScreen")))
	{
		const int32 Cell = 16;
		const bool Thread = (X % Cell) < 3 || (Y % Cell) < 3;
		const uint8 Alpha = Thread ? 220 : 18;
		const uint8 Value = Thread ? static_cast<uint8>(135 + Noise * 45) : 25;
		return FColor(Value, Value, Value, Alpha);
	}
	if (Kind.Contains(TEXT("VideoBars")))
	{
		const int32 Bar = FMath::Clamp(X * 7 / Size, 0, 6);
		static const FColor Bars[7] = {
			FColor(230, 230, 230, 255),
			FColor(230, 220, 40, 255),
			FColor(40, 220, 220, 255),
			FColor(40, 220, 55, 255),
			FColor(220, 45, 220, 255),
			FColor(220, 45, 45, 255),
			FColor(35, 55, 220, 255)
		};
		const bool Scanline = (Y % 8) == 0;
		FColor Color = Bars[Bar];
		if (Scanline)
		{
			Color.R = static_cast<uint8>(Color.R * 0.65f);
			Color.G = static_cast<uint8>(Color.G * 0.65f);
			Color.B = static_cast<uint8>(Color.B * 0.65f);
		}
		return Color;
	}
	if (Kind.Contains(TEXT("Lampshade")))
	{
		const bool Weave = (X % 12) < 3 || (Y % 14) < 3;
		const float Warm = FMath::Clamp(1.0f - Radius * 1.35f + Noise * 0.25f, 0.0f, 1.0f);
		FColor Color = LerpColor(FLinearColor(0.72f, 0.50f, 0.28f), FLinearColor(1.0f, 0.82f, 0.48f), Warm);
		if (Weave)
		{
			Color.R = static_cast<uint8>(Color.R * 0.82f);
			Color.G = static_cast<uint8>(Color.G * 0.78f);
			Color.B = static_cast<uint8>(Color.B * 0.65f);
		}
		Color.A = static_cast<uint8>(160 + Warm * 80.0f);
		return Color;
	}

	const float Soft = FMath::Clamp(1.0f - Radius * 2.0f + Noise * 0.25f, 0.0f, 1.0f);
	return FColor(220, 220, 220, static_cast<uint8>(Soft * 255));
}

static UTexture2D* CreateSpecialDemoTextureAsset(const FString& Kind, bool bColorTexture)
{
	const FString AssetName = TEXT("T_Demo_Special_") + SanitizeDemoAssetName(Kind);
	const FString AssetPath = TEXT("/Game/PBRStudio/SpecialMaterials/DemoTextures/") + AssetName;
	if (UTexture2D* Existing = Cast<UTexture2D>(LoadAssetIfExistsQuietly(AssetPath)))
	{
		return Existing;
	}

	const FString SourceFilePath = GetSpecialDemoSourceFilePath(Kind);
	if (!SourceFilePath.IsEmpty())
	{
		UPackage* Package = CreatePackage(*AssetPath);
		if (Package)
		{
			UTextureFactory* Factory = NewObject<UTextureFactory>();
			Factory->AddToRoot();
			Factory->SuppressImportOverwriteDialog();
			Factory->bCreateMaterial = false;
			Factory->bEditorImport = true;
			bool bCancelled = false;
			UObject* Imported = Factory->FactoryCreateFile(
				UTexture2D::StaticClass(),
				Package,
				FName(*AssetName),
				RF_Public | RF_Standalone,
				SourceFilePath,
				TEXT(""),
				GWarn,
				bCancelled);
			Factory->RemoveFromRoot();
			if (UTexture2D* ImportedTexture = Cast<UTexture2D>(Imported))
			{
				ImportedTexture->SRGB = bColorTexture;
				ImportedTexture->CompressionSettings = bColorTexture ? TextureCompressionSettings::TC_Default : TextureCompressionSettings::TC_Masks;
				ImportedTexture->PostEditChange();
				FAssetRegistryModule::AssetCreated(ImportedTexture);
				Package->SetDirtyFlag(true);
				UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
				return ImportedTexture;
			}
		}
	}

	constexpr int32 Size = 256;
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return GetDefaultTextureForSampler(bColorTexture ? EMaterialSamplerType::SAMPLERTYPE_Color : EMaterialSamplerType::SAMPLERTYPE_Masks);
	}

	UTexture2D* Texture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Texture)
	{
		return GetDefaultTextureForSampler(bColorTexture ? EMaterialSamplerType::SAMPLERTYPE_Color : EMaterialSamplerType::SAMPLERTYPE_Masks);
	}

	Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8);
	uint8* Pixels = Texture->Source.LockMip(0);
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const FColor Pixel = GenerateSpecialDemoPixel(Kind, X, Y, Size);
			const int32 PixelIndex = (Y * Size + X) * 4;
			Pixels[PixelIndex + 0] = Pixel.B;
			Pixels[PixelIndex + 1] = Pixel.G;
			Pixels[PixelIndex + 2] = Pixel.R;
			Pixels[PixelIndex + 3] = Pixel.A;
		}
	}
	Texture->Source.UnlockMip(0);
	Texture->SRGB = bColorTexture;
	Texture->CompressionSettings = bColorTexture ? TextureCompressionSettings::TC_Default : TextureCompressionSettings::TC_Masks;
	Texture->PostEditChange();
	FAssetRegistryModule::AssetCreated(Texture);
	Package->SetDirtyFlag(true);
	UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
	return Texture;
}

static FColor GenerateDemoBaseColorPixel(EPBRMaterialType MaterialType, int32 X, int32 Y, int32 Size)
{
	const float U = static_cast<float>(X) / static_cast<float>(Size);
	const float V = static_cast<float>(Y) / static_cast<float>(Size);
	const float Noise = Hash01(X / 4, Y / 4, static_cast<int32>(MaterialType) + 11);

	switch (MaterialType)
	{
	case EPBRMaterialType::Wood:
	{
		const float Rings = 0.5f + 0.5f * FMath::Sin((U * 18.0f + FMath::Sin(V * 12.0f) * 0.8f) * PI);
		return LerpColor(FLinearColor(0.30f, 0.16f, 0.07f), FLinearColor(0.78f, 0.48f, 0.22f), Rings * 0.75f + Noise * 0.18f);
	}
	case EPBRMaterialType::Stone:
		return LerpColor(FLinearColor(0.25f, 0.25f, 0.25f), FLinearColor(0.72f, 0.70f, 0.66f), Noise);
	case EPBRMaterialType::Tile:
	{
		const int32 Grid = 64;
		const bool bGrout = (X % Grid < 4) || (Y % Grid < 4);
		return bGrout ? FColor(45, 45, 42) : LerpColor(FLinearColor(0.62f, 0.60f, 0.55f), FLinearColor(0.86f, 0.84f, 0.78f), Noise * 0.5f);
	}
	case EPBRMaterialType::Fabric:
	{
		const bool Warp = (X % 10) < 3;
		const bool Weft = (Y % 12) < 3;
		const float Thread = (Warp ? 0.25f : 0.0f) + (Weft ? 0.25f : 0.0f) + Noise * 0.18f;
		return LerpColor(FLinearColor(0.22f, 0.24f, 0.32f), FLinearColor(0.55f, 0.58f, 0.68f), Thread);
	}
	case EPBRMaterialType::Leather:
		return LerpColor(FLinearColor(0.16f, 0.07f, 0.03f), FLinearColor(0.48f, 0.22f, 0.10f), Noise * 0.85f);
	case EPBRMaterialType::Plastic:
		return LerpColor(FLinearColor(0.05f, 0.08f, 0.12f), FLinearColor(0.18f, 0.42f, 0.80f), 0.65f + Noise * 0.1f);
	case EPBRMaterialType::Metal:
		return LerpColor(FLinearColor(0.42f, 0.42f, 0.40f), FLinearColor(0.90f, 0.88f, 0.82f), 0.55f + Noise * 0.2f);
	case EPBRMaterialType::Transparent:
		return FColor(190, 210, 220, 180);
	case EPBRMaterialType::Glass:
		return FColor(175, 215, 235, 120);
	case EPBRMaterialType::Water:
		return LerpColor(FLinearColor(0.03f, 0.25f, 0.35f), FLinearColor(0.10f, 0.58f, 0.75f), 0.45f + 0.35f * FMath::Sin((U + V) * 18.0f));
	case EPBRMaterialType::Emissive:
		return LerpColor(FLinearColor(0.02f, 0.06f, 0.08f), FLinearColor(0.0f, 0.95f, 1.0f), (X / 32) % 2 == 0 ? 1.0f : 0.25f);
	case EPBRMaterialType::Standard:
	default:
		return LerpColor(FLinearColor(0.45f, 0.43f, 0.38f), FLinearColor(0.72f, 0.70f, 0.64f), Noise);
	}
}

static UTexture2D* CreateDemoTextureAsset(EPBRMaterialType MaterialType, const FName& ParameterName)
{
	const FString AssetName = GetDemoTextureAssetName(MaterialType, ParameterName);
	const FString AssetPath = TEXT("/Game/PBRStudio/Templates/DemoTextures/") + AssetName;
	if (UTexture2D* Existing = Cast<UTexture2D>(LoadAssetIfExistsQuietly(AssetPath)))
	{
		return Existing;
	}
	if (UTexture2D* Existing = FindObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName)))
	{
		return Existing;
	}

	constexpr int32 Size = 256;
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return GetDemoTextureForParameter(ParameterName);
	}
	if (UTexture2D* Existing = FindObject<UTexture2D>(Package, *AssetName))
	{
		return Existing;
	}

	const FString SourceFilePath = GetDemoSourceFilePath(MaterialType, ParameterName);
	if (!SourceFilePath.IsEmpty())
	{
		UTextureFactory* Factory = NewObject<UTextureFactory>();
		Factory->AddToRoot();
		Factory->SuppressImportOverwriteDialog();
		Factory->ColorSpaceMode = IsColorTextureParameter(ParameterName) ? ETextureSourceColorSpace::SRGB : ETextureSourceColorSpace::Linear;

		bool bCancelled = false;
		UObject* Imported = Factory->FactoryCreateFile(
			UTexture2D::StaticClass(),
			Package,
			FName(*AssetName),
			RF_Public | RF_Standalone,
			SourceFilePath,
			TEXT(""),
			GWarn,
			bCancelled);

		Factory->RemoveFromRoot();

		if (UTexture2D* ImportedTexture = Cast<UTexture2D>(Imported))
		{
			ImportedTexture->SRGB = IsColorTextureParameter(ParameterName);
			if (ParameterName == FPBRMaterialParameters::NormalTexture)
			{
				ImportedTexture->CompressionSettings = TextureCompressionSettings::TC_Normalmap;
				ImportedTexture->bFlipGreenChannel = false;
			}
			else if (ParameterName != FPBRMaterialParameters::BaseColorTexture && ParameterName != FPBRMaterialParameters::EmissiveTexture)
			{
				ImportedTexture->CompressionSettings = TextureCompressionSettings::TC_Masks;
			}
			ImportedTexture->PostEditChange();
			FAssetRegistryModule::AssetCreated(ImportedTexture);
			Package->SetDirtyFlag(true);
			UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
			return ImportedTexture;
		}
	}

	UTexture2D* Texture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Texture)
	{
		return GetDemoTextureForParameter(ParameterName);
	}

	Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8);
	uint8* Pixels = Texture->Source.LockMip(0);
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			FColor Pixel = FColor::White;
			if (ParameterName == FPBRMaterialParameters::BaseColorTexture || ParameterName == FPBRMaterialParameters::EmissiveTexture)
			{
				Pixel = GenerateDemoBaseColorPixel(MaterialType, X, Y, Size);
			}
			else if (ParameterName == FPBRMaterialParameters::NormalTexture)
			{
				const uint8 BumpX = static_cast<uint8>(128 + (Hash01(X / 8, Y / 8, 43) - 0.5f) * 28.0f);
				const uint8 BumpY = static_cast<uint8>(128 + (Hash01(X / 8, Y / 8, 77) - 0.5f) * 28.0f);
				Pixel = FColor(BumpX, BumpY, 255, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::WaterRippleTexture)
			{
				const float WaveA = FMath::Sin((static_cast<float>(X) / Size) * UE_TWO_PI * 8.0f);
				const float WaveB = FMath::Sin((static_cast<float>(Y) / Size) * UE_TWO_PI * 10.96f);
				const uint8 Ripple = static_cast<uint8>(FMath::Clamp((WaveA + WaveB) * 32.0f + 128.0f, 0.0f, 255.0f));
				Pixel = FColor(Ripple, Ripple, Ripple, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::RoughnessTexture)
			{
				const uint8 Roughness = static_cast<uint8>(FMath::Clamp(GetTemplateDefaultRoughness(MaterialType) * 255.0f + (Hash01(X / 8, Y / 8, 91) - 0.5f) * 60.0f, 0.0f, 255.0f));
				Pixel = FColor(Roughness, Roughness, Roughness, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::MetallicTexture)
			{
				const uint8 Metallic = MaterialType == EPBRMaterialType::Metal ? 255 : 0;
				Pixel = FColor(Metallic, Metallic, Metallic, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::AOTexture)
			{
				const uint8 AO = static_cast<uint8>(210 + Hash01(X / 16, Y / 16, 121) * 45.0f);
				Pixel = FColor(AO, AO, AO, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::OpacityTexture)
			{
				const uint8 Opacity = static_cast<uint8>(GetTemplateDefaultOpacity(MaterialType) * 255.0f);
				Pixel = FColor(Opacity, Opacity, Opacity, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::SpecularTexture)
			{
				Pixel = FColor(128, 128, 128, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::HeightTexture)
			{
				const uint8 Height = static_cast<uint8>(128 + (Hash01(X / 8, Y / 8, 151) - 0.5f) * 90.0f);
				Pixel = FColor(Height, Height, Height, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::ClearCoatTexture)
			{
				const uint8 ClearCoat = MaterialType == EPBRMaterialType::Leather || MaterialType == EPBRMaterialType::Plastic || MaterialType == EPBRMaterialType::Wood ? 190 : 0;
				Pixel = FColor(ClearCoat, ClearCoat, ClearCoat, 255);
			}
			else if (ParameterName == FPBRMaterialParameters::ClearCoatRoughnessTexture)
			{
				Pixel = FColor(45, 45, 45, 255);
			}

			const int32 PixelIndex = (Y * Size + X) * 4;
			Pixels[PixelIndex + 0] = Pixel.B;
			Pixels[PixelIndex + 1] = Pixel.G;
			Pixels[PixelIndex + 2] = Pixel.R;
			Pixels[PixelIndex + 3] = Pixel.A;
		}
	}
	Texture->Source.UnlockMip(0);

	Texture->SRGB = ParameterName == FPBRMaterialParameters::BaseColorTexture || ParameterName == FPBRMaterialParameters::EmissiveTexture;
	if (ParameterName == FPBRMaterialParameters::NormalTexture)
	{
		Texture->CompressionSettings = TextureCompressionSettings::TC_Normalmap;
	}
	else if (ParameterName != FPBRMaterialParameters::BaseColorTexture && ParameterName != FPBRMaterialParameters::EmissiveTexture)
	{
		Texture->CompressionSettings = TextureCompressionSettings::TC_Masks;
	}
	Texture->PostEditChange();
	FAssetRegistryModule::AssetCreated(Texture);
	Package->SetDirtyFlag(true);
	UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
	return Texture;
}

static EMaterialSamplerType GetSamplerTypeForParameter(const FName& ParameterName)
{
	if (ParameterName == FPBRMaterialParameters::NormalTexture)
	{
		return EMaterialSamplerType::SAMPLERTYPE_Normal;
	}
	if (ParameterName == FPBRMaterialParameters::RoughnessTexture ||
		ParameterName == FPBRMaterialParameters::MetallicTexture ||
		ParameterName == FPBRMaterialParameters::AOTexture ||
		ParameterName == FPBRMaterialParameters::OpacityTexture ||
		ParameterName == FPBRMaterialParameters::SpecularTexture ||
		ParameterName == FPBRMaterialParameters::HeightTexture ||
		ParameterName == FPBRMaterialParameters::WaterRippleTexture ||
		ParameterName == FPBRMaterialParameters::GlassDirtTexture ||
		ParameterName == FPBRMaterialParameters::GlassDistortionTexture ||
		ParameterName == FPBRMaterialParameters::GlassFrostedTexture ||
		ParameterName == FPBRMaterialParameters::ClearCoatTexture ||
		ParameterName == FPBRMaterialParameters::ClearCoatRoughnessTexture)
	{
		return EMaterialSamplerType::SAMPLERTYPE_Masks;
	}
	return EMaterialSamplerType::SAMPLERTYPE_Color;
}

static bool WireNormalStrength(
	UMaterial* Material,
	UMaterialExpressionTextureSampleParameter2D* NormalTexture,
	UMaterialExpressionScalarParameter* NormalStrength,
	int32 X,
	int32 Y);

static bool BuildTemplateGraph(UMaterial* Material, EPBRMaterialType MaterialType, FString& OutMessage);
static void LayoutMaterialGraphByColumns(UMaterial* Material);
static void LayoutPBRTemplateGraphByGroups(UMaterial* Material);
static int32 RemoveUnusedMaterialExpressions(UMaterial* Material);
static bool BuildSpecialMaterialGraph(UMaterial* Material, const FString& AssetName, FString& OutMessage);
static bool UsesNormal(EPBRMaterialType MaterialType);
static bool UsesRoughness(EPBRMaterialType MaterialType);
static bool UsesMetallic(EPBRMaterialType MaterialType);
static bool UsesAO(EPBRMaterialType MaterialType);
static bool UsesOpacity(EPBRMaterialType MaterialType);
static bool UsesEmissive(EPBRMaterialType MaterialType);
static bool UsesSpecular(EPBRMaterialType MaterialType);
static bool UsesHeight(EPBRMaterialType MaterialType);
static bool UsesClearCoat(EPBRMaterialType MaterialType);
static bool UsesFabricFuzz(EPBRMaterialType MaterialType);
static bool UsesRefraction(EPBRMaterialType MaterialType);
static bool UsesWaterColor(EPBRMaterialType MaterialType);
static UMaterialInstanceConstant* EnsureWaterVariantExampleInstance(UMaterial* ParentMaterial, const FString& Suffix, const FLinearColor& WaterColor, float Roughness, float RippleStrength, FString& OutMessage);
static UMaterialInstanceConstant* EnsureTemplateVariantExampleInstance(
	UMaterial* ParentMaterial,
	EPBRMaterialType MaterialType,
	const FString& Suffix,
	const FLinearColor& BaseColorTint,
	float Opacity,
	float Roughness,
	float Specular,
	float Refraction,
	float EmissiveIntensity,
	FString& OutMessage);

static UMaterialInstanceConstant* EnsurePaintExampleMaterialInstance(
	UMaterial* ParentMaterial,
	EPBRPaintExampleType PaintType,
	FString& OutMessage);

static UMaterialExpression* BuildSharedUVControls(
	UMaterial* Material,
	UMaterialExpressionScalarParameter*& OutUVRotationDegrees,
	const FName& GroupUV);

static bool ConnectTextureSamplesToUV(UMaterial* Material, UMaterialExpression* UVExpression);

static bool BuildDynamicWaterNormal(
	UMaterial* Material,
	UMaterialExpressionTextureSampleParameter2D* NormalTexture,
	UMaterialExpressionScalarParameter* NormalStrength,
	UMaterialExpression* SharedUV,
	const FName& GroupSpecial,
	int32 X,
	int32 Y);

static UMaterialExpressionSingleLayerWaterMaterialOutput* AddSingleLayerWaterOutput(
	UMaterial* Material,
	const FName& GroupName,
	int32 X,
	int32 Y);

static UMaterialExpressionThinTranslucentMaterialOutput* AddThinTranslucentOutput(
	UMaterial* Material,
	UMaterialExpression* TransmittanceColor,
	UMaterialExpression* SurfaceCoverage,
	int32 X,
	int32 Y);

static void AddMaterialGraphComment(
	UMaterial* Material,
	const FString& Text,
	int32 X,
	int32 Y,
	int32 SizeX,
	int32 SizeY,
	const FLinearColor& Color);

static UMaterialExpressionTextureSampleParameter2D* AddTextureParameter(
	UMaterial* Material,
	const FName& ParameterName,
	const FName& GroupName,
	int32 SortPriority,
	int32 X,
	int32 Y,
	EMaterialSamplerType SamplerType = EMaterialSamplerType::SAMPLERTYPE_Color)
{
	const EPBRMaterialType MaterialType = Material ? GetMaterialTypeFromAssetName(Material->GetName()) : EPBRMaterialType::Standard;
	UMaterialExpressionTextureSampleParameter2D* Node = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
	Node->ParameterName = ParameterName;
	Node->Group = GroupName;
	Node->SortPriority = SortPriority;
	Node->SamplerType = SamplerType;
	Node->Texture = CreateDemoTextureAsset(MaterialType, ParameterName);
	if (!Node->Texture)
	{
		Node->Texture = GetDefaultTextureForSampler(SamplerType);
	}
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionTextureSampleParameter2D* AddTextureParameterForType(
	UMaterial* Material,
	EPBRMaterialType DemoMaterialType,
	const FName& ParameterName,
	const FName& GroupName,
	int32 SortPriority,
	int32 X,
	int32 Y,
	EMaterialSamplerType SamplerType = EMaterialSamplerType::SAMPLERTYPE_Color)
{
	UMaterialExpressionTextureSampleParameter2D* Node = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
	Node->ParameterName = ParameterName;
	Node->Group = GroupName;
	Node->SortPriority = SortPriority;
	Node->SamplerType = SamplerType;
	Node->Texture = CreateDemoTextureAsset(DemoMaterialType, ParameterName);
	if (!Node->Texture)
	{
		Node->Texture = GetDefaultTextureForSampler(SamplerType);
	}
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static bool RepairTextureParameterDefaults(UMaterial* Material)
{
	if (!Material)
	{
		return false;
	}

	bool bChanged = false;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		UMaterialExpressionTextureSampleParameter2D* TextureParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
		if (!TextureParameter)
		{
			continue;
		}

		const EMaterialSamplerType DesiredSampler = GetSamplerTypeForParameter(TextureParameter->ParameterName);
		UTexture2D* DesiredTexture = GetDefaultTextureForSampler(DesiredSampler);
		if (TextureParameter->SamplerType != DesiredSampler)
		{
			TextureParameter->SamplerType = DesiredSampler;
			bChanged = true;
		}
		if (DesiredTexture && TextureParameter->Texture != DesiredTexture)
		{
			TextureParameter->Texture = DesiredTexture;
			bChanged = true;
		}
	}

	if (bChanged)
	{
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
	}
	return bChanged;
}

static bool UpgradeNormalStrength(UMaterial* Material)
{
	if (!Material)
	{
		return false;
	}

	UMaterialExpressionTextureSampleParameter2D* NormalTexture = nullptr;
	UMaterialExpressionScalarParameter* NormalStrength = nullptr;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (UMaterialExpressionTextureSampleParameter2D* TextureParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
		{
			if (TextureParameter->ParameterName == FPBRMaterialParameters::NormalTexture)
			{
				NormalTexture = TextureParameter;
			}
		}
		else if (UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			if (ScalarParameter->ParameterName == FPBRMaterialParameters::NormalStrength)
			{
				NormalStrength = ScalarParameter;
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction && FunctionCall->MaterialFunction->GetName().Contains(TEXT("FlattenNormal")))
			{
				return false;
			}
		}
	}

	if (!WireNormalStrength(Material, NormalTexture, NormalStrength, -380, 20))
	{
		return false;
	}

	Material->PreEditChange(nullptr);
	LayoutMaterialGraphByColumns(Material);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	return true;
}

static bool UpgradeMetallicDefault(UMaterial* Material)
{
	if (!Material)
	{
		return false;
	}

	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			if (ScalarParameter->ParameterName == FPBRMaterialParameters::MetallicMultiplier &&
				!FMath::IsNearlyZero(ScalarParameter->DefaultValue))
			{
				ScalarParameter->DefaultValue = 0.0f;
				Material->PreEditChange(nullptr);
				Material->PostEditChange();
				Material->MarkPackageDirty();
				return true;
			}
		}
	}

	return false;
}

static bool UpgradeSharedUVControls(UMaterial* Material)
{
	if (!Material)
	{
		return false;
	}

	UMaterialExpressionVectorParameter* UVTiling = nullptr;
	UMaterialExpressionVectorParameter* UVOffset = nullptr;
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpressionRotator* ExistingRotator = nullptr;

	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			if (VectorParameter->ParameterName == FPBRMaterialParameters::UVTiling)
			{
				UVTiling = VectorParameter;
			}
			else if (VectorParameter->ParameterName == FPBRMaterialParameters::UVOffset)
			{
				UVOffset = VectorParameter;
			}
		}
		else if (UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			if (ScalarParameter->ParameterName == FPBRMaterialParameters::UVRotationDegrees)
			{
				UVRotationDegrees = ScalarParameter;
			}
		}
		else if (UMaterialExpressionRotator* Rotator = Cast<UMaterialExpressionRotator>(Expression))
		{
			ExistingRotator = Rotator;
		}
	}

	UMaterialExpression* UVExpression = ExistingRotator;
	bool bChanged = false;
	if (!UVExpression || !UVTiling || !UVOffset || !UVRotationDegrees)
	{
		const FName GroupUV(TEXT("08 UV 调整"));
		UVExpression = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
		bChanged = true;
	}

	bChanged |= ConnectTextureSamplesToUV(Material, UVExpression);
	if (bChanged)
	{
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
	}
	return bChanged;
}

static void ResetMaterialGraph(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	if (UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
	{
		EditorData->BaseColor.Expression = nullptr;
		EditorData->Metallic.Expression = nullptr;
		EditorData->Specular.Expression = nullptr;
		EditorData->Roughness.Expression = nullptr;
		EditorData->Normal.Expression = nullptr;
		EditorData->EmissiveColor.Expression = nullptr;
		EditorData->Opacity.Expression = nullptr;
		EditorData->OpacityMask.Expression = nullptr;
	EditorData->AmbientOcclusion.Expression = nullptr;
	EditorData->Refraction.Expression = nullptr;
	EditorData->WorldPositionOffset.Expression = nullptr;
	EditorData->PixelDepthOffset.Expression = nullptr;
	EditorData->ClearCoat.Expression = nullptr;
	EditorData->ClearCoatRoughness.Expression = nullptr;
	EditorData->Anisotropy.Expression = nullptr;
	EditorData->SubsurfaceColor.Expression = nullptr;
	EditorData->MaterialAttributes.Expression = nullptr;
	for (FVector2MaterialInput& CustomizedUV : EditorData->CustomizedUVs)
	{
		CustomizedUV.Expression = nullptr;
	}
	EditorData->ParameterGroupData.Empty();
	}

	Material->GetExpressionCollection().Empty();
}

static UMaterialExpressionScalarParameter* AddScalarParameter(
	UMaterial* Material,
	const FName& ParameterName,
	const FName& GroupName,
	int32 SortPriority,
	float DefaultValue,
	int32 X,
	int32 Y)
{
	UMaterialExpressionScalarParameter* Node = NewObject<UMaterialExpressionScalarParameter>(Material);
	Node->ParameterName = ParameterName;
	Node->Group = GroupName;
	Node->SortPriority = SortPriority;
	Node->DefaultValue = DefaultValue;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionVectorParameter* AddVectorParameter(
	UMaterial* Material,
	const FName& ParameterName,
	const FName& GroupName,
	int32 SortPriority,
	const FLinearColor& DefaultValue,
	int32 X,
	int32 Y)
{
	UMaterialExpressionVectorParameter* Node = NewObject<UMaterialExpressionVectorParameter>(Material);
	Node->ParameterName = ParameterName;
	Node->Group = GroupName;
	Node->SortPriority = SortPriority;
	Node->DefaultValue = DefaultValue;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionMultiply* AddMultiply(UMaterial* Material, int32 X, int32 Y)
{
	UMaterialExpressionMultiply* Node = NewObject<UMaterialExpressionMultiply>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionAdd* AddAdd(UMaterial* Material, int32 X, int32 Y)
{
	UMaterialExpressionAdd* Node = NewObject<UMaterialExpressionAdd>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionConstant* AddConstant(UMaterial* Material, float Value, int32 X, int32 Y)
{
	UMaterialExpressionConstant* Node = NewObject<UMaterialExpressionConstant>(Material);
	Node->R = Value;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionConstant3Vector* AddConstant3Vector(UMaterial* Material, const FLinearColor& Value, int32 X, int32 Y)
{
	UMaterialExpressionConstant3Vector* Node = NewObject<UMaterialExpressionConstant3Vector>(Material);
	Node->Constant = Value;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static void AddMaterialGraphComment(
	UMaterial* Material,
	const FString& Text,
	int32 X,
	int32 Y,
	int32 SizeX,
	int32 SizeY,
	const FLinearColor& Color)
{
	if (!Material)
	{
		return;
	}

	UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Material);
	Comment->Text = Text;
	Comment->MaterialExpressionEditorX = X;
	Comment->MaterialExpressionEditorY = Y;
	Comment->SizeX = SizeX;
	Comment->SizeY = SizeY;
	Comment->CommentColor = Color;
	Comment->FontSize = 22;
	Comment->bGroupMode = true;
	Comment->bCommentBubbleVisible_InDetailsPanel = true;
	Comment->bColorCommentBubble = true;
	Material->GetExpressionCollection().AddComment(Comment);
}

static void AddStandardTemplateComments(UMaterial* Material, EPBRMaterialType MaterialType)
{
	AddMaterialGraphComment(Material, TEXT("01 共享 UV 调整"), -2260, -1040, 1000, 980, FLinearColor(0.09f, 0.14f, 0.18f, 1.0f));
	AddMaterialGraphComment(Material, TEXT("02 基础颜色"), -1100, -1040, 1220, 760, FLinearColor(0.16f, 0.10f, 0.08f, 1.0f));
	if (UsesNormal(MaterialType))
	{
		AddMaterialGraphComment(Material, TEXT("03 法线"), -1100, -160, 1220, 560, FLinearColor(0.08f, 0.14f, 0.16f, 1.0f));
	}
	if (UsesRoughness(MaterialType))
	{
		AddMaterialGraphComment(Material, TEXT("04 粗糙度"), -1100, 520, 1220, 600, FLinearColor(0.14f, 0.14f, 0.08f, 1.0f));
	}
	if (UsesMetallic(MaterialType) || UsesAO(MaterialType) || UsesSpecular(MaterialType))
	{
		AddMaterialGraphComment(Material, TEXT("05 金属 / AO / 高光"), -1100, 1240, 1220, 1460, FLinearColor(0.09f, 0.12f, 0.10f, 1.0f));
	}
	if (UsesOpacity(MaterialType))
	{
		AddMaterialGraphComment(Material, TEXT("06 透明"), 360, -160, 1220, 620, FLinearColor(0.08f, 0.12f, 0.18f, 1.0f));
	}
	if (UsesEmissive(MaterialType))
	{
		AddMaterialGraphComment(Material, TEXT("07 自发光"), 360, 600, 1220, 760, FLinearColor(0.16f, 0.10f, 0.06f, 1.0f));
	}
	if (UsesHeight(MaterialType))
	{
		AddMaterialGraphComment(Material, TEXT("08 高度 / 置换"), 360, 1480, 1220, 900, FLinearColor(0.10f, 0.08f, 0.14f, 1.0f));
	}
	if (UsesClearCoat(MaterialType))
	{
		AddMaterialGraphComment(Material, TEXT("09 清漆"), 1720, 600, 1320, 900, FLinearColor(0.08f, 0.14f, 0.14f, 1.0f));
	}
	if (UsesWaterColor(MaterialType) || UsesFabricFuzz(MaterialType) || UsesRefraction(MaterialType))
	{
		const FString SpecialTitle = UsesWaterColor(MaterialType)
			? TEXT("10 水体 / DefaultLit 透明")
			: (MaterialType == EPBRMaterialType::Glass ? TEXT("10 玻璃 / 折射") : TEXT("10 类型专用"));
		AddMaterialGraphComment(Material, SpecialTitle, 1720, -160, 1320, UsesWaterColor(MaterialType) ? 1500 : 900, FLinearColor(0.06f, 0.16f, 0.20f, 1.0f));
	}
	AddMaterialGraphComment(Material, TEXT("11 材质输出"), 3240, -1040, 620, 900, FLinearColor(0.12f, 0.12f, 0.14f, 1.0f));
}

static void AddSpecialGraphComment(UMaterial* Material, const FString& Text, int32 X = -980, int32 Y = -420, int32 SizeX = 980, int32 SizeY = 760)
{
	AddMaterialGraphComment(Material, Text, X, Y, SizeX, SizeY, FLinearColor(0.12f, 0.14f, 0.17f, 1.0f));
}

static UMaterialExpressionSingleLayerWaterMaterialOutput* AddSingleLayerWaterOutput(
	UMaterial* Material,
	const FName& GroupName,
	int32 X,
	int32 Y)
{
	UMaterialExpressionSingleLayerWaterMaterialOutput* Node = NewObject<UMaterialExpressionSingleLayerWaterMaterialOutput>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);

	UMaterialExpressionConstant3Vector* Scattering = AddConstant3Vector(Material, FLinearColor(0.12f, 0.32f, 0.45f), X - 300, Y - 180);
	UMaterialExpressionConstant3Vector* Absorption = AddConstant3Vector(Material, FLinearColor(0.02f, 0.08f, 0.16f), X - 300, Y - 40);
	UMaterialExpressionScalarParameter* PhaseG = AddScalarParameter(Material, FName(TEXT("水体前向散射")), GroupName, 90, 0.35f, X - 300, Y + 100);
	UMaterialExpressionConstant3Vector* BehindWaterColor = AddConstant3Vector(Material, FLinearColor(0.85f, 0.96f, 1.0f), X - 300, Y + 240);

	Node->ScatteringCoefficients.Connect(0, Scattering);
	Node->AbsorptionCoefficients.Connect(0, Absorption);
	Node->PhaseG.Connect(0, PhaseG);
	Node->ColorScaleBehindWater.Connect(0, BehindWaterColor);
	return Node;
}

static UMaterialExpressionThinTranslucentMaterialOutput* AddThinTranslucentOutput(
	UMaterial* Material,
	UMaterialExpression* TransmittanceColor,
	UMaterialExpression* SurfaceCoverage,
	int32 X,
	int32 Y)
{
	UMaterialExpressionThinTranslucentMaterialOutput* Node = NewObject<UMaterialExpressionThinTranslucentMaterialOutput>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	if (TransmittanceColor)
	{
		Node->TransmittanceColor.Connect(0, TransmittanceColor);
	}
	if (SurfaceCoverage)
	{
		Node->SurfaceCoverage.Connect(0, SurfaceCoverage);
	}
	return Node;
}

static UMaterialExpressionStaticSwitchParameter* AddStaticSwitchParameter(
	UMaterial* Material,
	const FName& ParameterName,
	const FName& GroupName,
	int32 SortPriority,
	bool DefaultValue,
	int32 X,
	int32 Y)
{
	UMaterialExpressionStaticSwitchParameter* Node = NewObject<UMaterialExpressionStaticSwitchParameter>(Material);
	Node->ParameterName = ParameterName;
	Node->Group = GroupName;
	Node->SortPriority = SortPriority;
	Node->DefaultValue = DefaultValue;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionDivide* AddDivide(UMaterial* Material, int32 X, int32 Y, float ConstB)
{
	UMaterialExpressionDivide* Node = NewObject<UMaterialExpressionDivide>(Material);
	Node->ConstB = ConstB;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionOneMinus* AddOneMinus(UMaterial* Material, int32 X, int32 Y)
{
	UMaterialExpressionOneMinus* Node = NewObject<UMaterialExpressionOneMinus>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionMaterialFunctionCall* AddMaterialFunctionCall(UMaterial* Material, const TCHAR* FunctionPath, int32 X, int32 Y);
static bool ConnectFunctionInputByName(
	UMaterialExpressionMaterialFunctionCall* FunctionCall,
	const FString& ExpectedName,
	UMaterialExpression* Expression,
	int32 OutputIndex = 0);
static bool ConnectFunctionInputByIndex(
	UMaterialExpressionMaterialFunctionCall* FunctionCall,
	int32 InputIndex,
	UMaterialExpression* Expression,
	int32 OutputIndex = 0);

static UMaterialExpressionFresnel* AddFresnel(UMaterial* Material, int32 X, int32 Y)
{
	UMaterialExpressionFresnel* Node = NewObject<UMaterialExpressionFresnel>(Material);
	Node->Exponent = 4.0f;
	Node->BaseReflectFraction = 0.04f;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionMaterialFunctionCall* AddFresnelGlowFunction(
	UMaterial* Material,
	UMaterialExpression* Color,
	UMaterialExpression* Intensity,
	UMaterialExpression* Exponent,
	int32 X,
	int32 Y)
{
	UMaterialExpressionMaterialFunctionCall* Function = AddMaterialFunctionCall(
		Material,
		TEXT("/Game/PBRStudio/Functions/05_SpecialFX/MF_PBRStudio_FresnelGlow.MF_PBRStudio_FresnelGlow"),
		X,
		Y);
	if (!Function)
	{
		return nullptr;
	}

	const bool bConnected =
		ConnectFunctionInputByName(Function, TEXT("颜色"), Color) &
		ConnectFunctionInputByName(Function, TEXT("强度"), Intensity) &
		ConnectFunctionInputByName(Function, TEXT("衰减"), Exponent);
	return bConnected ? Function : nullptr;
}

static UMaterialExpressionMaterialFunctionCall* AddMaterialFunctionCall(UMaterial* Material, const TCHAR* FunctionPath, int32 X, int32 Y)
{
	static bool bEnsuredPBRStudioFunctions = false;
	const FString RequestedPath(FunctionPath);
	const bool bIsPBRStudioFunction =
		RequestedPath.Contains(PBRStudioProjectFunctionRoot, ESearchCase::CaseSensitive) ||
		RequestedPath.Contains(PBRStudioPluginFunctionRoot, ESearchCase::CaseSensitive);
	if (!bEnsuredPBRStudioFunctions && bIsPBRStudioFunction)
	{
		TArray<FString> Messages;
		FPBRMaterialFunctionLibrary::EnsureAllMaterialFunctions(Messages);
		bEnsuredPBRStudioFunctions = true;
	}

	TArray<FString> CandidatePaths;
	if (RequestedPath.Contains(PBRStudioProjectFunctionRoot, ESearchCase::CaseSensitive))
	{
		CandidatePaths.Add(RequestedPath.Replace(PBRStudioProjectFunctionRoot, PBRStudioPluginFunctionRoot, ESearchCase::CaseSensitive));
	}
	CandidatePaths.Add(RequestedPath);

	UMaterialFunctionInterface* Function = nullptr;
	for (const FString& CandidatePath : CandidatePaths)
	{
		Function = LoadObject<UMaterialFunctionInterface>(nullptr, *CandidatePath);
		if (!Function)
		{
			int32 DotIndex = INDEX_NONE;
			CandidatePath.FindChar(TEXT('.'), DotIndex);
			const FString AssetPath = DotIndex == INDEX_NONE ? CandidatePath : CandidatePath.Left(DotIndex);
			Function = Cast<UMaterialFunctionInterface>(UEditorAssetLibrary::LoadAsset(AssetPath));
		}
		if (Function)
		{
			break;
		}
	}
	if (!Function)
	{
		UE_LOG(LogTemp, Warning, TEXT("PBRStudio: Failed to load material function %s"), FunctionPath);
		return nullptr;
	}

	UMaterialExpressionMaterialFunctionCall* Node = NewObject<UMaterialExpressionMaterialFunctionCall>(Material);
	Node->SetMaterialFunction(Function);
	Node->UpdateFromFunctionResource();
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static bool ConnectFlattenNormalInputs(
	UMaterialExpressionMaterialFunctionCall* FlattenNormal,
	UMaterialExpression* NormalExpression,
	UMaterialExpression* FlatnessExpression)
{
	if (!FlattenNormal || !NormalExpression || !FlatnessExpression)
	{
		return false;
	}

	bool bConnectedNormal = false;
	bool bConnectedFlatness = false;
	for (int32 InputIndex = 0; InputIndex < FlattenNormal->FunctionInputs.Num(); ++InputIndex)
	{
		const FString InputName = FlattenNormal->GetInputName(InputIndex).ToString();
		if (InputName.Contains(TEXT("Normal"), ESearchCase::IgnoreCase))
		{
			FlattenNormal->FunctionInputs[InputIndex].Input.Connect(0, NormalExpression);
			bConnectedNormal = true;
		}
		else if (InputName.Contains(TEXT("Flat"), ESearchCase::IgnoreCase) ||
			InputName.Contains(TEXT("Strength"), ESearchCase::IgnoreCase))
		{
			FlattenNormal->FunctionInputs[InputIndex].Input.Connect(0, FlatnessExpression);
			bConnectedFlatness = true;
		}
	}

	return bConnectedNormal && bConnectedFlatness;
}

static bool ConnectFunctionInputByName(
	UMaterialExpressionMaterialFunctionCall* FunctionCall,
	const FString& ExpectedName,
	UMaterialExpression* Expression,
	int32 OutputIndex)
{
	if (!FunctionCall || !Expression)
	{
		return false;
	}

	for (int32 InputIndex = 0; InputIndex < FunctionCall->FunctionInputs.Num(); ++InputIndex)
	{
		const FString InputName = FunctionCall->GetInputName(InputIndex).ToString();
		if (InputName.Equals(ExpectedName, ESearchCase::IgnoreCase))
		{
			FunctionCall->FunctionInputs[InputIndex].Input.Connect(OutputIndex, Expression);
			return true;
		}
	}
	return false;
}

static bool ConnectFunctionInputByIndex(
	UMaterialExpressionMaterialFunctionCall* FunctionCall,
	int32 InputIndex,
	UMaterialExpression* Expression,
	int32 OutputIndex)
{
	if (!FunctionCall || !Expression || !FunctionCall->FunctionInputs.IsValidIndex(InputIndex))
	{
		return false;
	}

	FunctionCall->FunctionInputs[InputIndex].Input.Connect(OutputIndex, Expression);
	return true;
}

static bool WireNormalStrength(
	UMaterial* Material,
	UMaterialExpressionTextureSampleParameter2D* NormalTexture,
	UMaterialExpressionScalarParameter* NormalStrength,
	int32 X,
	int32 Y)
{
	if (!Material || !NormalTexture || !NormalStrength)
	{
		return false;
	}

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		return false;
	}

	if (UMaterialExpressionMaterialFunctionCall* NormalStrengthFunction = AddMaterialFunctionCall(
		Material,
		TEXT("/Game/PBRStudio/Functions/02_Surface/MF_PBRStudio_NormalStrength.MF_PBRStudio_NormalStrength"),
		X + 220,
		Y))
	{
		const bool bNormalConnected = ConnectFunctionInputByIndex(NormalStrengthFunction, 0, NormalTexture);
		const bool bStrengthConnected = ConnectFunctionInputByIndex(NormalStrengthFunction, 1, NormalStrength);
		if (bNormalConnected && bStrengthConnected)
		{
			EditorData->Normal.Connect(0, NormalStrengthFunction);
			return true;
		}
	}

	UMaterialExpressionOneMinus* StrengthToFlatness = AddOneMinus(Material, X, Y + 40);
	StrengthToFlatness->Input.Connect(0, NormalStrength);

	UMaterialExpressionMaterialFunctionCall* FlattenNormal = AddMaterialFunctionCall(
		Material,
		TEXT("/Engine/Functions/Engine_MaterialFunctions01/Texturing/FlattenNormal.FlattenNormal"),
		X + 220,
		Y);
	if (!FlattenNormal)
	{
		return false;
	}

	if (!ConnectFlattenNormalInputs(FlattenNormal, NormalTexture, StrengthToFlatness))
	{
		return false;
	}

	EditorData->Normal.Connect(0, FlattenNormal);
	return true;
}

static UMaterialExpressionComponentMask* AddMask(UMaterial* Material, int32 X, int32 Y, bool R, bool G, bool B, bool A = false)
{
	UMaterialExpressionComponentMask* Node = NewObject<UMaterialExpressionComponentMask>(Material);
	Node->R = R;
	Node->G = G;
	Node->B = B;
	Node->A = A;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionMax* AddMax(UMaterial* Material, int32 X, int32 Y)
{
	UMaterialExpressionMax* Node = NewObject<UMaterialExpressionMax>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpression* BuildSharedUVControls(
	UMaterial* Material,
	UMaterialExpressionScalarParameter*& OutUVRotationDegrees,
	const FName& GroupUV)
{
	UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
	TexCoord->MaterialExpressionEditorX = -1480;
	TexCoord->MaterialExpressionEditorY = -760;
	Material->GetExpressionCollection().AddExpression(TexCoord);

	UMaterialExpressionScalarParameter* UTiling = AddScalarParameter(Material, FPBRMaterialParameters::UVUTiling, GroupUV, 10, 1.0f, -1480, -620);
	UMaterialExpressionScalarParameter* VTiling = AddScalarParameter(Material, FPBRMaterialParameters::UVVTiling, GroupUV, 20, 1.0f, -1480, -500);
	UMaterialExpressionScalarParameter* UOffset = AddScalarParameter(Material, FPBRMaterialParameters::UVUOffset, GroupUV, 30, 0.0f, -1480, -380);
	UMaterialExpressionScalarParameter* VOffset = AddScalarParameter(Material, FPBRMaterialParameters::UVVOffset, GroupUV, 40, 0.0f, -1480, -260);
	OutUVRotationDegrees = AddScalarParameter(Material, FPBRMaterialParameters::UVRotationDegrees, GroupUV, 50, 0.0f, -1480, -140);

	if (UMaterialExpressionMaterialFunctionCall* UVFunction = AddMaterialFunctionCall(
		Material,
		TEXT("/Game/PBRStudio/Functions/01_UV/MF_PBRStudio_UVControls.MF_PBRStudio_UVControls"),
		-1040,
		-720))
	{
		const bool bConnected =
			ConnectFunctionInputByName(UVFunction, TEXT("UV"), TexCoord) &
			ConnectFunctionInputByName(UVFunction, TEXT("U 平铺"), UTiling) &
			ConnectFunctionInputByName(UVFunction, TEXT("V 平铺"), VTiling) &
			ConnectFunctionInputByName(UVFunction, TEXT("U 偏移"), UOffset) &
			ConnectFunctionInputByName(UVFunction, TEXT("V 偏移"), VOffset) &
			ConnectFunctionInputByName(UVFunction, TEXT("旋转角度"), OutUVRotationDegrees);
		if (bConnected)
		{
			return UVFunction;
		}
	}

	UMaterialExpressionAppendVector* TilingUV = NewObject<UMaterialExpressionAppendVector>(Material);
	TilingUV->MaterialExpressionEditorX = -1260;
	TilingUV->MaterialExpressionEditorY = -620;
	TilingUV->A.Connect(0, UTiling);
	TilingUV->B.Connect(0, VTiling);
	Material->GetExpressionCollection().AddExpression(TilingUV);

	UMaterialExpressionAppendVector* OffsetUV = NewObject<UMaterialExpressionAppendVector>(Material);
	OffsetUV->MaterialExpressionEditorX = -1260;
	OffsetUV->MaterialExpressionEditorY = -380;
	OffsetUV->A.Connect(0, UOffset);
	OffsetUV->B.Connect(0, VOffset);
	Material->GetExpressionCollection().AddExpression(OffsetUV);

	UMaterialExpressionMultiply* TilingMultiply = AddMultiply(Material, -1180, -720);
	TilingMultiply->A.Connect(0, TexCoord);
	TilingMultiply->B.Connect(0, TilingUV);

	UMaterialExpressionAdd* OffsetAdd = AddAdd(Material, -940, -720);
	OffsetAdd->A.Connect(0, TilingMultiply);
	OffsetAdd->B.Connect(0, OffsetUV);

	UMaterialExpressionMultiply* DegreesToRadians = AddMultiply(Material, -940, -520);
	DegreesToRadians->A.Connect(0, OutUVRotationDegrees);
	DegreesToRadians->ConstB = UE_PI / 180.0f;

	UMaterialExpressionRotator* Rotator = NewObject<UMaterialExpressionRotator>(Material);
	Rotator->CenterX = 0.5f;
	Rotator->CenterY = 0.5f;
	Rotator->Speed = 1.0f;
	Rotator->Coordinate.Connect(0, OffsetAdd);
	Rotator->Time.Connect(0, DegreesToRadians);
	Rotator->MaterialExpressionEditorX = -700;
	Rotator->MaterialExpressionEditorY = -720;
	Material->GetExpressionCollection().AddExpression(Rotator);

	return Rotator;
}

static bool ConnectTextureSamplesToUV(UMaterial* Material, UMaterialExpression* UVExpression)
{
	if (!Material || !UVExpression)
	{
		return false;
	}

	bool bChanged = false;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (UMaterialExpressionTextureSampleParameter2D* TextureParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
		{
			if (!TextureParameter->Coordinates.Expression)
			{
				TextureParameter->Coordinates.Connect(0, UVExpression);
				bChanged = true;
			}
		}
	}
	return bChanged;
}

static bool BuildDynamicWaterNormal(
	UMaterial* Material,
	UMaterialExpressionTextureSampleParameter2D* NormalTexture,
	UMaterialExpressionScalarParameter* NormalStrength,
	UMaterialExpression* SharedUV,
	const FName& GroupSpecial,
	int32 X,
	int32 Y)
{
	if (!Material || !NormalTexture || !NormalStrength || !SharedUV)
	{
		return false;
	}

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		return false;
	}

	UMaterialExpressionScalarParameter* FlowU = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedU, GroupSpecial, 40, PBRARMDefaultWaterFlowSpeedU, X, Y);
	UMaterialExpressionScalarParameter* FlowV = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedV, GroupSpecial, 50, PBRARMDefaultWaterFlowSpeedV, X, Y + 120);
	UMaterialExpressionScalarParameter* RippleScale = AddScalarParameter(Material, FPBRMaterialParameters::WaterRippleScale, GroupSpecial, 60, PBRARMDefaultWaterRippleScale, X, Y + 240);
	UMaterialExpressionScalarParameter* RippleStrength = AddScalarParameter(Material, FPBRMaterialParameters::WaterRippleStrength, GroupSpecial, 70, PBRARMDefaultWaterRippleStrength, X, Y + 360);

	UMaterialExpressionAppendVector* FlowSpeed = NewObject<UMaterialExpressionAppendVector>(Material);
	FlowSpeed->MaterialExpressionEditorX = X + 240;
	FlowSpeed->MaterialExpressionEditorY = Y + 40;
	FlowSpeed->A.Connect(0, FlowU);
	FlowSpeed->B.Connect(0, FlowV);
	Material->GetExpressionCollection().AddExpression(FlowSpeed);

	UMaterialExpressionPanner* PannerA = NewObject<UMaterialExpressionPanner>(Material);
	PannerA->MaterialExpressionEditorX = X + 460;
	PannerA->MaterialExpressionEditorY = Y;
	PannerA->Coordinate.Connect(0, SharedUV);
	PannerA->Speed.Connect(0, FlowSpeed);
	Material->GetExpressionCollection().AddExpression(PannerA);

	UMaterialExpressionPanner* PannerB = NewObject<UMaterialExpressionPanner>(Material);
	PannerB->MaterialExpressionEditorX = X + 460;
	PannerB->MaterialExpressionEditorY = Y + 220;
	PannerB->SpeedX = -0.02f;
	PannerB->SpeedY = 0.025f;
	PannerB->Coordinate.Connect(0, SharedUV);
	Material->GetExpressionCollection().AddExpression(PannerB);

	NormalTexture->Coordinates.Connect(0, PannerA);

	UMaterialExpressionStaticSwitchParameter* UseWaterRippleTexture = AddStaticSwitchParameter(
		Material,
		FPBRMaterialParameters::UseWaterRippleTexture,
		GroupSpecial,
		75,
		false,
		X + 700,
		Y + 40);

	UMaterialExpressionTextureSampleParameter2D* SecondaryNormal = AddTextureParameter(
		Material,
		FPBRMaterialParameters::NormalTexture,
		GroupSpecial,
		80,
		X + 700,
		Y + 220,
		EMaterialSamplerType::SAMPLERTYPE_Normal);
	SecondaryNormal->Coordinates.Connect(0, PannerB);

	UMaterialExpressionTextureSampleParameter2D* WaterRippleNormal = AddTextureParameter(
		Material,
		FPBRMaterialParameters::WaterRippleTexture,
		GroupSpecial,
		85,
		X + 700,
		Y + 420,
		EMaterialSamplerType::SAMPLERTYPE_Normal);
	WaterRippleNormal->Coordinates.Connect(0, PannerB);

	UseWaterRippleTexture->A.Connect(0, WaterRippleNormal);
	UseWaterRippleTexture->B.Connect(0, SecondaryNormal);

	UMaterialExpressionMultiply* SecondaryStrength = AddMultiply(Material, X + 980, Y + 220);
	SecondaryStrength->A.Connect(0, UseWaterRippleTexture);
	SecondaryStrength->B.Connect(0, RippleStrength);

	UMaterialExpressionAdd* NormalBlend = AddAdd(Material, X + 1220, Y + 80);
	NormalBlend->A.Connect(0, NormalTexture);
	NormalBlend->B.Connect(0, SecondaryStrength);

	UMaterialExpressionMultiply* NormalScale = AddMultiply(Material, X + 1460, Y + 80);
	NormalScale->A.Connect(0, NormalBlend);
	NormalScale->B.Connect(0, RippleScale);

	UMaterialExpressionMultiply* FinalNormalStrength = AddMultiply(Material, X + 1700, Y + 80);
	FinalNormalStrength->A.Connect(0, NormalScale);
	FinalNormalStrength->B.Connect(0, NormalStrength);
	EditorData->Normal.Connect(0, FinalNormalStrength);
	return true;
}

static UMaterialExpression* BuildWaterFlowUV(
	UMaterial* Material,
	UMaterialExpression* SharedUV,
	const FName& GroupSpecial,
	int32 X,
	int32 Y)
{
	if (!Material || !SharedUV)
	{
		return SharedUV;
	}

	UMaterialExpressionScalarParameter* FlowU = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedU, GroupSpecial, 80, PBRARMDefaultWaterFlowSpeedU, X, Y);
	UMaterialExpressionScalarParameter* FlowV = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedV, GroupSpecial, 90, PBRARMDefaultWaterFlowSpeedV, X, Y + 120);
	UMaterialExpressionAppendVector* FlowSpeed = NewObject<UMaterialExpressionAppendVector>(Material);
	FlowSpeed->MaterialExpressionEditorX = X + 240;
	FlowSpeed->MaterialExpressionEditorY = Y + 40;
	FlowSpeed->A.Connect(0, FlowU);
	FlowSpeed->B.Connect(0, FlowV);
	Material->GetExpressionCollection().AddExpression(FlowSpeed);

	UMaterialExpressionPanner* Panner = NewObject<UMaterialExpressionPanner>(Material);
	Panner->MaterialExpressionEditorX = X + 460;
	Panner->MaterialExpressionEditorY = Y;
	Panner->Coordinate.Connect(0, SharedUV);
	Panner->Speed.Connect(0, FlowSpeed);
	Material->GetExpressionCollection().AddExpression(Panner);
	return Panner;
}

static UMaterialExpression* BuildDynamicPannerUV(
	UMaterial* Material,
	UMaterialExpression* SharedUV,
	const FName& GroupDynamic,
	float DefaultSpeedU,
	float DefaultSpeedV,
	float DefaultScale,
	int32 X,
	int32 Y)
{
	if (!Material || !SharedUV)
	{
		return SharedUV;
	}

	UMaterialExpressionScalarParameter* SpeedU = AddScalarParameter(Material, FPBRMaterialParameters::DynamicSpeedU, GroupDynamic, 10, DefaultSpeedU, X, Y);
	UMaterialExpressionScalarParameter* SpeedV = AddScalarParameter(Material, FPBRMaterialParameters::DynamicSpeedV, GroupDynamic, 20, DefaultSpeedV, X, Y + 120);
	UMaterialExpressionScalarParameter* Scale = AddScalarParameter(Material, FPBRMaterialParameters::DynamicScale, GroupDynamic, 30, DefaultScale, X, Y + 240);

	if (UMaterialExpressionMaterialFunctionCall* DynamicFunction = AddMaterialFunctionCall(
		Material,
		TEXT("/Game/PBRStudio/Functions/02_Dynamic/MF_PBRStudio_DynamicPanner.MF_PBRStudio_DynamicPanner"),
		X + 360,
		Y + 40))
	{
		const bool bConnected =
			ConnectFunctionInputByIndex(DynamicFunction, 0, SharedUV) &
			ConnectFunctionInputByIndex(DynamicFunction, 1, SpeedU) &
			ConnectFunctionInputByIndex(DynamicFunction, 2, SpeedV) &
			ConnectFunctionInputByIndex(DynamicFunction, 3, Scale);
		if (bConnected)
		{
			return DynamicFunction;
		}
	}

	UMaterialExpressionAppendVector* Speed = NewObject<UMaterialExpressionAppendVector>(Material);
	Speed->MaterialExpressionEditorX = X + 260;
	Speed->MaterialExpressionEditorY = Y + 30;
	Speed->A.Connect(0, SpeedU);
	Speed->B.Connect(0, SpeedV);
	Material->GetExpressionCollection().AddExpression(Speed);

	UMaterialExpressionMultiply* ScaledUV = AddMultiply(Material, X + 260, Y + 230);
	ScaledUV->A.Connect(0, SharedUV);
	ScaledUV->B.Connect(0, Scale);

	UMaterialExpressionPanner* Panner = NewObject<UMaterialExpressionPanner>(Material);
	Panner->MaterialExpressionEditorX = X + 520;
	Panner->MaterialExpressionEditorY = Y + 80;
	Panner->Coordinate.Connect(0, ScaledUV);
	Panner->Speed.Connect(0, Speed);
	Material->GetExpressionCollection().AddExpression(Panner);
	return Panner;
}

static bool IsTranslucentMaterialType(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Transparent ||
		MaterialType == EPBRMaterialType::Glass ||
		MaterialType == EPBRMaterialType::Water;
}

static bool UsesNormal(EPBRMaterialType MaterialType)
{
	return MaterialType != EPBRMaterialType::Emissive;
}

static bool UsesRoughness(EPBRMaterialType MaterialType)
{
	return MaterialType != EPBRMaterialType::Emissive;
}

static bool UsesMetallic(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Standard ||
		MaterialType == EPBRMaterialType::Metal;
}

static bool UsesAO(EPBRMaterialType MaterialType)
{
	return MaterialType != EPBRMaterialType::Emissive;
}

static bool UsesOpacity(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Transparent ||
		MaterialType == EPBRMaterialType::Glass ||
		MaterialType == EPBRMaterialType::Water;
}

static bool UsesEmissive(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Emissive;
}

static bool UsesSpecular(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Standard ||
		MaterialType == EPBRMaterialType::Plastic ||
		MaterialType == EPBRMaterialType::Leather ||
		MaterialType == EPBRMaterialType::Metal ||
		MaterialType == EPBRMaterialType::Stone ||
		MaterialType == EPBRMaterialType::Tile ||
		MaterialType == EPBRMaterialType::Wood ||
		MaterialType == EPBRMaterialType::Fabric ||
		MaterialType == EPBRMaterialType::Transparent ||
		MaterialType == EPBRMaterialType::Glass ||
		MaterialType == EPBRMaterialType::Water;
}

static bool UsesHeight(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Stone ||
		MaterialType == EPBRMaterialType::Tile;
}

static bool UsesClearCoat(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Leather;
}

static bool UsesFabricFuzz(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Fabric;
}

static bool UsesRefraction(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Transparent ||
		MaterialType == EPBRMaterialType::Glass ||
		MaterialType == EPBRMaterialType::Water;
}

static bool UsesWaterColor(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Water;
}

static void ConfigureMaterialDomainForType(UMaterial* Material, EPBRMaterialType MaterialType)
{
	if (!Material)
	{
		return;
	}

	switch (MaterialType)
	{
	case EPBRMaterialType::Fabric:
		Material->SetShadingModel(EMaterialShadingModel::MSM_Cloth);
		break;
	case EPBRMaterialType::Leather:
		Material->SetShadingModel(EMaterialShadingModel::MSM_ClearCoat);
		break;
	default:
		Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
		break;
	}

	const bool bTranslucent = IsTranslucentMaterialType(MaterialType);
	Material->BlendMode = bTranslucent ? BLEND_Translucent : BLEND_Opaque;
	Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
	Material->RefractionMethod = MaterialType == EPBRMaterialType::Glass ? RM_IndexOfRefraction : RM_PixelNormalOffset;
	Material->bScreenSpaceReflections = bTranslucent;
	Material->bContactShadows = MaterialType == EPBRMaterialType::Glass;
	Material->bCastRayTracedShadows = MaterialType == EPBRMaterialType::Glass;
	Material->bCastDynamicShadowAsMasked = MaterialType == EPBRMaterialType::Glass;
	Material->bAllowTranslucentLocalLightShadow = MaterialType == EPBRMaterialType::Glass;
	Material->TranslucentLocalLightShadowQuality = MaterialType == EPBRMaterialType::Glass ? 1.0f : 0.0f;
	Material->TranslucentDirectionalLightShadowQuality = MaterialType == EPBRMaterialType::Glass ? 1.0f : 0.0f;
	Material->TranslucentShadowDensityScale = MaterialType == EPBRMaterialType::Glass ? 0.65f : 0.35f;
	Material->TranslucentSelfShadowDensityScale = MaterialType == EPBRMaterialType::Glass ? 0.5f : 0.0f;
	Material->TranslucentSelfShadowSecondDensityScale = MaterialType == EPBRMaterialType::Glass ? 0.25f : 0.0f;
	Material->TranslucentSelfShadowSecondOpacity = MaterialType == EPBRMaterialType::Glass ? 0.35f : 0.0f;
	Material->TranslucentBackscatteringExponent = MaterialType == EPBRMaterialType::Glass ? 8.0f : 0.0f;
	Material->TranslucentShadowStartOffset = 0.0f;
	Material->SetOverrideCastShadowAsMasked(MaterialType == EPBRMaterialType::Glass);
	Material->SetCastShadowAsMasked(MaterialType == EPBRMaterialType::Glass);
	Material->TwoSided = MaterialType == EPBRMaterialType::Fabric || MaterialType == EPBRMaterialType::Water;
}

static float GetTemplateDefaultRoughness(EPBRMaterialType MaterialType)
{
	switch (MaterialType)
	{
	case EPBRMaterialType::Glass:
		return 0.05f;
	case EPBRMaterialType::Water:
		return 0.02f;
	case EPBRMaterialType::Fabric:
		return 0.85f;
	case EPBRMaterialType::Leather:
		return 0.45f;
	case EPBRMaterialType::Plastic:
		return 0.35f;
	case EPBRMaterialType::Metal:
		return 0.28f;
	case EPBRMaterialType::Wood:
	case EPBRMaterialType::Stone:
	case EPBRMaterialType::Tile:
	case EPBRMaterialType::Transparent:
	case EPBRMaterialType::Standard:
	default:
		return 0.5f;
	}
}

static float GetTemplateDefaultMetallic(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Metal ? 1.0f : 0.0f;
}

static float GetTemplateDefaultOpacity(EPBRMaterialType MaterialType)
{
	switch (MaterialType)
	{
	case EPBRMaterialType::Glass:
		return 0.35f;
	case EPBRMaterialType::Water:
		return 0.65f;
	case EPBRMaterialType::Transparent:
		return 0.75f;
	default:
		return 1.0f;
	}
}

static float GetTemplateDefaultRefraction(EPBRMaterialType MaterialType)
{
	switch (MaterialType)
	{
	case EPBRMaterialType::Water:
		return 1.33f;
	case EPBRMaterialType::Glass:
		return 1.52f;
	case EPBRMaterialType::Transparent:
	default:
		return 1.0f;
	}
}

static FString GetTemplateChineseName(EPBRMaterialType MaterialType)
{
	switch (MaterialType)
	{
	case EPBRMaterialType::Wood:
		return TEXT("木材");
	case EPBRMaterialType::Stone:
		return TEXT("石材");
	case EPBRMaterialType::Tile:
		return TEXT("瓷砖");
	case EPBRMaterialType::Fabric:
		return TEXT("布艺");
	case EPBRMaterialType::Leather:
		return TEXT("皮革");
	case EPBRMaterialType::Plastic:
		return TEXT("塑料");
	case EPBRMaterialType::Metal:
		return TEXT("金属");
	case EPBRMaterialType::Transparent:
		return TEXT("半透明");
	case EPBRMaterialType::Glass:
		return TEXT("玻璃");
	case EPBRMaterialType::Water:
		return TEXT("水");
	case EPBRMaterialType::Emissive:
		return TEXT("自发光");
	case EPBRMaterialType::Standard:
	default:
		return TEXT("标准");
	}
}

static TArray<EPBRMaterialType> GetAllTemplateMaterialTypes()
{
	return {
		EPBRMaterialType::Standard,
		EPBRMaterialType::Wood,
		EPBRMaterialType::Stone,
		EPBRMaterialType::Tile,
		EPBRMaterialType::Fabric,
		EPBRMaterialType::Leather,
		EPBRMaterialType::Plastic,
		EPBRMaterialType::Metal,
		EPBRMaterialType::Transparent,
		EPBRMaterialType::Glass,
		EPBRMaterialType::Water,
		EPBRMaterialType::Emissive
	};
}

static UObject* LoadAssetIfExistsQuietly(const FString& PackagePath)
{
	return UEditorAssetLibrary::DoesAssetExist(PackagePath)
		? UEditorAssetLibrary::LoadAsset(PackagePath)
		: nullptr;
}

static UMaterial* EnsureSpecialMaterialAsset(const FString& AssetName, FString& OutMessage)
{
	const FString PackagePath = TEXT("/Game/PBRStudio/SpecialMaterials/") + AssetName;
	if (UObject* Existing = LoadAssetIfExistsQuietly(PackagePath))
	{
		if (UMaterial* ExistingMaterial = Cast<UMaterial>(Existing))
		{
			ResetMaterialGraph(ExistingMaterial);
			BuildSpecialMaterialGraph(ExistingMaterial, AssetName, OutMessage);
			UEditorLoadingAndSavingUtils::SavePackages({ ExistingMaterial->GetPackage() }, true);
			OutMessage = TEXT("已重建特殊材质图表");
			return ExistingMaterial;
		}
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutMessage = TEXT("创建特殊材质包失败");
		return nullptr;
	}

	UMaterial* Material = NewObject<UMaterial>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Material)
	{
		OutMessage = TEXT("创建特殊材质失败");
		return nullptr;
	}

	BuildSpecialMaterialGraph(Material, AssetName, OutMessage);
	FAssetRegistryModule::AssetCreated(Material);
	Package->SetDirtyFlag(true);
	UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
	OutMessage = TEXT("已创建特殊材质");
	return Material;
}

static FString GetSpecialExampleName(const FString& AssetName)
{
	if (AssetName == TEXT("SM_Decal_DBuffer_Color"))
	{
		return TEXT("MI_示例_贴花材质");
	}
	if (AssetName == TEXT("SM_UI_Texture"))
	{
		return TEXT("MI_示例_UI材质");
	}
	if (AssetName == TEXT("SM_Niagara_Particle_Soft"))
	{
		return TEXT("MI_示例_Niagara粒子材质");
	}
	if (AssetName == TEXT("SM_RuntimeVirtualTexture_Output"))
	{
		return TEXT("MI_示例_RVT输出材质");
	}
	if (AssetName == TEXT("SM_DistanceField_Blend"))
	{
		return TEXT("MI_示例_DistanceField混合材质");
	}
	if (AssetName == TEXT("SM_PostProcess_Tint"))
	{
		return TEXT("MI_示例_后处理调色");
	}
	if (AssetName == TEXT("SM_LightFunction_SoftMask"))
	{
		return TEXT("MI_示例_灯光遮罩");
	}
	if (AssetName == TEXT("SM_Hologram_Fresnel"))
	{
		return TEXT("MI_示例_全息边缘光");
	}
	if (AssetName == TEXT("SM_Niagara_Smoke_Soft"))
	{
		return TEXT("MI_示例_烟雾粒子");
	}
	if (AssetName == TEXT("SM_Niagara_Lightning"))
	{
		return TEXT("MI_示例_闪电粒子");
	}
	if (AssetName == TEXT("SM_CarPaint_ClearCoat"))
	{
		return TEXT("MI_示例_车漆清漆");
	}
	if (AssetName == TEXT("SM_Abyss_Mirror"))
	{
		return TEXT("MI_示例_深渊镜");
	}
	if (AssetName == TEXT("SM_TwoSided_Leaf"))
	{
		return TEXT("MI_示例_双面树叶");
	}
	if (AssetName == TEXT("SM_WindowScreen_Mesh"))
	{
		return TEXT("MI_示例_窗纱");
	}
	if (AssetName == TEXT("SM_Video_Screen"))
	{
		return TEXT("MI_示例_视频屏幕");
	}
	if (AssetName == TEXT("SM_Lampshade_Translucent"))
	{
		return TEXT("MI_示例_透光灯罩");
	}
	return TEXT("MI_示例_特殊材质");
}

static UMaterialInstanceConstant* EnsureSpecialExampleMaterialInstance(UMaterial* ParentMaterial, const FString& ParentAssetName, FString& OutMessage)
{
	if (!ParentMaterial)
	{
		OutMessage = TEXT("Special material parent unavailable");
		return nullptr;
	}

	const FString AssetName = GetSpecialExampleName(ParentAssetName);
	const FString PackagePath = TEXT("/Game/PBRStudio/SpecialMaterials/Examples/") + AssetName;
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(LoadAssetIfExistsQuietly(PackagePath));
	if (!Instance)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			OutMessage = TEXT("Failed to create special example package");
			return nullptr;
		}
		Instance = NewObject<UMaterialInstanceConstant>(Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!Instance)
		{
			OutMessage = TEXT("Failed to create special example instance");
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Instance);
		Package->SetDirtyFlag(true);
	}

	Instance->SetParentEditorOnly(ParentMaterial);
	Instance->ClearParameterValuesEditorOnly();
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor::White);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, ParentAssetName == TEXT("SM_DistanceField_Blend") ? 100.0f : 0.85f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, ParentAssetName == TEXT("SM_Niagara_Particle_Soft") ? 3.0f : 1.0f);
	if (ParentAssetName == TEXT("SM_PostProcess_Tint"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor(1.02f, 0.96f, 0.88f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 1.0f);
	}
	else if (ParentAssetName == TEXT("SM_LightFunction_SoftMask"))
	{
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.65f);
	}
	else if (ParentAssetName == TEXT("SM_Hologram_Fresnel"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor(0.12f, 0.78f, 1.0f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 4.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.45f);
	}
	else if (ParentAssetName == TEXT("SM_Niagara_Smoke_Soft"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor(0.55f, 0.58f, 0.62f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 0.8f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.55f);
	}
	else if (ParentAssetName == TEXT("SM_Niagara_Lightning"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor(0.2f, 0.65f, 1.0f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 8.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.9f);
	}
	else if (ParentAssetName == TEXT("SM_CarPaint_ClearCoat"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor(0.85f, 0.02f, 0.015f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.18f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoat, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatRoughness, 0.04f);
	}
	else if (ParentAssetName == TEXT("SM_Abyss_Mirror"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor(0.05f, 0.22f, 0.85f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 3.5f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.88f);
	}
	else if (ParentAssetName == TEXT("SM_TwoSided_Leaf"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor(0.42f, 0.8f, 0.24f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.62f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.55f);
	}
	else if (ParentAssetName == TEXT("SM_WindowScreen_Mesh"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor(0.62f, 0.62f, 0.58f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.55f);
	}
	else if (ParentAssetName == TEXT("SM_Video_Screen"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor::White);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 2.0f);
	}
	else if (ParentAssetName == TEXT("SM_Lampshade_Translucent"))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor(1.0f, 0.72f, 0.38f));
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor(1.0f, 0.58f, 0.22f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.58f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 1.4f);
	}
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.5f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::HeightStrength, 0.5f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVRotationDegrees, 0.0f);
	Instance->PostEditChange();
	UEditorLoadingAndSavingUtils::SavePackages({ Instance->GetPackage() }, true);
	OutMessage = TEXT("Special example instance created");
	return Instance;
}

FString FPBRMaterialTemplateManager::GetTemplateAssetName(EPBRMaterialType MaterialType)
{
	switch (MaterialType)
	{
	case EPBRMaterialType::Wood:
		return TEXT("M_PBR_Wood");
	case EPBRMaterialType::Stone:
		return TEXT("M_PBR_Stone");
	case EPBRMaterialType::Tile:
		return TEXT("M_PBR_Tile");
	case EPBRMaterialType::Fabric:
		return TEXT("M_PBR_Fabric");
	case EPBRMaterialType::Leather:
		return TEXT("M_PBR_Leather");
	case EPBRMaterialType::Plastic:
		return TEXT("M_PBR_Plastic");
	case EPBRMaterialType::Metal:
		return TEXT("M_PBR_Metal");
	case EPBRMaterialType::Transparent:
		return TEXT("M_PBR_Transparent");
	case EPBRMaterialType::Glass:
		return TEXT("M_PBR_Glass");
	case EPBRMaterialType::Water:
		return TEXT("M_PBR_Water");
	case EPBRMaterialType::Emissive:
		return TEXT("M_PBR_Emissive");
	case EPBRMaterialType::Standard:
	default:
		return TEXT("M_PBR_Standard");
	}
}

FString FPBRMaterialTemplateManager::GetTemplatePackagePath(EPBRMaterialType MaterialType)
{
	return FString(PBRStudioPluginTemplateRoot) + GetTemplateAssetName(MaterialType);
}

FString FPBRMaterialTemplateManager::GetExampleMaterialInstancePackagePath(EPBRMaterialType MaterialType)
{
	return TEXT("/Game/PBRStudio/Templates/Examples/MI_示例_") + GetTemplateChineseName(MaterialType);
}

UMaterial* FPBRMaterialTemplateManager::EnsureTemplateMaterial(EPBRMaterialType MaterialType, FString& OutMessage)
{
	const FString FullPath = GetTemplatePackagePath(MaterialType);
	if (UObject* Existing = LoadAssetIfExistsQuietly(FullPath))
	{
		if (UMaterial* ExistingMaterial = Cast<UMaterial>(Existing))
		{
			ResetMaterialGraph(ExistingMaterial);
			BuildTemplateGraph(ExistingMaterial, MaterialType, OutMessage);
			SaveMaterial(ExistingMaterial);
			OutMessage = TEXT("已重建插件自带母材质图表并清理多余节点");
			return ExistingMaterial;
		}
	}

	const FString AssetName = GetTemplateAssetName(MaterialType);
	switch (MaterialType)
	{
	case EPBRMaterialType::Standard:
	case EPBRMaterialType::Wood:
	case EPBRMaterialType::Stone:
	case EPBRMaterialType::Tile:
	case EPBRMaterialType::Fabric:
	case EPBRMaterialType::Leather:
	case EPBRMaterialType::Plastic:
	case EPBRMaterialType::Metal:
	case EPBRMaterialType::Transparent:
	case EPBRMaterialType::Glass:
	case EPBRMaterialType::Water:
	case EPBRMaterialType::Emissive:
	default:
		if (UMaterial* PluginMaterial = CreateStandardTemplate(FullPath, AssetName, MaterialType, OutMessage))
		{
			OutMessage = TEXT("已创建插件自带 PBR 母材质");
			return PluginMaterial;
		}

		const FString PluginMessage = OutMessage;
		const FString ProjectPath = FString(PBRStudioProjectTemplateRoot) + GetTemplateAssetName(MaterialType);
		if (UObject* ExistingProject = LoadAssetIfExistsQuietly(ProjectPath))
		{
			if (UMaterial* ExistingProjectMaterial = Cast<UMaterial>(ExistingProject))
			{
				ResetMaterialGraph(ExistingProjectMaterial);
				BuildTemplateGraph(ExistingProjectMaterial, MaterialType, OutMessage);
				SaveMaterial(ExistingProjectMaterial);
				OutMessage = TEXT("插件母材质不可写，已回退并重建项目母材质: ") + PluginMessage;
				return ExistingProjectMaterial;
			}
		}

		UMaterial* ProjectMaterial = CreateStandardTemplate(ProjectPath, AssetName, MaterialType, OutMessage);
		if (ProjectMaterial)
		{
			OutMessage = TEXT("插件母材质不可写，已回退创建项目母材质: ") + PluginMessage;
		}
		return ProjectMaterial;
	}
}

int32 FPBRMaterialTemplateManager::EnsureAllTemplateMaterials(TArray<FString>& OutMessages)
{
	int32 CreatedOrLoaded = 0;
	for (EPBRMaterialType MaterialType : GetAllTemplateMaterialTypes())
	{
		FString Message;
		if (UMaterial* Material = EnsureTemplateMaterial(MaterialType, Message))
		{
			FString ExampleMessage;
			EnsureExampleMaterialInstance(MaterialType, ExampleMessage);
			CreatedOrLoaded++;
			OutMessages.Add(GetTemplateAssetName(MaterialType) + TEXT(": ") + Message + TEXT("; ") + ExampleMessage);
		}
		else
		{
			OutMessages.Add(GetTemplateAssetName(MaterialType) + TEXT(": 失败 - ") + Message);
		}
	}
	return CreatedOrLoaded;
}

int32 FPBRMaterialTemplateManager::EnsureSpecialTemplateMaterials(TArray<FString>& OutMessages)
{
	struct FSpecialMaterialRequest
	{
		FString AssetName;
	};

	const TArray<FSpecialMaterialRequest> Requests = {
		{ TEXT("SM_Decal_DBuffer_Color") },
		{ TEXT("SM_UI_Texture") },
		{ TEXT("SM_Niagara_Particle_Soft") },
		{ TEXT("SM_RuntimeVirtualTexture_Output") },
		{ TEXT("SM_DistanceField_Blend") },
		{ TEXT("SM_PostProcess_Tint") },
		{ TEXT("SM_LightFunction_SoftMask") },
		{ TEXT("SM_Hologram_Fresnel") },
		{ TEXT("SM_Niagara_Smoke_Soft") },
		{ TEXT("SM_Niagara_Lightning") },
		{ TEXT("SM_CarPaint_ClearCoat") },
		{ TEXT("SM_Abyss_Mirror") },
		{ TEXT("SM_TwoSided_Leaf") },
		{ TEXT("SM_WindowScreen_Mesh") },
		{ TEXT("SM_Video_Screen") },
		{ TEXT("SM_Lampshade_Translucent") }
	};

	int32 CreatedOrLoaded = 0;
	for (const FSpecialMaterialRequest& Request : Requests)
	{
		FString Message;
		if (UMaterial* Material = EnsureSpecialMaterialAsset(Request.AssetName, Message))
		{
			FString ExampleMessage;
			EnsureSpecialExampleMaterialInstance(Material, Request.AssetName, ExampleMessage);
			CreatedOrLoaded++;
			OutMessages.Add(Request.AssetName + TEXT(": ") + Message + TEXT("; ") + ExampleMessage);
		}
		else
		{
			OutMessages.Add(Request.AssetName + TEXT(": 失败 - ") + Message);
		}
	}
	return CreatedOrLoaded;
}

const TArray<FString>& FPBRMaterialTemplateManager::GetSpecialMaterialAssetNames()
{
	static const TArray<FString> AssetNames = {
		TEXT("SM_Decal_DBuffer_Color"),
		TEXT("SM_UI_Texture"),
		TEXT("SM_Niagara_Particle_Soft"),
		TEXT("SM_RuntimeVirtualTexture_Output"),
		TEXT("SM_DistanceField_Blend"),
		TEXT("SM_PostProcess_Tint"),
		TEXT("SM_LightFunction_SoftMask"),
		TEXT("SM_Hologram_Fresnel"),
		TEXT("SM_Niagara_Smoke_Soft"),
		TEXT("SM_Niagara_Lightning"),
		TEXT("SM_CarPaint_ClearCoat"),
		TEXT("SM_Abyss_Mirror"),
		TEXT("SM_TwoSided_Leaf"),
		TEXT("SM_WindowScreen_Mesh"),
		TEXT("SM_Video_Screen"),
		TEXT("SM_Lampshade_Translucent")
	};
	return AssetNames;
}

UMaterialInstanceConstant* FPBRMaterialTemplateManager::EnsureExampleMaterialInstance(EPBRMaterialType MaterialType, FString& OutMessage)
{
	FString ParentMessage;
	UMaterial* ParentMaterial = EnsureTemplateMaterial(MaterialType, ParentMessage);
	if (!ParentMaterial)
	{
		OutMessage = TEXT("示例实例失败: 母材质不可用");
		return nullptr;
	}

	const FString PackagePath = FPBRMaterialTemplateManager::GetExampleMaterialInstancePackagePath(MaterialType);
	const FString AssetName = FPaths::GetBaseFilename(PackagePath);
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(LoadAssetIfExistsQuietly(PackagePath));
	if (!Instance)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			OutMessage = TEXT("示例实例失败: 创建包失败");
			return nullptr;
		}
		Instance = NewObject<UMaterialInstanceConstant>(Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!Instance)
		{
			OutMessage = TEXT("示例实例失败: 创建对象失败");
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Instance);
		Package->SetDirtyFlag(true);
	}

	Instance->SetParentEditorOnly(ParentMaterial);
	Instance->ClearParameterValuesEditorOnly();
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseBaseColorTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseNormalTexture), UsesNormal(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseRoughnessTexture), UsesRoughness(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseMetallicTexture), UsesMetallic(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseAOTexture), UsesAO(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseOpacityTexture), UsesOpacity(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), UsesEmissive(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseSpecularTexture), UsesSpecular(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseHeightTexture), UsesHeight(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseClearCoatTexture), UsesClearCoat(MaterialType));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseClearCoatRoughnessTexture), UsesClearCoat(MaterialType));
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::BaseColorTexture));
	if (UsesNormal(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::NormalTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::NormalTexture));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 1.0f);
	}
	if (UsesRoughness(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::RoughnessTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::RoughnessTexture));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, GetTemplateDefaultRoughness(MaterialType));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, GetTemplateDefaultRoughness(MaterialType));
	}
	if (UsesMetallic(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::MetallicTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::MetallicTexture));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, GetTemplateDefaultMetallic(MaterialType));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, GetTemplateDefaultMetallic(MaterialType));
	}
	if (UsesAO(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::AOTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::AOTexture));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOValue, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOMultiplier, 1.0f);
	}
	if (UsesSpecular(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::SpecularTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::SpecularTexture));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.5f);
	}
	if (UsesOpacity(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::OpacityTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::OpacityTexture));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, GetTemplateDefaultOpacity(MaterialType));
	}
	if (UsesEmissive(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::EmissiveTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::EmissiveTexture));
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, MaterialType == EPBRMaterialType::Emissive ? FLinearColor::White : FLinearColor::Black);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, MaterialType == EPBRMaterialType::Emissive ? 1.0f : 0.0f);
	}
	if (UsesHeight(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::HeightTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::HeightTexture));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::HeightStrength, 0.5f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::PixelDepthOffsetStrength, 0.0f);
	}
	if (UsesClearCoat(MaterialType))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::ClearCoatTexture));
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatRoughnessTexture, CreateDemoTextureAsset(MaterialType, FPBRMaterialParameters::ClearCoatRoughnessTexture));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoat, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatRoughness, 0.15f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Anisotropy, 0.0f);
	}
	if (UsesFabricFuzz(MaterialType))
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzColor, FLinearColor(0.6f, 0.58f, 0.52f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzStrength, 0.12f);
	}
	if (UsesRefraction(MaterialType))
	{
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, GetTemplateDefaultRefraction(MaterialType));
	}
	if (UsesWaterColor(MaterialType))
	{
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseWaterRippleTexture), false);
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::WaterColor, FLinearColor(0.12f, 0.42f, 0.72f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedU, PBRARMDefaultWaterFlowSpeedU);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedV, PBRARMDefaultWaterFlowSpeedV);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleScale, PBRARMDefaultWaterRippleScale);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleStrength, PBRARMDefaultWaterRippleStrength);
	}
	if (MaterialType == EPBRMaterialType::Glass || MaterialType == EPBRMaterialType::Transparent)
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor(0.82f, 0.95f, 1.0f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, MaterialType == EPBRMaterialType::Glass ? 0.22f : 0.55f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, MaterialType == EPBRMaterialType::Glass ? 1.52f : 1.05f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.8f);
	}
	if (MaterialType == EPBRMaterialType::Emissive)
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor(1.0f, 0.78f, 0.48f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 2.5f);
	}
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor::White);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVRotationDegrees, 0.0f);
	Instance->PostEditChange();
	UEditorLoadingAndSavingUtils::SavePackages({ Instance->GetPackage() }, true);
	if (MaterialType == EPBRMaterialType::Water)
	{
		FString ClearWaterMessage;
		FString DeepWaterMessage;
		EnsureWaterVariantExampleInstance(ParentMaterial, TEXT("清水"), FLinearColor(0.04f, 0.35f, 0.55f), 0.02f, 0.75f, ClearWaterMessage);
		EnsureWaterVariantExampleInstance(ParentMaterial, TEXT("深水"), FLinearColor(0.01f, 0.12f, 0.22f), 0.035f, 0.45f, DeepWaterMessage);
	}
	else if (MaterialType == EPBRMaterialType::Standard)
	{
		FString PaintMessage;
		EnsurePaintExampleMaterialInstance(ParentMaterial, EPBRPaintExampleType::LatexPaint, PaintMessage);
		EnsurePaintExampleMaterialInstance(ParentMaterial, EPBRPaintExampleType::Microcement, PaintMessage);
	}
	else if (MaterialType == EPBRMaterialType::Glass)
	{
		FString VariantMessage;
		EnsureTemplateVariantExampleInstance(ParentMaterial, MaterialType, TEXT("玻璃_清玻璃"), FLinearColor(0.92f, 0.98f, 1.0f), 0.18f, 0.01f, 0.95f, 1.52f, 0.0f, VariantMessage);
		EnsureTemplateVariantExampleInstance(ParentMaterial, MaterialType, TEXT("玻璃_磨砂玻璃"), FLinearColor(0.82f, 0.92f, 0.96f), 0.42f, 0.32f, 0.65f, 1.48f, 0.0f, VariantMessage);
		EnsureTemplateVariantExampleInstance(ParentMaterial, MaterialType, TEXT("玻璃_蓝色玻璃"), FLinearColor(0.42f, 0.72f, 1.0f), 0.28f, 0.06f, 0.9f, 1.52f, 0.0f, VariantMessage);
		EnsureTemplateVariantExampleInstance(ParentMaterial, MaterialType, TEXT("玻璃_水滴玻璃"), FLinearColor(0.86f, 0.96f, 1.0f), 0.36f, 0.08f, 0.95f, 1.52f, 0.0f, VariantMessage);
	}
	else if (MaterialType == EPBRMaterialType::Transparent)
	{
		FString VariantMessage;
		EnsureTemplateVariantExampleInstance(ParentMaterial, MaterialType, TEXT("半透明_玉石"), FLinearColor(0.52f, 0.92f, 0.72f), 0.68f, 0.38f, 0.45f, 1.08f, 0.28f, VariantMessage);
		EnsureTemplateVariantExampleInstance(ParentMaterial, MaterialType, TEXT("半透明_蜡烛"), FLinearColor(1.0f, 0.78f, 0.46f), 0.72f, 0.55f, 0.35f, 1.03f, 0.65f, VariantMessage);
		EnsureTemplateVariantExampleInstance(ParentMaterial, MaterialType, TEXT("半透明_软塑料膜"), FLinearColor(0.86f, 0.96f, 1.0f), 0.48f, 0.18f, 0.55f, 1.02f, 0.0f, VariantMessage);
	}
	OutMessage = TEXT("已创建/更新中文示例材质实例");
	return Instance;
}

UMaterial* FPBRMaterialTemplateManager::CreateStandardTemplate(const FString& PackagePath, const FString& AssetName, EPBRMaterialType MaterialType, FString& OutMessage)
{
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutMessage = TEXT("创建母材质包失败");
		return nullptr;
	}

	UMaterial* Material = NewObject<UMaterial>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Material)
	{
		OutMessage = TEXT("创建母材质失败");
		return nullptr;
	}

	BuildTemplateGraph(Material, MaterialType, OutMessage);

	FAssetRegistryModule::AssetCreated(Material);
	Package->SetDirtyFlag(true);
	SaveMaterial(Material);

	OutMessage = TEXT("已创建 PBR 母材质");
	return Material;
}

static void AddSpecialBaseGroups(UMaterial* Material, const TArray<TPair<FName, int32>>& Groups)
{
	if (UMaterialEditorOnlyData* EditorData = Material ? Material->GetEditorOnlyData() : nullptr)
	{
		for (const TPair<FName, int32>& Group : Groups)
		{
			EditorData->ParameterGroupData.Add(FParameterGroupData(Group.Key.ToString(), Group.Value));
		}
	}
}

static UMaterialInstanceConstant* EnsureWaterVariantExampleInstance(UMaterial* ParentMaterial, const FString& Suffix, const FLinearColor& WaterColor, float Roughness, float RippleStrength, FString& OutMessage)
{
	const FString PackagePath = TEXT("/Game/PBRStudio/Templates/Examples/MI_示例_水_") + Suffix;
	const FString AssetName = FPaths::GetBaseFilename(PackagePath);
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(LoadAssetIfExistsQuietly(PackagePath));
	if (!Instance)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			OutMessage = TEXT("水示例实例失败: 创建包失败");
			return nullptr;
		}
		Instance = NewObject<UMaterialInstanceConstant>(Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!Instance)
		{
			OutMessage = TEXT("水示例实例失败: 创建对象失败");
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Instance);
		Package->SetDirtyFlag(true);
	}

	Instance->SetParentEditorOnly(ParentMaterial);
	Instance->ClearParameterValuesEditorOnly();
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseBaseColorTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseNormalTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseRoughnessTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseSpecularTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseWaterRippleTexture), false);
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTexture, CreateDemoTextureAsset(EPBRMaterialType::Water, FPBRMaterialParameters::BaseColorTexture));
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::NormalTexture, CreateDemoTextureAsset(EPBRMaterialType::Water, FPBRMaterialParameters::NormalTexture));
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::RoughnessTexture, CreateDemoTextureAsset(EPBRMaterialType::Water, FPBRMaterialParameters::RoughnessTexture));
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::SpecularTexture, CreateDemoTextureAsset(EPBRMaterialType::Water, FPBRMaterialParameters::SpecularTexture));
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::WaterColor, WaterColor);
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor::White);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, Roughness);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, Roughness);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.9f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, RippleStrength);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedU, Suffix == TEXT("深水") ? 0.08f : PBRARMDefaultWaterFlowSpeedU);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedV, Suffix == TEXT("深水") ? 0.04f : PBRARMDefaultWaterFlowSpeedV);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleScale, Suffix == TEXT("深水") ? 12.0f : PBRARMDefaultWaterRippleScale);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleStrength, RippleStrength);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVRotationDegrees, 0.0f);
	Instance->PostEditChange();
	UEditorLoadingAndSavingUtils::SavePackages({ Instance->GetPackage() }, true);
	OutMessage = TEXT("已创建/更新水材质示例: ") + Suffix;
	return Instance;
}

static UMaterialInstanceConstant* EnsurePaintExampleMaterialInstance(
	UMaterial* ParentMaterial,
	EPBRPaintExampleType PaintType,
	FString& OutMessage)
{
	if (!ParentMaterial)
	{
		OutMessage = TEXT("涂料示例实例失败: 母材质不可用");
		return nullptr;
	}

	const FString DisplayName = GetPaintExampleChineseName(PaintType);
	const FString PackagePath = TEXT("/Game/PBRStudio/Templates/Examples/MI_示例_涂料_") + DisplayName;
	const FString AssetName = FPaths::GetBaseFilename(PackagePath);
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(LoadAssetIfExistsQuietly(PackagePath));
	if (!Instance)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			OutMessage = TEXT("涂料示例实例失败: 创建包失败");
			return nullptr;
		}
		Instance = NewObject<UMaterialInstanceConstant>(Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!Instance)
		{
			OutMessage = TEXT("涂料示例实例失败: 创建对象失败");
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Instance);
		Package->SetDirtyFlag(true);
	}

	const bool bMicrocement = PaintType == EPBRPaintExampleType::Microcement;
	Instance->SetParentEditorOnly(ParentMaterial);
	Instance->ClearParameterValuesEditorOnly();
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseBaseColorTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseNormalTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseRoughnessTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseMetallicTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseAOTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseOpacityTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseSpecularTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseHeightTexture), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseClearCoatTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseClearCoatRoughnessTexture), false);
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTexture, CreatePaintDemoTextureAsset(PaintType, FPBRMaterialParameters::BaseColorTexture));
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::NormalTexture, CreatePaintDemoTextureAsset(PaintType, FPBRMaterialParameters::NormalTexture));
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::RoughnessTexture, CreatePaintDemoTextureAsset(PaintType, FPBRMaterialParameters::RoughnessTexture));
	Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::HeightTexture, CreatePaintDemoTextureAsset(PaintType, FPBRMaterialParameters::HeightTexture));
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, bMicrocement ? FLinearColor(0.72f, 0.70f, 0.66f) : FLinearColor(0.96f, 0.95f, 0.90f));
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, bMicrocement ? 0.92f : 1.04f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, bMicrocement ? 0.72f : 0.28f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, bMicrocement ? 0.76f : 0.88f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, bMicrocement ? 0.82f : 0.95f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOValue, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOMultiplier, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, bMicrocement ? 0.36f : 0.24f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::HeightStrength, bMicrocement ? 0.018f : 0.006f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::PixelDepthOffsetStrength, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoat, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatRoughness, 0.85f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Anisotropy, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUTiling, bMicrocement ? 2.0f : 1.5f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVTiling, bMicrocement ? 2.0f : 1.5f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVRotationDegrees, 0.0f);
	Instance->PostEditChange();
	UEditorLoadingAndSavingUtils::SavePackages({ Instance->GetPackage() }, true);
	OutMessage = TEXT("已创建/更新涂料材质示例: ") + DisplayName;
	return Instance;
}

static UMaterialInstanceConstant* EnsureTemplateVariantExampleInstance(
	UMaterial* ParentMaterial,
	EPBRMaterialType MaterialType,
	const FString& Suffix,
	const FLinearColor& BaseColorTint,
	float Opacity,
	float Roughness,
	float Specular,
	float Refraction,
	float EmissiveIntensity,
	FString& OutMessage)
{
	if (!ParentMaterial)
	{
		OutMessage = TEXT("示例实例失败: 母材质不可用");
		return nullptr;
	}

	const FString PackagePath = TEXT("/Game/PBRStudio/Templates/Examples/MI_示例_") + Suffix;
	const FString AssetName = FPaths::GetBaseFilename(PackagePath);
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(LoadAssetIfExistsQuietly(PackagePath));
	if (!Instance)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			OutMessage = TEXT("示例实例失败: 创建包失败");
			return nullptr;
		}
		Instance = NewObject<UMaterialInstanceConstant>(Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!Instance)
		{
			OutMessage = TEXT("示例实例失败: 创建对象失败");
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Instance);
		Package->SetDirtyFlag(true);
	}

	Instance->SetParentEditorOnly(ParentMaterial);
	Instance->ClearParameterValuesEditorOnly();
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseBaseColorTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseNormalTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseRoughnessTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseOpacityTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseSpecularTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseAOTexture), false);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), false);
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, BaseColorTint);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, Opacity);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, Roughness);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, Roughness);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, Specular);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, Refraction);
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, BaseColorTint);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, EmissiveIntensity);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, MaterialType == EPBRMaterialType::Glass ? 0.35f : 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVRotationDegrees, 0.0f);
	Instance->PostEditChange();
	UEditorLoadingAndSavingUtils::SavePackages({ Instance->GetPackage() }, true);
	OutMessage = TEXT("已创建/更新材质示例: ") + Suffix;
	return Instance;
}

static bool BuildSpecialWaterGraph(UMaterial* Material, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_SingleLayerWater);
	Material->BlendMode = BLEND_Opaque;
	Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
	Material->bScreenSpaceReflections = true;
	Material->RefractionMethod = RM_PixelNormalOffset;
	Material->TwoSided = true;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupSurface(TEXT("01 水面"));
	const FName GroupNormal(TEXT("02 动态水波"));
	const FName GroupOptics(TEXT("03 透明和折射"));
	const FName GroupUV(TEXT("04 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupSurface, 10 }, { GroupNormal, 20 }, { GroupOptics, 30 }, { GroupUV, 40 } });

	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);

	UMaterialExpressionVectorParameter* WaterColor = AddVectorParameter(Material, FPBRMaterialParameters::WaterColor, GroupSurface, 10, FLinearColor(0.02f, 0.22f, 0.34f), -900, -320);
	UMaterialExpressionScalarParameter* Roughness = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupSurface, 20, 0.015f, -900, -180);
	UMaterialExpressionScalarParameter* Specular = AddScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupSurface, 30, 0.9f, -900, -40);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupOptics, 10, 0.45f, -900, 120);
	UMaterialExpressionTextureSampleParameter2D* NormalTex = AddTextureParameterForType(Material, EPBRMaterialType::Water, FPBRMaterialParameters::NormalTexture, GroupNormal, 10, -900, 440, EMaterialSamplerType::SAMPLERTYPE_Normal);
	UMaterialExpressionScalarParameter* NormalStrength = AddScalarParameter(Material, FPBRMaterialParameters::NormalStrength, GroupNormal, 20, 0.65f, -620, 480);

	EditorData->BaseColor.Connect(0, WaterColor);
	EditorData->Roughness.Connect(0, Roughness);
	EditorData->Specular.Connect(0, Specular);
	EditorData->Opacity.Connect(0, Opacity);
	BuildDynamicWaterNormal(Material, NormalTex, NormalStrength, SharedUV, GroupNormal, -360, 620);
	AddSingleLayerWaterOutput(Material, GroupOptics, 360, 240);
	ConnectTextureSamplesToUV(Material, SharedUV);
	OutMessage = TEXT("Built independent dynamic water material");
	return true;
}

static bool BuildSpecialGlassGraph(UMaterial* Material, bool bDroplets, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;
	Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
	Material->bScreenSpaceReflections = true;
	Material->RefractionMethod = RM_IndexOfRefraction;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupGlass(TEXT("01 玻璃"));
	const FName GroupNormal(TEXT("02 表面细节"));
	const FName GroupUV(TEXT("03 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupGlass, 10 }, { GroupNormal, 20 }, { GroupUV, 30 } });

	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);

	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupGlass, 10, FLinearColor(0.78f, 0.93f, 1.0f), -900, -320);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupGlass, 20, bDroplets ? 0.38f : 0.22f, -900, -180);
	UMaterialExpressionScalarParameter* Roughness = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupGlass, 30, bDroplets ? 0.08f : 0.015f, -900, -40);
	UMaterialExpressionScalarParameter* Specular = AddScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupGlass, 40, 0.95f, -900, 100);
	UMaterialExpressionScalarParameter* Refraction = AddScalarParameter(Material, FPBRMaterialParameters::RefractionAmount, GroupGlass, 50, 1.52f, -900, 240);

	EditorData->BaseColor.Connect(0, Tint);
	EditorData->Opacity.Connect(0, Opacity);
	EditorData->Roughness.Connect(0, Roughness);
	EditorData->Specular.Connect(0, Specular);
	EditorData->Refraction.Connect(0, Refraction);

	if (bDroplets)
	{
		UMaterialExpressionTextureSampleParameter2D* NormalTex = AddTextureParameterForType(Material, EPBRMaterialType::Glass, FPBRMaterialParameters::NormalTexture, GroupNormal, 10, -900, 420, EMaterialSamplerType::SAMPLERTYPE_Normal);
		UMaterialExpressionScalarParameter* NormalStrength = AddScalarParameter(Material, FPBRMaterialParameters::NormalStrength, GroupNormal, 20, 0.85f, -620, 460);
		if (!WireNormalStrength(Material, NormalTex, NormalStrength, -360, 420))
		{
			EditorData->Normal.Connect(0, NormalTex);
		}
		ConnectTextureSamplesToUV(Material, SharedUV);
	}

	AddSpecialGraphComment(Material, TEXT("玻璃表面"), -980, -420, 980, 760);
	OutMessage = bDroplets ? TEXT("Built independent glass droplets material") : TEXT("Built independent clear glass material");
	return true;
}

static bool BuildSpecialTransparentGraph(UMaterial* Material, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;
	Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
	Material->bScreenSpaceReflections = true;
	Material->RefractionMethod = RM_IndexOfRefraction;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupTransparent(TEXT("01 半透明"));
	const FName GroupUV(TEXT("02 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupTransparent, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	EditorData->BaseColor.Connect(0, AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupTransparent, 10, FLinearColor(0.75f, 0.88f, 0.96f), -700, -260));
	EditorData->Opacity.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupTransparent, 20, 0.55f, -700, -120));
	EditorData->Roughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupTransparent, 30, 0.25f, -700, 20));
	EditorData->Specular.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupTransparent, 40, 0.65f, -700, 160));
	EditorData->Refraction.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::RefractionAmount, GroupTransparent, 50, 1.05f, -700, 300));
	OutMessage = TEXT("Built independent soft translucent material");
	return true;
}

static bool BuildSpecialTranslucentOnyxGraph(UMaterial* Material, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupOnyx(TEXT("01 透光石"));
	const FName GroupUV(TEXT("02 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupOnyx, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpressionTextureSampleParameter2D* BaseTex = AddTextureParameterForType(Material, EPBRMaterialType::Emissive, FPBRMaterialParameters::BaseColorTexture, GroupOnyx, 10, -900, -320);
	UMaterialExpressionVectorParameter* WarmTint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupOnyx, 20, FLinearColor(1.0f, 0.72f, 0.42f), -900, -160);
	UMaterialExpressionMultiply* TintedBase = AddMultiply(Material, -620, -260);
	TintedBase->A.Connect(0, BaseTex);
	TintedBase->B.Connect(0, WarmTint);
	EditorData->BaseColor.Connect(0, TintedBase);

	UMaterialExpressionVectorParameter* EmissiveColor = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupOnyx, 30, FLinearColor(1.0f, 0.68f, 0.36f), -900, 20);
	UMaterialExpressionScalarParameter* EmissiveIntensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupOnyx, 40, 2.2f, -900, 160);
	UMaterialExpressionMultiply* Emissive = AddMultiply(Material, -620, 80);
	Emissive->A.Connect(0, EmissiveColor);
	Emissive->B.Connect(0, EmissiveIntensity);
	EditorData->EmissiveColor.Connect(0, Emissive);
	EditorData->Opacity.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupOnyx, 50, 0.82f, -900, 300));
	EditorData->Roughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupOnyx, 60, 0.42f, -900, 440));
	ConnectTextureSamplesToUV(Material, SharedUV);
	OutMessage = TEXT("Built independent translucent onyx material");
	return true;
}

static bool BuildSpecialClearCoatGraph(UMaterial* Material, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_ClearCoat);
	Material->BlendMode = BLEND_Opaque;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupCoat(TEXT("01 清漆表面"));
	const FName GroupUV(TEXT("02 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupCoat, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpressionTextureSampleParameter2D* CarPaintTex = AddTextureParameterForType(Material, EPBRMaterialType::Metal, FPBRMaterialParameters::BaseColorTexture, GroupCoat, 10, -700, -440);
	CarPaintTex->Texture = CreateSpecialDemoTextureAsset(TEXT("CarPaint_RedFlakes"), true);
	UMaterialExpressionVectorParameter* PaintTint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupCoat, 20, FLinearColor(0.75f, 0.02f, 0.015f), -700, -280);
	UMaterialExpressionMultiply* PaintColor = AddMultiply(Material, -420, -380);
	PaintColor->A.Connect(0, CarPaintTex);
	PaintColor->B.Connect(0, PaintTint);
	EditorData->BaseColor.Connect(0, PaintColor);
	EditorData->Roughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupCoat, 20, 0.18f, -700, -160));
	EditorData->Specular.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupCoat, 30, 0.65f, -700, -20));
	EditorData->ClearCoat.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::ClearCoat, GroupCoat, 40, 1.0f, -700, 120));
	EditorData->ClearCoatRoughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::ClearCoatRoughness, GroupCoat, 50, 0.04f, -700, 260));
	AddScalarParameter(Material, FPBRMaterialParameters::FlakeScale, GroupCoat, 60, 24.0f, -700, 400);
	AddScalarParameter(Material, FPBRMaterialParameters::FlakeIntensity, GroupCoat, 70, 0.35f, -700, 540);
	ConnectTextureSamplesToUV(Material, SharedUV);
	AddSpecialGraphComment(Material, TEXT("车漆 / 清漆"));
	OutMessage = TEXT("Built independent glossy clear coat material");
	return true;
}

static bool BuildSpecialBrushedMetalGraph(UMaterial* Material, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Opaque;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupMetal(TEXT("01 拉丝金属"));
	const FName GroupUV(TEXT("02 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupMetal, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	EditorData->BaseColor.Connect(0, AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupMetal, 10, FLinearColor(0.75f, 0.72f, 0.66f), -700, -300));
	EditorData->Metallic.Connect(0, AddConstant(Material, 1.0f, -700, -160));
	EditorData->Roughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupMetal, 20, 0.28f, -700, -20));
	EditorData->Specular.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupMetal, 30, 0.75f, -700, 120));
	EditorData->Anisotropy.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::Anisotropy, GroupMetal, 40, 0.85f, -700, 260));
	OutMessage = TEXT("Built independent brushed metal material");
	return true;
}

static bool BuildSpecialDecalGraphV2(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_DeferredDecal;
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;
	Material->DecalBlendMode = DBM_DBuffer_Color;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupDecal(TEXT("01 Decal"));
	const FName GroupUV(TEXT("02 UV"));
	AddSpecialBaseGroups(Material, { { GroupDecal, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpressionTextureSampleParameter2D* DecalTex = AddTextureParameterForType(Material, EPBRMaterialType::Standard, FPBRMaterialParameters::BaseColorTexture, GroupDecal, 10, -900, -260);
	DecalTex->Texture = CreateSpecialDemoTextureAsset(TEXT("Decal_GrimeDamage"), true);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupDecal, 20, FLinearColor::White, -900, -100);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupDecal, 30, 0.75f, -900, 60);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -220);
	Tinted->A.Connect(0, DecalTex);
	Tinted->B.Connect(0, Tint);
	EditorData->BaseColor.Connect(0, Tinted);
	EditorData->Opacity.Connect(0, Opacity);
	ConnectTextureSamplesToUV(Material, SharedUV);
	AddSpecialGraphComment(Material, TEXT("贴花: 污渍 / 破损"));
	OutMessage = TEXT("Built decal material");
	return true;
}

static bool BuildSpecialUIGraphV2(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_UI;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->BlendMode = BLEND_Translucent;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupUI(TEXT("01 UI"));
	const FName GroupUV(TEXT("02 UV"));
	AddSpecialBaseGroups(Material, { { GroupUI, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpressionTextureSampleParameter2D* UITex = AddTextureParameterForType(Material, EPBRMaterialType::Emissive, FPBRMaterialParameters::BaseColorTexture, GroupUI, 10, -900, -260);
	UITex->Texture = CreateSpecialDemoTextureAsset(TEXT("Hologram_UILines"), true);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupUI, 20, FLinearColor::White, -900, -100);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupUI, 30, 1.0f, -900, 60);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -220);
	Tinted->A.Connect(0, UITex);
	Tinted->B.Connect(0, Tint);
	EditorData->EmissiveColor.Connect(0, Tinted);
	EditorData->Opacity.Connect(0, Opacity);
	ConnectTextureSamplesToUV(Material, SharedUV);
	AddSpecialGraphComment(Material, TEXT("UI 贴图"));
	OutMessage = TEXT("Built UI material");
	return true;
}

static bool BuildSpecialNiagaraParticleGraphV2(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->BlendMode = BLEND_Additive;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupParticle(TEXT("01 Niagara"));
	const FName GroupUV(TEXT("02 UV 调整"));
	const FName GroupDynamic(TEXT("03 动态控制"));
	AddSpecialBaseGroups(Material, { { GroupParticle, 10 }, { GroupUV, 20 }, { GroupDynamic, 30 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpression* DynamicUV = BuildDynamicPannerUV(Material, SharedUV, GroupDynamic, 0.02f, 0.18f, 1.0f, -360, 300);
	UMaterialExpressionTextureSampleParameter2D* ParticleMask = AddTextureParameterForType(Material, EPBRMaterialType::Emissive, FPBRMaterialParameters::EmissiveTexture, GroupParticle, 5, -900, -360);
	ParticleMask->Texture = CreateSpecialDemoTextureAsset(TEXT("Fire_Sprite"), true);
	ParticleMask->Coordinates.Connect(0, DynamicUV);
	UMaterialExpressionParticleColor* ParticleColor = NewObject<UMaterialExpressionParticleColor>(Material);
	ParticleColor->MaterialExpressionEditorX = -900;
	ParticleColor->MaterialExpressionEditorY = -160;
	Material->GetExpressionCollection().AddExpression(ParticleColor);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupParticle, 10, FLinearColor(1.0f, 0.45f, 0.12f), -900, 40);
	UMaterialExpressionScalarParameter* Intensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupParticle, 20, 3.0f, -900, 200);
	UMaterialExpressionScalarParameter* DynamicIntensity = AddScalarParameter(Material, FPBRMaterialParameters::DynamicIntensity, GroupDynamic, 40, 1.0f, -360, 660);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -100);
	Tinted->A.Connect(0, ParticleColor);
	Tinted->B.Connect(0, Tint);
	UMaterialExpressionMultiply* Emissive = AddMultiply(Material, -360, -100);
	Emissive->A.Connect(0, Tinted);
	Emissive->B.Connect(0, Intensity);
	UMaterialExpressionMultiply* TexturedEmissive = AddMultiply(Material, -120, -120);
	TexturedEmissive->A.Connect(0, Emissive);
	TexturedEmissive->B.Connect(0, ParticleMask);
	UMaterialExpressionMultiply* FinalEmissive = AddMultiply(Material, 120, -120);
	FinalEmissive->A.Connect(0, TexturedEmissive);
	FinalEmissive->B.Connect(0, DynamicIntensity);
	EditorData->EmissiveColor.Connect(0, FinalEmissive);
	EditorData->Opacity.Connect(0, ParticleMask);
	AddSpecialGraphComment(Material, TEXT("Niagara: 火 / 烟 / 闪电 Sprite"));
	OutMessage = TEXT("Built Niagara particle material");
	return true;
}

static bool BuildSpecialNiagaraSpriteGraph(UMaterial* Material, const FString& TextureKind, const FLinearColor& DefaultColor, float DefaultIntensity, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->BlendMode = BLEND_Additive;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupParticle(TEXT("01 粒子"));
	const FName GroupUV(TEXT("02 UV 调整"));
	const FName GroupDynamic(TEXT("03 动态控制"));
	AddSpecialBaseGroups(Material, { { GroupParticle, 10 }, { GroupUV, 20 }, { GroupDynamic, 30 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpression* DynamicUV = BuildDynamicPannerUV(Material, SharedUV, GroupDynamic, TextureKind.Contains(TEXT("Smoke")) ? 0.03f : 0.0f, TextureKind.Contains(TEXT("Lightning")) ? 0.35f : 0.08f, 1.0f, -360, 260);
	UMaterialExpressionTextureSampleParameter2D* SpriteTexture = AddTextureParameterForType(Material, EPBRMaterialType::Emissive, FPBRMaterialParameters::EmissiveTexture, GroupParticle, 10, -900, -260);
	SpriteTexture->Texture = CreateSpecialDemoTextureAsset(TextureKind, true);
	SpriteTexture->Coordinates.Connect(0, DynamicUV);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupParticle, 20, DefaultColor, -900, -80);
	UMaterialExpressionScalarParameter* Intensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupParticle, 30, DefaultIntensity, -900, 80);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupParticle, 40, 0.75f, -900, 240);
	UMaterialExpressionScalarParameter* DynamicIntensity = AddScalarParameter(Material, FPBRMaterialParameters::DynamicIntensity, GroupDynamic, 40, 1.0f, -360, 620);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -220);
	Tinted->A.Connect(0, SpriteTexture);
	Tinted->B.Connect(0, Tint);
	UMaterialExpressionMultiply* Emissive = AddMultiply(Material, -360, -220);
	Emissive->A.Connect(0, Tinted);
	Emissive->B.Connect(0, Intensity);
	UMaterialExpressionMultiply* FinalEmissive = AddMultiply(Material, -120, -220);
	FinalEmissive->A.Connect(0, Emissive);
	FinalEmissive->B.Connect(0, DynamicIntensity);
	UMaterialExpressionMultiply* Alpha = AddMultiply(Material, -360, 40);
	Alpha->A.Connect(0, SpriteTexture);
	Alpha->B.Connect(0, Opacity);
	EditorData->EmissiveColor.Connect(0, FinalEmissive);
	EditorData->Opacity.Connect(0, Alpha);
	AddSpecialGraphComment(Material, TEXT("粒子 Sprite"));
	OutMessage = TEXT("Built Niagara sprite material");
	return true;
}

static bool BuildSpecialRVTOutputGraphV2(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Opaque;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupRVT(TEXT("01 RVT Output"));
	const FName GroupUV(TEXT("02 UV"));
	AddSpecialBaseGroups(Material, { { GroupRVT, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpressionTextureSampleParameter2D* BaseTex = AddTextureParameterForType(Material, EPBRMaterialType::Stone, FPBRMaterialParameters::BaseColorTexture, GroupRVT, 10, -900, -320);
	UMaterialExpressionTextureSampleParameter2D* NormalTex = AddTextureParameterForType(Material, EPBRMaterialType::Stone, FPBRMaterialParameters::NormalTexture, GroupRVT, 20, -900, -120, EMaterialSamplerType::SAMPLERTYPE_Normal);
	UMaterialExpressionScalarParameter* Roughness = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupRVT, 30, 0.65f, -900, 80);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupRVT, 40, 1.0f, -900, 240);
	UMaterialExpressionScalarParameter* WorldHeight = AddScalarParameter(Material, FPBRMaterialParameters::HeightStrength, GroupRVT, 50, 0.5f, -900, 400);
	EditorData->BaseColor.Connect(0, BaseTex);
	EditorData->Normal.Connect(0, NormalTex);
	EditorData->Roughness.Connect(0, Roughness);
	UMaterialExpressionRuntimeVirtualTextureOutput* RVTOutput = NewObject<UMaterialExpressionRuntimeVirtualTextureOutput>(Material);
	RVTOutput->MaterialExpressionEditorX = -360;
	RVTOutput->MaterialExpressionEditorY = -180;
	RVTOutput->BaseColor.Connect(0, BaseTex);
	RVTOutput->Normal.Connect(0, NormalTex);
	RVTOutput->Roughness.Connect(0, Roughness);
	RVTOutput->Opacity.Connect(0, Opacity);
	RVTOutput->WorldHeight.Connect(0, WorldHeight);
	Material->GetExpressionCollection().AddExpression(RVTOutput);
	ConnectTextureSamplesToUV(Material, SharedUV);
	AddSpecialGraphComment(Material, TEXT("Runtime Virtual Texture 输出"));
	OutMessage = TEXT("Built Runtime Virtual Texture output material");
	return true;
}

static bool BuildSpecialDistanceFieldGraphV2(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupDF(TEXT("01 Distance Field"));
	AddSpecialBaseGroups(Material, { { GroupDF, 10 } });
	UMaterialExpressionDistanceToNearestSurface* Distance = NewObject<UMaterialExpressionDistanceToNearestSurface>(Material);
	Distance->MaterialExpressionEditorX = -900;
	Distance->MaterialExpressionEditorY = -260;
	Material->GetExpressionCollection().AddExpression(Distance);
	UMaterialExpressionScalarParameter* FadeDistance = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupDF, 10, 100.0f, -900, -80);
	UMaterialExpressionDivide* NormalizedDistance = AddDivide(Material, -620, -220, 100.0f);
	NormalizedDistance->A.Connect(0, Distance);
	NormalizedDistance->B.Connect(0, FadeDistance);
	UMaterialExpressionCustom* InvertFade = NewObject<UMaterialExpressionCustom>(Material);
	InvertFade->MaterialExpressionEditorX = -360;
	InvertFade->MaterialExpressionEditorY = -220;
	InvertFade->Code = TEXT("return saturate(1.0 - Distance01);");
	InvertFade->Description = TEXT("Distance Field Fade");
	InvertFade->OutputType = CMOT_Float1;
	FCustomInput DistanceInput;
	DistanceInput.InputName = TEXT("Distance01");
	DistanceInput.Input.Connect(0, NormalizedDistance);
	InvertFade->Inputs.Add(DistanceInput);
	Material->GetExpressionCollection().AddExpression(InvertFade);
	EditorData->BaseColor.Connect(0, AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupDF, 20, FLinearColor(0.1f, 0.45f, 0.9f), -900, 140));
	EditorData->Opacity.Connect(0, InvertFade);
	EditorData->Roughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupDF, 30, 0.4f, -900, 300));
	AddSpecialGraphComment(Material, TEXT("Distance Field 混合"));
	OutMessage = TEXT("Built Distance Field blend material");
	return true;
}

static bool BuildSpecialPostProcessTintGraph(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_PostProcess;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->BlendMode = BLEND_Opaque;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupPost(TEXT("01 后处理"));
	AddSpecialBaseGroups(Material, { { GroupPost, 10 } });
	UMaterialExpressionSceneTexture* SceneColor = NewObject<UMaterialExpressionSceneTexture>(Material);
	SceneColor->SceneTextureId = PPI_PostProcessInput0;
	SceneColor->MaterialExpressionEditorX = -900;
	SceneColor->MaterialExpressionEditorY = -260;
	Material->GetExpressionCollection().AddExpression(SceneColor);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupPost, 10, FLinearColor(1.0f, 0.96f, 0.9f), -900, -80);
	UMaterialExpressionScalarParameter* Strength = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupPost, 20, 1.0f, -900, 80);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -220);
	Tinted->A.Connect(0, SceneColor);
	Tinted->B.Connect(0, Tint);
	UMaterialExpressionMultiply* Result = AddMultiply(Material, -360, -220);
	Result->A.Connect(0, Tinted);
	Result->B.Connect(0, Strength);
	EditorData->EmissiveColor.Connect(0, Result);
	AddSpecialGraphComment(Material, TEXT("后处理调色"));
	OutMessage = TEXT("Built post process tint material");
	return true;
}

static bool BuildSpecialLightFunctionGraph(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_LightFunction;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->BlendMode = BLEND_Opaque;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupLight(TEXT("01 灯光函数"));
	AddSpecialBaseGroups(Material, { { GroupLight, 10 } });
	UMaterialExpressionLightVector* LightVector = NewObject<UMaterialExpressionLightVector>(Material);
	LightVector->MaterialExpressionEditorX = -900;
	LightVector->MaterialExpressionEditorY = -220;
	Material->GetExpressionCollection().AddExpression(LightVector);
	UMaterialExpressionScalarParameter* Strength = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupLight, 10, 0.65f, -900, -40);
	UMaterialExpressionFresnel* SoftEdge = AddFresnel(Material, -620, -220);
	UMaterialExpressionMultiply* Mask = AddMultiply(Material, -360, -220);
	Mask->A.Connect(0, SoftEdge);
	Mask->B.Connect(0, Strength);
	EditorData->EmissiveColor.Connect(0, Mask);
	AddSpecialGraphComment(Material, TEXT("灯光遮罩"));
	OutMessage = TEXT("Built light function soft mask material");
	return true;
}

static bool BuildSpecialHologramGraph(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->BlendMode = BLEND_Additive;
	Material->TwoSided = true;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupHolo(TEXT("01 全息"));
	const FName GroupDynamic(TEXT("02 动态控制"));
	AddSpecialBaseGroups(Material, { { GroupHolo, 10 }, { GroupDynamic, 20 } });
	UMaterialExpressionFresnel* Fresnel = AddFresnel(Material, -900, -260);
	UMaterialExpressionVectorParameter* Color = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupHolo, 10, FLinearColor(0.1f, 0.75f, 1.0f), -900, -80);
	UMaterialExpressionScalarParameter* Intensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupHolo, 20, 4.0f, -900, 80);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupHolo, 30, 0.45f, -900, 240);
	UMaterialExpressionScalarParameter* EdgeGlow = AddScalarParameter(Material, FPBRMaterialParameters::EdgeGlowStrength, GroupDynamic, 10, 1.0f, -900, 400);
	UMaterialExpressionScalarParameter* DynamicIntensity = AddScalarParameter(Material, FPBRMaterialParameters::DynamicIntensity, GroupDynamic, 20, 1.0f, -900, 560);
	UMaterialExpressionMultiply* EdgeColor = AddMultiply(Material, -620, -220);
	EdgeColor->A.Connect(0, Fresnel);
	EdgeColor->B.Connect(0, Color);
	UMaterialExpressionMultiply* EdgeBoost = AddMultiply(Material, -360, -220);
	EdgeBoost->A.Connect(0, EdgeColor);
	EdgeBoost->B.Connect(0, EdgeGlow);
	UMaterialExpressionMultiply* Emissive = AddMultiply(Material, -120, -220);
	Emissive->A.Connect(0, EdgeBoost);
	Emissive->B.Connect(0, Intensity);
	UMaterialExpressionMultiply* FinalEmissive = AddMultiply(Material, 120, -220);
	FinalEmissive->A.Connect(0, Emissive);
	FinalEmissive->B.Connect(0, DynamicIntensity);
	UMaterialExpressionMultiply* Alpha = AddMultiply(Material, -360, 40);
	Alpha->A.Connect(0, Fresnel);
	Alpha->B.Connect(0, Opacity);
	EditorData->EmissiveColor.Connect(0, FinalEmissive);
	EditorData->Opacity.Connect(0, Alpha);
	AddSpecialGraphComment(Material, TEXT("全息边缘光"), -1020, -360, 1400, 1120);
	OutMessage = TEXT("Built hologram fresnel material");
	return true;
}

static bool BuildSpecialAbyssMirrorGraph(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->BlendMode = BLEND_Additive;
	Material->TwoSided = true;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupAbyss(TEXT("01 深渊镜"));
	const FName GroupUV(TEXT("02 UV 调整"));
	const FName GroupDynamic(TEXT("03 动态控制"));
	AddSpecialBaseGroups(Material, { { GroupAbyss, 10 }, { GroupUV, 20 }, { GroupDynamic, 30 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpression* DynamicUV = BuildDynamicPannerUV(Material, SharedUV, GroupDynamic, 0.015f, 0.035f, 1.0f, -360, 360);
	UMaterialExpressionTextureSampleParameter2D* AbyssTex = AddTextureParameterForType(Material, EPBRMaterialType::Emissive, FPBRMaterialParameters::EmissiveTexture, GroupAbyss, 10, -900, -320);
	AbyssTex->Texture = CreateSpecialDemoTextureAsset(TEXT("Abyss_MirrorSpiral"), true);
	AbyssTex->Coordinates.Connect(0, DynamicUV);
	UMaterialExpressionFresnel* Fresnel = AddFresnel(Material, -900, -80);
	UMaterialExpressionVectorParameter* EdgeColor = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupAbyss, 20, FLinearColor(0.05f, 0.22f, 0.85f), -900, 80);
	UMaterialExpressionScalarParameter* Intensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupAbyss, 30, 3.5f, -900, 240);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupAbyss, 40, 0.88f, -900, 400);
	UMaterialExpressionScalarParameter* EdgeGlowStrength = AddScalarParameter(Material, FPBRMaterialParameters::EdgeGlowStrength, GroupDynamic, 40, 1.25f, -360, 720);
	UMaterialExpressionMultiply* EdgeGlow = AddMultiply(Material, -620, -80);
	EdgeGlow->A.Connect(0, Fresnel);
	EdgeGlow->B.Connect(0, EdgeColor);
	UMaterialExpressionMultiply* EdgeBoost = AddMultiply(Material, -360, -40);
	EdgeBoost->A.Connect(0, EdgeGlow);
	EdgeBoost->B.Connect(0, EdgeGlowStrength);
	UMaterialExpressionAdd* AbyssAndEdge = AddAdd(Material, -360, -220);
	AbyssAndEdge->A.Connect(0, AbyssTex);
	AbyssAndEdge->B.Connect(0, EdgeBoost);
	UMaterialExpressionMultiply* Emissive = AddMultiply(Material, -120, -220);
	Emissive->A.Connect(0, AbyssAndEdge);
	Emissive->B.Connect(0, Intensity);
	UMaterialExpressionMultiply* Alpha = AddMultiply(Material, -120, 60);
	Alpha->A.Connect(0, AbyssTex);
	Alpha->B.Connect(0, Opacity);
	EditorData->EmissiveColor.Connect(0, Emissive);
	EditorData->Opacity.Connect(0, Alpha);
	AddSpecialGraphComment(Material, TEXT("深渊镜 / 黑镜旋涡"), -1020, -460, 1600, 1420);
	OutMessage = TEXT("Built abyss mirror material");
	return true;
}

static bool BuildSpecialLeafGraph(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_TwoSidedFoliage);
	Material->BlendMode = BLEND_Masked;
	Material->TwoSided = true;
	Material->OpacityMaskClipValue = 0.35f;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupLeaf(TEXT("01 双面树叶"));
	const FName GroupUV(TEXT("02 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupLeaf, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpressionTextureSampleParameter2D* LeafTex = AddTextureParameterForType(Material, EPBRMaterialType::Fabric, FPBRMaterialParameters::BaseColorTexture, GroupLeaf, 10, -900, -320);
	LeafTex->Texture = CreateSpecialDemoTextureAsset(TEXT("Leaf_BladeMask"), true);
	LeafTex->Coordinates.Connect(0, SharedUV);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupLeaf, 20, FLinearColor(0.42f, 0.8f, 0.24f), -900, -120);
	UMaterialExpressionScalarParameter* Roughness = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupLeaf, 30, 0.62f, -900, 80);
	UMaterialExpressionScalarParameter* Backlight = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupLeaf, 40, 0.55f, -900, 260);
	UMaterialExpressionScalarParameter* MaskThreshold = AddScalarParameter(Material, FPBRMaterialParameters::MaskThreshold, GroupLeaf, 50, 0.35f, -900, 440);
	UMaterialExpressionMultiply* BaseColor = AddMultiply(Material, -620, -260);
	BaseColor->A.Connect(0, LeafTex);
	BaseColor->B.Connect(0, Tint);
	UMaterialExpressionMultiply* Subsurface = AddMultiply(Material, -360, -80);
	Subsurface->A.Connect(0, Tint);
	Subsurface->B.Connect(0, Backlight);
	EditorData->BaseColor.Connect(0, BaseColor);
	EditorData->OpacityMask.Connect(4, LeafTex);
	EditorData->Roughness.Connect(0, Roughness);
	EditorData->SubsurfaceColor.Connect(0, Subsurface);
	AddSpecialGraphComment(Material, TEXT("双面树叶 / Two Sided Foliage"), -1020, -420, 1480, 1080);
	OutMessage = TEXT("Built two sided leaf material");
	return true;
}

static bool BuildSpecialWindowScreenGraph(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Masked;
	Material->TwoSided = true;
	Material->OpacityMaskClipValue = 0.28f;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupScreen(TEXT("01 窗纱"));
	const FName GroupUV(TEXT("02 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupScreen, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpressionTextureSampleParameter2D* MeshTex = AddTextureParameterForType(Material, EPBRMaterialType::Fabric, FPBRMaterialParameters::BaseColorTexture, GroupScreen, 10, -900, -320);
	MeshTex->Texture = CreateSpecialDemoTextureAsset(TEXT("WindowScreen_Mesh"), true);
	MeshTex->Coordinates.Connect(0, SharedUV);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupScreen, 20, FLinearColor(0.62f, 0.62f, 0.58f), -900, -120);
	UMaterialExpressionScalarParameter* Roughness = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupScreen, 30, 0.78f, -900, 80);
	AddScalarParameter(Material, FPBRMaterialParameters::MaskThreshold, GroupScreen, 40, 0.28f, -900, 240);
	UMaterialExpressionMultiply* BaseColor = AddMultiply(Material, -620, -260);
	BaseColor->A.Connect(0, MeshTex);
	BaseColor->B.Connect(0, Tint);
	EditorData->BaseColor.Connect(0, BaseColor);
	EditorData->OpacityMask.Connect(4, MeshTex);
	EditorData->Roughness.Connect(0, Roughness);
	AddSpecialGraphComment(Material, TEXT("窗纱 / Masked 双面网格"), -1020, -420, 1480, 920);
	OutMessage = TEXT("Built window screen mesh material");
	return true;
}

static bool BuildSpecialVideoScreenGraph(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->BlendMode = BLEND_Opaque;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupVideo(TEXT("01 视频屏幕"));
	const FName GroupUV(TEXT("02 UV 调整"));
	const FName GroupDynamic(TEXT("03 动态控制"));
	AddSpecialBaseGroups(Material, { { GroupVideo, 10 }, { GroupUV, 20 }, { GroupDynamic, 30 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpression* DynamicUV = BuildDynamicPannerUV(Material, SharedUV, GroupDynamic, 0.0f, 0.0f, 1.0f, -360, 260);
	UMaterialExpressionTextureSampleParameter2D* VideoTex = AddTextureParameterForType(Material, EPBRMaterialType::Emissive, FPBRMaterialParameters::EmissiveTexture, GroupVideo, 10, -900, -320);
	VideoTex->Texture = CreateSpecialDemoTextureAsset(TEXT("VideoBars_TestPattern"), true);
	VideoTex->Coordinates.Connect(0, DynamicUV);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupVideo, 20, FLinearColor::White, -900, -120);
	UMaterialExpressionScalarParameter* Intensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupVideo, 30, 2.0f, -900, 80);
	AddScalarParameter(Material, FPBRMaterialParameters::ScanlineStrength, GroupDynamic, 40, 0.25f, -360, 620);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -260);
	Tinted->A.Connect(0, VideoTex);
	Tinted->B.Connect(0, Tint);
	UMaterialExpressionMultiply* Emissive = AddMultiply(Material, -360, -260);
	Emissive->A.Connect(0, Tinted);
	Emissive->B.Connect(0, Intensity);
	EditorData->EmissiveColor.Connect(0, Emissive);
	AddSpecialGraphComment(Material, TEXT("视频屏幕 / 可替换为 Media Texture"), -1020, -420, 1620, 1200);
	OutMessage = TEXT("Built video screen material");
	return true;
}

static bool BuildSpecialLampshadeGraph(UMaterial* Material, FString& OutMessage)
{
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;
	Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
	Material->TwoSided = true;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupShade(TEXT("01 透光灯罩"));
	const FName GroupUV(TEXT("02 UV 调整"));
	AddSpecialBaseGroups(Material, { { GroupShade, 10 }, { GroupUV, 20 } });
	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);
	UMaterialExpressionTextureSampleParameter2D* ShadeTex = AddTextureParameterForType(Material, EPBRMaterialType::Fabric, FPBRMaterialParameters::BaseColorTexture, GroupShade, 10, -900, -320);
	ShadeTex->Texture = CreateSpecialDemoTextureAsset(TEXT("Lampshade_WarmFabric"), true);
	ShadeTex->Coordinates.Connect(0, SharedUV);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupShade, 20, FLinearColor(1.0f, 0.72f, 0.38f), -900, -120);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupShade, 30, 0.58f, -900, 80);
	UMaterialExpressionVectorParameter* EmissiveColor = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupShade, 40, FLinearColor(1.0f, 0.58f, 0.22f), -900, 260);
	UMaterialExpressionScalarParameter* EmissiveIntensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupShade, 50, 1.4f, -900, 440);
	UMaterialExpressionMultiply* BaseColor = AddMultiply(Material, -620, -260);
	BaseColor->A.Connect(0, ShadeTex);
	BaseColor->B.Connect(0, Tint);
	UMaterialExpressionMultiply* Emissive = AddMultiply(Material, -620, 240);
	Emissive->A.Connect(0, EmissiveColor);
	Emissive->B.Connect(0, EmissiveIntensity);
	EditorData->BaseColor.Connect(0, BaseColor);
	EditorData->Opacity.Connect(0, Opacity);
	EditorData->EmissiveColor.Connect(0, Emissive);
	EditorData->Roughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupShade, 60, 0.72f, -900, 620));
	AddSpecialGraphComment(Material, TEXT("视频灯罩 / 半透明暖色透光"), -1020, -420, 1480, 1260);
	OutMessage = TEXT("Built translucent lampshade material");
	return true;
}

static bool BuildSpecialMaterialGraph(UMaterial* Material, const FString& AssetName, FString& OutMessage)
{
	if (!Material)
	{
		OutMessage = TEXT("Special material is null");
		return false;
	}

	bool bBuilt = false;
	if (AssetName == TEXT("SM_Decal_DBuffer_Color"))
	{
		bBuilt = BuildSpecialDecalGraphV2(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_UI_Texture"))
	{
		bBuilt = BuildSpecialUIGraphV2(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_Niagara_Particle_Soft"))
	{
		bBuilt = BuildSpecialNiagaraParticleGraphV2(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_RuntimeVirtualTexture_Output"))
	{
		bBuilt = BuildSpecialRVTOutputGraphV2(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_DistanceField_Blend"))
	{
		bBuilt = BuildSpecialDistanceFieldGraphV2(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_PostProcess_Tint"))
	{
		bBuilt = BuildSpecialPostProcessTintGraph(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_LightFunction_SoftMask"))
	{
		bBuilt = BuildSpecialLightFunctionGraph(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_Hologram_Fresnel"))
	{
		bBuilt = BuildSpecialHologramGraph(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_Niagara_Smoke_Soft"))
	{
		bBuilt = BuildSpecialNiagaraSpriteGraph(Material, TEXT("Smoke_Sprite"), FLinearColor(0.55f, 0.58f, 0.62f), 0.8f, OutMessage);
	}
	else if (AssetName == TEXT("SM_Niagara_Lightning"))
	{
		bBuilt = BuildSpecialNiagaraSpriteGraph(Material, TEXT("Lightning_Sprite"), FLinearColor(0.2f, 0.65f, 1.0f), 8.0f, OutMessage);
	}
	else if (AssetName == TEXT("SM_CarPaint_ClearCoat"))
	{
		bBuilt = BuildSpecialClearCoatGraph(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_Abyss_Mirror"))
	{
		bBuilt = BuildSpecialAbyssMirrorGraph(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_TwoSided_Leaf"))
	{
		bBuilt = BuildSpecialLeafGraph(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_WindowScreen_Mesh"))
	{
		bBuilt = BuildSpecialWindowScreenGraph(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_Video_Screen"))
	{
		bBuilt = BuildSpecialVideoScreenGraph(Material, OutMessage);
	}
	else if (AssetName == TEXT("SM_Lampshade_Translucent"))
	{
		bBuilt = BuildSpecialLampshadeGraph(Material, OutMessage);
	}
	else
	{
		bBuilt = BuildSpecialUIGraphV2(Material, OutMessage);
	}

	if (bBuilt)
	{
		RemoveUnusedMaterialExpressions(Material);
		LayoutMaterialGraphByColumns(Material);
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
	}
	return bBuilt;
}

static constexpr const TCHAR* PBRARMParentGraphLayoutMarker = TEXT("ARM Parent Graph Layout v8");
static constexpr float PBRARMDefaultWaterOpacity = 0.65f;

struct FPBRARMGraphLane
{
	int32 Y = 0;
	int32 Height = 0;
};

struct FPBRARMParentGraphLayout
{
	int32 InputX = -560;
	int32 MaskX = -280;
	int32 FunctionX = 40;
	int32 SwitchX = 320;
	int32 MidX = 640;
	int32 SpecialInputX = 700;
	int32 SpecialFunctionX = 1010;
	int32 SpecialResultX = 1320;
	int32 OutputDeclX = 1620;
	int32 OutputUseX = 1810;

	FPBRARMGraphLane BaseColor{ -620, 460 };
	FPBRARMGraphLane Normal{ -100, 420 };
	FPBRARMGraphLane Scalar{ 380, 1540 };
	FPBRARMGraphLane Opacity{ 2040, 460 };
	FPBRARMGraphLane Emissive{ 2540, 560 };
	FPBRARMGraphLane Displacement{ 1340, 460 };
	FPBRARMGraphLane TypeSpecific{ 1340, 900 };
	FPBRARMGraphLane Output{ -420, 1500 };
};

static const FPBRARMParentGraphLayout PBRARMLayout{};

static bool PBRARMShouldUseOpacityGraph(EPBRMaterialType MaterialType)
{
	return IsTranslucentMaterialType(MaterialType);
}

static bool PBRARMShouldUseEmissiveGraph(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Emissive;
}

static bool PBRARMShouldUseMetallicGraph(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Standard ||
		MaterialType == EPBRMaterialType::Metal;
}

static bool PBRARMShouldUseDisplacementGraph(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Stone ||
		MaterialType == EPBRMaterialType::Tile;
}

static bool PBRARMShouldUseTypeSpecificGraph(EPBRMaterialType MaterialType)
{
	switch (MaterialType)
	{
	case EPBRMaterialType::Fabric:
	case EPBRMaterialType::Leather:
	case EPBRMaterialType::Metal:
	case EPBRMaterialType::Water:
	case EPBRMaterialType::Glass:
	case EPBRMaterialType::Transparent:
		return true;
	default:
		return false;
	}
}

static void PBRARMAddMaterialComment(UMaterial* Material, const FString& Text, const FLinearColor& Color, int32 X, int32 Y, int32 SizeX, int32 SizeY)
{
	UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Material);
	Comment->Text = Text;
	Comment->CommentColor = Color;
	Comment->MaterialExpressionEditorX = X;
	Comment->MaterialExpressionEditorY = Y;
	Comment->SizeX = SizeX;
	Comment->SizeY = SizeY;
	Comment->FontSize = 20;
	Comment->bGroupMode = true;
	Material->GetExpressionCollection().AddExpression(Comment);
}

static UMaterialExpressionNamedRerouteDeclaration* PBRARMAddNamedRerouteDeclaration(UMaterial* Material, const FName& Name, UMaterialExpression* InputExpression, int32 OutputIndex, const FLinearColor& Color, int32 X, int32 Y)
{
	if (!Material || !InputExpression)
	{
		return nullptr;
	}

	UMaterialExpressionNamedRerouteDeclaration* Declaration = NewObject<UMaterialExpressionNamedRerouteDeclaration>(Material);
	Declaration->Name = Name;
	Declaration->NodeColor = Color;
	Declaration->VariableGuid = FGuid::NewGuid();
	Declaration->MaterialExpressionEditorX = X;
	Declaration->MaterialExpressionEditorY = Y;
	Declaration->Input.Connect(OutputIndex, InputExpression);
	Material->GetExpressionCollection().AddExpression(Declaration);
	return Declaration;
}

static UMaterialExpressionNamedRerouteUsage* PBRARMAddNamedRerouteUsage(UMaterial* Material, UMaterialExpressionNamedRerouteDeclaration* Declaration, int32 X, int32 Y)
{
	if (!Declaration)
	{
		return nullptr;
	}

	UMaterialExpressionNamedRerouteUsage* Usage = NewObject<UMaterialExpressionNamedRerouteUsage>(Material);
	Usage->Declaration = Declaration;
	Usage->DeclarationGuid = Declaration->VariableGuid;
	Usage->MaterialExpressionEditorX = X;
	Usage->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Usage);
	return Usage;
}

static UMaterialExpressionReroute* PBRARMAddReroute(UMaterial* Material, UMaterialExpression* InputExpression, int32 X, int32 Y)
{
	if (!InputExpression)
	{
		return nullptr;
	}

	UMaterialExpressionReroute* Reroute = NewObject<UMaterialExpressionReroute>(Material);
	Reroute->MaterialExpressionEditorX = X;
	Reroute->MaterialExpressionEditorY = Y;
	Reroute->Input.Connect(0, InputExpression);
	Material->GetExpressionCollection().AddExpression(Reroute);
	return Reroute;
}

static UMaterialExpressionFresnel* PBRARMAddFresnel(UMaterial* Material, float Exponent, float BaseReflectFraction, int32 X, int32 Y)
{
	UMaterialExpressionFresnel* Node = NewObject<UMaterialExpressionFresnel>(Material);
	Node->Exponent = Exponent;
	Node->BaseReflectFraction = BaseReflectFraction;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionLinearInterpolate* PBRARMAddLerp(UMaterial* Material, float ConstAlpha, int32 X, int32 Y)
{
	UMaterialExpressionLinearInterpolate* Node = NewObject<UMaterialExpressionLinearInterpolate>(Material);
	Node->ConstAlpha = ConstAlpha;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionComponentMask* PBRARMAddTextureMask(UMaterial* Material, UMaterialExpression* Input, bool bR, bool bG, bool bB, bool bA, int32 X, int32 Y)
{
	UMaterialExpressionComponentMask* Node = NewObject<UMaterialExpressionComponentMask>(Material);
	Node->R = bR;
	Node->G = bG;
	Node->B = bB;
	Node->A = bA;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Node->Input.Connect(0, Input);
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionCustom* PBRARMAddCustom(UMaterial* Material, ECustomMaterialOutputType OutputType, const FString& Description, const FString& Code, int32 InputCount, int32 X, int32 Y)
{
	UMaterialExpressionCustom* Node = NewObject<UMaterialExpressionCustom>(Material);
	Node->OutputType = OutputType;
	Node->Description = Description;
	Node->Code = Code;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Node->Inputs.AddDefaulted(InputCount);
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionCustom* PBRARMAddFlakeMask(UMaterial* Material, UMaterialExpression* UVExpression, UMaterialExpression* ScaleExpression, int32 X, int32 Y)
{
	UMaterialExpressionCustom* Node = PBRARMAddCustom(
		Material,
		CMOT_Float1,
		TEXT("AR Metal Flake Mask"),
		TEXT("float flakeScale = max(Scale, 1.0);\nfloat2 cell = floor(UV * flakeScale);\nfloat sparkle = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);\nreturn step(0.975, sparkle);"),
		2,
		X,
		Y);
	Node->Inputs[0].InputName = TEXT("UV");
	Node->Inputs[0].Input.Connect(0, UVExpression);
	Node->Inputs[1].InputName = TEXT("Scale");
	Node->Inputs[1].Input.Connect(0, ScaleExpression);
	return Node;
}

static UMaterialExpressionCustom* PBRARMAddCenteredHeightOffset(UMaterial* Material, UMaterialExpression* HeightExpression, UMaterialExpression* StrengthExpression, int32 X, int32 Y)
{
	UMaterialExpressionCustom* Node = PBRARMAddCustom(Material, CMOT_Float1, TEXT("AR Centered Height Offset"), TEXT("return (Height - 0.5) * Strength;"), 2, X, Y);
	Node->Inputs[0].InputName = TEXT("Height");
	Node->Inputs[0].Input.Connect(0, HeightExpression);
	Node->Inputs[1].InputName = TEXT("Strength");
	Node->Inputs[1].Input.Connect(0, StrengthExpression);
	return Node;
}

static UMaterialExpressionCustom* PBRARMAddWaterRippleMask(
	UMaterial* Material,
	UMaterialExpression* UVExpression,
	UMaterialExpression* TimeExpression,
	UMaterialExpression* FlowUExpression,
	UMaterialExpression* FlowVExpression,
	UMaterialExpression* ScaleExpression,
	UMaterialExpression* StrengthExpression,
	int32 X,
	int32 Y)
{
	UMaterialExpressionCustom* Node = PBRARMAddCustom(
		Material,
		CMOT_Float1,
		TEXT("AR Water Ripple Mask"),
		TEXT("float2 movedUV = UV + Time * float2(FlowU, FlowV);\nfloat waveScale = max(Scale, 0.01) * 6.2831853;\nfloat waves = (sin(movedUV.x * waveScale) + sin(movedUV.y * waveScale * 1.37)) * 0.25 + 0.5;\nreturn saturate(waves) * Strength * 1.5;"),
		6,
		X,
		Y);
	Node->Inputs[0].InputName = TEXT("UV");
	Node->Inputs[0].Input.Connect(0, UVExpression);
	Node->Inputs[1].InputName = TEXT("Time");
	Node->Inputs[1].Input.Connect(0, TimeExpression);
	Node->Inputs[2].InputName = TEXT("FlowU");
	Node->Inputs[2].Input.Connect(0, FlowUExpression);
	Node->Inputs[3].InputName = TEXT("FlowV");
	Node->Inputs[3].Input.Connect(0, FlowVExpression);
	Node->Inputs[4].InputName = TEXT("Scale");
	Node->Inputs[4].Input.Connect(0, ScaleExpression);
	Node->Inputs[5].InputName = TEXT("Strength");
	Node->Inputs[5].Input.Connect(0, StrengthExpression);
	return Node;
}

static UMaterialExpressionCustom* PBRARMAddWaterFlowUV(
	UMaterial* Material,
	UMaterialExpression* UVExpression,
	UMaterialExpression* TimeExpression,
	UMaterialExpression* FlowUExpression,
	UMaterialExpression* FlowVExpression,
	int32 X,
	int32 Y)
{
	UMaterialExpressionCustom* Node = PBRARMAddCustom(Material, CMOT_Float2, TEXT("AR Water Flow UV"), TEXT("return UV + Time * float2(FlowU, FlowV);"), 4, X, Y);
	Node->Inputs[0].InputName = TEXT("UV");
	Node->Inputs[0].Input.Connect(0, UVExpression);
	Node->Inputs[1].InputName = TEXT("Time");
	Node->Inputs[1].Input.Connect(0, TimeExpression);
	Node->Inputs[2].InputName = TEXT("FlowU");
	Node->Inputs[2].Input.Connect(0, FlowUExpression);
	Node->Inputs[3].InputName = TEXT("FlowV");
	Node->Inputs[3].Input.Connect(0, FlowVExpression);
	return Node;
}

static UMaterialExpressionCustom* PBRARMAddWaterRippleNormal(
	UMaterial* Material,
	UMaterialExpression* UVExpression,
	UMaterialExpression* TimeExpression,
	UMaterialExpression* FlowUExpression,
	UMaterialExpression* FlowVExpression,
	UMaterialExpression* ScaleExpression,
	UMaterialExpression* StrengthExpression,
	int32 X,
	int32 Y)
{
	UMaterialExpressionCustom* Node = PBRARMAddCustom(
		Material,
		CMOT_Float3,
		TEXT("AR Water Ripple Normal"),
		TEXT("float2 movedUV = UV + Time * float2(FlowU, FlowV);\nfloat waveScale = max(Scale, 0.01) * 6.2831853;\nfloat dx = cos(dot(movedUV, float2(1.0, 0.32)) * waveScale) * waveScale;\ndx += cos(dot(movedUV * 1.73 + 0.19, float2(-0.56, 0.83)) * waveScale * 0.61) * waveScale * 0.61;\nfloat dy = cos(dot(movedUV, float2(-0.27, 1.0)) * waveScale * 0.87) * waveScale * 0.87;\ndy += cos(dot(movedUV * 1.31 + 0.43, float2(0.72, 0.41)) * waveScale * 0.49) * waveScale * 0.49;\nfloat normalScale = Strength * 0.006;\nreturn normalize(float3(-dx * normalScale, dy * normalScale, 1.0));"),
		6,
		X,
		Y);
	Node->Inputs[0].InputName = TEXT("UV");
	Node->Inputs[0].Input.Connect(0, UVExpression);
	Node->Inputs[1].InputName = TEXT("Time");
	Node->Inputs[1].Input.Connect(0, TimeExpression);
	Node->Inputs[2].InputName = TEXT("FlowU");
	Node->Inputs[2].Input.Connect(0, FlowUExpression);
	Node->Inputs[3].InputName = TEXT("FlowV");
	Node->Inputs[3].Input.Connect(0, FlowVExpression);
	Node->Inputs[4].InputName = TEXT("Scale");
	Node->Inputs[4].Input.Connect(0, ScaleExpression);
	Node->Inputs[5].InputName = TEXT("Strength");
	Node->Inputs[5].Input.Connect(0, StrengthExpression);
	return Node;
}

static UMaterialExpressionCustom* PBRARMAddGlassCausticsMask(UMaterial* Material, UMaterialExpression* UVExpression, UMaterialExpression* TimeExpression, UMaterialExpression* ScaleExpression, UMaterialExpression* SpeedExpression, UMaterialExpression* IntensityExpression, int32 X, int32 Y)
{
	UMaterialExpressionCustom* Node = PBRARMAddCustom(
		Material,
		CMOT_Float1,
		TEXT("AR Glass Caustics Mask"),
		TEXT("float causticScale = max(Scale, 0.01) * 6.2831853;\nfloat2 movedUV = UV * causticScale + Time * Speed * float2(1.17, -0.73);\nfloat waveA = sin(movedUV.x + sin(movedUV.y * 0.83));\nfloat waveB = sin(dot(movedUV, float2(-0.62, 1.21)) + Time * Speed * 1.7);\nfloat waveC = sin(dot(movedUV, float2(1.37, 0.48)) - Time * Speed * 1.31);\nfloat mask = saturate((waveA + waveB + waveC) * 0.22 + 0.52);\nmask = pow(mask, 5.0);\nreturn saturate(mask * Intensity * 3.0);"),
		5,
		X,
		Y);
	Node->Inputs[0].InputName = TEXT("UV");
	Node->Inputs[0].Input.Connect(0, UVExpression);
	Node->Inputs[1].InputName = TEXT("Time");
	Node->Inputs[1].Input.Connect(0, TimeExpression);
	Node->Inputs[2].InputName = TEXT("Scale");
	Node->Inputs[2].Input.Connect(0, ScaleExpression);
	Node->Inputs[3].InputName = TEXT("Speed");
	Node->Inputs[3].Input.Connect(0, SpeedExpression);
	Node->Inputs[4].InputName = TEXT("Intensity");
	Node->Inputs[4].Input.Connect(0, IntensityExpression);
	return Node;
}

static UMaterialExpressionCustom* PBRARMAddUVRotateDegrees(UMaterial* Material, UMaterialExpression* UVExpression, UMaterialExpression* RotationDegrees, int32 X, int32 Y)
{
	UMaterialExpressionCustom* Node = PBRARMAddCustom(
		Material,
		CMOT_Float2,
		TEXT("AR UV Rotate Degrees"),
		TEXT("float radiansValue = radians(Degrees);\nfloat s = sin(radiansValue);\nfloat c = cos(radiansValue);\nfloat2 centered = UV - float2(0.5, 0.5);\nfloat2 rotated = float2(centered.x * c - centered.y * s, centered.x * s + centered.y * c);\nreturn rotated + float2(0.5, 0.5);"),
		2,
		X,
		Y);
	Node->Inputs[0].InputName = TEXT("UV");
	Node->Inputs[0].Input.Connect(0, UVExpression);
	Node->Inputs[1].InputName = TEXT("Degrees");
	Node->Inputs[1].Input.Connect(0, RotationDegrees);
	return Node;
}

static UMaterialExpression* PBRARMBuildUVTransform(
	UMaterial* Material,
	const FName& GroupName,
	UMaterialExpression* InputUV,
	const FName& UTilingName,
	const FName& VTilingName,
	const FName& UOffsetName,
	const FName& VOffsetName,
	const FName& RotationDegreesName,
	int32 SortPriorityBase,
	int32 X,
	int32 Y)
{
	UMaterialExpressionScalarParameter* UTiling = AddScalarParameter(Material, UTilingName, GroupName, SortPriorityBase + 10, 1.0f, X, Y + 100);
	UMaterialExpressionScalarParameter* VTiling = AddScalarParameter(Material, VTilingName, GroupName, SortPriorityBase + 20, 1.0f, X, Y + 190);
	UMaterialExpressionScalarParameter* UOffset = AddScalarParameter(Material, UOffsetName, GroupName, SortPriorityBase + 30, 0.0f, X, Y + 280);
	UMaterialExpressionScalarParameter* VOffset = AddScalarParameter(Material, VOffsetName, GroupName, SortPriorityBase + 40, 0.0f, X, Y + 370);
	UMaterialExpressionScalarParameter* RotationDegrees = AddScalarParameter(Material, RotationDegreesName, GroupName, SortPriorityBase + 50, 0.0f, X, Y + 460);

	if (UMaterialExpressionMaterialFunctionCall* UVFunction = AddMaterialFunctionCall(
		Material,
		TEXT("/Game/PBRStudio/Functions/01_UV/MF_PBRStudio_UVControls.MF_PBRStudio_UVControls"),
		X + 300,
		Y + 170))
	{
		const bool bConnected =
			ConnectFunctionInputByName(UVFunction, TEXT("UV"), InputUV) &
			ConnectFunctionInputByName(UVFunction, TEXT("U 平铺"), UTiling) &
			ConnectFunctionInputByName(UVFunction, TEXT("V 平铺"), VTiling) &
			ConnectFunctionInputByName(UVFunction, TEXT("U 偏移"), UOffset) &
			ConnectFunctionInputByName(UVFunction, TEXT("V 偏移"), VOffset) &
			ConnectFunctionInputByName(UVFunction, TEXT("旋转角度"), RotationDegrees);
		if (bConnected)
		{
			return UVFunction;
		}
	}

	UMaterialExpression* UVInputExpression = InputUV;
	if (!UVInputExpression)
	{
		UMaterialExpressionTextureCoordinate* FallbackTexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
		FallbackTexCoord->MaterialExpressionEditorX = X;
		FallbackTexCoord->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(FallbackTexCoord);
		UVInputExpression = FallbackTexCoord;
	}

	UMaterialExpressionAppendVector* TilingAppend = NewObject<UMaterialExpressionAppendVector>(Material);
	TilingAppend->MaterialExpressionEditorX = X + 270;
	TilingAppend->MaterialExpressionEditorY = Y + 120;
	TilingAppend->A.Connect(0, UTiling);
	TilingAppend->B.Connect(0, VTiling);
	Material->GetExpressionCollection().AddExpression(TilingAppend);

	UMaterialExpressionAppendVector* OffsetAppend = NewObject<UMaterialExpressionAppendVector>(Material);
	OffsetAppend->MaterialExpressionEditorX = X + 270;
	OffsetAppend->MaterialExpressionEditorY = Y + 300;
	OffsetAppend->A.Connect(0, UOffset);
	OffsetAppend->B.Connect(0, VOffset);
	Material->GetExpressionCollection().AddExpression(OffsetAppend);

	UMaterialExpressionMultiply* TiledUV = AddMultiply(Material, X + 500, Y + 100);
	TiledUV->A.Connect(0, UVInputExpression);
	TiledUV->B.Connect(0, TilingAppend);

	UMaterialExpressionAdd* OffsetUV = AddAdd(Material, X + 700, Y + 130);
	OffsetUV->A.Connect(0, TiledUV);
	OffsetUV->B.Connect(0, OffsetAppend);
	return PBRARMAddUVRotateDegrees(Material, OffsetUV, RotationDegrees, X + 880, Y + 160);
}

static UMaterialExpression* PBRARMBuildSimpleUV(UMaterial* Material, const FName& GroupName)
{
	UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
	TexCoord->MaterialExpressionEditorX = -1580;
	TexCoord->MaterialExpressionEditorY = -500;
	Material->GetExpressionCollection().AddExpression(TexCoord);

	return PBRARMBuildUVTransform(
		Material,
		GroupName,
		TexCoord,
		FPBRMaterialParameters::UVUTiling,
		FPBRMaterialParameters::UVVTiling,
		FPBRMaterialParameters::UVUOffset,
		FPBRMaterialParameters::UVVOffset,
		FPBRMaterialParameters::UVRotationDegrees,
		10,
		-1580,
		-360);
}

static UMaterialExpression* PBRARMBuildChannelUV(
	UMaterial* Material,
	UMaterialExpressionNamedRerouteDeclaration* SharedUVDeclaration,
	UMaterialExpression* SharedUVExpression,
	const FName& ChannelName,
	const FName& GroupName,
	int32 SortPriorityBase,
	int32 X,
	int32 Y,
	int32 UsageX,
	int32 UsageY)
{
	PBRARMAddMaterialComment(Material, FString::Printf(TEXT("%s UV Override"), *ChannelName.ToString()), FLinearColor(0.035f, 0.035f, 0.035f, 1.0f), X - 50, Y - 70, 1180, 620);
	const FPBRChannelUVParameterNames ChannelUV = FPBRMaterialParameters::GetChannelUVNames(ChannelName);
	UMaterialExpression* LocalSharedUV = PBRARMAddReroute(Material, PBRARMAddNamedRerouteUsage(Material, SharedUVDeclaration, X, Y), X + 210, Y + 20);
	if (!LocalSharedUV)
	{
		LocalSharedUV = SharedUVExpression;
	}
	UMaterialExpression* ChannelTransform = PBRARMBuildUVTransform(
		Material,
		GroupName,
		LocalSharedUV,
		ChannelUV.UTiling,
		ChannelUV.VTiling,
		ChannelUV.UOffset,
		ChannelUV.VOffset,
		ChannelUV.RotationDegrees,
		SortPriorityBase,
		X,
		Y);

	UMaterialExpressionStaticSwitchParameter* UseIndependentUV = AddStaticSwitchParameter(Material, ChannelUV.UseIndependentUV, GroupName, SortPriorityBase + 1, false, X + 900, Y + 220);
	UseIndependentUV->A.Connect(0, ChannelTransform);
	UseIndependentUV->B.Connect(0, LocalSharedUV ? LocalSharedUV : ChannelTransform);

	UMaterialExpressionNamedRerouteDeclaration* ChannelUVDeclaration = PBRARMAddNamedRerouteDeclaration(
		Material,
		FName(*(ChannelName.ToString() + TEXT(" UV"))),
		UseIndependentUV,
		0,
		FLinearColor(0.20f, 0.56f, 0.58f, 1.0f),
		X + 1040,
		Y + 220);
	return PBRARMAddNamedRerouteUsage(Material, ChannelUVDeclaration, UsageX, UsageY);
}

static bool BuildARMStyleTemplateGraph(UMaterial* Material, EPBRMaterialType MaterialType, FString& OutMessage)
{
	if (!Material)
	{
		OutMessage = TEXT("母材质为空");
		return false;
	}

	ConfigureMaterialDomainForType(Material, MaterialType);
	const bool bUseGlassGraph = MaterialType == EPBRMaterialType::Glass;
	const bool bUseMetallicGraph = PBRARMShouldUseMetallicGraph(MaterialType);
	const bool bUseOpacityGraph = PBRARMShouldUseOpacityGraph(MaterialType);
	const bool bUseEmissiveGraph = PBRARMShouldUseEmissiveGraph(MaterialType);
	const bool bUseDisplacementGraph = PBRARMShouldUseDisplacementGraph(MaterialType);
	const bool bUseTypeSpecificGraph = PBRARMShouldUseTypeSpecificGraph(MaterialType);
	const bool bUseWaterGraph = MaterialType == EPBRMaterialType::Water;

	Material->bEnableTessellation = bUseDisplacementGraph;
	Material->bEnableDisplacementFade = bUseDisplacementGraph;
	Material->DisplacementScaling.Magnitude = bUseDisplacementGraph ? 1.0f : 0.0f;
	Material->DisplacementScaling.Center = 0.5f;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("无法访问材质编辑数据");
		return false;
	}
	EditorData->ParameterGroupData.Empty();

	const FName GroupBaseColor(bUseGlassGraph ? TEXT("00 - 颜色") : TEXT("01 基础颜色"));
	const FName GroupNormal(bUseGlassGraph ? TEXT("03 - 法线") : TEXT("02 法线"));
	const FName GroupRoughness(bUseGlassGraph ? TEXT("04 - 反射") : TEXT("03 粗糙度"));
	const FName GroupMetallic(bUseGlassGraph ? TEXT("04 - 反射") : TEXT("04 金属"));
	const FName GroupOpacity(bUseGlassGraph ? TEXT("01 - 透明") : TEXT("05 透明"));
	const FName GroupEmissive(TEXT("06 自发光"));
	const FName GroupSpecial(bUseGlassGraph ? TEXT("02 - 折射") : TEXT("07 类型专属"));
	const FName GroupUV(bUseGlassGraph ? TEXT("05 - UV") : TEXT("08 UV 调整"));

	if (bUseGlassGraph)
	{
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("00 - 颜色"), 10));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("01 - 透明"), 20));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("02 - 折射"), 30));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("03 - 法线"), 40));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("04 - 反射"), 50));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("05 - UV"), 60));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("06 - 污渍"), 70));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("07 - 扭曲"), 80));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("08 - 磨砂"), 90));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("09 - 阴影"), 100));
		EditorData->ParameterGroupData.Add(FParameterGroupData(TEXT("10 - 光线追踪"), 110));
	}
	else
	{
		EditorData->ParameterGroupData.Add(FParameterGroupData(GroupBaseColor.ToString(), 10));
		EditorData->ParameterGroupData.Add(FParameterGroupData(GroupNormal.ToString(), 20));
		EditorData->ParameterGroupData.Add(FParameterGroupData(GroupRoughness.ToString(), 30));
		if (bUseMetallicGraph)
		{
			EditorData->ParameterGroupData.Add(FParameterGroupData(GroupMetallic.ToString(), 40));
		}
		EditorData->ParameterGroupData.Add(FParameterGroupData(GroupOpacity.ToString(), 50));
		EditorData->ParameterGroupData.Add(FParameterGroupData(GroupEmissive.ToString(), 60));
		EditorData->ParameterGroupData.Add(FParameterGroupData(GroupSpecial.ToString(), 70));
		EditorData->ParameterGroupData.Add(FParameterGroupData(GroupUV.ToString(), 80));
	}

	PBRARMAddMaterialComment(Material, PBRARMParentGraphLayoutMarker, FLinearColor(0.04f, 0.04f, 0.04f, 1.0f), -1640, -620, 860, 600);
	if (!bUseGlassGraph)
	{
		PBRARMAddMaterialComment(Material, TEXT("UV Overrides: optional per-channel controls"), FLinearColor(0.05f, 0.05f, 0.05f, 1.0f), -3640, 120, 2660, bUseWaterGraph || bUseEmissiveGraph || bUseDisplacementGraph || bUseOpacityGraph ? 2740 : 2080);
	}
	PBRARMAddMaterialComment(Material, TEXT("Base Color"), FLinearColor(0.44f, 0.42f, 0.22f, 1.0f), -720, PBRARMLayout.BaseColor.Y - 80, 1140, PBRARMLayout.BaseColor.Height);
	PBRARMAddMaterialComment(Material, TEXT("Normal"), FLinearColor(0.02f, 0.06f, 0.55f, 1.0f), -720, PBRARMLayout.Normal.Y - 80, 1140, PBRARMLayout.Normal.Height);
	PBRARMAddMaterialComment(Material, bUseMetallicGraph ? TEXT("Surface Scalars: Roughness / Specular / Metallic / AO") : TEXT("Surface Scalars: Roughness / Specular / AO"), FLinearColor(0.56f, 0.02f, 0.12f, 1.0f), -720, PBRARMLayout.Scalar.Y - 80, 1140, PBRARMLayout.Scalar.Height);
	if (bUseOpacityGraph)
	{
		PBRARMAddMaterialComment(Material, TEXT("Opacity"), FLinearColor(0.03f, 0.24f, 0.22f, 1.0f), -720, PBRARMLayout.Opacity.Y - 80, 1140, PBRARMLayout.Opacity.Height);
	}
	if (bUseEmissiveGraph)
	{
		PBRARMAddMaterialComment(Material, TEXT("Emissive"), FLinearColor(0.03f, 0.18f, 0.28f, 1.0f), -720, PBRARMLayout.Emissive.Y - 80, 1140, PBRARMLayout.Emissive.Height);
	}
	if (bUseDisplacementGraph)
	{
		PBRARMAddMaterialComment(Material, TEXT("Displacement - Experimental"), FLinearColor(0.32f, 0.02f, 0.50f, 1.0f), 680, PBRARMLayout.Displacement.Y - 80, 1180, PBRARMLayout.Displacement.Height);
	}
	if (bUseTypeSpecificGraph)
	{
		const int32 TypeSpecificHeight = MaterialType == EPBRMaterialType::Water
			? 900
			: (MaterialType == EPBRMaterialType::Glass ? 1780 : 360);
		PBRARMAddMaterialComment(Material, TEXT("Type Specific"), FLinearColor(0.12f, 0.18f, 0.20f, 1.0f), 620, PBRARMLayout.TypeSpecific.Y - 80, 1360, TypeSpecificHeight);
	}
	PBRARMAddMaterialComment(Material, TEXT("Material Output"), FLinearColor(0.04f, 0.04f, 0.04f, 1.0f), PBRARMLayout.OutputUseX - 70, PBRARMLayout.Output.Y - 80, 390, 1240);
	Material->EditorX = PBRARMLayout.OutputUseX + 260;
	Material->EditorY = PBRARMLayout.Output.Y;

	UMaterialExpression* SharedUV = PBRARMBuildSimpleUV(Material, GroupUV);
	UMaterialExpressionNamedRerouteDeclaration* SharedUVDeclaration = PBRARMAddNamedRerouteDeclaration(Material, TEXT("UVs"), SharedUV, 0, FLinearColor(0.20f, 0.56f, 0.58f, 1.0f), -820, -230);
	constexpr int32 UVLeftX = -3580;
	constexpr int32 UVRightX = -2260;
	constexpr int32 UVTopY = 260;
	constexpr int32 UVRowGap = 660;
	UMaterialExpression* BaseColorUV = bUseGlassGraph ? SharedUV : PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("基础色"), GroupUV, 100, UVLeftX, UVTopY, PBRARMLayout.InputX - 180, PBRARMLayout.BaseColor.Y + 120);
	UMaterialExpression* NormalUV = bUseGlassGraph ? SharedUV : PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("法线"), GroupUV, 200, UVLeftX, UVTopY + UVRowGap, PBRARMLayout.InputX - 180, PBRARMLayout.Normal.Y + 120);
	UMaterialExpression* RoughnessUV = bUseGlassGraph ? SharedUV : PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("粗糙度"), GroupUV, 300, UVLeftX, UVTopY + UVRowGap * 2, PBRARMLayout.InputX - 180, PBRARMLayout.Scalar.Y + 140);
	UMaterialExpression* SpecularUV = bUseGlassGraph ? SharedUV : PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("高光"), GroupUV, 400, UVRightX, UVTopY, PBRARMLayout.InputX - 180, PBRARMLayout.Scalar.Y + 520);
	UMaterialExpression* MetallicUV = bUseMetallicGraph ? (bUseGlassGraph ? SharedUV : PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("金属度"), GroupUV, 500, UVRightX, UVTopY + UVRowGap, PBRARMLayout.InputX - 180, PBRARMLayout.Scalar.Y + 900)) : nullptr;
	UMaterialExpression* AOUV = bUseGlassGraph ? SharedUV : PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("环境遮蔽"), GroupUV, 600, UVRightX, bUseMetallicGraph ? UVTopY + UVRowGap * 2 : UVTopY + UVRowGap, PBRARMLayout.InputX - 180, PBRARMLayout.Scalar.Y + 1280);
	UMaterialExpression* OpacityUV = bUseOpacityGraph ? (bUseGlassGraph ? SharedUV : PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("透明"), GroupUV, 700, UVRightX, UVTopY + UVRowGap * 3, PBRARMLayout.InputX - 180, PBRARMLayout.Opacity.Y + 140)) : nullptr;
	UMaterialExpression* HeightUV = bUseDisplacementGraph ? PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("高度"), GroupUV, 800, UVLeftX, UVTopY + UVRowGap * 3, PBRARMLayout.MidX - 180, PBRARMLayout.Displacement.Y + 120) : nullptr;
	UMaterialExpression* EmissiveUV = bUseEmissiveGraph ? PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("自发光"), GroupUV, 900, UVLeftX, UVTopY + UVRowGap * 3, PBRARMLayout.InputX - 180, PBRARMLayout.Emissive.Y + 140) : nullptr;
	UMaterialExpression* WaterRippleUV = bUseWaterGraph ? PBRARMBuildChannelUV(Material, SharedUVDeclaration, SharedUV, TEXT("水纹"), GroupUV, 1000, UVLeftX, UVTopY + UVRowGap * 3, PBRARMLayout.SpecialInputX - 180, PBRARMLayout.TypeSpecific.Y + 520) : nullptr;

	const int32 BaseY = PBRARMLayout.BaseColor.Y;
	const int32 NormalY = PBRARMLayout.Normal.Y;
	const int32 RoughnessY = PBRARMLayout.Scalar.Y + 40;
	const int32 SpecularY = PBRARMLayout.Scalar.Y + 420;
	const int32 MetallicY = PBRARMLayout.Scalar.Y + 800;
	const int32 AOY = PBRARMLayout.Scalar.Y + 1180;
	const int32 OpacityY = PBRARMLayout.Opacity.Y + 40;
	const int32 EmissiveY = PBRARMLayout.Emissive.Y + 40;
	const int32 DisplacementY = PBRARMLayout.Displacement.Y + 40;
	const int32 SpecialY = PBRARMLayout.TypeSpecific.Y + 40;
	const int32 OutputY = PBRARMLayout.Output.Y + 80;

	UMaterialExpressionTextureSampleParameter2D* BaseTex = AddTextureParameter(Material, FPBRMaterialParameters::BaseColorTexture, GroupBaseColor, 10, PBRARMLayout.InputX, BaseY + 80);
	BaseTex->Coordinates.Connect(0, BaseColorUV);
	UMaterialExpressionVectorParameter* BaseTint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupBaseColor, 20, FLinearColor::White, PBRARMLayout.InputX, BaseY + 220);
	UMaterialExpressionScalarParameter* BaseIntensity = AddScalarParameter(Material, FPBRMaterialParameters::BaseColorIntensity, GroupBaseColor, 30, 1.0f, PBRARMLayout.InputX, BaseY + 340);
	UMaterialExpression* BaseTextureIntensity = nullptr;
	UMaterialExpression* BaseSolidIntensity = nullptr;
	if (UMaterialExpressionMaterialFunctionCall* BaseColorFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/02_Surface/MF_PBRStudio_BaseColor.MF_PBRStudio_BaseColor"), PBRARMLayout.FunctionX, BaseY + 120))
	{
		const bool bConnected =
			ConnectFunctionInputByName(BaseColorFunction, TEXT("贴图颜色"), BaseTex) &
			ConnectFunctionInputByName(BaseColorFunction, TEXT("调色"), BaseTint) &
			ConnectFunctionInputByName(BaseColorFunction, TEXT("强度"), BaseIntensity);
		if (bConnected)
		{
			BaseTextureIntensity = BaseColorFunction;
			BaseSolidIntensity = BaseColorFunction;
		}
	}
	if (!BaseTextureIntensity || !BaseSolidIntensity)
	{
		UMaterialExpressionMultiply* BaseTintMultiply = AddMultiply(Material, PBRARMLayout.MaskX, BaseY + 100);
		BaseTintMultiply->A.Connect(0, BaseTex);
		BaseTintMultiply->B.Connect(0, BaseTint);
		UMaterialExpressionMultiply* FallbackBaseTextureIntensity = AddMultiply(Material, PBRARMLayout.FunctionX, BaseY + 100);
		FallbackBaseTextureIntensity->A.Connect(0, BaseTintMultiply);
		FallbackBaseTextureIntensity->B.Connect(0, BaseIntensity);
		UMaterialExpressionMultiply* FallbackBaseSolidIntensity = AddMultiply(Material, PBRARMLayout.FunctionX, BaseY + 280);
		FallbackBaseSolidIntensity->A.Connect(0, BaseTint);
		FallbackBaseSolidIntensity->B.Connect(0, BaseIntensity);
		BaseTextureIntensity = FallbackBaseTextureIntensity;
		BaseSolidIntensity = FallbackBaseSolidIntensity;
	}
	UMaterialExpressionStaticSwitchParameter* UseBaseTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseBaseColorTexture, GroupBaseColor, 40, false, PBRARMLayout.SwitchX, BaseY + 180);
	UseBaseTexture->A.Connect(0, BaseTextureIntensity);
	UseBaseTexture->B.Connect(BaseTextureIntensity == BaseSolidIntensity ? 1 : 0, BaseSolidIntensity);
	UMaterialExpression* BaseColorOutput = UseBaseTexture;

	UMaterialExpressionTextureSampleParameter2D* NormalTex = AddTextureParameter(Material, FPBRMaterialParameters::NormalTexture, GroupNormal, 10, PBRARMLayout.InputX, NormalY + 80, EMaterialSamplerType::SAMPLERTYPE_Normal);
	NormalTex->Coordinates.Connect(0, NormalUV);
	UMaterialExpressionScalarParameter* NormalStrength = AddScalarParameter(Material, FPBRMaterialParameters::NormalStrength, GroupNormal, 20, 1.0f, PBRARMLayout.InputX, NormalY + 220);
	UMaterialExpression* NormalOutput = nullptr;
	if (UMaterialExpressionMaterialFunctionCall* NormalFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/02_Surface/MF_PBRStudio_NormalStrength.MF_PBRStudio_NormalStrength"), PBRARMLayout.FunctionX, NormalY + 120))
	{
		const bool bConnected =
			ConnectFunctionInputByName(NormalFunction, TEXT("法线"), NormalTex) &
			ConnectFunctionInputByName(NormalFunction, TEXT("强度"), NormalStrength);
		if (bConnected)
		{
			NormalOutput = NormalFunction;
		}
	}
	if (!NormalOutput)
	{
		UMaterialExpressionMultiply* NormalMultiply = AddMultiply(Material, PBRARMLayout.FunctionX, NormalY + 120);
		NormalMultiply->A.Connect(0, NormalTex);
		NormalMultiply->B.Connect(0, NormalStrength);
		NormalOutput = NormalMultiply;
	}
	UMaterialExpressionConstant3Vector* FlatNormal = AddConstant3Vector(Material, FLinearColor(0.0f, 0.0f, 1.0f), PBRARMLayout.FunctionX, NormalY + 260);
	UMaterialExpressionStaticSwitchParameter* UseNormalTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseNormalTexture, GroupNormal, 30, false, PBRARMLayout.SwitchX, NormalY + 180);
	UseNormalTexture->A.Connect(0, NormalOutput);
	UseNormalTexture->B.Connect(0, FlatNormal);
	UMaterialExpression* NormalMaterialOutput = UseNormalTexture;

	UMaterialExpressionTextureSampleParameter2D* RoughnessTex = AddTextureParameter(Material, FPBRMaterialParameters::RoughnessTexture, GroupRoughness, 10, PBRARMLayout.InputX, RoughnessY, EMaterialSamplerType::SAMPLERTYPE_Masks);
	RoughnessTex->Coordinates.Connect(0, RoughnessUV);
	UMaterialExpressionComponentMask* RoughnessMask = PBRARMAddTextureMask(Material, RoughnessTex, true, false, false, false, PBRARMLayout.MaskX, RoughnessY);
	UMaterialExpressionScalarParameter* RoughnessMultiplier = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessMultiplier, GroupRoughness, 20, 1.0f, PBRARMLayout.InputX, RoughnessY + 140);
	UMaterialExpressionScalarParameter* RoughnessValue = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupRoughness, 30, 0.5f, PBRARMLayout.InputX, RoughnessY + 260);
	UMaterialExpression* RoughnessTextureOutput = nullptr;
	UMaterialExpression* RoughnessSolidOutput = nullptr;
	if (UMaterialExpressionMaterialFunctionCall* RoughnessFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/02_Surface/MF_PBRStudio_ScalarTexture.MF_PBRStudio_ScalarTexture"), PBRARMLayout.FunctionX, RoughnessY + 40))
	{
		const bool bInputsConnected =
			ConnectFunctionInputByName(RoughnessFunction, TEXT("贴图值"), RoughnessMask) &
			ConnectFunctionInputByName(RoughnessFunction, TEXT("倍增"), RoughnessMultiplier) &
			ConnectFunctionInputByName(RoughnessFunction, TEXT("固定值"), RoughnessValue);
		if (bInputsConnected)
		{
			RoughnessTextureOutput = RoughnessFunction;
			RoughnessSolidOutput = RoughnessFunction;
		}
	}
	if (!RoughnessTextureOutput)
	{
		UMaterialExpressionMultiply* RoughnessMultiply = AddMultiply(Material, PBRARMLayout.FunctionX, RoughnessY + 40);
		RoughnessMultiply->A.Connect(0, RoughnessMask);
		RoughnessMultiply->B.Connect(0, RoughnessMultiplier);
		RoughnessTextureOutput = RoughnessMultiply;
		RoughnessSolidOutput = RoughnessValue;
	}
	UMaterialExpressionStaticSwitchParameter* UseRoughnessTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseRoughnessTexture, GroupRoughness, 40, false, PBRARMLayout.SwitchX, RoughnessY + 120);
	UseRoughnessTexture->A.Connect(0, RoughnessTextureOutput);
	UseRoughnessTexture->B.Connect(RoughnessTextureOutput == RoughnessSolidOutput ? 1 : 0, RoughnessSolidOutput);
	UMaterialExpression* RoughnessMaterialOutput = UseRoughnessTexture;

	UMaterialExpressionTextureSampleParameter2D* SpecularTex = AddTextureParameter(Material, FPBRMaterialParameters::SpecularTexture, GroupRoughness, 50, PBRARMLayout.InputX, SpecularY, EMaterialSamplerType::SAMPLERTYPE_Masks);
	SpecularTex->Coordinates.Connect(0, SpecularUV);
	UMaterialExpressionComponentMask* SpecularMask = PBRARMAddTextureMask(Material, SpecularTex, true, false, false, false, PBRARMLayout.MaskX, SpecularY);
	UMaterialExpressionScalarParameter* SpecularLevel = AddScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupRoughness, 60, 0.5f, PBRARMLayout.InputX, SpecularY + 140);
	UMaterialExpression* SpecularTextureOutput = nullptr;
	UMaterialExpression* SpecularSolidOutput = nullptr;
	if (UMaterialExpressionMaterialFunctionCall* SpecularFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/02_Surface/MF_PBRStudio_ScalarTexture.MF_PBRStudio_ScalarTexture"), PBRARMLayout.FunctionX, SpecularY + 40))
	{
		const bool bInputsConnected =
			ConnectFunctionInputByName(SpecularFunction, TEXT("贴图值"), SpecularMask) &
			ConnectFunctionInputByName(SpecularFunction, TEXT("倍增"), SpecularLevel) &
			ConnectFunctionInputByName(SpecularFunction, TEXT("固定值"), SpecularLevel);
		if (bInputsConnected)
		{
			SpecularTextureOutput = SpecularFunction;
			SpecularSolidOutput = SpecularFunction;
		}
	}
	if (!SpecularTextureOutput)
	{
		UMaterialExpressionMultiply* SpecularMultiply = AddMultiply(Material, PBRARMLayout.FunctionX, SpecularY + 40);
		SpecularMultiply->A.Connect(0, SpecularMask);
		SpecularMultiply->B.Connect(0, SpecularLevel);
		SpecularTextureOutput = SpecularMultiply;
		SpecularSolidOutput = SpecularLevel;
	}
	UMaterialExpressionStaticSwitchParameter* UseSpecularTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseSpecularTexture, GroupRoughness, 70, false, PBRARMLayout.SwitchX, SpecularY + 120);
	UseSpecularTexture->A.Connect(0, SpecularTextureOutput);
	UseSpecularTexture->B.Connect(SpecularTextureOutput == SpecularSolidOutput ? 1 : 0, SpecularSolidOutput);
	UMaterialExpression* SpecularMaterialOutput = UseSpecularTexture;

	UMaterialExpression* MetallicMaterialOutput = nullptr;
	if (bUseMetallicGraph)
	{
		UMaterialExpressionTextureSampleParameter2D* MetallicTex = AddTextureParameter(Material, FPBRMaterialParameters::MetallicTexture, GroupMetallic, 10, PBRARMLayout.InputX, MetallicY, EMaterialSamplerType::SAMPLERTYPE_Masks);
		MetallicTex->Coordinates.Connect(0, MetallicUV);
		UMaterialExpressionComponentMask* MetallicMask = PBRARMAddTextureMask(Material, MetallicTex, true, false, false, false, PBRARMLayout.MaskX, MetallicY);
		UMaterialExpressionScalarParameter* MetallicMultiplier = AddScalarParameter(Material, FPBRMaterialParameters::MetallicMultiplier, GroupMetallic, 20, 0.0f, PBRARMLayout.InputX, MetallicY + 140);
		UMaterialExpressionScalarParameter* MetallicValue = AddScalarParameter(Material, FPBRMaterialParameters::MetallicValue, GroupMetallic, 30, 0.0f, PBRARMLayout.InputX, MetallicY + 260);
		UMaterialExpression* MetallicTextureOutput = nullptr;
		UMaterialExpression* MetallicSolidOutput = nullptr;
		if (UMaterialExpressionMaterialFunctionCall* MetallicFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/02_Surface/MF_PBRStudio_ScalarTexture.MF_PBRStudio_ScalarTexture"), PBRARMLayout.FunctionX, MetallicY + 40))
		{
			const bool bInputsConnected =
				ConnectFunctionInputByName(MetallicFunction, TEXT("贴图值"), MetallicMask) &
				ConnectFunctionInputByName(MetallicFunction, TEXT("倍增"), MetallicMultiplier) &
				ConnectFunctionInputByName(MetallicFunction, TEXT("固定值"), MetallicValue);
			if (bInputsConnected)
			{
				MetallicTextureOutput = MetallicFunction;
				MetallicSolidOutput = MetallicFunction;
			}
		}
		if (!MetallicTextureOutput)
		{
			UMaterialExpressionMultiply* MetallicMultiply = AddMultiply(Material, PBRARMLayout.FunctionX, MetallicY + 40);
			MetallicMultiply->A.Connect(0, MetallicMask);
			MetallicMultiply->B.Connect(0, MetallicMultiplier);
			MetallicTextureOutput = MetallicMultiply;
			MetallicSolidOutput = MetallicValue;
		}
		UMaterialExpressionStaticSwitchParameter* UseMetallicTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseMetallicTexture, GroupMetallic, 40, false, PBRARMLayout.SwitchX, MetallicY + 120);
		UseMetallicTexture->A.Connect(0, MetallicTextureOutput);
		UseMetallicTexture->B.Connect(MetallicTextureOutput == MetallicSolidOutput ? 1 : 0, MetallicSolidOutput);
		MetallicMaterialOutput = UseMetallicTexture;
	}
	else
	{
		MetallicMaterialOutput = AddConstant(Material, 0.0f, PBRARMLayout.SwitchX, MetallicY + 120);
	}

	UMaterialExpressionTextureSampleParameter2D* AOTex = AddTextureParameter(Material, FPBRMaterialParameters::AOTexture, GroupOpacity, 20, PBRARMLayout.InputX, AOY, EMaterialSamplerType::SAMPLERTYPE_Masks);
	AOTex->Coordinates.Connect(0, AOUV);
	UMaterialExpressionComponentMask* AOMask = PBRARMAddTextureMask(Material, AOTex, true, false, false, false, PBRARMLayout.MaskX, AOY);
	UMaterialExpressionStaticSwitchParameter* UseAOTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseAOTexture, GroupOpacity, 30, false, PBRARMLayout.SwitchX, AOY);
	UMaterialExpressionConstant* FullAO = AddConstant(Material, 1.0f, PBRARMLayout.MaskX, AOY - 140);
	UseAOTexture->A.Connect(0, AOMask);
	UseAOTexture->B.Connect(0, FullAO);
	UMaterialExpression* AOMaterialOutput = UseAOTexture;

	UMaterialExpressionStaticSwitchParameter* UseOpacityTexture = nullptr;
	UMaterialExpression* OpacityMaterialOutput = nullptr;
	if (bUseOpacityGraph)
	{
		float DefaultOpacity = 1.0f;
		if (MaterialType == EPBRMaterialType::Glass)
		{
			DefaultOpacity = 0.35f;
		}
		else if (MaterialType == EPBRMaterialType::Transparent)
		{
			DefaultOpacity = 0.55f;
		}
		else if (MaterialType == EPBRMaterialType::Water)
		{
			DefaultOpacity = PBRARMDefaultWaterOpacity;
		}
		UMaterialExpressionScalarParameter* OpacityValue = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupOpacity, 10, DefaultOpacity, PBRARMLayout.InputX, OpacityY);
		UMaterialExpressionTextureSampleParameter2D* OpacityTex = AddTextureParameter(Material, FPBRMaterialParameters::OpacityTexture, GroupOpacity, 40, PBRARMLayout.InputX, OpacityY + 180, EMaterialSamplerType::SAMPLERTYPE_Masks);
		OpacityTex->Coordinates.Connect(0, OpacityUV);
		UMaterialExpressionComponentMask* OpacityMask = PBRARMAddTextureMask(Material, OpacityTex, true, false, false, false, PBRARMLayout.MaskX, OpacityY + 180);
		UMaterialExpressionMultiply* OpacityTextureMultiply = AddMultiply(Material, PBRARMLayout.FunctionX, OpacityY + 160);
		OpacityTextureMultiply->A.Connect(0, OpacityValue);
		OpacityTextureMultiply->B.Connect(0, OpacityMask);
		UseOpacityTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseOpacityTexture, GroupOpacity, 50, false, PBRARMLayout.SwitchX, OpacityY + 120);
		UseOpacityTexture->A.Connect(0, OpacityTextureMultiply);
		UseOpacityTexture->B.Connect(0, OpacityValue);
		OpacityMaterialOutput = UseOpacityTexture;
	}

	UMaterialExpression* EmissiveMaterialOutput = nullptr;
	if (bUseEmissiveGraph)
	{
		UMaterialExpressionVectorParameter* EmissiveColor = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupEmissive, 10, FLinearColor::Black, PBRARMLayout.InputX, EmissiveY);
		UMaterialExpressionScalarParameter* EmissiveIntensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupEmissive, 20, 0.0f, PBRARMLayout.InputX, EmissiveY + 120);
		AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseEmissiveTemperature, GroupEmissive, 30, false, PBRARMLayout.InputX, EmissiveY + 240);
		AddScalarParameter(Material, FPBRMaterialParameters::EmissiveTemperatureKelvin, GroupEmissive, 40, 6500.0f, PBRARMLayout.InputX, EmissiveY + 360);
		UMaterialExpressionTextureSampleParameter2D* EmissiveTex = AddTextureParameter(Material, FPBRMaterialParameters::EmissiveTexture, GroupEmissive, 50, PBRARMLayout.InputX, EmissiveY + 480);
		EmissiveTex->Coordinates.Connect(0, EmissiveUV);
		UMaterialExpressionMultiply* EmissiveColorIntensity = AddMultiply(Material, PBRARMLayout.FunctionX, EmissiveY + 60);
		EmissiveColorIntensity->A.Connect(0, EmissiveColor);
		EmissiveColorIntensity->B.Connect(0, EmissiveIntensity);
		UMaterialExpressionMultiply* EmissiveTextureIntensity = AddMultiply(Material, PBRARMLayout.FunctionX, EmissiveY + 420);
		EmissiveTextureIntensity->A.Connect(0, EmissiveTex);
		EmissiveTextureIntensity->B.Connect(0, EmissiveIntensity);
		UMaterialExpressionStaticSwitchParameter* UseEmissiveTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseEmissiveTexture, GroupEmissive, 60, false, PBRARMLayout.SwitchX, EmissiveY + 240);
		UseEmissiveTexture->A.Connect(0, EmissiveTextureIntensity);
		UseEmissiveTexture->B.Connect(0, EmissiveColorIntensity);
		EmissiveMaterialOutput = UseEmissiveTexture;
	}

	UMaterialExpression* DisplacementMaterialOutput = nullptr;
	if (bUseDisplacementGraph)
	{
		UMaterialExpressionScalarParameter* HeightStrength = AddScalarParameter(Material, FPBRMaterialParameters::HeightStrength, GroupSpecial, 20, 0.0f, PBRARMLayout.SpecialInputX, DisplacementY + 180);
		UMaterialExpressionTextureSampleParameter2D* HeightTex = AddTextureParameter(Material, FPBRMaterialParameters::HeightTexture, GroupSpecial, 21, PBRARMLayout.SpecialInputX, DisplacementY, EMaterialSamplerType::SAMPLERTYPE_Masks);
		HeightTex->Coordinates.Connect(0, HeightUV);
		UMaterialExpressionComponentMask* HeightMask = PBRARMAddTextureMask(Material, HeightTex, true, false, false, false, PBRARMLayout.SpecialFunctionX - 260, DisplacementY);
		UMaterialExpression* HeightOffset = nullptr;
		if (UMaterialExpressionMaterialFunctionCall* HeightOffsetFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/02_Surface/MF_PBRStudio_HeightOffset.MF_PBRStudio_HeightOffset"), PBRARMLayout.SpecialFunctionX, DisplacementY + 40))
		{
			const bool bConnected =
				ConnectFunctionInputByName(HeightOffsetFunction, TEXT("高度值"), HeightMask) &
				ConnectFunctionInputByName(HeightOffsetFunction, TEXT("强度"), HeightStrength);
			if (bConnected)
			{
				HeightOffset = HeightOffsetFunction;
			}
		}
		if (!HeightOffset)
		{
			HeightOffset = PBRARMAddCenteredHeightOffset(Material, HeightMask, HeightStrength, PBRARMLayout.SpecialFunctionX, DisplacementY + 40);
		}
		UMaterialExpressionConstant* ZeroHeightOffset = AddConstant(Material, 0.0f, PBRARMLayout.SpecialFunctionX, DisplacementY + 220);
		UMaterialExpressionStaticSwitchParameter* UseHeightTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseHeightTexture, GroupSpecial, 22, false, PBRARMLayout.SpecialResultX - 220, DisplacementY + 100);
		UseHeightTexture->A.Connect(0, HeightOffset);
		UseHeightTexture->B.Connect(0, ZeroHeightOffset);
		UMaterialExpressionVertexNormalWS* VertexNormalWS = NewObject<UMaterialExpressionVertexNormalWS>(Material);
		VertexNormalWS->MaterialExpressionEditorX = PBRARMLayout.SpecialResultX - 220;
		VertexNormalWS->MaterialExpressionEditorY = DisplacementY + 260;
		Material->GetExpressionCollection().AddExpression(VertexNormalWS);
		UMaterialExpressionMultiply* HeightWPO = AddMultiply(Material, PBRARMLayout.SpecialResultX, DisplacementY + 180);
		HeightWPO->A.Connect(0, VertexNormalWS);
		HeightWPO->B.Connect(0, UseHeightTexture);
		DisplacementMaterialOutput = HeightWPO;
	}

	UMaterialExpression* RefractionMaterialOutput = nullptr;
	int32 RefractionMaterialOutputIndex = 0;
	if (MaterialType == EPBRMaterialType::Fabric)
	{
		UMaterialExpressionVectorParameter* FabricFuzzColor = AddVectorParameter(Material, FPBRMaterialParameters::FabricFuzzColor, GroupSpecial, 60, FLinearColor::White, PBRARMLayout.SpecialInputX, SpecialY);
		UMaterialExpressionScalarParameter* FabricFuzzStrength = AddScalarParameter(Material, FPBRMaterialParameters::FabricFuzzStrength, GroupSpecial, 70, 0.0f, PBRARMLayout.SpecialInputX, SpecialY + 140);
		UMaterialExpressionFresnel* FuzzFresnel = PBRARMAddFresnel(Material, 3.0f, 0.02f, PBRARMLayout.SpecialFunctionX, SpecialY);
		UMaterialExpressionMultiply* FuzzMask = AddMultiply(Material, PBRARMLayout.SpecialResultX - 300, SpecialY + 40);
		FuzzMask->A.Connect(0, FuzzFresnel);
		FuzzMask->B.Connect(0, FabricFuzzStrength);
		UMaterialExpressionMultiply* FuzzColor = AddMultiply(Material, PBRARMLayout.SpecialResultX - 80, SpecialY + 40);
		FuzzColor->A.Connect(0, FabricFuzzColor);
		FuzzColor->B.Connect(0, FuzzMask);
		UMaterialExpressionAdd* FabricBase = AddAdd(Material, PBRARMLayout.SpecialResultX + 140, SpecialY);
		FabricBase->A.Connect(0, BaseColorOutput);
		FabricBase->B.Connect(0, FuzzColor);
		BaseColorOutput = FabricBase;
		EditorData->SubsurfaceColor.Connect(0, FabricFuzzColor);
	}
	else if (MaterialType == EPBRMaterialType::Leather)
	{
		UMaterialExpressionScalarParameter* ClearCoat = AddScalarParameter(Material, FPBRMaterialParameters::ClearCoat, GroupSpecial, 30, 0.0f, PBRARMLayout.SpecialInputX, SpecialY);
		UMaterialExpressionScalarParameter* ClearCoatRoughness = AddScalarParameter(Material, FPBRMaterialParameters::ClearCoatRoughness, GroupSpecial, 40, 0.25f, PBRARMLayout.SpecialInputX, SpecialY + 140);
		EditorData->ClearCoat.Connect(0, ClearCoat);
		EditorData->ClearCoatRoughness.Connect(0, ClearCoatRoughness);
	}
	else if (MaterialType == EPBRMaterialType::Metal)
	{
		UMaterialExpressionScalarParameter* Anisotropy = AddScalarParameter(Material, FPBRMaterialParameters::Anisotropy, GroupSpecial, 50, 0.0f, PBRARMLayout.SpecialInputX, SpecialY);
		UMaterialExpressionScalarParameter* FlakeScale = AddScalarParameter(Material, FPBRMaterialParameters::FlakeScale, GroupSpecial, 140, 35.0f, PBRARMLayout.SpecialInputX, SpecialY + 140);
		UMaterialExpressionScalarParameter* FlakeIntensity = AddScalarParameter(Material, FPBRMaterialParameters::FlakeIntensity, GroupSpecial, 150, 0.0f, PBRARMLayout.SpecialInputX, SpecialY + 260);
		EditorData->Anisotropy.Connect(0, Anisotropy);
		UMaterialExpressionCustom* FlakeMask = PBRARMAddFlakeMask(Material, MetallicUV, FlakeScale, PBRARMLayout.SpecialFunctionX, SpecialY + 60);
		UMaterialExpressionMultiply* FlakeAmount = AddMultiply(Material, PBRARMLayout.SpecialResultX - 300, SpecialY + 100);
		FlakeAmount->A.Connect(0, FlakeMask);
		FlakeAmount->B.Connect(0, FlakeIntensity);
		UMaterialExpressionConstant3Vector* FlakeColor = AddConstant3Vector(Material, FLinearColor::White, PBRARMLayout.SpecialResultX - 300, SpecialY + 240);
		UMaterialExpressionMultiply* FlakeTint = AddMultiply(Material, PBRARMLayout.SpecialResultX - 80, SpecialY + 120);
		FlakeTint->A.Connect(0, FlakeColor);
		FlakeTint->B.Connect(0, FlakeAmount);
		UMaterialExpressionAdd* MetalBase = AddAdd(Material, PBRARMLayout.SpecialResultX + 140, SpecialY + 80);
		MetalBase->A.Connect(0, BaseColorOutput);
		MetalBase->B.Connect(0, FlakeTint);
		BaseColorOutput = MetalBase;
	}
	else if (MaterialType == EPBRMaterialType::Water)
	{
		UMaterialExpressionScalarParameter* RefractionAmount = AddScalarParameter(Material, FPBRMaterialParameters::RefractionAmount, GroupSpecial, 80, 1.45f, PBRARMLayout.SpecialInputX, SpecialY);
		UMaterialExpressionVectorParameter* WaterColor = AddVectorParameter(Material, FPBRMaterialParameters::WaterColor, GroupSpecial, 90, FLinearColor(0.12f, 0.42f, 0.72f, 1.0f), PBRARMLayout.SpecialInputX, SpecialY + 120);
		UMaterialExpressionScalarParameter* WaterFlowSpeedU = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedU, GroupSpecial, 100, PBRARMDefaultWaterFlowSpeedU, PBRARMLayout.SpecialInputX, SpecialY + 240);
		UMaterialExpressionScalarParameter* WaterFlowSpeedV = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedV, GroupSpecial, 110, PBRARMDefaultWaterFlowSpeedV, PBRARMLayout.SpecialInputX, SpecialY + 360);
		UMaterialExpressionScalarParameter* WaterRippleScale = AddScalarParameter(Material, FPBRMaterialParameters::WaterRippleScale, GroupSpecial, 120, PBRARMDefaultWaterRippleScale, PBRARMLayout.SpecialInputX, SpecialY + 480);
		UMaterialExpressionScalarParameter* WaterRippleStrength = AddScalarParameter(Material, FPBRMaterialParameters::WaterRippleStrength, GroupSpecial, 130, PBRARMDefaultWaterRippleStrength, PBRARMLayout.SpecialInputX, SpecialY + 600);
		UMaterialExpressionLinearInterpolate* WaterTint = PBRARMAddLerp(Material, 0.45f, PBRARMLayout.SpecialFunctionX, SpecialY + 40);
		WaterTint->A.Connect(0, BaseColorOutput);
		WaterTint->B.Connect(0, WaterColor);
		UMaterialExpressionTime* WaterTime = NewObject<UMaterialExpressionTime>(Material);
		WaterTime->bIgnorePause = true;
		WaterTime->MaterialExpressionEditorX = PBRARMLayout.SpecialFunctionX;
		WaterTime->MaterialExpressionEditorY = SpecialY + 220;
		Material->GetExpressionCollection().AddExpression(WaterTime);
		UMaterialExpression* RippleMask = nullptr;
		if (UMaterialExpressionMaterialFunctionCall* RippleMaskFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/03_Water/MF_PBRStudio_WaterRippleMask.MF_PBRStudio_WaterRippleMask"), PBRARMLayout.SpecialFunctionX, SpecialY + 180))
		{
			const bool bConnected =
				ConnectFunctionInputByName(RippleMaskFunction, TEXT("UV"), BaseColorUV) &
				ConnectFunctionInputByName(RippleMaskFunction, TEXT("Time"), WaterTime) &
				ConnectFunctionInputByName(RippleMaskFunction, TEXT("U 速度"), WaterFlowSpeedU) &
				ConnectFunctionInputByName(RippleMaskFunction, TEXT("V 速度"), WaterFlowSpeedV) &
				ConnectFunctionInputByName(RippleMaskFunction, TEXT("缩放"), WaterRippleScale) &
				ConnectFunctionInputByName(RippleMaskFunction, TEXT("强度"), WaterRippleStrength);
			if (bConnected)
			{
				RippleMask = RippleMaskFunction;
			}
		}
		if (!RippleMask)
		{
			RippleMask = PBRARMAddWaterRippleMask(Material, BaseColorUV, WaterTime, WaterFlowSpeedU, WaterFlowSpeedV, WaterRippleScale, WaterRippleStrength, PBRARMLayout.SpecialFunctionX, SpecialY + 180);
		}
		UMaterialExpression* WaterFlowUV = nullptr;
		if (UMaterialExpressionMaterialFunctionCall* WaterFlowFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/03_Water/MF_PBRStudio_WaterFlowUV.MF_PBRStudio_WaterFlowUV"), PBRARMLayout.SpecialFunctionX, SpecialY + 460))
		{
			const bool bConnected =
				ConnectFunctionInputByName(WaterFlowFunction, TEXT("UV"), WaterRippleUV) &
				ConnectFunctionInputByName(WaterFlowFunction, TEXT("Time"), WaterTime) &
				ConnectFunctionInputByName(WaterFlowFunction, TEXT("U 速度"), WaterFlowSpeedU) &
				ConnectFunctionInputByName(WaterFlowFunction, TEXT("V 速度"), WaterFlowSpeedV) &
				ConnectFunctionInputByName(WaterFlowFunction, TEXT("缩放"), WaterRippleScale);
			if (bConnected)
			{
				WaterFlowUV = WaterFlowFunction;
			}
		}
		if (!WaterFlowUV)
		{
			WaterFlowUV = PBRARMAddWaterFlowUV(Material, WaterRippleUV, WaterTime, WaterFlowSpeedU, WaterFlowSpeedV, PBRARMLayout.SpecialFunctionX, SpecialY + 460);
		}
		UMaterialExpressionTextureSampleParameter2D* WaterRippleTex = AddTextureParameter(Material, FPBRMaterialParameters::WaterRippleTexture, GroupSpecial, 131, PBRARMLayout.SpecialResultX, SpecialY + 500, EMaterialSamplerType::SAMPLERTYPE_Masks);
		WaterRippleTex->Coordinates.Connect(0, WaterFlowUV);
		UMaterialExpressionComponentMask* WaterRippleTextureMask = PBRARMAddTextureMask(Material, WaterRippleTex, true, false, false, false, PBRARMLayout.SpecialResultX + 220, SpecialY + 500);
		UMaterialExpressionMultiply* WaterRippleTextureStrength = AddMultiply(Material, PBRARMLayout.SpecialResultX + 420, SpecialY + 500);
		WaterRippleTextureStrength->A.Connect(0, WaterRippleTextureMask);
		WaterRippleTextureStrength->B.Connect(0, WaterRippleStrength);
		UMaterialExpressionStaticSwitchParameter* UseWaterRippleTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseWaterRippleTexture, GroupSpecial, 132, true, PBRARMLayout.SpecialResultX + 620, SpecialY + 420);
		UseWaterRippleTexture->A.Connect(0, WaterRippleTextureStrength);
		UseWaterRippleTexture->B.Connect(0, RippleMask);
		UMaterialExpressionMultiply* RippleTint = AddMultiply(Material, PBRARMLayout.SpecialResultX + 220, SpecialY + 260);
		RippleTint->A.Connect(0, WaterColor);
		RippleTint->B.Connect(0, UseWaterRippleTexture);
		UMaterialExpressionAdd* WaterRippleBase = AddAdd(Material, PBRARMLayout.SpecialResultX + 420, SpecialY + 260);
		WaterRippleBase->A.Connect(0, WaterTint);
		WaterRippleBase->B.Connect(0, RippleTint);
		BaseColorOutput = WaterRippleBase;
		UMaterialExpression* RippleNormal = nullptr;
		if (UMaterialExpressionMaterialFunctionCall* RippleNormalFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/03_Water/MF_PBRStudio_WaterRippleNormal.MF_PBRStudio_WaterRippleNormal"), PBRARMLayout.SpecialFunctionX, SpecialY + 680))
		{
			const bool bConnected =
				ConnectFunctionInputByName(RippleNormalFunction, TEXT("UV"), NormalUV) &
				ConnectFunctionInputByName(RippleNormalFunction, TEXT("Time"), WaterTime) &
				ConnectFunctionInputByName(RippleNormalFunction, TEXT("U 速度"), WaterFlowSpeedU) &
				ConnectFunctionInputByName(RippleNormalFunction, TEXT("V 速度"), WaterFlowSpeedV) &
				ConnectFunctionInputByName(RippleNormalFunction, TEXT("缩放"), WaterRippleScale) &
				ConnectFunctionInputByName(RippleNormalFunction, TEXT("强度"), WaterRippleStrength);
			if (bConnected)
			{
				RippleNormal = RippleNormalFunction;
			}
		}
		if (!RippleNormal)
		{
			RippleNormal = PBRARMAddWaterRippleNormal(Material, NormalUV, WaterTime, WaterFlowSpeedU, WaterFlowSpeedV, WaterRippleScale, WaterRippleStrength, PBRARMLayout.SpecialFunctionX, SpecialY + 680);
		}
		NormalMaterialOutput = RippleNormal;
		RefractionMaterialOutput = RefractionAmount;
		RefractionMaterialOutputIndex = 0;
		EditorData->SurfaceThickness.Connect(0, WaterRippleStrength);
	}
	else if (MaterialType == EPBRMaterialType::Glass)
	{
		const FName GroupGlassColor(TEXT("00 - 颜色"));
		const FName GroupGlassOpacity(TEXT("01 - 透明"));
		const FName GroupGlassRefraction(TEXT("02 - 折射"));
		const FName GroupGlassReflection(TEXT("04 - 反射"));
		const FName GroupGlassDirt(TEXT("06 - 污渍"));
		const FName GroupGlassDistortion(TEXT("07 - 扭曲"));
		const FName GroupGlassFrosted(TEXT("08 - 磨砂"));
		const FName GroupGlassShadow(TEXT("09 - 阴影"));
		const FName GroupGlassRayTracing(TEXT("10 - 光线追踪"));

		UMaterialExpressionScalarParameter* RefractionAmount = AddScalarParameter(Material, FPBRMaterialParameters::RefractionAmount, GroupGlassRefraction, 80, 1.45f, PBRARMLayout.SpecialInputX, SpecialY);
		UMaterialExpressionScalarParameter* GlassOpacityFresnelStrength = AddScalarParameter(Material, FPBRMaterialParameters::GlassOpacityFresnelStrength, GroupGlassOpacity, 90, 0.35f, PBRARMLayout.SpecialInputX, SpecialY + 120);
		UMaterialExpressionScalarParameter* GlassFresnelBaseReflection = AddScalarParameter(Material, FPBRMaterialParameters::GlassFresnelBaseReflection, GroupGlassReflection, 100, 0.02f, PBRARMLayout.SpecialInputX, SpecialY + 240);
		UMaterialExpressionScalarParameter* GlassFresnelExp = AddScalarParameter(Material, FPBRMaterialParameters::GlassFresnelExp, GroupGlassReflection, 110, 5.0f, PBRARMLayout.SpecialInputX, SpecialY + 360);
		UMaterialExpressionScalarParameter* GlassFrostedStrength = AddScalarParameter(Material, FPBRMaterialParameters::GlassFrostedStrength, GroupGlassFrosted, 120, 0.0f, PBRARMLayout.SpecialInputX, SpecialY + 480);
		UMaterialExpressionVectorParameter* GlassAbsorptionColor = AddVectorParameter(Material, FPBRMaterialParameters::GlassAbsorptionColor, GroupGlassColor, 130, FLinearColor(0.78f, 0.92f, 1.0f, 1.0f), PBRARMLayout.SpecialInputX, SpecialY + 620);
		UMaterialExpressionScalarParameter* GlassAbsorptionStrength = AddScalarParameter(Material, FPBRMaterialParameters::GlassAbsorptionStrength, GroupGlassColor, 140, 0.15f, PBRARMLayout.SpecialInputX, SpecialY + 740);
		UMaterialExpressionScalarParameter* GlassEdgeTintStrength = AddScalarParameter(Material, FPBRMaterialParameters::GlassEdgeTintStrength, GroupGlassColor, 150, 0.25f, PBRARMLayout.SpecialInputX, SpecialY + 860);
		UMaterialExpressionScalarParameter* GlassShadowOpacity = AddScalarParameter(Material, FPBRMaterialParameters::GlassShadowOpacity, GroupGlassShadow, 160, 0.55f, PBRARMLayout.SpecialInputX, SpecialY + 980);
		UMaterialExpressionScalarParameter* GlassCausticsIntensity = AddScalarParameter(Material, FPBRMaterialParameters::GlassCausticsIntensity, GroupGlassShadow, 170, 0.0f, PBRARMLayout.SpecialFunctionX - 120, SpecialY + 720);
		UMaterialExpressionScalarParameter* GlassCausticsScale = AddScalarParameter(Material, FPBRMaterialParameters::GlassCausticsScale, GroupGlassShadow, 180, 24.0f, PBRARMLayout.SpecialFunctionX - 120, SpecialY + 840);
		UMaterialExpressionScalarParameter* GlassCausticsSpeed = AddScalarParameter(Material, FPBRMaterialParameters::GlassCausticsSpeed, GroupGlassShadow, 190, 0.12f, PBRARMLayout.SpecialFunctionX - 120, SpecialY + 960);

		UMaterialExpressionVectorParameter* GlassDirtColor = AddVectorParameter(Material, FPBRMaterialParameters::GlassDirtColor, GroupGlassDirt, 200, FLinearColor(0.35f, 0.32f, 0.26f, 1.0f), PBRARMLayout.SpecialInputX, SpecialY + 1120);
		UMaterialExpressionTextureSampleParameter2D* GlassDirtTex = AddTextureParameter(Material, FPBRMaterialParameters::GlassDirtTexture, GroupGlassDirt, 205, PBRARMLayout.SpecialResultX - 740, SpecialY + 1560, EMaterialSamplerType::SAMPLERTYPE_Masks);
		GlassDirtTex->Coordinates.Connect(0, BaseColorUV);
		UMaterialExpressionComponentMask* GlassDirtMask = PBRARMAddTextureMask(Material, GlassDirtTex, true, false, false, false, PBRARMLayout.SpecialResultX - 520, SpecialY + 1560);
		UMaterialExpressionStaticSwitchParameter* UseGlassDirtTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseGlassDirtTexture, GroupGlassDirt, 206, false, PBRARMLayout.SpecialResultX - 520, SpecialY + 1560);
		UMaterialExpressionConstant* FullGlassDirtMask = AddConstant(Material, 1.0f, PBRARMLayout.SpecialResultX - 520, SpecialY + 1700);
		UseGlassDirtTexture->A.Connect(0, GlassDirtMask);
		UseGlassDirtTexture->B.Connect(0, FullGlassDirtMask);
		UMaterialExpressionScalarParameter* GlassDirtIntensity = AddScalarParameter(Material, FPBRMaterialParameters::GlassDirtIntensity, GroupGlassDirt, 210, 0.0f, PBRARMLayout.SpecialInputX, SpecialY + 1260);
		UMaterialExpressionScalarParameter* GlassDirtOpacity = AddScalarParameter(Material, FPBRMaterialParameters::GlassDirtOpacity, GroupGlassDirt, 220, 0.35f, PBRARMLayout.SpecialInputX, SpecialY + 1380);
		UMaterialExpressionScalarParameter* GlassDirtRoughness = AddScalarParameter(Material, FPBRMaterialParameters::GlassDirtRoughness, GroupGlassDirt, 230, 0.65f, PBRARMLayout.SpecialInputX, SpecialY + 1500);

		UMaterialExpressionTextureSampleParameter2D* GlassDistortionTex = AddTextureParameter(Material, FPBRMaterialParameters::GlassDistortionTexture, GroupGlassDistortion, 235, PBRARMLayout.SpecialFunctionX - 340, SpecialY + 1120, EMaterialSamplerType::SAMPLERTYPE_Masks);
		GlassDistortionTex->Coordinates.Connect(0, BaseColorUV);
		UMaterialExpressionComponentMask* GlassDistortionMask = PBRARMAddTextureMask(Material, GlassDistortionTex, true, false, false, false, PBRARMLayout.SpecialFunctionX - 120, SpecialY + 1120);
		UMaterialExpressionStaticSwitchParameter* UseGlassDistortionTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseGlassDistortionTexture, GroupGlassDistortion, 236, false, PBRARMLayout.SpecialResultX - 80, SpecialY + 1120);
		UMaterialExpressionConstant* FullGlassDistortionMask = AddConstant(Material, 1.0f, PBRARMLayout.SpecialFunctionX + 120, SpecialY + 1240);
		UseGlassDistortionTexture->A.Connect(0, GlassDistortionMask);
		UseGlassDistortionTexture->B.Connect(0, FullGlassDistortionMask);
		UMaterialExpressionScalarParameter* GlassDistortionIntensity = AddScalarParameter(Material, FPBRMaterialParameters::GlassDistortionIntensity, GroupGlassDistortion, 240, 0.0f, PBRARMLayout.SpecialFunctionX - 120, SpecialY + 1120);
		UMaterialExpressionScalarParameter* GlassDistortionIORIntensity = AddScalarParameter(Material, FPBRMaterialParameters::GlassDistortionIORIntensity, GroupGlassDistortion, 250, 0.0f, PBRARMLayout.SpecialFunctionX - 120, SpecialY + 1240);

		UMaterialExpressionTextureSampleParameter2D* GlassFrostedTex = AddTextureParameter(Material, FPBRMaterialParameters::GlassFrostedTexture, GroupGlassFrosted, 255, PBRARMLayout.SpecialFunctionX + 100, SpecialY + 1400, EMaterialSamplerType::SAMPLERTYPE_Masks);
		GlassFrostedTex->Coordinates.Connect(0, BaseColorUV);
		UMaterialExpressionComponentMask* GlassFrostedMask = PBRARMAddTextureMask(Material, GlassFrostedTex, true, false, false, false, PBRARMLayout.SpecialFunctionX + 320, SpecialY + 1400);
		UMaterialExpressionStaticSwitchParameter* UseGlassFrostedTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseGlassFrostedTexture, GroupGlassFrosted, 256, false, PBRARMLayout.SpecialResultX - 80, SpecialY + 1400);
		UMaterialExpressionConstant* FullGlassFrostedMask = AddConstant(Material, 1.0f, PBRARMLayout.SpecialFunctionX + 320, SpecialY + 1540);
		UseGlassFrostedTexture->A.Connect(0, GlassFrostedMask);
		UseGlassFrostedTexture->B.Connect(0, FullGlassFrostedMask);

		UMaterialExpressionScalarParameter* GlassShadowHighlightClamp = AddScalarParameter(Material, FPBRMaterialParameters::GlassShadowHighlightClamp, GroupGlassShadow, 260, 0.85f, PBRARMLayout.SpecialFunctionX - 120, SpecialY + 1360);
		UMaterialExpressionScalarParameter* GlassShadowNormalIntensity = AddScalarParameter(Material, FPBRMaterialParameters::GlassShadowNormalIntensity, GroupGlassShadow, 270, 0.25f, PBRARMLayout.SpecialFunctionX - 120, SpecialY + 1480);
		UMaterialExpressionScalarParameter* GlassRTOpacity = AddScalarParameter(Material, FPBRMaterialParameters::GlassRTOpacity, GroupGlassRayTracing, 280, 0.35f, PBRARMLayout.SpecialResultX - 360, SpecialY + 1120);
		UMaterialExpressionScalarParameter* GlassRTRefractionAmount = AddScalarParameter(Material, FPBRMaterialParameters::GlassRTRefractionAmount, GroupGlassRayTracing, 290, 1.45f, PBRARMLayout.SpecialResultX - 360, SpecialY + 1240);
		UMaterialExpressionScalarParameter* GlassRTFrostedStrength = AddScalarParameter(Material, FPBRMaterialParameters::GlassRTFrostedStrength, GroupGlassRayTracing, 300, 0.0f, PBRARMLayout.SpecialResultX - 360, SpecialY + 1360);

		UMaterialExpressionFresnel* GlassEdgeFresnel = PBRARMAddFresnel(Material, 4.0f, 0.0f, PBRARMLayout.SpecialFunctionX, SpecialY + 520);
		UMaterialExpressionMultiply* GlassEdgeTintMask = AddMultiply(Material, PBRARMLayout.SpecialResultX - 520, SpecialY + 560);
		GlassEdgeTintMask->A.Connect(0, GlassEdgeFresnel);
		GlassEdgeTintMask->B.Connect(0, GlassEdgeTintStrength);
		UMaterialExpressionMultiply* GlassAbsorptionAmount = AddMultiply(Material, PBRARMLayout.SpecialResultX - 520, SpecialY + 700);
		GlassAbsorptionAmount->A.Connect(0, GlassAbsorptionColor);
		GlassAbsorptionAmount->B.Connect(0, GlassAbsorptionStrength);
		UMaterialExpressionMultiply* GlassEdgeTint = AddMultiply(Material, PBRARMLayout.SpecialResultX - 300, SpecialY + 620);
		GlassEdgeTint->A.Connect(0, GlassAbsorptionColor);
		GlassEdgeTint->B.Connect(0, GlassEdgeTintMask);
		UMaterialExpressionAdd* GlassTint = AddAdd(Material, PBRARMLayout.SpecialResultX - 80, SpecialY + 640);
		GlassTint->A.Connect(0, GlassAbsorptionAmount);
		GlassTint->B.Connect(0, GlassEdgeTint);
		UMaterialExpressionMultiply* GlassDirtAmount = AddMultiply(Material, PBRARMLayout.SpecialResultX - 300, SpecialY + 1560);
		GlassDirtAmount->A.Connect(0, UseGlassDirtTexture);
		GlassDirtAmount->B.Connect(0, GlassDirtIntensity);
		UMaterialExpressionMultiply* GlassDirtTint = AddMultiply(Material, PBRARMLayout.SpecialResultX - 300, SpecialY + 1120);
		GlassDirtTint->A.Connect(0, GlassDirtColor);
		GlassDirtTint->B.Connect(0, GlassDirtAmount);
		UMaterialExpressionAdd* GlassTintWithDirt = AddAdd(Material, PBRARMLayout.SpecialResultX - 80, SpecialY + 1120);
		GlassTintWithDirt->A.Connect(0, GlassTint);
		GlassTintWithDirt->B.Connect(0, GlassDirtTint);
		UMaterialExpressionAdd* GlassTintedBase = AddAdd(Material, PBRARMLayout.SpecialResultX + 140, SpecialY + 640);
		GlassTintedBase->A.Connect(0, BaseColorOutput);
		GlassTintedBase->B.Connect(0, GlassTintWithDirt);
		BaseColorOutput = GlassTintedBase;

		UMaterialExpressionMultiply* GlassDirtRoughnessAmount = AddMultiply(Material, PBRARMLayout.SpecialResultX - 300, SpecialY + 1280);
		GlassDirtRoughnessAmount->A.Connect(0, GlassDirtAmount);
		GlassDirtRoughnessAmount->B.Connect(0, GlassDirtRoughness);
		UMaterialExpressionAdd* GlassRoughnessWithDirt = AddAdd(Material, PBRARMLayout.SpecialResultX - 80, SpecialY + 1280);
		GlassRoughnessWithDirt->A.Connect(0, RoughnessMaterialOutput);
		GlassRoughnessWithDirt->B.Connect(0, GlassDirtRoughnessAmount);
		UMaterialExpressionSaturate* GlassRoughnessSaturated = NewObject<UMaterialExpressionSaturate>(Material);
		GlassRoughnessSaturated->MaterialExpressionEditorX = PBRARMLayout.SpecialResultX + 140;
		GlassRoughnessSaturated->MaterialExpressionEditorY = SpecialY + 1280;
		GlassRoughnessSaturated->Input.Connect(0, GlassRoughnessWithDirt);
		Material->GetExpressionCollection().AddExpression(GlassRoughnessSaturated);
		RoughnessMaterialOutput = GlassRoughnessSaturated;

		UMaterialExpressionMultiply* GlassDistortionScalarAmount = AddMultiply(Material, PBRARMLayout.SpecialResultX + 120, SpecialY + 1120);
		GlassDistortionScalarAmount->A.Connect(0, GlassDistortionIntensity);
		GlassDistortionScalarAmount->B.Connect(0, GlassDistortionIORIntensity);
		UMaterialExpressionMultiply* GlassDistortionAmount = AddMultiply(Material, PBRARMLayout.SpecialResultX + 340, SpecialY + 1120);
		GlassDistortionAmount->A.Connect(0, GlassDistortionScalarAmount);
		GlassDistortionAmount->B.Connect(0, UseGlassDistortionTexture);
		UMaterialExpressionAdd* GlassRefractionWithDistortion = AddAdd(Material, PBRARMLayout.SpecialResultX + 140, SpecialY + 1460);
		GlassRefractionWithDistortion->A.Connect(0, RefractionAmount);
		GlassRefractionWithDistortion->B.Connect(0, GlassDistortionAmount);
		UMaterialExpressionMultiply* GlassFrostedAmount = AddMultiply(Material, PBRARMLayout.SpecialResultX + 120, SpecialY + 1400);
		GlassFrostedAmount->A.Connect(0, GlassFrostedStrength);
		GlassFrostedAmount->B.Connect(0, UseGlassFrostedTexture);

		UMaterialExpressionTime* GlassTime = NewObject<UMaterialExpressionTime>(Material);
		GlassTime->bIgnorePause = true;
		GlassTime->MaterialExpressionEditorX = PBRARMLayout.SpecialFunctionX;
		GlassTime->MaterialExpressionEditorY = SpecialY + 820;
		Material->GetExpressionCollection().AddExpression(GlassTime);
		UMaterialExpressionCustom* GlassCausticsMask = PBRARMAddGlassCausticsMask(Material, BaseColorUV, GlassTime, GlassCausticsScale, GlassCausticsSpeed, GlassCausticsIntensity, PBRARMLayout.SpecialResultX - 300, SpecialY + 860);
		UMaterialExpressionMultiply* GlassCausticsColor = AddMultiply(Material, PBRARMLayout.SpecialResultX - 80, SpecialY + 860);
		GlassCausticsColor->A.Connect(0, GlassAbsorptionColor);
		GlassCausticsColor->B.Connect(0, GlassCausticsMask);
		if (EmissiveMaterialOutput)
		{
			UMaterialExpressionAdd* GlassEmissiveWithCaustics = AddAdd(Material, PBRARMLayout.SpecialResultX + 140, SpecialY + 860);
			GlassEmissiveWithCaustics->A.Connect(0, EmissiveMaterialOutput);
			GlassEmissiveWithCaustics->B.Connect(0, GlassCausticsColor);
			EmissiveMaterialOutput = GlassEmissiveWithCaustics;
		}
		else
		{
			EmissiveMaterialOutput = GlassCausticsColor;
		}

		if (UMaterialExpressionMaterialFunctionCall* GlassOpticsFunction = AddMaterialFunctionCall(Material, TEXT("/Game/PBRStudio/Functions/04_Glass/MF_PBRStudio_GlassOptics.MF_PBRStudio_GlassOptics"), PBRARMLayout.SpecialFunctionX, SpecialY + 180))
		{
			const bool bConnected =
				ConnectFunctionInputByName(GlassOpticsFunction, TEXT("透明度"), UseOpacityTexture) &
				ConnectFunctionInputByName(GlassOpticsFunction, TEXT("玻璃折射率"), RefractionAmount) &
				ConnectFunctionInputByName(GlassOpticsFunction, TEXT("透明菲涅尔强度"), GlassOpacityFresnelStrength) &
				ConnectFunctionInputByName(GlassOpticsFunction, TEXT("菲涅尔基础反射"), GlassFresnelBaseReflection) &
				ConnectFunctionInputByName(GlassOpticsFunction, TEXT("菲涅尔指数"), GlassFresnelExp) &
				ConnectFunctionInputByName(GlassOpticsFunction, TEXT("毛玻璃强度"), GlassFrostedAmount);
			if (bConnected)
			{
				UMaterialExpressionAdd* FrostedRoughness = AddAdd(Material, PBRARMLayout.SpecialResultX - 260, SpecialY + 180);
				FrostedRoughness->A.Connect(0, RoughnessMaterialOutput);
				FrostedRoughness->B.Connect(2, GlassOpticsFunction);
				UMaterialExpressionSaturate* FrostedRoughnessOutput = NewObject<UMaterialExpressionSaturate>(Material);
				FrostedRoughnessOutput->MaterialExpressionEditorX = PBRARMLayout.SpecialResultX;
				FrostedRoughnessOutput->MaterialExpressionEditorY = SpecialY + 180;
				FrostedRoughnessOutput->Input.Connect(0, FrostedRoughness);
				Material->GetExpressionCollection().AddExpression(FrostedRoughnessOutput);
				RoughnessMaterialOutput = FrostedRoughnessOutput;

				UMaterialExpressionShadowReplace* GlassShadowOpacitySwitch = NewObject<UMaterialExpressionShadowReplace>(Material);
				GlassShadowOpacitySwitch->MaterialExpressionEditorX = PBRARMLayout.SpecialResultX;
				GlassShadowOpacitySwitch->MaterialExpressionEditorY = SpecialY + 420;
				GlassShadowOpacitySwitch->Default.Connect(0, GlassOpticsFunction);
				UMaterialExpressionMultiply* ShadowNormalBoost = AddMultiply(Material, PBRARMLayout.SpecialResultX - 300, SpecialY + 360);
				ShadowNormalBoost->A.Connect(0, GlassShadowOpacity);
				ShadowNormalBoost->B.Connect(0, GlassShadowNormalIntensity);
				UMaterialExpressionAdd* ShadowOpacityWithNormal = AddAdd(Material, PBRARMLayout.SpecialResultX - 80, SpecialY + 360);
				ShadowOpacityWithNormal->A.Connect(0, GlassShadowOpacity);
				ShadowOpacityWithNormal->B.Connect(0, ShadowNormalBoost);
				UMaterialExpressionMultiply* ShadowOpacityClamped = AddMultiply(Material, PBRARMLayout.SpecialResultX + 120, SpecialY + 360);
				ShadowOpacityClamped->A.Connect(0, ShadowOpacityWithNormal);
				ShadowOpacityClamped->B.Connect(0, GlassShadowHighlightClamp);
				GlassShadowOpacitySwitch->Shadow.Connect(0, ShadowOpacityClamped);
				Material->GetExpressionCollection().AddExpression(GlassShadowOpacitySwitch);

				UMaterialExpressionMultiply* DirtOpacityAmount = AddMultiply(Material, PBRARMLayout.SpecialResultX + 120, SpecialY + 520);
				DirtOpacityAmount->A.Connect(0, GlassDirtAmount);
				DirtOpacityAmount->B.Connect(0, GlassDirtOpacity);
				UMaterialExpressionAdd* GlassOpacityWithDirt = AddAdd(Material, PBRARMLayout.SpecialResultX + 340, SpecialY + 520);
				GlassOpacityWithDirt->A.Connect(0, GlassShadowOpacitySwitch);
				GlassOpacityWithDirt->B.Connect(0, DirtOpacityAmount);
				UMaterialExpressionSaturate* GlassOpacitySaturated = NewObject<UMaterialExpressionSaturate>(Material);
				GlassOpacitySaturated->MaterialExpressionEditorX = PBRARMLayout.SpecialResultX + 560;
				GlassOpacitySaturated->MaterialExpressionEditorY = SpecialY + 520;
				GlassOpacitySaturated->Input.Connect(0, GlassOpacityWithDirt);
				Material->GetExpressionCollection().AddExpression(GlassOpacitySaturated);

				UMaterialExpressionRayTracingQualitySwitch* RTOpacitySwitch = NewObject<UMaterialExpressionRayTracingQualitySwitch>(Material);
				RTOpacitySwitch->MaterialExpressionEditorX = PBRARMLayout.SpecialResultX + 780;
				RTOpacitySwitch->MaterialExpressionEditorY = SpecialY + 520;
				RTOpacitySwitch->Normal.Connect(0, GlassOpacitySaturated);
				RTOpacitySwitch->RayTraced.Connect(0, GlassRTOpacity);
				Material->GetExpressionCollection().AddExpression(RTOpacitySwitch);

				UMaterialExpressionRayTracingQualitySwitch* RTRefractionSwitch = NewObject<UMaterialExpressionRayTracingQualitySwitch>(Material);
				RTRefractionSwitch->MaterialExpressionEditorX = PBRARMLayout.SpecialResultX + 520;
				RTRefractionSwitch->MaterialExpressionEditorY = SpecialY + 1460;
				RTRefractionSwitch->Normal.Connect(0, GlassRefractionWithDistortion);
				RTRefractionSwitch->RayTraced.Connect(0, GlassRTRefractionAmount);
				Material->GetExpressionCollection().AddExpression(RTRefractionSwitch);

				UMaterialExpressionAdd* RTRoughnessWithFrost = AddAdd(Material, PBRARMLayout.SpecialResultX + 360, SpecialY + 1280);
				RTRoughnessWithFrost->A.Connect(0, RoughnessMaterialOutput);
				RTRoughnessWithFrost->B.Connect(0, GlassRTFrostedStrength);
				UMaterialExpressionRayTracingQualitySwitch* RTRoughnessSwitch = NewObject<UMaterialExpressionRayTracingQualitySwitch>(Material);
				RTRoughnessSwitch->MaterialExpressionEditorX = PBRARMLayout.SpecialResultX + 580;
				RTRoughnessSwitch->MaterialExpressionEditorY = SpecialY + 1280;
				RTRoughnessSwitch->Normal.Connect(0, RoughnessMaterialOutput);
				RTRoughnessSwitch->RayTraced.Connect(0, RTRoughnessWithFrost);
				Material->GetExpressionCollection().AddExpression(RTRoughnessSwitch);

				OpacityMaterialOutput = RTOpacitySwitch;
				RefractionMaterialOutput = RTRefractionSwitch;
				RoughnessMaterialOutput = RTRoughnessSwitch;
			}
		}
		if (!RefractionMaterialOutput)
		{
			RefractionMaterialOutput = GlassRefractionWithDistortion;
		}
		RefractionMaterialOutputIndex = 0;
	}
	else if (MaterialType == EPBRMaterialType::Transparent)
	{
		UMaterialExpressionScalarParameter* RefractionAmount = AddScalarParameter(Material, FPBRMaterialParameters::RefractionAmount, GroupSpecial, 80, 1.45f, PBRARMLayout.SpecialInputX, SpecialY);
		RefractionMaterialOutput = RefractionAmount;
		RefractionMaterialOutputIndex = 0;
	}

	UMaterialExpressionNamedRerouteDeclaration* BaseColorDeclaration = PBRARMAddNamedRerouteDeclaration(Material, TEXT("Base Color"), BaseColorOutput, 0, FLinearColor(0.45f, 0.20f, 0.85f, 1.0f), PBRARMLayout.SwitchX + 260, BaseY + 160);
	UMaterialExpressionNamedRerouteDeclaration* MetallicDeclaration = PBRARMAddNamedRerouteDeclaration(Material, TEXT("Metallic"), MetallicMaterialOutput, 0, FLinearColor(0.05f, 0.50f, 0.38f, 1.0f), PBRARMLayout.SwitchX + 260, MetallicY + 80);
	UMaterialExpressionNamedRerouteDeclaration* SpecularDeclaration = PBRARMAddNamedRerouteDeclaration(Material, TEXT("Specular"), SpecularMaterialOutput, 0, FLinearColor(0.40f, 0.50f, 0.12f, 1.0f), PBRARMLayout.SwitchX + 260, SpecularY + 80);
	UMaterialExpressionNamedRerouteDeclaration* RoughnessDeclaration = PBRARMAddNamedRerouteDeclaration(Material, TEXT("Roughness"), RoughnessMaterialOutput, 0, FLinearColor(0.45f, 0.50f, 0.12f, 1.0f), PBRARMLayout.SwitchX + 260, RoughnessY + 80);
	UMaterialExpressionNamedRerouteDeclaration* NormalDeclaration = PBRARMAddNamedRerouteDeclaration(Material, TEXT("Normal"), NormalMaterialOutput, 0, FLinearColor(0.10f, 0.48f, 0.40f, 1.0f), PBRARMLayout.SwitchX + 260, NormalY + 160);
	UMaterialExpressionNamedRerouteDeclaration* AODeclaration = PBRARMAddNamedRerouteDeclaration(Material, TEXT("AO"), AOMaterialOutput, 0, FLinearColor(0.50f, 0.28f, 0.08f, 1.0f), PBRARMLayout.SwitchX + 260, AOY + 60);
	UMaterialExpressionNamedRerouteDeclaration* DisplacementDeclaration = DisplacementMaterialOutput
		? PBRARMAddNamedRerouteDeclaration(Material, TEXT("Displacement"), DisplacementMaterialOutput, 0, FLinearColor(0.10f, 0.48f, 0.40f, 1.0f), PBRARMLayout.SpecialResultX + 160, DisplacementY + 160)
		: nullptr;
	UMaterialExpressionNamedRerouteDeclaration* OpacityDeclaration = OpacityMaterialOutput
		? PBRARMAddNamedRerouteDeclaration(Material, TEXT("Opacity"), OpacityMaterialOutput, 0, FLinearColor(0.10f, 0.48f, 0.40f, 1.0f), PBRARMLayout.SwitchX + 260, OpacityY + 120)
		: nullptr;
	UMaterialExpressionNamedRerouteDeclaration* EmissiveDeclaration = EmissiveMaterialOutput
		? PBRARMAddNamedRerouteDeclaration(Material, TEXT("Emissive"), EmissiveMaterialOutput, 0, FLinearColor(0.82f, 0.42f, 0.06f, 1.0f), PBRARMLayout.SwitchX + 260, EmissiveY + 240)
		: nullptr;
	UMaterialExpressionNamedRerouteDeclaration* RefractionDeclaration = RefractionMaterialOutput
		? PBRARMAddNamedRerouteDeclaration(Material, TEXT("Refraction"), RefractionMaterialOutput, RefractionMaterialOutputIndex, FLinearColor(0.10f, 0.42f, 0.70f, 1.0f), PBRARMLayout.SpecialResultX + 520, SpecialY + 360)
		: nullptr;

	EditorData->BaseColor.Connect(0, PBRARMAddNamedRerouteUsage(Material, BaseColorDeclaration, PBRARMLayout.OutputUseX, OutputY));
	EditorData->Metallic.Connect(0, PBRARMAddNamedRerouteUsage(Material, MetallicDeclaration, PBRARMLayout.OutputUseX, OutputY + 110));
	EditorData->Specular.Connect(0, PBRARMAddNamedRerouteUsage(Material, SpecularDeclaration, PBRARMLayout.OutputUseX, OutputY + 220));
	EditorData->Roughness.Connect(0, PBRARMAddNamedRerouteUsage(Material, RoughnessDeclaration, PBRARMLayout.OutputUseX, OutputY + 330));
	EditorData->Normal.Connect(0, PBRARMAddNamedRerouteUsage(Material, NormalDeclaration, PBRARMLayout.OutputUseX, OutputY + 440));
	EditorData->AmbientOcclusion.Connect(0, PBRARMAddNamedRerouteUsage(Material, AODeclaration, PBRARMLayout.OutputUseX, OutputY + 550));
	if (DisplacementDeclaration)
	{
		EditorData->WorldPositionOffset.Connect(0, PBRARMAddNamedRerouteUsage(Material, DisplacementDeclaration, PBRARMLayout.OutputUseX, OutputY + 660));
	}
	if (OpacityDeclaration)
	{
		EditorData->Opacity.Connect(0, PBRARMAddNamedRerouteUsage(Material, OpacityDeclaration, PBRARMLayout.OutputUseX, OutputY + 770));
	}
	if (EmissiveDeclaration)
	{
		EditorData->EmissiveColor.Connect(0, PBRARMAddNamedRerouteUsage(Material, EmissiveDeclaration, PBRARMLayout.OutputUseX, OutputY + 880));
	}
	if (RefractionDeclaration)
	{
		EditorData->Refraction.Connect(0, PBRARMAddNamedRerouteUsage(Material, RefractionDeclaration, PBRARMLayout.OutputUseX, OutputY + 990));
	}

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	OutMessage = TEXT("已按 ARM Parent Graph Layout v8 构建母材质图表");
	return true;
}

static bool BuildTemplateGraph(UMaterial* Material, EPBRMaterialType MaterialType, FString& OutMessage)
{
	return BuildARMStyleTemplateGraph(Material, MaterialType, OutMessage);
}
static void LayoutMaterialGraphByColumns(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	TMap<int32, TArray<UMaterialExpression*>> Columns;
	for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
	{
		if (!Expression || Cast<UMaterialExpressionComment>(Expression))
		{
			continue;
		}
		const int32 Column = FMath::RoundToInt(static_cast<float>(Expression->MaterialExpressionEditorX) / 420.0f);
		Columns.FindOrAdd(Column).Add(Expression);
	}

	TArray<int32> Keys;
	Columns.GetKeys(Keys);
	Keys.Sort();
	for (const int32 Column : Keys)
	{
		TArray<UMaterialExpression*>& Expressions = Columns[Column];
		Expressions.Sort([](const UMaterialExpression& A, const UMaterialExpression& B)
		{
			return A.MaterialExpressionEditorY == B.MaterialExpressionEditorY
				? A.GetClass()->GetName() < B.GetClass()->GetName()
				: A.MaterialExpressionEditorY < B.MaterialExpressionEditorY;
		});

		const int32 X = Column * 520;
		int32 Y = -900;
		for (UMaterialExpression* Expression : Expressions)
		{
			if (!Expression || Cast<UMaterialExpressionComment>(Expression))
			{
				continue;
			}
			Expression->MaterialExpressionEditorX = X;
			Expression->MaterialExpressionEditorY = Y;
			Y += 230;
		}
	}
}

static void AddReachableExpression(UMaterialExpression* Expression, TSet<UMaterialExpression*>& Reachable)
{
	if (!Expression || Reachable.Contains(Expression))
	{
		return;
	}

	Reachable.Add(Expression);
	for (FExpressionInputIterator It(Expression); It; ++It)
	{
		AddReachableExpression(It.Input ? It.Input->Expression : nullptr, Reachable);
	}
}

static void AddConnectedExpression(FExpressionInput* Input, TSet<UMaterialExpression*>& Reachable)
{
	if (!Input)
	{
		return;
	}

	AddReachableExpression(Input->Expression, Reachable);
}

static int32 RemoveUnusedMaterialExpressions(UMaterial* Material)
{
	if (!Material)
	{
		return 0;
	}

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		return 0;
	}

	TSet<UMaterialExpression*> Reachable;
	AddConnectedExpression(&EditorData->BaseColor, Reachable);
	AddConnectedExpression(&EditorData->Metallic, Reachable);
	AddConnectedExpression(&EditorData->Specular, Reachable);
	AddConnectedExpression(&EditorData->Roughness, Reachable);
	AddConnectedExpression(&EditorData->Normal, Reachable);
	AddConnectedExpression(&EditorData->EmissiveColor, Reachable);
	AddConnectedExpression(&EditorData->Opacity, Reachable);
	AddConnectedExpression(&EditorData->OpacityMask, Reachable);
	AddConnectedExpression(&EditorData->AmbientOcclusion, Reachable);
	AddConnectedExpression(&EditorData->Refraction, Reachable);
	AddConnectedExpression(&EditorData->WorldPositionOffset, Reachable);
	AddConnectedExpression(&EditorData->PixelDepthOffset, Reachable);
	AddConnectedExpression(&EditorData->ClearCoat, Reachable);
	AddConnectedExpression(&EditorData->ClearCoatRoughness, Reachable);
	AddConnectedExpression(&EditorData->Anisotropy, Reachable);
	AddConnectedExpression(&EditorData->SubsurfaceColor, Reachable);
	AddConnectedExpression(&EditorData->MaterialAttributes, Reachable);
	for (FVector2MaterialInput& CustomizedUV : EditorData->CustomizedUVs)
	{
		AddConnectedExpression(&CustomizedUV, Reachable);
	}

	TArray<UMaterialExpression*> Expressions = Material->GetExpressionCollection().Expressions;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (Cast<UMaterialExpressionSingleLayerWaterMaterialOutput>(Expression) ||
			Cast<UMaterialExpressionThinTranslucentMaterialOutput>(Expression) ||
			Cast<UMaterialExpressionRuntimeVirtualTextureOutput>(Expression))
		{
			AddReachableExpression(Expression, Reachable);
		}
	}

	int32 Removed = 0;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (!Expression || Cast<UMaterialExpressionComment>(Expression) || Reachable.Contains(Expression))
		{
			continue;
		}

		Material->GetExpressionCollection().RemoveExpression(Expression);
		++Removed;
	}
	return Removed;
}

static FName InferPBRGraphGroupFromInput(const FExpressionInput& Input, const TSet<UMaterialExpression*>& GroupExpressions, const FName& GroupName)
{
	if (Input.Expression && GroupExpressions.Contains(Input.Expression))
	{
		return GroupName;
	}
	return NAME_None;
}

static bool IsPBRGraphInputConnectedToGroup(const FExpressionInput& Input, const TSet<UMaterialExpression*>& GroupExpressions)
{
	return Input.Expression && GroupExpressions.Contains(Input.Expression);
}

static FName InferPBRGraphGroupFromConnections(UMaterialExpression* Expression, const UMaterialEditorOnlyData* EditorData)
{
	if (!Expression || !EditorData)
	{
		return NAME_None;
	}

	TSet<UMaterialExpression*> Reachable;
	AddReachableExpression(Expression, Reachable);
	if (Reachable.Num() == 0)
	{
		return NAME_None;
	}

	if (const FName GroupName = InferPBRGraphGroupFromInput(EditorData->BaseColor, Reachable, TEXT("01 基础颜色")); !GroupName.IsNone())
	{
		return GroupName;
	}
	if (IsPBRGraphInputConnectedToGroup(EditorData->ClearCoat, Reachable) ||
		IsPBRGraphInputConnectedToGroup(EditorData->ClearCoatRoughness, Reachable))
	{
		return TEXT("10 清漆");
	}
	if (const FName GroupName = InferPBRGraphGroupFromInput(EditorData->Normal, Reachable, TEXT("02 法线")); !GroupName.IsNone())
	{
		return GroupName;
	}
	if (const FName GroupName = InferPBRGraphGroupFromInput(EditorData->Roughness, Reachable, TEXT("03 粗糙度")); !GroupName.IsNone())
	{
		return GroupName;
	}
	if (IsPBRGraphInputConnectedToGroup(EditorData->Metallic, Reachable) ||
		IsPBRGraphInputConnectedToGroup(EditorData->AmbientOcclusion, Reachable) ||
		IsPBRGraphInputConnectedToGroup(EditorData->Specular, Reachable))
	{
		return TEXT("04 金属和环境遮蔽");
	}
	if (IsPBRGraphInputConnectedToGroup(EditorData->Opacity, Reachable) ||
		IsPBRGraphInputConnectedToGroup(EditorData->OpacityMask, Reachable) ||
		IsPBRGraphInputConnectedToGroup(EditorData->Refraction, Reachable))
	{
		return TEXT("05 透明");
	}
	if (const FName GroupName = InferPBRGraphGroupFromInput(EditorData->EmissiveColor, Reachable, TEXT("06 自发光")); !GroupName.IsNone())
	{
		return GroupName;
	}
	if (IsPBRGraphInputConnectedToGroup(EditorData->WorldPositionOffset, Reachable) ||
		IsPBRGraphInputConnectedToGroup(EditorData->PixelDepthOffset, Reachable))
	{
		return TEXT("09 高度和置换");
	}
	if (const FName GroupName = InferPBRGraphGroupFromInput(EditorData->SubsurfaceColor, Reachable, TEXT("07 类型专用")); !GroupName.IsNone())
	{
		return GroupName;
	}
	return NAME_None;
}

static bool IsPBRGraphMainInputExpression(const UMaterialExpression* Expression, const UMaterialEditorOnlyData* EditorData)
{
	if (!Expression || !EditorData)
	{
		return false;
	}

	return EditorData->BaseColor.Expression == Expression ||
		EditorData->Metallic.Expression == Expression ||
		EditorData->Specular.Expression == Expression ||
		EditorData->Roughness.Expression == Expression ||
		EditorData->Normal.Expression == Expression ||
		EditorData->EmissiveColor.Expression == Expression ||
		EditorData->Opacity.Expression == Expression ||
		EditorData->OpacityMask.Expression == Expression ||
		EditorData->AmbientOcclusion.Expression == Expression ||
		EditorData->Refraction.Expression == Expression ||
		EditorData->WorldPositionOffset.Expression == Expression ||
		EditorData->PixelDepthOffset.Expression == Expression ||
		EditorData->ClearCoat.Expression == Expression ||
		EditorData->ClearCoatRoughness.Expression == Expression ||
		EditorData->Anisotropy.Expression == Expression ||
		EditorData->SubsurfaceColor.Expression == Expression ||
		EditorData->MaterialAttributes.Expression == Expression;
}

static bool MoveExpressionToGroup(
	UMaterialExpression* Expression,
	TMap<FName, TMap<int32, int32>>& GroupColumnCounters,
	const TMap<FName, FIntPoint>& GroupOrigins,
	const UMaterialEditorOnlyData* EditorData)
{
	if (!Expression)
	{
		return false;
	}

	FName GroupName(NAME_None);
	int32 Column = 0;
	int32 SortPriority = 0;
	if (UMaterialExpressionTextureSampleParameter2D* Texture = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
	{
		GroupName = Texture->Group;
		SortPriority = Texture->SortPriority;
		Column = 0;
	}
	else if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
	{
		GroupName = Scalar->Group;
		SortPriority = Scalar->SortPriority;
		Column = 0;
	}
	else if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		GroupName = Vector->Group;
		SortPriority = Vector->SortPriority;
		Column = 0;
	}
	else if (UMaterialExpressionStaticSwitchParameter* Switch = Cast<UMaterialExpressionStaticSwitchParameter>(Expression))
	{
		GroupName = Switch->Group;
		SortPriority = Switch->SortPriority;
		Column = 3;
	}
	else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
	{
		const FString FunctionName = FunctionCall->MaterialFunction ? FunctionCall->MaterialFunction->GetName() : FString();
		if (FunctionName.Contains(TEXT("UVControls")))
		{
			GroupName = TEXT("08 UV 调整");
			SortPriority = 10;
			Column = 2;
		}
		else if (FunctionName.Contains(TEXT("NormalStrength")))
		{
			GroupName = TEXT("02 法线");
			SortPriority = 20;
			Column = 2;
		}
		else if (FunctionName.Contains(TEXT("BaseColorBlend")))
		{
			GroupName = TEXT("01 基础颜色");
			SortPriority = 30;
			Column = 2;
		}
		else if (FunctionName.Contains(TEXT("ScalarTextureSwitch")))
		{
			const int32 Y = Expression->MaterialExpressionEditorY;
			if (Y < 420)
			{
				GroupName = TEXT("03 粗糙度");
			}
			else if (Y < 760)
			{
				GroupName = TEXT("05 透明");
			}
			else
			{
				GroupName = TEXT("04 金属和环境遮蔽");
			}
			SortPriority = Y;
			Column = 2;
		}
		else if (FunctionName.Contains(TEXT("HeightDisplacement")))
		{
			GroupName = TEXT("09 高度和置换");
			SortPriority = 30;
			Column = 2;
		}
		else if (FunctionName.Contains(TEXT("DynamicPanner")))
		{
			GroupName = TEXT("07 类型专用");
			SortPriority = 30;
			Column = 2;
		}
	}
	else if (Cast<UMaterialExpressionTextureCoordinate>(Expression))
	{
		GroupName = TEXT("08 UV 调整");
		SortPriority = 0;
		Column = 0;
	}
	else if (Cast<UMaterialExpressionRotator>(Expression) ||
		Cast<UMaterialExpressionPanner>(Expression) ||
		Cast<UMaterialExpressionAppendVector>(Expression))
	{
		const int32 Y = Expression->MaterialExpressionEditorY;
		if (Y < -100)
		{
			GroupName = TEXT("08 UV 调整");
		}
		else
		{
			GroupName = TEXT("07 类型专用");
		}
		SortPriority = Y;
		Column = 1;
	}
	else if (Cast<UMaterialExpressionMultiply>(Expression) ||
		Cast<UMaterialExpressionAdd>(Expression) ||
		Cast<UMaterialExpressionMax>(Expression) ||
		Cast<UMaterialExpressionComponentMask>(Expression) ||
		Cast<UMaterialExpressionFresnel>(Expression) ||
		Cast<UMaterialExpressionVertexNormalWS>(Expression) ||
		Cast<UMaterialExpressionConstant3Vector>(Expression) ||
		Cast<UMaterialExpressionConstant>(Expression) ||
		Cast<UMaterialExpressionDivide>(Expression) ||
		Cast<UMaterialExpressionOneMinus>(Expression))
	{
		const int32 Y = Expression->MaterialExpressionEditorY;
		if (Y < -380)
		{
			GroupName = TEXT("01 基础颜色");
		}
		else if (Y < 180)
		{
			GroupName = TEXT("02 法线");
		}
		else if (Y < 500)
		{
			GroupName = TEXT("03 粗糙度");
		}
		else if (Y < 900)
		{
			if (Expression->MaterialExpressionEditorX > 240)
			{
				GroupName = TEXT("10 清漆");
			}
			else if (Expression->MaterialExpressionEditorX > -120)
			{
				GroupName = TEXT("05 透明");
			}
			else
			{
				GroupName = TEXT("04 金属和环境遮蔽");
			}
		}
		else if (Y < 1180)
		{
			GroupName = TEXT("06 自发光");
		}
		else if (Y < 1550)
		{
			GroupName = TEXT("09 高度和置换");
		}
		else
		{
			GroupName = TEXT("07 类型专用");
		}
		SortPriority = Y;
		Column = 1;
	}
	else if (Cast<UMaterialExpressionSingleLayerWaterMaterialOutput>(Expression) ||
		Cast<UMaterialExpressionThinTranslucentMaterialOutput>(Expression) ||
		Cast<UMaterialExpressionRuntimeVirtualTextureOutput>(Expression))
	{
		GroupName = TEXT("07 类型专用");
		SortPriority = 90;
		Column = 3;
	}

	if (const FName ConnectedGroupName = InferPBRGraphGroupFromConnections(Expression, EditorData); !ConnectedGroupName.IsNone())
	{
		if (!GroupOrigins.Contains(GroupName) || Column == 1)
		{
			GroupName = ConnectedGroupName;
		}
	}

	const FIntPoint* Origin = GroupOrigins.Find(GroupName);
	if (!Origin)
	{
		const FName ConnectedGroupName = InferPBRGraphGroupFromConnections(Expression, EditorData);
		Origin = GroupOrigins.Find(ConnectedGroupName);
		if (!Origin)
		{
			return false;
		}
		GroupName = ConnectedGroupName;
		if (Column == 0)
		{
			Column = 1;
		}
	}

	TMap<int32, int32>& ColumnCounters = GroupColumnCounters.FindOrAdd(GroupName);
	int32& Counter = ColumnCounters.FindOrAdd(Column);
	const int32 Row = Counter++;
	const int32 ColumnWidth = Column == 3 ? 360 : 300;
	Expression->MaterialExpressionEditorX = Origin->X + Column * ColumnWidth;
	Expression->MaterialExpressionEditorY = Origin->Y + Row * 170 + FMath::Clamp(SortPriority, 0, 80) / 5;
	if (IsPBRGraphMainInputExpression(Expression, EditorData))
	{
		Expression->MaterialExpressionEditorX += 90;
	}
	return true;
}

static void LayoutPBRTemplateGraphByGroups(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	const UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	const TMap<FName, FIntPoint> GroupOrigins = {
		{ TEXT("08 UV 调整"), FIntPoint(-2200, -920) },
		{ TEXT("01 基础颜色"), FIntPoint(-1040, -920) },
		{ TEXT("02 法线"), FIntPoint(-1040, -40) },
		{ TEXT("03 粗糙度"), FIntPoint(-1040, 640) },
		{ TEXT("04 金属和环境遮蔽"), FIntPoint(-1040, 1360) },
		{ TEXT("05 透明"), FIntPoint(420, -40) },
		{ TEXT("06 自发光"), FIntPoint(420, 720) },
		{ TEXT("07 类型专用"), FIntPoint(1780, -40) },
		{ TEXT("09 高度和置换"), FIntPoint(420, 1600) },
		{ TEXT("10 清漆"), FIntPoint(1780, 720) }
	};

	TMap<FName, TMap<int32, int32>> GroupColumnCounters;
	TArray<UMaterialExpression*> LooseExpressions;
	for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
	{
		if (!Expression || Cast<UMaterialExpressionComment>(Expression))
		{
			continue;
		}

		if (!MoveExpressionToGroup(Expression, GroupColumnCounters, GroupOrigins, EditorData))
		{
			LooseExpressions.Add(Expression);
		}
	}

	LooseExpressions.Sort([](const UMaterialExpression& A, const UMaterialExpression& B)
	{
		return A.MaterialExpressionEditorY == B.MaterialExpressionEditorY
			? A.MaterialExpressionEditorX < B.MaterialExpressionEditorX
			: A.MaterialExpressionEditorY < B.MaterialExpressionEditorY;
	});

	int32 LooseIndex = 0;
	for (UMaterialExpression* Expression : LooseExpressions)
	{
		if (!Expression)
		{
			continue;
		}

		Expression->MaterialExpressionEditorX = 3280;
		Expression->MaterialExpressionEditorY = -920 + LooseIndex * 170;
		++LooseIndex;
	}

	Material->EditorX = 3300;
	Material->EditorY = -920;
}

void FPBRMaterialTemplateManager::SaveMaterial(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	TArray<UPackage*> PackagesToSave = { Material->GetPackage() };
	UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
}
