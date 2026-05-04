#include "Services/PBRMaterialFactory.h"
#include "Services/PBRDataStore.h"
#include "Services/PBRMaterialInstanceFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/TextureFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "PackageTools.h"
#include "FileHelpers.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "EditorAssetLibrary.h"

// Connection helper: try connecting an expression output to a material input pin.
// In UE5, material properties are accessed via FMaterialInputInfo or the
// UMaterialEditingLibrary.
static bool ConnectExprToMaterialInput(UMaterial* Material, UMaterialExpression* Expression,
	int32 OutputIndex, const FString& InputPinName)
{
	if (!Material || !Expression) return false;

	// Get list of material properties that can receive connections
	TArray<FMaterialParameterInfo> ParamInfo;
	TArray<FGuid> ParamIds;
	Material->GetAllParameterInfoOfType(EMaterialParameterType::Texture, ParamInfo, ParamIds);

	// Direct input connection by pin name
	// The material expression inputs are stored in the Material->GetEditorOnlyData()
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData) return false;

	// Try each known material input
	auto TryConnect = [&](FExpressionInput& Input) -> bool
	{
		if (InputPinName.Contains(TEXT("BaseColor")) || InputPinName == TEXT("MP_BaseColor"))
		{
			Input.Connect(OutputIndex, Expression);
			return true;
		}
		return false;
	};

	// Map pin names to material inputs
	if (InputPinName == TEXT("BaseColor") || InputPinName == TEXT("MP_BaseColor"))
	{
		EditorData->BaseColor.Connect(OutputIndex, Expression);
		return true;
	}
	if (InputPinName == TEXT("Metallic") || InputPinName == TEXT("MP_Metallic"))
	{
		EditorData->Metallic.Connect(OutputIndex, Expression);
		return true;
	}
	if (InputPinName == TEXT("Specular") || InputPinName == TEXT("MP_Specular"))
	{
		EditorData->Specular.Connect(OutputIndex, Expression);
		return true;
	}
	if (InputPinName == TEXT("Roughness") || InputPinName == TEXT("MP_Roughness"))
	{
		EditorData->Roughness.Connect(OutputIndex, Expression);
		return true;
	}
	if (InputPinName == TEXT("EmissiveColor") || InputPinName == TEXT("MP_EmissiveColor"))
	{
		EditorData->EmissiveColor.Connect(OutputIndex, Expression);
		return true;
	}
	if (InputPinName == TEXT("Opacity") || InputPinName == TEXT("MP_Opacity"))
	{
		EditorData->Opacity.Connect(OutputIndex, Expression);
		return true;
	}
	if (InputPinName == TEXT("OpacityMask") || InputPinName == TEXT("MP_OpacityMask"))
	{
		EditorData->OpacityMask.Connect(OutputIndex, Expression);
		return true;
	}
	if (InputPinName == TEXT("Normal") || InputPinName == TEXT("MP_Normal"))
	{
		EditorData->Normal.Connect(OutputIndex, Expression);
		return true;
	}
	if (InputPinName == TEXT("AmbientOcclusion") || InputPinName == TEXT("MP_AmbientOcclusion"))
	{
		EditorData->AmbientOcclusion.Connect(OutputIndex, Expression);
		return true;
	}

	return false;
}

FString FPBRMaterialFactory::GetMaterialPropertyForChannel(const FString& Channel, const FString& TargetMode)
{
	static const TMap<FString, FString> PropertyMap = {
		{ TEXT("BaseColor"),    TEXT("MP_BaseColor") },
		{ TEXT("Roughness"),    TEXT("MP_Roughness") },
		{ TEXT("Metallic"),     TEXT("MP_Metallic") },
		{ TEXT("Specular"),     TEXT("MP_Specular") },
		{ TEXT("Normal"),       TEXT("MP_Normal") },
		{ TEXT("NormalDX"),     TEXT("MP_Normal") },
		{ TEXT("NormalGL"),     TEXT("MP_Normal") },
		{ TEXT("AO"),           TEXT("MP_AmbientOcclusion") },
		{ TEXT("Emissive"),     TEXT("MP_EmissiveColor") },
		{ TEXT("Opacity"),      TEXT("MP_Opacity") },
	};

	const FString* Found = PropertyMap.Find(Channel);
	return Found ? *Found : TEXT("MP_BaseColor");
}

