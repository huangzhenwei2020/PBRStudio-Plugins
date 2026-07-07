#include "Services/PBRSceneMaterialReplacer.h"

#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "EditorSupportDelegates.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EditorFramework/AssetImportData.h"
#include "EngineUtils.h"
#include "Engine/TextureDefines.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionRotator.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "MaterialUtilities.h"
#include "MaterialShared.h"
#include "MaterialEditingLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Services/PBRMaterialFactory.h"
#include "Services/PBRMaterialInstanceFactory.h"
#include "Services/PBRMaterialTemplateManager.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"

TArray<FPBRSceneMaterialSlot> FPBRSceneMaterialReplacer::LastReplacementSlots;

static UTexture2D* LoadSceneDefaultTexture(const TCHAR* AssetPath)
{
	return LoadObject<UTexture2D>(nullptr, AssetPath);
}

static UTexture2D* GetSceneDefaultTextureForSampler(EMaterialSamplerType SamplerType)
{
	switch (SamplerType)
	{
	case EMaterialSamplerType::SAMPLERTYPE_Normal:
		return LoadSceneDefaultTexture(TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
	case EMaterialSamplerType::SAMPLERTYPE_Masks:
	case EMaterialSamplerType::SAMPLERTYPE_LinearColor:
		return LoadSceneDefaultTexture(TEXT("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks.DefaultDiffuse_TC_Masks"));
	case EMaterialSamplerType::SAMPLERTYPE_Color:
	default:
		return LoadSceneDefaultTexture(TEXT("/Engine/EngineMaterials/T_Default_BaseColor.T_Default_BaseColor"));
	}
}

bool FPBRSceneMaterialReplacer::IsSupportedImagePath(const FString& Path)
{
	const FString Ext = FPaths::GetExtension(Path).ToLower();
	static const TSet<FString> Supported = {
		TEXT("jpg"), TEXT("jpeg"), TEXT("png"), TEXT("tga"), TEXT("bmp"),
		TEXT("tif"), TEXT("tiff"), TEXT("exr"), TEXT("hdr")
	};
	if (!Supported.Contains(Ext))
	{
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	return ImageWrapperModule.GetImageFormatFromExtension(*Ext) != EImageFormat::Invalid;
}

bool FPBRSceneMaterialReplacer::IsPBRStudioGeneratedMaterial(UMaterialInterface* Material)
{
	if (!Material)
	{
		return false;
	}

	const FString Path = Material->GetPathName();
	const FString Name = Material->GetName();
	const auto IsKnownPBRStudioMaterialName = [](const FString& CandidateName)
	{
		return CandidateName.StartsWith(TEXT("M_PBR_")) ||
			CandidateName.StartsWith(TEXT("MI_PBRSR_")) ||
			CandidateName.StartsWith(TEXT("MI_PBRR_")) ||
			CandidateName.StartsWith(TEXT("MI_PBR_")) ||
			CandidateName.Equals(TEXT("M_AdvancedGlass")) ||
			CandidateName.Equals(TEXT("M_PBRStudio_SceneReplace")) ||
			CandidateName.Equals(TEXT("M_PBRStudio_SceneReplace_Glass")) ||
			CandidateName.Equals(TEXT("M_PBRStudio_SceneReplace_Emissive"));
	};

	if (Path.Contains(TEXT("/PBRStudio/")) ||
		Path.Contains(TEXT("/PBRStudio.")) ||
		Path.Contains(TEXT("/Game/PBRStudio/")) ||
		IsKnownPBRStudioMaterialName(Name))
	{
		return true;
	}

	if (const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
	{
		UMaterialInterface* Parent = Instance->Parent;
		if (Parent)
		{
			return FPBRSceneMaterialReplacer::IsPBRStudioGeneratedMaterial(Parent);
		}
	}

	return false;
}

static FString TextureSourceFilename(UTexture2D* Texture)
{
	if (!Texture)
	{
		return FString();
	}
	FString SourcePath = Texture->AssetImportData ? Texture->AssetImportData.Get()->GetFirstFilename() : FString();
	return SourcePath;
}

static FString NormalizeMaterialSearchText(const FString& Text)
{
	FString Result = Text.ToLower();
	Result.ReplaceInline(TEXT("-"), TEXT("_"));
	Result.ReplaceInline(TEXT(" "), TEXT("_"));
	Result.ReplaceInline(TEXT("."), TEXT("_"));
	return Result;
}

static bool NameHasAnyToken(const FString& Text, std::initializer_list<const TCHAR*> Tokens)
{
	const FString Normalized = NormalizeMaterialSearchText(Text);
	for (const TCHAR* Token : Tokens)
	{
		if (Normalized.Contains(Token))
		{
			return true;
		}
	}
	return false;
}

static bool IsTextureSourceUsable(UTexture2D* Texture, FString& OutSourcePath)
{
	OutSourcePath = TextureSourceFilename(Texture);
	return Texture && (OutSourcePath.IsEmpty() || (FPBRSceneMaterialReplacer::IsSupportedImagePath(OutSourcePath) && FPaths::FileExists(OutSourcePath)));
}

static FString BuildTextureSearchText(UTexture2D* Texture, const FString& ExtraText = FString())
{
	if (!Texture)
	{
		return ExtraText;
	}

	const FString SourcePath = TextureSourceFilename(Texture);
	const FString SourceFile = FPaths::GetCleanFilename(SourcePath);
	return ExtraText + TEXT(" ") + Texture->GetName() + TEXT(" ") + Texture->GetPathName() + TEXT(" ") + SourceFile + TEXT(" ") + SourcePath;
}

static int32 CountTextureTokens(const FString& NormalizedText, std::initializer_list<const TCHAR*> Tokens)
{
	int32 Count = 0;
	for (const TCHAR* Token : Tokens)
	{
		if (NormalizedText.Contains(Token))
		{
			++Count;
		}
	}
	return Count;
}

static bool HasDelimitedTextureToken(const FString& Text, std::initializer_list<const TCHAR*> Tokens)
{
	FString Padded = NormalizeMaterialSearchText(Text);
	Padded.ReplaceInline(TEXT("__"), TEXT("_"));
	Padded = TEXT("_") + Padded + TEXT("_");
	for (const TCHAR* Token : Tokens)
	{
		if (Padded.Contains(FString::Printf(TEXT("_%s_"), Token)))
		{
			return true;
		}
	}
	return false;
}

static int32 ScoreBaseColorTextureCandidate(UTexture2D* Texture, const FString& ExtraText, bool bFromBaseColorProperty, bool bFromInstanceOverride)
{
	if (!Texture)
	{
		return MIN_int32;
	}

	FString SourcePath;
	if (!IsTextureSourceUsable(Texture, SourcePath))
	{
		return MIN_int32;
	}

	const FString SearchText = BuildTextureSearchText(Texture, ExtraText);
	const FString Normalized = NormalizeMaterialSearchText(SearchText);
	int32 Score = 0;

	if (bFromInstanceOverride)
	{
		Score += 20;
	}
	if (bFromBaseColorProperty)
	{
		Score += 45;
	}

	Score += 26 * CountTextureTokens(Normalized, {
		TEXT("basecolor"), TEXT("base_color"), TEXT("base_colour"), TEXT("basecolour"),
		TEXT("albedo"), TEXT("diffusecolor"), TEXT("diffuse_color"), TEXT("diffuse_colour"),
		TEXT("texmap_diffuse"), TEXT("map_diffuse"), TEXT("vray_diffuse"), TEXT("corona_diffuse"),
		TEXT("基础颜色"), TEXT("基础色"), TEXT("漫反射"), TEXT("底色")
	});
	Score += 14 * CountTextureTokens(Normalized, {
		TEXT("diffuse"), TEXT("albedo_texture"), TEXT("color_texture"), TEXT("colour_texture"),
		TEXT("base_col"), TEXT("basecol"), TEXT("base")
	});
	Score += 8 * CountTextureTokens(Normalized, {
		TEXT("color"), TEXT("colour"), TEXT("col"), TEXT("clr"), TEXT("bitmap"), TEXT("texmap")
	});

	if (HasDelimitedTextureToken(SearchText, { TEXT("d"), TEXT("diff"), TEXT("dif"), TEXT("c"), TEXT("col"), TEXT("color"), TEXT("base"), TEXT("bc") }))
	{
		Score += 18;
	}

	Score -= 34 * CountTextureTokens(Normalized, {
		TEXT("normal"), TEXT("normalmap"), TEXT("nrm"), TEXT("nor"), TEXT("bump"), TEXT("roughness"),
		TEXT("rough"), TEXT("glossiness"), TEXT("gloss"), TEXT("metallic"), TEXT("metalness"), TEXT("metal"),
		TEXT("ambientocclusion"), TEXT("occlusion"), TEXT("height"), TEXT("displacement"), TEXT("disp"),
		TEXT("opacity"), TEXT("alpha"), TEXT("transparency"), TEXT("mask"), TEXT("orm"), TEXT("arm"),
		TEXT("specular"), TEXT("spec"), TEXT("emissive"), TEXT("emission"), TEXT("lightmap")
	});
	if (HasDelimitedTextureToken(SearchText, { TEXT("n"), TEXT("nrm"), TEXT("nor"), TEXT("r"), TEXT("rough"), TEXT("m"), TEXT("metal"), TEXT("ao"), TEXT("h"), TEXT("disp"), TEXT("opacity"), TEXT("alpha"), TEXT("mask") }))
	{
		Score -= 22;
	}

	if (Texture->SRGB)
	{
		Score += 10;
	}
	else
	{
		Score -= 8;
	}

	switch (Texture->CompressionSettings)
	{
	case TextureCompressionSettings::TC_Default:
		Score += 8;
		break;
	case TextureCompressionSettings::TC_Normalmap:
		Score -= 80;
		break;
	case TextureCompressionSettings::TC_Masks:
	case TextureCompressionSettings::TC_Grayscale:
		Score -= 28;
		break;
	default:
		break;
	}

	return Score;
}

struct FBaseColorTextureCandidate
{
	UTexture2D* Texture = nullptr;
	FString SourcePath;
	int32 Score = MIN_int32;
};

static void ConsiderBaseColorTextureCandidate(
	TArray<FBaseColorTextureCandidate>& Candidates,
	UTexture2D* Texture,
	const FString& ExtraText,
	bool bFromBaseColorProperty,
	bool bFromInstanceOverride)
{
	if (!Texture)
	{
		return;
	}

	FString SourcePath;
	if (!IsTextureSourceUsable(Texture, SourcePath))
	{
		return;
	}

	const int32 Score = ScoreBaseColorTextureCandidate(Texture, ExtraText, bFromBaseColorProperty, bFromInstanceOverride);
	if (Score <= MIN_int32 / 2)
	{
		return;
	}

	for (FBaseColorTextureCandidate& Existing : Candidates)
	{
		if (Existing.Texture == Texture)
		{
			if (Score > Existing.Score)
			{
				Existing.Score = Score;
				Existing.SourcePath = SourcePath;
			}
			return;
		}
	}

	FBaseColorTextureCandidate Candidate;
	Candidate.Texture = Texture;
	Candidate.SourcePath = SourcePath;
	Candidate.Score = Score;
	Candidates.Add(Candidate);
}

static bool DoesParamMatchAnyName(const FMaterialParameterInfo& Info, const TArray<FName>& ParamNames)
{
	for (const FName& ParamName : ParamNames)
	{
		if (Info.Name.ToString().Equals(ParamName.ToString(), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

static FString NormalizeParameterNameForLooseMatch(const FString& InName)
{
	FString Result = InName.ToLower().TrimStartAndEnd();
	int32 ParenIndex = INDEX_NONE;
	if (Result.FindChar(TEXT('('), ParenIndex))
	{
		Result = Result.Left(ParenIndex).TrimStartAndEnd();
	}
	Result.ReplaceInline(TEXT("_"), TEXT(""));
	Result.ReplaceInline(TEXT("-"), TEXT(""));
	Result.ReplaceInline(TEXT(" "), TEXT(""));
	return Result;
}

static bool IsDiffuseLikeParameterName(const FName& Name)
{
	const FString Normalized = NormalizeParameterNameForLooseMatch(Name.ToString());
	return Normalized == TEXT("diffuse") ||
		Normalized == TEXT("diffusecolor") ||
		Normalized == TEXT("diffusetexture") ||
		Normalized == TEXT("texmapdiffuse") ||
		Normalized == TEXT("mapdiffuse") ||
		Normalized == TEXT("vraydiffuse") ||
		Normalized == TEXT("coronadiffuse") ||
		Normalized == TEXT("漫反射") ||
		Normalized == TEXT("颜色") ||
		Normalized == TEXT("基础颜色") ||
		Normalized == TEXT("基础色");
}

static UTexture2D* FindDiffuseTextureInInstanceOverrides(UMaterialInterface* Material, FString& OutSourcePath)
{
	const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	if (!Instance)
	{
		return nullptr;
	}

	auto TryTexture = [&OutSourcePath](UTexture* Value) -> UTexture2D*
	{
		if (UTexture2D* Texture = Cast<UTexture2D>(Value))
		{
			if (IsTextureSourceUsable(Texture, OutSourcePath))
			{
				return Texture;
			}
		}
		return nullptr;
	};

	for (const FTextureParameterValue& ParameterValue : Instance->TextureParameterValues)
	{
		if (IsDiffuseLikeParameterName(ParameterValue.ParameterInfo.Name))
		{
			if (UTexture2D* Texture = TryTexture(ParameterValue.ParameterValue.Get()))
			{
				return Texture;
			}
		}
	}

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
	Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		if (!IsDiffuseLikeParameterName(Info.Name))
		{
			continue;
		}

		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(Info, Value, true))
		{
			if (UTexture2D* Texture = TryTexture(Value))
			{
				return Texture;
			}
		}
	}

	return nullptr;
}

static bool TryGetDiffuseColorFromInstanceOverrides(UMaterialInterface* Material, FLinearColor& OutColor)
{
	const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	if (!Instance)
	{
		return false;
	}

	for (const FVectorParameterValue& ParameterValue : Instance->VectorParameterValues)
	{
		if (IsDiffuseLikeParameterName(ParameterValue.ParameterInfo.Name))
		{
			OutColor = ParameterValue.ParameterValue;
			return true;
		}
	}

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
	Material->GetAllVectorParameterInfo(ParameterInfos, ParameterIds);
	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		if (!IsDiffuseLikeParameterName(Info.Name))
		{
			continue;
		}

		FLinearColor Value = FLinearColor::White;
		if (Material->GetVectorParameterValue(Info, Value, true))
		{
			OutColor = Value;
			return true;
		}
	}

	return false;
}

static UTexture2D* FindTextureInInstanceOverrides(UMaterialInterface* Material, const TArray<FName>& ParamNames, FString& OutSourcePath)
{
	const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	if (!Instance)
	{
		return nullptr;
	}

	for (const FTextureParameterValue& ParameterValue : Instance->TextureParameterValues)
	{
		if (!DoesParamMatchAnyName(ParameterValue.ParameterInfo, ParamNames))
		{
			continue;
		}
		if (UTexture2D* Texture = Cast<UTexture2D>(ParameterValue.ParameterValue.Get()))
		{
			if (IsTextureSourceUsable(Texture, OutSourcePath))
			{
				return Texture;
			}
		}
	}

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
	Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		if (!DoesParamMatchAnyName(Info, ParamNames))
		{
			continue;
		}

		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(Info, Value, true))
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Value))
			{
				if (IsTextureSourceUsable(Texture, OutSourcePath))
				{
					return Texture;
				}
			}
		}
	}
	return nullptr;
}

static UTexture2D* FindTextureInInstanceOverridesByTokens(
	UMaterialInterface* Material,
	std::initializer_list<const TCHAR*> IncludeTokens,
	std::initializer_list<const TCHAR*> ExcludeTokens,
	FString& OutSourcePath)
{
	const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	if (!Instance)
	{
		return nullptr;
	}

	for (const FTextureParameterValue& ParameterValue : Instance->TextureParameterValues)
	{
		UTexture2D* Texture = Cast<UTexture2D>(ParameterValue.ParameterValue.Get());
		if (!Texture)
		{
			continue;
		}

		const FString NameText = ParameterValue.ParameterInfo.Name.ToString() + TEXT(" ") +
			Texture->GetName() + TEXT(" ") + Texture->GetPathName() + TEXT(" ") + TextureSourceFilename(Texture);
		if (NameHasAnyToken(NameText, IncludeTokens) && !NameHasAnyToken(NameText, ExcludeTokens))
		{
			if (IsTextureSourceUsable(Texture, OutSourcePath))
			{
				return Texture;
			}
		}
	}

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
	Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		UTexture* Value = nullptr;
		if (!Material->GetTextureParameterValue(Info, Value, true))
		{
			continue;
		}

		UTexture2D* Texture = Cast<UTexture2D>(Value);
		if (!Texture)
		{
			continue;
		}

		const FString NameText = Info.Name.ToString() + TEXT(" ") +
			Texture->GetName() + TEXT(" ") + Texture->GetPathName() + TEXT(" ") + TextureSourceFilename(Texture);
		if (NameHasAnyToken(NameText, IncludeTokens) && !NameHasAnyToken(NameText, ExcludeTokens))
		{
			if (IsTextureSourceUsable(Texture, OutSourcePath))
			{
				return Texture;
			}
		}
	}
	return nullptr;
}

static bool ShouldOnlyUseInstanceTextureOverrides(UMaterialInterface* Material)
{
	return Cast<UMaterialInstance>(Material) != nullptr;
}

static UTexture2D* FindTextureByParamNames(UMaterialInterface* Material, const TArray<FName>& ParamNames, FString& OutSourcePath)
{
	if (!Material)
	{
		return nullptr;
	}

	if (UTexture2D* Texture = FindTextureInInstanceOverrides(Material, ParamNames, OutSourcePath))
	{
		return Texture;
	}
	if (ShouldOnlyUseInstanceTextureOverrides(Material))
	{
		return nullptr;
	}

	for (const FName& ParamName : ParamNames)
	{
		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(FMaterialParameterInfo(ParamName), Value))
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Value))
			{
				if (IsTextureSourceUsable(Texture, OutSourcePath))
				{
					return Texture;
				}
			}
		}
	}

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
	Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		if (!DoesParamMatchAnyName(Info, ParamNames))
		{
			continue;
		}
		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(Info, Value))
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Value))
			{
				if (IsTextureSourceUsable(Texture, OutSourcePath))
				{
					return Texture;
				}
			}
		}
	}
	return nullptr;
}

static UTexture2D* FindTextureInMaterialProperty(UMaterialInterface* Material, EMaterialProperty Property, FString& OutSourcePath)
{
	if (!Material)
	{
		return nullptr;
	}
	if (ShouldOnlyUseInstanceTextureOverrides(Material))
	{
		return nullptr;
	}

	TArray<UTexture*> UsedTextures;
	if (Material->GetTexturesInPropertyChain(Property, UsedTextures, nullptr, nullptr))
	{
		for (UTexture* Texture : UsedTextures)
		{
			if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
			{
				if (IsTextureSourceUsable(Texture2D, OutSourcePath))
				{
					return Texture2D;
				}
			}
		}
	}
	return nullptr;
}

static UTexture2D* FindUsedTextureByTokens(
	UMaterialInterface* Material,
	std::initializer_list<const TCHAR*> IncludeTokens,
	std::initializer_list<const TCHAR*> ExcludeTokens,
	FString& OutSourcePath)
{
	if (!Material)
	{
		return nullptr;
	}
	if (UTexture2D* Texture = FindTextureInInstanceOverridesByTokens(Material, IncludeTokens, ExcludeTokens, OutSourcePath))
	{
		return Texture;
	}
	if (ShouldOnlyUseInstanceTextureOverrides(Material))
	{
		return nullptr;
	}

	TArray<UTexture*> UsedTextures;
	Material->GetUsedTextures(UsedTextures, EMaterialQualityLevel::High);
	for (UTexture* Texture : UsedTextures)
	{
		UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		if (!Texture2D)
		{
			continue;
		}

		const FString NameText = Texture2D->GetName() + TEXT(" ") + Texture2D->GetPathName() + TEXT(" ") + TextureSourceFilename(Texture2D);
		if (NameHasAnyToken(NameText, IncludeTokens) && !NameHasAnyToken(NameText, ExcludeTokens))
		{
			if (IsTextureSourceUsable(Texture2D, OutSourcePath))
			{
				return Texture2D;
			}
		}
	}
	return nullptr;
}

static void AddOriginalTextureChannel(FPBRMaterialSet& Set, const FName& Channel, UTexture2D* Texture)
{
	if (!Texture)
	{
		return;
	}
	FString SourcePath = TextureSourceFilename(Texture);
	if (!SourcePath.IsEmpty() && FPBRSceneMaterialReplacer::IsSupportedImagePath(SourcePath) && FPaths::FileExists(SourcePath))
	{
		Set.Channels.Add(Channel.ToString(), SourcePath);
	}
}

static bool TryGetMaterialColorParameter(UMaterialInterface* Material, const TArray<FName>& ParameterNames, FLinearColor& OutColor)
{
	if (!Material)
	{
		return false;
	}

	for (const FName& ParameterName : ParameterNames)
	{
		FLinearColor Value = FLinearColor::White;
		if (Material->GetVectorParameterValue(FMaterialParameterInfo(ParameterName), Value))
		{
			OutColor = Value;
			return true;
		}
	}
	return false;
}

static FLinearColor GetInheritedBaseColor(UMaterialInterface* Material)
{
	FLinearColor Color = FLinearColor::White;
	if (TryGetMaterialColorParameter(Material, {
		TEXT("BaseColor"),
		TEXT("Base Color"),
		TEXT("BaseColorTint"),
		TEXT("Base_Color"),
		TEXT("Diffuse"),
		TEXT("DiffuseColor"),
		TEXT("Diffuse Color"),
		TEXT("Albedo"),
		TEXT("AlbedoColor"),
		TEXT("Color"),
		TEXT("Tint"),
		TEXT("颜色"),
		TEXT("基础颜色")
	}, Color))
	{
		return Color;
	}

	if (UMaterial* BaseMaterial = Material ? Material->GetMaterial() : nullptr)
	{
		if (UMaterialEditorOnlyData* EditorData = BaseMaterial->GetEditorOnlyData())
		{
			if (UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(EditorData->BaseColor.Expression))
			{
				return VectorParameter->DefaultValue;
			}
			if (UMaterialExpressionConstant3Vector* ConstantColor = Cast<UMaterialExpressionConstant3Vector>(EditorData->BaseColor.Expression))
			{
				return ConstantColor->Constant;
			}
		}
	}

	return Color;
}

