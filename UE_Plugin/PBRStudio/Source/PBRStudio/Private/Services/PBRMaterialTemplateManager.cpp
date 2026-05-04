#include "Services/PBRMaterialTemplateManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/TextureFactory.h"
#include "FileHelpers.h"
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
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionRotator.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVectorParameter.h"
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
	if (UTexture2D* Existing = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(AssetPath)))
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
static bool BuildSpecialMaterialGraph(UMaterial* Material, const FString& AssetName, FString& OutMessage);

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

static UMaterialExpressionMaterialFunctionCall* AddMaterialFunctionCall(UMaterial* Material, const TCHAR* FunctionPath, int32 X, int32 Y)
{
	UMaterialFunctionInterface* Function = LoadObject<UMaterialFunctionInterface>(nullptr, FunctionPath);
	if (!Function)
	{
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

	UMaterialExpressionDivide* DegreesToTurns = AddDivide(Material, -940, -520, 360.0f);
	DegreesToTurns->A.Connect(0, OutUVRotationDegrees);

	UMaterialExpressionRotator* Rotator = NewObject<UMaterialExpressionRotator>(Material);
	Rotator->CenterX = 0.5f;
	Rotator->CenterY = 0.5f;
	Rotator->Speed = 1.0f;
	Rotator->Coordinate.Connect(0, OffsetAdd);
	Rotator->Time.Connect(0, DegreesToTurns);
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

	UMaterialExpressionScalarParameter* FlowU = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedU, GroupSpecial, 40, 0.12f, X, Y);
	UMaterialExpressionScalarParameter* FlowV = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedV, GroupSpecial, 50, 0.06f, X, Y + 120);
	UMaterialExpressionScalarParameter* RippleScale = AddScalarParameter(Material, FPBRMaterialParameters::WaterRippleScale, GroupSpecial, 60, 1.0f, X, Y + 240);
	UMaterialExpressionScalarParameter* RippleStrength = AddScalarParameter(Material, FPBRMaterialParameters::WaterRippleStrength, GroupSpecial, 70, 0.5f, X, Y + 360);

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

	UMaterialExpressionTextureSampleParameter2D* SecondaryNormal = AddTextureParameter(
		Material,
		FPBRMaterialParameters::NormalTexture,
		GroupSpecial,
		80,
		X + 700,
		Y + 220,
		EMaterialSamplerType::SAMPLERTYPE_Normal);
	SecondaryNormal->Coordinates.Connect(0, PannerB);

	UMaterialExpressionMultiply* SecondaryStrength = AddMultiply(Material, X + 980, Y + 220);
	SecondaryStrength->A.Connect(0, SecondaryNormal);
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

	UMaterialExpressionScalarParameter* FlowU = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedU, GroupSpecial, 80, 0.12f, X, Y);
	UMaterialExpressionScalarParameter* FlowV = AddScalarParameter(Material, FPBRMaterialParameters::WaterFlowSpeedV, GroupSpecial, 90, 0.06f, X, Y + 120);
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
		MaterialType == EPBRMaterialType::Metal ||
		MaterialType == EPBRMaterialType::Plastic;
}

static bool UsesAO(EPBRMaterialType MaterialType)
{
	return MaterialType != EPBRMaterialType::Emissive;
}

static bool UsesOpacity(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Standard || IsTranslucentMaterialType(MaterialType);
}

static bool UsesEmissive(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Standard ||
		MaterialType == EPBRMaterialType::Emissive;
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
	return MaterialType != EPBRMaterialType::Emissive &&
		MaterialType != EPBRMaterialType::Glass &&
		MaterialType != EPBRMaterialType::Water &&
		MaterialType != EPBRMaterialType::Transparent;
}

