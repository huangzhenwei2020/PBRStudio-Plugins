#include "Widgets/SPBRSpecialMaterialsTab.h"

#include "Services/PBRDataStore.h"
#include "Services/PBRMaterialTemplateManager.h"
#include "Services/PBRMaterialInstanceFactory.h"
#include "Models/PBRMaterialTypes.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PrimitiveComponent.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DesktopPlatformModule.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDesktopPlatform.h"
#include "FileHelpers.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "SPBRSpecialMaterialsTab"

static FString SpecialDisplayName(const FString& AssetName)
{
	if (AssetName.Contains(TEXT("Decal"))) return TEXT("贴花 / 污渍破损");
	if (AssetName.Contains(TEXT("UI_Texture"))) return TEXT("UI 贴图材质");
	if (AssetName.Contains(TEXT("Niagara_Particle"))) return TEXT("Niagara 柔边粒子");
	if (AssetName.Contains(TEXT("RuntimeVirtualTexture"))) return TEXT("RVT 输出材质");
	if (AssetName.Contains(TEXT("DistanceField"))) return TEXT("Distance Field 混合");
	if (AssetName.Contains(TEXT("PostProcess"))) return TEXT("后处理调色");
	if (AssetName.Contains(TEXT("LightFunction"))) return TEXT("灯光函数遮罩");
	if (AssetName.Contains(TEXT("Hologram"))) return TEXT("全息边缘光");
	if (AssetName.Contains(TEXT("Smoke"))) return TEXT("烟雾粒子");
	if (AssetName.Contains(TEXT("Lightning"))) return TEXT("闪电粒子");
	if (AssetName.Contains(TEXT("CarPaint"))) return TEXT("车漆清漆");
	if (AssetName.Contains(TEXT("Abyss"))) return TEXT("深渊镜");
	if (AssetName.Contains(TEXT("Leaf"))) return TEXT("双面树叶");
	if (AssetName.Contains(TEXT("WindowScreen"))) return TEXT("窗纱网格");
	if (AssetName.Contains(TEXT("Video"))) return TEXT("视频屏幕");
	if (AssetName.Contains(TEXT("Lampshade"))) return TEXT("透光灯罩");
	return AssetName;
}

static FString SpecialCategory(const FString& AssetName)
{
	if (AssetName.Contains(TEXT("Niagara")) || AssetName.Contains(TEXT("Smoke")) || AssetName.Contains(TEXT("Lightning"))) return TEXT("粒子");
	if (AssetName.Contains(TEXT("Decal")) || AssetName.Contains(TEXT("UI")) || AssetName.Contains(TEXT("PostProcess")) || AssetName.Contains(TEXT("LightFunction"))) return TEXT("非表面域");
	if (AssetName.Contains(TEXT("Leaf")) || AssetName.Contains(TEXT("WindowScreen")) || AssetName.Contains(TEXT("Lampshade"))) return TEXT("半透明/遮罩");
	if (AssetName.Contains(TEXT("RVT")) || AssetName.Contains(TEXT("RuntimeVirtualTexture")) || AssetName.Contains(TEXT("DistanceField"))) return TEXT("高级输出");
	return TEXT("特殊表面");
}

static FString SpecialDescription(const FString& AssetName)
{
	if (AssetName.Contains(TEXT("Decal"))) return TEXT("用于脏迹、破损、漏水印等 DBuffer 贴花，不走普通 PBR 套装流程。");
	if (AssetName.Contains(TEXT("Niagara")) || AssetName.Contains(TEXT("Smoke")) || AssetName.Contains(TEXT("Lightning"))) return TEXT("用于 Niagara 粒子、烟雾、闪电、火光等动态特效，提供柔边、速度和发光参数。");
	if (AssetName.Contains(TEXT("CarPaint"))) return TEXT("用于汽车漆、清漆层、金属片闪点等特殊反射表面。");
	if (AssetName.Contains(TEXT("Abyss"))) return TEXT("用于深渊镜、无限反射感、动态旋涡类装饰材质。");
	if (AssetName.Contains(TEXT("Leaf"))) return TEXT("用于树叶、薄片、双面 foliage 类材质。");
	if (AssetName.Contains(TEXT("WindowScreen"))) return TEXT("用于窗纱、网格、镂空布料等 Masked 双面材质。");
	if (AssetName.Contains(TEXT("Video"))) return TEXT("用于视频屏幕、LED 屏、测试图和可替换 Media Texture 的发光材质。");
	if (AssetName.Contains(TEXT("Lampshade"))) return TEXT("用于灯罩、透光布料、暖色半透明外壳。");
	return TEXT("特殊材质模板，适合不属于普通 PBR 贴图套件的材质类型。");
}

static FString SpecialMaterialPath(const FString& AssetName)
{
	return FString::Printf(TEXT("/Game/PBRStudio/SpecialMaterials/%s.%s"), *AssetName, *AssetName);
}

static bool IsSpecialTextureFile(const FString& FilePath)
{
	const FString Ext = FPaths::GetExtension(FilePath).ToLower();
	return Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg") || Ext == TEXT("tga") ||
		Ext == TEXT("bmp") || Ext == TEXT("tif") || Ext == TEXT("tiff") || Ext == TEXT("exr") ||
		Ext == TEXT("hdr") || Ext == TEXT("webp");
}

static FString NormalizeSpecialTokenText(const FString& Text)
{
	FString Result = Text.ToLower();
	Result.ReplaceInline(TEXT("-"), TEXT("_"));
	Result.ReplaceInline(TEXT(" "), TEXT("_"));
	Result.ReplaceInline(TEXT("."), TEXT("_"));
	return Result;
}

static bool HasAnySpecialToken(const FString& Text, const TArray<FString>& Tokens)
{
	for (const FString& Token : Tokens)
	{
		if (Text.Contains(Token))
		{
			return true;
		}
	}
	return false;
}

static FString DetectSpecialTextureRole(const FString& FilePath)
{
	const FString Name = NormalizeSpecialTokenText(FPaths::GetBaseFilename(FilePath));
	const FString Folder = NormalizeSpecialTokenText(FPaths::GetPath(FilePath));
	const FString Text = Folder + TEXT("_") + Name;

	if (HasAnySpecialToken(Text, { TEXT("preview"), TEXT("thumb"), TEXT("thumbnail"), TEXT("sample"), TEXT("预览") }))
	{
		return TEXT("预览图");
	}
	if (HasAnySpecialToken(Text, { TEXT("flipbook"), TEXT("sprite_sheet"), TEXT("spritesheet"), TEXT("atlas"), TEXT("subuv"), TEXT("sequence"), TEXT("序列"), TEXT("图集") }))
	{
		return TEXT("粒子序列/图集");
	}
	if (HasAnySpecialToken(Text, { TEXT("alpha"), TEXT("opacity"), TEXT("mask"), TEXT("cutout"), TEXT("transparency"), TEXT("trans"), TEXT("matte"), TEXT("不透明"), TEXT("透明"), TEXT("遮罩") }))
	{
		return TEXT("透明遮罩");
	}
	if (HasAnySpecialToken(Text, { TEXT("emissive"), TEXT("emission"), TEXT("emit"), TEXT("glow"), TEXT("light"), TEXT("led"), TEXT("neon"), TEXT("发光"), TEXT("自发光") }))
	{
		return TEXT("发光贴图");
	}
	if (HasAnySpecialToken(Text, { TEXT("normal"), TEXT("nrm"), TEXT("_nor"), TEXT("distort"), TEXT("distortion"), TEXT("flow"), TEXT("ripple"), TEXT("warp"), TEXT("扰动"), TEXT("流动"), TEXT("法线") }))
	{
		return TEXT("扰动/法线");
	}
	if (HasAnySpecialToken(Text, { TEXT("depth"), TEXT("soft"), TEXT("distance"), TEXT("fade"), TEXT("depthfade"), TEXT("深度"), TEXT("柔边") }))
	{
		return TEXT("柔边/深度");
	}
	if (HasAnySpecialToken(Text, { TEXT("flake"), TEXT("sparkle"), TEXT("metalflake"), TEXT("clearcoat"), TEXT("coat"), TEXT("闪点"), TEXT("清漆") }))
	{
		return TEXT("清漆/闪点");
	}
	if (HasAnySpecialToken(Text, { TEXT("pattern"), TEXT("mesh"), TEXT("net"), TEXT("screen"), TEXT("grid"), TEXT("lace"), TEXT("weave"), TEXT("网格"), TEXT("窗纱"), TEXT("图案") }))
	{
		return TEXT("图案/网格");
	}
	if (HasAnySpecialToken(Text, { TEXT("media"), TEXT("video"), TEXT("movie"), TEXT("testpattern"), TEXT("bars"), TEXT("视频"), TEXT("屏幕") }))
	{
		return TEXT("视频/媒体");
	}
	if (HasAnySpecialToken(Text, { TEXT("color"), TEXT("basecolor"), TEXT("diffuse"), TEXT("albedo"), TEXT("base"), TEXT("col"), TEXT("rgba"), TEXT("颜色"), TEXT("漫反射") }))
	{
		return TEXT("颜色/精灵");
	}
	return TEXT("颜色/精灵");
}

