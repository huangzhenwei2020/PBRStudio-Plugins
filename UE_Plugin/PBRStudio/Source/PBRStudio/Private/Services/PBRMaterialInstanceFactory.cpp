#include "Services/PBRMaterialInstanceFactory.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "FileHelpers.h"
#include "Factories/TextureFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Services/PBRMaterialTemplateManager.h"

FString FPBRMaterialInstanceFactory::SanitizeAssetName(const FString& InName)
{
	FString Out = InName;
	const FString Invalid = TEXT("\\/:*?\"<>|.,;'");
	for (int32 i = 0; i < Invalid.Len(); ++i)
	{
		Out.ReplaceInline(*FString::Chr(Invalid[i]), TEXT("_"));
	}
	Out.ReplaceInline(TEXT(" "), TEXT("_"));
	if (Out.Len() > 80)
	{
		Out = Out.Left(80);
	}
	return Out.IsEmpty() ? TEXT("PBRMaterial") : Out;
}

FString FPBRMaterialInstanceFactory::BuildSetPackagePath(const FPBRMaterialSet& Set, const FPBRMaterialCreateOptions& Options)
{
	const FString CleanName = SanitizeAssetName(Set.Name);
	FString Root = Options.PackageRoot;
	Root.RemoveFromEnd(TEXT("/"));
	return Options.bCreateIsolatedMaterialFolder ? Root / CleanName : Root;
}

static bool IsTruthyMode(const FString& Value, const TArray<FString>& Accepted)
{
	const FString Normalized = Value.TrimStartAndEnd().ToLower();
	for (const FString& Token : Accepted)
	{
		if (Normalized == Token.ToLower())
		{
			return true;
		}
	}
	return false;
}

static FString ChooseNormalChannel(const FPBRMaterialSet& Set, const FString& NormalPreference)
{
	const bool bPreferGL = IsTruthyMode(NormalPreference, { TEXT("opengl"), TEXT("gl"), TEXT("normalgl"), TEXT("open gl"), TEXT("y+"), TEXT("ogl") });
	const bool bPreferDX = IsTruthyMode(NormalPreference, { TEXT("directx"), TEXT("dx"), TEXT("normaldx"), TEXT("direct x"), TEXT("y-"), TEXT("自动"), TEXT("auto") });

	TArray<FString> Order;
	if (bPreferGL)
	{
		Order = { FPBRChannels::NormalGL.ToString(), FPBRChannels::Normal.ToString(), FPBRChannels::NormalDX.ToString() };
	}
	else if (bPreferDX)
	{
		Order = { FPBRChannels::NormalDX.ToString(), FPBRChannels::Normal.ToString(), FPBRChannels::NormalGL.ToString() };
	}
	else
	{
		Order = { FPBRChannels::NormalDX.ToString(), FPBRChannels::Normal.ToString(), FPBRChannels::NormalGL.ToString() };
	}

	for (const FString& Channel : Order)
	{
		if (Set.Channels.Contains(Channel))
		{
			return Channel;
		}
	}
	return FString();
}

static TMap<FString, FString> BuildChannelsForCreation(const FPBRMaterialSet& Set, const FPBRMaterialCreateOptions& Options)
{
	TMap<FString, FString> Channels = Set.Channels;

	const FString ChosenNormal = ChooseNormalChannel(Set, Options.NormalPreference);
	if (!ChosenNormal.IsEmpty())
	{
		const FString ChosenPath = Set.Channels[ChosenNormal];
		Channels.Remove(FPBRChannels::Normal.ToString());
		Channels.Remove(FPBRChannels::NormalDX.ToString());
		Channels.Remove(FPBRChannels::NormalGL.ToString());
		Channels.Add(ChosenNormal, ChosenPath);
	}

	if (!Channels.Contains(FPBRChannels::Roughness.ToString()) && Channels.Contains(FPBRChannels::Glossiness.ToString()))
	{
		Channels.Add(FPBRChannels::Roughness.ToString(), Channels[FPBRChannels::Glossiness.ToString()]);
		Channels.Remove(FPBRChannels::Glossiness.ToString());
	}

	return Channels;
}

static bool AssetExistsAtPath(const FString& AssetPath)
{
	return UEditorAssetLibrary::DoesAssetExist(AssetPath) || UEditorAssetLibrary::LoadAsset(AssetPath) != nullptr;
}