FString FPBRMaterialFactory::TryGetSlotOverride(const FString& TargetMode, const FString& Channel)
{
	TMap<FString, FString> Slots;
	FPBRDataStore::LoadSlotLearning(Slots);
	FString Key = TargetMode + TEXT("|") + Channel;
	const FString* Override = Slots.Find(Key);
	return Override ? *Override : FString();
}

void FPBRMaterialFactory::LearnSlot(const FString& TargetMode, UMaterialInterface* Mat,
	const FString& Channel, const FString& PropertyName)
{
	if (!Mat) return;
	TMap<FString, FString> Slots;
	FPBRDataStore::LoadSlotLearning(Slots);
	FString Key = TargetMode + TEXT("|") + Channel;
	Slots.Add(Key, PropertyName);
	FPBRDataStore::SaveSlotLearning(Slots);
}

static EPBRMaterialType ParseMaterialTypeMode(const FString& Mode)
{
	const FString Normalized = Mode.TrimStartAndEnd().ToLower();
	if (Normalized.Contains(TEXT("木")) || Normalized.Contains(TEXT("wood")) || Normalized.Contains(TEXT("timber")) || Normalized.Contains(TEXT("floor")))
	{
		return EPBRMaterialType::Wood;
	}
	if (Normalized.Contains(TEXT("石")) || Normalized.Contains(TEXT("stone")) || Normalized.Contains(TEXT("rock")) || Normalized.Contains(TEXT("marble")) || Normalized.Contains(TEXT("granite")))
	{
		return EPBRMaterialType::Stone;
	}
	if (Normalized.Contains(TEXT("砖")) || Normalized.Contains(TEXT("瓷砖")) || Normalized.Contains(TEXT("tile")) || Normalized.Contains(TEXT("ceramic")))
	{
		return EPBRMaterialType::Tile;
	}
	if (Normalized.Contains(TEXT("布")) || Normalized.Contains(TEXT("fabric")) || Normalized.Contains(TEXT("cloth")))
	{
		return EPBRMaterialType::Fabric;
	}
	if (Normalized.Contains(TEXT("皮")) || Normalized.Contains(TEXT("leather")))
	{
		return EPBRMaterialType::Leather;
	}
	if (Normalized.Contains(TEXT("塑料")) || Normalized.Contains(TEXT("plastic")) || Normalized.Contains(TEXT("pvc")))
	{
		return EPBRMaterialType::Plastic;
	}
	if (Normalized.Contains(TEXT("金属")) || Normalized.Contains(TEXT("metal")) || Normalized.Contains(TEXT("steel")) || Normalized.Contains(TEXT("iron")) || Normalized.Contains(TEXT("copper")))
	{
		return EPBRMaterialType::Metal;
	}
	if (Normalized.Contains(TEXT("半透明")) || Normalized.Contains(TEXT("透明")) || Normalized.Contains(TEXT("transparent")) || Normalized.Contains(TEXT("opacity")))
	{
		return EPBRMaterialType::Transparent;
	}
	if (Normalized.Contains(TEXT("玻璃")) || Normalized.Contains(TEXT("glass")))
	{
		return EPBRMaterialType::Glass;
	}
	if (Normalized.Contains(TEXT("水")) || Normalized.Contains(TEXT("water")))
	{
		return EPBRMaterialType::Water;
	}
	if (Normalized.Contains(TEXT("自发光")) || Normalized.Contains(TEXT("发光")) || Normalized.Contains(TEXT("emissive")) || Normalized.Contains(TEXT("light")))
	{
		return EPBRMaterialType::Emissive;
	}
	return EPBRMaterialType::Standard;
}

static bool IsAutoMaterialTypeMode(const FString& Mode)
{
	const FString Normalized = Mode.TrimStartAndEnd().ToLower();
	return Normalized.IsEmpty() || Normalized == TEXT("自动") || Normalized == TEXT("auto");
}

static FString BuildMaterialSetTextForDetection(const FPBRMaterialSet& Set)
{
	TArray<FString> Parts = { Set.Name, Set.Folder };
	for (const TPair<FString, FString>& ChannelPair : Set.Channels)
	{
		Parts.Add(ChannelPair.Key);
		Parts.Add(FPaths::GetBaseFilename(ChannelPair.Value));
	}
	return FString::Join(Parts, TEXT(" ")).ToLower();
}