static bool UsesClearCoat(EPBRMaterialType MaterialType)
{
	return MaterialType == EPBRMaterialType::Standard ||
		MaterialType == EPBRMaterialType::Leather ||
		MaterialType == EPBRMaterialType::Plastic ||
		MaterialType == EPBRMaterialType::Wood ||
		MaterialType == EPBRMaterialType::Metal;
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
		return 0.55f;
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
		return 1.333f;
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

static UMaterial* EnsureSpecialMaterialAsset(const FString& AssetName, FString& OutMessage)
{
	const FString PackagePath = TEXT("/Game/PBRStudio/SpecialMaterials/") + AssetName;
	if (UObject* Existing = UEditorAssetLibrary::LoadAsset(PackagePath))
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
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(PackagePath));
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
	return TEXT("/Game/PBRStudio/Templates/") + GetTemplateAssetName(MaterialType);
}

UMaterial* FPBRMaterialTemplateManager::EnsureTemplateMaterial(EPBRMaterialType MaterialType, FString& OutMessage)
{
	const FString FullPath = GetTemplatePackagePath(MaterialType);
	if (UObject* Existing = UEditorAssetLibrary::LoadAsset(FullPath))
	{
		if (UMaterial* ExistingMaterial = Cast<UMaterial>(Existing))
		{
			ResetMaterialGraph(ExistingMaterial);
			BuildTemplateGraph(ExistingMaterial, MaterialType, OutMessage);
			SaveMaterial(ExistingMaterial);
			OutMessage = TEXT("已重建母材质图表并清理多余节点");
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
		return CreateStandardTemplate(FullPath, AssetName, MaterialType, OutMessage);
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
		{ TEXT("SM_DistanceField_Blend") }
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

UMaterialInstanceConstant* FPBRMaterialTemplateManager::EnsureExampleMaterialInstance(EPBRMaterialType MaterialType, FString& OutMessage)
{
	FString ParentMessage;
	UMaterial* ParentMaterial = EnsureTemplateMaterial(MaterialType, ParentMessage);
	if (!ParentMaterial)
	{
		OutMessage = TEXT("示例实例失败: 母材质不可用");
		return nullptr;
	}

	const FString PackagePath = TEXT("/Game/PBRStudio/Templates/Examples/MI_示例_") + GetTemplateChineseName(MaterialType);
	const FString AssetName = FPaths::GetBaseFilename(PackagePath);
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(PackagePath));
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
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::WaterColor, FLinearColor(0.04f, 0.35f, 0.55f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedU, 0.12f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedV, 0.06f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleScale, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleStrength, 0.75f);
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

static bool BuildSpecialWaterGraph(UMaterial* Material, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;

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
	UMaterialExpressionScalarParameter* Refraction = AddScalarParameter(Material, FPBRMaterialParameters::RefractionAmount, GroupOptics, 20, 1.333f, -900, 260);
	UMaterialExpressionTextureSampleParameter2D* NormalTex = AddTextureParameterForType(Material, EPBRMaterialType::Water, FPBRMaterialParameters::NormalTexture, GroupNormal, 10, -900, 440, EMaterialSamplerType::SAMPLERTYPE_Normal);
	UMaterialExpressionScalarParameter* NormalStrength = AddScalarParameter(Material, FPBRMaterialParameters::NormalStrength, GroupNormal, 20, 0.65f, -620, 480);

	EditorData->BaseColor.Connect(0, WaterColor);
	EditorData->Roughness.Connect(0, Roughness);
	EditorData->Specular.Connect(0, Specular);
	EditorData->Opacity.Connect(0, Opacity);
	EditorData->Refraction.Connect(0, Refraction);
	BuildDynamicWaterNormal(Material, NormalTex, NormalStrength, SharedUV, GroupNormal, -360, 620);
	ConnectTextureSamplesToUV(Material, SharedUV);
	OutMessage = TEXT("Built independent dynamic water material");
	return true;
}

static bool BuildSpecialGlassGraph(UMaterial* Material, bool bDroplets, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;

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

	OutMessage = bDroplets ? TEXT("Built independent glass droplets material") : TEXT("Built independent clear glass material");
	return true;
}

static bool BuildSpecialTransparentGraph(UMaterial* Material, FString& OutMessage)
{
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = BLEND_Translucent;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("Special material editor data unavailable");
		return false;
	}

	const FName GroupTransparent(TEXT("01 半透明"));
	AddSpecialBaseGroups(Material, { { GroupTransparent, 10 } });
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
	AddSpecialBaseGroups(Material, { { GroupCoat, 10 } });
	EditorData->BaseColor.Connect(0, AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupCoat, 10, FLinearColor(0.02f, 0.025f, 0.03f), -700, -300));
	EditorData->Roughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupCoat, 20, 0.18f, -700, -160));
	EditorData->Specular.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupCoat, 30, 0.65f, -700, -20));
	EditorData->ClearCoat.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::ClearCoat, GroupCoat, 40, 1.0f, -700, 120));
	EditorData->ClearCoatRoughness.Connect(0, AddScalarParameter(Material, FPBRMaterialParameters::ClearCoatRoughness, GroupCoat, 50, 0.04f, -700, 260));
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
	AddSpecialBaseGroups(Material, { { GroupMetal, 10 } });
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
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupDecal, 20, FLinearColor::White, -900, -100);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupDecal, 30, 0.75f, -900, 60);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -220);
	Tinted->A.Connect(0, DecalTex);
	Tinted->B.Connect(0, Tint);
	EditorData->BaseColor.Connect(0, Tinted);
	EditorData->Opacity.Connect(0, Opacity);
	ConnectTextureSamplesToUV(Material, SharedUV);
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
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupUI, 20, FLinearColor::White, -900, -100);
	UMaterialExpressionScalarParameter* Opacity = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupUI, 30, 1.0f, -900, 60);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -220);
	Tinted->A.Connect(0, UITex);
	Tinted->B.Connect(0, Tint);
	EditorData->EmissiveColor.Connect(0, Tinted);
	EditorData->Opacity.Connect(0, Opacity);
	ConnectTextureSamplesToUV(Material, SharedUV);
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
	AddSpecialBaseGroups(Material, { { GroupParticle, 10 } });
	UMaterialExpressionParticleColor* ParticleColor = NewObject<UMaterialExpressionParticleColor>(Material);
	ParticleColor->MaterialExpressionEditorX = -900;
	ParticleColor->MaterialExpressionEditorY = -160;
	Material->GetExpressionCollection().AddExpression(ParticleColor);
	UMaterialExpressionVectorParameter* Tint = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupParticle, 10, FLinearColor(1.0f, 0.45f, 0.12f), -900, 40);
	UMaterialExpressionScalarParameter* Intensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupParticle, 20, 3.0f, -900, 200);
	UMaterialExpressionMultiply* Tinted = AddMultiply(Material, -620, -100);
	Tinted->A.Connect(0, ParticleColor);
	Tinted->B.Connect(0, Tint);
	UMaterialExpressionMultiply* Emissive = AddMultiply(Material, -360, -100);
	Emissive->A.Connect(0, Tinted);
	Emissive->B.Connect(0, Intensity);
	EditorData->EmissiveColor.Connect(0, Emissive);
	EditorData->Opacity.Connect(4, ParticleColor);
	OutMessage = TEXT("Built Niagara particle material");
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
	OutMessage = TEXT("Built Distance Field blend material");
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
	else
	{
		bBuilt = BuildSpecialUIGraphV2(Material, OutMessage);
	}

	if (bBuilt)
	{
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
	}
	return bBuilt;
}