static int32 SpecialRolePriority(const FString& Role)
{
	if (Role == TEXT("预览图")) return 0;
	if (Role == TEXT("粒子序列/图集")) return 100;
	if (Role == TEXT("视频/媒体")) return 95;
	if (Role == TEXT("发光贴图")) return 90;
	if (Role == TEXT("透明遮罩")) return 85;
	if (Role == TEXT("图案/网格")) return 80;
	if (Role == TEXT("扰动/法线")) return 70;
	if (Role == TEXT("清漆/闪点")) return 65;
	if (Role == TEXT("柔边/深度")) return 60;
	return 50;
}

static int64 GetSpecialTextureFileSize(const FString& FilePath)
{
	const int64 Size = IFileManager::Get().FileSize(*FilePath);
	return Size > 0 ? Size : 0;
}

static bool ShouldReplaceSpecialTextureForRole(const FString& ExistingFile, const FString& NewFile, const FString& Role)
{
	if (ExistingFile.IsEmpty())
	{
		return true;
	}

	const FString ExistingName = NormalizeSpecialTokenText(FPaths::GetBaseFilename(ExistingFile));
	const FString NewName = NormalizeSpecialTokenText(FPaths::GetBaseFilename(NewFile));
	const bool bExistingPreview = ExistingName.Contains(TEXT("preview")) || ExistingName.Contains(TEXT("thumb"));
	const bool bNewPreview = NewName.Contains(TEXT("preview")) || NewName.Contains(TEXT("thumb"));
	if (Role != TEXT("预览图") && bExistingPreview != bNewPreview)
	{
		return !bNewPreview;
	}

	const int64 ExistingSize = GetSpecialTextureFileSize(ExistingFile);
	const int64 NewSize = GetSpecialTextureFileSize(NewFile);
	if (ExistingSize != NewSize)
	{
		return NewSize > ExistingSize;
	}

	return NewName.Len() < ExistingName.Len();
}

static FString JoinSpecialTextureRoles(const TMap<FString, FString>& Textures)
{
	TArray<FString> Parts;
	for (const auto& Pair : Textures)
	{
		Parts.Add(Pair.Key + TEXT(": ") + FPaths::GetCleanFilename(Pair.Value));
	}
	Parts.Sort();
	return FString::Join(Parts, TEXT(" | "));
}

static FString JoinSpecialTextureFiles(const TArray<FString>& Files)
{
	TArray<FString> Parts;
	for (const FString& File : Files)
	{
		Parts.Add(FPaths::GetCleanFilename(File));
	}
	Parts.Sort();
	return FString::Join(Parts, TEXT(" | "));
}

static FString GetPreferredSpecialPreview(const TMap<FString, FString>& Textures);

static void RebuildSpecialTexturesFromManualRows(
	const TArray<TSharedPtr<FPBRSpecialTextureChannelItem>>& Rows,
	TMap<FString, FString>& OutTextures,
	TMap<FString, FString>& OutTextureRoles,
	FString& OutPreviewPath)
{
	OutTextures.Reset();
	OutTextureRoles.Reset();
	OutPreviewPath.Empty();

	for (const TSharedPtr<FPBRSpecialTextureChannelItem>& Row : Rows)
	{
		if (!Row.IsValid() || Row->FilePath.IsEmpty())
		{
			continue;
		}

		OutTextureRoles.Add(Row->FilePath, Row->Role);
		if (Row->Role == TEXT("不使用"))
		{
			continue;
		}

		if (Row->Role == TEXT("预览图"))
		{
			OutPreviewPath = Row->FilePath;
		}

		const FString* Existing = OutTextures.Find(Row->Role);
		if (!Existing || ShouldReplaceSpecialTextureForRole(*Existing, Row->FilePath, Row->Role))
		{
			OutTextures.Add(Row->Role, Row->FilePath);
		}
	}

	if (OutPreviewPath.IsEmpty())
	{
		OutPreviewPath = GetPreferredSpecialPreview(OutTextures);
	}
}

static FString GuessSpecialAssetNameFromText(const FString& Text)
{
	const FString Lower = Text.ToLower();
	if (Lower.Contains(TEXT("decal")) || Lower.Contains(TEXT("stain")) || Lower.Contains(TEXT("grime")) ||
		Lower.Contains(TEXT("damage")) || Lower.Contains(TEXT("crack")) || Lower.Contains(TEXT("贴花")) ||
		Lower.Contains(TEXT("污")) || Lower.Contains(TEXT("破损")))
	{
		return TEXT("SM_Decal_DBuffer_Color");
	}
	if (Lower.Contains(TEXT("leaf")) || Lower.Contains(TEXT("foliage")) || Lower.Contains(TEXT("plant")) ||
		Lower.Contains(TEXT("grass")) || Lower.Contains(TEXT("树叶")) || Lower.Contains(TEXT("叶")))
	{
		return TEXT("SM_TwoSided_Leaf");
	}
	if (Lower.Contains(TEXT("screen")) || Lower.Contains(TEXT("mesh")) || Lower.Contains(TEXT("curtain")) ||
		Lower.Contains(TEXT("net")) || Lower.Contains(TEXT("纱")) || Lower.Contains(TEXT("窗纱")))
	{
		return TEXT("SM_WindowScreen_Mesh");
	}
	if (Lower.Contains(TEXT("video")) || Lower.Contains(TEXT("led")) || Lower.Contains(TEXT("monitor")) ||
		Lower.Contains(TEXT("tv")) || Lower.Contains(TEXT("视频")) || Lower.Contains(TEXT("屏幕")))
	{
		return TEXT("SM_Video_Screen");
	}
	if (Lower.Contains(TEXT("lamp")) || Lower.Contains(TEXT("shade")) || Lower.Contains(TEXT("灯罩")))
	{
		return TEXT("SM_Lampshade_Translucent");
	}
	if (Lower.Contains(TEXT("smoke")) || Lower.Contains(TEXT("fog")) || Lower.Contains(TEXT("烟")) ||
		Lower.Contains(TEXT("雾")))
	{
		return TEXT("SM_Niagara_Smoke_Soft");
	}
	if (Lower.Contains(TEXT("lightning")) || Lower.Contains(TEXT("bolt")) || Lower.Contains(TEXT("spark")) ||
		Lower.Contains(TEXT("闪电")))
	{
		return TEXT("SM_Niagara_Lightning");
	}
	if (Lower.Contains(TEXT("particle")) || Lower.Contains(TEXT("fire")) || Lower.Contains(TEXT("flame")) ||
		Lower.Contains(TEXT("sprite")) || Lower.Contains(TEXT("粒子")) || Lower.Contains(TEXT("火")))
	{
		return TEXT("SM_Niagara_Particle_Soft");
	}
	if (Lower.Contains(TEXT("carpaint")) || Lower.Contains(TEXT("car_paint")) || Lower.Contains(TEXT("clearcoat")) ||
		Lower.Contains(TEXT("flake")) || Lower.Contains(TEXT("车漆")))
	{
		return TEXT("SM_CarPaint_ClearCoat");
	}
	if (Lower.Contains(TEXT("mirror")) || Lower.Contains(TEXT("abyss")) || Lower.Contains(TEXT("镜")))
	{
		return TEXT("SM_Abyss_Mirror");
	}
	if (Lower.Contains(TEXT("ui")) || Lower.Contains(TEXT("hud")) || Lower.Contains(TEXT("interface")))
	{
		return TEXT("SM_UI_Texture");
	}
	return TEXT("SM_Decal_DBuffer_Color");
}