static EPBRMaterialType DetectMaterialTypeFromSet(const FPBRMaterialSet& Set)
{
	const FString Text = BuildMaterialSetTextForDetection(Set);
	const bool bHasMetal = Text.Contains(TEXT("metal")) || Text.Contains(TEXT("steel")) ||
		Text.Contains(TEXT("iron")) || Text.Contains(TEXT("copper")) || Text.Contains(TEXT("aluminum")) ||
		Text.Contains(TEXT("aluminium")) || Text.Contains(TEXT("brass")) || Text.Contains(TEXT("bronze")) ||
		Text.Contains(TEXT("chrome")) || Text.Contains(TEXT("zinc")) || Text.Contains(TEXT("nickel")) ||
		Text.Contains(TEXT("gold")) || Text.Contains(TEXT("silver")) || Text.Contains(TEXT("rust")) ||
		Set.Channels.Contains(FPBRChannels::Metallic.ToString());

	if (Text.Contains(TEXT("water")) || Text.Contains(TEXT("pool")) || Text.Contains(TEXT("ocean")) ||
		Text.Contains(TEXT("river")) || Text.Contains(TEXT("lake")) || Text.Contains(TEXT("水")))
	{
		return EPBRMaterialType::Water;
	}
	if (Text.Contains(TEXT("glass")) || Text.Contains(TEXT("window")) || Text.Contains(TEXT("mirror")) ||
		Text.Contains(TEXT("玻璃")))
	{
		return EPBRMaterialType::Glass;
	}
	if (bHasMetal)
	{
		return EPBRMaterialType::Metal;
	}
	if (Text.Contains(TEXT("curtain")) || Text.Contains(TEXT("sheer")) || Text.Contains(TEXT("transparent")) ||
		Text.Contains(TEXT("opacity")) || Text.Contains(TEXT("alpha")) || Text.Contains(TEXT("半透明")) ||
		Text.Contains(TEXT("透明")) || Set.Channels.Contains(FPBRChannels::Opacity.ToString()))
	{
		return EPBRMaterialType::Transparent;
	}
	if (Text.Contains(TEXT("emissive")) || Text.Contains(TEXT("glow")) || Text.Contains(TEXT("neon")) ||
		Text.Contains(TEXT("发光")) || Text.Contains(TEXT("自发光")) || Set.Channels.Contains(FPBRChannels::Emissive.ToString()))
	{
		return EPBRMaterialType::Emissive;
	}
	if (Text.Contains(TEXT("metal")) || Text.Contains(TEXT("steel")) || Text.Contains(TEXT("iron")) ||
		Text.Contains(TEXT("copper")) || Text.Contains(TEXT("aluminum")) || Text.Contains(TEXT("金属")) ||
		Set.Channels.Contains(FPBRChannels::Metallic.ToString()))
	{
		return EPBRMaterialType::Metal;
	}
	if (Text.Contains(TEXT("fabric")) || Text.Contains(TEXT("cloth")) || Text.Contains(TEXT("linen")) ||
		Text.Contains(TEXT("cotton")) || Text.Contains(TEXT("wool")) || Text.Contains(TEXT("carpet")) ||
		Text.Contains(TEXT("rug")) || Text.Contains(TEXT("curtain")) || Text.Contains(TEXT("布")) ||
		Text.Contains(TEXT("织物")) || Text.Contains(TEXT("地毯")))
	{
		return EPBRMaterialType::Fabric;
	}
	if (Text.Contains(TEXT("leather")) || Text.Contains(TEXT("皮革")) || Text.Contains(TEXT("皮")))
	{
		return EPBRMaterialType::Leather;
	}
	if (Text.Contains(TEXT("wood")) || Text.Contains(TEXT("timber")) || Text.Contains(TEXT("floor")) ||
		Text.Contains(TEXT("木")) || Text.Contains(TEXT("木地板")))
	{
		return EPBRMaterialType::Wood;
	}
	if (Text.Contains(TEXT("tile")) || Text.Contains(TEXT("ceramic")) || Text.Contains(TEXT("brick")) ||
		Text.Contains(TEXT("瓷砖")) || Text.Contains(TEXT("砖")))
	{
		return EPBRMaterialType::Tile;
	}
	if (Text.Contains(TEXT("stone")) || Text.Contains(TEXT("rock")) || Text.Contains(TEXT("marble")) ||
		Text.Contains(TEXT("granite")) || Text.Contains(TEXT("石")) || Text.Contains(TEXT("大理石")))
	{
		return EPBRMaterialType::Stone;
	}
	if (Text.Contains(TEXT("plastic")) || Text.Contains(TEXT("pvc")) || Text.Contains(TEXT("塑料")))
	{
		return EPBRMaterialType::Plastic;
	}

	return EPBRMaterialType::Standard;
}

