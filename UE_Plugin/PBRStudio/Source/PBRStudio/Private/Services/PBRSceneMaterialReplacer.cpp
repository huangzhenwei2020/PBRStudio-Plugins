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
#include "FileHelpers.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
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
#include "MaterialShared.h"
#include "MaterialEditingLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "Components/PrimitiveComponent.h"
#include "Services/PBRMaterialFactory.h"
#include "Services/PBRMaterialInstanceFactory.h"
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
	if (Path.Contains(TEXT("/PBRStudio/")) || Path.Contains(TEXT("/PBRStudio.")))
	{
		return true;
	}

	if (const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
	{
		UMaterialInterface* Parent = Instance->Parent;
		if (Parent)
		{
			const FString ParentPath = Parent->GetPathName();
			const FString ParentName = Parent->GetName();
			if (ParentPath.Contains(TEXT("/PBRStudio/")) || ParentName.StartsWith(TEXT("M_PBR_")))
			{
				return true;
			}
		}
	}

	const FString Name = Material->GetName();
	return Name.StartsWith(TEXT("MI_PBRSR_")) || Name.StartsWith(TEXT("MI_PBRR_")) || Name.StartsWith(TEXT("MI_PBR_"));
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
		TEXT("LightMap"), TEXT("自发光"), TEXT("发光贴图")
	};

	if (UTexture2D* Texture = FindTextureInInstanceOverrides(Material, PreferredParams, OutSourcePath))
	{
		return Texture;
	}
	if (UTexture2D* Texture = FindTextureInInstanceOverridesByTokens(
		Material,
		{ TEXT("emissive"), TEXT("emission"), TEXT("emit"), TEXT("selfillum"), TEXT("self_illum"), TEXT("glow"), TEXT("light") },
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
		const bool bLooksEmissive =
			Name.Contains(TEXT("emissive")) || Name.Contains(TEXT("emission")) ||
			Name.Contains(TEXT("emit")) || Name.Contains(TEXT("selfillum")) ||
			Name.Contains(TEXT("self_illum")) || Name.Contains(TEXT("glow")) ||
			Name.Contains(TEXT("light"));
		if (bLooksEmissive)
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

	const FString Name = Material->GetName().ToLower();
	if (Name.Contains(TEXT("light")) || Name.Contains(TEXT("emissive")) ||
		Name.Contains(TEXT("emission")) || Name.Contains(TEXT("selfillum")) ||
		Name.Contains(TEXT("glow")) || Name.Contains(TEXT("neon")) ||
		Name.Contains(TEXT("灯")) || Name.Contains(TEXT("发光")))
	{
		return true;
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

	const EBlendMode BlendMode = Material->GetBlendMode();
	if (BlendMode == BLEND_Translucent ||
		BlendMode == BLEND_Additive ||
		BlendMode == BLEND_AlphaComposite ||
		BlendMode == BLEND_AlphaHoldout ||
		BlendMode == BLEND_TranslucentColoredTransmittance)
	{
		return true;
	}

	const FString Name = Material->GetName().ToLower();
	if (Name.Contains(TEXT("glass")) || Name.Contains(TEXT("window")) ||
		Name.Contains(TEXT("transparent")) || Name.Contains(TEXT("translucent")) ||
		Name.Contains(TEXT("crystal")) || Name.Contains(TEXT("acrylic")) ||
		Name.Contains(TEXT("pane")) || Name.Contains(TEXT("玻璃")) ||
		Name.Contains(TEXT("透明")) || Name.Contains(TEXT("窗")))
	{
		return true;
	}

	return OpacityTexture != nullptr;
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
					if (Candidate->bIsPBRStudioMaterial)
					{
						Candidate->ReplacementKind = EPBRSceneReplacementKind::NotReplaceable;
						Candidate->ReplaceMode = TEXT("不可替换");
						Candidate->Status = TEXT("不可替换: PBRStudio 材质");
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
						Candidate->ReplacementKind = EPBRSceneReplacementKind::Glass;
						Candidate->bCanReplace = true;
						Candidate->bUseBPRReplacement = false;
						Candidate->ReplaceMode = TEXT("普通替换");
						Candidate->Status = TEXT("普通替换: 普通玻璃");
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
		UTexture2D* BaseTexture = Candidate->BaseColorTexture.Get();
		if (Candidate->bUseBPRReplacement && !BaseTexture)
		{
			Candidate->ReplaceMode = TEXT("不可替换");
			Candidate->Status = TEXT("不可替换: 未找到基础颜色贴图");
			OutResult.Messages.Add(Candidate->MaterialName + TEXT(": 未找到基础颜色贴图"));
			Processed++;
			ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("跳过 %d/%d: %s 缺少基础颜色贴图"), Processed, TotalToReplace, *Candidate->MaterialName));
			continue;
		}

		FPBRMaterialSet GeneratedSet;
		FString GenerateMessage;
		const FString OutputName = Candidate->OutputMaterialName.TrimStartAndEnd().IsEmpty()
			? Candidate->MaterialName
			: Candidate->OutputMaterialName.TrimStartAndEnd();
		FPBRSceneReplaceSettings EffectiveSettings = Settings;
		if (!Candidate->bUseBPRReplacement)
		{
			EffectiveSettings.bGenerateNormal = false;
			EffectiveSettings.bGenerateRoughness = false;
			EffectiveSettings.bGenerateMetallic = false;
			EffectiveSettings.bGenerateAO = false;
			EffectiveSettings.bGenerateSpecular = false;
			EffectiveSettings.bGenerateOpacity = Candidate->ReplacementKind == EPBRSceneReplacementKind::Glass && Candidate->OpacityTexture.IsValid();
			EffectiveSettings.bGenerateHeight = false;
			EffectiveSettings.bGenerateORM = false;
		}
		if (Candidate->ReplacementKind == EPBRSceneReplacementKind::Emissive)
		{
			EffectiveSettings.bGenerateEmissive = true;
		}
		ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("生成贴图 %d/%d: %s"), CurrentIndex, TotalToReplace, *OutputName));
		if (!GeneratePBRSetFromTexture(
			BaseTexture,
			OutputName,
			EffectiveSettings,
			*Candidate,
			GeneratedSet,
			GenerateMessage))
		{
			OutResult.Messages.Add(Candidate->MaterialName + TEXT(": ") + GenerateMessage);
			Processed++;
			ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("跳过 %d/%d: %s"), Processed, TotalToReplace, *GenerateMessage));
			continue;
		}

		FString MasterMessage;
		UMaterial* SceneReplaceMaster = nullptr;
		if (Candidate->ReplacementKind == EPBRSceneReplacementKind::Glass)
		{
			SceneReplaceMaster = EnsureSceneGlassMasterMaterial(MasterMessage);
		}
		else if (Candidate->ReplacementKind == EPBRSceneReplacementKind::Emissive)
		{
			SceneReplaceMaster = EnsureSceneEmissiveMasterMaterial(MasterMessage);
		}
		else
		{
			SceneReplaceMaster = EnsureSceneReplaceMasterMaterial(MasterMessage);
		}
		if (!SceneReplaceMaster)
		{
			OutResult.Messages.Add(Candidate->MaterialName + TEXT(": ") + MasterMessage);
			Processed++;
			ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("跳过 %d/%d: %s"), Processed, TotalToReplace, *MasterMessage));
			continue;
		}

		FPBRMaterialCreateOptions Options;
		Options.PackageRoot = Settings.OutputRoot;
		Options.MaterialInstancePrefix = TEXT("");
		Options.MaterialType = Candidate->ReplacementKind == EPBRSceneReplacementKind::Glass
			? EPBRMaterialType::Glass
			: (Candidate->ReplacementKind == EPBRSceneReplacementKind::Emissive ? EPBRMaterialType::Emissive : EPBRMaterialType::Standard);
		Options.NormalPreference = TEXT("DirectX");
		Options.bCreateIsolatedMaterialFolder = true;
		Options.bImportTextures = true;
		Options.ParentMaterialOverride = SceneReplaceMaster;
		Options.bEnsureExampleMaterial = false;
		Options.bAllowExistingAssets = true;

		FPBRMaterialCreateResult CreateResult;
		ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("创建材质实例 %d/%d: %s"), CurrentIndex, TotalToReplace, *OutputName));
		if (!FPBRMaterialInstanceFactory::CreateInstanceFromSet(GeneratedSet, Options, CreateResult))
		{
			OutResult.Messages.Add(Candidate->MaterialName + TEXT(": ") + CreateResult.Message);
			Processed++;
			ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("跳过 %d/%d: %s"), Processed, TotalToReplace, *CreateResult.Message));
			continue;
		}

		UMaterialInterface* NewMaterial = CreateResult.MaterialInstance.LoadSynchronous();
		if (!NewMaterial)
		{
			OutResult.Messages.Add(Candidate->MaterialName + TEXT(": 新材质加载失败"));
			Processed++;
			ReportProgress(Processed, TotalToReplace, FString::Printf(TEXT("跳过 %d/%d: 新材质加载失败"), Processed, TotalToReplace));
			continue;
		}
		if (Candidate->bLooksEmissive)
		{
			if (UMaterialInstanceConstant* NewInstance = Cast<UMaterialInstanceConstant>(NewMaterial))
			{
				NewInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 4.0f);
				NewInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), true);
				NewInstance->MarkPackageDirty();
			}
		}
		if (UMaterialInstanceConstant* NewInstance = Cast<UMaterialInstanceConstant>(NewMaterial))
		{
			NewInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, EffectiveSettings.NormalStrength);
			NewInstance->MarkPackageDirty();
		}
		if (!Candidate->BaseColorTexture.IsValid())
		{
			if (UMaterialInstanceConstant* NewInstance = Cast<UMaterialInstanceConstant>(NewMaterial))
			{
				NewInstance->SetVectorParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTint, Candidate->InheritedBaseColor);
				NewInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::BaseColorIntensity, 1.0f);
				NewInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseBaseColorTexture), false);
				NewInstance->MarkPackageDirty();
			}
		}
		if (Candidate->ReplacementKind == EPBRSceneReplacementKind::Glass)
		{
			if (UMaterialInstanceConstant* NewInstance = Cast<UMaterialInstanceConstant>(NewMaterial))
			{
				NewInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.35f);
				NewInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RefractionAmount, 1.52f);
				NewInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::RoughnessValue, 0.05f);
				NewInstance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::SpecularLevel, 0.75f);
				NewInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseOpacityTexture), Candidate->OpacityTexture.IsValid());
				NewInstance->MarkPackageDirty();
			}
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