static FString GuessSpecialAssetNameFromTextures(const FString& Folder, const TMap<FString, FString>& Textures)
{
	const FString Text = NormalizeSpecialTokenText(Folder + TEXT(" ") + JoinSpecialTextureRoles(Textures));
	if (Textures.Contains(TEXT("粒子序列/图集")) || Textures.Contains(TEXT("柔边/深度")))
	{
		if (Text.Contains(TEXT("smoke")) || Text.Contains(TEXT("fog")) || Text.Contains(TEXT("烟")) || Text.Contains(TEXT("雾")))
		{
			return TEXT("SM_Niagara_Smoke_Soft");
		}
		if (Text.Contains(TEXT("lightning")) || Text.Contains(TEXT("bolt")) || Text.Contains(TEXT("闪电")))
		{
			return TEXT("SM_Niagara_Lightning");
		}
		return TEXT("SM_Niagara_Particle_Soft");
	}
	if (Textures.Contains(TEXT("视频/媒体")))
	{
		return TEXT("SM_Video_Screen");
	}
	if (Textures.Contains(TEXT("清漆/闪点")))
	{
		return TEXT("SM_CarPaint_ClearCoat");
	}
	if (Textures.Contains(TEXT("图案/网格")) && (Text.Contains(TEXT("screen")) || Text.Contains(TEXT("net")) || Text.Contains(TEXT("curtain")) || Text.Contains(TEXT("窗纱"))))
	{
		return TEXT("SM_WindowScreen_Mesh");
	}
	if (Textures.Contains(TEXT("透明遮罩")) && (Text.Contains(TEXT("leaf")) || Text.Contains(TEXT("foliage")) || Text.Contains(TEXT("叶"))))
	{
		return TEXT("SM_TwoSided_Leaf");
	}
	if (Text.Contains(TEXT("lamp")) || Text.Contains(TEXT("shade")) || Text.Contains(TEXT("灯罩")))
	{
		return TEXT("SM_Lampshade_Translucent");
	}
	return GuessSpecialAssetNameFromText(Text);
}

static FString GetPreferredSpecialPreview(const TMap<FString, FString>& Textures)
{
	if (const FString* Preview = Textures.Find(TEXT("预览图")))
	{
		return *Preview;
	}
	if (const FString* Color = Textures.Find(TEXT("颜色/精灵")))
	{
		return *Color;
	}
	if (const FString* Pattern = Textures.Find(TEXT("图案/网格")))
	{
		return *Pattern;
	}
	if (const FString* Emissive = Textures.Find(TEXT("发光贴图")))
	{
		return *Emissive;
	}
	if (const FString* Atlas = Textures.Find(TEXT("粒子序列/图集")))
	{
		return *Atlas;
	}
	return FString();
}

static FString SpecialRoleToImportChannel(const FString& Role)
{
	if (Role == TEXT("扰动/法线"))
	{
		return FPBRChannels::Normal.ToString();
	}
	if (Role == TEXT("透明遮罩") || Role == TEXT("柔边/深度"))
	{
		return FPBRChannels::Opacity.ToString();
	}
	if (Role == TEXT("清漆/闪点"))
	{
		return FPBRChannels::ClearCoat.ToString();
	}
	return FPBRChannels::BaseColor.ToString();
}

static bool SpecialTemplatePrefersEmissive(const FString& AssetName)
{
	return AssetName.Contains(TEXT("Niagara")) ||
		AssetName.Contains(TEXT("Smoke")) ||
		AssetName.Contains(TEXT("Lightning")) ||
		AssetName.Contains(TEXT("Video")) ||
		AssetName.Contains(TEXT("Hologram")) ||
		AssetName.Contains(TEXT("LightFunction")) ||
		AssetName.Contains(TEXT("PostProcess")) ||
		AssetName.Contains(TEXT("Abyss"));
}

static bool SpecialTemplatePrefersMask(const FString& AssetName)
{
	return AssetName.Contains(TEXT("Leaf")) ||
		AssetName.Contains(TEXT("WindowScreen")) ||
		AssetName.Contains(TEXT("Decal")) ||
		AssetName.Contains(TEXT("LightFunction"));
}

static FString BuildSpecialImportedPackagePath(const FString& DisplayName)
{
	return TEXT("/Game/PBRStudio/SpecialMaterials/Imported") / FPBRMaterialInstanceFactory::SanitizeAssetName(DisplayName);
}

static void SaveSpecialPackages(const TArray<UPackage*>& Packages)
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

static UMaterialInstanceConstant* CreateSpecialMaterialInstanceFromTextures(
	const FPBRSpecialMaterialItem& Item,
	UMaterialInterface* ParentMaterial,
	TArray<FString>& Messages)
{
	if (!ParentMaterial || Item.SpecialTextures.Num() == 0)
	{
		return nullptr;
	}

	const FString CleanName = FPBRMaterialInstanceFactory::SanitizeAssetName(Item.DisplayName);
	const FString PackagePath = BuildSpecialImportedPackagePath(Item.DisplayName);
	const FString InstanceName = TEXT("MI_") + CleanName;
	const FString InstancePath = PackagePath / InstanceName;

	if (UMaterialInstanceConstant* Existing = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(InstancePath)))
	{
		Messages.Add(FString::Printf(TEXT("使用已有特殊材质实例: %s"), *InstancePath));
		return Existing;
	}

	TMap<FString, UTexture2D*> ImportedByRole;
	for (const TPair<FString, FString>& Pair : Item.SpecialTextures)
	{
		if (Pair.Key == TEXT("预览图") || !FPaths::FileExists(Pair.Value))
		{
			continue;
		}

		const FString TextureName = TEXT("T_") + CleanName + TEXT("_") + FPBRMaterialInstanceFactory::SanitizeAssetName(Pair.Key);
		if (UTexture2D* Texture = FPBRMaterialInstanceFactory::ImportTextureToAsset(Pair.Value, PackagePath, TextureName, SpecialRoleToImportChannel(Pair.Key)))
		{
			ImportedByRole.Add(Pair.Key, Texture);
		}
	}

	UPackage* InstancePackage = CreatePackage(*InstancePath);
	if (!InstancePackage)
	{
		Messages.Add(FString::Printf(TEXT("创建特殊材质实例包失败: %s"), *Item.DisplayName));
		return nullptr;
	}

	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;
	UObject* Created = Factory->FactoryCreateNew(
		UMaterialInstanceConstant::StaticClass(),
		InstancePackage,
		FName(*InstanceName),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn);

	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Created);
	if (!Instance)
	{
		Messages.Add(FString::Printf(TEXT("创建特殊材质实例失败: %s"), *Item.DisplayName));
		return nullptr;
	}

	const bool bEmissiveTemplate = SpecialTemplatePrefersEmissive(Item.AssetName);
	if (UTexture2D** Color = ImportedByRole.Find(TEXT("颜色/精灵")))
	{
		Instance->SetTextureParameterValueEditorOnly(bEmissiveTemplate ? FPBRMaterialParameters::EmissiveTexture : FPBRMaterialParameters::BaseColorTexture, *Color);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(bEmissiveTemplate ? FPBRMaterialParameters::UseEmissiveTexture : FPBRMaterialParameters::UseBaseColorTexture), true);
	}
	if (UTexture2D** Atlas = ImportedByRole.Find(TEXT("粒子序列/图集")))
	{
		Instance->SetTextureParameterValueEditorOnly(bEmissiveTemplate ? FPBRMaterialParameters::EmissiveTexture : FPBRMaterialParameters::BaseColorTexture, *Atlas);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(bEmissiveTemplate ? FPBRMaterialParameters::UseEmissiveTexture : FPBRMaterialParameters::UseBaseColorTexture), true);
	}
	if (UTexture2D** Pattern = ImportedByRole.Find(TEXT("图案/网格")))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::BaseColorTexture, *Pattern);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseBaseColorTexture), true);
	}
	if (UTexture2D** Video = ImportedByRole.Find(TEXT("视频/媒体")))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::EmissiveTexture, *Video);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), true);
	}
	if (UTexture2D** Emissive = ImportedByRole.Find(TEXT("发光贴图")))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::EmissiveTexture, *Emissive);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseEmissiveTexture), true);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::EmissiveIntensity, 2.0f);
	}
	if (UTexture2D** Opacity = ImportedByRole.Find(TEXT("透明遮罩")))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::OpacityTexture, *Opacity);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseOpacityTexture), true);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, SpecialTemplatePrefersMask(Item.AssetName) ? 1.0f : 0.65f);
	}
	if (UTexture2D** SoftDepth = ImportedByRole.Find(TEXT("柔边/深度")))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::OpacityTexture, *SoftDepth);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseOpacityTexture), true);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::Opacity, 0.8f);
	}
	if (UTexture2D** Normal = ImportedByRole.Find(TEXT("扰动/法线")))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::NormalTexture, *Normal);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseNormalTexture), true);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::NormalStrength, Item.AssetName.Contains(TEXT("Water")) ? 0.45f : 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::DistortionStrength, bEmissiveTemplate ? 0.12f : 0.04f);
	}
	if (UTexture2D** Flake = ImportedByRole.Find(TEXT("清漆/闪点")))
	{
		Instance->SetTextureParameterValueEditorOnly(FPBRMaterialParameters::ClearCoatTexture, *Flake);
		Instance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(FPBRMaterialParameters::UseClearCoatTexture), true);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::ClearCoat, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::FlakeIntensity, 0.8f);
	}

	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVUTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::UVVTiling, 1.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::DynamicSpeedU, bEmissiveTemplate ? 0.05f : 0.0f);
	Instance->SetScalarParameterValueEditorOnly(FPBRMaterialParameters::DynamicSpeedV, bEmissiveTemplate ? 0.02f : 0.0f);

	FAssetRegistryModule::AssetCreated(Instance);
	InstancePackage->SetDirtyFlag(true);
	Instance->PostEditChange();
	SaveSpecialPackages({ InstancePackage });
	Messages.Add(FString::Printf(TEXT("已创建特殊材质实例: %s，识别 %d 张贴图"), *InstancePath, ImportedByRole.Num()));
	return Instance;
}