static FString BuildTextureAssetPath(const FString& SetPackagePath, const FString& TextureName)
{
	return SetPackagePath / TEXT("Textures") / FPBRMaterialInstanceFactory::SanitizeAssetName(TextureName);
}

static bool IsLinearTextureChannel(const FString& Channel)
{
	return Channel == FPBRChannels::Normal.ToString() ||
		Channel == FPBRChannels::NormalDX.ToString() ||
		Channel == FPBRChannels::NormalGL.ToString() ||
		Channel == FPBRChannels::Roughness.ToString() ||
		Channel == FPBRChannels::Glossiness.ToString() ||
		Channel == FPBRChannels::Metallic.ToString() ||
		Channel == FPBRChannels::AO.ToString() ||
		Channel == FPBRChannels::Opacity.ToString() ||
		Channel == FPBRChannels::Height.ToString() ||
		Channel == FPBRChannels::Displacement.ToString() ||
		Channel == FPBRChannels::ORM.ToString() ||
		Channel == FPBRChannels::ARM.ToString() ||
		Channel == FPBRChannels::Specular.ToString() ||
		Channel == FPBRChannels::ClearCoat.ToString() ||
		Channel == FPBRChannels::ClearCoatRoughness.ToString() ||
		Channel == FPBRChannels::Anisotropy.ToString() ||
		Channel == FPBRChannels::Thickness.ToString();
}

UTexture2D* FPBRMaterialInstanceFactory::ImportTextureToAsset(const FString& FilePath, const FString& PackagePath, const FString& AssetName)
{
	return ImportTextureToAsset(FilePath, PackagePath, AssetName, FString());
}

UMaterialInterface* FPBRMaterialInstanceFactory::FindExistingMaterialInstance(
	const FPBRMaterialSet& Set,
	const FPBRMaterialCreateOptions& Options,
	FString& OutInstancePath)
{
	const FString SetPackagePath = BuildSetPackagePath(Set, Options);
	const FString CleanSetName = SanitizeAssetName(Set.Name);
	const FString InstanceName = Options.MaterialInstancePrefix + CleanSetName;
	OutInstancePath = SetPackagePath / InstanceName;

	UObject* Existing = UEditorAssetLibrary::LoadAsset(OutInstancePath);
	return Cast<UMaterialInterface>(Existing);
}

UTexture2D* FPBRMaterialInstanceFactory::ImportTextureToAsset(const FString& FilePath, const FString& PackagePath, const FString& AssetName, const FString& Channel)
{
	if (!FPaths::FileExists(FilePath))
	{
		return nullptr;
	}

	const FString CleanAssetName = SanitizeAssetName(AssetName);
	const FString FullPackagePath = PackagePath / TEXT("Textures") / CleanAssetName;

	if (UObject* Existing = UEditorAssetLibrary::LoadAsset(FullPackagePath))
	{
		UTexture2D* ExistingTexture = Cast<UTexture2D>(Existing);
		ConfigureTextureForChannel(ExistingTexture, Channel);
		if (ExistingTexture)
		{
			SavePackages({ ExistingTexture->GetPackage() });
		}
		return ExistingTexture;
	}

	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		return nullptr;
	}

	UTextureFactory* Factory = NewObject<UTextureFactory>();
	Factory->AddToRoot();
	Factory->SuppressImportOverwriteDialog();
	Factory->ColorSpaceMode = IsLinearTextureChannel(Channel) ? ETextureSourceColorSpace::Linear : ETextureSourceColorSpace::SRGB;

	bool bCancelled = false;
	UObject* Imported = Factory->FactoryCreateFile(
		UTexture2D::StaticClass(),
		Package,
		FName(*CleanAssetName),
		RF_Public | RF_Standalone,
		FilePath,
		TEXT(""),
		GWarn,
		bCancelled);

	Factory->RemoveFromRoot();

	UTexture2D* Texture = Cast<UTexture2D>(Imported);
	if (Texture)
	{
		ConfigureTextureForChannel(Texture, Channel);
		FAssetRegistryModule::AssetCreated(Texture);
		Package->SetDirtyFlag(true);
		Texture->PostEditChange();
		SavePackages({ Package });
	}
	return Texture;
}