struct FPBRSceneTextureParameterBinding
{
	FName TextureParameterName;
	FName SwitchParameterName;
};

struct FPBRSceneSourceChannelState
{
	bool bHasPropertyChain = false;
	bool bHasTexture = false;
	bool bShouldBake = false;
	UTexture* Texture = nullptr;
	bool bHasColor = false;
	FLinearColor Color = FLinearColor::White;
	bool bHasScalar = false;
	float Scalar = 0.0f;
};

static TArray<FPBRSceneTextureParameterBinding> GetSceneTextureParameterBindings()
{
	return {
		{ FPBRMaterialParameters::BaseColorTexture, FPBRMaterialParameters::UseBaseColorTexture },
		{ FPBRMaterialParameters::NormalTexture, FPBRMaterialParameters::UseNormalTexture },
		{ FPBRMaterialParameters::RoughnessTexture, FPBRMaterialParameters::UseRoughnessTexture },
		{ FPBRMaterialParameters::SpecularTexture, FPBRMaterialParameters::UseSpecularTexture },
		{ FPBRMaterialParameters::MetallicTexture, FPBRMaterialParameters::UseMetallicTexture },
		{ FPBRMaterialParameters::AOTexture, FPBRMaterialParameters::UseAOTexture },
		{ FPBRMaterialParameters::OpacityTexture, FPBRMaterialParameters::UseOpacityTexture },
		{ FPBRMaterialParameters::HeightTexture, FPBRMaterialParameters::UseHeightTexture },
		{ FPBRMaterialParameters::EmissiveTexture, FPBRMaterialParameters::UseEmissiveTexture },
		{ FPBRMaterialParameters::WaterRippleTexture, FPBRMaterialParameters::UseWaterRippleTexture },
		{ FPBRMaterialParameters::GlassDirtTexture, FPBRMaterialParameters::UseGlassDirtTexture },
		{ FPBRMaterialParameters::GlassDistortionTexture, FPBRMaterialParameters::UseGlassDistortionTexture },
		{ FPBRMaterialParameters::GlassFrostedTexture, FPBRMaterialParameters::UseGlassFrostedTexture }
	};
}

static TArray<FName> GetSceneMigrationTextureParametersForTarget(UMaterialInstanceConstant* TargetInstance)
{
	const TArray<FName> CandidateTextureParameters = {
		FPBRMaterialParameters::BaseColorTexture,
		FPBRMaterialParameters::NormalTexture,
		FPBRMaterialParameters::RoughnessTexture,
		FPBRMaterialParameters::SpecularTexture,
		FPBRMaterialParameters::MetallicTexture,
		FPBRMaterialParameters::AOTexture,
		FPBRMaterialParameters::OpacityTexture,
		FPBRMaterialParameters::HeightTexture,
		FPBRMaterialParameters::EmissiveTexture
	};

	if (!TargetInstance)
	{
		return CandidateTextureParameters;
	}

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> TextureParameters;
	TargetInstance->GetAllParametersOfType(EMaterialParameterType::Texture, TextureParameters);
	if (TextureParameters.IsEmpty())
	{
		return CandidateTextureParameters;
	}

	TSet<FName> AvailableParameterNames;
	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : TextureParameters)
	{
		AvailableParameterNames.Add(Pair.Key.Name);
	}

	TArray<FName> Result;
	for (const FName& CandidateName : CandidateTextureParameters)
	{
		if (AvailableParameterNames.Contains(CandidateName))
		{
			Result.Add(CandidateName);
		}
	}
	return Result.IsEmpty() ? CandidateTextureParameters : Result;
}

static bool ReadSceneStaticSwitchParameter(const UMaterialInstanceConstant* Instance, const FName& ParameterName, bool bDefaultValue)
{
	if (!Instance)
	{
		return bDefaultValue;
	}

	bool bValue = bDefaultValue;
	FGuid ExpressionGuid;
	return Instance->GetStaticSwitchParameterValue(FMaterialParameterInfo(ParameterName), bValue, ExpressionGuid) ? bValue : bDefaultValue;
}

static TArray<FName> GetSceneMaterialUVChannels()
{
	return {
		TEXT("基础色"),
		TEXT("法线"),
		TEXT("粗糙度"),
		TEXT("高光"),
		TEXT("金属度"),
		TEXT("环境遮蔽"),
		TEXT("透明"),
		TEXT("高度"),
		TEXT("自发光"),
		TEXT("水纹")
	};
}

static FName GetSceneTextureSwitchParameterName(const FName& TextureParameterName)
{
	for (const FPBRSceneTextureParameterBinding& Binding : GetSceneTextureParameterBindings())
	{
		if (Binding.TextureParameterName == TextureParameterName)
		{
			return Binding.SwitchParameterName;
		}
	}
	return NAME_None;
}

static EMaterialSamplerType GetSceneSamplerTypeForTextureParameter(const FName& TextureParameterName)
{
	if (TextureParameterName == FPBRMaterialParameters::NormalTexture)
	{
		return EMaterialSamplerType::SAMPLERTYPE_Normal;
	}
	if (TextureParameterName == FPBRMaterialParameters::RoughnessTexture ||
		TextureParameterName == FPBRMaterialParameters::SpecularTexture ||
		TextureParameterName == FPBRMaterialParameters::MetallicTexture ||
		TextureParameterName == FPBRMaterialParameters::AOTexture ||
		TextureParameterName == FPBRMaterialParameters::OpacityTexture ||
		TextureParameterName == FPBRMaterialParameters::HeightTexture ||
		TextureParameterName == FPBRMaterialParameters::WaterRippleTexture ||
		TextureParameterName == FPBRMaterialParameters::GlassDirtTexture ||
		TextureParameterName == FPBRMaterialParameters::GlassDistortionTexture ||
		TextureParameterName == FPBRMaterialParameters::GlassFrostedTexture)
	{
		return EMaterialSamplerType::SAMPLERTYPE_Masks;
	}
	return EMaterialSamplerType::SAMPLERTYPE_Color;
}

static UTexture2D* LoadSceneEngineDefaultTexture(EMaterialSamplerType SamplerType)
{
	switch (SamplerType)
	{
	case EMaterialSamplerType::SAMPLERTYPE_Normal:
		return LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
	case EMaterialSamplerType::SAMPLERTYPE_Masks:
	case EMaterialSamplerType::SAMPLERTYPE_LinearColor:
		return LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks.DefaultDiffuse_TC_Masks"));
	case EMaterialSamplerType::SAMPLERTYPE_Color:
	default:
		return LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/T_Default_BaseColor.T_Default_BaseColor"));
	}
}

static bool IsSceneSourceTemplateDefaultTexture(const UTexture* Texture)
{
	if (!Texture)
	{
		return false;
	}

	const FString TexturePath = Texture->GetPathName();
	return TexturePath.StartsWith(TEXT("/Game/PBRStudio/Templates/DemoTextures/")) ||
		TexturePath.StartsWith(TEXT("/PBRStudio/Templates/DemoTextures/")) ||
		TexturePath.StartsWith(TEXT("/Engine/EngineMaterials/")) ||
		TexturePath.Contains(TEXT("/DemoTextures/"), ESearchCase::IgnoreCase);
}

static bool IsValidSceneSourceMigrationTexture(const UTexture* Texture)
{
	return Texture && !IsSceneSourceTemplateDefaultTexture(Texture);
}

static bool SceneNameContainsAnyToken(const FString& Name, const TArray<FString>& Tokens)
{
	for (const FString& Token : Tokens)
	{
		if (Name.Contains(Token, ESearchCase::IgnoreCase, ESearchDir::FromStart))
		{
			return true;
		}
	}
	return false;
}

static bool IsSceneExplicitEmissiveName(const FString& Name)
{
	return Name.Contains(TEXT("emissive"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("emission"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("selfillum"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("self_illum"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("glow"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("neon"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("led"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("lamp"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("bulb"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("lightbox"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("light_box"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("发光"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("自发光"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("霓虹"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("灯带"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("灯箱"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("灯管"), ESearchCase::IgnoreCase) ||
		Name.Contains(TEXT("灯泡"), ESearchCase::IgnoreCase);
}

static TArray<FString> GetSceneTextureParameterSearchTokens(const FName& TargetParameterName)
{
	if (TargetParameterName == FPBRMaterialParameters::BaseColorTexture)
	{
		return { TEXT("BaseColor"), TEXT("Base Color"), TEXT("Diffuse"), TEXT("Albedo"), TEXT("Color"), TEXT("基础色"), TEXT("基础颜色"), TEXT("漫反射"), TEXT("颜色") };
	}
	if (TargetParameterName == FPBRMaterialParameters::NormalTexture)
	{
		return { TEXT("Normal"), TEXT("Bump"), TEXT("法线") };
	}
	if (TargetParameterName == FPBRMaterialParameters::RoughnessTexture)
	{
		return { TEXT("Roughness"), TEXT("Rough"), TEXT("Glossiness"), TEXT("Gloss"), TEXT("粗糙") };
	}
	if (TargetParameterName == FPBRMaterialParameters::SpecularTexture)
	{
		return { TEXT("Specular"), TEXT("Spec"), TEXT("Reflection"), TEXT("高光"), TEXT("镜面") };
	}
	if (TargetParameterName == FPBRMaterialParameters::MetallicTexture)
	{
		return { TEXT("Metallic"), TEXT("Metalness"), TEXT("Metal"), TEXT("金属") };
	}
	if (TargetParameterName == FPBRMaterialParameters::AOTexture)
	{
		return { TEXT("AmbientOcclusion"), TEXT("Ambient Occlusion"), TEXT("Occlusion"), TEXT("AO"), TEXT("环境遮蔽") };
	}
	if (TargetParameterName == FPBRMaterialParameters::OpacityTexture)
	{
		return { TEXT("Opacity"), TEXT("Alpha"), TEXT("Transparency"), TEXT("透明") };
	}
	if (TargetParameterName == FPBRMaterialParameters::HeightTexture)
	{
		return { TEXT("Height"), TEXT("Displacement"), TEXT("Bump"), TEXT("高度"), TEXT("置换") };
	}
	if (TargetParameterName == FPBRMaterialParameters::EmissiveTexture)
	{
		return { TEXT("Emissive"), TEXT("Emission"), TEXT("Glow"), TEXT("SelfIllum"), TEXT("Neon"), TEXT("LED"), TEXT("自发光"), TEXT("发光"), TEXT("霓虹") };
	}
	return {};
}

static EMaterialProperty GetSceneMaterialPropertyForTextureParameter(const FName& TargetParameterName)
{
	if (TargetParameterName == FPBRMaterialParameters::BaseColorTexture) { return MP_BaseColor; }
	if (TargetParameterName == FPBRMaterialParameters::NormalTexture) { return MP_Normal; }
	if (TargetParameterName == FPBRMaterialParameters::RoughnessTexture) { return MP_Roughness; }
	if (TargetParameterName == FPBRMaterialParameters::SpecularTexture) { return MP_Specular; }
	if (TargetParameterName == FPBRMaterialParameters::MetallicTexture) { return MP_Metallic; }
	if (TargetParameterName == FPBRMaterialParameters::AOTexture) { return MP_AmbientOcclusion; }
	if (TargetParameterName == FPBRMaterialParameters::OpacityTexture) { return MP_Opacity; }
	if (TargetParameterName == FPBRMaterialParameters::HeightTexture) { return MP_WorldPositionOffset; }
	if (TargetParameterName == FPBRMaterialParameters::EmissiveTexture) { return MP_EmissiveColor; }
	return MP_MAX;
}

static bool IsSimpleSceneValueMigrationExpression(const UMaterialExpression* Expression)
{
	return Cast<UMaterialExpressionScalarParameter>(Expression) ||
		Cast<UMaterialExpressionVectorParameter>(Expression) ||
		Cast<UMaterialExpressionConstant>(Expression) ||
		Cast<UMaterialExpressionConstant2Vector>(Expression) ||
		Cast<UMaterialExpressionConstant3Vector>(Expression) ||
		Cast<UMaterialExpressionConstant4Vector>(Expression) ||
		Cast<UMaterialExpressionReroute>(Expression) ||
		Cast<UMaterialExpressionNamedRerouteDeclaration>(Expression) ||
		Cast<UMaterialExpressionNamedRerouteUsage>(Expression);
}

static bool IsSceneColorTargetParameter(const FName& TargetParameterName)
{
	return TargetParameterName == FPBRMaterialParameters::BaseColorTexture ||
		TargetParameterName == FPBRMaterialParameters::EmissiveTexture;
}

static bool IsSceneScalarTargetParameter(const FName& TargetParameterName)
{
	return TargetParameterName == FPBRMaterialParameters::RoughnessTexture ||
		TargetParameterName == FPBRMaterialParameters::SpecularTexture ||
		TargetParameterName == FPBRMaterialParameters::MetallicTexture ||
		TargetParameterName == FPBRMaterialParameters::OpacityTexture ||
		TargetParameterName == FPBRMaterialParameters::HeightTexture;
}

static bool ResolveSceneScalarExpressionValue(UMaterialInterface* SourceMaterial, const UMaterialExpression* Expression, float& OutValue)
{
	if (!Expression)
	{
		return false;
	}

	if (const UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
	{
		return SourceMaterial && SourceMaterial->GetScalarParameterValue(FMaterialParameterInfo(ScalarParameter->ParameterName), OutValue);
	}
	if (const UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
	{
		OutValue = Constant->R;
		return true;
	}
	if (const UMaterialExpressionConstant2Vector* Constant2 = Cast<UMaterialExpressionConstant2Vector>(Expression))
	{
		OutValue = Constant2->R;
		return true;
	}
	if (const UMaterialExpressionConstant3Vector* Constant3 = Cast<UMaterialExpressionConstant3Vector>(Expression))
	{
		OutValue = Constant3->Constant.R;
		return true;
	}
	if (const UMaterialExpressionConstant4Vector* Constant4 = Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		OutValue = Constant4->Constant.R;
		return true;
	}
	if (const UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		FLinearColor Color;
		if (SourceMaterial && SourceMaterial->GetVectorParameterValue(FMaterialParameterInfo(VectorParameter->ParameterName), Color))
		{
			OutValue = Color.R;
			return true;
		}
	}
	return false;
}

static bool ResolveSceneColorExpressionValue(UMaterialInterface* SourceMaterial, const UMaterialExpression* Expression, FLinearColor& OutColor)
{
	if (!Expression)
	{
		return false;
	}

	if (const UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		return SourceMaterial && SourceMaterial->GetVectorParameterValue(FMaterialParameterInfo(VectorParameter->ParameterName), OutColor);
	}
	if (const UMaterialExpressionConstant3Vector* Constant3 = Cast<UMaterialExpressionConstant3Vector>(Expression))
	{
		OutColor = Constant3->Constant;
		OutColor.A = 1.0f;
		return true;
	}
	if (const UMaterialExpressionConstant4Vector* Constant4 = Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		OutColor = Constant4->Constant;
		return true;
	}
	if (const UMaterialExpressionConstant2Vector* Constant2 = Cast<UMaterialExpressionConstant2Vector>(Expression))
	{
		OutColor = FLinearColor(Constant2->R, Constant2->G, 0.0f, 1.0f);
		return true;
	}

	float ScalarValue = 0.0f;
	if (ResolveSceneScalarExpressionValue(SourceMaterial, Expression, ScalarValue))
	{
		OutColor = FLinearColor(ScalarValue, ScalarValue, ScalarValue, 1.0f);
		return true;
	}
	return false;
}

static FPBRSceneSourceChannelState AnalyzeSceneSourceMaterialChannel(UMaterialInterface* SourceMaterial, const FName& TargetParameterName)
{
	FPBRSceneSourceChannelState State;
	if (!SourceMaterial)
	{
		return State;
	}

	const EMaterialProperty MaterialProperty = GetSceneMaterialPropertyForTextureParameter(TargetParameterName);
	UMaterial* SourceBaseMaterial = SourceMaterial->GetMaterial();
	if (!SourceBaseMaterial || MaterialProperty == MP_MAX)
	{
		return State;
	}

	FStaticParameterSet StaticParameters;
	SourceMaterial->GetStaticParameterValues(StaticParameters);

	TArray<UTexture*> ChainTextures;
	TArray<FName> TextureParameterNames;
	SourceMaterial->GetTexturesInPropertyChain(MaterialProperty, ChainTextures, &TextureParameterNames, &StaticParameters, ERHIFeatureLevel::Num, EMaterialQualityLevel::Num);

	TArray<UTexture*> UniqueTextures;
	bool bSawAnyChainTexture = false;
	for (int32 TextureIndex = 0; TextureIndex < ChainTextures.Num(); ++TextureIndex)
	{
		UTexture* ResolvedTexture = ChainTextures[TextureIndex];
		if (TextureParameterNames.IsValidIndex(TextureIndex) && !TextureParameterNames[TextureIndex].IsNone())
		{
			UTexture* ParameterTexture = nullptr;
			if (SourceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TextureParameterNames[TextureIndex]), ParameterTexture))
			{
				ResolvedTexture = ParameterTexture;
			}
		}

		if (ResolvedTexture)
		{
			bSawAnyChainTexture = true;
		}
		if (IsValidSceneSourceMigrationTexture(ResolvedTexture))
		{
			UniqueTextures.AddUnique(ResolvedTexture);
		}
	}

	TArray<UMaterialExpression*> ChainExpressions;
	SourceBaseMaterial->GetExpressionsInPropertyChain(MaterialProperty, ChainExpressions, &StaticParameters, ERHIFeatureLevel::Num, EMaterialQualityLevel::Num, ERHIShadingPath::Num, true);
	State.bHasPropertyChain = ChainExpressions.Num() > 0 || UniqueTextures.Num() > 0;
	State.bHasTexture = UniqueTextures.Num() > 0;

	bool bValueChainIsSimple = UniqueTextures.IsEmpty() && ChainExpressions.Num() > 0;
	for (const UMaterialExpression* Expression : ChainExpressions)
	{
		if (!Expression)
		{
			continue;
		}
		bValueChainIsSimple = bValueChainIsSimple && IsSimpleSceneValueMigrationExpression(Expression);
	}

	if (bSawAnyChainTexture && UniqueTextures.IsEmpty())
	{
		return State;
	}
	if (UniqueTextures.Num() > 1)
	{
		State.bShouldBake = true;
		return State;
	}
	if (UniqueTextures.Num() == 1)
	{
		State.Texture = UniqueTextures[0];
		return State;
	}

	if (bValueChainIsSimple)
	{
		for (const UMaterialExpression* Expression : ChainExpressions)
		{
			if (IsSceneColorTargetParameter(TargetParameterName) && ResolveSceneColorExpressionValue(SourceMaterial, Expression, State.Color))
			{
				State.bHasColor = true;
				return State;
			}
			if (IsSceneScalarTargetParameter(TargetParameterName) && ResolveSceneScalarExpressionValue(SourceMaterial, Expression, State.Scalar))
			{
				State.bHasScalar = true;
				return State;
			}
		}
	}

	if (State.bHasPropertyChain && !bValueChainIsSimple)
	{
		State.bShouldBake = true;
	}

	return State;
}

static void ApplySceneTextureMigrationDefaults(UMaterialInstanceConstant* TargetInstance, const FName& TargetParameterName)
{
	if (!TargetInstance)
	{
		return;
	}

	if (TargetParameterName == FPBRMaterialParameters::BaseColorTexture)
	{
		TargetInstance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor::White);
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	}
	else if (TargetParameterName == FPBRMaterialParameters::NormalTexture)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 1.0f);
	}
	else if (TargetParameterName == FPBRMaterialParameters::RoughnessTexture)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
	}
	else if (TargetParameterName == FPBRMaterialParameters::SpecularTexture)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 1.0f);
	}
	else if (TargetParameterName == FPBRMaterialParameters::MetallicTexture)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 1.0f);
	}
	else if (TargetParameterName == FPBRMaterialParameters::OpacityTexture)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
	}
	else if (TargetParameterName == FPBRMaterialParameters::EmissiveTexture)
	{
		TargetInstance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor::White);
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 1.0f);
	}
}

static bool ApplySimpleSceneSourceChannelValue(UMaterialInstanceConstant* TargetInstance, const FName& TargetParameterName, const FPBRSceneSourceChannelState& State)
{
	if (!TargetInstance)
	{
		return false;
	}

	if (TargetParameterName == FPBRMaterialParameters::BaseColorTexture && State.bHasColor)
	{
		TargetInstance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, State.Color);
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
		TargetInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseBaseColorTexture), false);
		return true;
	}
	if (TargetParameterName == FPBRMaterialParameters::RoughnessTexture && State.bHasScalar)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, FMath::Clamp(State.Scalar, 0.0f, 1.0f));
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		TargetInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseRoughnessTexture), false);
		return true;
	}
	if (TargetParameterName == FPBRMaterialParameters::SpecularTexture && State.bHasScalar)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, FMath::Clamp(State.Scalar, 0.0f, 1.0f));
		TargetInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseSpecularTexture), false);
		return true;
	}
	if (TargetParameterName == FPBRMaterialParameters::MetallicTexture && State.bHasScalar)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, FMath::Clamp(State.Scalar, 0.0f, 1.0f));
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 1.0f);
		TargetInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseMetallicTexture), false);
		return true;
	}
	if (TargetParameterName == FPBRMaterialParameters::OpacityTexture && State.bHasScalar)
	{
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, FMath::Clamp(State.Scalar, 0.0f, 1.0f));
		TargetInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseOpacityTexture), false);
		return true;
	}
	if (TargetParameterName == FPBRMaterialParameters::EmissiveTexture && State.bHasColor)
	{
		const float EmissiveStrength = FMath::Max3(State.Color.R, State.Color.G, State.Color.B);
		const float SafeStrength = FMath::Max(EmissiveStrength, 1.0f);
		const FLinearColor NormalizedColor = EmissiveStrength > 1.0f
			? FLinearColor(State.Color.R / SafeStrength, State.Color.G / SafeStrength, State.Color.B / SafeStrength, State.Color.A)
			: State.Color;
		TargetInstance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, NormalizedColor);
		TargetInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, EmissiveStrength > UE_SMALL_NUMBER ? SafeStrength : 0.0f);
		TargetInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), false);
		return true;
	}

	return false;
}