void SPBRSpecialMaterialsTab::Construct(const FArguments& InArgs)
{
	OnCompactModeChanged = InArgs._OnCompactModeChanged;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	SpecialTemplateOptions.Reset();
	for (const FString& AssetName : FPBRMaterialTemplateManager::GetSpecialMaterialAssetNames())
	{
		SpecialTemplateOptions.Add(MakeShared<FString>(AssetName));
	}
	TextureRoleOptions = {
		MakeShared<FString>(TEXT("颜色/精灵")),
		MakeShared<FString>(TEXT("透明遮罩")),
		MakeShared<FString>(TEXT("发光贴图")),
		MakeShared<FString>(TEXT("扰动/法线")),
		MakeShared<FString>(TEXT("粒子序列/图集")),
		MakeShared<FString>(TEXT("柔边/深度")),
		MakeShared<FString>(TEXT("清漆/闪点")),
		MakeShared<FString>(TEXT("图案/网格")),
		MakeShared<FString>(TEXT("视频/媒体")),
		MakeShared<FString>(TEXT("预览图")),
		MakeShared<FString>(TEXT("不使用"))
	};

	Items.Reset();

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
			[
				SNew(STextBlock)
				.Visibility(this, &SPBRSpecialMaterialsTab::GetStandardControlsVisibility)
				.Text(LOCTEXT("Title", "特殊材质套件 - 选择类型、创建材质、应用到场景"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 8)
			[
				SNew(STextBlock)
				.Visibility(this, &SPBRSpecialMaterialsTab::GetStandardControlsVisibility)
				.Text(LOCTEXT("Intro", "这里专门管理贴花、粒子、车漆、树叶、窗纱、视频屏幕、灯罩等非标准 PBR 材质。"))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPBRSpecialMaterialsTab::GetStandardControlsVisibility)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("Folder", "文件夹:"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SAssignNew(FolderPathBox, SEditableTextBox)
					.HintText(LOCTEXT("FolderHint", "选择包含特殊材质贴图的文件夹..."))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton).Text(LOCTEXT("Browse", "浏览...")).OnClicked(this, &SPBRSpecialMaterialsTab::OnBrowseFolder)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton).Text(LOCTEXT("OpenFolder", "打开")).OnClicked(this, &SPBRSpecialMaterialsTab::OnOpenFolder)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPBRSpecialMaterialsTab::GetStandardControlsVisibility)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
				[
					SAssignNew(RecursiveCheck, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[ SNew(STextBlock).Text(LOCTEXT("Recursive", "递归扫描")) ]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ScanFolder", "扫描文件夹"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.OnClicked(this, &SPBRSpecialMaterialsTab::OnScanFolder)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SHorizontalBox)
				.Visibility(this, &SPBRSpecialMaterialsTab::GetStandardControlsVisibility)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateSpecialTemplates", "一键创建特殊材质"))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
					.OnClicked(this, &SPBRSpecialMaterialsTab::OnCreateSpecialTemplates)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton).Text(LOCTEXT("CreateSelected", "创建选中")).OnClicked(this, &SPBRSpecialMaterialsTab::OnCreateSelected)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton).Text(LOCTEXT("CreateChecked", "创建勾选")).ButtonStyle(FAppStyle::Get(), "FlatButton.Primary").OnClicked(this, &SPBRSpecialMaterialsTab::OnCreateChecked)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton).Text(LOCTEXT("CreateAll", "全部创建")).OnClicked(this, &SPBRSpecialMaterialsTab::OnCreateAll)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton).Text(LOCTEXT("ApplySelected", "应用到选中对象")).OnClicked(this, &SPBRSpecialMaterialsTab::OnApplySelectedToSelection)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ManualChannels", "手动通道"))
					.Visibility(this, &SPBRSpecialMaterialsTab::GetScannedActionVisibility)
					.OnClicked(this, &SPBRSpecialMaterialsTab::OnManualTextureChannels)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton).Text(LOCTEXT("CheckAll", "全选")).OnClicked(this, &SPBRSpecialMaterialsTab::OnCheckAll)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton).Text(LOCTEXT("CheckNone", "全不选")).OnClicked(this, &SPBRSpecialMaterialsTab::OnCheckNone)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton).Text(LOCTEXT("ListMode", "列表")).OnClicked(this, &SPBRSpecialMaterialsTab::OnShowListView)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("GridMode", "网格")).OnClicked(this, &SPBRSpecialMaterialsTab::OnShowGridView)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
					.Text_Lambda([this]()
					{
						return bCompactMode ? LOCTEXT("SpecialStandardMode", "标准模式") : LOCTEXT("SpecialCompactMode", "精简模式");
					})
					.OnClicked(this, &SPBRSpecialMaterialsTab::OnToggleCompactMode)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton).Text(LOCTEXT("SpecialListCompact", "列表")).OnClicked(this, &SPBRSpecialMaterialsTab::OnShowListView)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton).Text(LOCTEXT("SpecialGridCompact", "网格")).OnClicked(this, &SPBRSpecialMaterialsTab::OnShowGridView)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
				[
					SAssignNew(CountText, STextBlock)
					.Text(FText::Format(LOCTEXT("CountCompact", "{0} 个特殊材质"), FText::AsNumber(Items.Num())))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(STextBlock)
				.Visibility(this, &SPBRSpecialMaterialsTab::GetStandardControlsVisibility)
				.Text(this, &SPBRSpecialMaterialsTab::GetStatusText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 4)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(ListView, SListView<TSharedPtr<FPBRSpecialMaterialItem>>)
					.Visibility(this, &SPBRSpecialMaterialsTab::GetListVisibility)
					.ListItemsSource(&Items)
					.HeaderRow(BuildHeaderRow())
					.OnGenerateRow(this, &SPBRSpecialMaterialsTab::OnGenerateRow)
					.OnSelectionChanged(this, &SPBRSpecialMaterialsTab::OnSelectionChanged)
					.OnContextMenuOpening(this, &SPBRSpecialMaterialsTab::MakeContextMenu)
					.SelectionMode(ESelectionMode::Multi)
				]
				+ SOverlay::Slot()
				[
					SAssignNew(TileView, STileView<TSharedPtr<FPBRSpecialMaterialItem>>)
					.Visibility(this, &SPBRSpecialMaterialsTab::GetGridVisibility)
					.ListItemsSource(&Items)
					.ItemWidth(140)
					.ItemHeight(126)
					.OnGenerateTile(this, &SPBRSpecialMaterialsTab::OnGenerateTile)
					.OnSelectionChanged(this, &SPBRSpecialMaterialsTab::OnSelectionChanged)
					.OnContextMenuOpening(this, &SPBRSpecialMaterialsTab::MakeContextMenu)
					.SelectionMode(ESelectionMode::Multi)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 8)
			[
				SNew(SSeparator)
				.Visibility(this, &SPBRSpecialMaterialsTab::GetStandardControlsVisibility)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SBorder)
				.Visibility(this, &SPBRSpecialMaterialsTab::GetStandardControlsVisibility)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(10)
				[
					SAssignNew(StatusText, STextBlock)
					.Text(this, &SPBRSpecialMaterialsTab::GetStatusText)
					.AutoWrapText(true)
				]
			]
		]
	];
}

void SPBRSpecialMaterialsTab::LoadExternalLibraryFolderAndScan()
{
	TSharedPtr<FJsonObject> Config;
	if (FPBRDataStore::LoadConfig(Config) && Config.IsValid())
	{
		FString LastFolder;
		if (Config->TryGetStringField(TEXT("special_materials_last_folder"), LastFolder) && FolderPathBox.IsValid())
		{
			FolderPathBox->SetText(FText::FromString(LastFolder));
		}
	}

	if (FolderPathBox.IsValid() && FPaths::DirectoryExists(FolderPathBox->GetText().ToString()))
	{
		OnScanFolder();
	}
}