static EPBRMaterialType ResolveMaterialType(const FPBRMaterialSet& Set, const FString& Mode)
{
	return IsAutoMaterialTypeMode(Mode) ? DetectMaterialTypeFromSet(Set) : ParseMaterialTypeMode(Mode);
}

EPBRMaterialType FPBRMaterialFactory::ResolveMaterialTypeForSet(const FPBRMaterialSet& Set, const FString& MaterialTypeMode)
{
	return ResolveMaterialType(Set, MaterialTypeMode);
}

FString FPBRMaterialFactory::MaterialTypeToDisplayName(EPBRMaterialType Type)
{
	switch (Type)
	{
	case EPBRMaterialType::Wood:			return TEXT("木材");
	case EPBRMaterialType::Stone:			return TEXT("石材");
	case EPBRMaterialType::Tile:			return TEXT("瓷砖");
	case EPBRMaterialType::Fabric:			return TEXT("布艺");
	case EPBRMaterialType::Leather:			return TEXT("皮革");
	case EPBRMaterialType::Plastic:			return TEXT("塑料");
	case EPBRMaterialType::Metal:			return TEXT("金属");
	case EPBRMaterialType::Transparent:		return TEXT("半透明");
	case EPBRMaterialType::Glass:			return TEXT("玻璃");
	case EPBRMaterialType::Water:			return TEXT("水");
	case EPBRMaterialType::Emissive:		return TEXT("自发光");
	default:								return TEXT("标准");
	}
}

static bool MaterialTypeUsesChannel(EPBRMaterialType MaterialType, const FString& Channel)
{
	if (Channel == FPBRChannels::BaseColor.ToString() ||
		Channel == FPBRChannels::Normal.ToString() ||
		Channel == FPBRChannels::NormalDX.ToString() ||
		Channel == FPBRChannels::NormalGL.ToString())
	{
		return MaterialType != EPBRMaterialType::Emissive || Channel == FPBRChannels::BaseColor.ToString();
	}
	if (Channel == FPBRChannels::Roughness.ToString() || Channel == FPBRChannels::Glossiness.ToString())
	{
		return MaterialType != EPBRMaterialType::Emissive && MaterialType != EPBRMaterialType::Water;
	}
	if (Channel == FPBRChannels::Metallic.ToString())
	{
		return MaterialType == EPBRMaterialType::Standard ||
			MaterialType == EPBRMaterialType::Metal ||
			MaterialType == EPBRMaterialType::Plastic;
	}
	if (Channel == FPBRChannels::AO.ToString())
	{
		return MaterialType != EPBRMaterialType::Glass &&
			MaterialType != EPBRMaterialType::Water &&
			MaterialType != EPBRMaterialType::Emissive;
	}
	if (Channel == FPBRChannels::Opacity.ToString())
	{
		return MaterialType == EPBRMaterialType::Standard ||
			MaterialType == EPBRMaterialType::Transparent ||
			MaterialType == EPBRMaterialType::Glass ||
			MaterialType == EPBRMaterialType::Water;
	}
	if (Channel == FPBRChannels::Emissive.ToString())
	{
		return MaterialType == EPBRMaterialType::Standard || MaterialType == EPBRMaterialType::Emissive;
	}
	if (Channel == FPBRChannels::Specular.ToString())
	{
		return MaterialType == EPBRMaterialType::Standard ||
			MaterialType == EPBRMaterialType::Plastic ||
			MaterialType == EPBRMaterialType::Leather ||
			MaterialType == EPBRMaterialType::Transparent ||
			MaterialType == EPBRMaterialType::Glass ||
			MaterialType == EPBRMaterialType::Water;
	}
	if (Channel == FPBRChannels::Height.ToString() || Channel == FPBRChannels::Displacement.ToString())
	{
		return MaterialType == EPBRMaterialType::Standard ||
			MaterialType == EPBRMaterialType::Wood ||
			MaterialType == EPBRMaterialType::Stone ||
			MaterialType == EPBRMaterialType::Tile ||
			MaterialType == EPBRMaterialType::Fabric ||
			MaterialType == EPBRMaterialType::Leather;
	}
	if (Channel == FPBRChannels::ClearCoat.ToString() || Channel == FPBRChannels::ClearCoatRoughness.ToString())
	{
		return MaterialType == EPBRMaterialType::Standard ||
			MaterialType == EPBRMaterialType::Leather ||
			MaterialType == EPBRMaterialType::Plastic ||
			MaterialType == EPBRMaterialType::Wood ||
			MaterialType == EPBRMaterialType::Metal;
	}
	if (Channel == FPBRChannels::Anisotropy.ToString() || Channel == FPBRChannels::Thickness.ToString() ||
		Channel == FPBRChannels::ORM.ToString() || Channel == FPBRChannels::ARM.ToString())
	{
		return MaterialType == EPBRMaterialType::Standard;
	}
	return true;
}