void FPBRMaterialInstanceFactory::ConfigureTextureForChannel(UTexture2D* Texture, const FString& Channel)
{
	if (!Texture || Channel.IsEmpty())
	{
		return;
	}

	if (Channel == FPBRChannels::Normal.ToString() ||
		Channel == FPBRChannels::NormalDX.ToString() ||
		Channel == FPBRChannels::NormalGL.ToString())
	{
		Texture->SRGB = false;
		Texture->CompressionSettings = TextureCompressionSettings::TC_Normalmap;
		Texture->bFlipGreenChannel = (Channel == FPBRChannels::NormalGL.ToString());
	}
	else if (Channel == FPBRChannels::Roughness.ToString() ||
		Channel == FPBRChannels::Glossiness.ToString() ||
		Channel == FPBRChannels::Metallic.ToString() ||
		Channel == FPBRChannels::AO.ToString() ||
		Channel == FPBRChannels::Opacity.ToString() ||
		Channel == FPBRChannels::Height.ToString() ||
		Channel == FPBRChannels::Displacement.ToString() ||
		Channel == FPBRChannels::ORM.ToString() ||
		Channel == FPBRChannels::ARM.ToString() ||
		Channel == FPBRChannels::Specular.ToString() ||
		Channel == FPBRChannels::ClearCoat.ToString() ||
		Channel == FPBRChannels::ClearCoatRoughness.ToString() ||
		Channel == FPBRChannels::Anisotropy.ToString() ||
		Channel == FPBRChannels::Thickness.ToString())
	{
		Texture->SRGB = false;
		Texture->CompressionSettings = TextureCompressionSettings::TC_Masks;
	}
	else
	{
		Texture->SRGB = true;
		Texture->CompressionSettings = TextureCompressionSettings::TC_Default;
		Texture->bFlipGreenChannel = false;
	}

	Texture->PostEditChange();
	Texture->MarkPackageDirty();
}

FName FPBRMaterialInstanceFactory::GetTextureParameterForChannel(const FString& Channel)
{
	if (Channel == FPBRChannels::BaseColor.ToString())
	{
		return FPBRMaterialParameters::BaseColorTexture;
	}
	if (Channel == FPBRChannels::Normal.ToString() || Channel == FPBRChannels::NormalDX.ToString() || Channel == FPBRChannels::NormalGL.ToString())
	{
		return FPBRMaterialParameters::NormalTexture;
	}
	if (Channel == FPBRChannels::Roughness.ToString() || Channel == FPBRChannels::Glossiness.ToString())
	{
		return FPBRMaterialParameters::RoughnessTexture;
	}
	if (Channel == FPBRChannels::Metallic.ToString())
	{
		return FPBRMaterialParameters::MetallicTexture;
	}
	if (Channel == FPBRChannels::AO.ToString())
	{
		return FPBRMaterialParameters::AOTexture;
	}
	if (Channel == FPBRChannels::Opacity.ToString())
	{
		return FPBRMaterialParameters::OpacityTexture;
	}
	if (Channel == FPBRChannels::Emissive.ToString())
	{
		return FPBRMaterialParameters::EmissiveTexture;
	}
	if (Channel == FPBRChannels::Specular.ToString())
	{
		return FPBRMaterialParameters::SpecularTexture;
	}
	if (Channel == FPBRChannels::Height.ToString() || Channel == FPBRChannels::Displacement.ToString())
	{
		return FPBRMaterialParameters::HeightTexture;
	}
	if (Channel == FPBRChannels::ClearCoat.ToString())
	{
		return FPBRMaterialParameters::ClearCoatTexture;
	}
	if (Channel == FPBRChannels::ClearCoatRoughness.ToString())
	{
		return FPBRMaterialParameters::ClearCoatRoughnessTexture;
	}
	return NAME_None;
}

static bool HasImportedTextureChannel(const TMap<FString, UTexture2D*>& Textures, const FName& ChannelName)
{
	const UTexture2D* const* Texture = Textures.Find(ChannelName.ToString());
	return Texture && *Texture;
}

static bool HasAnyImportedTextureChannel(const TMap<FString, UTexture2D*>& Textures, const TArray<FName>& ChannelNames)
{
	for (const FName& ChannelName : ChannelNames)
	{
		if (HasImportedTextureChannel(Textures, ChannelName))
		{
			return true;
		}
	}
	return false;
}