void SPBRSpecialMaterialsTab::RebuildItems()
{
	Items.Reset();
	for (const FString& AssetName : FPBRMaterialTemplateManager::GetSpecialMaterialAssetNames())
	{
		TSharedPtr<FPBRSpecialMaterialItem> Item = MakeShared<FPBRSpecialMaterialItem>();
		Item->AssetName = AssetName;
		Item->DisplayName = SpecialDisplayName(AssetName);
		Item->Category = SpecialCategory(AssetName);
		Item->Description = SpecialDescription(AssetName);
		Item->Status = TEXT("未创建");
		Item->CreatedMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(SpecialMaterialPath(AssetName)));
		if (Item->CreatedMaterial.IsValid())
		{
			Item->Status = TEXT("已存在");
		}
		Items.Add(Item);
	}
}

bool SPBRSpecialMaterialsTab::HasScannedItems() const
{
	for (const TSharedPtr<FPBRSpecialMaterialItem>& Item : Items)
	{
		if (Item.IsValid() && !Item->SourceFolder.IsEmpty())
		{
			return true;
		}
	}
	return false;
}

EVisibility SPBRSpecialMaterialsTab::GetScannedActionVisibility() const
{
	return HasScannedItems() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SPBRSpecialMaterialsTab::OnBrowseFolder()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveTopLevelWindow().IsValid())
	{
		ParentWindowHandle = FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle();
	}

	FString Folder;
	const bool bSelected = DesktopPlatform->OpenDirectoryDialog(
		ParentWindowHandle,
		TEXT("选择特殊材质贴图文件夹"),
		FolderPathBox.IsValid() ? FolderPathBox->GetText().ToString() : FString(),
		Folder);

	if (bSelected && FolderPathBox.IsValid())
	{
		FolderPathBox->SetText(FText::FromString(Folder));
		OnScanFolder();
	}
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::OnOpenFolder()
{
	if (FolderPathBox.IsValid())
	{
		const FString Folder = FolderPathBox->GetText().ToString();
		if (!Folder.IsEmpty())
		{
			FPlatformProcess::ExploreFolder(*Folder);
		}
	}
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::OnScanFolder()
{
	const FString Root = FolderPathBox.IsValid() ? FolderPathBox->GetText().ToString() : FString();
	if (Root.IsEmpty() || !FPaths::DirectoryExists(Root))
	{
		LastMessages = { TEXT("特殊材质扫描失败: 文件夹不存在") };
		RefreshViews();
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		Config = MakeShared<FJsonObject>();
	}
	Config->SetStringField(TEXT("special_materials_last_folder"), Root);
	FPBRDataStore::SaveConfig(Config);

	TArray<FString> Files;
	const bool bRecursive = !RecursiveCheck.IsValid() || RecursiveCheck->IsChecked();
	if (bRecursive)
	{
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.*"), true, false);
	}
	else
	{
		IFileManager::Get().FindFiles(Files, *(Root / TEXT("*.*")), true, false);
		for (FString& File : Files)
		{
			File = Root / File;
		}
	}

	TMap<FString, TArray<FString>> FilesByFolder;
	for (const FString& File : Files)
	{
		if (!IsSpecialTextureFile(File))
		{
			continue;
		}

		const FString Folder = FPaths::GetPath(File);
		FilesByFolder.FindOrAdd(Folder).Add(File);
	}

	Items.Reset();
	int32 Added = 0;
	int32 TextureCount = 0;
	for (const TPair<FString, TArray<FString>>& FolderPair : FilesByFolder)
	{
		TMap<FString, FString> SpecialTextures;
		TMap<FString, FString> SpecialTextureRoles;
		for (const FString& File : FolderPair.Value)
		{
			const FString Role = DetectSpecialTextureRole(File);
			SpecialTextureRoles.Add(File, Role);
			const FString* Existing = SpecialTextures.Find(Role);
			if (!Existing || ShouldReplaceSpecialTextureForRole(*Existing, File, Role))
			{
				SpecialTextures.Add(Role, File);
			}
		}

		if (SpecialTextures.Num() == 0)
		{
			continue;
		}

		TextureCount += SpecialTextures.Num();
		const FString AssetName = GuessSpecialAssetNameFromTextures(FolderPair.Key, SpecialTextures);
		TSharedPtr<FPBRSpecialMaterialItem> Item = MakeShared<FPBRSpecialMaterialItem>();
		Item->AssetName = AssetName;
		Item->DisplayName = FPaths::GetCleanFilename(FolderPair.Key);
		Item->Category = SpecialCategory(AssetName);
		Item->Description = FString::Printf(
			TEXT("自动识别: %s。建议模板: %s"),
			*JoinSpecialTextureRoles(SpecialTextures),
			*SpecialDisplayName(AssetName));
		Item->Status = FString::Printf(TEXT("已扫描: %d 张"), SpecialTextures.Num());
		Item->SourceFolder = FolderPair.Key;
		Item->SpecialTextures = SpecialTextures;
		Item->SpecialTextureRoles = SpecialTextureRoles;
		Item->PreviewPath = GetPreferredSpecialPreview(SpecialTextures);
		Item->CreatedMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(SpecialMaterialPath(AssetName)));
		Items.Add(Item);
		Added++;
	}

	LastMessages = { FString::Printf(TEXT("已扫描 %d 个特殊材质文件夹，识别 %d 张特殊贴图。"), Added, TextureCount) };
	RefreshViews();
	return FReply::Handled();
}

void SPBRSpecialMaterialsTab::RefreshViews()
{
	if (ListView.IsValid()) { ListView->RequestListRefresh(); }
	if (TileView.IsValid()) { TileView->RequestListRefresh(); }
	if (CountText.IsValid())
	{
		CountText->SetText(FText::Format(LOCTEXT("CountRefresh", "{0} 个特殊材质"), FText::AsNumber(Items.Num())));
	}
	if (StatusText.IsValid()) { StatusText->SetText(GetStatusText()); }
}

FReply SPBRSpecialMaterialsTab::OnCreateSelected() { CreateMaterials(TEXT("selected")); return FReply::Handled(); }
FReply SPBRSpecialMaterialsTab::OnCreateChecked() { CreateMaterials(TEXT("checked")); return FReply::Handled(); }
FReply SPBRSpecialMaterialsTab::OnCreateAll() { CreateMaterials(TEXT("all")); return FReply::Handled(); }

FReply SPBRSpecialMaterialsTab::OnCreateSpecialTemplates()
{
	LastMessages.Reset();
	LastCreatedCount = FPBRMaterialTemplateManager::EnsureSpecialTemplateMaterials(LastMessages);
	RefreshViews();
	return FReply::Handled();
}

void SPBRSpecialMaterialsTab::CreateMaterials(const FString& Scope)
{
	LastMessages.Reset();
	LastCreatedCount = 0;

	TSet<FPBRSpecialMaterialItem*> TargetItems;
	if (Scope == TEXT("selected"))
	{
		const TArray<TSharedPtr<FPBRSpecialMaterialItem>> Selected = bGridViewMode && TileView.IsValid()
			? TileView->GetSelectedItems()
			: (ListView.IsValid() ? ListView->GetSelectedItems() : TArray<TSharedPtr<FPBRSpecialMaterialItem>>());
		for (const TSharedPtr<FPBRSpecialMaterialItem>& Item : Selected)
		{
			if (Item.IsValid()) { TargetItems.Add(Item.Get()); }
		}
	}

	for (TSharedPtr<FPBRSpecialMaterialItem>& Item : Items)
	{
		if (!Item.IsValid()) { continue; }
		if (Item->SourceFolder.IsEmpty())
		{
			continue;
		}
		const bool bTarget = Scope == TEXT("all") || (Scope == TEXT("checked") && Item->bChecked) || TargetItems.Contains(Item.Get());
		if (!bTarget)
		{
			continue;
		}

		if (LastCreatedCount == 0)
		{
			LastCreatedCount = FPBRMaterialTemplateManager::EnsureSpecialTemplateMaterials(LastMessages);
		}
		UMaterialInterface* ParentMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(SpecialMaterialPath(Item->AssetName)));
		if (Item->SpecialTextures.Num() > 0)
		{
			UMaterialInstanceConstant* Instance = CreateSpecialMaterialInstanceFromTextures(*Item, ParentMaterial, LastMessages);
			Item->CreatedMaterial = Instance ? Cast<UMaterialInterface>(Instance) : ParentMaterial;
			Item->Status = Instance ? TEXT("已创建实例") : TEXT("实例创建失败");
		}
		else
		{
			Item->CreatedMaterial = ParentMaterial;
			Item->Status = Item->CreatedMaterial.IsValid() ? TEXT("已创建") : TEXT("创建失败");
		}
	}
	if (LastMessages.Num() == 0)
	{
		LastMessages = { TEXT("没有扫描出来的特殊材质图片套件。请先选择文件夹并扫描。") };
	}
	RefreshViews();
}