void FPBRMaterialFactory::GetUnusedChannelsForMaterialType(
	const FPBRMaterialSet& Set,
	EPBRMaterialType MaterialType,
	TArray<FString>& OutUnusedChannels)
{
	OutUnusedChannels.Reset();
	for (const TPair<FString, FString>& ChannelPair : Set.Channels)
	{
		const FString& Channel = ChannelPair.Key;
		if (Channel == TEXT("Preview") || Channel == TEXT("Unknown"))
		{
			continue;
		}
		if (!MaterialTypeUsesChannel(MaterialType, Channel))
		{
			OutUnusedChannels.Add(Channel);
		}
	}
	OutUnusedChannels.Sort();
}

static FString SanitizeAssetName(const FString& InName)
{
	FString Out = InName;
	// Replace invalid chars
	const FString Invalid = TEXT("\\/:*?\"<>|.,;'");
	for (int32 i = 0; i < Invalid.Len(); ++i)
	{
		Out.ReplaceInline(*FString::Chr(Invalid[i]), TEXT("_"));
	}
	// Remove spaces
	Out.ReplaceInline(TEXT(" "), TEXT("_"));
	if (Out.Len() > 80) Out = Out.Left(80);
	return Out;
}

UTexture2D* FPBRMaterialFactory::ImportTextureToAsset(const FString& FilePath, const FString& PackagePath, const FString& AssetName)
{
	if (!FPaths::FileExists(FilePath)) return nullptr;

	FString Sanitized = SanitizeAssetName(AssetName);
	FString FullPackagePath = PackagePath + Sanitized;

	// Check if asset already exists
	if (UObject* Existing = UEditorAssetLibrary::LoadAsset(FullPackagePath))
	{
		return Cast<UTexture2D>(Existing);
	}

	// Create package
	FString PackageFilename;
	FPackageName::TryConvertLongPackageNameToFilename(FullPackagePath, PackageFilename);
	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package) return nullptr;

	// Use texture factory to import
	UTextureFactory* Factory = NewObject<UTextureFactory>();
	Factory->AddToRoot();
	Factory->SuppressImportOverwriteDialog();

	bool bCancelled = false;
	UObject* Imported = Factory->FactoryCreateFile(
		UTexture2D::StaticClass(), Package,
		FName(*Sanitized),
		RF_Public | RF_Standalone,
		FilePath,
		TEXT(""),
		GWarn,
		bCancelled
	);

	Factory->RemoveFromRoot();

	if (Imported)
	{
		UTexture2D* Tex = Cast<UTexture2D>(Imported);
		if (Tex)
		{
			FAssetRegistryModule::AssetCreated(Tex);
			Package->SetDirtyFlag(true);
			Tex->PostEditChange();
		}
		return Tex;
	}

	return nullptr;
}

UMaterialExpressionTextureSample* FPBRMaterialFactory::CreateTextureSampleNode(
	UMaterial* Material, UTexture2D* Texture, const FString& Channel)
{
	if (!Material || !Texture) return nullptr;

	UMaterialExpressionTextureSample* Sample = NewObject<UMaterialExpressionTextureSample>(Material);
	Sample->Texture = Texture;

	if (Channel == TEXT("Normal") || Channel == TEXT("NormalDX") || Channel == TEXT("NormalGL"))
	{
		Sample->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
	}

	Material->GetExpressionCollection().AddExpression(Sample);
	return Sample;
}