static bool BuildTemplateGraph(UMaterial* Material, EPBRMaterialType MaterialType, FString& OutMessage)
{
	if (!Material)
	{
		OutMessage = TEXT("母材质为空");
		return false;
	}

	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->BlendMode = IsTranslucentMaterialType(MaterialType) ? BLEND_Translucent : BLEND_Opaque;
	Material->bEnableTessellation = UsesHeight(MaterialType);
	Material->bEnableDisplacementFade = UsesHeight(MaterialType);
	Material->DisplacementScaling.Magnitude = UsesHeight(MaterialType) ? 1.0f : 0.0f;
	Material->DisplacementScaling.Center = 0.5f;
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("无法访问材质编辑数据");
		return false;
	}

	const FName GroupBaseColor(TEXT("01 基础颜色"));
	const FName GroupNormal(TEXT("02 法线"));
	const FName GroupRoughness(TEXT("03 粗糙度"));
	const FName GroupMetalAO(TEXT("04 金属和环境遮蔽"));
	const FName GroupOpacity(TEXT("05 透明"));
	const FName GroupEmissive(TEXT("06 自发光"));
	const FName GroupSpecial(TEXT("07 类型专用"));
	const FName GroupUV(TEXT("08 UV 调整"));

	const FName GroupHeight(TEXT("09 高度和置换"));
	const FName GroupClearCoat(TEXT("10 清漆"));

	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupBaseColor.ToString(), 10));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupNormal.ToString(), 20));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupRoughness.ToString(), 30));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupMetalAO.ToString(), 40));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupOpacity.ToString(), 50));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupEmissive.ToString(), 60));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupSpecial.ToString(), 70));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupUV.ToString(), 80));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupHeight.ToString(), 90));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupClearCoat.ToString(), 100));

	UMaterialExpressionScalarParameter* UVRotationDegrees = nullptr;
	UMaterialExpression* SharedUV = BuildSharedUVControls(Material, UVRotationDegrees, GroupUV);

	UMaterialExpressionTextureSampleParameter2D* BaseTex = AddTextureParameter(Material, FPBRMaterialParameters::BaseColorTexture, GroupBaseColor, 10, -900, -350);
	if (MaterialType == EPBRMaterialType::Water)
	{
		BaseTex->Coordinates.Connect(0, BuildWaterFlowUV(Material, SharedUV, GroupSpecial, -250, 1080));
	}
	UMaterialExpressionVectorParameter* BaseTint = AddVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupBaseColor, 20, FLinearColor::White, -900, -180);
	UMaterialExpressionScalarParameter* BaseIntensity = AddScalarParameter(Material, FPBRMaterialParameters::BaseColorIntensity, GroupBaseColor, 30, 1.0f, -900, -40);
	UMaterialExpressionMultiply* BaseTintMultiply = AddMultiply(Material, -620, -280);
	UMaterialExpressionMultiply* BaseIntensityMultiply = AddMultiply(Material, -380, -280);
	UMaterialExpressionMultiply* BaseSolidMultiply = AddMultiply(Material, -380, -120);
	UMaterialExpressionStaticSwitchParameter* UseBaseColorTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseBaseColorTexture, GroupBaseColor, 40, true, -120, -280);
	BaseTintMultiply->A.Connect(0, BaseTex);
	BaseTintMultiply->B.Connect(0, BaseTint);
	BaseIntensityMultiply->A.Connect(0, BaseTintMultiply);
	BaseIntensityMultiply->B.Connect(0, BaseIntensity);
	BaseSolidMultiply->A.Connect(0, BaseTint);
	BaseSolidMultiply->B.Connect(0, BaseIntensity);
	UseBaseColorTexture->A.Connect(0, BaseIntensityMultiply);
	UseBaseColorTexture->B.Connect(0, BaseSolidMultiply);
	EditorData->BaseColor.Connect(0, UseBaseColorTexture);

	if (UsesNormal(MaterialType))
	{
		UMaterialExpressionTextureSampleParameter2D* NormalTex = AddTextureParameter(
			Material,
			FPBRMaterialParameters::NormalTexture,
			GroupNormal,
			10,
			-900,
			20,
			EMaterialSamplerType::SAMPLERTYPE_Normal);
		UMaterialExpressionScalarParameter* NormalStrength = AddScalarParameter(Material, FPBRMaterialParameters::NormalStrength, GroupNormal, 20, 1.0f, -620, 60);
		if (MaterialType == EPBRMaterialType::Water)
		{
			BuildDynamicWaterNormal(Material, NormalTex, NormalStrength, SharedUV, GroupSpecial, -250, 1540);
		}
		else if (!WireNormalStrength(Material, NormalTex, NormalStrength, -380, 20))
		{
			EditorData->Normal.Connect(0, NormalTex);
		}
	}

	if (UsesRoughness(MaterialType))
	{
		UMaterialExpressionTextureSampleParameter2D* RoughnessTex = AddTextureParameter(
			Material,
			FPBRMaterialParameters::RoughnessTexture,
			GroupRoughness,
			10,
			-900,
			220,
			EMaterialSamplerType::SAMPLERTYPE_Masks);
		const float DefaultRoughness = GetTemplateDefaultRoughness(MaterialType);
		UMaterialExpressionScalarParameter* RoughnessMul = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessMultiplier, GroupRoughness, 20, DefaultRoughness, -900, 360);
		UMaterialExpressionScalarParameter* RoughnessValue = AddScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupRoughness, 30, DefaultRoughness, -900, 500);
		UMaterialExpressionStaticSwitchParameter* UseRoughnessTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseRoughnessTexture, GroupRoughness, 40, true, -380, 260);
		UMaterialExpressionMultiply* RoughnessMultiply = AddMultiply(Material, -620, 260);
		RoughnessMultiply->A.Connect(0, RoughnessTex);
		RoughnessMultiply->B.Connect(0, RoughnessMul);
		UseRoughnessTexture->A.Connect(0, RoughnessMultiply);
		UseRoughnessTexture->B.Connect(0, RoughnessValue);
		EditorData->Roughness.Connect(0, UseRoughnessTexture);
	}

	if (UsesMetallic(MaterialType))
	{
		UMaterialExpressionTextureSampleParameter2D* MetallicTex = AddTextureParameter(
			Material,
			FPBRMaterialParameters::MetallicTexture,
			GroupMetalAO,
			10,
			-900,
			500,
			EMaterialSamplerType::SAMPLERTYPE_Masks);
		UMaterialExpressionScalarParameter* MetallicMul = AddScalarParameter(Material, FPBRMaterialParameters::MetallicMultiplier, GroupMetalAO, 20, GetTemplateDefaultMetallic(MaterialType), -900, 640);
		UMaterialExpressionScalarParameter* MetallicValue = AddScalarParameter(Material, FPBRMaterialParameters::MetallicValue, GroupMetalAO, 30, GetTemplateDefaultMetallic(MaterialType), -900, 700);
		UMaterialExpressionStaticSwitchParameter* UseMetallicTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseMetallicTexture, GroupMetalAO, 40, true, -380, 540);
		UMaterialExpressionMultiply* MetallicMultiply = AddMultiply(Material, -620, 540);
		MetallicMultiply->A.Connect(0, MetallicTex);
		MetallicMultiply->B.Connect(0, MetallicMul);
		UseMetallicTexture->A.Connect(0, MetallicMultiply);
		UseMetallicTexture->B.Connect(0, MetallicValue);
		EditorData->Metallic.Connect(0, UseMetallicTexture);
	}

	if (UsesAO(MaterialType))
	{
		UMaterialExpressionTextureSampleParameter2D* AOTex = AddTextureParameter(
			Material,
			FPBRMaterialParameters::AOTexture,
			GroupMetalAO,
			30,
			-900,
			780,
			EMaterialSamplerType::SAMPLERTYPE_Masks);
		UMaterialExpressionScalarParameter* AOMul = AddScalarParameter(Material, FPBRMaterialParameters::AOMultiplier, GroupMetalAO, 40, 1.0f, -900, 920);
		UMaterialExpressionScalarParameter* AOValue = AddScalarParameter(Material, FPBRMaterialParameters::AOValue, GroupMetalAO, 50, 1.0f, -900, 1060);
		UMaterialExpressionStaticSwitchParameter* UseAOTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseAOTexture, GroupMetalAO, 60, true, -380, 820);
		UMaterialExpressionMultiply* AOMultiply = AddMultiply(Material, -620, 820);
		AOMultiply->A.Connect(0, AOTex);
		AOMultiply->B.Connect(0, AOMul);
		UseAOTexture->A.Connect(0, AOMultiply);
		UseAOTexture->B.Connect(0, AOValue);
		EditorData->AmbientOcclusion.Connect(0, UseAOTexture);
	}

	if (UsesSpecular(MaterialType))
	{
		UMaterialExpressionTextureSampleParameter2D* SpecularTex = AddTextureParameter(Material, FPBRMaterialParameters::SpecularTexture, GroupMetalAO, 50, -900, 1040, EMaterialSamplerType::SAMPLERTYPE_Masks);
		UMaterialExpressionScalarParameter* SpecularLevel = AddScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupMetalAO, 60, 0.5f, -900, 1180);
		UMaterialExpressionStaticSwitchParameter* UseSpecularTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseSpecularTexture, GroupMetalAO, 70, true, -380, 1080);
		UMaterialExpressionMultiply* SpecularMultiply = AddMultiply(Material, -620, 1080);
		SpecularMultiply->A.Connect(0, SpecularTex);
		SpecularMultiply->B.Connect(0, SpecularLevel);
		UseSpecularTexture->A.Connect(0, SpecularMultiply);
		UseSpecularTexture->B.Connect(0, SpecularLevel);
		EditorData->Specular.Connect(0, UseSpecularTexture);
	}

	if (UsesOpacity(MaterialType))
	{
		UMaterialExpressionTextureSampleParameter2D* OpacityTex = AddTextureParameter(Material, FPBRMaterialParameters::OpacityTexture, GroupOpacity, 10, -250, 420, EMaterialSamplerType::SAMPLERTYPE_Masks);
		const float DefaultOpacity = GetTemplateDefaultOpacity(MaterialType);
		UMaterialExpressionScalarParameter* OpacityValue = AddScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupOpacity, 20, DefaultOpacity, -250, 560);
		UMaterialExpressionStaticSwitchParameter* UseOpacityTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseOpacityTexture, GroupOpacity, 30, true, 240, 500);
		UMaterialExpressionMultiply* OpacityMultiply = AddMultiply(Material, 0, 500);
		OpacityMultiply->A.Connect(0, OpacityTex);
		OpacityMultiply->B.Connect(0, OpacityValue);
		UseOpacityTexture->A.Connect(0, OpacityMultiply);
		UseOpacityTexture->B.Connect(0, OpacityValue);
		EditorData->Opacity.Connect(0, UseOpacityTexture);
	}

	if (UsesEmissive(MaterialType))
	{
		UMaterialExpressionTextureSampleParameter2D* EmissiveTex = AddTextureParameter(Material, FPBRMaterialParameters::EmissiveTexture, GroupEmissive, 10, -250, 700);
		UMaterialExpressionVectorParameter* EmissiveColor = AddVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupEmissive, 20, MaterialType == EPBRMaterialType::Emissive ? FLinearColor::White : FLinearColor::Black, -250, 840);
		UMaterialExpressionScalarParameter* EmissiveIntensity = AddScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupEmissive, 30, MaterialType == EPBRMaterialType::Emissive ? 1.0f : 0.0f, -250, 980);
		UMaterialExpressionStaticSwitchParameter* UseEmissiveTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseEmissiveTexture, GroupEmissive, 40, true, 480, 780);
		UMaterialExpressionMultiply* EmissiveColorMultiply = AddMultiply(Material, 0, 780);
		UMaterialExpressionMultiply* EmissiveIntensityMultiply = AddMultiply(Material, 240, 780);
		UMaterialExpressionMultiply* EmissiveSolidMultiply = AddMultiply(Material, 240, 980);
		EmissiveColorMultiply->A.Connect(0, EmissiveTex);
		EmissiveColorMultiply->B.Connect(0, EmissiveColor);
		EmissiveIntensityMultiply->A.Connect(0, EmissiveColorMultiply);
		EmissiveIntensityMultiply->B.Connect(0, EmissiveIntensity);
		EmissiveSolidMultiply->A.Connect(0, EmissiveColor);
		EmissiveSolidMultiply->B.Connect(0, EmissiveIntensity);
		UseEmissiveTexture->A.Connect(0, EmissiveIntensityMultiply);
		UseEmissiveTexture->B.Connect(0, EmissiveSolidMultiply);
		EditorData->EmissiveColor.Connect(0, UseEmissiveTexture);
	}

	if (UsesHeight(MaterialType))
	{
		UMaterialExpressionTextureSampleParameter2D* HeightTex = AddTextureParameter(Material, FPBRMaterialParameters::HeightTexture, GroupHeight, 10, -250, 1180, EMaterialSamplerType::SAMPLERTYPE_Masks);
		UMaterialExpressionScalarParameter* HeightStrength = AddScalarParameter(Material, FPBRMaterialParameters::HeightStrength, GroupHeight, 20, 0.0f, -250, 1320);
		UMaterialExpressionScalarParameter* PixelDepthOffsetStrength = AddScalarParameter(Material, FPBRMaterialParameters::PixelDepthOffsetStrength, GroupHeight, 30, 0.0f, -250, 1460);
		UMaterialExpressionConstant3Vector* NoWorldOffset = NewObject<UMaterialExpressionConstant3Vector>(Material);
		NoWorldOffset->Constant = FLinearColor::Black;
		NoWorldOffset->MaterialExpressionEditorX = 520;
		NoWorldOffset->MaterialExpressionEditorY = 1360;
		Material->GetExpressionCollection().AddExpression(NoWorldOffset);
		UMaterialExpressionStaticSwitchParameter* UseHeightTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseHeightTexture, GroupHeight, 40, true, 520, 1220);
		UMaterialExpressionVertexNormalWS* VertexNormal = NewObject<UMaterialExpressionVertexNormalWS>(Material);
		VertexNormal->MaterialExpressionEditorX = 20;
		VertexNormal->MaterialExpressionEditorY = 1380;
		Material->GetExpressionCollection().AddExpression(VertexNormal);
		UMaterialExpressionMultiply* HeightMultiply = AddMultiply(Material, 20, 1220);
		UMaterialExpressionMultiply* PDOMultiply = AddMultiply(Material, 280, 1220);
		UMaterialExpressionMultiply* WorldOffsetMultiply = AddMultiply(Material, 280, 1400);
		HeightMultiply->A.Connect(0, HeightTex);
		HeightMultiply->B.Connect(0, HeightStrength);
		PDOMultiply->A.Connect(0, HeightMultiply);
		PDOMultiply->B.Connect(0, PixelDepthOffsetStrength);
		WorldOffsetMultiply->A.Connect(0, VertexNormal);
		WorldOffsetMultiply->B.Connect(0, HeightMultiply);
		UseHeightTexture->A.Connect(0, WorldOffsetMultiply);
		UseHeightTexture->B.Connect(0, NoWorldOffset);
		EditorData->WorldPositionOffset.Connect(0, UseHeightTexture);
		EditorData->PixelDepthOffset.Connect(0, PDOMultiply);
		Material->MaxWorldPositionOffsetDisplacement = 20.0f;
		Material->bAlwaysEvaluateWorldPositionOffset = true;
		Material->bEnableTessellation = true;
		Material->bEnableDisplacementFade = true;
		Material->DisplacementScaling.Magnitude = 1.0f;
		Material->DisplacementScaling.Center = 0.5f;
	}

	if (UsesClearCoat(MaterialType))
	{
		Material->SetShadingModel(EMaterialShadingModel::MSM_ClearCoat);
		UMaterialExpressionTextureSampleParameter2D* ClearCoatTex = AddTextureParameter(Material, FPBRMaterialParameters::ClearCoatTexture, GroupClearCoat, 10, 360, 1040, EMaterialSamplerType::SAMPLERTYPE_Masks);
		UMaterialExpressionScalarParameter* ClearCoatValue = AddScalarParameter(Material, FPBRMaterialParameters::ClearCoat, GroupClearCoat, 20, 0.0f, 360, 1180);
		UMaterialExpressionStaticSwitchParameter* UseClearCoatTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseClearCoatTexture, GroupClearCoat, 30, true, 860, 1080);
		UMaterialExpressionMultiply* ClearCoatMultiply = AddMultiply(Material, 620, 1080);
		ClearCoatMultiply->A.Connect(0, ClearCoatTex);
		ClearCoatMultiply->B.Connect(0, ClearCoatValue);
		UseClearCoatTexture->A.Connect(0, ClearCoatMultiply);
		UseClearCoatTexture->B.Connect(0, ClearCoatValue);
		EditorData->ClearCoat.Connect(0, UseClearCoatTexture);

		UMaterialExpressionTextureSampleParameter2D* ClearCoatRoughnessTex = AddTextureParameter(Material, FPBRMaterialParameters::ClearCoatRoughnessTexture, GroupClearCoat, 30, 360, 1320, EMaterialSamplerType::SAMPLERTYPE_Masks);
		UMaterialExpressionScalarParameter* ClearCoatRoughnessValue = AddScalarParameter(Material, FPBRMaterialParameters::ClearCoatRoughness, GroupClearCoat, 40, 0.15f, 360, 1460);
		UMaterialExpressionStaticSwitchParameter* UseClearCoatRoughnessTexture = AddStaticSwitchParameter(Material, FPBRMaterialParameters::UseClearCoatRoughnessTexture, GroupClearCoat, 50, true, 860, 1360);
		UMaterialExpressionMultiply* ClearCoatRoughnessMultiply = AddMultiply(Material, 620, 1360);
		ClearCoatRoughnessMultiply->A.Connect(0, ClearCoatRoughnessTex);
		ClearCoatRoughnessMultiply->B.Connect(0, ClearCoatRoughnessValue);
		UseClearCoatRoughnessTexture->A.Connect(0, ClearCoatRoughnessMultiply);
		UseClearCoatRoughnessTexture->B.Connect(0, ClearCoatRoughnessValue);
		EditorData->ClearCoatRoughness.Connect(0, UseClearCoatRoughnessTexture);
	}

	if (UsesFabricFuzz(MaterialType))
	{
		UMaterialExpressionFresnel* FabricFresnel = AddFresnel(Material, 80, 780);
		UMaterialExpressionVectorParameter* FuzzColor = AddVectorParameter(Material, FPBRMaterialParameters::FabricFuzzColor, GroupSpecial, 10, FLinearColor(0.6f, 0.58f, 0.52f), 80, 900);
		UMaterialExpressionScalarParameter* FuzzStrength = AddScalarParameter(Material, FPBRMaterialParameters::FabricFuzzStrength, GroupSpecial, 20, 0.12f, 80, 1040);
		UMaterialExpressionMultiply* FuzzColorMultiply = AddMultiply(Material, 360, 840);
		UMaterialExpressionMultiply* FuzzStrengthMultiply = AddMultiply(Material, 600, 840);
		FuzzColorMultiply->A.Connect(0, FabricFresnel);
		FuzzColorMultiply->B.Connect(0, FuzzColor);
		FuzzStrengthMultiply->A.Connect(0, FuzzColorMultiply);
		FuzzStrengthMultiply->B.Connect(0, FuzzStrength);
		EditorData->EmissiveColor.Connect(0, FuzzStrengthMultiply);
	}

	if (UsesRefraction(MaterialType))
	{
		const float DefaultRefraction = GetTemplateDefaultRefraction(MaterialType);
		UMaterialExpressionScalarParameter* Refraction = AddScalarParameter(Material, FPBRMaterialParameters::RefractionAmount, GroupSpecial, 10, DefaultRefraction, 80, 1220);
		EditorData->Refraction.Connect(0, Refraction);
	}

	if (UsesWaterColor(MaterialType))
	{
		UMaterialExpressionVectorParameter* WaterColor = AddVectorParameter(Material, FPBRMaterialParameters::WaterColor, GroupSpecial, 20, FLinearColor(0.12f, 0.42f, 0.52f), 80, 1360);
		UMaterialExpressionMultiply* WaterTintMultiply = AddMultiply(Material, 360, 1360);
		WaterTintMultiply->A.Connect(0, UseBaseColorTexture);
		WaterTintMultiply->B.Connect(0, WaterColor);
		EditorData->BaseColor.Connect(0, WaterTintMultiply);
	}

	ConnectTextureSamplesToUV(Material, SharedUV);

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	OutMessage = TEXT("已构建母材质图表");
	return true;
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