FReply SPBRSpecialMaterialsTab::OnCheckAll()
{
	for (TSharedPtr<FPBRSpecialMaterialItem>& Item : Items) { if (Item.IsValid()) { Item->bChecked = true; } }
	RefreshViews();
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::OnCheckNone()
{
	for (TSharedPtr<FPBRSpecialMaterialItem>& Item : Items) { if (Item.IsValid()) { Item->bChecked = false; } }
	RefreshViews();
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::OnShowListView()
{
	bGridViewMode = false;
	RefreshViews();
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::OnShowGridView()
{
	bGridViewMode = true;
	RefreshViews();
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::OnToggleCompactMode()
{
	bCompactMode = !bCompactMode;
	if (bCompactMode)
	{
		bGridViewMode = true;
	}
	if (OnCompactModeChanged.IsBound())
	{
		OnCompactModeChanged.Execute(bCompactMode);
	}
	RefreshViews();
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::ToggleCompactModeFromGlobal()
{
	return OnToggleCompactMode();
}

void SPBRSpecialMaterialsTab::SetCompactModeFromGlobal(bool bInCompactMode)
{
	if (bCompactMode == bInCompactMode)
	{
		return;
	}
	bCompactMode = bInCompactMode;
	if (bCompactMode)
	{
		bGridViewMode = true;
	}
	if (OnCompactModeChanged.IsBound())
	{
		OnCompactModeChanged.Execute(bCompactMode);
	}
	RefreshViews();
}

EVisibility SPBRSpecialMaterialsTab::GetStandardControlsVisibility() const
{
	return bCompactMode ? EVisibility::Collapsed : EVisibility::Visible;
}

FReply SPBRSpecialMaterialsTab::OnApplySelectedToSelection()
{
	const TArray<TSharedPtr<FPBRSpecialMaterialItem>> Selected = bGridViewMode && TileView.IsValid()
		? TileView->GetSelectedItems()
		: (ListView.IsValid() ? ListView->GetSelectedItems() : TArray<TSharedPtr<FPBRSpecialMaterialItem>>());
	if (Selected.Num() == 0 || !Selected[0].IsValid())
	{
		return FReply::Handled();
	}
	UMaterialInterface* Material = Selected[0]->CreatedMaterial.LoadSynchronous();
	if (!Material)
	{
		CreateMaterials(TEXT("selected"));
		Material = Selected[0]->CreatedMaterial.LoadSynchronous();
	}
	if (!Material)
	{
		Selected[0]->Status = TEXT("未应用: 请先创建材质");
		RefreshViews();
		return FReply::Handled();
	}

	int32 Applied = 0;
	USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
	if (Selection)
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			AActor* Actor = Cast<AActor>(*It);
			if (!Actor) { continue; }
			TArray<UPrimitiveComponent*> Components;
			Actor->GetComponents<UPrimitiveComponent>(Components);
			for (UPrimitiveComponent* Component : Components)
			{
				if (Component && Component->GetNumMaterials() > 0)
				{
					Component->SetMaterial(0, Material);
					Applied++;
				}
			}
		}
	}
	Selected[0]->Status = Applied > 0 ? FString::Printf(TEXT("已应用: %d 个组件"), Applied) : TEXT("未应用: 没有选中可用对象");
	RefreshViews();
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::OnOpenSelectedLocation()
{
	const TArray<TSharedPtr<FPBRSpecialMaterialItem>> Selected = bGridViewMode && TileView.IsValid()
		? TileView->GetSelectedItems()
		: (ListView.IsValid() ? ListView->GetSelectedItems() : TArray<TSharedPtr<FPBRSpecialMaterialItem>>());
	if (Selected.Num() > 0 && Selected[0].IsValid() && !Selected[0]->SourceFolder.IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*Selected[0]->SourceFolder);
	}
	else
	{
		FPlatformProcess::ExploreFolder(*FPackageName::LongPackageNameToFilename(TEXT("/Game/PBRStudio/SpecialMaterials")));
	}
	return FReply::Handled();
}

FReply SPBRSpecialMaterialsTab::OnManualTextureChannels()
{
	const TArray<TSharedPtr<FPBRSpecialMaterialItem>> Selected = bGridViewMode && TileView.IsValid()
		? TileView->GetSelectedItems()
		: (ListView.IsValid() ? ListView->GetSelectedItems() : TArray<TSharedPtr<FPBRSpecialMaterialItem>>());
	if (Selected.Num() == 0 || !Selected[0].IsValid())
	{
		LastMessages = { TEXT("请先选中一个扫描出来的特殊材质。") };
		RefreshViews();
		return FReply::Handled();
	}

	return OnManualTextureChannelsForItem(Selected[0]);
}

FReply SPBRSpecialMaterialsTab::OnManualTextureChannelsForItem(TSharedPtr<FPBRSpecialMaterialItem> Item)
{
	if (!Item.IsValid())
	{
		LastMessages = { TEXT("请先选中一个扫描出来的特殊材质。") };
		RefreshViews();
		return FReply::Handled();
	}

	if (Item->SourceFolder.IsEmpty() || !FPaths::DirectoryExists(Item->SourceFolder))
	{
		LastMessages = { TEXT("这个条目没有来源文件夹，不能手动修改贴图通道。") };
		RefreshViews();
		return FReply::Handled();
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Item->SourceFolder, TEXT("*.*"), true, false);
	Files.RemoveAll([](const FString& File) { return !IsSpecialTextureFile(File); });
	Files.Sort();

	ManualTextureRows.Reset();
	for (const FString& File : Files)
	{
		TSharedPtr<FPBRSpecialTextureChannelItem> Row = MakeShared<FPBRSpecialTextureChannelItem>();
		Row->FilePath = File;
		Row->Role = Item->SpecialTextureRoles.Contains(File) ? Item->SpecialTextureRoles[File] : DetectSpecialTextureRole(File);
		for (const TPair<FString, FString>& Pair : Item->SpecialTextures)
		{
			if (FPaths::IsSamePath(Pair.Value, File))
			{
				Row->Role = Pair.Key;
				break;
			}
		}
		ManualTextureRows.Add(Row);
	}

	if (ManualTextureRows.Num() == 0)
	{
		LastMessages = { TEXT("来源文件夹里没有可识别的图片文件。") };
		RefreshViews();
		return FReply::Handled();
	}

	TSharedPtr<SListView<TSharedPtr<FPBRSpecialTextureChannelItem>>> ManualListView;
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("手动修改特殊材质贴图通道")))
		.ClientSize(FVector2D(820, 520))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 6)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("材质: %s    文件夹: %s"), *Item->DisplayName, *Item->SourceFolder)))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(12, 4)
		[
			SAssignNew(ManualListView, SListView<TSharedPtr<FPBRSpecialTextureChannelItem>>)
			.ListItemsSource(&ManualTextureRows)
			.HeaderRow(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(TEXT("Preview")).DefaultLabel(LOCTEXT("ManualPreview", "预览")).FillWidth(0.14f)
				+ SHeaderRow::Column(TEXT("File")).DefaultLabel(LOCTEXT("ManualFile", "贴图文件")).FillWidth(0.66f)
				+ SHeaderRow::Column(TEXT("Role")).DefaultLabel(LOCTEXT("ManualRole", "通道")).FillWidth(0.34f))
			.OnGenerateRow(this, &SPBRSpecialMaterialsTab::GenerateManualTextureRow)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("ManualApply", "应用通道"))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.OnClicked_Lambda([this, Item, Window]()
				{
					ApplyManualTextureChannels(Item);
					Window->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ManualCancel", "取消"))
				.OnClicked_Lambda([Window]()
				{
					Window->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]);

	FSlateApplication::Get().AddWindow(Window);
	return FReply::Handled();
}

void SPBRSpecialMaterialsTab::OnSelectionChanged(TSharedPtr<FPBRSpecialMaterialItem> Item, ESelectInfo::Type SelectInfo)
{
}

TSharedRef<SWidget> SPBRSpecialMaterialsTab::GenerateSpecialTemplateOption(FStringOption Option) const
{
	const FString AssetName = Option.IsValid() ? *Option : FString();
	return SNew(STextBlock).Text(FText::FromString(SpecialDisplayName(AssetName)));
}

void SPBRSpecialMaterialsTab::OnSpecialTemplateSelected(FStringOption Option, ESelectInfo::Type SelectInfo, TSharedPtr<FPBRSpecialMaterialItem> Item)
{
	if (!Option.IsValid() || !Item.IsValid())
	{
		return;
	}

	ApplyManualSpecialTemplate(Item, *Option);
}

FText SPBRSpecialMaterialsTab::GetSpecialTemplateText(TSharedPtr<FPBRSpecialMaterialItem> Item) const
{
	return FText::FromString(Item.IsValid() ? SpecialDisplayName(Item->AssetName) : FString());
}

void SPBRSpecialMaterialsTab::ApplyManualSpecialTemplate(TSharedPtr<FPBRSpecialMaterialItem> Item, const FString& AssetName)
{
	if (!Item.IsValid() || AssetName.IsEmpty())
	{
		return;
	}

	Item->AssetName = AssetName;
	Item->Category = SpecialCategory(AssetName);
	Item->Description = Item->SpecialTextures.Num() > 0
		? FString::Printf(TEXT("手动识别为: %s。贴图: %s"), *SpecialDisplayName(AssetName), *JoinSpecialTextureRoles(Item->SpecialTextures))
		: SpecialDescription(AssetName);
	Item->Status = TEXT("手动识别");
	Item->CreatedMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(SpecialMaterialPath(AssetName)));
	LastMessages = { FString::Printf(TEXT("已手动识别: %s -> %s"), *Item->DisplayName, *SpecialDisplayName(AssetName)) };
	RefreshViews();
}