static UTexture* FindBestSceneSourceTextureParameter(UMaterialInterface* SourceMaterial, const FName& TargetParameterName)
{
	if (!SourceMaterial)
	{
		return nullptr;
	}

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> TextureParameters;
	SourceMaterial->GetAllParametersOfType(EMaterialParameterType::Texture, TextureParameters);
	if (TextureParameters.IsEmpty())
	{
		return nullptr;
	}

	const TArray<FString> SearchTokens = GetSceneTextureParameterSearchTokens(TargetParameterName);
	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : TextureParameters)
	{
		UTexture* Texture = Cast<UTexture>(Pair.Value.Value.AsTextureObject());
		if (!IsValidSceneSourceMigrationTexture(Texture))
		{
			continue;
		}

		if (Pair.Key.Name == TargetParameterName || SceneNameContainsAnyToken(Pair.Key.Name.ToString(), SearchTokens))
		{
			return Texture;
		}
	}

	if (TextureParameters.Num() == 1)
	{
		const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair = *TextureParameters.CreateConstIterator();
		UTexture* Texture = Cast<UTexture>(Pair.Value.Value.AsTextureObject());
		return IsValidSceneSourceMigrationTexture(Texture) ? Texture : nullptr;
	}

	return nullptr;
}

static FString BuildSceneBakedTexturePackagePath(const UPrimitiveComponent* Component, int32 SlotIndex, const FName& TargetParameterName, const FString& OutputRoot)
{
	const AActor* Owner = Component ? Component->GetOwner() : nullptr;
	const FString ActorName = FPBRMaterialInstanceFactory::SanitizeAssetName(Owner ? Owner->GetActorLabel() : TEXT("Actor"));
	const FString ComponentName = FPBRMaterialInstanceFactory::SanitizeAssetName(Component ? Component->GetName() : TEXT("Component"));
	const FString ParameterName = FPBRMaterialInstanceFactory::SanitizeAssetName(TargetParameterName.ToString());
	FString Root = OutputRoot.IsEmpty() ? FString(TEXT("/Game/PBRStudio/SceneReplaced")) : OutputRoot;
	Root.RemoveFromEnd(TEXT("/"));
	return Root / TEXT("_Baked") / FString::Printf(TEXT("T_PBRSR_%s_%s_Slot%d_%s"), *ActorName, *ComponentName, SlotIndex, *ParameterName);
}

static UTexture2D* BakeSceneSourceMaterialPropertyTexture(
	UMaterialInterface* SourceMaterial,
	const UPrimitiveComponent* Component,
	int32 SlotIndex,
	const FName& TargetParameterName,
	const FString& OutputRoot)
{
	if (!SourceMaterial)
	{
		return nullptr;
	}

	const EMaterialProperty MaterialProperty = GetSceneMaterialPropertyForTextureParameter(TargetParameterName);
	if (MaterialProperty == MP_MAX || !FMaterialUtilities::SupportsExport(IsOpaqueBlendMode(*SourceMaterial), MaterialProperty))
	{
		return nullptr;
	}

	const FString PackagePath = BuildSceneBakedTexturePackagePath(Component, SlotIndex, TargetParameterName, OutputRoot);
	if (UTexture2D* ExistingTexture = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(PackagePath)))
	{
		return ExistingTexture;
	}

	FIntPoint TextureSize = FMaterialUtilities::FindMaxTextureSize(SourceMaterial, FIntPoint(512, 512));
	TextureSize.X = FMath::Clamp(TextureSize.X, 256, 2048);
	TextureSize.Y = FMath::Clamp(TextureSize.Y, 256, 2048);

	FMeshData MeshSettings;
	MeshSettings.MeshDescription = nullptr;
	MeshSettings.TextureCoordinateBox = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));
	MeshSettings.TextureCoordinateIndex = 0;
	if (const UMeshComponent* MeshComponent = Cast<UMeshComponent>(Component))
	{
		MeshSettings.PrimitiveData = FPrimitiveData(MeshComponent);
	}

	FMaterialData MaterialSettings;
	MaterialSettings.Material = SourceMaterial;
	MaterialSettings.PropertySizes.Add(MaterialProperty, TextureSize);
	MaterialSettings.bTangentSpaceNormal = TargetParameterName == FPBRMaterialParameters::NormalTexture;
	MaterialSettings.BlendMode = SourceMaterial->GetBlendMode();
	MaterialSettings.BackgroundColor = TargetParameterName == FPBRMaterialParameters::NormalTexture
		? FColor(128, 128, 255, 255)
		: FColor::Black;

	TArray<FMeshData*> MeshSettingPtrs{ &MeshSettings };
	TArray<FMaterialData*> MaterialSettingPtrs{ &MaterialSettings };
	TArray<FBakeOutput> BakeOutputs;
	IMaterialBakingModule& BakingModule = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>(TEXT("MaterialBaking"));
	BakingModule.BakeMaterials(MaterialSettingPtrs, MeshSettingPtrs, BakeOutputs);
	if (BakeOutputs.IsEmpty())
	{
		return nullptr;
	}

	TArray<FColor>* Samples = BakeOutputs[0].PropertyData.Find(MaterialProperty);
	FIntPoint* BakedSize = BakeOutputs[0].PropertySizes.Find(MaterialProperty);
	if (!Samples || !BakedSize || Samples->Num() != BakedSize->X * BakedSize->Y)
	{
		return nullptr;
	}

	const EMaterialSamplerType SamplerType = GetSceneSamplerTypeForTextureParameter(TargetParameterName);
	const TextureCompressionSettings CompressionSettings = SamplerType == EMaterialSamplerType::SAMPLERTYPE_Normal
		? TC_Normalmap
		: (SamplerType == EMaterialSamplerType::SAMPLERTYPE_Color ? TC_Default : TC_Masks);
	const bool bSRGB = TargetParameterName == FPBRMaterialParameters::BaseColorTexture ||
		TargetParameterName == FPBRMaterialParameters::EmissiveTexture;

	UPackage* Package = CreatePackage(*PackagePath);
	UTexture2D* Texture = FMaterialUtilities::CreateTexture(Package, PackagePath, *BakedSize, *Samples, CompressionSettings, TEXTUREGROUP_World, RF_Public | RF_Standalone, bSRGB);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Texture);
	UEditorLoadingAndSavingUtils::SavePackages({ Texture->GetPackage() }, true);
	return Texture;
}