static float GetDefaultRoughnessMultiplier(EPBRMaterialType MaterialType, bool bHasRoughnessTexture)
{
	if (bHasRoughnessTexture)
	{
		return 1.0f;
	}

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
	case EPBRMaterialType::Transparent:
		return 0.55f;
	case EPBRMaterialType::Emissive:
		return 0.0f;
	case EPBRMaterialType::Standard:
	default:
		return 0.5f;
	}
}

static float GetDefaultOpacity(EPBRMaterialType MaterialType, bool bHasOpacityTexture)
{
	if (bHasOpacityTexture)
	{
		return 1.0f;
	}

	switch (MaterialType)
	{
	case EPBRMaterialType::Glass:
		return 0.35f;
	case EPBRMaterialType::Water:
		return 0.55f;
	case EPBRMaterialType::Transparent:
		return 0.75f;
	case EPBRMaterialType::Fabric:
	case EPBRMaterialType::Wood:
	case EPBRMaterialType::Stone:
	case EPBRMaterialType::Tile:
	case EPBRMaterialType::Leather:
	case EPBRMaterialType::Plastic:
	case EPBRMaterialType::Metal:
	case EPBRMaterialType::Emissive:
	case EPBRMaterialType::Standard:
	default:
		return 1.0f;
	}
}

static bool MaterialNameSuggestsStrongDisplacement(const FString& Text)
{
	return Text.Contains(TEXT("rock")) || Text.Contains(TEXT("stone")) || Text.Contains(TEXT("cliff")) ||
		Text.Contains(TEXT("brick")) || Text.Contains(TEXT("wall")) || Text.Contains(TEXT("paving")) ||
		Text.Contains(TEXT("pavement")) || Text.Contains(TEXT("cobble")) || Text.Contains(TEXT("slab")) ||
		Text.Contains(TEXT("bark")) || Text.Contains(TEXT("damage")) || Text.Contains(TEXT("damaged")) ||
		Text.Contains(TEXT("broken")) || Text.Contains(TEXT("cracked")) || Text.Contains(TEXT("concrete")) ||
		Text.Contains(TEXT("asphalt")) || Text.Contains(TEXT("rubble")) || Text.Contains(TEXT("岩")) ||
		Text.Contains(TEXT("石")) || Text.Contains(TEXT("砖")) || Text.Contains(TEXT("墙")) ||
		Text.Contains(TEXT("路")) || Text.Contains(TEXT("树皮")) || Text.Contains(TEXT("破损")) ||
		Text.Contains(TEXT("裂"));
}

static float GetDefaultHeightStrength(EPBRMaterialType MaterialType, const FPBRMaterialSet* SourceSet, bool bHasHeightTexture)
{
	if (!bHasHeightTexture)
	{
		return 0.0f;
	}

	const FString DetectionText = SourceSet
		? (SourceSet->Name + TEXT(" ") + SourceSet->Folder).ToLower()
		: FString();
	if (MaterialNameSuggestsStrongDisplacement(DetectionText))
	{
		return 1.0f;
	}

	switch (MaterialType)
	{
	case EPBRMaterialType::Stone:
	case EPBRMaterialType::Tile:
		return 1.0f;
	case EPBRMaterialType::Wood:
		return DetectionText.Contains(TEXT("bark")) || DetectionText.Contains(TEXT("树皮")) ? 1.0f : 0.35f;
	case EPBRMaterialType::Leather:
	case EPBRMaterialType::Fabric:
		return 0.18f;
	case EPBRMaterialType::Standard:
		return 0.5f;
	default:
		return 0.05f;
	}
}