void FPBRMaterialFactory::SplitORMChannels(UMaterial* Material, UTexture2D* ORMTexture,
	FPBRMaterialSet& Set, const FString& NormalPreference, TArray<FString>& OutNotes)
{
	if (!Material || !ORMTexture) return;

	// R = Ambient Occlusion, G = Roughness, B = Metallic (standard ORM layout)
	// Create three component mask nodes from the same texture
	auto CreateMask = [&](int32 R, int32 G, int32 B, int32 A, const FString& ChannelName) -> UMaterialExpressionComponentMask*
	{
		UMaterialExpressionTextureSample* Sample = NewObject<UMaterialExpressionTextureSample>(Material);
		Sample->Texture = ORMTexture;
		Sample->SamplerType = EMaterialSamplerType::SAMPLERTYPE_LinearColor;
		Material->GetExpressionCollection().AddExpression(Sample);

		UMaterialExpressionComponentMask* Mask = NewObject<UMaterialExpressionComponentMask>(Material);
		Mask->R = R;
		Mask->G = G;
		Mask->B = B;
		Mask->A = A;
		Mask->Input.Connect(0, Sample);
		Material->GetExpressionCollection().AddExpression(Mask);

		ConnectToMaterialInput(Material, Mask, ChannelName, TEXT("PBR Material Metal/Rough"));
		return Mask;
	};

	// Standard ORM: R=Occlusion, G=Roughness, B=Metallic
	if (!Set.Channels.Contains(TEXT("AO")))
	{
		CreateMask(1, 0, 0, 0, TEXT("AO"));
		OutNotes.Add(TEXT("ORM R channel connected as AO"));
	}
	if (!Set.Channels.Contains(TEXT("Roughness")))
	{
		CreateMask(0, 1, 0, 0, TEXT("Roughness"));
		OutNotes.Add(TEXT("ORM G channel connected as Roughness"));
	}
	if (!Set.Channels.Contains(TEXT("Metallic")))
	{
		CreateMask(0, 0, 1, 0, TEXT("Metallic"));
		OutNotes.Add(TEXT("ORM B channel connected as Metallic"));
	}
}

bool FPBRMaterialFactory::ConnectToMaterialInput(UMaterial* Material,
	UMaterialExpression* Expression, const FString& Channel, const FString& TargetMode)
{
	FString PropName = GetMaterialPropertyForChannel(Channel, TargetMode);

	// Check for slot override (learned from previous connections)
	FString Override = TryGetSlotOverride(TargetMode, Channel);
	if (!Override.IsEmpty())
	{
		bool bOk = ConnectExprToMaterialInput(Material, Expression, 0, Override);
		if (bOk) return true;
	}

	return ConnectExprToMaterialInput(Material, Expression, 0, PropName);
}

UMaterialInterface* FPBRMaterialFactory::CreateMaterialFromPBRSet(
	const FPBRMaterialSet& Set,
	const FCreateSettings& Settings,
	FString& OutNotes)
{
	OutNotes.Empty();

	FPBRMaterialCreateOptions Options;
	Options.PackageRoot = Settings.PackagePath;
	Options.MaterialInstancePrefix = Settings.MaterialPrefix.StartsWith(TEXT("M_"))
		? Settings.MaterialPrefix.Replace(TEXT("M_"), TEXT("MI_"))
		: Settings.MaterialPrefix;
	Options.MaterialType = ResolveMaterialType(Set, Settings.MaterialTypeMode);
	if (Settings.MaterialTypeMode.Equals(TEXT("Standard"), ESearchCase::IgnoreCase) ||
		Settings.MaterialTypeMode.Contains(TEXT("标准")) ||
		Settings.MaterialTypeMode.Contains(TEXT("标准")))
	{
		Options.MaterialType = EPBRMaterialType::Standard;
	}
	Options.bImportTextures = true;
	Options.bCreateIsolatedMaterialFolder = true;
	Options.NormalPreference = Settings.NormalPreference;

	FPBRMaterialCreateResult Result;
	if (FPBRMaterialInstanceFactory::CreateInstanceFromSet(Set, Options, Result))
	{
		OutNotes = Result.Message;
		return Result.MaterialInstance.LoadSynchronous();
	}

	OutNotes = Result.Message.IsEmpty() ? TEXT("创建材质实例失败") : Result.Message;
	return nullptr;
}