TSharedRef<SWidget> SPBRSpecialMaterialsTab::GenerateTextureRoleOption(FStringOption Option) const
{
	return SNew(STextBlock).Text(FText::FromString(Option.IsValid() ? *Option : FString()));
}

TSharedRef<ITableRow> SPBRSpecialMaterialsTab::GenerateManualTextureRow(TSharedPtr<FPBRSpecialTextureChannelItem> TextureItem, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FPBRSpecialTextureChannelItem>>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.14f).Padding(4)
			[
				SNew(SBox)
				.WidthOverride(48.0f)
				.HeightOverride(48.0f)
				[
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit)
					.StretchDirection(EStretchDirection::DownOnly)
					[
						SNew(SImage)
						.Image_Lambda([this, TextureItem]()
						{
							return TextureItem.IsValid() ? GetManualTextureBrush(TextureItem->FilePath) : FAppStyle::GetBrush("WhiteBrush");
						})
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.52f).Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TextureItem.IsValid() ? FPaths::GetCleanFilename(TextureItem->FilePath) : FString()))
				.ToolTipText(FText::FromString(TextureItem.IsValid() ? TextureItem->FilePath : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.34f).Padding(4)
			[
				SNew(SComboBox<FStringOption>)
				.OptionsSource(&TextureRoleOptions)
				.OnGenerateWidget(this, &SPBRSpecialMaterialsTab::GenerateTextureRoleOption)
				.OnSelectionChanged(this, &SPBRSpecialMaterialsTab::OnTextureRoleSelected, TextureItem)
				[
					SNew(STextBlock)
					.Text_Lambda([TextureItem]() { return FText::FromString(TextureItem.IsValid() ? TextureItem->Role : FString()); })
				]
			]
		];
}

void SPBRSpecialMaterialsTab::OnTextureRoleSelected(FStringOption Option, ESelectInfo::Type SelectInfo, TSharedPtr<FPBRSpecialTextureChannelItem> TextureItem)
{
	if (Option.IsValid() && TextureItem.IsValid())
	{
		TextureItem->Role = *Option;
	}
}

void SPBRSpecialMaterialsTab::ApplyManualTextureChannels(TSharedPtr<FPBRSpecialMaterialItem> Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	RebuildSpecialTexturesFromManualRows(ManualTextureRows, Item->SpecialTextures, Item->SpecialTextureRoles, Item->PreviewPath);
	const FString SuggestedAssetName = GuessSpecialAssetNameFromTextures(Item->SourceFolder, Item->SpecialTextures);
	Item->AssetName = SuggestedAssetName;
	Item->Category = SpecialCategory(SuggestedAssetName);
	Item->Description = Item->SpecialTextures.Num() > 0
		? FString::Printf(TEXT("手动通道: %s。建议模板: %s"), *JoinSpecialTextureRoles(Item->SpecialTextures), *SpecialDisplayName(SuggestedAssetName))
		: TEXT("已手动忽略全部贴图。");
	Item->Status = FString::Printf(TEXT("手动通道: %d 张"), Item->SpecialTextures.Num());
	Item->CreatedMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(SpecialMaterialPath(Item->AssetName)));
	LastMessages = { FString::Printf(TEXT("已更新 %s 的贴图通道。当前使用 %d 张贴图。"), *Item->DisplayName, Item->SpecialTextures.Num()) };
	RefreshViews();
}

const FSlateBrush* SPBRSpecialMaterialsTab::GetManualTextureBrush(const FString& ImagePath)
{
	if (ImagePath.IsEmpty() || !FPaths::FileExists(ImagePath))
	{
		return FAppStyle::GetBrush("WhiteBrush");
	}

	if (!PreviewBrushCache.Contains(ImagePath))
	{
		PreviewBrushCache.Add(
			ImagePath,
			MakeShared<FSlateDynamicImageBrush>(FName(*ImagePath), GetPreviewImageSize(ImagePath)));
	}

	const TSharedPtr<FSlateDynamicImageBrush>* Brush = PreviewBrushCache.Find(ImagePath);
	return Brush && Brush->IsValid() ? Brush->Get() : FAppStyle::GetBrush("WhiteBrush");
}

TSharedRef<SHeaderRow> SPBRSpecialMaterialsTab::BuildHeaderRow()
{
	return SNew(SHeaderRow)
		+ SHeaderRow::Column(TEXT("Check")).DefaultLabel(LOCTEXT("ColCheck", "选择")).FillWidth(0.07f)
		+ SHeaderRow::Column(TEXT("Preview")).DefaultLabel(LOCTEXT("ColPreview", "预览")).FillWidth(0.1f)
		+ SHeaderRow::Column(TEXT("Status")).DefaultLabel(LOCTEXT("ColStatus", "状态")).FillWidth(0.12f)
		+ SHeaderRow::Column(TEXT("Name")).DefaultLabel(LOCTEXT("ColName", "名称")).FillWidth(0.18f)
		+ SHeaderRow::Column(TEXT("Category")).DefaultLabel(LOCTEXT("ColCategory", "分类")).FillWidth(0.12f)
		+ SHeaderRow::Column(TEXT("Asset")).DefaultLabel(LOCTEXT("ColAsset", "手动识别")).FillWidth(0.16f)
		+ SHeaderRow::Column(TEXT("Channels")).DefaultLabel(LOCTEXT("ColChannels", "贴图通道")).FillWidth(0.11f)
		+ SHeaderRow::Column(TEXT("Desc")).DefaultLabel(LOCTEXT("ColDesc", "用途/来源")).FillWidth(0.22f);
}

TSharedRef<ITableRow> SPBRSpecialMaterialsTab::OnGenerateRow(TSharedPtr<FPBRSpecialMaterialItem> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FPBRSpecialMaterialItem>>, Owner)
		.OnDragDetected(this, &SPBRSpecialMaterialsTab::OnMaterialDragDetected, Item)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.07f).Padding(4)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]() { return Item.IsValid() && Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState NewState)
				{
					if (Item.IsValid()) { Item->bChecked = NewState == ECheckBoxState::Checked; RefreshViews(); }
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.1f).Padding(4)
			[
				SNew(SBox)
				.WidthOverride(54.0f)
				.HeightOverride(54.0f)
				[
					BuildPreviewWidget(Item, FVector2D(54.0f, 54.0f))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(4)
			[
				SNew(STextBlock).Text_Lambda([Item]() { return FText::FromString(Item.IsValid() ? Item->Status : FString()); })
			]
			+ SHorizontalBox::Slot().FillWidth(0.18f).Padding(4)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->DisplayName : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(4)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->Category : FString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.16f).Padding(4)
			[
				SNew(SComboBox<FStringOption>)
				.OptionsSource(&SpecialTemplateOptions)
				.OnGenerateWidget(this, &SPBRSpecialMaterialsTab::GenerateSpecialTemplateOption)
				.OnSelectionChanged(this, &SPBRSpecialMaterialsTab::OnSpecialTemplateSelected, Item)
				[
					SNew(STextBlock)
					.Text(this, &SPBRSpecialMaterialsTab::GetSpecialTemplateText, Item)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.11f).Padding(4)
			[
				SNew(SButton)
				.Text(LOCTEXT("RowManualChannels", "通道..."))
				.Visibility_Lambda([Item]()
				{
					return Item.IsValid() && !Item->SourceFolder.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.OnClicked(this, &SPBRSpecialMaterialsTab::OnManualTextureChannelsForItem, Item)
			]
			+ SHorizontalBox::Slot().FillWidth(0.22f).Padding(4)
			[
				SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? Item->Description : FString())).AutoWrapText(true)
			]
		];
}