void FPBRMaterialInstanceFactory::ApplyTextureParameters(UMaterialInstanceConstant* Instance, const TMap<FString, UTexture2D*>& Textures, EPBRMaterialType MaterialType, const FPBRMaterialSet* SourceSet)
{
	if (!Instance)
	{
		return;
	}

	for (const TPair<FString, UTexture2D*>& Pair : Textures)
	{
		const FName ParameterName = GetTextureParameterForChannel(Pair.Key);
		if (!ParameterName.IsNone() && Pair.Value)
		{
			Instance->SetTextureParameterValueEditorOnly(ParameterName, Pair.Value);
		}
	}

	const bool bHasRoughnessTexture = HasImportedTextureChannel(Textures, FPBRChannels::Roughness);
	const bool bHasMetallicTexture = HasImportedTextureChannel(Textures, FPBRChannels::Metallic);
	const bool bHasAOTexture = HasImportedTextureChannel(Textures, FPBRChannels::AO);
	const bool bHasOpacityTexture = HasImportedTextureChannel(Textures, FPBRChannels::Opacity);
	const bool bHasEmissiveTexture = HasImportedTextureChannel(Textures, FPBRChannels::Emissive);
	const bool bHasSpecularTexture = HasImportedTextureChannel(Textures, FPBRChannels::Specular);
	const bool bHasHeightTexture = HasAnyImportedTextureChannel(Textures, { FPBRChannels::Height, FPBRChannels::Displacement });
	const bool bHasClearCoatTexture = HasImportedTextureChannel(Textures, FPBRChannels::ClearCoat);
	const bool bHasClearCoatRoughnessTexture = HasImportedTextureChannel(Textures, FPBRChannels::ClearCoatRoughness);

	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseBaseColorTexture), HasImportedTextureChannel(Textures, FPBRChannels::BaseColor));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseNormalTexture), HasAnyImportedTextureChannel(Textures, { FPBRChannels::Normal, FPBRChannels::NormalDX, FPBRChannels::NormalGL }));
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseRoughnessTexture), bHasRoughnessTexture);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseMetallicTexture), bHasMetallicTexture);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseAOTexture), bHasAOTexture);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseOpacityTexture), bHasOpacityTexture);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), bHasEmissiveTexture);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseSpecularTexture), bHasSpecularTexture);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseHeightTexture), bHasHeightTexture);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseClearCoatTexture), bHasClearCoatTexture);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseClearCoatRoughnessTexture), bHasClearCoatRoughnessTexture);

	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor::White);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, GetDefaultRoughnessMultiplier(MaterialType, false));
	Instance->SetScalarParameterValueEditorOnly(
		FPBRMaterialParameters::RoughnessMultiplier,
		GetDefaultRoughnessMultiplier(MaterialType, bHasRoughnessTexture));
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, MaterialType == EPBRMaterialType::Metal ? 1.0f : 0.0f);
	Instance->SetScalarParameterValueEditorOnly(
		FPBRMaterialParameters::MetallicMultiplier,
		bHasMetallicTexture ? 1.0f : (MaterialType == EPBRMaterialType::Metal ? 1.0f : 0.0f));
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOValue, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(
		FPBRMaterialParameters::AOMultiplier,
		bHasAOTexture ? 1.0f : 1.0f);
	Instance->SetScalarParameterValueEditorOnly(
		FPBRMaterialParameters::Opacity,
		GetDefaultOpacity(MaterialType, bHasOpacityTexture));
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, (bHasEmissiveTexture || MaterialType == EPBRMaterialType::Emissive) ? 1.0f : 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, bHasSpecularTexture ? 1.0f : 0.5f);
	const float HeightStrength = GetDefaultHeightStrength(MaterialType, SourceSet, bHasHeightTexture);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::HeightStrength, HeightStrength);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::PixelDepthOffsetStrength, bHasHeightTexture ? FMath::Min(HeightStrength, 0.25f) : 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoat, bHasClearCoatTexture ? 1.0f : 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatRoughness, bHasClearCoatRoughnessTexture ? 1.0f : 0.15f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Anisotropy, 0.0f);
	if (MaterialType == EPBRMaterialType::Fabric)
	{
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzColor, FLinearColor(0.6f, 0.58f, 0.52f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzStrength, 0.12f);
	}
	if (MaterialType == EPBRMaterialType::Glass)
	{
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.52f);
	}
	else if (MaterialType == EPBRMaterialType::Water)
	{
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.333f);
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::WaterColor, FLinearColor(0.12f, 0.42f, 0.52f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedU, 0.12f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedV, 0.06f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleScale, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleStrength, 0.75f);
	}
	else if (MaterialType == EPBRMaterialType::Transparent)
	{
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.0f);
	}
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVRotationDegrees, 0.0f);
	Instance->PostEditChange();
}