static void ApplySceneManagedMaterialDefaults(UMaterialInstanceConstant* Instance)
{
	if (!Instance)
	{
		return;
	}

	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, FLinearColor::White);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.5f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.5f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOValue, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOMultiplier, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor::Black);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 0.0f);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTemperature), false);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveTemperatureKelvin, 6500.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::HeightStrength, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::PixelDepthOffsetStrength, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoat, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatRoughness, 0.25f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Anisotropy, 0.0f);
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzColor, FLinearColor::White);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzStrength, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.45f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassOpacityFresnelStrength, 0.35f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassFresnelBaseReflection, 0.02f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassFresnelExp, 5.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassFrostedStrength, 0.0f);
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::GlassAbsorptionColor, FLinearColor(0.78f, 0.92f, 1.0f, 1.0f));
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassAbsorptionStrength, 0.15f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassEdgeTintStrength, 0.25f);
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::GlassDirtColor, FLinearColor(0.35f, 0.32f, 0.26f, 1.0f));
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDirtIntensity, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDirtOpacity, 0.35f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDirtRoughness, 0.65f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDistortionIntensity, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDistortionIORIntensity, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassShadowOpacity, 0.55f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassShadowHighlightClamp, 0.85f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassShadowNormalIntensity, 0.25f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassCausticsIntensity, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassCausticsScale, 24.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassCausticsSpeed, 0.12f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassRTOpacity, 0.35f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassRTRefractionAmount, 1.45f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassRTFrostedStrength, 0.0f);
	Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::WaterColor, FLinearColor(0.12f, 0.42f, 0.72f, 1.0f));
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedU, 0.18f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedV, 0.09f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleScale, 18.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleStrength, 0.8f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::FlakeScale, 35.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::FlakeIntensity, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVOffset, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVRotationDegrees, 0.0f);

	for (const FPBRSceneTextureParameterBinding& Binding : GetSceneTextureParameterBindings())
	{
		UTexture* DefaultTexture = LoadSceneEngineDefaultTexture(GetSceneSamplerTypeForTextureParameter(Binding.TextureParameterName));
		if (DefaultTexture)
		{
			Instance->SetTextureParameterValueEditorOnly(Binding.TextureParameterName, DefaultTexture);
		}
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(Binding.SwitchParameterName), false);
	}

	for (const FName& ChannelName : GetSceneMaterialUVChannels())
	{
		const FPBRChannelUVParameterNames ChannelUV = FPBRMaterialParameters::GetChannelUVNames(ChannelName);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(ChannelUV.UseIndependentUV), false);
		Instance->SetScalarParameterValueEditorOnly(ChannelUV.UTiling, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(ChannelUV.VTiling, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(ChannelUV.UOffset, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(ChannelUV.VOffset, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(ChannelUV.RotationDegrees, 0.0f);
	}
}

static void AppendSceneScalarParameterAliases(const FName& ParameterName, TArray<FName>& OutNames)
{
	OutNames.AddUnique(ParameterName);
	const auto AddAlias = [&OutNames](const TCHAR* Alias)
	{
		OutNames.AddUnique(FName(Alias));
	};

	if (ParameterName == FPBRMaterialParameters::RoughnessValue || ParameterName == FPBRMaterialParameters::RoughnessMultiplier)
	{
		AddAlias(TEXT("Roughness"));
	}
	else if (ParameterName == FPBRMaterialParameters::SpecularLevel)
	{
		AddAlias(TEXT("Specular"));
	}
	else if (ParameterName == FPBRMaterialParameters::Opacity)
	{
		AddAlias(TEXT("Opacity"));
	}
	else if (ParameterName == FPBRMaterialParameters::RefractionAmount)
	{
		AddAlias(TEXT("IOR"));
		AddAlias(TEXT("Refraction"));
	}
	else if (ParameterName == FPBRMaterialParameters::NormalStrength)
	{
		AddAlias(TEXT("Normal Intensity"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassOpacityFresnelStrength)
	{
		AddAlias(TEXT("Opacity Fresnel"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassFrostedStrength)
	{
		AddAlias(TEXT("Frosted Intensity"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassDirtIntensity)
	{
		AddAlias(TEXT("Dirt Intensity"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassDirtOpacity)
	{
		AddAlias(TEXT("Dirt Opacity"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassDirtRoughness)
	{
		AddAlias(TEXT("Dirt Roughness"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassDistortionIntensity)
	{
		AddAlias(TEXT("Distortion Intensity"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassDistortionIORIntensity)
	{
		AddAlias(TEXT("Distortion Intensity IOR"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassShadowOpacity)
	{
		AddAlias(TEXT("Shadow Opacity"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassShadowHighlightClamp)
	{
		AddAlias(TEXT("Shadow Opacity Clamp Highlight"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassShadowNormalIntensity)
	{
		AddAlias(TEXT("Shadow Normals Intensity"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassCausticsIntensity)
	{
		AddAlias(TEXT("Caustic Power"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassRTOpacity)
	{
		AddAlias(TEXT("RT Opacity"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassRTRefractionAmount)
	{
		AddAlias(TEXT("RT IOR"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassRTFrostedStrength)
	{
		AddAlias(TEXT("RT Frosted Intensity"));
	}
}

static void AppendSceneVectorParameterAliases(const FName& ParameterName, TArray<FName>& OutNames)
{
	OutNames.AddUnique(ParameterName);
	const auto AddAlias = [&OutNames](const TCHAR* Alias)
	{
		OutNames.AddUnique(FName(Alias));
	};

	if (ParameterName == FPBRMaterialParameters::BaseColorTint || ParameterName == FPBRMaterialParameters::GlassAbsorptionColor)
	{
		AddAlias(TEXT("Color"));
		AddAlias(TEXT("BaseColor"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassDirtColor)
	{
		AddAlias(TEXT("Dirt Color"));
	}
}

static void AppendSceneSwitchParameterAliases(const FName& ParameterName, TArray<FName>& OutNames)
{
	if (ParameterName.IsNone())
	{
		return;
	}

	OutNames.AddUnique(ParameterName);
	const auto AddAlias = [&OutNames](const TCHAR* Alias)
	{
		OutNames.AddUnique(FName(Alias));
	};

	if (ParameterName == FPBRMaterialParameters::UseBaseColorTexture)
	{
		AddAlias(TEXT("Base Color Texture ?"));
	}
	else if (ParameterName == FPBRMaterialParameters::UseNormalTexture)
	{
		AddAlias(TEXT("Normals?"));
	}
	else if (ParameterName == FPBRMaterialParameters::UseOpacityTexture)
	{
		AddAlias(TEXT("Opacity Mask ?"));
	}
	else if (ParameterName == FPBRMaterialParameters::UseGlassDirtTexture)
	{
		AddAlias(TEXT("Dirt?"));
	}
	else if (ParameterName == FPBRMaterialParameters::UseGlassDistortionTexture)
	{
		AddAlias(TEXT("Distortion ?"));
	}
	else if (ParameterName == FPBRMaterialParameters::UseGlassFrostedTexture)
	{
		AddAlias(TEXT("Frosted Glass ?"));
	}
}

static void AppendSceneTextureParameterAliases(const FName& ParameterName, TArray<FName>& OutNames)
{
	OutNames.AddUnique(ParameterName);
	const auto AddAlias = [&OutNames](const TCHAR* Alias)
	{
		OutNames.AddUnique(FName(Alias));
	};

	if (ParameterName == FPBRMaterialParameters::BaseColorTexture)
	{
		AddAlias(TEXT("Base Color Texture"));
	}
	else if (ParameterName == FPBRMaterialParameters::NormalTexture)
	{
		AddAlias(TEXT("Normal Texture"));
	}
	else if (ParameterName == FPBRMaterialParameters::OpacityTexture)
	{
		AddAlias(TEXT("Opacity Mask Texture"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassDirtTexture)
	{
		AddAlias(TEXT("DirtTexture"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassDistortionTexture)
	{
		AddAlias(TEXT("Distortion Texture"));
	}
	else if (ParameterName == FPBRMaterialParameters::GlassFrostedTexture)
	{
		AddAlias(TEXT("Frosted Normal Texture"));
	}
}

static void SetSceneScalarParameterEditorOnly(UMaterialInstanceConstant* Instance, const FName& ParameterName, float Value)
{
	TArray<FName> ParameterNames;
	AppendSceneScalarParameterAliases(ParameterName, ParameterNames);
	for (const FName& Name : ParameterNames)
	{
		Instance->SetScalarParameterValueEditorOnly(Name, Value);
	}
}

static void SetSceneVectorParameterEditorOnly(UMaterialInstanceConstant* Instance, const FName& ParameterName, const FLinearColor& Value)
{
	TArray<FName> ParameterNames;
	AppendSceneVectorParameterAliases(ParameterName, ParameterNames);
	for (const FName& Name : ParameterNames)
	{
		Instance->SetVectorParameterValueEditorOnly(Name, Value);
	}
}

static void SetSceneSwitchParameterEditorOnly(UMaterialInstanceConstant* Instance, const FName& ParameterName, bool bValue)
{
	TArray<FName> ParameterNames;
	AppendSceneSwitchParameterAliases(ParameterName, ParameterNames);
	for (const FName& Name : ParameterNames)
	{
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(Name), bValue);
	}
}

static bool SetSceneTextureParameterEditorOnly(UMaterialInstanceConstant* Instance, const FName& ParameterName, UTexture* Texture)
{
	TArray<FName> ParameterNames;
	AppendSceneTextureParameterAliases(ParameterName, ParameterNames);
	for (const FName& Name : ParameterNames)
	{
		Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Name), Texture);
	}

	TArray<FName> SwitchNames;
	AppendSceneSwitchParameterAliases(GetSceneTextureSwitchParameterName(ParameterName), SwitchNames);
	for (const FName& SwitchName : SwitchNames)
	{
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(SwitchName), Texture != nullptr);
	}
	return SwitchNames.Num() > 0;
}

static void ApplyAdvancedGlassParameterDefaults(UMaterialInstanceConstant* Instance)
{
	if (!Instance)
	{
		return;
	}

	SetSceneVectorParameterEditorOnly(Instance, FPBRMaterialParameters::GlassAbsorptionColor, FLinearColor(0.78f, 0.92f, 1.0f, 1.0f));
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::Opacity, 0.35f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::RefractionAmount, 1.45f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::NormalStrength, 1.0f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassOpacityFresnelStrength, 0.35f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassFrostedStrength, 0.0f);
	SetSceneVectorParameterEditorOnly(Instance, FPBRMaterialParameters::GlassDirtColor, FLinearColor(0.35f, 0.32f, 0.26f, 1.0f));
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassDirtIntensity, 0.0f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassDirtOpacity, 0.35f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassDirtRoughness, 0.65f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassDistortionIntensity, 0.0f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassDistortionIORIntensity, 0.0f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassShadowOpacity, 1.0f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassShadowHighlightClamp, -0.7f);
	Instance->SetScalarParameterValueEditorOnly(FName(TEXT("Shadow Clamp")), 2.0f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassShadowNormalIntensity, 1.0f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassCausticsIntensity, 0.0f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassRTOpacity, 0.35f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassRTRefractionAmount, 1.45f);
	SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::GlassRTFrostedStrength, 0.0f);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FName(TEXT("Shadow ?"))), true);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FName(TEXT("Shadow Fake Caustic ?"))), true);
	SetSceneSwitchParameterEditorOnly(Instance, FPBRMaterialParameters::UseGlassDirtTexture, false);
	SetSceneSwitchParameterEditorOnly(Instance, FPBRMaterialParameters::UseGlassDistortionTexture, false);
	SetSceneSwitchParameterEditorOnly(Instance, FPBRMaterialParameters::UseGlassFrostedTexture, false);

	FMaterialInstanceBasePropertyOverrides BaseOverrides = Instance->BasePropertyOverrides;
	BaseOverrides.bOverride_CastDynamicShadowAsMasked = true;
	BaseOverrides.bCastDynamicShadowAsMasked = true;
	Instance->UpdateStaticPermutation(Instance->GetStaticParameters(), BaseOverrides, true);
	Instance->MarkPackageDirty();
}

static void EnableAdvancedGlassComponentShadowSettings(UPrimitiveComponent* Component)
{
	if (!Component)
	{
		return;
	}

	Component->Modify();
	Component->SetCastShadow(true);
	Component->bCastDynamicShadow = true;
	Component->bCastStaticShadow = true;
	Component->bCastVolumetricTranslucentShadow = true;
	Component->MarkRenderStateDirty();
}

static void MigrateSceneSourceMaterialToManagedInstance(
	UMaterialInterface* SourceMaterial,
	UMaterialInstanceConstant* TargetInstance,
	const UPrimitiveComponent* Component,
	int32 SlotIndex,
	const FString& OutputRoot)
{
	if (!SourceMaterial || !TargetInstance)
	{
		return;
	}

	const TArray<FName> CandidateTextureParameters = GetSceneMigrationTextureParametersForTarget(TargetInstance);

	for (const FName& TargetParameterName : CandidateTextureParameters)
	{
		FPBRSceneSourceChannelState ChannelState = AnalyzeSceneSourceMaterialChannel(SourceMaterial, TargetParameterName);
		if (!ChannelState.bHasPropertyChain)
		{
			UTexture* SourceTexture = FindBestSceneSourceTextureParameter(SourceMaterial, TargetParameterName);
			if (!SourceTexture)
			{
				continue;
			}
			ChannelState.bHasTexture = true;
			ChannelState.Texture = SourceTexture;
		}

		UTexture* SourceTexture = nullptr;
		if (ChannelState.bShouldBake)
		{
			SourceTexture = BakeSceneSourceMaterialPropertyTexture(SourceMaterial, Component, SlotIndex, TargetParameterName, OutputRoot);
			if (!SourceTexture)
			{
				UE_LOG(LogTemp, Warning, TEXT("PBRStudio: Failed to bake source material channel %s from %s"), *TargetParameterName.ToString(), *SourceMaterial->GetName());
			}
		}
		if (!SourceTexture)
		{
			SourceTexture = ChannelState.Texture;
		}

		if (SourceTexture)
		{
			SetSceneTextureParameterEditorOnly(TargetInstance, TargetParameterName, SourceTexture);
			ApplySceneTextureMigrationDefaults(TargetInstance, TargetParameterName);
			continue;
		}

		ApplySimpleSceneSourceChannelValue(TargetInstance, TargetParameterName, ChannelState);
	}
}

static bool SyncSceneTextureUsageSwitches(UMaterialInstanceConstant* Instance)
{
	if (!Instance)
	{
		return false;
	}

	bool bChanged = false;
	for (const FPBRSceneTextureParameterBinding& Binding : GetSceneTextureParameterBindings())
	{
		UTexture* Texture = nullptr;
		Instance->GetTextureParameterValue(FMaterialParameterInfo(Binding.TextureParameterName), Texture);
		const bool bShouldUseTexture = IsValidSceneSourceMigrationTexture(Texture);
		if (ReadSceneStaticSwitchParameter(Instance, Binding.SwitchParameterName, false) != bShouldUseTexture)
		{
			SetSceneSwitchParameterEditorOnly(Instance, Binding.SwitchParameterName, bShouldUseTexture);
			bChanged = true;
		}
	}
	return bChanged;
}

static FString BuildSceneManagedInstancePackagePath(const FString& OutputRoot, const FString& OutputName)
{
	FString Root = OutputRoot.IsEmpty() ? FString(TEXT("/Game/PBRStudio/SceneReplaced")) : OutputRoot;
	Root.RemoveFromEnd(TEXT("/"));
	const FString CleanName = FPBRMaterialInstanceFactory::SanitizeAssetName(OutputName);
	return Root / CleanName / CleanName;
}

static EPBRMaterialType ResolveSceneManagedMaterialType(const FPBRSceneMaterialCandidate& Candidate, UMaterialInterface* SourceMaterial)
{
	if (Candidate.ReplacementKind == EPBRSceneReplacementKind::Glass)
	{
		return EPBRMaterialType::Glass;
	}
	if (Candidate.ReplacementKind == EPBRSceneReplacementKind::Emissive)
	{
		return EPBRMaterialType::Emissive;
	}
	if (Candidate.bLooksTransparent || (SourceMaterial && !IsOpaqueBlendMode(*SourceMaterial)))
	{
		return EPBRMaterialType::Transparent;
	}
	return EPBRMaterialType::Standard;
}

static UMaterialInstanceConstant* CreateOrUpdateSceneManagedReplacementInstance(
	const FPBRSceneMaterialCandidate& Candidate,
	const FString& OutputName,
	const FPBRSceneReplaceSettings& Settings,
	UMaterialInterface* SourceMaterial,
	FString& OutMessage)
{
	OutMessage.Empty();
	const EPBRMaterialType MaterialType = ResolveSceneManagedMaterialType(Candidate, SourceMaterial);
	FString ParentMessage;
	UMaterial* ParentMaterial = FPBRMaterialTemplateManager::EnsureTemplateMaterial(MaterialType, ParentMessage);
	if (!ParentMaterial)
	{
		OutMessage = ParentMessage.IsEmpty() ? TEXT("无法确定 PBRStudio 母材质") : ParentMessage;
		return nullptr;
	}

	const FString InstancePackagePath = BuildSceneManagedInstancePackagePath(Settings.OutputRoot, OutputName);
	const FString InstanceAssetName = FPackageName::GetLongPackageAssetName(InstancePackagePath);
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(InstancePackagePath));

	if (!Instance)
	{
		UPackage* Package = CreatePackage(*InstancePackagePath);
		if (!Package)
		{
			OutMessage = FString::Printf(TEXT("无法创建资源包 %s"), *InstancePackagePath);
			return nullptr;
		}

		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = ParentMaterial;
		UObject* CreatedObject = Factory->FactoryCreateNew(
			UMaterialInstanceConstant::StaticClass(),
			Package,
			FName(*InstanceAssetName),
			RF_Public | RF_Standalone,
			nullptr,
			GWarn);
		Instance = Cast<UMaterialInstanceConstant>(CreatedObject);
		if (!Instance)
		{
			OutMessage = TEXT("材质实例创建失败");
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Instance);
		Package->SetDirtyFlag(true);
	}

	Instance->Modify();
	Instance->SetParentEditorOnly(ParentMaterial);
	ApplySceneManagedMaterialDefaults(Instance);

	if (MaterialType == EPBRMaterialType::Glass)
	{
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.02f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.5f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.35f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.45f);
		ApplyAdvancedGlassParameterDefaults(Instance);
		EnableAdvancedGlassComponentShadowSettings(Candidate.Slots.Num() > 0 ? Candidate.Slots[0].Component.Get() : nullptr);
	}
	else if (Candidate.ReplacementKind == EPBRSceneReplacementKind::Emissive || Candidate.bLooksEmissive)
	{
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 1.0f);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTemperature), false);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
	}

	MigrateSceneSourceMaterialToManagedInstance(
		SourceMaterial,
		Instance,
		Candidate.Slots.Num() > 0 ? Candidate.Slots[0].Component.Get() : nullptr,
		Candidate.Slots.Num() > 0 ? Candidate.Slots[0].MaterialIndex : INDEX_NONE,
		Settings.OutputRoot);
	SyncSceneTextureUsageSwitches(Instance);

	if (!ReadSceneStaticSwitchParameter(Instance, FPBRMaterialParameters::UseBaseColorTexture, false))
	{
		SetSceneVectorParameterEditorOnly(Instance, FPBRMaterialParameters::BaseColorTint, Candidate.InheritedBaseColor);
		SetSceneScalarParameterEditorOnly(Instance, FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	}

	Instance->PostEditChange();
	Instance->MarkPackageDirty();
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	UEditorLoadingAndSavingUtils::SavePackages({ Instance->GetPackage() }, true);

	OutMessage = FString::Printf(TEXT("已按 PBRStudio 统一母材质逻辑转换材质实例：%s"), *Instance->GetName());
	return Instance;
}

static void RefreshSceneMaterialInstance(UMaterialInterface* Material)
{
	if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Material))
	{
		UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
		Instance->PostEditChange();
		Instance->MarkPackageDirty();
	}
	else if (UMaterial* BaseMaterial = Cast<UMaterial>(Material))
	{
		BaseMaterial->PostEditChange();
		BaseMaterial->MarkPackageDirty();
	}
	else if (Material)
	{
		Material->MarkPackageDirty();
	}
}

static void RefreshPrimitiveAfterMaterialChange(UPrimitiveComponent* Component, bool bImmediate = false)
{
	if (!Component)
	{
		return;
	}
	Component->InvalidateLightingCache();
	Component->MarkRenderStateDirty();
	if (bImmediate)
	{
		Component->RecreateRenderState_Concurrent();
		Component->PostEditChange();
	}
	Component->MarkPackageDirty();
}

static void AddPBRStudioMaterialAssetIfMatched(const FString& AssetPath, TSet<TWeakObjectPtr<UMaterialInterface>>& Materials)
{
	if (AssetPath.IsEmpty())
	{
		return;
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (UMaterialInterface* Material = Cast<UMaterialInterface>(Asset))
	{
		if (FPBRSceneMaterialReplacer::IsPBRStudioGeneratedMaterial(Material))
		{
			Materials.Add(Material);
		}
	}
}

static void AddPBRStudioMaterialAssetsUnderRoot(const FString& RootPath, TSet<TWeakObjectPtr<UMaterialInterface>>& Materials)
{
	if (RootPath.IsEmpty())
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*RootPath), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		const FName ClassName = Asset.AssetClassPath.GetAssetName();
		if (ClassName == TEXT("Material") ||
			ClassName == TEXT("MaterialInstance") ||
			ClassName == TEXT("MaterialInstanceConstant"))
		{
			AddPBRStudioMaterialAssetIfMatched(Asset.GetObjectPathString(), Materials);
		}
	}
}

static void AddLoadedPBRStudioMaterials(TSet<TWeakObjectPtr<UMaterialInterface>>& Materials)
{
	for (TObjectIterator<UMaterialInterface> It; It; ++It)
	{
		UMaterialInterface* Material = *It;
		if (Material && FPBRSceneMaterialReplacer::IsPBRStudioGeneratedMaterial(Material))
		{
			Materials.Add(Material);
		}
	}
}

static void RefreshPBRStudioTexturesUnderRoot(const FString& RootPath)
{
	const TArray<FString> AssetPaths = UEditorAssetLibrary::ListAssets(RootPath, true, false);
	for (const FString& AssetPath : AssetPaths)
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
		{
			Texture->PreEditChange(nullptr);
			Texture->PostEditChange();
			Texture->MarkPackageDirty();
		}
	}
}

static void RefreshSceneAfterMaterialReplacement(const TSet<TWeakObjectPtr<UPrimitiveComponent>>& Components, const TSet<TWeakObjectPtr<UMaterialInterface>>& Materials)
{
	for (const TWeakObjectPtr<UMaterialInterface>& MaterialPtr : Materials)
	{
		if (UMaterialInterface* Material = MaterialPtr.Get())
		{
			RefreshSceneMaterialInstance(Material);
		}
	}
	for (const TWeakObjectPtr<UPrimitiveComponent>& ComponentPtr : Components)
	{
		if (UPrimitiveComponent* Component = ComponentPtr.Get())
		{
			RefreshPrimitiveAfterMaterialChange(Component);
		}
	}
	FEditorDelegates::RefreshEditor.Broadcast();
	FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void FPBRSceneMaterialReplacer::RefreshCurrentLevelMaterialAssignments()
{
	if (!GEditor)
	{
		return;
	}

	TSet<TWeakObjectPtr<UPrimitiveComponent>> Components;
	TSet<TWeakObjectPtr<UMaterialInterface>> Materials;

	const TArray<FString> MaterialRoots = {
		TEXT("/Game/Materials/PBR"),
		TEXT("/Game/PBRStudio/SceneReplaced"),
		TEXT("/Game/PBRStudio/Templates"),
		TEXT("/Game/PBRStudio/SpecialMaterials"),
		TEXT("/Game/PBRStudio/Substrate")
	};
	for (const FString& Root : MaterialRoots)
	{
		AddPBRStudioMaterialAssetsUnderRoot(Root, Materials);
	}
	AddLoadedPBRStudioMaterials(Materials);
	RefreshPBRStudioTexturesUnderRoot(TEXT("/Game/PBRStudio"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			TArray<UPrimitiveComponent*> PrimitiveComponents;
			Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			for (UPrimitiveComponent* Component : PrimitiveComponents)
			{
				if (!Component)
				{
					continue;
				}

				const int32 MaterialCount = Component->GetNumMaterials();
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					if (UMaterialInterface* Material = Component->GetMaterial(MaterialIndex))
					{
						Materials.Add(Material);
						Components.Add(Component);
						Component->SetMaterial(MaterialIndex, Material);
					}
				}
			}
		}
	}

	for (const TWeakObjectPtr<UMaterialInterface>& MaterialPtr : Materials)
	{
		if (UMaterialInterface* Material = MaterialPtr.Get())
		{
			RefreshSceneMaterialInstance(Material);
		}
	}

	for (const TWeakObjectPtr<UPrimitiveComponent>& ComponentPtr : Components)
	{
		if (UPrimitiveComponent* Component = ComponentPtr.Get())
		{
			RefreshPrimitiveAfterMaterialChange(Component, true);
		}
	}

	FEditorDelegates::RefreshEditor.Broadcast();
	FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	GEditor->RedrawAllViewports(false);
	FlushRenderingCommands();
}

static UMaterialExpressionTextureSampleParameter2D* AddSceneTextureParameter(
	UMaterial* Material,
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
	Node->Texture = GetSceneDefaultTextureForSampler(SamplerType);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionScalarParameter* AddSceneScalarParameter(
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

static UMaterialExpressionVectorParameter* AddSceneVectorParameter(
	UMaterial* Material,
	const FName& ParameterName,
	const FName& GroupName,
	const FLinearColor& DefaultValue,
	int32 SortPriority,
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

static UMaterialExpressionMultiply* AddSceneMultiply(UMaterial* Material, int32 X, int32 Y)
{
	UMaterialExpressionMultiply* Node = NewObject<UMaterialExpressionMultiply>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionAdd* AddSceneAdd(UMaterial* Material, int32 X, int32 Y)
{
	UMaterialExpressionAdd* Node = NewObject<UMaterialExpressionAdd>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionLinearInterpolate* AddSceneLerp(UMaterial* Material, int32 X, int32 Y)
{
	UMaterialExpressionLinearInterpolate* Node = NewObject<UMaterialExpressionLinearInterpolate>(Material);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionDivide* AddSceneDivide(UMaterial* Material, int32 X, int32 Y, float ConstB)
{
	UMaterialExpressionDivide* Node = NewObject<UMaterialExpressionDivide>(Material);
	Node->ConstB = ConstB;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionConstant* AddSceneConstant(UMaterial* Material, float Value, int32 X, int32 Y)
{
	UMaterialExpressionConstant* Node = NewObject<UMaterialExpressionConstant>(Material);
	Node->R = Value;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionConstant3Vector* AddSceneConstant3Vector(UMaterial* Material, const FLinearColor& Value, int32 X, int32 Y)
{
	UMaterialExpressionConstant3Vector* Node = NewObject<UMaterialExpressionConstant3Vector>(Material);
	Node->Constant = Value;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static UMaterialExpressionStaticSwitchParameter* AddSceneStaticSwitchParameter(
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

static UMaterialExpression* BuildSceneSharedUVControls(UMaterial* Material, const FName& GroupUV, int32 X, int32 Y)
{
	UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
	TexCoord->MaterialExpressionEditorX = X;
	TexCoord->MaterialExpressionEditorY = Y;
	Material->GetExpressionCollection().AddExpression(TexCoord);

	UMaterialExpressionScalarParameter* UTiling = AddSceneScalarParameter(Material, FPBRMaterialParameters::UVUTiling, GroupUV, 10, 1.0f, X, Y + 140);
	UMaterialExpressionScalarParameter* VTiling = AddSceneScalarParameter(Material, FPBRMaterialParameters::UVVTiling, GroupUV, 20, 1.0f, X, Y + 260);
	UMaterialExpressionScalarParameter* UOffset = AddSceneScalarParameter(Material, FPBRMaterialParameters::UVUOffset, GroupUV, 30, 0.0f, X, Y + 380);
	UMaterialExpressionScalarParameter* VOffset = AddSceneScalarParameter(Material, FPBRMaterialParameters::UVVOffset, GroupUV, 40, 0.0f, X, Y + 500);
	UMaterialExpressionScalarParameter* RotationDegrees = AddSceneScalarParameter(Material, FPBRMaterialParameters::UVRotationDegrees, GroupUV, 50, 0.0f, X, Y + 620);

	UMaterialExpressionAppendVector* TilingUV = NewObject<UMaterialExpressionAppendVector>(Material);
	TilingUV->MaterialExpressionEditorX = X + 260;
	TilingUV->MaterialExpressionEditorY = Y + 160;
	TilingUV->A.Connect(0, UTiling);
	TilingUV->B.Connect(0, VTiling);
	Material->GetExpressionCollection().AddExpression(TilingUV);

	UMaterialExpressionAppendVector* OffsetUV = NewObject<UMaterialExpressionAppendVector>(Material);
	OffsetUV->MaterialExpressionEditorX = X + 260;
	OffsetUV->MaterialExpressionEditorY = Y + 380;
	OffsetUV->A.Connect(0, UOffset);
	OffsetUV->B.Connect(0, VOffset);
	Material->GetExpressionCollection().AddExpression(OffsetUV);

	UMaterialExpressionMultiply* TilingMultiply = AddSceneMultiply(Material, X + 520, Y + 80);
	TilingMultiply->A.Connect(0, TexCoord);
	TilingMultiply->B.Connect(0, TilingUV);

	UMaterialExpressionAdd* OffsetAdd = AddSceneAdd(Material, X + 760, Y + 80);
	OffsetAdd->A.Connect(0, TilingMultiply);
	OffsetAdd->B.Connect(0, OffsetUV);

	UMaterialExpressionMultiply* DegreesToRadians = AddSceneMultiply(Material, X + 760, Y + 300);
	DegreesToRadians->A.Connect(0, RotationDegrees);
	DegreesToRadians->ConstB = UE_PI / 180.0f;

	UMaterialExpressionRotator* Rotator = NewObject<UMaterialExpressionRotator>(Material);
	Rotator->CenterX = 0.5f;
	Rotator->CenterY = 0.5f;
	Rotator->Speed = 1.0f;
	Rotator->Coordinate.Connect(0, OffsetAdd);
	Rotator->Time.Connect(0, DegreesToRadians);
	Rotator->MaterialExpressionEditorX = X + 1000;
	Rotator->MaterialExpressionEditorY = Y + 80;
	Material->GetExpressionCollection().AddExpression(Rotator);
	return Rotator;
}

static void ConnectSceneTextureSamplesToUV(UMaterial* Material, UMaterialExpression* UVExpression)
{
	if (!Material || !UVExpression)
	{
		return;
	}
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (UMaterialExpressionTextureSample* TextureSample = Cast<UMaterialExpressionTextureSample>(Expression))
		{
			TextureSample->Coordinates.Connect(0, UVExpression);
		}
	}
}

static UMaterialExpressionComment* AddSceneComment(UMaterial* Material, const FString& Text, int32 X, int32 Y, int32 SizeX, int32 SizeY, const FLinearColor& Color)
{
	UMaterialExpressionComment* Node = NewObject<UMaterialExpressionComment>(Material);
	Node->Text = Text;
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Node->SizeX = SizeX;
	Node->SizeY = SizeY;
	Node->CommentColor = Color;
	Node->FontSize = 18;
	Node->bGroupMode = true;
	Material->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static void SaveSceneReplaceMaterial(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}
	TArray<UPackage*> Packages = { Material->GetPackage() };
	UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
}

static void ResetSceneReplaceMaterialGraph(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	Material->MaterialDomain = MD_Surface;
	Material->BlendMode = BLEND_Opaque;
	Material->TwoSided = false;
	Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	Material->bEnableTessellation = false;
	Material->bEnableDisplacementFade = false;
	Material->bAlwaysEvaluateWorldPositionOffset = false;
	Material->MaxWorldPositionOffsetDisplacement = 0.0f;
	Material->DisplacementScaling.Magnitude = 0.0f;
	Material->DisplacementScaling.Center = 0.5f;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		return;
	}

	Material->GetExpressionCollection().Empty();
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

static void ConfigureSceneReplaceMaterialType(UMaterial* Material, EPBRSceneReplacementKind Kind)
{
	if (!Material)
	{
		return;
	}

	Material->MaterialDomain = MD_Surface;
	Material->TwoSided = false;
	Material->bEnableTessellation = false;
	Material->bEnableDisplacementFade = false;
	Material->bAlwaysEvaluateWorldPositionOffset = false;
	Material->MaxWorldPositionOffsetDisplacement = 0.0f;
	Material->DisplacementScaling.Magnitude = 0.0f;
	Material->DisplacementScaling.Center = 0.5f;

	if (Kind == EPBRSceneReplacementKind::Glass)
	{
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
		Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
	}
	else if (Kind == EPBRSceneReplacementKind::Emissive)
	{
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	}
	else
	{
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
	}
}

static bool BuildSceneReplaceMaterialGraph(UMaterial* Material, FString& OutMessage)
{
	if (!Material)
	{
		OutMessage = TEXT("场景替换母材质为空");
		return false;
	}

	ResetSceneReplaceMaterialGraph(Material);
	ConfigureSceneReplaceMaterialType(Material, EPBRSceneReplacementKind::BPR);

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("无法访问场景替换母材质编辑数据");
		return false;
	}

	const FName GroupBase(TEXT("01 基础颜色"));
	const FName GroupNormal(TEXT("02 法线"));
	const FName GroupMasks(TEXT("03 粗糙金属AO"));
	const FName GroupSwitch(TEXT("04 贴图开关"));
	const FName GroupUV(TEXT("05 UV 调整"));
	const FName GroupEmissive(TEXT("06 自发光"));
	const FName GroupHeight(TEXT("07 高度保留"));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupBase.ToString(), 10));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupNormal.ToString(), 20));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupMasks.ToString(), 30));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupSwitch.ToString(), 40));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupUV.ToString(), 50));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupEmissive.ToString(), 60));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupHeight.ToString(), 70));

	AddSceneComment(Material, TEXT("01 基础颜色: 保留导入材质的颜色贴图，也可作为无基础色材质的灰色占位。"), -980, -360, 760, 560, FLinearColor(0.12f, 0.22f, 0.32f, 0.35f));
	AddSceneComment(Material, TEXT("02 法线: 自动生成或继承的 DirectX 法线贴图，强度可在实例中调整。"), -980, 120, 760, 260, FLinearColor(0.10f, 0.28f, 0.20f, 0.35f));
	AddSceneComment(Material, TEXT("03 粗糙/金属/AO: 由颜色图估算，用户可在实例中继续微调乘数。"), -980, 420, 760, 760, FLinearColor(0.30f, 0.24f, 0.10f, 0.35f));
	AddSceneComment(Material, TEXT("04 贴图开关: 在材质实例里可单独关闭贴图，回退到数值参数。"), -40, -360, 520, 520, FLinearColor(0.16f, 0.18f, 0.28f, 0.35f));
	AddSceneComment(Material, TEXT("05 UV 调整: 所有导入贴图共用平铺、偏移和旋转。"), -1780, -360, 700, 760, FLinearColor(0.18f, 0.18f, 0.18f, 0.35f));
	AddSceneComment(Material, TEXT("06 自发光: V-Ray/Corona 灯光材质会自动接入，强度默认更高。"), -980, 1140, 760, 600, FLinearColor(0.32f, 0.16f, 0.08f, 0.35f));
	AddSceneComment(Material, TEXT("07 高度保留: 默认不参与位移，避免导入模型开裂。"), -980, 1680, 620, 420, FLinearColor(0.18f, 0.18f, 0.18f, 0.35f));

	UMaterialExpression* SharedUV = BuildSceneSharedUVControls(Material, GroupUV, -1700, -260);

	UMaterialExpressionTextureSampleParameter2D* BaseTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::BaseColorTexture, GroupBase, 10, -900, -260);
	UMaterialExpressionVectorParameter* BaseTint = AddSceneVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupBase, FLinearColor::White, 20, -900, -100);
	UMaterialExpressionScalarParameter* BaseIntensity = AddSceneScalarParameter(Material, FPBRMaterialParameters::BaseColorIntensity, GroupBase, 30, 1.0f, -900, 60);
	UMaterialExpressionMultiply* TintedBase = AddSceneMultiply(Material, -600, -240);
	TintedBase->A.Connect(0, BaseTex);
	TintedBase->B.Connect(0, BaseTint);
	UMaterialExpressionMultiply* FinalBase = AddSceneMultiply(Material, -320, -220);
	FinalBase->A.Connect(0, TintedBase);
	FinalBase->B.Connect(0, BaseIntensity);
	UMaterialExpressionMultiply* SolidBase = AddSceneMultiply(Material, -320, -60);
	SolidBase->A.Connect(0, BaseTint);
	SolidBase->B.Connect(0, BaseIntensity);
	UMaterialExpressionStaticSwitchParameter* UseBaseColorTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseBaseColorTexture, GroupSwitch, 10, true, -40, -260);
	UseBaseColorTexture->A.Connect(0, FinalBase);
	UseBaseColorTexture->B.Connect(0, SolidBase);
	EditorData->BaseColor.Connect(0, UseBaseColorTexture);

	UMaterialExpressionTextureSampleParameter2D* NormalTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::NormalTexture, GroupNormal, 10, -900, 220, EMaterialSamplerType::SAMPLERTYPE_Normal);
	UMaterialExpressionScalarParameter* NormalStrength = AddSceneScalarParameter(Material, FPBRMaterialParameters::NormalStrength, GroupNormal, 20, 1.0f, -900, 360);
	UMaterialExpressionConstant3Vector* FlatNormalValue = AddSceneConstant3Vector(Material, FLinearColor(0.0f, 0.0f, 1.0f), -620, 300);
	UMaterialExpressionLinearInterpolate* NormalWithStrength = AddSceneLerp(Material, -360, 220);
	NormalWithStrength->A.Connect(0, FlatNormalValue);
	NormalWithStrength->B.Connect(0, NormalTex);
	NormalWithStrength->Alpha.Connect(0, NormalStrength);
	UMaterialExpressionStaticSwitchParameter* UseNormalTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseNormalTexture, GroupSwitch, 20, true, -40, -120);
	UseNormalTexture->A.Connect(0, NormalWithStrength);
	UseNormalTexture->B.Connect(0, FlatNormalValue);
	EditorData->Normal.Connect(0, UseNormalTexture);

	UMaterialExpressionTextureSampleParameter2D* RoughnessTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::RoughnessTexture, GroupMasks, 10, -900, 520, EMaterialSamplerType::SAMPLERTYPE_Masks);
	UMaterialExpressionScalarParameter* RoughnessMul = AddSceneScalarParameter(Material, FPBRMaterialParameters::RoughnessMultiplier, GroupMasks, 20, 1.0f, -620, 520);
	UMaterialExpressionScalarParameter* RoughnessValue = AddSceneScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupMasks, 25, 0.5f, -620, 640);
	UMaterialExpressionMultiply* Roughness = AddSceneMultiply(Material, -340, 520);
	Roughness->A.Connect(0, RoughnessTex);
	Roughness->B.Connect(0, RoughnessMul);
	UMaterialExpressionStaticSwitchParameter* UseRoughnessTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseRoughnessTexture, GroupSwitch, 30, true, -40, 20);
	UseRoughnessTexture->A.Connect(0, Roughness);
	UseRoughnessTexture->B.Connect(0, RoughnessValue);
	EditorData->Roughness.Connect(0, UseRoughnessTexture);

	UMaterialExpressionTextureSampleParameter2D* MetallicTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::MetallicTexture, GroupMasks, 30, -900, 760, EMaterialSamplerType::SAMPLERTYPE_Masks);
	UMaterialExpressionScalarParameter* MetallicMul = AddSceneScalarParameter(Material, FPBRMaterialParameters::MetallicMultiplier, GroupMasks, 40, 1.0f, -620, 760);
	UMaterialExpressionScalarParameter* MetallicValue = AddSceneScalarParameter(Material, FPBRMaterialParameters::MetallicValue, GroupMasks, 45, 0.0f, -620, 880);
	UMaterialExpressionMultiply* Metallic = AddSceneMultiply(Material, -340, 760);
	Metallic->A.Connect(0, MetallicTex);
	Metallic->B.Connect(0, MetallicMul);
	UMaterialExpressionStaticSwitchParameter* UseMetallicTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseMetallicTexture, GroupSwitch, 40, true, -40, 160);
	UseMetallicTexture->A.Connect(0, Metallic);
	UseMetallicTexture->B.Connect(0, MetallicValue);
	EditorData->Metallic.Connect(0, UseMetallicTexture);

	UMaterialExpressionTextureSampleParameter2D* AOTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::AOTexture, GroupMasks, 50, -900, 1000, EMaterialSamplerType::SAMPLERTYPE_Masks);
	UMaterialExpressionScalarParameter* AOMul = AddSceneScalarParameter(Material, FPBRMaterialParameters::AOMultiplier, GroupMasks, 60, 1.0f, -620, 1000);
	UMaterialExpressionScalarParameter* AOValue = AddSceneScalarParameter(Material, FPBRMaterialParameters::AOValue, GroupMasks, 65, 1.0f, -620, 1120);
	UMaterialExpressionMultiply* AO = AddSceneMultiply(Material, -340, 1000);
	AO->A.Connect(0, AOTex);
	AO->B.Connect(0, AOMul);
	UMaterialExpressionStaticSwitchParameter* UseAOTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseAOTexture, GroupSwitch, 50, true, -40, 300);
	UseAOTexture->A.Connect(0, AO);
	UseAOTexture->B.Connect(0, AOValue);
	EditorData->AmbientOcclusion.Connect(0, UseAOTexture);

	UMaterialExpressionTextureSampleParameter2D* EmissiveTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::EmissiveTexture, GroupEmissive, 10, -900, 1240);
	UMaterialExpressionVectorParameter* EmissiveColor = AddSceneVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupEmissive, FLinearColor::White, 20, -900, 1400);
	UMaterialExpressionScalarParameter* EmissiveIntensity = AddSceneScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupEmissive, 30, 0.0f, -900, 1560);
	UMaterialExpressionMultiply* EmissiveTinted = AddSceneMultiply(Material, -600, 1260);
	EmissiveTinted->A.Connect(0, EmissiveTex);
	EmissiveTinted->B.Connect(0, EmissiveColor);
	UMaterialExpressionMultiply* EmissiveFinal = AddSceneMultiply(Material, -320, 1260);
	EmissiveFinal->A.Connect(0, EmissiveTinted);
	EmissiveFinal->B.Connect(0, EmissiveIntensity);
	UMaterialExpressionMultiply* EmissiveSolid = AddSceneMultiply(Material, -320, 1480);
	EmissiveSolid->A.Connect(0, EmissiveColor);
	EmissiveSolid->B.Connect(0, EmissiveIntensity);
	UMaterialExpressionStaticSwitchParameter* UseEmissiveTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseEmissiveTexture, GroupSwitch, 60, true, -40, 440);
	UseEmissiveTexture->A.Connect(0, EmissiveFinal);
	UseEmissiveTexture->B.Connect(0, EmissiveSolid);
	EditorData->EmissiveColor.Connect(0, UseEmissiveTexture);

	AddSceneTextureParameter(Material, FPBRMaterialParameters::HeightTexture, GroupHeight, 10, -900, 1780, EMaterialSamplerType::SAMPLERTYPE_Masks);
	AddSceneScalarParameter(Material, FPBRMaterialParameters::HeightStrength, GroupHeight, 20, 0.0f, -620, 1780);
	AddSceneScalarParameter(Material, FPBRMaterialParameters::PixelDepthOffsetStrength, GroupHeight, 30, 0.0f, -620, 1920);
	ConnectSceneTextureSamplesToUV(Material, SharedUV);

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	OutMessage = TEXT("已修复场景替换专用母材质");
	return true;
}