TSharedRef<ITableRow> SPBRSpecialMaterialsTab::OnGenerateTile(TSharedPtr<FPBRSpecialMaterialItem> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FPBRSpecialMaterialItem>>, Owner)
		.OnDragDetected(this, &SPBRSpecialMaterialsTab::OnMaterialDragDetected, Item)
		[
			SNew(SBorder)
			.Padding(6)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2)
				[
					SNew(SBox).WidthOverride(86).HeightOverride(86)
					[
						BuildPreviewWidget(Item, FVector2D(86.0f, 86.0f))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.IsValid() ? Item->DisplayName : FString()))
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]
			]
		];
}

TSharedPtr<SWidget> SPBRSpecialMaterialsTab::MakeContextMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	const TArray<TSharedPtr<FPBRSpecialMaterialItem>> Selected = bGridViewMode && TileView.IsValid()
		? TileView->GetSelectedItems()
		: (ListView.IsValid() ? ListView->GetSelectedItems() : TArray<TSharedPtr<FPBRSpecialMaterialItem>>());
	const bool bHasScannedSelection = Selected.ContainsByPredicate([](const TSharedPtr<FPBRSpecialMaterialItem>& Item)
	{
		return Item.IsValid() && !Item->SourceFolder.IsEmpty();
	});

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ManualDetectSection", "手动识别为"));
	for (const FStringOption& Option : SpecialTemplateOptions)
	{
		if (!Option.IsValid())
		{
			continue;
		}

		const FString AssetName = *Option;
		MenuBuilder.AddMenuEntry(
			FText::FromString(SpecialDisplayName(AssetName)),
			FText::FromString(AssetName),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, AssetName, Selected]()
			{
				for (const TSharedPtr<FPBRSpecialMaterialItem>& Item : Selected)
				{
					ApplyManualSpecialTemplate(Item, AssetName);
				}
			})));
	}
	MenuBuilder.EndSection();
	MenuBuilder.AddMenuSeparator();
	if (bHasScannedSelection)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ManualChannelsContext", "手动修改贴图通道"),
			LOCTEXT("ManualChannelsContextTip", "逐张指定特殊贴图的通道角色"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Selected]()
			{
				for (const TSharedPtr<FPBRSpecialMaterialItem>& Item : Selected)
				{
					if (Item.IsValid() && !Item->SourceFolder.IsEmpty())
					{
						OnManualTextureChannelsForItem(Item);
						break;
					}
				}
			})));
	}
	MenuBuilder.AddMenuEntry(
		LOCTEXT("CreateContext", "创建/刷新材质"),
		LOCTEXT("CreateContextTip", "创建或刷新当前特殊材质模板"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { CreateMaterials(TEXT("selected")); })));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ApplyContext", "应用到选中对象"),
		LOCTEXT("ApplyContextTip", "把当前特殊材质应用到场景选中对象"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { OnApplySelectedToSelection(); })));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenContext", "打开生成位置"),
		LOCTEXT("OpenContextTip", "打开 /Game/PBRStudio/SpecialMaterials 对应内容文件夹"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { OnOpenSelectedLocation(); })));
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SPBRSpecialMaterialsTab::BuildPreviewWidget(TSharedPtr<FPBRSpecialMaterialItem> Item, const FVector2D& Size)
{
	if (Item.IsValid() && !Item->PreviewPath.IsEmpty() && FPaths::FileExists(Item->PreviewPath))
	{
		return SNew(SImage)
			.Image(GetPreviewBrush(*Item))
			.DesiredSizeOverride(Size);
	}

	UMaterialInterface* Material = GetPreviewFallbackMaterial(Item);
	if (!ThumbnailPool.IsValid() || !Material)
	{
		return SNew(SImage)
			.Image(FAppStyle::GetBrush("ClassThumbnail.Material"))
			.DesiredSizeOverride(Size);
	}

	const FString CacheKey = Material->GetPathName() + FString::Printf(TEXT("_%dx%d"), FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y));
	TSharedPtr<FAssetThumbnail>& Thumbnail = MaterialThumbnailCache.FindOrAdd(CacheKey);
	if (!Thumbnail.IsValid())
	{
		Thumbnail = MakeShared<FAssetThumbnail>(Material, static_cast<uint32>(Size.X), static_cast<uint32>(Size.Y), ThumbnailPool);
	}
	return Thumbnail->MakeThumbnailWidget();
}

UMaterialInterface* SPBRSpecialMaterialsTab::GetPreviewFallbackMaterial(TSharedPtr<FPBRSpecialMaterialItem> Item) const
{
	if (Item.IsValid())
	{
		if (UMaterialInterface* Created = Item->CreatedMaterial.LoadSynchronous())
		{
			return Created;
		}
		if (!Item->AssetName.IsEmpty())
		{
			if (UMaterialInterface* TemplateMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(SpecialMaterialPath(Item->AssetName))))
			{
				return TemplateMaterial;
			}
		}
	}
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
}

const FSlateBrush* SPBRSpecialMaterialsTab::GetPreviewBrush(const FPBRSpecialMaterialItem& Item)
{
	if (Item.PreviewPath.IsEmpty() || !FPaths::FileExists(Item.PreviewPath))
	{
		return FAppStyle::GetBrush("WhiteBrush");
	}

	if (!PreviewBrushCache.Contains(Item.PreviewPath))
	{
		PreviewBrushCache.Add(
			Item.PreviewPath,
			MakeShared<FSlateDynamicImageBrush>(FName(*Item.PreviewPath), GetPreviewImageSize(Item.PreviewPath)));
	}

	const TSharedPtr<FSlateDynamicImageBrush>* Brush = PreviewBrushCache.Find(Item.PreviewPath);
	return Brush && Brush->IsValid() ? Brush->Get() : FAppStyle::GetBrush("WhiteBrush");
}

FVector2D SPBRSpecialMaterialsTab::GetPreviewImageSize(const FString& ImagePath) const
{
	constexpr float MaxSize = 86.0f;
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *ImagePath))
	{
		return FVector2D(MaxSize, MaxSize);
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		return FVector2D(MaxSize, MaxSize);
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		return FVector2D(MaxSize, MaxSize);
	}

	const float Width = static_cast<float>(ImageWrapper->GetWidth());
	const float Height = static_cast<float>(ImageWrapper->GetHeight());
	if (Width <= 0.0f || Height <= 0.0f)
	{
		return FVector2D(MaxSize, MaxSize);
	}

	const float Scale = MaxSize / FMath::Max(Width, Height);
	return FVector2D(Width * Scale, Height * Scale);
}

FReply SPBRSpecialMaterialsTab::OnMaterialDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent, TSharedPtr<FPBRSpecialMaterialItem> Item)
{
	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || !Item.IsValid())
	{
		return FReply::Unhandled();
	}
	UMaterialInterface* Material = Item->CreatedMaterial.LoadSynchronous();
	if (!Material)
	{
		Item->Status = TEXT("不能拖拽: 请先创建材质");
		RefreshViews();
		return FReply::Unhandled();
	}
	Item->Status = TEXT("拖拽中: 可放到场景物体或材质槽");
	RefreshViews();
	return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(FAssetData(Material)));
}

FText SPBRSpecialMaterialsTab::GetStatusText() const
{
	if (LastMessages.Num() == 0)
	{
		return LOCTEXT("Ready", "一键创建特殊材质会生成母材质和示例材质。下面的扫描列表用于你电脑里的特殊材质图片，可手动指定每张图的通道后创建实例。");
	}
	if (LastCreatedCount > 0)
	{
		return FText::FromString(FString::Printf(TEXT("已处理 %d 个特殊材质。\n\n%s"), LastCreatedCount, *FString::Join(LastMessages, TEXT("\n"))));
	}
	return FText::FromString(FString::Join(LastMessages, TEXT("\n")));
}

EVisibility SPBRSpecialMaterialsTab::GetListVisibility() const
{
	return bGridViewMode ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SPBRSpecialMaterialsTab::GetGridVisibility() const
{
	return bGridViewMode ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