bool FPBRMaterialInstanceFactory::CreateInstanceFromSet(
	const FPBRMaterialSet& Set,
	const FPBRMaterialCreateOptions& Options,
	FPBRMaterialCreateResult& OutResult)
{
	OutResult = FPBRMaterialCreateResult();

	FString TemplateMessage;
	UMaterial* ParentMaterial = FPBRMaterialTemplateManager::EnsureTemplateMaterial(Options.MaterialType, TemplateMessage);
	if (!ParentMaterial)
	{
		OutResult.Message = TemplateMessage;
		return false;
	}
	FString ExampleMessage;
	FPBRMaterialTemplateManager::EnsureExampleMaterialInstance(Options.MaterialType, ExampleMessage);
	OutResult.ParentMaterial = ParentMaterial;

	const FString SetPackagePath = BuildSetPackagePath(Set, Options);
	OutResult.PackagePath = SetPackagePath;
	const FString CleanSetName = SanitizeAssetName(Set.Name);
	const FString InstanceName = Options.MaterialInstancePrefix + CleanSetName;
	const FString InstancePath = SetPackagePath / InstanceName;

	FString ExistingInstancePath;
	if (UMaterialInterface* ExistingInstance = FindExistingMaterialInstance(Set, Options, ExistingInstancePath))
	{
		OutResult.MaterialInstance = ExistingInstance;
		OutResult.bSkippedBecauseExists = true;
		OutResult.Message = TEXT("使用已有材质实例");
		return true;
	}

	TArray<FString> ExistingAssetNames;
	const TMap<FString, FString> ChannelsForCreation = BuildChannelsForCreation(Set, Options);
	if (Options.bImportTextures)
	{
		for (const TPair<FString, FString>& ChannelPair : ChannelsForCreation)
		{
			const FString& Channel = ChannelPair.Key;
			if (Channel == TEXT("Preview") || Channel == TEXT("Unknown"))
			{
				continue;
			}

			const FString TextureName = TEXT("T_") + CleanSetName + TEXT("_") + Channel;
			if (AssetExistsAtPath(BuildTextureAssetPath(SetPackagePath, TextureName)))
			{
				ExistingAssetNames.Add(TextureName);
			}
		}
	}

	if (ExistingAssetNames.Num() > 0)
	{
		OutResult.bSkippedBecauseExists = true;
		OutResult.Message = FString::Printf(
			TEXT("重复: 已存在 %s，已跳过创建"),
			*FString::Join(ExistingAssetNames, TEXT(", ")));
		return false;
	}

	TMap<FString, UTexture2D*> ImportedTextures;
	if (Options.bImportTextures)
	{
		for (const TPair<FString, FString>& ChannelPair : ChannelsForCreation)
		{
			const FString& Channel = ChannelPair.Key;
			if (Channel == TEXT("Preview") || Channel == TEXT("Unknown"))
			{
				continue;
			}

			const FString TextureName = TEXT("T_") + CleanSetName + TEXT("_") + Channel;
			if (UTexture2D* Texture = ImportTextureToAsset(ChannelPair.Value, SetPackagePath, TextureName, Channel))
			{
				ImportedTextures.Add(Channel, Texture);
				OutResult.ImportedTextures.Add(Channel, Texture);
			}
		}
	}

	UPackage* InstancePackage = CreatePackage(*InstancePath);
	if (!InstancePackage)
	{
		OutResult.Message = TEXT("创建材质实例包失败");
		return false;
	}

	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;
	UObject* NewObject = Factory->FactoryCreateNew(
		UMaterialInstanceConstant::StaticClass(),
		InstancePackage,
		FName(*InstanceName),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn);

	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(NewObject);
	if (!Instance)
	{
		OutResult.Message = TEXT("创建材质实例失败");
		return false;
	}

	ApplyTextureParameters(Instance, ImportedTextures, Options.MaterialType, &Set);
	FAssetRegistryModule::AssetCreated(Instance);
	InstancePackage->SetDirtyFlag(true);
	SavePackages({ InstancePackage });

	OutResult.MaterialInstance = Instance;
	OutResult.Message = TEXT("已创建材质实例");
	return true;
}

void FPBRMaterialInstanceFactory::SavePackages(const TArray<UPackage*>& Packages)
{
	TArray<UPackage*> ValidPackages;
	for (UPackage* Package : Packages)
	{
		if (Package)
		{
			ValidPackages.Add(Package);
		}
	}
	if (ValidPackages.Num() > 0)
	{
		UEditorLoadingAndSavingUtils::SavePackages(ValidPackages, true);
	}
}