UMaterial* FPBRSceneMaterialReplacer::EnsureSceneReplaceMasterMaterial(FString& OutMessage)
{
	OutMessage.Empty();
	const FString PackagePath = TEXT("/Game/PBRStudio/SceneReplaceTemplates/M_PBRStudio_SceneReplace");
	const FString AssetName = TEXT("M_PBRStudio_SceneReplace");

	if (UMaterial* Existing = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(PackagePath)))
	{
		Existing->Modify();
		if (!BuildSceneReplaceMaterialGraph(Existing, OutMessage))
		{
			return nullptr;
		}
		SaveSceneReplaceMaterial(Existing);
		return Existing;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutMessage = TEXT("创建场景替换母材质包失败");
		return nullptr;
	}

	UMaterial* Material = NewObject<UMaterial>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Material)
	{
		OutMessage = TEXT("创建场景替换母材质失败");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Material);
	Package->SetDirtyFlag(true);
	if (!BuildSceneReplaceMaterialGraph(Material, OutMessage))
	{
		return nullptr;
	}
	SaveSceneReplaceMaterial(Material);
	OutMessage = TEXT("已创建场景替换专用母材质");
	return Material;
}

static UMaterial* EnsureSceneMasterMaterial(
	const FString& PackagePath,
	const FString& AssetName,
	TFunctionRef<bool(UMaterial*, FString&)> BuildGraph,
	const FString& CreateFailedMessage,
	FString& OutMessage)
{
	OutMessage.Empty();

	if (UMaterial* Existing = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(PackagePath)))
	{
		Existing->Modify();
		if (!BuildGraph(Existing, OutMessage))
		{
			return nullptr;
		}
		SaveSceneReplaceMaterial(Existing);
		return Existing;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutMessage = CreateFailedMessage + TEXT("包失败");
		return nullptr;
	}

	UMaterial* Material = NewObject<UMaterial>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Material)
	{
		OutMessage = CreateFailedMessage + TEXT("失败");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Material);
	Package->SetDirtyFlag(true);
	if (!BuildGraph(Material, OutMessage))
	{
		return nullptr;
	}
	SaveSceneReplaceMaterial(Material);
	return Material;
}

static bool BuildSceneGlassMaterialGraph(UMaterial* Material, FString& OutMessage)
{
	if (!Material)
	{
		OutMessage = TEXT("玻璃替换母材质为空");
		return false;
	}

	ResetSceneReplaceMaterialGraph(Material);
	ConfigureSceneReplaceMaterialType(Material, EPBRSceneReplacementKind::Glass);

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("无法访问玻璃替换母材质编辑数据");
		return false;
	}

	const FName GroupBase(TEXT("01 玻璃颜色"));
	const FName GroupOptics(TEXT("02 透明折射"));
	const FName GroupNormal(TEXT("03 法线"));
	const FName GroupSwitch(TEXT("04 贴图开关"));
	const FName GroupUV(TEXT("05 UV 调整"));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupBase.ToString(), 10));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupOptics.ToString(), 20));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupNormal.ToString(), 30));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupSwitch.ToString(), 40));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupUV.ToString(), 50));

	AddSceneComment(Material, TEXT("01 普通玻璃: 使用 Translucent + Default Lit，避免 Thin Translucent 输出节点带来的 SM6 报错。"), -980, -360, 780, 520, FLinearColor(0.12f, 0.22f, 0.32f, 0.35f));
	AddSceneComment(Material, TEXT("02 透明/折射: Opacity 控制透明度，Refraction 控制普通折射强度。"), -980, 180, 760, 520, FLinearColor(0.10f, 0.24f, 0.26f, 0.35f));
	AddSceneComment(Material, TEXT("03 UV 调整: 颜色、透明和法线贴图共用平铺、偏移、旋转。"), -1780, -360, 700, 760, FLinearColor(0.18f, 0.18f, 0.18f, 0.35f));
	AddSceneComment(Material, TEXT("04 贴图开关: 关闭贴图后回退为颜色/数值。"), -40, -360, 540, 520, FLinearColor(0.16f, 0.18f, 0.28f, 0.35f));

	UMaterialExpression* SharedUV = BuildSceneSharedUVControls(Material, GroupUV, -1700, -260);

	UMaterialExpressionTextureSampleParameter2D* BaseTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::BaseColorTexture, GroupBase, 10, -900, -260);
	UMaterialExpressionVectorParameter* BaseTint = AddSceneVectorParameter(Material, FPBRMaterialParameters::BaseColorTint, GroupBase, FLinearColor(0.8f, 0.95f, 1.0f, 1.0f), 20, -900, -100);
	UMaterialExpressionScalarParameter* BaseIntensity = AddSceneScalarParameter(Material, FPBRMaterialParameters::BaseColorIntensity, GroupBase, 30, 0.65f, -900, 60);
	UMaterialExpressionMultiply* TintedBase = AddSceneMultiply(Material, -600, -240);
	TintedBase->A.Connect(0, BaseTex);
	TintedBase->B.Connect(0, BaseTint);
	UMaterialExpressionMultiply* FinalBase = AddSceneMultiply(Material, -320, -220);
	FinalBase->A.Connect(0, TintedBase);
	FinalBase->B.Connect(0, BaseIntensity);
	UMaterialExpressionMultiply* SolidBase = AddSceneMultiply(Material, -320, -40);
	SolidBase->A.Connect(0, BaseTint);
	SolidBase->B.Connect(0, BaseIntensity);
	UMaterialExpressionStaticSwitchParameter* UseBaseColorTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseBaseColorTexture, GroupSwitch, 10, true, -40, -260);
	UseBaseColorTexture->A.Connect(0, FinalBase);
	UseBaseColorTexture->B.Connect(0, SolidBase);
	EditorData->BaseColor.Connect(0, UseBaseColorTexture);

	UMaterialExpressionTextureSampleParameter2D* OpacityTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::OpacityTexture, GroupOptics, 10, -900, 260, EMaterialSamplerType::SAMPLERTYPE_Masks);
	UMaterialExpressionScalarParameter* OpacityValue = AddSceneScalarParameter(Material, FPBRMaterialParameters::Opacity, GroupOptics, 20, 0.35f, -620, 260);
	UMaterialExpressionMultiply* OpacityWithTexture = AddSceneMultiply(Material, -340, 260);
	OpacityWithTexture->A.Connect(0, OpacityTex);
	OpacityWithTexture->B.Connect(0, OpacityValue);
	UMaterialExpressionStaticSwitchParameter* UseOpacityTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseOpacityTexture, GroupSwitch, 20, true, -40, -120);
	UseOpacityTexture->A.Connect(0, OpacityWithTexture);
	UseOpacityTexture->B.Connect(0, OpacityValue);
	EditorData->Opacity.Connect(0, UseOpacityTexture);

	UMaterialExpressionScalarParameter* RoughnessValue = AddSceneScalarParameter(Material, FPBRMaterialParameters::RoughnessValue, GroupOptics, 30, 0.05f, -620, 420);
	UMaterialExpressionScalarParameter* SpecularValue = AddSceneScalarParameter(Material, FPBRMaterialParameters::SpecularLevel, GroupOptics, 40, 0.75f, -620, 560);
	UMaterialExpressionScalarParameter* RefractionValue = AddSceneScalarParameter(Material, FPBRMaterialParameters::RefractionAmount, GroupOptics, 50, 1.52f, -620, 700);
	EditorData->Roughness.Connect(0, RoughnessValue);
	EditorData->Specular.Connect(0, SpecularValue);
	EditorData->Refraction.Connect(0, RefractionValue);

	UMaterialExpressionTextureSampleParameter2D* NormalTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::NormalTexture, GroupNormal, 10, -900, 900, EMaterialSamplerType::SAMPLERTYPE_Normal);
	UMaterialExpressionScalarParameter* NormalStrength = AddSceneScalarParameter(Material, FPBRMaterialParameters::NormalStrength, GroupNormal, 20, 0.35f, -900, 1040);
	UMaterialExpressionConstant3Vector* FlatNormalValue = AddSceneConstant3Vector(Material, FLinearColor(0.0f, 0.0f, 1.0f), -620, 980);
	UMaterialExpressionLinearInterpolate* NormalWithStrength = AddSceneLerp(Material, -340, 900);
	NormalWithStrength->A.Connect(0, FlatNormalValue);
	NormalWithStrength->B.Connect(0, NormalTex);
	NormalWithStrength->Alpha.Connect(0, NormalStrength);
	UMaterialExpressionStaticSwitchParameter* UseNormalTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseNormalTexture, GroupSwitch, 30, true, -40, 20);
	UseNormalTexture->A.Connect(0, NormalWithStrength);
	UseNormalTexture->B.Connect(0, FlatNormalValue);
	EditorData->Normal.Connect(0, UseNormalTexture);

	ConnectSceneTextureSamplesToUV(Material, SharedUV);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	OutMessage = TEXT("已修复普通玻璃替换母材质");
	return true;
}

static bool BuildSceneEmissiveMaterialGraph(UMaterial* Material, FString& OutMessage)
{
	if (!Material)
	{
		OutMessage = TEXT("自发光替换母材质为空");
		return false;
	}

	ResetSceneReplaceMaterialGraph(Material);
	ConfigureSceneReplaceMaterialType(Material, EPBRSceneReplacementKind::Emissive);

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutMessage = TEXT("无法访问自发光替换母材质编辑数据");
		return false;
	}

	const FName GroupEmissive(TEXT("01 自发光"));
	const FName GroupSwitch(TEXT("02 贴图开关"));
	const FName GroupUV(TEXT("03 UV 调整"));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupEmissive.ToString(), 10));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupSwitch.ToString(), 20));
	EditorData->ParameterGroupData.Add(FParameterGroupData(GroupUV.ToString(), 30));

	AddSceneComment(Material, TEXT("01 普通自发光: 使用 Unlit + Emissive Color，适合导入场景里的灯带、灯箱、霓虹材质。"), -980, -360, 780, 560, FLinearColor(0.32f, 0.16f, 0.08f, 0.35f));
	AddSceneComment(Material, TEXT("02 UV 调整: 自发光贴图共用平铺、偏移、旋转。"), -1780, -360, 700, 760, FLinearColor(0.18f, 0.18f, 0.18f, 0.35f));
	AddSceneComment(Material, TEXT("03 贴图开关: 可关闭贴图，直接使用自发光颜色和强度。"), -40, -360, 540, 360, FLinearColor(0.16f, 0.18f, 0.28f, 0.35f));

	UMaterialExpression* SharedUV = BuildSceneSharedUVControls(Material, GroupUV, -1700, -260);

	UMaterialExpressionTextureSampleParameter2D* EmissiveTex = AddSceneTextureParameter(Material, FPBRMaterialParameters::EmissiveTexture, GroupEmissive, 10, -900, -260);
	UMaterialExpressionVectorParameter* EmissiveColor = AddSceneVectorParameter(Material, FPBRMaterialParameters::EmissiveColor, GroupEmissive, FLinearColor::White, 20, -900, -100);
	UMaterialExpressionScalarParameter* EmissiveIntensity = AddSceneScalarParameter(Material, FPBRMaterialParameters::EmissiveIntensity, GroupEmissive, 30, 4.0f, -900, 60);
	UMaterialExpressionMultiply* EmissiveTinted = AddSceneMultiply(Material, -600, -240);
	EmissiveTinted->A.Connect(0, EmissiveTex);
	EmissiveTinted->B.Connect(0, EmissiveColor);
	UMaterialExpressionMultiply* EmissiveFinal = AddSceneMultiply(Material, -320, -220);
	EmissiveFinal->A.Connect(0, EmissiveTinted);
	EmissiveFinal->B.Connect(0, EmissiveIntensity);
	UMaterialExpressionMultiply* EmissiveSolid = AddSceneMultiply(Material, -320, -40);
	EmissiveSolid->A.Connect(0, EmissiveColor);
	EmissiveSolid->B.Connect(0, EmissiveIntensity);
	UMaterialExpressionStaticSwitchParameter* UseEmissiveTexture = AddSceneStaticSwitchParameter(Material, FPBRMaterialParameters::UseEmissiveTexture, GroupSwitch, 10, true, -40, -260);
	UseEmissiveTexture->A.Connect(0, EmissiveFinal);
	UseEmissiveTexture->B.Connect(0, EmissiveSolid);
	EditorData->EmissiveColor.Connect(0, UseEmissiveTexture);

	ConnectSceneTextureSamplesToUV(Material, SharedUV);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	OutMessage = TEXT("已修复普通自发光替换母材质");
	return true;
}

UMaterial* FPBRSceneMaterialReplacer::EnsureSceneGlassMasterMaterial(FString& OutMessage)
{
	UMaterial* Material = EnsureSceneMasterMaterial(
		TEXT("/Game/PBRStudio/SceneReplaceTemplates/M_PBRStudio_SceneReplace_Glass"),
		TEXT("M_PBRStudio_SceneReplace_Glass"),
		BuildSceneGlassMaterialGraph,
		TEXT("创建普通玻璃替换母材质"),
		OutMessage);
	if (Material && OutMessage.IsEmpty())
	{
		OutMessage = TEXT("已创建普通玻璃替换母材质");
	}
	return Material;
}

UMaterial* FPBRSceneMaterialReplacer::EnsureSceneEmissiveMasterMaterial(FString& OutMessage)
{
	UMaterial* Material = EnsureSceneMasterMaterial(
		TEXT("/Game/PBRStudio/SceneReplaceTemplates/M_PBRStudio_SceneReplace_Emissive"),
		TEXT("M_PBRStudio_SceneReplace_Emissive"),
		BuildSceneEmissiveMaterialGraph,
		TEXT("创建普通自发光替换母材质"),
		OutMessage);
	if (Material && OutMessage.IsEmpty())
	{
		OutMessage = TEXT("已创建普通自发光替换母材质");
	}
	return Material;
}

UTexture2D* FPBRSceneMaterialReplacer::FindBaseColorTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	if (!Material)
	{
		return nullptr;
	}

	if (UTexture2D* DiffuseTexture = FindDiffuseTextureInInstanceOverrides(Material, OutSourcePath))
	{
		return DiffuseTexture;
	}

	static const TArray<FName> PreferredParams = {
		TEXT("BaseColor"), TEXT("Base Color"), TEXT("BaseColorTexture"), TEXT("Base_Color"),
		TEXT("Diffuse"), TEXT("DiffuseTexture"), TEXT("Diffuse Color"), TEXT("Albedo"),
		TEXT("AlbedoTexture"), TEXT("Color"), TEXT("Colour"), TEXT("texmap_diffuse"),
		TEXT("map_diffuse"), TEXT("DiffuseMap"), TEXT("Diffuse Map"), TEXT("颜色"), TEXT("基础颜色贴图")
	};

	TArray<FBaseColorTextureCandidate> Candidates;

	if (const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
	{
		for (const FTextureParameterValue& ParameterValue : Instance->TextureParameterValues)
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(ParameterValue.ParameterValue.Get()))
			{
				ConsiderBaseColorTextureCandidate(Candidates, Texture, ParameterValue.ParameterInfo.Name.ToString(), false, true);
			}
		}
	}

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
	Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(Info, Value, true))
		{
			ConsiderBaseColorTextureCandidate(Candidates, Cast<UTexture2D>(Value), Info.Name.ToString(), false, true);
		}
	}

	for (const FName& ParamName : PreferredParams)
	{
		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(FMaterialParameterInfo(ParamName), Value))
		{
			ConsiderBaseColorTextureCandidate(Candidates, Cast<UTexture2D>(Value), ParamName.ToString(), false, false);
		}
	}

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(Info, Value))
		{
			ConsiderBaseColorTextureCandidate(Candidates, Cast<UTexture2D>(Value), Info.Name.ToString(), false, false);
		}
	}

	TArray<UTexture*> UsedTextures;
	TArray<FName> BaseColorParamNames;
	if (Material->GetTexturesInPropertyChain(MP_BaseColor, UsedTextures, &BaseColorParamNames, nullptr))
	{
		for (int32 Index = 0; Index < UsedTextures.Num(); ++Index)
		{
			const FString ExtraText = BaseColorParamNames.IsValidIndex(Index) ? BaseColorParamNames[Index].ToString() : FString(TEXT("BaseColor"));
			ConsiderBaseColorTextureCandidate(Candidates, Cast<UTexture2D>(UsedTextures[Index]), ExtraText, true, false);
		}
	}

	UsedTextures.Reset();
	Material->GetUsedTextures(UsedTextures, EMaterialQualityLevel::High);
	for (UTexture* Texture : UsedTextures)
	{
		ConsiderBaseColorTextureCandidate(Candidates, Cast<UTexture2D>(Texture), FString(), false, false);
	}

	Candidates.Sort([](const FBaseColorTextureCandidate& A, const FBaseColorTextureCandidate& B)
	{
		return A.Score > B.Score;
	});

	if (Candidates.Num() > 0 && Candidates[0].Score >= 18)
	{
		OutSourcePath = Candidates[0].SourcePath;
		return Candidates[0].Texture;
	}

	for (const FBaseColorTextureCandidate& Candidate : Candidates)
	{
		if (Candidate.Texture && Candidate.Texture->SRGB && Candidate.Texture->CompressionSettings == TextureCompressionSettings::TC_Default && Candidate.Score >= -5)
		{
			OutSourcePath = Candidate.SourcePath;
			return Candidate.Texture;
		}
	}

	return nullptr;
}

static bool DoesBaseColorNeedMaterialBake(UMaterialInterface* Material)
{
	if (!Material)
	{
		return false;
	}

	TArray<UTexture*> UsedTextures;
	TArray<FName> BaseColorParamNames;
	if (!Material->GetTexturesInPropertyChain(MP_BaseColor, UsedTextures, &BaseColorParamNames, nullptr))
	{
		return false;
	}

	TSet<FSoftObjectPath> UniqueTextures;
	for (UTexture* Texture : UsedTextures)
	{
		if (Texture)
		{
			UniqueTextures.Add(FSoftObjectPath(Texture));
		}
	}
	return UniqueTextures.Num() > 1;
}

UTexture2D* FPBRSceneMaterialReplacer::FindNormalTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	static const TArray<FName> PreferredParams = {
		TEXT("Normal"), TEXT("NormalTexture"), TEXT("Normal Map"), TEXT("NormalMap"),
		TEXT("Bump"), TEXT("BumpTexture"), TEXT("Bump Map"), TEXT("texmap_bump"),
		TEXT("VRayNormal"), TEXT("CoronaNormal")
	};
	if (UTexture2D* Texture = FindTextureByParamNames(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInMaterialProperty(Material, MP_Normal, OutSourcePath))
	{
		return Texture;
	}
	return FindUsedTextureByTokens(
		Material,
		{ TEXT("normal"), TEXT("_nor"), TEXT("_nrm"), TEXT("_n_"), TEXT("_dx"), TEXT("_gl"), TEXT("bump") },
		{ TEXT("height"), TEXT("rough"), TEXT("metal"), TEXT("opacity"), TEXT("base"), TEXT("diffuse"), TEXT("albedo") },
		OutSourcePath);
}

UTexture2D* FPBRSceneMaterialReplacer::FindRoughnessTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	static const TArray<FName> PreferredParams = {
		TEXT("Roughness"), TEXT("RoughnessTexture"), TEXT("Roughness Map"), TEXT("ReflectionGlossiness"),
		TEXT("Glossiness"), TEXT("GlossinessTexture"), TEXT("Reflect Glossiness"), TEXT("texmap_reflectionGlossiness"),
		TEXT("Reflection Roughness")
	};
	if (UTexture2D* Texture = FindTextureByParamNames(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInMaterialProperty(Material, MP_Roughness, OutSourcePath))
	{
		return Texture;
	}
	return FindUsedTextureByTokens(
		Material,
		{ TEXT("roughness"), TEXT("rough"), TEXT("_rgh"), TEXT("_r_"), TEXT("glossiness"), TEXT("gloss") },
		{ TEXT("normal"), TEXT("metal"), TEXT("opacity"), TEXT("base"), TEXT("diffuse"), TEXT("albedo") },
		OutSourcePath);
}

UTexture2D* FPBRSceneMaterialReplacer::FindMetallicTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	static const TArray<FName> PreferredParams = {
		TEXT("Metallic"), TEXT("MetallicTexture"), TEXT("Metalness"), TEXT("MetalnessTexture"),
		TEXT("Metal Map"), TEXT("texmap_metalness"), TEXT("texmap_metallic")
	};
	if (UTexture2D* Texture = FindTextureByParamNames(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInMaterialProperty(Material, MP_Metallic, OutSourcePath))
	{
		return Texture;
	}
	return FindUsedTextureByTokens(
		Material,
		{ TEXT("metallic"), TEXT("metalness"), TEXT("metal"), TEXT("_mtl"), TEXT("_met") },
		{ TEXT("normal"), TEXT("rough"), TEXT("opacity"), TEXT("base"), TEXT("diffuse"), TEXT("albedo") },
		OutSourcePath);
}

UTexture2D* FPBRSceneMaterialReplacer::FindAOTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	static const TArray<FName> PreferredParams = {
		TEXT("AO"), TEXT("AOTexture"), TEXT("AmbientOcclusion"), TEXT("Ambient Occlusion"),
		TEXT("Occlusion"), TEXT("OcclusionTexture")
	};
	if (UTexture2D* Texture = FindTextureByParamNames(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInMaterialProperty(Material, MP_AmbientOcclusion, OutSourcePath))
	{
		return Texture;
	}
	return FindUsedTextureByTokens(
		Material,
		{ TEXT("ambientocclusion"), TEXT("ambient_occlusion"), TEXT("occlusion"), TEXT("_ao"), TEXT("ao_") },
		{ TEXT("normal"), TEXT("rough"), TEXT("metal"), TEXT("opacity"), TEXT("base"), TEXT("diffuse"), TEXT("albedo") },
		OutSourcePath);
}

UTexture2D* FPBRSceneMaterialReplacer::FindSpecularTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	static const TArray<FName> PreferredParams = {
		TEXT("Specular"), TEXT("SpecularTexture"), TEXT("Specular Map"), TEXT("Reflection"),
		TEXT("ReflectionTexture"), TEXT("texmap_reflection"), TEXT("Spec")
	};
	if (UTexture2D* Texture = FindTextureByParamNames(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInMaterialProperty(Material, MP_Specular, OutSourcePath))
	{
		return Texture;
	}
	return FindUsedTextureByTokens(
		Material,
		{ TEXT("specular"), TEXT("_spec"), TEXT("reflection"), TEXT("_refl") },
		{ TEXT("normal"), TEXT("rough"), TEXT("metal"), TEXT("opacity"), TEXT("base"), TEXT("diffuse"), TEXT("albedo") },
		OutSourcePath);
}

UTexture2D* FPBRSceneMaterialReplacer::FindHeightTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	static const TArray<FName> PreferredParams = {
		TEXT("Height"), TEXT("HeightTexture"), TEXT("Height Map"), TEXT("Displacement"),
		TEXT("DisplacementTexture"), TEXT("Displacement Map"), TEXT("texmap_displacement"),
		TEXT("Bump Height")
	};
	if (UTexture2D* Texture = FindTextureByParamNames(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInMaterialProperty(Material, MP_Displacement, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInMaterialProperty(Material, MP_WorldPositionOffset, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInMaterialProperty(Material, MP_PixelDepthOffset, OutSourcePath))
	{
		return Texture;
	}
	return FindUsedTextureByTokens(
		Material,
		{ TEXT("height"), TEXT("displacement"), TEXT("disp"), TEXT("_h_"), TEXT("_d_"), TEXT("bump") },
		{ TEXT("normal"), TEXT("rough"), TEXT("metal"), TEXT("opacity"), TEXT("base"), TEXT("diffuse"), TEXT("albedo") },
		OutSourcePath);
}

UTexture2D* FPBRSceneMaterialReplacer::FindEmissiveTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	if (!Material)
	{
		return nullptr;
	}

	static const TArray<FName> PreferredParams = {
		TEXT("Emissive"), TEXT("EmissiveTexture"), TEXT("Emissive Color"),
		TEXT("SelfIllumination"), TEXT("SelfIlluminationTexture"), TEXT("Glow"),
		TEXT("Neon"), TEXT("LED"), TEXT("自发光"), TEXT("发光贴图")
	};

	if (UTexture2D* Texture = FindTextureInInstanceOverrides(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInInstanceOverridesByTokens(
		Material,
		{ TEXT("emissive"), TEXT("emission"), TEXT("emit"), TEXT("selfillum"), TEXT("self_illum"), TEXT("glow"), TEXT("neon"), TEXT("led") },
		{ TEXT("normal"), TEXT("rough"), TEXT("metal"), TEXT("opacity"), TEXT("base"), TEXT("diffuse"), TEXT("albedo") },
		OutSourcePath))
	{
		return Texture;
	}
	if (ShouldOnlyUseInstanceTextureOverrides(Material))
	{
		return nullptr;
	}

	for (const FName& ParamName : PreferredParams)
	{
		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(FMaterialParameterInfo(ParamName), Value))
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Value))
			{
				OutSourcePath = TextureSourceFilename(Texture);
				if (OutSourcePath.IsEmpty() || IsSupportedImagePath(OutSourcePath))
				{
					return Texture;
				}
			}
		}
	}

	TArray<UTexture*> UsedTextures;
	if (Material->GetTexturesInPropertyChain(MP_EmissiveColor, UsedTextures, nullptr, nullptr))
	{
		for (UTexture* Texture : UsedTextures)
		{
			UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
			if (!Texture2D)
			{
				continue;
			}

			OutSourcePath = TextureSourceFilename(Texture2D);
			if (OutSourcePath.IsEmpty() || IsSupportedImagePath(OutSourcePath))
			{
				return Texture2D;
			}
		}
	}

	UsedTextures.Reset();
	Material->GetUsedTextures(UsedTextures, EMaterialQualityLevel::High);
	for (UTexture* Texture : UsedTextures)
	{
		UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		if (!Texture2D)
		{
			continue;
		}
		const FString Name = Texture2D->GetName().ToLower();
		if (IsSceneExplicitEmissiveName(Name))
		{
			OutSourcePath = TextureSourceFilename(Texture2D);
			if (OutSourcePath.IsEmpty() || IsSupportedImagePath(OutSourcePath))
			{
				return Texture2D;
			}
		}
	}

	return nullptr;
}

UTexture2D* FPBRSceneMaterialReplacer::FindOpacityTexture(UMaterialInterface* Material, FString& OutSourcePath)
{
	OutSourcePath.Empty();
	if (!Material)
	{
		return nullptr;
	}

	static const TArray<FName> PreferredParams = {
		TEXT("Opacity"), TEXT("OpacityTexture"), TEXT("Opacity Map"), TEXT("Alpha"),
		TEXT("AlphaTexture"), TEXT("Transparency"), TEXT("Transparent"), TEXT("玻璃透明"),
		TEXT("透明贴图"), TEXT("透明度")
	};

	if (UTexture2D* Texture = FindTextureInInstanceOverrides(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInInstanceOverridesByTokens(
		Material,
		{ TEXT("opacity"), TEXT("alpha"), TEXT("transparency"), TEXT("transparent"), TEXT("translucency"), TEXT("_op"), TEXT("_a"), TEXT("mask") },
		{ TEXT("normal"), TEXT("rough"), TEXT("metal"), TEXT("base"), TEXT("diffuse"), TEXT("albedo") },
		OutSourcePath))
	{
		return Texture;
	}
	if (ShouldOnlyUseInstanceTextureOverrides(Material))
	{
		return nullptr;
	}

	for (const FName& ParamName : PreferredParams)
	{
		UTexture* Value = nullptr;
		if (Material->GetTextureParameterValue(FMaterialParameterInfo(ParamName), Value))
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Value))
			{
				OutSourcePath = TextureSourceFilename(Texture);
				if (OutSourcePath.IsEmpty() || IsSupportedImagePath(OutSourcePath))
				{
					return Texture;
				}
			}
		}
	}

	TArray<UTexture*> UsedTextures;
	if (Material->GetTexturesInPropertyChain(MP_Opacity, UsedTextures, nullptr, nullptr) ||
		Material->GetTexturesInPropertyChain(MP_OpacityMask, UsedTextures, nullptr, nullptr))
	{
		for (UTexture* Texture : UsedTextures)
		{
			UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
			if (!Texture2D)
			{
				continue;
			}

			OutSourcePath = TextureSourceFilename(Texture2D);
			if (OutSourcePath.IsEmpty() || IsSupportedImagePath(OutSourcePath))
			{
				return Texture2D;
			}
		}
	}

	UsedTextures.Reset();
	Material->GetUsedTextures(UsedTextures, EMaterialQualityLevel::High);
	for (UTexture* Texture : UsedTextures)
	{
		UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		if (!Texture2D)
		{
			continue;
		}

		const FString Name = Texture2D->GetName().ToLower();
		const bool bLooksOpacity =
			Name.Contains(TEXT("opacity")) || Name.Contains(TEXT("alpha")) ||
			Name.Contains(TEXT("transparency")) || Name.Contains(TEXT("transparent")) ||
			Name.Contains(TEXT("translucency")) || Name.Contains(TEXT("_op")) ||
			Name.EndsWith(TEXT("_op")) || Name.EndsWith(TEXT("_a")) ||
			Name.Contains(TEXT("mask"));
		if (bLooksOpacity)
		{
			OutSourcePath = TextureSourceFilename(Texture2D);
			if (OutSourcePath.IsEmpty() || IsSupportedImagePath(OutSourcePath))
			{
				return Texture2D;
			}
		}
	}

	return nullptr;
}

bool FPBRSceneMaterialReplacer::IsEmissiveMaterial(UMaterialInterface* Material, UTexture2D* EmissiveTexture)
{
	if (!Material)
	{
		return false;
	}

	const FString Name = (Material->GetName() + TEXT(" ") + Material->GetPathName()).ToLower();
	if (!IsSceneExplicitEmissiveName(Name))
	{
		return false;
	}

	if (EmissiveTexture)
	{
		return true;
	}
	if (Cast<UMaterialInstance>(Material))
	{
		return false;
	}

	TArray<UTexture*> EmissiveTextures;
	return Material->GetTexturesInPropertyChain(MP_EmissiveColor, EmissiveTextures, nullptr, nullptr) && EmissiveTextures.Num() > 0;
}

bool FPBRSceneMaterialReplacer::IsTransparentMaterial(UMaterialInterface* Material, UTexture2D* OpacityTexture)
{
	if (!Material)
	{
		return false;
	}

	if (!IsOpaqueBlendMode(*Material))
	{
		return true;
	}

	return OpacityTexture != nullptr;
}

static bool IsSceneGlassMaterial(UMaterialInterface* Material)
{
	if (!Material)
	{
		return false;
	}

	const FString Name = (Material->GetName() + TEXT(" ") + Material->GetPathName()).ToLower();
	return Name.Contains(TEXT("glass")) ||
		Name.Contains(TEXT("window")) ||
		Name.Contains(TEXT("crystal")) ||
		Name.Contains(TEXT("acrylic")) ||
		Name.Contains(TEXT("pane")) ||
		Name.Contains(TEXT("glazing")) ||
		Name.Contains(TEXT("玻璃")) ||
		Name.Contains(TEXT("窗"));
}

static FString BuildSelectionEditableMaterialOutputName(UPrimitiveComponent* Component, int32 MaterialIndex, UMaterialInterface* SourceMaterial)
{
	const AActor* Owner = Component ? Component->GetOwner() : nullptr;
	const FString ActorName = FPBRMaterialInstanceFactory::SanitizeAssetName(Owner ? Owner->GetActorLabel() : TEXT("Actor"));
	const FString ComponentName = FPBRMaterialInstanceFactory::SanitizeAssetName(Component ? Component->GetName() : TEXT("Component"));
	const FString MaterialName = FPBRMaterialInstanceFactory::SanitizeAssetName(SourceMaterial ? SourceMaterial->GetName() : TEXT("Material"));
	return FString::Printf(TEXT("MI_%s_%s_Slot%d_%s"), *ActorName, *ComponentName, MaterialIndex, *MaterialName).Left(180);
}

static void FillSelectionEditableCandidate(
	FPBRSceneMaterialCandidate& Candidate,
	UPrimitiveComponent* Component,
	int32 MaterialIndex,
	UMaterialInterface* SourceMaterial)
{
	Candidate.Material = SourceMaterial;
	Candidate.MaterialName = SourceMaterial ? SourceMaterial->GetName() : TEXT("Material");
	Candidate.OutputMaterialName = BuildSelectionEditableMaterialOutputName(Component, MaterialIndex, SourceMaterial);
	Candidate.MaterialPath = SourceMaterial ? SourceMaterial->GetPathName() : FString();
	Candidate.InheritedBaseColor = GetInheritedBaseColor(SourceMaterial);
	Candidate.bBaseColorNeedsBake = DoesBaseColorNeedMaterialBake(SourceMaterial);

	FLinearColor DiffuseOverrideColor;
	if (TryGetDiffuseColorFromInstanceOverrides(SourceMaterial, DiffuseOverrideColor))
	{
		Candidate.InheritedBaseColor = DiffuseOverrideColor;
	}

	Candidate.bIsPBRStudioMaterial = FPBRSceneMaterialReplacer::IsPBRStudioGeneratedMaterial(SourceMaterial);
	Candidate.bCanReplace = true;
	Candidate.bChecked = true;
	Candidate.bUseBPRReplacement = false;

	if (IsSceneGlassMaterial(SourceMaterial))
	{
		Candidate.ReplacementKind = EPBRSceneReplacementKind::Glass;
	}
	else
	{
		Candidate.ReplacementKind = EPBRSceneReplacementKind::Simple;
	}

	FPBRSceneMaterialSlot Slot;
	Slot.Component = Component;
	Slot.ComponentPath = FSoftObjectPath(Component);
	Slot.MaterialIndex = MaterialIndex;
	Slot.OriginalMaterial = SourceMaterial;
	Slot.OriginalMaterialPath = FSoftObjectPath(SourceMaterial);
	Candidate.Slots.Add(Slot);
}

static void ApplySelectionMaterialTypeDefaults(UMaterialInstanceConstant* Instance, EPBRMaterialType MaterialType)
{
	if (!Instance)
	{
		return;
	}

	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOValue, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::AOMultiplier, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.5f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::HeightStrength, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::PixelDepthOffsetStrength, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoat, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatRoughness, 0.25f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Anisotropy, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzStrength, 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTemperature), false);
	if (MaterialType != EPBRMaterialType::Metal)
	{
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseMetallicTexture), false);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
	}
	if (MaterialType != EPBRMaterialType::Emissive)
	{
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), false);
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor::Black);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 0.0f);
	}

	switch (MaterialType)
	{
	case EPBRMaterialType::Wood:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.52f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.34f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 0.9f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	case EPBRMaterialType::Stone:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.48f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.46f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 1.18f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::HeightStrength, 0.08f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	case EPBRMaterialType::Tile:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.38f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.52f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 0.8f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::HeightStrength, 0.035f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	case EPBRMaterialType::Fabric:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.86f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.18f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 0.75f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzColor, FLinearColor(0.6f, 0.58f, 0.52f, 1.0f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::FabricFuzzStrength, 0.14f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	case EPBRMaterialType::Leather:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.42f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.42f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 0.65f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoat, 0.35f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatRoughness, 0.22f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	case EPBRMaterialType::Plastic:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.48f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.42f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 0.5f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	case EPBRMaterialType::Metal:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.28f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.5f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, 0.45f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Anisotropy, 0.22f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	case EPBRMaterialType::Transparent:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.22f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.62f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.15f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.4f);
		break;
	case EPBRMaterialType::Glass:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.02f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.35f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.45f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.5f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassOpacityFresnelStrength, 0.35f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassFresnelBaseReflection, 0.02f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassFresnelExp, 5.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassFrostedStrength, 0.0f);
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::GlassAbsorptionColor, FLinearColor(0.78f, 0.92f, 1.0f, 1.0f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassAbsorptionStrength, 0.15f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassEdgeTintStrength, 0.25f);
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::GlassDirtColor, FLinearColor(0.35f, 0.32f, 0.26f, 1.0f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDirtIntensity, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDirtOpacity, 0.35f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDirtRoughness, 0.65f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDistortionIntensity, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassDistortionIORIntensity, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassShadowOpacity, 0.55f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassShadowHighlightClamp, 0.85f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassShadowNormalIntensity, 0.25f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassCausticsIntensity, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassCausticsScale, 24.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassCausticsSpeed, 0.12f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassRTOpacity, 0.35f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassRTRefractionAmount, 1.45f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::GlassRTFrostedStrength, 0.0f);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseGlassDirtTexture), false);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseGlassDistortionTexture), false);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseGlassFrostedTexture), false);
		ApplyAdvancedGlassParameterDefaults(Instance);
		break;
	case EPBRMaterialType::Water:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.05f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.65f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.33f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.55f);
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::WaterColor, FLinearColor(0.12f, 0.42f, 0.72f, 1.0f));
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedU, 0.18f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterFlowSpeedV, 0.09f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleScale, 18.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::WaterRippleStrength, 0.8f);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseWaterRippleTexture), false);
		break;
	case EPBRMaterialType::Emissive:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.35f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::EmissiveColor, FLinearColor::White);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 2.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	case EPBRMaterialType::Standard:
	default:
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.5f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicValue, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 1.0f);
		break;
	}
}

FPBRSceneEditableMaterialResult FPBRSceneMaterialReplacer::EnsureEditableMaterialForSlot(UPrimitiveComponent* Component, int32 MaterialIndex)
{
	FPBRSceneEditableMaterialResult Result;
	if (!Component)
	{
		Result.Message = TEXT("没有可编辑的网格组件");
		return Result;
	}
	if (MaterialIndex < 0 || MaterialIndex >= Component->GetNumMaterials())
	{
		Result.Message = TEXT("材质槽索引无效");
		return Result;
	}

	UMaterialInterface* CurrentMaterial = Component->GetMaterial(MaterialIndex);
	if (!CurrentMaterial)
	{
		Result.Message = TEXT("当前槽位没有材质");
		return Result;
	}

	if (UMaterialInstanceConstant* ExistingInstance = Cast<UMaterialInstanceConstant>(CurrentMaterial))
	{
		if (IsPBRStudioGeneratedMaterial(ExistingInstance))
		{
			Result.Instance = ExistingInstance;
			Result.Message = FString::Printf(TEXT("当前槽位已使用现有 PBRStudio 材质实例参数：%s"), *ExistingInstance->GetName());
			return Result;
		}
	}

	FPBRSceneMaterialCandidate Candidate;
	FillSelectionEditableCandidate(Candidate, Component, MaterialIndex, CurrentMaterial);
	FString SourcePath;
	Candidate.BaseColorTexture = FindBaseColorTexture(CurrentMaterial, SourcePath);
	Candidate.BaseColorSourcePath = SourcePath;
	Candidate.bBaseColorNeedsBake = DoesBaseColorNeedMaterialBake(CurrentMaterial);
	Candidate.NormalTexture = FindNormalTexture(CurrentMaterial, Candidate.NormalSourcePath);
	Candidate.RoughnessTexture = FindRoughnessTexture(CurrentMaterial, Candidate.RoughnessSourcePath);
	Candidate.MetallicTexture = FindMetallicTexture(CurrentMaterial, Candidate.MetallicSourcePath);
	Candidate.AOTexture = FindAOTexture(CurrentMaterial, Candidate.AOSourcePath);
	Candidate.SpecularTexture = FindSpecularTexture(CurrentMaterial, Candidate.SpecularSourcePath);
	Candidate.HeightTexture = FindHeightTexture(CurrentMaterial, Candidate.HeightSourcePath);
	Candidate.EmissiveTexture = FindEmissiveTexture(CurrentMaterial, Candidate.EmissiveSourcePath);
	Candidate.OpacityTexture = FindOpacityTexture(CurrentMaterial, Candidate.OpacitySourcePath);
	Candidate.bLooksEmissive = IsEmissiveMaterial(CurrentMaterial, Candidate.EmissiveTexture.Get());
	Candidate.bLooksTransparent = IsTransparentMaterial(CurrentMaterial, Candidate.OpacityTexture.Get());
	if (IsSceneGlassMaterial(CurrentMaterial))
	{
		Candidate.ReplacementKind = EPBRSceneReplacementKind::Glass;
	}
	else if (Candidate.bLooksEmissive)
	{
		Candidate.ReplacementKind = EPBRSceneReplacementKind::Emissive;
	}
	else
	{
		Candidate.ReplacementKind = EPBRSceneReplacementKind::Simple;
	}

	FPBRSceneReplaceSettings Settings;
	Settings.OutputRoot = TEXT("/Game/PBRStudio/SelectedMaterials");
	Settings.bGenerateOpacity = true;
	Settings.bGenerateEmissive = true;

	FString ConvertMessage;
	UMaterialInstanceConstant* Instance = nullptr;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "PBRMakeSelectedMaterialEditable", "PBRStudio Make Selected Material Editable"));
		if (AActor* Owner = Component->GetOwner())
		{
			Owner->Modify();
		}
		Component->Modify();
		Instance = CreateOrUpdateSceneManagedReplacementInstance(Candidate, Candidate.OutputMaterialName, Settings, CurrentMaterial, ConvertMessage);
		if (!Instance)
		{
			Result.Message = ConvertMessage.IsEmpty() ? TEXT("创建可编辑材质实例失败") : ConvertMessage;
			return Result;
		}

		Component->SetMaterial(MaterialIndex, Instance);
		RefreshPrimitiveAfterMaterialChange(Component, true);
	}

	Result.Instance = Instance;
	Result.Message = ConvertMessage.IsEmpty() ? FString::Printf(TEXT("已接管材质槽：%s"), *Instance->GetName()) : ConvertMessage;
	if (Candidate.bBaseColorNeedsBake)
	{
		Result.Message += TEXT("；原材质复杂基础色通道已按目标材质通道尝试烘焙迁移。");
	}
	Result.bCreatedOrUpdatedInstance = true;
	Result.bAssignedToSlot = true;
	return Result;
}

bool FPBRSceneMaterialReplacer::SetMaterialTypeForSlot(UPrimitiveComponent* Component, int32 MaterialIndex, EPBRMaterialType MaterialType, FString& OutMessage)
{
	FPBRSceneEditableMaterialResult EditableResult = EnsureEditableMaterialForSlot(Component, MaterialIndex);
	UMaterialInstanceConstant* Instance = EditableResult.Instance;
	if (!Instance)
	{
		OutMessage = EditableResult.Message;
		return false;
	}

	FString ParentMessage;
	UMaterial* ParentMaterial = FPBRMaterialTemplateManager::EnsureTemplateMaterial(MaterialType, ParentMessage);
	if (!ParentMaterial)
	{
		OutMessage = ParentMessage.IsEmpty() ? TEXT("无法确定目标母材质") : ParentMessage;
		return false;
	}

	{
		const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "PBRSetSelectedMaterialType", "PBRStudio Set Selected Material Type"));
		Instance->Modify();
		Instance->SetParentEditorOnly(ParentMaterial);
		ApplySelectionMaterialTypeDefaults(Instance, MaterialType);
		if (MaterialType == EPBRMaterialType::Glass)
		{
			EnableAdvancedGlassComponentShadowSettings(Component);
		}
		Instance->InitStaticPermutation();
		UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
		Instance->PostEditChange();
		Instance->MarkPackageDirty();
		RefreshPrimitiveAfterMaterialChange(Component, true);
	}

	OutMessage = FString::Printf(TEXT("已切换材质类型：%s"), *Instance->GetName());
	return true;
}

bool FPBRSceneMaterialReplacer::SetScalarParameterForSlot(UPrimitiveComponent* Component, int32 MaterialIndex, const FName& ParameterName, float Value, FString& OutMessage)
{
	FPBRSceneEditableMaterialResult EditableResult = EnsureEditableMaterialForSlot(Component, MaterialIndex);
	UMaterialInstanceConstant* Instance = EditableResult.Instance;
	if (!Instance)
	{
		OutMessage = EditableResult.Message;
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "PBRSetSelectedMaterialScalar", "PBRStudio Set Selected Material Scalar"));
	Instance->Modify();
	SetSceneScalarParameterEditorOnly(Instance, ParameterName, Value);
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	Instance->PostEditChange();
	Instance->MarkPackageDirty();
	RefreshPrimitiveAfterMaterialChange(Component, false);
	OutMessage = FString::Printf(TEXT("已更新参数：%s"), *ParameterName.ToString());
	return true;
}

bool FPBRSceneMaterialReplacer::SetVectorParameterForSlot(UPrimitiveComponent* Component, int32 MaterialIndex, const FName& ParameterName, const FLinearColor& Value, FString& OutMessage)
{
	FPBRSceneEditableMaterialResult EditableResult = EnsureEditableMaterialForSlot(Component, MaterialIndex);
	UMaterialInstanceConstant* Instance = EditableResult.Instance;
	if (!Instance)
	{
		OutMessage = EditableResult.Message;
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "PBRSetSelectedMaterialVector", "PBRStudio Set Selected Material Vector"));
	Instance->Modify();
	SetSceneVectorParameterEditorOnly(Instance, ParameterName, Value);
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	Instance->PostEditChange();
	Instance->MarkPackageDirty();
	RefreshPrimitiveAfterMaterialChange(Component, false);
	OutMessage = FString::Printf(TEXT("已更新颜色：%s"), *ParameterName.ToString());
	return true;
}

bool FPBRSceneMaterialReplacer::SetStaticSwitchParameterForSlot(UPrimitiveComponent* Component, int32 MaterialIndex, const FName& ParameterName, bool bValue, FString& OutMessage)
{
	FPBRSceneEditableMaterialResult EditableResult = EnsureEditableMaterialForSlot(Component, MaterialIndex);
	UMaterialInstanceConstant* Instance = EditableResult.Instance;
	if (!Instance)
	{
		OutMessage = EditableResult.Message;
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "PBRSetSelectedMaterialSwitch", "PBRStudio Set Selected Material Switch"));
	Instance->Modify();
	SetSceneSwitchParameterEditorOnly(Instance, ParameterName, bValue);
	Instance->InitStaticPermutation();
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	Instance->PostEditChange();
	Instance->MarkPackageDirty();
	RefreshPrimitiveAfterMaterialChange(Component, true);
	OutMessage = FString::Printf(TEXT("已更新开关：%s"), *ParameterName.ToString());
	return true;
}

bool FPBRSceneMaterialReplacer::SetTextureParameterForSlot(UPrimitiveComponent* Component, int32 MaterialIndex, const FName& ParameterName, UTexture* Texture, FString& OutMessage)
{
	FPBRSceneEditableMaterialResult EditableResult = EnsureEditableMaterialForSlot(Component, MaterialIndex);
	UMaterialInstanceConstant* Instance = EditableResult.Instance;
	if (!Instance)
	{
		OutMessage = EditableResult.Message;
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "PBRSetSelectedMaterialTexture", "PBRStudio Set Selected Material Texture"));
	Instance->Modify();
	const bool bChangedPermutation = SetSceneTextureParameterEditorOnly(Instance, ParameterName, Texture);
	if (bChangedPermutation)
	{
		Instance->InitStaticPermutation();
	}
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	Instance->PostEditChange();
	Instance->MarkPackageDirty();
	RefreshPrimitiveAfterMaterialChange(Component, bChangedPermutation);
	OutMessage = FString::Printf(TEXT("已更新贴图：%s"), *ParameterName.ToString());
	return true;
}

void FPBRSceneMaterialReplacer::ScanCurrentLevel(TArray<TSharedPtr<FPBRSceneMaterialCandidate>>& OutCandidates)
{
	OutCandidates.Reset();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}

	TMap<UMaterialInterface*, TSharedPtr<FPBRSceneMaterialCandidate>> ByMaterial;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TArray<UPrimitiveComponent*> Components;
		Actor->GetComponents<UPrimitiveComponent>(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			const int32 NumMaterials = Component->GetNumMaterials();
			for (int32 Index = 0; Index < NumMaterials; ++Index)
			{
				UMaterialInterface* Material = Component->GetMaterial(Index);
				if (!Material)
				{
					continue;
				}

				TSharedPtr<FPBRSceneMaterialCandidate>& Candidate = ByMaterial.FindOrAdd(Material);
				if (!Candidate.IsValid())
				{
					Candidate = MakeShared<FPBRSceneMaterialCandidate>();
					Candidate->Material = Material;
					Candidate->MaterialName = Material->GetName();
					Candidate->OutputMaterialName = TEXT("MI_PBRSR_") + Candidate->MaterialName;
					Candidate->MaterialPath = Material->GetPathName();
					Candidate->InheritedBaseColor = GetInheritedBaseColor(Material);
					Candidate->bBaseColorNeedsBake = DoesBaseColorNeedMaterialBake(Material);
					FLinearColor DiffuseOverrideColor;
					if (TryGetDiffuseColorFromInstanceOverrides(Material, DiffuseOverrideColor))
					{
						Candidate->InheritedBaseColor = DiffuseOverrideColor;
					}
					Candidate->bIsPBRStudioMaterial = IsPBRStudioGeneratedMaterial(Material);
					Candidate->bChecked = !Candidate->bIsPBRStudioMaterial;

					FString SourcePath;
					Candidate->BaseColorTexture = FindBaseColorTexture(Material, SourcePath);
					Candidate->BaseColorSourcePath = SourcePath;
					FString NormalSourcePath;
					Candidate->NormalTexture = FindNormalTexture(Material, NormalSourcePath);
					Candidate->NormalSourcePath = NormalSourcePath;
					FString RoughnessSourcePath;
					Candidate->RoughnessTexture = FindRoughnessTexture(Material, RoughnessSourcePath);
					Candidate->RoughnessSourcePath = RoughnessSourcePath;
					FString MetallicSourcePath;
					Candidate->MetallicTexture = FindMetallicTexture(Material, MetallicSourcePath);
					Candidate->MetallicSourcePath = MetallicSourcePath;
					FString AOSourcePath;
					Candidate->AOTexture = FindAOTexture(Material, AOSourcePath);
					Candidate->AOSourcePath = AOSourcePath;
					FString SpecularSourcePath;
					Candidate->SpecularTexture = FindSpecularTexture(Material, SpecularSourcePath);
					Candidate->SpecularSourcePath = SpecularSourcePath;
					FString HeightSourcePath;
					Candidate->HeightTexture = FindHeightTexture(Material, HeightSourcePath);
					Candidate->HeightSourcePath = HeightSourcePath;
					FString EmissiveSourcePath;
					Candidate->EmissiveTexture = FindEmissiveTexture(Material, EmissiveSourcePath);
					Candidate->EmissiveSourcePath = EmissiveSourcePath;
					FString OpacitySourcePath;
					Candidate->OpacityTexture = FindOpacityTexture(Material, OpacitySourcePath);
					Candidate->OpacitySourcePath = OpacitySourcePath;
					Candidate->bLooksEmissive = IsEmissiveMaterial(Material, Candidate->EmissiveTexture.Get());
					Candidate->bLooksTransparent = IsTransparentMaterial(Material, Candidate->OpacityTexture.Get());
					const bool bLooksGlass = IsSceneGlassMaterial(Material);
					if (Candidate->bIsPBRStudioMaterial)
					{
						Candidate->ReplacementKind = EPBRSceneReplacementKind::NotReplaceable;
						Candidate->ReplaceMode = TEXT("不可替换");
						Candidate->Status = TEXT("不可替换: PBRStudio 材质");
					}
					else if (bLooksGlass)
					{
						Candidate->ReplacementKind = EPBRSceneReplacementKind::Glass;
						Candidate->bCanReplace = true;
						Candidate->bUseBPRReplacement = false;
						Candidate->ReplaceMode = TEXT("普通替换");
						Candidate->Status = TEXT("普通替换: 玻璃");
					}
					else if (Candidate->bLooksEmissive)
					{
						Candidate->ReplacementKind = EPBRSceneReplacementKind::Emissive;
						Candidate->bCanReplace = true;
						Candidate->bUseBPRReplacement = false;
						Candidate->ReplaceMode = TEXT("普通替换");
						Candidate->Status = TEXT("普通替换: 自发光");
					}
					else if (Candidate->bLooksTransparent)
					{
						Candidate->ReplacementKind = EPBRSceneReplacementKind::Simple;
						Candidate->bCanReplace = true;
						Candidate->bUseBPRReplacement = false;
						Candidate->ReplaceMode = TEXT("普通替换");
						Candidate->Status = TEXT("普通替换: 半透明");
					}
					else if (!Candidate->BaseColorTexture.IsValid())
					{
						Candidate->ReplacementKind = EPBRSceneReplacementKind::Simple;
						Candidate->bCanReplace = true;
						Candidate->bUseBPRReplacement = false;
						Candidate->ReplaceMode = TEXT("普通替换");
						Candidate->Status = TEXT("普通替换: 无基础色贴图，继承原材质颜色");
					}
					else if (!SourcePath.IsEmpty() && !IsSupportedImagePath(SourcePath))
					{
						Candidate->ReplacementKind = EPBRSceneReplacementKind::NotReplaceable;
						Candidate->ReplaceMode = TEXT("不可替换");
						Candidate->Status = TEXT("不可替换: 基础图后缀不支持");
					}
					else
					{
						Candidate->ReplacementKind = EPBRSceneReplacementKind::BPR;
						Candidate->bCanReplace = true;
						Candidate->bUseBPRReplacement = true;
						Candidate->ReplaceMode = TEXT("BPR替换");
						Candidate->Status = TEXT("BPR替换");
					}
				}

				FPBRSceneMaterialSlot Slot;
				Slot.Component = Component;
				Slot.ComponentPath = FSoftObjectPath(Component);
				Slot.MaterialIndex = Index;
				Slot.OriginalMaterial = Material;
				Slot.OriginalMaterialPath = FSoftObjectPath(Material);
				Candidate->Slots.Add(Slot);
			}
		}
	}

	ByMaterial.GenerateValueArray(OutCandidates);
	OutCandidates.Sort([](const TSharedPtr<FPBRSceneMaterialCandidate>& A, const TSharedPtr<FPBRSceneMaterialCandidate>& B)
	{
		return A.IsValid() && B.IsValid() ? A->MaterialName < B->MaterialName : A.IsValid();
	});
}

static bool DownsamplePixelsToMaxDimension(TArray<FColor>& Pixels, int32& Width, int32& Height, int32 MaxDimension)
{
	if (MaxDimension <= 0 || Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
	{
		return true;
	}

	const int32 SourceMax = FMath::Max(Width, Height);
	if (SourceMax <= MaxDimension)
	{
		return true;
	}

	const float Scale = static_cast<float>(MaxDimension) / static_cast<float>(SourceMax);
	const int32 NewWidth = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Width) * Scale));
	const int32 NewHeight = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Height) * Scale));
	TArray<FColor> Resized;
	Resized.SetNumUninitialized(NewWidth * NewHeight);

	for (int32 y = 0; y < NewHeight; ++y)
	{
		const int32 SourceY = FMath::Clamp(FMath::FloorToInt((static_cast<float>(y) + 0.5f) / Scale), 0, Height - 1);
		for (int32 x = 0; x < NewWidth; ++x)
		{
			const int32 SourceX = FMath::Clamp(FMath::FloorToInt((static_cast<float>(x) + 0.5f) / Scale), 0, Width - 1);
			Resized[y * NewWidth + x] = Pixels[SourceY * Width + SourceX];
		}
	}

	Pixels = MoveTemp(Resized);
	Width = NewWidth;
	Height = NewHeight;
	return true;
}

bool FPBRSceneMaterialReplacer::LoadTexturePixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, int32 MaxDimension)
{
	OutPixels.Reset();
	OutWidth = 0;
	OutHeight = 0;
	if (!Texture)
	{
		return false;
	}

	const FString SourcePath = TextureSourceFilename(Texture);
	if (!SourcePath.IsEmpty() && FPaths::FileExists(SourcePath) && IsSupportedImagePath(SourcePath))
	{
		FImage LoadedImage;
		if (FImageUtils::LoadImage(*SourcePath, LoadedImage) && LoadedImage.IsImageInfoValid() && LoadedImage.GetNumPixels() > 0)
		{
			LoadedImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			OutWidth = LoadedImage.SizeX;
			OutHeight = LoadedImage.SizeY;
			const TArrayView64<const FColor> LoadedPixels = LoadedImage.AsBGRA8();
			OutPixels.SetNumUninitialized(LoadedPixels.Num());
			FMemory::Memcpy(OutPixels.GetData(), LoadedPixels.GetData(), LoadedPixels.Num() * sizeof(FColor));
			return OutPixels.Num() == OutWidth * OutHeight && DownsamplePixelsToMaxDimension(OutPixels, OutWidth, OutHeight, MaxDimension);
		}
	}

	FImage SourceImage;
	if (FImageUtils::GetTexture2DSourceImage(Texture, SourceImage) && SourceImage.IsImageInfoValid() && SourceImage.GetNumPixels() > 0)
	{
		SourceImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		OutWidth = SourceImage.SizeX;
		OutHeight = SourceImage.SizeY;
		const TArrayView64<const FColor> SourcePixels = SourceImage.AsBGRA8();
		OutPixels.SetNumUninitialized(SourcePixels.Num());
		FMemory::Memcpy(OutPixels.GetData(), SourcePixels.GetData(), SourcePixels.Num() * sizeof(FColor));
		return OutPixels.Num() == OutWidth * OutHeight && DownsamplePixelsToMaxDimension(OutPixels, OutWidth, OutHeight, MaxDimension);
	}

	Texture->WaitForPendingInitOrStreaming();
	if (!Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return false;
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	OutWidth = Mip.SizeX;
	OutHeight = Mip.SizeY;
	void* Data = Mip.BulkData.Lock(LOCK_READ_ONLY);
	if (!Data)
	{
		Mip.BulkData.Unlock();
		return false;
	}
	OutPixels.SetNumUninitialized(OutWidth * OutHeight);
	FMemory::Memcpy(OutPixels.GetData(), Data, OutPixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	return DownsamplePixelsToMaxDimension(OutPixels, OutWidth, OutHeight, MaxDimension);
}

static uint8 ClampByte(float Value)
{
	return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value), 0, 255));
}

static uint8 Luminance(const FColor& C)
{
	return ClampByte(0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B);
}

static int32 GetSafeSceneReplaceOutputSize(const FPBRSceneReplaceSettings& Settings)
{
	return FMath::Clamp(Settings.OutputSize > 0 ? Settings.OutputSize : 2048, 256, 2048);
}

static bool BuildHeightMap(const TArray<FColor>& ColorPixels, int32 Width, int32 Height, TArray<uint8>& OutHeightMap)
{
	if (ColorPixels.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		return false;
	}

	OutHeightMap.SetNumUninitialized(Width * Height);
	for (int32 i = 0; i < ColorPixels.Num(); ++i)
	{
		OutHeightMap[i] = Luminance(ColorPixels[i]);
	}
	return true;
}

static void CalculateLocalRange(const TArray<uint8>& HeightMap, int32 Width, int32 Height, int32 X, int32 Y, float& OutLocalMin, float& OutLocalMax)
{
	OutLocalMin = 255.0f;
	OutLocalMax = 0.0f;
	for (int32 yy = FMath::Max(0, Y - 2); yy <= FMath::Min(Height - 1, Y + 2); ++yy)
	{
		for (int32 xx = FMath::Max(0, X - 2); xx <= FMath::Min(Width - 1, X + 2); ++xx)
		{
			const uint8 V = HeightMap[yy * Width + xx];
			OutLocalMin = FMath::Min(OutLocalMin, static_cast<float>(V));
			OutLocalMax = FMath::Max(OutLocalMax, static_cast<float>(V));
		}
	}
}

static bool SaveGeneratedPixelsImage(const FString& FilePath, const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
	if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		return false;
	}

	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(FilePath));
	const FImageView ImageView(Pixels.GetData(), Width, Height, EGammaSpace::sRGB);
	return FImageUtils::SaveImageByExtension(*FilePath, ImageView, 100);
}

static bool SaveGeneratedSolidImage(const FString& FilePath, const FColor& Color, int32 Width, int32 Height)
{
	TArray<FColor> Pixels;
	Pixels.Init(Color, Width * Height);
	return SaveGeneratedPixelsImage(FilePath, Pixels, Width, Height);
}

static bool GenerateAndSaveNormalImage(const FString& FilePath, const TArray<uint8>& HeightMap, int32 Width, int32 Height, float NormalStrength)
{
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 y = 0; y < Height; ++y)
	{
		for (int32 x = 0; x < Width; ++x)
		{
			const int32 Index = y * Width + x;
			const int32 X0 = FMath::Max(0, x - 1);
			const int32 X1 = FMath::Min(Width - 1, x + 1);
			const int32 Y0 = FMath::Max(0, y - 1);
			const int32 Y1 = FMath::Min(Height - 1, y + 1);
			const float Dx = (HeightMap[y * Width + X1] - HeightMap[y * Width + X0]) / 255.0f;
			const float Dy = (HeightMap[Y1 * Width + x] - HeightMap[Y0 * Width + x]) / 255.0f;
			FVector3f N(-Dx * NormalStrength, Dy * NormalStrength, 1.0f);
			N.Normalize();
			Pixels[Index] = FColor(
				ClampByte((N.X * 0.5f + 0.5f) * 255.0f),
				ClampByte((N.Y * 0.5f + 0.5f) * 255.0f),
				ClampByte(FMath::Clamp(N.Z, 0.0f, 1.0f) * 255.0f),
				255);
		}
	}
	return SaveGeneratedPixelsImage(FilePath, Pixels, Width, Height);
}

static bool GenerateAndSaveRoughnessImage(const FString& FilePath, const TArray<uint8>& HeightMap, int32 Width, int32 Height, float RoughnessContrast)
{
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 y = 0; y < Height; ++y)
	{
		for (int32 x = 0; x < Width; ++x)
		{
			float LocalMin = 255.0f;
			float LocalMax = 0.0f;
			CalculateLocalRange(HeightMap, Width, Height, x, y, LocalMin, LocalMax);
			const uint8 Roughness = ClampByte(FMath::Clamp((LocalMax - LocalMin) * RoughnessContrast, 35.0f, 235.0f));
			Pixels[y * Width + x] = FColor(Roughness, Roughness, Roughness, 255);
		}
	}
	return SaveGeneratedPixelsImage(FilePath, Pixels, Width, Height);
}

static bool GenerateAndSaveAOImage(const FString& FilePath, const TArray<uint8>& HeightMap, int32 Width, int32 Height, float AOStrength)
{
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 y = 0; y < Height; ++y)
	{
		for (int32 x = 0; x < Width; ++x)
		{
			float LocalMin = 255.0f;
			float LocalMax = 0.0f;
			CalculateLocalRange(HeightMap, Width, Height, x, y, LocalMin, LocalMax);
			const int32 Index = y * Width + x;
			const uint8 AO = ClampByte(FMath::Clamp(255.0f - FMath::Max(0.0f, (LocalMax - HeightMap[Index]) * AOStrength), 0.0f, 255.0f));
			Pixels[Index] = FColor(AO, AO, AO, 255);
		}
	}
	return SaveGeneratedPixelsImage(FilePath, Pixels, Width, Height);
}

static bool GenerateAndSaveHeightImage(const FString& FilePath, const TArray<uint8>& HeightMap, int32 Width, int32 Height)
{
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 i = 0; i < HeightMap.Num(); ++i)
	{
		const uint8 H = HeightMap[i];
		Pixels[i] = FColor(H, H, H, 255);
	}
	return SaveGeneratedPixelsImage(FilePath, Pixels, Width, Height);
}

static bool GenerateAndSaveORMImage(const FString& FilePath, const TArray<uint8>& HeightMap, int32 Width, int32 Height, bool bLooksMetal, float RoughnessContrast, float AOStrength)
{
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	const uint8 Metallic = bLooksMetal ? 255 : 0;
	for (int32 y = 0; y < Height; ++y)
	{
		for (int32 x = 0; x < Width; ++x)
		{
			float LocalMin = 255.0f;
			float LocalMax = 0.0f;
			CalculateLocalRange(HeightMap, Width, Height, x, y, LocalMin, LocalMax);
			const int32 Index = y * Width + x;
			const uint8 Roughness = ClampByte(FMath::Clamp((LocalMax - LocalMin) * RoughnessContrast, 35.0f, 235.0f));
			const uint8 AO = ClampByte(FMath::Clamp(255.0f - FMath::Max(0.0f, (LocalMax - HeightMap[Index]) * AOStrength), 0.0f, 255.0f));
			Pixels[Index] = FColor(Metallic, Roughness, AO, 255);
		}
	}
	return SaveGeneratedPixelsImage(FilePath, Pixels, Width, Height);
}

bool FPBRSceneMaterialReplacer::SaveGeneratedImage(const FString& FilePath, const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
	if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		return false;
	}

	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(FilePath));
	const FImageView ImageView(Pixels.GetData(), Width, Height, EGammaSpace::sRGB);
	return FImageUtils::SaveImageByExtension(*FilePath, ImageView, 100);
}

bool FPBRSceneMaterialReplacer::GeneratePBRSetFromTexture(UTexture2D* BaseColorTexture, const FString& MaterialName,
	const FPBRSceneReplaceSettings& Settings, const FPBRSceneMaterialCandidate& Candidate, FPBRMaterialSet& OutSet, FString& OutMessage)
{
	OutSet = FPBRMaterialSet();
	OutMessage.Empty();
	UTexture2D* EmissiveTexture = Candidate.EmissiveTexture.Get();
	UTexture2D* OpacityTexture = Candidate.OpacityTexture.Get();
	const bool bLooksEmissive = Candidate.bLooksEmissive || Candidate.ReplacementKind == EPBRSceneReplacementKind::Emissive;
	const bool bHasBaseColorTexture = BaseColorTexture != nullptr;
	const int32 MaxOutputSize = GetSafeSceneReplaceOutputSize(Settings);

	TArray<FColor> ColorPixels;
	int32 Width = 0;
	int32 Height = 0;
	if (!LoadTexturePixels(BaseColorTexture, ColorPixels, Width, Height, MaxOutputSize))
	{
		TArray<FColor> SeedPixels;
		UTexture2D* SeedTexture = (bLooksEmissive && EmissiveTexture) ? EmissiveTexture : OpacityTexture;
		if (SeedTexture && LoadTexturePixels(SeedTexture, SeedPixels, Width, Height, MaxOutputSize))
		{
			if (bLooksEmissive && SeedTexture == EmissiveTexture)
			{
				ColorPixels = MoveTemp(SeedPixels);
			}
			else
			{
				ColorPixels.Init(FColor(128, 128, 128, 255), Width * Height);
			}
		}
		else
		{
			Width = 512;
			Height = 512;
			ColorPixels.Init(FColor(128, 128, 128, 255), Width * Height);
		}
	}

	const FString CleanName = FPBRMaterialInstanceFactory::SanitizeAssetName(MaterialName);
	const FString ProjectSavedDir = FPaths::ProjectSavedDir() / TEXT("PBRStudio") / TEXT("SceneReplace") / CleanName;
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ProjectSavedDir);

	TArray<uint8> HeightMap;
	if (!BuildHeightMap(ColorPixels, Width, Height, HeightMap))
	{
		OutMessage = TEXT("读取基础颜色贴图失败");
		return false;
	}

	const FString DetectionText = MaterialName.ToLower();
	const bool bLooksMetal = DetectionText.Contains(TEXT("metal")) || DetectionText.Contains(TEXT("steel")) ||
		DetectionText.Contains(TEXT("iron")) || DetectionText.Contains(TEXT("copper")) ||
		DetectionText.Contains(TEXT("aluminum")) || DetectionText.Contains(TEXT("chrome")) ||
		DetectionText.Contains(TEXT("brass")) || DetectionText.Contains(TEXT("bronze"));

	const FString BaseColorPath = ProjectSavedDir / (CleanName + TEXT("_BaseColor.png"));
	const FString NormalPath = ProjectSavedDir / (CleanName + TEXT("_NormalDX.png"));
	const FString RoughnessPath = ProjectSavedDir / (CleanName + TEXT("_Roughness.png"));
	const FString MetallicPath = ProjectSavedDir / (CleanName + TEXT("_Metallic.png"));
	const FString AOPath = ProjectSavedDir / (CleanName + TEXT("_AO.png"));
	const FString HeightPath = ProjectSavedDir / (CleanName + TEXT("_Height.png"));
	const FString ORMPath = ProjectSavedDir / (CleanName + TEXT("_ORM.png"));
	const FString SpecularPath = ProjectSavedDir / (CleanName + TEXT("_Specular.png"));
	const FString OpacityPath = ProjectSavedDir / (CleanName + TEXT("_Opacity.png"));
	const FString EmissivePath = ProjectSavedDir / (CleanName + TEXT("_Emissive.png"));

	if (bHasBaseColorTexture && !SaveGeneratedImage(BaseColorPath, ColorPixels, Width, Height))
	{
		OutMessage = TEXT("保存生成贴图失败");
		return false;
	}
	if (Settings.bGenerateNormal && !GenerateAndSaveNormalImage(NormalPath, HeightMap, Width, Height, 1.0f))
	{
		OutMessage = TEXT("保存法线贴图失败");
		return false;
	}
	if (Settings.bGenerateRoughness && !GenerateAndSaveRoughnessImage(RoughnessPath, HeightMap, Width, Height, Settings.RoughnessContrast))
	{
		OutMessage = TEXT("保存粗糙度贴图失败");
		return false;
	}
	if (Settings.bGenerateMetallic && !SaveGeneratedSolidImage(MetallicPath, bLooksMetal ? FColor::White : FColor::Black, Width, Height))
	{
		OutMessage = TEXT("保存金属贴图失败");
		return false;
	}
	if (Settings.bGenerateAO && !GenerateAndSaveAOImage(AOPath, HeightMap, Width, Height, Settings.AOStrength))
	{
		OutMessage = TEXT("保存环境遮蔽贴图失败");
		return false;
	}
	if (Settings.bGenerateHeight)
	{
		if (!GenerateAndSaveHeightImage(HeightPath, HeightMap, Width, Height))
		{
			OutMessage = TEXT("保存高度贴图失败");
			return false;
		}
	}
	if (Settings.bGenerateORM)
	{
		if (!GenerateAndSaveORMImage(ORMPath, HeightMap, Width, Height, bLooksMetal, Settings.RoughnessContrast, Settings.AOStrength))
		{
			OutMessage = TEXT("保存 ORM 贴图失败");
			return false;
		}
	}
	if (Settings.bGenerateSpecular)
	{
		if (!SaveGeneratedSolidImage(SpecularPath, FColor(128, 128, 128, 255), Width, Height))
		{
			OutMessage = TEXT("保存高光贴图失败");
			return false;
		}
	}
	if (Settings.bGenerateOpacity)
	{
		TArray<FColor> OpacityPixels;
		OpacityPixels.Init(FColor::White, Width * Height);
		if (OpacityTexture)
		{
			TArray<FColor> LoadedOpacityPixels;
			int32 OpacityWidth = 0;
			int32 OpacityHeight = 0;
			if (LoadTexturePixels(OpacityTexture, LoadedOpacityPixels, OpacityWidth, OpacityHeight, MaxOutputSize) &&
				OpacityWidth == Width && OpacityHeight == Height)
			{
				OpacityPixels = MoveTemp(LoadedOpacityPixels);
			}
		}
		if (!SaveGeneratedPixelsImage(OpacityPath, OpacityPixels, Width, Height))
		{
			OutMessage = TEXT("保存透明贴图失败");
			return false;
		}
	}
	if (Settings.bGenerateEmissive && bLooksEmissive)
	{
		TArray<FColor> EmissivePixels = ColorPixels;
		if (EmissiveTexture && EmissiveTexture != BaseColorTexture)
		{
			TArray<FColor> LoadedEmissivePixels;
			int32 EmissiveWidth = 0;
			int32 EmissiveHeight = 0;
			if (LoadTexturePixels(EmissiveTexture, LoadedEmissivePixels, EmissiveWidth, EmissiveHeight, MaxOutputSize) &&
				EmissiveWidth == Width && EmissiveHeight == Height)
			{
				EmissivePixels = MoveTemp(LoadedEmissivePixels);
			}
		}
		if (!SaveGeneratedPixelsImage(EmissivePath, EmissivePixels, Width, Height))
		{
			OutMessage = TEXT("保存自发光贴图失败");
			return false;
		}
	}

	OutSet.Name = CleanName;
	OutSet.Folder = ProjectSavedDir;
	if (bHasBaseColorTexture)
	{
		OutSet.Channels.Add(FPBRChannels::BaseColor.ToString(), BaseColorPath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::BaseColor, Candidate.BaseColorTexture.Get());
	}
	if (Settings.bGenerateNormal)
	{
		OutSet.Channels.Add(FPBRChannels::NormalDX.ToString(), NormalPath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::NormalDX, Candidate.NormalTexture.Get());
	}
	if (Settings.bGenerateRoughness)
	{
		OutSet.Channels.Add(FPBRChannels::Roughness.ToString(), RoughnessPath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::Roughness, Candidate.RoughnessTexture.Get());
	}
	if (Settings.bGenerateMetallic)
	{
		OutSet.Channels.Add(FPBRChannels::Metallic.ToString(), MetallicPath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::Metallic, Candidate.MetallicTexture.Get());
	}
	if (Settings.bGenerateAO)
	{
		OutSet.Channels.Add(FPBRChannels::AO.ToString(), AOPath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::AO, Candidate.AOTexture.Get());
	}
	if (Settings.bGenerateHeight)
	{
		OutSet.Channels.Add(FPBRChannels::Height.ToString(), HeightPath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::Height, Candidate.HeightTexture.Get());
	}
	if (Settings.bGenerateORM)
	{
		OutSet.Channels.Add(FPBRChannels::ORM.ToString(), ORMPath);
	}
	if (Settings.bGenerateSpecular)
	{
		OutSet.Channels.Add(FPBRChannels::Specular.ToString(), SpecularPath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::Specular, Candidate.SpecularTexture.Get());
	}
	if (Settings.bGenerateOpacity)
	{
		OutSet.Channels.Add(FPBRChannels::Opacity.ToString(), OpacityPath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::Opacity, Candidate.OpacityTexture.Get());
	}
	else
	{
		AddOriginalTextureChannel(OutSet, FPBRChannels::Opacity, Candidate.OpacityTexture.Get());
	}
	if (Settings.bGenerateEmissive && bLooksEmissive)
	{
		OutSet.Channels.Add(FPBRChannels::Emissive.ToString(), EmissivePath);
		AddOriginalTextureChannel(OutSet, FPBRChannels::Emissive, Candidate.EmissiveTexture.Get());
	}
	else if (bLooksEmissive)
	{
		AddOriginalTextureChannel(OutSet, FPBRChannels::Emissive, Candidate.EmissiveTexture.Get());
	}
	OutMessage = TEXT("已生成 PBR 套图");
	return true;
}

bool FPBRSceneMaterialReplacer::ReplaceCandidates(
	const TArray<TSharedPtr<FPBRSceneMaterialCandidate>>& Candidates,
	const FPBRSceneReplaceSettings& Settings,
	FPBRSceneReplaceResult& OutResult)
{
	OutResult = FPBRSceneReplaceResult();
	LastReplacementSlots.Reset();
	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "ReplaceSceneMaterials", "PBRStudio 替换场景材质"));
	TSet<TWeakObjectPtr<UPrimitiveComponent>> AllChangedComponents;
	TSet<TWeakObjectPtr<UMaterialInterface>> AllChangedMaterials;
	auto ReportProgress = [&Settings](int32 Current, int32 Total, const FString& Status)
	{
		if (Settings.ProgressCallback)
		{
			Settings.ProgressCallback(Current, Total, Status);
		}
	};

	int32 TotalToReplace = 0;
	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Candidate : Candidates)
	{
		if (Candidate.IsValid() && Candidate->bCanReplace && Candidate->bChecked && !Candidate->bIsPBRStudioMaterial)
		{
			TotalToReplace++;
		}
	}

	int32 Processed = 0;
	ReportProgress(0, TotalToReplace, FString::Printf(TEXT("准备替换 %d 个材质"), TotalToReplace));

	for (const TSharedPtr<FPBRSceneMaterialCandidate>& Candidate : Candidates)
	{
		if (!Candidate.IsValid())
		{
			continue;
		}
		OutResult.ScannedMaterials++;
		if (!Candidate->bCanReplace || !Candidate->bChecked || Candidate->bIsPBRStudioMaterial)
		{
			continue;
		}

		OutResult.ReplaceableMaterials++;
		const int32 CurrentIndex = Processed + 1;
		ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("正在处理 %d/%d: %s"), CurrentIndex, TotalToReplace, *Candidate->MaterialName));
		UMaterialInterface* SourceMaterial = Candidate->Material.Get();
		if (!SourceMaterial)
		{
			Candidate->Status = TEXT("不可替换: 原材质无效");
			OutResult.Messages.Add(Candidate->MaterialName + TEXT(": 原材质无效"));
			Processed++;
			ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("跳过 %d/%d: 原材质无效"), Processed, TotalToReplace));
			continue;
		}

		const FString OutputName = Candidate->OutputMaterialName.TrimStartAndEnd().IsEmpty()
			? Candidate->MaterialName
			: Candidate->OutputMaterialName.TrimStartAndEnd();

		FString ConvertMessage;
		ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("转换材质实例 %d/%d: %s"), CurrentIndex, TotalToReplace, *OutputName));
		UMaterialInterface* NewMaterial = CreateOrUpdateSceneManagedReplacementInstance(
			*Candidate,
			OutputName,
			Settings,
			SourceMaterial,
			ConvertMessage);
		if (!NewMaterial)
		{
			OutResult.Messages.Add(Candidate->MaterialName + TEXT(": ") + ConvertMessage);
			Processed++;
			ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("跳过 %d/%d: %s"), Processed, TotalToReplace, *ConvertMessage));
			continue;
		}

		int32 ChangedSlots = 0;
		for (const FPBRSceneMaterialSlot& Slot : Candidate->Slots)
		{
			UPrimitiveComponent* Component = Slot.Component.Get();
			if (!Component || Slot.MaterialIndex == INDEX_NONE)
			{
				continue;
			}
			LastReplacementSlots.Add(Slot);
			Component->Modify();
			Component->SetMaterial(Slot.MaterialIndex, NewMaterial);
			AllChangedComponents.Add(Component);
			if (Component->GetOwner())
			{
				Component->GetOwner()->Modify();
				Component->GetOwner()->MarkPackageDirty();
			}
			ChangedSlots++;
		}

		if (ChangedSlots > 0)
		{
			AllChangedMaterials.Add(NewMaterial);
			OutResult.ReplacedMaterials++;
			OutResult.ReplacedSlots += ChangedSlots;
			Candidate->Status = FString::Printf(TEXT("已%s %d 个槽位: %s"), *Candidate->ReplaceMode, ChangedSlots, *OutputName);
		}
		Processed++;
		ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("已处理 %d/%d: %s"), Processed, TotalToReplace, *OutputName));
	}

	if (OutResult.ReplacedSlots > 0)
	{
		ReportProgress(TotalToReplace, TotalToReplace, TEXT("正在更新场景显示..."));
		RefreshSceneAfterMaterialReplacement(AllChangedComponents, AllChangedMaterials);
	}

	OutResult.Messages.Add(FString::Printf(TEXT("完成: 替换 %d 个材质, %d 个槽位"), OutResult.ReplacedMaterials, OutResult.ReplacedSlots));
	ReportProgress(TotalToReplace, TotalToReplace, OutResult.Messages.Last());
	return OutResult.ReplacedSlots > 0;
}

int32 FPBRSceneMaterialReplacer::UndoLastReplacement(FPBRSceneReplaceResult& OutResult)
{
	OutResult = FPBRSceneReplaceResult();
	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "UndoSceneMaterialReplacement", "PBRStudio 撤回场景材质替换"));
	int32 Count = 0;
	for (const FPBRSceneMaterialSlot& Slot : LastReplacementSlots)
	{
		UPrimitiveComponent* Component = Slot.Component.Get();
		UMaterialInterface* Original = Slot.OriginalMaterial.Get();
		if (!Component && Slot.ComponentPath.IsValid())
		{
			Component = Cast<UPrimitiveComponent>(Slot.ComponentPath.ResolveObject());
		}
		if (!Original && Slot.OriginalMaterialPath.IsValid())
		{
			Original = Cast<UMaterialInterface>(Slot.OriginalMaterialPath.TryLoad());
		}
		if (!Component || !Original || Slot.MaterialIndex == INDEX_NONE)
		{
			continue;
		}
		Component->Modify();
		Component->SetMaterial(Slot.MaterialIndex, Original);
		RefreshPrimitiveAfterMaterialChange(Component);
		if (Component->GetOwner())
		{
			Component->GetOwner()->Modify();
			Component->GetOwner()->MarkPackageDirty();
		}
		Count++;
	}
	LastReplacementSlots.Reset();
	OutResult.ReplacedSlots = Count;
	OutResult.Messages.Add(FString::Printf(TEXT("已撤回 %d 个材质槽位"), Count));
	return Count;
}
