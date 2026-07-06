#include "Widgets/SPBRMagicOutlinerWindow.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetData.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ContentBrowserModule.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/Light.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Selection.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "GameFramework/Actor.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "IContentBrowserSingleton.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Materials/Material.h"
#include "InputCoreTypes.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/MessageDialog.h"
#include "Misc/Base64.h"
#include "ImageUtils.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Models/PBRMaterialTypes.h"
#include "PBRStudioModule.h"
#include "PBRStudioStyle.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/DrawElements.h"
#include "Services/PBRDataStore.h"
#include "Services/PBRLocalization.h"
#include "Services/PBRSceneMaterialReplacer.h"
#include "Styling/AppStyle.h"
#include "ScopedTransaction.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "SPBRMagicOutlinerWindow"

namespace
{
const TCHAR* MagicOutlinerAutoSelectConfigKey = TEXT("magic_outliner_auto_select_checked");
const TCHAR* MagicOutlinerIsolationShortcutConfigKey = TEXT("magic_outliner_isolation_shortcut");
const TCHAR* MagicOutlinerCompactModeConfigKey = TEXT("magic_outliner_compact_mode");
const TCHAR* MagicOutlinerClassicSkinConfigKey = TEXT("magic_outliner_classic_skin");
const TCHAR* MagicOutlinerThemeConfigKey = TEXT("magic_outliner_theme");

static FText PBRText(const TCHAR* Key, const TCHAR* Chinese, const TCHAR* English)
{
	return FPBRLocalization::Text(Key, Chinese, English);
}

struct FPBRMagicMaterialTypeOption
{
	EPBRMaterialType Type;
	const TCHAR* Key;
	const TCHAR* Chinese;
	const TCHAR* English;
};

struct FPBRMagicAISuggestion
{
	EPBRMaterialType MaterialType = EPBRMaterialType::Standard;
	FString Summary;
	TMap<FName, float> Scalars;
	TMap<FName, FLinearColor> Colors;
	TMap<FName, bool> Switches;
};

struct FPBRMagicAIImagePayload
{
	FString Label;
	FString TextureName;
	FString TexturePath;
	FString DataUrl;
};

struct FPBRMagicAIProviderPreset
{
	const TCHAR* Label;
	const TCHAR* EndpointUrl;
	const TCHAR* DefaultModel;
	const TCHAR* ApiPageUrl;
	const TCHAR* KeyHint;
};

static const TArray<FPBRMagicMaterialTypeOption>& GetMagicMaterialTypeOptions()
{
	static const TArray<FPBRMagicMaterialTypeOption> Options = {
		{ EPBRMaterialType::Standard, TEXT("MagicMaterialTypeStandard"), TEXT("标准"), TEXT("Standard") },
		{ EPBRMaterialType::Wood, TEXT("MagicMaterialTypeWood"), TEXT("木材"), TEXT("Wood") },
		{ EPBRMaterialType::Stone, TEXT("MagicMaterialTypeStone"), TEXT("石材"), TEXT("Stone") },
		{ EPBRMaterialType::Tile, TEXT("MagicMaterialTypeTile"), TEXT("瓷砖"), TEXT("Tile") },
		{ EPBRMaterialType::Fabric, TEXT("MagicMaterialTypeFabric"), TEXT("布料"), TEXT("Fabric") },
		{ EPBRMaterialType::Leather, TEXT("MagicMaterialTypeLeather"), TEXT("皮革"), TEXT("Leather") },
		{ EPBRMaterialType::Plastic, TEXT("MagicMaterialTypePlastic"), TEXT("塑料"), TEXT("Plastic") },
		{ EPBRMaterialType::Metal, TEXT("MagicMaterialTypeMetal"), TEXT("金属"), TEXT("Metal") },
		{ EPBRMaterialType::Transparent, TEXT("MagicMaterialTypeTransparent"), TEXT("半透明"), TEXT("Transparent") },
		{ EPBRMaterialType::Glass, TEXT("MagicMaterialTypeGlass"), TEXT("玻璃"), TEXT("Glass") },
		{ EPBRMaterialType::Water, TEXT("MagicMaterialTypeWater"), TEXT("水"), TEXT("Water") },
		{ EPBRMaterialType::Emissive, TEXT("MagicMaterialTypeEmissive"), TEXT("自发光"), TEXT("Emissive") }
	};
	return Options;
}

static const TArray<FPBRMagicAIProviderPreset>& GetMagicAIProviderPresets()
{
	static const TArray<FPBRMagicAIProviderPreset> Presets = {
		{ TEXT("OpenAI"), TEXT("https://api.openai.com/v1"), TEXT("gpt-4.1-mini"), TEXT("https://platform.openai.com/api-keys"), TEXT("OPENAI_API_KEY 或 sk-...") },
		{ TEXT("DeepSeek"), TEXT("https://api.deepseek.com/v1"), TEXT("deepseek-chat"), TEXT("https://platform.deepseek.com/api_keys"), TEXT("DEEPSEEK_API_KEY 或 sk-...") },
		{ TEXT("SiliconFlow"), TEXT("https://api.siliconflow.cn/v1"), TEXT("deepseek-ai/DeepSeek-V3"), TEXT("https://cloud.siliconflow.cn/account/ak"), TEXT("SILICONFLOW_API_KEY 或 sk-...") },
		{ TEXT("智谱/Z.AI"), TEXT("https://open.bigmodel.cn/api/paas/v4"), TEXT("glm-4.5"), TEXT("https://bigmodel.cn/usercenter/proj-mgmt/apikeys"), TEXT("ZHIPUAI_API_KEY 或 glm-...") },
		{ TEXT("OpenRouter"), TEXT("https://openrouter.ai/api/v1"), TEXT("openai/gpt-4.1-mini"), TEXT("https://openrouter.ai/settings/keys"), TEXT("OPENROUTER_API_KEY 或 sk-or-...") },
		{ TEXT("Ollama 本地"), TEXT("http://localhost:11434/v1"), TEXT("llama3.1"), TEXT("https://ollama.com/download"), TEXT("本地通常可留空") }
	};
	return Presets;
}

static const FPBRMagicAIProviderPreset* FindMagicAIProviderPresetByEndpoint(const FString& EndpointUrl)
{
	FString NormalizedEndpoint = EndpointUrl.TrimStartAndEnd().ToLower();
	NormalizedEndpoint.ReplaceInline(TEXT("/chat/completions"), TEXT(""));
	while (NormalizedEndpoint.EndsWith(TEXT("/")))
	{
		NormalizedEndpoint.LeftChopInline(1);
	}
	for (const FPBRMagicAIProviderPreset& Preset : GetMagicAIProviderPresets())
	{
		FString PresetEndpoint = FString(Preset.EndpointUrl).TrimStartAndEnd().ToLower();
		while (PresetEndpoint.EndsWith(TEXT("/")))
		{
			PresetEndpoint.LeftChopInline(1);
		}
		if (NormalizedEndpoint.Contains(PresetEndpoint))
		{
			return &Preset;
		}
	}
	return nullptr;
}

static FText GetMagicMaterialTypeLabel(EPBRMaterialType MaterialType)
{
	for (const FPBRMagicMaterialTypeOption& Option : GetMagicMaterialTypeOptions())
	{
		if (Option.Type == MaterialType)
		{
			return PBRText(Option.Key, Option.Chinese, Option.English);
		}
	}
	return PBRText(TEXT("MagicMaterialTypeUnknown"), TEXT("未知"), TEXT("Unknown"));
}

static EPBRMaterialType GuessMagicMaterialTypeFromMaterial(UMaterialInterface* Material)
{
	const UMaterialInterface* InspectMaterial = Material;
	if (const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
	{
		InspectMaterial = Instance->Parent;
	}

	const FString Name = InspectMaterial ? (InspectMaterial->GetName() + TEXT(" ") + InspectMaterial->GetPathName()).ToLower() : FString();
	if (Name.Contains(TEXT("wood")) || Name.Contains(TEXT("木")))
	{
		return EPBRMaterialType::Wood;
	}
	if (Name.Contains(TEXT("stone")) || Name.Contains(TEXT("石")))
	{
		return EPBRMaterialType::Stone;
	}
	if (Name.Contains(TEXT("tile")) || Name.Contains(TEXT("瓷")) || Name.Contains(TEXT("砖")))
	{
		return EPBRMaterialType::Tile;
	}
	if (Name.Contains(TEXT("fabric")) || Name.Contains(TEXT("cloth")) || Name.Contains(TEXT("布")))
	{
		return EPBRMaterialType::Fabric;
	}
	if (Name.Contains(TEXT("leather")) || Name.Contains(TEXT("皮")))
	{
		return EPBRMaterialType::Leather;
	}
	if (Name.Contains(TEXT("plastic")) || Name.Contains(TEXT("塑")))
	{
		return EPBRMaterialType::Plastic;
	}
	if (Name.Contains(TEXT("metal")) || Name.Contains(TEXT("金属")))
	{
		return EPBRMaterialType::Metal;
	}
	if (Name.Contains(TEXT("transparent")) || Name.Contains(TEXT("translucent")) || Name.Contains(TEXT("半透明")))
	{
		return EPBRMaterialType::Transparent;
	}
	if (Name.Contains(TEXT("glass")) || Name.Contains(TEXT("玻璃")))
	{
		return EPBRMaterialType::Glass;
	}
	if (Name.Contains(TEXT("water")) || Name.Contains(TEXT("水")))
	{
		return EPBRMaterialType::Water;
	}
	if (Name.Contains(TEXT("emissive")) || Name.Contains(TEXT("自发光")))
	{
		return EPBRMaterialType::Emissive;
	}
	return EPBRMaterialType::Standard;
}

static bool PBRMagicTextHasAny(const FString& Text, std::initializer_list<const TCHAR*> Tokens)
{
	for (const TCHAR* Token : Tokens)
	{
		if (Text.Contains(Token, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

static FName PBRMagicGetAITextureUsageSwitchName(const FName& TextureParameterName)
{
	if (TextureParameterName == FPBRMaterialParameters::BaseColorTexture) { return FPBRMaterialParameters::UseBaseColorTexture; }
	if (TextureParameterName == FPBRMaterialParameters::NormalTexture) { return FPBRMaterialParameters::UseNormalTexture; }
	if (TextureParameterName == FPBRMaterialParameters::RoughnessTexture) { return FPBRMaterialParameters::UseRoughnessTexture; }
	if (TextureParameterName == FPBRMaterialParameters::SpecularTexture) { return FPBRMaterialParameters::UseSpecularTexture; }
	if (TextureParameterName == FPBRMaterialParameters::MetallicTexture) { return FPBRMaterialParameters::UseMetallicTexture; }
	if (TextureParameterName == FPBRMaterialParameters::AOTexture) { return FPBRMaterialParameters::UseAOTexture; }
	if (TextureParameterName == FPBRMaterialParameters::OpacityTexture) { return FPBRMaterialParameters::UseOpacityTexture; }
	if (TextureParameterName == FPBRMaterialParameters::HeightTexture) { return FPBRMaterialParameters::UseHeightTexture; }
	if (TextureParameterName == FPBRMaterialParameters::EmissiveTexture) { return FPBRMaterialParameters::UseEmissiveTexture; }
	if (TextureParameterName == FPBRMaterialParameters::WaterRippleTexture) { return FPBRMaterialParameters::UseWaterRippleTexture; }
	if (TextureParameterName == FPBRMaterialParameters::GlassDirtTexture) { return FPBRMaterialParameters::UseGlassDirtTexture; }
	if (TextureParameterName == FPBRMaterialParameters::GlassDistortionTexture) { return FPBRMaterialParameters::UseGlassDistortionTexture; }
	if (TextureParameterName == FPBRMaterialParameters::GlassFrostedTexture) { return FPBRMaterialParameters::UseGlassFrostedTexture; }
	return NAME_None;
}

static bool PBRMagicGetStaticSwitchValue(UMaterialInstanceConstant* Instance, const FName& SwitchName, bool bDefaultValue)
{
	if (!Instance || SwitchName.IsNone())
	{
		return bDefaultValue;
	}
	bool bValue = bDefaultValue;
	FGuid ExpressionGuid;
	return Instance->GetStaticSwitchParameterValue(FMaterialParameterInfo(SwitchName), bValue, ExpressionGuid) ? bValue : bDefaultValue;
}

static bool PBRMagicLooksLikeDefaultTemplateTexture(UTexture* Texture)
{
	if (!Texture)
	{
		return false;
	}
	const FString Path = Texture->GetPathName();
	return Path.Contains(TEXT("/PBRStudio/Textures/T_ARM_Default_"), ESearchCase::IgnoreCase) ||
		Path.Contains(TEXT("/ArchRenderMaster/Textures/T_ARM_Default_"), ESearchCase::IgnoreCase);
}

static int32 PBRMagicGetAITexturePriority(const FName& ParameterName, const FString& TextureName)
{
	const FString Text = (ParameterName.ToString() + TEXT(" ") + TextureName).ToLower();
	if (Text.Contains(TEXT("base")) || Text.Contains(TEXT("albedo")) || Text.Contains(TEXT("diffuse")) || Text.Contains(TEXT("color")) || Text.Contains(TEXT("colour")) || Text.Contains(TEXT("基础色")) || Text.Contains(TEXT("颜色")))
	{
		return 0;
	}
	if (Text.Contains(TEXT("opacity")) || Text.Contains(TEXT("alpha")) || Text.Contains(TEXT("透明")))
	{
		return 5;
	}
	if (Text.Contains(TEXT("normal")) || Text.Contains(TEXT("nrm")) || Text.Contains(TEXT("法线")) ||
		Text.Contains(TEXT("rough")) || Text.Contains(TEXT("gloss")) || Text.Contains(TEXT("metal")) || Text.Contains(TEXT("spec")) || Text.Contains(TEXT("ao")) || Text.Contains(TEXT("mask")) || Text.Contains(TEXT("masks")) || Text.Contains(TEXT("粗糙")) || Text.Contains(TEXT("金属")) || Text.Contains(TEXT("遮蔽")))
	{
		return 100;
	}
	return 10;
}

static bool PBRMagicShouldExposeTextureToAI(UMaterialInstanceConstant* Instance, const FMaterialParameterInfo& Info, UTexture* Texture)
{
	if (!Texture)
	{
		return false;
	}
	if (PBRMagicGetAITexturePriority(Info.Name, Texture->GetName()) >= 100)
	{
		return false;
	}
	const FName SwitchName = PBRMagicGetAITextureUsageSwitchName(Info.Name);
	const bool bTextureEnabled = SwitchName.IsNone() || PBRMagicGetStaticSwitchValue(Instance, SwitchName, false);
	return bTextureEnabled || !PBRMagicLooksLikeDefaultTemplateTexture(Texture);
}

static bool PBRMagicShouldUseTextureForParameterSignal(UMaterialInstanceConstant* Instance, const FMaterialParameterInfo& Info, UTexture* Texture)
{
	if (!Texture || PBRMagicLooksLikeDefaultTemplateTexture(Texture))
	{
		return false;
	}
	const FName SwitchName = PBRMagicGetAITextureUsageSwitchName(Info.Name);
	return SwitchName.IsNone() || PBRMagicGetStaticSwitchValue(Instance, SwitchName, false);
}

static bool PBRMagicTryGetTextureAverageLuma(UTexture2D* Texture, float& OutLuma)
{
	OutLuma = 0.0f;
	if (!Texture)
	{
		return false;
	}

	FImage SourceImage;
	if (!FImageUtils::GetTexture2DSourceImage(Texture, SourceImage) || !SourceImage.IsImageInfoValid() || SourceImage.GetNumPixels() <= 0)
	{
		return false;
	}

	SourceImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::Linear);
	const TArrayView64<const FColor> Pixels = SourceImage.AsBGRA8();
	if (Pixels.Num() <= 0)
	{
		return false;
	}

	double Sum = 0.0;
	for (const FColor& Pixel : Pixels)
	{
		Sum += (0.2126 * static_cast<double>(Pixel.R) + 0.7152 * static_cast<double>(Pixel.G) + 0.0722 * static_cast<double>(Pixel.B)) / 255.0;
	}
	OutLuma = FMath::Clamp(static_cast<float>(Sum / static_cast<double>(Pixels.Num())), 0.0f, 1.0f);
	return true;
}

static FString PBRMagicGetTextureSignalRole(const FName& ParameterName, const FString& TextureName)
{
	const FString Text = (ParameterName.ToString() + TEXT(" ") + TextureName).ToLower();
	if (Text.Contains(TEXT("rough")) || Text.Contains(TEXT("gloss")) || Text.Contains(TEXT("粗糙")))
	{
		return TEXT("roughness/gloss");
	}
	if (Text.Contains(TEXT("metal")) || Text.Contains(TEXT("金属")))
	{
		return TEXT("metallic");
	}
	if (Text.Contains(TEXT("spec")) || Text.Contains(TEXT("高光")))
	{
		return TEXT("specular");
	}
	if (Text.Contains(TEXT("ao")) || Text.Contains(TEXT("occlusion")) || Text.Contains(TEXT("遮蔽")))
	{
		return TEXT("ambient_occlusion");
	}
	if (Text.Contains(TEXT("normal")) || Text.Contains(TEXT("nrm")) || Text.Contains(TEXT("法线")))
	{
		return TEXT("normal");
	}
	if (Text.Contains(TEXT("height")) || Text.Contains(TEXT("displacement")) || Text.Contains(TEXT("高度")) || Text.Contains(TEXT("置换")))
	{
		return TEXT("height");
	}
	return FString();
}

static FString PBRMagicCollectMaterialAIText(UMaterialInstanceConstant* Instance)
{
	if (!Instance)
	{
		return FString();
	}

	FString Text = Instance->GetName();

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> TextureParameters;
	Instance->GetAllParametersOfType(EMaterialParameterType::Texture, TextureParameters);
	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : TextureParameters)
	{
		UTexture* Texture = nullptr;
		if (Instance->GetTextureParameterValue(Pair.Key, Texture) && PBRMagicShouldExposeTextureToAI(Instance, Pair.Key, Texture))
		{
			Text += TEXT(" ");
			Text += Texture->GetName();
			Text += TEXT(" ");
			Text += Texture->GetPathName();
		}
	}
	return Text;
}

static FString PBRMagicGetAIConfigString(const TCHAR* Key, const TCHAR* DefaultValue)
{
	TSharedPtr<FJsonObject> Config;
	if (FPBRDataStore::LoadConfig(Config) && Config.IsValid())
	{
		FString Value;
		if (Config->TryGetStringField(Key, Value))
		{
			return Value;
		}
	}
	return DefaultValue;
}

static void PBRMagicSaveAIConfigString(const TCHAR* Key, const FString& Value)
{
	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		Config = MakeShared<FJsonObject>();
	}
	Config->SetStringField(Key, Value);
	FPBRDataStore::SaveConfig(Config);
}

struct FPBRMagicAIProviderSettings
{
	FString Provider = TEXT("LocalRules");
	FString EndpointUrl;
	FString Model = TEXT("gpt-4.1-mini");
	FString ApiKeyOrEnvironmentVariable = TEXT("PBRSTUDIO_AI_API_KEY");
	float RequestTimeoutSeconds = 20.0f;

	bool ShouldUseRemoteModel() const
	{
		return Provider.Equals(TEXT("OpenAICompatible"), ESearchCase::IgnoreCase) && !EndpointUrl.IsEmpty() && !Model.IsEmpty();
	}
};

static FPBRMagicAIProviderSettings PBRMagicLoadAIProviderSettings()
{
	FPBRMagicAIProviderSettings Settings;
	Settings.Provider = PBRMagicGetAIConfigString(TEXT("material_ai_provider"), TEXT("LocalRules"));
	Settings.EndpointUrl = PBRMagicGetAIConfigString(TEXT("material_ai_endpoint"), TEXT(""));
	Settings.Model = PBRMagicGetAIConfigString(TEXT("material_ai_model"), TEXT("gpt-4.1-mini"));
	Settings.ApiKeyOrEnvironmentVariable = PBRMagicGetAIConfigString(TEXT("material_ai_api_key_or_env"), TEXT("PBRSTUDIO_AI_API_KEY"));
	return Settings;
}

static FString PBRMagicNormalizeAIEndpointUrl(FString EndpointUrl)
{
	EndpointUrl.TrimStartAndEndInline();
	while (EndpointUrl.EndsWith(TEXT("/")))
	{
		EndpointUrl.LeftChopInline(1);
	}
	if (EndpointUrl.EndsWith(TEXT("/chat/completions"), ESearchCase::IgnoreCase))
	{
		return EndpointUrl;
	}
	if (EndpointUrl.EndsWith(TEXT("/v1"), ESearchCase::IgnoreCase) || EndpointUrl.EndsWith(TEXT("/v4"), ESearchCase::IgnoreCase))
	{
		return EndpointUrl + TEXT("/chat/completions");
	}
	return EndpointUrl;
}

static bool PBRMagicLooksLikeInlineApiKey(const FString& Value)
{
	const FString Trimmed = Value.TrimStartAndEnd();
	return Trimmed.StartsWith(TEXT("sk-"), ESearchCase::IgnoreCase) ||
		Trimmed.StartsWith(TEXT("glm-"), ESearchCase::IgnoreCase) ||
		Trimmed.Contains(TEXT("."));
}

static FString PBRMagicResolveAIApiKey(const FPBRMagicAIProviderSettings& Settings)
{
	const FString KeyField = Settings.ApiKeyOrEnvironmentVariable.TrimStartAndEnd();
	if (KeyField.IsEmpty())
	{
		return FString();
	}
	const FString EnvironmentValue = FPlatformMisc::GetEnvironmentVariable(*KeyField).TrimStartAndEnd();
	if (!EnvironmentValue.IsEmpty())
	{
		return EnvironmentValue;
	}
	return PBRMagicLooksLikeInlineApiKey(KeyField) ? KeyField : FString();
}

static FString PBRMagicJsonStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const FString& Fallback = FString())
{
	if (!Object.IsValid())
	{
		return Fallback;
	}
	FString Value;
	return Object->TryGetStringField(FieldName, Value) ? Value : Fallback;
}

static float PBRMagicJsonNumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, float Fallback)
{
	if (!Object.IsValid())
	{
		return Fallback;
	}
	double Value = 0.0;
	return Object->TryGetNumberField(FieldName, Value) ? static_cast<float>(Value) : Fallback;
}

static bool PBRMagicJsonBoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool bFallback)
{
	if (!Object.IsValid())
	{
		return bFallback;
	}
	bool bValue = false;
	return Object->TryGetBoolField(FieldName, bValue) ? bValue : bFallback;
}

static FString PBRMagicNormalizeParameterKey(FString Text)
{
	Text.ToLowerInline();
	const TCHAR Separators[] = { TCHAR('_'), TCHAR('-'), TCHAR('.'), TCHAR('/'), TCHAR('\\'), TCHAR(' '), TCHAR(':') };
	for (const TCHAR Separator : Separators)
	{
		Text.ReplaceCharInline(Separator, TCHAR(' '));
	}
	Text.ReplaceInline(TEXT("："), TEXT(" "));
	Text.ReplaceInline(TEXT(" "), TEXT(""));
	return Text;
}

static void PBRMagicAddParameterAlias(TMap<FString, FName>& Aliases, const FName& ParameterName, std::initializer_list<const TCHAR*> Names)
{
	Aliases.Add(PBRMagicNormalizeParameterKey(ParameterName.ToString()), ParameterName);
	for (const TCHAR* Name : Names)
	{
		Aliases.Add(PBRMagicNormalizeParameterKey(Name), ParameterName);
	}
}

static FName PBRMagicResolveScalarParameterName(const FString& RawName)
{
	static TMap<FString, FName> Aliases;
	if (Aliases.Num() == 0)
	{
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::BaseColorIntensity, { TEXT("基础色强度"), TEXT("颜色强度"), TEXT("base_color_intensity"), TEXT("basecolorintensity"), TEXT("albedo_intensity"), TEXT("color_intensity") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::NormalStrength, { TEXT("法线强度"), TEXT("normal_strength"), TEXT("normalstrength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::RoughnessValue, { TEXT("粗糙度数值"), TEXT("roughness"), TEXT("roughness_value") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::RoughnessMultiplier, { TEXT("粗糙度强度"), TEXT("roughness_multiplier"), TEXT("roughness_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::SpecularLevel, { TEXT("高光强度"), TEXT("specular"), TEXT("specular_level") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::AOMultiplier, { TEXT("环境遮蔽强度"), TEXT("ao_multiplier"), TEXT("ambient_occlusion_strength"), TEXT("occlusion_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::MetallicValue, { TEXT("金属度数值"), TEXT("metallic"), TEXT("metallic_value"), TEXT("metalness") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::MetallicMultiplier, { TEXT("金属度强度"), TEXT("metallic_multiplier"), TEXT("metallic_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::Anisotropy, { TEXT("各向异性"), TEXT("anisotropy") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::FlakeScale, { TEXT("金属颗粒缩放"), TEXT("flake_scale") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::FlakeIntensity, { TEXT("金属颗粒强度"), TEXT("flake_intensity") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::Opacity, { TEXT("透明度"), TEXT("opacity"), TEXT("transparency") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::HeightStrength, { TEXT("置换强度"), TEXT("高度强度"), TEXT("height_strength"), TEXT("displacement_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::PixelDepthOffsetStrength, { TEXT("深度偏移强度"), TEXT("pixel_depth_offset"), TEXT("pixel_depth_offset_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::EmissiveIntensity, { TEXT("自发光强度"), TEXT("emissive_intensity"), TEXT("emission_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::EmissiveTemperatureKelvin, { TEXT("自发光色温"), TEXT("emissive_temperature"), TEXT("emissive_kelvin"), TEXT("temperature_kelvin") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::FabricFuzzStrength, { TEXT("织物绒毛强度"), TEXT("fabric_fuzz_strength"), TEXT("fuzz_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::ClearCoat, { TEXT("清漆强度"), TEXT("clear_coat"), TEXT("clearcoat") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::ClearCoatRoughness, { TEXT("清漆粗糙度"), TEXT("clear_coat_roughness"), TEXT("clearcoat_roughness") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::RefractionAmount, { TEXT("折射强度"), TEXT("refraction"), TEXT("refraction_amount"), TEXT("ior") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassOpacityFresnelStrength, { TEXT("透明菲涅尔强度"), TEXT("glass_opacity_fresnel"), TEXT("opacity_fresnel") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassFresnelBaseReflection, { TEXT("菲涅尔基础反射"), TEXT("glass_fresnel_base"), TEXT("fresnel_base") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassFresnelExp, { TEXT("菲涅尔指数"), TEXT("glass_fresnel_exp"), TEXT("fresnel_exponent") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassFrostedStrength, { TEXT("毛玻璃强度"), TEXT("frosted_strength"), TEXT("glass_frosted_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassAbsorptionStrength, { TEXT("玻璃吸收强度"), TEXT("glass_absorption_strength"), TEXT("absorption_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassEdgeTintStrength, { TEXT("玻璃边缘染色"), TEXT("glass_edge_tint"), TEXT("edge_tint") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassDirtIntensity, { TEXT("玻璃污渍强度"), TEXT("glass_dirt_intensity"), TEXT("dirt_intensity") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassDirtOpacity, { TEXT("玻璃污渍透明度"), TEXT("glass_dirt_opacity"), TEXT("dirt_opacity") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassDirtRoughness, { TEXT("玻璃污渍粗糙度"), TEXT("glass_dirt_roughness"), TEXT("dirt_roughness") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassDistortionIntensity, { TEXT("玻璃扭曲强度"), TEXT("glass_distortion"), TEXT("distortion_intensity") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassDistortionIORIntensity, { TEXT("玻璃 IOR 扭曲强度"), TEXT("glass_ior_distortion"), TEXT("ior_distortion") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassShadowOpacity, { TEXT("玻璃投影强度"), TEXT("glass_shadow_opacity"), TEXT("shadow_opacity") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassCausticsIntensity, { TEXT("玻璃焦散强度"), TEXT("glass_caustics_intensity"), TEXT("caustics_intensity") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassCausticsScale, { TEXT("玻璃焦散大小"), TEXT("glass_caustics_scale"), TEXT("caustics_scale") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::WaterRippleStrength, { TEXT("水波强度"), TEXT("water_ripple_strength"), TEXT("ripple_strength") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::WaterRippleScale, { TEXT("水波缩放"), TEXT("water_ripple_scale"), TEXT("ripple_scale") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::WaterFlowSpeedU, { TEXT("水流 U 速度"), TEXT("water_flow_u"), TEXT("flow_u") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::WaterFlowSpeedV, { TEXT("水流 V 速度"), TEXT("water_flow_v"), TEXT("flow_v") });
	}
	if (const FName* ParameterName = Aliases.Find(PBRMagicNormalizeParameterKey(RawName)))
	{
		return *ParameterName;
	}
	return NAME_None;
}

static FName PBRMagicResolveSwitchParameterName(const FString& RawName)
{
	static TMap<FString, FName> Aliases;
	if (Aliases.Num() == 0)
	{
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseBaseColorTexture, { TEXT("使用基础色贴图"), TEXT("use_base_color_texture"), TEXT("use_albedo_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseNormalTexture, { TEXT("使用法线贴图"), TEXT("use_normal_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseRoughnessTexture, { TEXT("使用粗糙度贴图"), TEXT("use_roughness_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseSpecularTexture, { TEXT("使用高光贴图"), TEXT("use_specular_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseMetallicTexture, { TEXT("使用金属度贴图"), TEXT("use_metallic_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseAOTexture, { TEXT("使用环境遮蔽贴图"), TEXT("use_ao_texture"), TEXT("use_occlusion_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseOpacityTexture, { TEXT("使用透明贴图"), TEXT("use_opacity_texture"), TEXT("use_alpha_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseHeightTexture, { TEXT("使用高度贴图"), TEXT("use_height_texture"), TEXT("use_displacement_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseEmissiveTexture, { TEXT("使用自发光贴图"), TEXT("use_emissive_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseWaterRippleTexture, { TEXT("使用水波贴图"), TEXT("use_water_ripple_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseGlassDirtTexture, { TEXT("使用玻璃污渍贴图"), TEXT("use_glass_dirt_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseGlassDistortionTexture, { TEXT("使用玻璃扭曲贴图"), TEXT("use_glass_distortion_texture") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::UseGlassFrostedTexture, { TEXT("使用毛玻璃贴图"), TEXT("use_glass_frosted_texture") });
	}
	if (const FName* ParameterName = Aliases.Find(PBRMagicNormalizeParameterKey(RawName)))
	{
		return *ParameterName;
	}
	return NAME_None;
}

static FName PBRMagicResolveColorParameterName(const FString& RawName)
{
	static TMap<FString, FName> Aliases;
	if (Aliases.Num() == 0)
	{
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::BaseColorTint, { TEXT("基础色调"), TEXT("base_color"), TEXT("base_color_tint"), TEXT("albedo_tint") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::EmissiveColor, { TEXT("自发光颜色"), TEXT("emissive_color"), TEXT("emission_color") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::FabricFuzzColor, { TEXT("织物绒毛颜色"), TEXT("fabric_fuzz_color"), TEXT("fuzz_color") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::WaterColor, { TEXT("水颜色"), TEXT("water_color") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassAbsorptionColor, { TEXT("玻璃吸收颜色"), TEXT("glass_absorption_color"), TEXT("absorption_color") });
		PBRMagicAddParameterAlias(Aliases, FPBRMaterialParameters::GlassDirtColor, { TEXT("玻璃污渍颜色"), TEXT("glass_dirt_color"), TEXT("dirt_color") });
	}
	if (const FName* ParameterName = Aliases.Find(PBRMagicNormalizeParameterKey(RawName)))
	{
		return *ParameterName;
	}
	return NAME_None;
}

static bool PBRMagicParseMaterialType(const FString& TypeText, EPBRMaterialType& OutType)
{
	const FString Key = PBRMagicNormalizeParameterKey(TypeText);
	if (Key == TEXT("standard") || Key == TEXT("default") || Key == TEXT("pbr") || Key == TEXT("标准"))
	{
		OutType = EPBRMaterialType::Standard;
		return true;
	}
	if (Key == TEXT("wood") || Key == TEXT("timber") || Key == TEXT("木材") || Key == TEXT("木纹"))
	{
		OutType = EPBRMaterialType::Wood;
		return true;
	}
	if (Key == TEXT("stone") || Key == TEXT("marble") || Key == TEXT("granite") || Key == TEXT("石材") || Key == TEXT("大理石"))
	{
		OutType = EPBRMaterialType::Stone;
		return true;
	}
	if (Key == TEXT("tile") || Key == TEXT("brick") || Key == TEXT("ceramic") || Key == TEXT("porcelain") || Key == TEXT("瓷砖") || Key == TEXT("砖"))
	{
		OutType = EPBRMaterialType::Tile;
		return true;
	}
	if (Key == TEXT("fabric") || Key == TEXT("cloth") || Key == TEXT("carpet") || Key == TEXT("布艺") || Key == TEXT("织物") || Key == TEXT("地毯"))
	{
		OutType = EPBRMaterialType::Fabric;
		return true;
	}
	if (Key == TEXT("leather") || Key == TEXT("皮革") || Key == TEXT("真皮"))
	{
		OutType = EPBRMaterialType::Leather;
		return true;
	}
	if (Key == TEXT("plastic") || Key == TEXT("rubber") || Key == TEXT("pvc") || Key == TEXT("塑料") || Key == TEXT("橡胶"))
	{
		OutType = EPBRMaterialType::Plastic;
		return true;
	}
	if (Key == TEXT("metal") || Key == TEXT("metallic") || Key == TEXT("steel") || Key == TEXT("金属") || Key == TEXT("不锈钢"))
	{
		OutType = EPBRMaterialType::Metal;
		return true;
	}
	if (Key == TEXT("transparent") || Key == TEXT("translucent") || Key == TEXT("半透明") || Key == TEXT("透明"))
	{
		OutType = EPBRMaterialType::Transparent;
		return true;
	}
	if (Key == TEXT("glass") || Key == TEXT("window") || Key == TEXT("玻璃") || Key == TEXT("窗"))
	{
		OutType = EPBRMaterialType::Glass;
		return true;
	}
	if (Key == TEXT("water") || Key == TEXT("水") || Key == TEXT("水体") || Key == TEXT("水面"))
	{
		OutType = EPBRMaterialType::Water;
		return true;
	}
	if (Key == TEXT("emissive") || Key == TEXT("emission") || Key == TEXT("light") || Key == TEXT("自发光") || Key == TEXT("发光"))
	{
		OutType = EPBRMaterialType::Emissive;
		return true;
	}
	return false;
}

static void PBRMagicApplyTypeSuggestionDefaults(FPBRMagicAISuggestion& Suggestion);

static FPBRMagicAISuggestion PBRMagicBuildLocalAISuggestion(UMaterialInstanceConstant* Instance)
{
	FPBRMagicAISuggestion Suggestion;
	const FString Text = PBRMagicCollectMaterialAIText(Instance);

	if (PBRMagicTextHasAny(Text, { TEXT("glass"), TEXT("玻璃"), TEXT("mirror"), TEXT("透明玻璃") }))
	{
		Suggestion.MaterialType = EPBRMaterialType::Glass;
		Suggestion.Scalars.Add(FPBRMaterialParameters::Opacity, 0.35f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.02f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::RefractionAmount, 1.45f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::SpecularLevel, 0.5f);
		Suggestion.Summary = TEXT("AI 本地规则判断为玻璃，已建议透明、折射和低粗糙度。");
	}
	else if (PBRMagicTextHasAny(Text, { TEXT("water"), TEXT("pool"), TEXT("river"), TEXT("水"), TEXT("水面") }))
	{
		Suggestion.MaterialType = EPBRMaterialType::Water;
		Suggestion.Scalars.Add(FPBRMaterialParameters::Opacity, 0.65f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.05f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::RefractionAmount, 1.33f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::WaterRippleStrength, 0.8f);
		Suggestion.Summary = TEXT("AI 本地规则判断为水材质，已建议透明、折射和水波参数。");
	}
	else if (PBRMagicTextHasAny(Text, { TEXT("emissive"), TEXT("emit"), TEXT("light"), TEXT("led"), TEXT("neon"), TEXT("自发光"), TEXT("灯带") }))
	{
		Suggestion.MaterialType = EPBRMaterialType::Emissive;
		Suggestion.Scalars.Add(FPBRMaterialParameters::EmissiveIntensity, 2.0f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.35f);
		Suggestion.Colors.Add(FPBRMaterialParameters::EmissiveColor, FLinearColor::White);
		Suggestion.Summary = TEXT("AI 本地规则判断为自发光材质，已建议发光强度和颜色。");
	}
	else if (PBRMagicTextHasAny(Text, { TEXT("wood"), TEXT("timber"), TEXT("oak"), TEXT("木"), TEXT("木纹"), TEXT("木材") }))
	{
		Suggestion.MaterialType = EPBRMaterialType::Wood;
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.58f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::NormalStrength, 0.9f);
		Suggestion.Summary = TEXT("AI 本地规则判断为木材，已建议木纹常用粗糙度和法线强度。");
	}
	else if (PBRMagicTextHasAny(Text, { TEXT("stone"), TEXT("marble"), TEXT("granite"), TEXT("rock"), TEXT("石"), TEXT("大理石"), TEXT("岩石") }))
	{
		Suggestion.MaterialType = EPBRMaterialType::Stone;
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.48f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::NormalStrength, 1.18f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::HeightStrength, 0.08f);
		Suggestion.Summary = TEXT("AI 本地规则判断为石材，已建议粗糙度、法线和高度强度。");
	}
	else if (PBRMagicTextHasAny(Text, { TEXT("tile"), TEXT("ceramic"), TEXT("瓷砖"), TEXT("地砖"), TEXT("墙砖") }))
	{
		Suggestion.MaterialType = EPBRMaterialType::Tile;
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.38f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::SpecularLevel, 0.52f);
		Suggestion.Summary = TEXT("AI 本地规则判断为瓷砖，已建议较低粗糙度和较高高光。");
	}
	else if (PBRMagicTextHasAny(Text, { TEXT("fabric"), TEXT("cotton"), TEXT("twill"), TEXT("cloth"), TEXT("carpet"), TEXT("rug"), TEXT("textile"), TEXT("curtain"), TEXT("布"), TEXT("布料"), TEXT("织物"), TEXT("棉"), TEXT("地毯"), TEXT("窗帘") }))
	{
		Suggestion.MaterialType = EPBRMaterialType::Fabric;
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.86f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::SpecularLevel, 0.18f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::MetallicValue, 0.0f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::FabricFuzzStrength, 0.14f);
		Suggestion.Switches.Add(FPBRMaterialParameters::UseMetallicTexture, false);
		Suggestion.Switches.Add(FPBRMaterialParameters::UseEmissiveTexture, false);
		Suggestion.Summary = TEXT("AI 本地规则判断为布料，已建议高粗糙度和绒毛参数。");
	}
	else if (PBRMagicTextHasAny(Text, { TEXT("metal"), TEXT("steel"), TEXT("iron"), TEXT("aluminum"), TEXT("copper"), TEXT("brass"), TEXT("chrome"), TEXT("金属"), TEXT("不锈钢"), TEXT("铝"), TEXT("铜"), TEXT("黄铜") }))
	{
		Suggestion.MaterialType = EPBRMaterialType::Metal;
		Suggestion.Scalars.Add(FPBRMaterialParameters::MetallicValue, 1.0f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::MetallicMultiplier, 1.0f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.28f);
		Suggestion.Summary = TEXT("AI 本地规则判断为金属材质，已建议金属度和粗糙度。");
	}
	else
	{
		Suggestion.MaterialType = GuessMagicMaterialTypeFromMaterial(Instance);
		Suggestion.Scalars.Add(FPBRMaterialParameters::RoughnessValue, 0.5f);
		Suggestion.Scalars.Add(FPBRMaterialParameters::NormalStrength, 1.0f);
		Suggestion.Summary = TEXT("AI 本地规则未找到强特征，已按通用标准材质建议基础参数。");
	}

	PBRMagicApplyTypeSuggestionDefaults(Suggestion);
	return Suggestion;
}

static bool PBRMagicSerializeJsonObject(const TSharedRef<FJsonObject>& Object, FString& OutJson)
{
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Object, Writer);
}

static FString PBRMagicBuildRemoteAIUserText(UMaterialInstanceConstant* Instance, const FPBRMagicAISuggestion& LocalSuggestion)
{
	FString Text;
	Text += TEXT("Analyze this Unreal Engine material and return ONLY a compact JSON object.\n");
	Text += TEXT("You will receive base color/albedo style texture thumbnails when available. Use those color images as the primary evidence for material_type, then texture names and material instance names. Identify whether the surface is wood, stone, tile, fabric, leather, plastic, metal, transparent, glass, water, emissive, or standard PBR.\n");
	Text += TEXT("Important: names like Fabric, Cotton, Twill, Cloth, Carpet, Rug, Textile, 布料, 织物, 地毯 strongly indicate Fabric. Gray roughness/mask textures are not metal evidence. Only choose Metal when a base color image/name clearly shows metal, steel, iron, aluminum, copper, brass, or chrome.\n");
	Text += TEXT("Generic parameter names listed later, including metallic parameter names, are only writable controls and are NOT evidence for material classification.\n");
	Text += TEXT("Allowed material_type values: Standard, Wood, Stone, Tile, Fabric, Leather, Plastic, Metal, Transparent, Glass, Water, Emissive. Use Standard for grass, moss, plants, soil, sand, and ordinary non-metal PBR surfaces.\n");
	Text += TEXT("JSON schema: {\"material_type\":\"Standard\",\"surface_label\":\"grass/vegetation\",\"confidence\":0.0-1.0,\"evidence\":\"short reason\",\"scalar_suggestions\":[{\"parameter\":\"粗糙度数值\",\"value\":0.82,\"min\":0,\"max\":1,\"reason\":\"...\"}],\"switch_suggestions\":[{\"parameter\":\"使用基础色贴图\",\"value\":true,\"reason\":\"...\"}],\"color_suggestions\":[{\"parameter\":\"基础色调\",\"rgba\":[1,1,1,1],\"reason\":\"...\"}]}.\n");
	Text += TEXT("Return a complete practical parameter set for the chosen type, not only roughness/normal/height. Useful scalar parameters include: 基础色强度, 粗糙度数值, 粗糙度强度, 高光强度, 法线强度, 环境遮蔽强度, 金属度数值, 金属度强度, 各向异性, 织物绒毛强度, 清漆强度, 清漆粗糙度, 透明度, 折射强度, 自发光强度, 玻璃吸收强度, 透明菲涅尔强度, 菲涅尔基础反射, 菲涅尔指数, 毛玻璃强度, 水波强度, 水波缩放, 水流 U 速度, 水流 V 速度. Suggest at least 6 scalar parameters when the material type supports them.\n");
	Text += TEXT("Prefer physically plausible parameters. Do not mark vegetation, grass, soil, brick, stone, wood, plastic, fabric, leather, glass, or water as Metal unless the evidence is clearly metallic.\n");
	Text += FString::Printf(TEXT("Local rule guess: %s\n"), *GetMagicMaterialTypeLabel(LocalSuggestion.MaterialType).ToString());

	if (Instance)
	{
		Text += FString::Printf(TEXT("Material instance: %s\n"), *Instance->GetPathName());
		Text += TEXT("Current parent/template material is intentionally omitted because it may be the result of a previous conversion and is not classification evidence.\n");

		Text += TEXT("Textures:\n");
		TMap<FMaterialParameterInfo, FMaterialParameterMetadata> TextureParameters;
		Instance->GetAllParametersOfType(EMaterialParameterType::Texture, TextureParameters);
		TArray<FString> ParameterSignals;
		for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : TextureParameters)
		{
			UTexture* Texture = nullptr;
			if (!Instance->GetTextureParameterValue(Pair.Key, Texture) || !Texture)
			{
				continue;
			}
			if (PBRMagicShouldExposeTextureToAI(Instance, Pair.Key, Texture))
			{
				Text += FString::Printf(TEXT("- %s: %s (%s)\n"), *Pair.Key.Name.ToString(), *Texture->GetName(), *Texture->GetPathName());
			}
			if (PBRMagicShouldUseTextureForParameterSignal(Instance, Pair.Key, Texture))
			{
				const FString Role = PBRMagicGetTextureSignalRole(Pair.Key.Name, Texture->GetName());
				UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
				float AverageLuma = 0.0f;
				if (!Role.IsEmpty() && Texture2D && PBRMagicTryGetTextureAverageLuma(Texture2D, AverageLuma))
				{
					ParameterSignals.Add(FString::Printf(TEXT("- %s map %s average_luma=%.3f; use this only for parameter suggestions, not material_type."),
						*Role,
						*Texture->GetName(),
						AverageLuma));
				}
				else if (!Role.IsEmpty())
				{
					ParameterSignals.Add(FString::Printf(TEXT("- %s map %s is present; use this only for parameter suggestions, not material_type."),
						*Role,
						*Texture->GetName()));
				}
			}
		}
		if (ParameterSignals.Num() > 0)
		{
			Text += TEXT("Non-color texture signals for parameter estimation only. Do NOT classify material_type from these maps:\n");
			for (const FString& Signal : ParameterSignals)
			{
				Text += Signal;
				Text += TEXT("\n");
			}
		}
	}

	Text += TEXT("Useful scalar parameter names: 基础色强度, 法线强度, 粗糙度数值, 粗糙度强度, 高光强度, 金属度数值, 金属度强度, 透明度, 置换强度, 自发光强度, 织物绒毛强度, 折射强度, 毛玻璃强度, 水波强度, 水波缩放.\n");
	Text += TEXT("Useful switch parameter names: 使用基础色贴图, 使用法线贴图, 使用粗糙度贴图, 使用高光贴图, 使用金属度贴图, 使用环境遮蔽贴图, 使用高度贴图, 使用透明贴图, 使用自发光贴图.\n");
	Text += TEXT("Useful color parameter names: 基础色调, 自发光颜色, 织物绒毛颜色, 水颜色, 玻璃吸收颜色, 玻璃污渍颜色.\n");
	return Text;
}

static bool PBRMagicEncodeTextureDataUrl(UTexture2D* Texture, int32 MaxDimension, FString& OutDataUrl)
{
	OutDataUrl.Reset();
	if (!Texture)
	{
		return false;
	}

	FImage SourceImage;
	if (!FImageUtils::GetTexture2DSourceImage(Texture, SourceImage) || !SourceImage.IsImageInfoValid() || SourceImage.GetNumPixels() <= 0)
	{
		return false;
	}

	SourceImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	int32 Width = SourceImage.SizeX;
	int32 Height = SourceImage.SizeY;
	TArray<FColor> Pixels;
	const TArrayView64<const FColor> SourcePixels = SourceImage.AsBGRA8();
	Pixels.SetNumUninitialized(SourcePixels.Num());
	FMemory::Memcpy(Pixels.GetData(), SourcePixels.GetData(), SourcePixels.Num() * sizeof(FColor));

	const int32 MaxSide = FMath::Max(Width, Height);
	if (MaxSide > MaxDimension && MaxDimension > 0)
	{
		const float Scale = static_cast<float>(MaxDimension) / static_cast<float>(MaxSide);
		const int32 NewWidth = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Width) * Scale));
		const int32 NewHeight = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Height) * Scale));
		TArray<FColor> ResizedPixels;
		FImageUtils::ImageResize(Width, Height, Pixels, NewWidth, NewHeight, ResizedPixels, true);
		Pixels = MoveTemp(ResizedPixels);
		Width = NewWidth;
		Height = NewHeight;
	}

	TArray<uint8> RawBGRA;
	RawBGRA.SetNumUninitialized(Pixels.Num() * sizeof(FColor));
	FMemory::Memcpy(RawBGRA.GetData(), Pixels.GetData(), RawBGRA.Num());

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetRaw(RawBGRA.GetData(), RawBGRA.Num(), Width, Height, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	const TArray64<uint8> Compressed = ImageWrapper->GetCompressed(82);
	if (Compressed.Num() == 0)
	{
		return false;
	}

	OutDataUrl = FString::Printf(TEXT("data:image/jpeg;base64,%s"), *FBase64::Encode(Compressed.GetData(), static_cast<uint32>(Compressed.Num())));
	return true;
}

static TArray<FPBRMagicAIImagePayload> PBRMagicBuildRemoteAIImagePayloads(UMaterialInstanceConstant* Instance)
{
	TArray<FPBRMagicAIImagePayload> Payloads;
	if (!Instance)
	{
		return Payloads;
	}

	TSet<FString> AddedTextures;
	struct FTextureCandidate
	{
		FMaterialParameterInfo Info;
		UTexture2D* Texture = nullptr;
		int32 Priority = 100;
	};
	TArray<FTextureCandidate> Candidates;
	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> TextureParameters;
	Instance->GetAllParametersOfType(EMaterialParameterType::Texture, TextureParameters);
	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : TextureParameters)
	{
		UTexture* Texture = nullptr;
		if (!Instance->GetTextureParameterValue(Pair.Key, Texture) || !Texture)
		{
			continue;
		}

		UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		if (!Texture2D)
		{
			continue;
		}
		if (!PBRMagicShouldExposeTextureToAI(Instance, Pair.Key, Texture2D))
		{
			continue;
		}
		const int32 Priority = PBRMagicGetAITexturePriority(Pair.Key.Name, Texture2D->GetName());
		if (Priority >= 100)
		{
			continue;
		}
		Candidates.Add({ Pair.Key, Texture2D, Priority });
	}

	Candidates.Sort([](const FTextureCandidate& A, const FTextureCandidate& B)
	{
		if (A.Priority != B.Priority)
		{
			return A.Priority < B.Priority;
		}
		return A.Texture && B.Texture ? A.Texture->GetName() < B.Texture->GetName() : A.Texture != nullptr;
	});

	for (const FTextureCandidate& Candidate : Candidates)
	{
		if (Payloads.Num() >= 3)
		{
			break;
		}
		UTexture2D* Texture2D = Candidate.Texture;
		if (!Texture2D)
		{
			continue;
		}
		const FString TexturePath = Texture2D->GetPathName();
		if (AddedTextures.Contains(TexturePath))
		{
			continue;
		}

		FString DataUrl;
		if (!PBRMagicEncodeTextureDataUrl(Texture2D, 768, DataUrl))
		{
			continue;
		}

		FPBRMagicAIImagePayload Payload;
		Payload.Label = Candidate.Info.Name.ToString();
		Payload.TextureName = Texture2D->GetName();
		Payload.TexturePath = TexturePath;
		Payload.DataUrl = MoveTemp(DataUrl);
		Payloads.Add(MoveTemp(Payload));
		AddedTextures.Add(TexturePath);
	}

	return Payloads;
}

static bool PBRMagicBuildRemoteAIRequestBody(const FPBRMagicAIProviderSettings& Settings, const FString& UserText, const TArray<FPBRMagicAIImagePayload>& Images, FString& OutBody)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Settings.Model);
	Root->SetNumberField(TEXT("temperature"), 0.1);
	Root->SetNumberField(TEXT("max_tokens"), 900);

	TSharedRef<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
	Root->SetObjectField(TEXT("response_format"), ResponseFormat);

	const FString Model = Settings.Model.TrimStartAndEnd().ToLower();
	const FString Endpoint = Settings.EndpointUrl.TrimStartAndEnd().ToLower();
	if (Model.StartsWith(TEXT("glm")) || Endpoint.Contains(TEXT("bigmodel.cn")) || Endpoint.Contains(TEXT("z.ai")))
	{
		TSharedRef<FJsonObject> Thinking = MakeShared<FJsonObject>();
		Thinking->SetStringField(TEXT("type"), TEXT("disabled"));
		Root->SetObjectField(TEXT("thinking"), Thinking);
	}

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
	SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessage->SetStringField(TEXT("content"), TEXT("You are a senior real-time rendering material classifier. Return valid JSON only. Never invent unsupported material_type values."));
	Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

	TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	if (Images.Num() == 0)
	{
		UserMessage->SetStringField(TEXT("content"), UserText);
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> ContentParts;
		TSharedRef<FJsonObject> TextPart = MakeShared<FJsonObject>();
		TextPart->SetStringField(TEXT("type"), TEXT("text"));
		TextPart->SetStringField(TEXT("text"), UserText);
		ContentParts.Add(MakeShared<FJsonValueObject>(TextPart));

		for (const FPBRMagicAIImagePayload& Image : Images)
		{
			TSharedRef<FJsonObject> ImagePart = MakeShared<FJsonObject>();
			ImagePart->SetStringField(TEXT("type"), TEXT("image_url"));
			TSharedRef<FJsonObject> ImageUrl = MakeShared<FJsonObject>();
			ImageUrl->SetStringField(TEXT("url"), Image.DataUrl);
			ImageUrl->SetStringField(TEXT("detail"), TEXT("high"));
			ImagePart->SetObjectField(TEXT("image_url"), ImageUrl);
			ContentParts.Add(MakeShared<FJsonValueObject>(ImagePart));
		}
		UserMessage->SetArrayField(TEXT("content"), ContentParts);
	}
	Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
	Root->SetArrayField(TEXT("messages"), Messages);

	return PBRMagicSerializeJsonObject(Root, OutBody);
}

static bool PBRMagicPostRemoteAIRequest(const FPBRMagicAIProviderSettings& Settings, const FString& RequestBody, FString& OutResponse, FString& OutError)
{
	OutResponse.Reset();
	OutError.Reset();
	if (Settings.EndpointUrl.IsEmpty())
	{
		OutError = TEXT("远端 AI 未配置接口地址");
		return false;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(PBRMagicNormalizeAIEndpointUrl(Settings.EndpointUrl));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	const FString ApiKey = PBRMagicResolveAIApiKey(Settings);
	if (!ApiKey.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	}
	Request->SetContentAsString(RequestBody);

	bool bCompleted = false;
	bool bSucceeded = false;
	int32 ResponseCode = 0;
	Request->OnProcessRequestComplete().BindLambda([&bCompleted, &bSucceeded, &ResponseCode, &OutResponse, &OutError](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		bCompleted = true;
		bSucceeded = bWasSuccessful && Response.IsValid();
		ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		OutResponse = Response.IsValid() ? Response->GetContentAsString() : FString();
		if (!bSucceeded || ResponseCode < 200 || ResponseCode >= 300)
		{
			OutError = ResponseCode == 0
				? TEXT("远端 AI 请求失败：网络不可用或无法连接接口")
				: FString::Printf(TEXT("远端 AI 请求失败：HTTP %d"), ResponseCode);
			if (!OutResponse.IsEmpty())
			{
				OutError += FString::Printf(TEXT("，响应：%s"), *OutResponse.Left(300));
			}
		}
	});

	if (!Request->ProcessRequest())
	{
		OutError = TEXT("远端 AI 请求启动失败");
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();
	const double TimeoutSeconds = FMath::Clamp(static_cast<double>(Settings.RequestTimeoutSeconds), 3.0, 120.0);
	while (!bCompleted)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.05f);
		if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
		{
			Request->CancelRequest();
			OutError = FString::Printf(TEXT("远端 AI 请求超时：%.0f 秒"), TimeoutSeconds);
			return false;
		}
		FPlatformProcess::Sleep(0.01f);
	}

	if (!bSucceeded || ResponseCode < 200 || ResponseCode >= 300)
	{
		if (OutError.IsEmpty())
		{
			OutError = ResponseCode == 0
				? TEXT("远端 AI 请求失败：网络不可用或无法连接接口")
				: FString::Printf(TEXT("远端 AI 请求失败：HTTP %d"), ResponseCode);
		}
		return false;
	}

	return true;
}

static FString PBRMagicNormalizeAIModelsUrl(const FString& EndpointUrl)
{
	FString BaseUrl = PBRMagicNormalizeAIEndpointUrl(EndpointUrl);
	if (BaseUrl.EndsWith(TEXT("/chat/completions"), ESearchCase::IgnoreCase))
	{
		BaseUrl.LeftChopInline(17);
	}
	while (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1);
	}
	return BaseUrl + TEXT("/models");
}

static bool PBRMagicGetRemoteAIModels(const FPBRMagicAIProviderSettings& Settings, TArray<FString>& OutModels, FString& OutStatus)
{
	OutModels.Reset();
	OutStatus.Reset();
	if (Settings.EndpointUrl.IsEmpty())
	{
		OutStatus = TEXT("请先填写 API 接口地址");
		return false;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(PBRMagicNormalizeAIModelsUrl(Settings.EndpointUrl));
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	const FString ApiKey = PBRMagicResolveAIApiKey(Settings);
	if (!ApiKey.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	}

	bool bCompleted = false;
	bool bSucceeded = false;
	int32 ResponseCode = 0;
	FString ResponseBody;
	FString Error;
	Request->OnProcessRequestComplete().BindLambda([&bCompleted, &bSucceeded, &ResponseCode, &ResponseBody, &Error](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		bCompleted = true;
		bSucceeded = bWasSuccessful && Response.IsValid();
		ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();
		if (!bSucceeded || ResponseCode < 200 || ResponseCode >= 300)
		{
			Error = ResponseCode == 0
				? TEXT("模型列表请求失败：无法连接接口")
				: FString::Printf(TEXT("模型列表请求失败：HTTP %d"), ResponseCode);
			if (!ResponseBody.IsEmpty())
			{
				Error += FString::Printf(TEXT("，响应：%s"), *ResponseBody.Left(300));
			}
		}
	});

	if (!Request->ProcessRequest())
	{
		OutStatus = TEXT("模型列表请求启动失败");
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();
	while (!bCompleted)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.05f);
		if (FPlatformTime::Seconds() - StartTime > 20.0)
		{
			Request->CancelRequest();
			OutStatus = TEXT("模型列表请求超时");
			return false;
		}
		FPlatformProcess::Sleep(0.01f);
	}

	if (!bSucceeded || ResponseCode < 200 || ResponseCode >= 300)
	{
		OutStatus = Error.IsEmpty() ? TEXT("模型列表请求失败") : Error;
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutStatus = TEXT("模型列表响应不是合法 JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
	if (Root->TryGetArrayField(TEXT("data"), Data) && Data)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Data)
		{
			if (!Value.IsValid())
			{
				continue;
			}
			if (Value->Type == EJson::String)
			{
				OutModels.Add(Value->AsString());
				continue;
			}
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const FString ModelId = PBRMagicJsonStringField(Object, TEXT("id"), PBRMagicJsonStringField(Object, TEXT("name")));
			if (!ModelId.IsEmpty())
			{
				OutModels.Add(ModelId);
			}
		}
	}

	OutModels.Sort();
	if (OutModels.Num() == 0)
	{
		OutStatus = TEXT("接口返回成功，但没有找到模型 id");
		return false;
	}

	OutStatus = FString::Printf(TEXT("已获取 %d 个模型"), OutModels.Num());
	return true;
}

static bool PBRMagicExtractChatCompletionContent(const FString& ResponseBody, FString& OutContent, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("远端 AI 响应不是合法 JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (Root->TryGetArrayField(TEXT("choices"), Choices) && Choices && Choices->Num() > 0)
	{
		const TSharedPtr<FJsonObject> ChoiceObject = (*Choices)[0]->AsObject();
		if (ChoiceObject.IsValid())
		{
			const TSharedPtr<FJsonObject>* MessageObject = nullptr;
			if (ChoiceObject->TryGetObjectField(TEXT("message"), MessageObject) && MessageObject && MessageObject->IsValid())
			{
				if ((*MessageObject)->TryGetStringField(TEXT("content"), OutContent) && !OutContent.IsEmpty())
				{
					return true;
				}
			}
		}
	}

	if (Root->TryGetStringField(TEXT("output_text"), OutContent) && !OutContent.IsEmpty())
	{
		return true;
	}

	OutError = TEXT("远端 AI 响应缺少 message.content");
	return false;
}

static bool PBRMagicValidateRemoteAIProvider(const FPBRMagicAIProviderSettings& Settings, FString& OutStatus)
{
	if (!Settings.ShouldUseRemoteModel())
	{
		OutStatus = TEXT("请选择 OpenAICompatible，并填写接口地址和模型名");
		return false;
	}

	FString RequestBody;
	if (!PBRMagicBuildRemoteAIRequestBody(Settings, TEXT("Return ONLY this JSON object with no markdown: {\"ok\":true,\"message\":\"ready\"}."), TArray<FPBRMagicAIImagePayload>(), RequestBody))
	{
		OutStatus = TEXT("验证请求体生成失败");
		return false;
	}

	FString ResponseBody;
	FString Error;
	if (!PBRMagicPostRemoteAIRequest(Settings, RequestBody, ResponseBody, Error))
	{
		OutStatus = Error;
		return false;
	}

	FString Content;
	if (!PBRMagicExtractChatCompletionContent(ResponseBody, Content, Error))
	{
		OutStatus = Error;
		return false;
	}

	OutStatus = FString::Printf(TEXT("AI 接口验证成功：%s"), *Settings.Model);
	return true;
}

static bool PBRMagicParseJsonObjectFromText(const FString& Text, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
{
	FString JsonText = Text.TrimStartAndEnd();
	const int32 FirstBrace = JsonText.Find(TEXT("{"));
	const int32 LastBrace = JsonText.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (FirstBrace != INDEX_NONE && LastBrace != INDEX_NONE && LastBrace > FirstBrace)
	{
		JsonText = JsonText.Mid(FirstBrace, LastBrace - FirstBrace + 1);
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
	{
		OutError = TEXT("远端 AI 内容不是合法 JSON 对象");
		return false;
	}
	return true;
}

static bool PBRMagicParseRemoteColor(const TSharedPtr<FJsonObject>& Object, FLinearColor& OutColor)
{
	const TArray<TSharedPtr<FJsonValue>>* RGBA = nullptr;
	if (Object.IsValid() && Object->TryGetArrayField(TEXT("rgba"), RGBA) && RGBA && RGBA->Num() >= 3)
	{
		const float R = FMath::Clamp(static_cast<float>((*RGBA)[0]->AsNumber()), 0.0f, 1.0f);
		const float G = FMath::Clamp(static_cast<float>((*RGBA)[1]->AsNumber()), 0.0f, 1.0f);
		const float B = FMath::Clamp(static_cast<float>((*RGBA)[2]->AsNumber()), 0.0f, 1.0f);
		const float A = RGBA->Num() > 3 ? FMath::Clamp(static_cast<float>((*RGBA)[3]->AsNumber()), 0.0f, 1.0f) : 1.0f;
		OutColor = FLinearColor(R, G, B, A);
		return true;
	}
	if (Object.IsValid())
	{
		OutColor = FLinearColor(
			FMath::Clamp(PBRMagicJsonNumberField(Object, TEXT("r"), 1.0f), 0.0f, 1.0f),
			FMath::Clamp(PBRMagicJsonNumberField(Object, TEXT("g"), 1.0f), 0.0f, 1.0f),
			FMath::Clamp(PBRMagicJsonNumberField(Object, TEXT("b"), 1.0f), 0.0f, 1.0f),
			FMath::Clamp(PBRMagicJsonNumberField(Object, TEXT("a"), 1.0f), 0.0f, 1.0f));
		return true;
	}
	return false;
}

static void PBRMagicAddAIScalarDefault(FPBRMagicAISuggestion& Suggestion, const FName& ParameterName, float Value)
{
	if (!Suggestion.Scalars.Contains(ParameterName))
	{
		Suggestion.Scalars.Add(ParameterName, Value);
	}
}

static void PBRMagicAddAISwitchDefault(FPBRMagicAISuggestion& Suggestion, const FName& ParameterName, bool bValue)
{
	if (!Suggestion.Switches.Contains(ParameterName))
	{
		Suggestion.Switches.Add(ParameterName, bValue);
	}
}

static void PBRMagicAddAIColorDefault(FPBRMagicAISuggestion& Suggestion, const FName& ParameterName, const FLinearColor& Value)
{
	if (!Suggestion.Colors.Contains(ParameterName))
	{
		Suggestion.Colors.Add(ParameterName, Value);
	}
}

static void PBRMagicApplyTypeSuggestionDefaults(FPBRMagicAISuggestion& Suggestion)
{
	PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::BaseColorIntensity, 1.0f);
	PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessMultiplier, 1.0f);
	PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::NormalStrength, 1.0f);
	PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::AOMultiplier, 1.0f);

	if (Suggestion.MaterialType != EPBRMaterialType::Metal)
	{
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::MetallicValue, 0.0f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		PBRMagicAddAISwitchDefault(Suggestion, FPBRMaterialParameters::UseMetallicTexture, false);
	}
	if (Suggestion.MaterialType != EPBRMaterialType::Emissive)
	{
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::EmissiveIntensity, 0.0f);
		PBRMagicAddAISwitchDefault(Suggestion, FPBRMaterialParameters::UseEmissiveTexture, false);
	}

	switch (Suggestion.MaterialType)
	{
	case EPBRMaterialType::Wood:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.58f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.35f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::NormalStrength, 0.9f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::HeightStrength, 0.04f);
		break;
	case EPBRMaterialType::Stone:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.52f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.38f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::NormalStrength, 1.18f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::HeightStrength, 0.08f);
		break;
	case EPBRMaterialType::Tile:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.38f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.52f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::NormalStrength, 0.9f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::HeightStrength, 0.02f);
		break;
	case EPBRMaterialType::Fabric:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.86f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.18f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::NormalStrength, 0.85f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::FabricFuzzStrength, 0.14f);
		PBRMagicAddAIColorDefault(Suggestion, FPBRMaterialParameters::FabricFuzzColor, FLinearColor(0.6f, 0.58f, 0.52f, 1.0f));
		break;
	case EPBRMaterialType::Leather:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.42f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.48f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::ClearCoat, 0.35f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::ClearCoatRoughness, 0.22f);
		break;
	case EPBRMaterialType::Plastic:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.46f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.45f);
		break;
	case EPBRMaterialType::Metal:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::MetallicValue, 1.0f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::MetallicMultiplier, 1.0f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.28f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.5f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::Anisotropy, 0.12f);
		break;
	case EPBRMaterialType::Transparent:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::Opacity, 0.45f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.12f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.5f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RefractionAmount, 1.35f);
		break;
	case EPBRMaterialType::Glass:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::Opacity, 0.35f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.02f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.5f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RefractionAmount, 1.45f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::GlassOpacityFresnelStrength, 0.35f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::GlassFresnelBaseReflection, 0.02f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::GlassFresnelExp, 5.0f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::GlassAbsorptionStrength, 0.15f);
		PBRMagicAddAIColorDefault(Suggestion, FPBRMaterialParameters::GlassAbsorptionColor, FLinearColor(0.78f, 0.92f, 1.0f, 1.0f));
		break;
	case EPBRMaterialType::Water:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::Opacity, 0.65f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.05f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RefractionAmount, 1.33f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::WaterRippleStrength, 0.8f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::WaterRippleScale, 18.0f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::WaterFlowSpeedU, 0.18f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::WaterFlowSpeedV, 0.09f);
		PBRMagicAddAIColorDefault(Suggestion, FPBRMaterialParameters::WaterColor, FLinearColor(0.12f, 0.42f, 0.72f, 1.0f));
		break;
	case EPBRMaterialType::Emissive:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.35f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.25f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::EmissiveIntensity, 2.0f);
		PBRMagicAddAIColorDefault(Suggestion, FPBRMaterialParameters::EmissiveColor, FLinearColor::White);
		break;
	default:
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::RoughnessValue, 0.5f);
		PBRMagicAddAIScalarDefault(Suggestion, FPBRMaterialParameters::SpecularLevel, 0.45f);
		break;
	}
}

static bool PBRMagicLooksLikeFabricText(const FString& Text)
{
	return PBRMagicTextHasAny(Text, {
		TEXT("fabric"), TEXT("cotton"), TEXT("twill"), TEXT("cloth"), TEXT("carpet"), TEXT("rug"), TEXT("textile"),
		TEXT("布"), TEXT("布料"), TEXT("织物"), TEXT("棉"), TEXT("地毯")
	});
}

static bool PBRMagicLooksLikeMetalText(const FString& Text)
{
	return PBRMagicTextHasAny(Text, {
		TEXT("metal"), TEXT("steel"), TEXT("iron"), TEXT("aluminum"), TEXT("aluminium"), TEXT("copper"), TEXT("brass"), TEXT("chrome"), TEXT("stainless"),
		TEXT("金属"), TEXT("钢"), TEXT("铁"), TEXT("铝"), TEXT("铜"), TEXT("黄铜"), TEXT("铬"), TEXT("不锈钢")
	});
}

static bool PBRMagicTryRemoteAISuggestion(UMaterialInstanceConstant* Instance, const FPBRMagicAISuggestion& LocalSuggestion, FPBRMagicAISuggestion& OutSuggestion, FString& OutStatus)
{
	const FPBRMagicAIProviderSettings Settings = PBRMagicLoadAIProviderSettings();
	if (!Settings.ShouldUseRemoteModel())
	{
		OutStatus = TEXT("使用本地规则；可在 AI 设置中选择 OpenAICompatible。");
		return false;
	}

	const TArray<FPBRMagicAIImagePayload> Images = PBRMagicBuildRemoteAIImagePayloads(Instance);
	FString RequestBody;
	if (!PBRMagicBuildRemoteAIRequestBody(Settings, PBRMagicBuildRemoteAIUserText(Instance, LocalSuggestion), Images, RequestBody))
	{
		OutStatus = TEXT("远端 AI 请求体生成失败，已使用本地规则。");
		return false;
	}

	FString ResponseBody;
	FString Error;
	if (!PBRMagicPostRemoteAIRequest(Settings, RequestBody, ResponseBody, Error))
	{
		OutStatus = FString::Printf(TEXT("%s，已使用本地规则。"), *Error);
		return false;
	}

	FString Content;
	if (!PBRMagicExtractChatCompletionContent(ResponseBody, Content, Error))
	{
		OutStatus = FString::Printf(TEXT("%s，已使用本地规则。"), *Error);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	if (!PBRMagicParseJsonObjectFromText(Content, Root, Error))
	{
		OutStatus = FString::Printf(TEXT("%s，已使用本地规则。"), *Error);
		return false;
	}

	const FString TypeText = PBRMagicJsonStringField(Root, TEXT("material_type"), PBRMagicJsonStringField(Root, TEXT("type")));
	EPBRMaterialType RemoteType = EPBRMaterialType::Standard;
	if (!PBRMagicParseMaterialType(TypeText, RemoteType))
	{
		OutStatus = FString::Printf(TEXT("远端 AI 返回未知材质类型：%s，已使用本地规则。"), *TypeText);
		return false;
	}
	const FString ClassificationText = PBRMagicCollectMaterialAIText(Instance);
	const bool bCorrectedFabricMetal = RemoteType == EPBRMaterialType::Metal && PBRMagicLooksLikeFabricText(ClassificationText);
	const bool bRejectedWeakMetal = RemoteType == EPBRMaterialType::Metal && LocalSuggestion.MaterialType != EPBRMaterialType::Metal && !PBRMagicLooksLikeMetalText(ClassificationText);
	if (bCorrectedFabricMetal)
	{
		RemoteType = EPBRMaterialType::Fabric;
	}
	else if (bRejectedWeakMetal)
	{
		RemoteType = LocalSuggestion.MaterialType;
	}

	OutSuggestion = FPBRMagicAISuggestion();
	OutSuggestion.MaterialType = RemoteType;
	PBRMagicApplyTypeSuggestionDefaults(OutSuggestion);
	OutSuggestion.Summary = FString::Printf(TEXT("远端 AI 建议为%s：%s"),
		*GetMagicMaterialTypeLabel(RemoteType).ToString(),
		*PBRMagicJsonStringField(Root, TEXT("evidence"), TEXT("已按接口返回参数应用")));
	if (bCorrectedFabricMetal)
	{
		OutSuggestion.Summary += TEXT(" 已根据贴图/材质名称中的 Fabric/Cotton/地毯特征，将金属误判纠正为布料。");
	}
	else if (bRejectedWeakMetal)
	{
		OutSuggestion.Summary += TEXT(" 远端返回金属，但当前材质名和关键贴图没有明确金属证据，已回退到本地判断。");
	}

	const TArray<TSharedPtr<FJsonValue>>* Scalars = nullptr;
	if (Root->TryGetArrayField(TEXT("scalar_suggestions"), Scalars) && Scalars)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Scalars)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			const FName ParameterName = PBRMagicResolveScalarParameterName(PBRMagicJsonStringField(Object, TEXT("parameter"), PBRMagicJsonStringField(Object, TEXT("name"))));
			if (!ParameterName.IsNone())
			{
				if (RemoteType != EPBRMaterialType::Metal &&
					(ParameterName == FPBRMaterialParameters::MetallicValue || ParameterName == FPBRMaterialParameters::MetallicMultiplier))
				{
					continue;
				}
				OutSuggestion.Scalars.Add(ParameterName, PBRMagicJsonNumberField(Object, TEXT("value"), 0.0f));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Switches = nullptr;
	if (Root->TryGetArrayField(TEXT("switch_suggestions"), Switches) && Switches)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Switches)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			const FName ParameterName = PBRMagicResolveSwitchParameterName(PBRMagicJsonStringField(Object, TEXT("parameter"), PBRMagicJsonStringField(Object, TEXT("name"))));
			if (!ParameterName.IsNone())
			{
				if (RemoteType != EPBRMaterialType::Metal && ParameterName == FPBRMaterialParameters::UseMetallicTexture)
				{
					continue;
				}
				OutSuggestion.Switches.Add(ParameterName, PBRMagicJsonBoolField(Object, TEXT("value"), false));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Colors = nullptr;
	if (Root->TryGetArrayField(TEXT("color_suggestions"), Colors) && Colors)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Colors)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			const FName ParameterName = PBRMagicResolveColorParameterName(PBRMagicJsonStringField(Object, TEXT("parameter"), PBRMagicJsonStringField(Object, TEXT("name"))));
			FLinearColor Color;
			if (!ParameterName.IsNone() && PBRMagicParseRemoteColor(Object, Color))
			{
				OutSuggestion.Colors.Add(ParameterName, Color);
			}
		}
	}

	if (RemoteType != EPBRMaterialType::Metal)
	{
		OutSuggestion.Scalars.Add(FPBRMaterialParameters::MetallicValue, 0.0f);
		OutSuggestion.Scalars.Add(FPBRMaterialParameters::MetallicMultiplier, 0.0f);
		OutSuggestion.Switches.Add(FPBRMaterialParameters::UseMetallicTexture, false);
	}

	FString SentTextureNames;
	for (const FPBRMagicAIImagePayload& Image : Images)
	{
		if (!SentTextureNames.IsEmpty())
		{
			SentTextureNames += TEXT(", ");
		}
		SentTextureNames += Image.TextureName;
	}
	OutStatus = Images.Num() > 0
		? FString::Printf(TEXT("远端 AI 识图完成：%s / %s，发送 %d 张颜色贴图：%s"), *Settings.Provider, *Settings.Model, Images.Num(), *SentTextureNames)
		: FString::Printf(TEXT("远端 AI 文本识别完成：%s / %s，未找到可发送颜色贴图"), *Settings.Provider, *Settings.Model);
	return true;
}

static void PBRMagicGetAIScalarRange(const FName& ParameterName, float& MinValue, float& MaxValue)
{
	MinValue = 0.0f;
	MaxValue = 1.0f;

	if (ParameterName == FPBRMaterialParameters::NormalStrength ||
		ParameterName == FPBRMaterialParameters::BaseColorIntensity)
	{
		MaxValue = 5.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::RoughnessMultiplier ||
		ParameterName == FPBRMaterialParameters::AOMultiplier)
	{
		MaxValue = 4.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::EmissiveIntensity)
	{
		MaxValue = 100.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::EmissiveTemperatureKelvin)
	{
		MinValue = 1000.0f;
		MaxValue = 20000.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::RefractionAmount)
	{
		MinValue = 1.0f;
		MaxValue = 2.4f;
	}
	else if (ParameterName == FPBRMaterialParameters::FabricFuzzStrength ||
		ParameterName == FPBRMaterialParameters::WaterRippleStrength ||
		ParameterName == FPBRMaterialParameters::MetallicMultiplier ||
		ParameterName == FPBRMaterialParameters::GlassAbsorptionStrength ||
		ParameterName == FPBRMaterialParameters::GlassEdgeTintStrength ||
		ParameterName == FPBRMaterialParameters::GlassDirtIntensity ||
		ParameterName == FPBRMaterialParameters::GlassDistortionIntensity ||
		ParameterName == FPBRMaterialParameters::GlassDistortionIORIntensity)
	{
		MaxValue = 2.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::WaterRippleScale ||
		ParameterName == FPBRMaterialParameters::GlassCausticsScale)
	{
		MinValue = 0.01f;
		MaxValue = 100.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::GlassCausticsIntensity)
	{
		MaxValue = 10.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::WaterFlowSpeedU ||
		ParameterName == FPBRMaterialParameters::WaterFlowSpeedV ||
		ParameterName == FPBRMaterialParameters::GlassCausticsSpeed)
	{
		MinValue = -5.0f;
		MaxValue = 5.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::Anisotropy)
	{
		MinValue = -1.0f;
		MaxValue = 1.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::FlakeScale ||
		ParameterName == FPBRMaterialParameters::UVUTiling ||
		ParameterName == FPBRMaterialParameters::UVVTiling)
	{
		MinValue = 0.01f;
		MaxValue = 100.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::FlakeIntensity)
	{
		MaxValue = 2.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::UVUOffset ||
		ParameterName == FPBRMaterialParameters::UVVOffset)
	{
		MinValue = -10.0f;
		MaxValue = 10.0f;
	}
	else if (ParameterName == FPBRMaterialParameters::UVRotationDegrees)
	{
		MinValue = -360.0f;
		MaxValue = 360.0f;
	}
}

static FString PBRMagicBuildAISuggestionPreviewText(const FPBRMagicAISuggestion& Suggestion)
{
	FString Text;
	Text += FString::Printf(TEXT("材质类型：%s\n"), *GetMagicMaterialTypeLabel(Suggestion.MaterialType).ToString());
	if (!Suggestion.Summary.IsEmpty())
	{
		Text += FString::Printf(TEXT("说明：%s\n"), *Suggestion.Summary);
	}
	if (Suggestion.Scalars.Num() > 0)
	{
		Text += TEXT("\n数值参数：\n");
		for (const TPair<FName, float>& Pair : Suggestion.Scalars)
		{
			Text += FString::Printf(TEXT("- %s = %.3f\n"), *Pair.Key.ToString(), Pair.Value);
		}
	}
	if (Suggestion.Switches.Num() > 0)
	{
		Text += TEXT("\n开关参数：\n");
		for (const TPair<FName, bool>& Pair : Suggestion.Switches)
		{
			Text += FString::Printf(TEXT("- %s = %s\n"), *Pair.Key.ToString(), Pair.Value ? TEXT("启用") : TEXT("关闭"));
		}
	}
	if (Suggestion.Colors.Num() > 0)
	{
		Text += TEXT("\n颜色参数：\n");
		for (const TPair<FName, FLinearColor>& Pair : Suggestion.Colors)
		{
			Text += FString::Printf(TEXT("- %s = R %.2f / G %.2f / B %.2f / A %.2f\n"), *Pair.Key.ToString(), Pair.Value.R, Pair.Value.G, Pair.Value.B, Pair.Value.A);
		}
	}
	return Text;
}

static FString ActorLabel(AActor* Actor);
static FString BuildNameCheckKeyFromLabel(const FString& Label);

static FLinearColor PBRLinearColorFromHex(const TCHAR* Hex)
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(FString(Hex)));
}

struct FPBRMagicTopTab
{
	EPBRMagicOutlinerCategory Category;
	FText Label;
	FName Icon;
};

struct FPBRMagicTheme
{
	FName Id;
	FText Label;
	FLinearColor Background;
	FLinearColor Panel;
	FLinearColor PanelRaised;
	FLinearColor TableRow;
	FLinearColor TableRowHover;
	FLinearColor Selection;
	FLinearColor SelectionText;
	FLinearColor Primary;
	FLinearColor PrimarySoft;
	FLinearColor Success;
	FLinearColor Warning;
	FLinearColor Danger;
	FLinearColor Text;
	FLinearColor TextMuted;
	FLinearColor Border;
	FLinearColor DropZone;
	FLinearColor ThumbnailBorder;
};

static const TArray<FPBRMagicTheme>& GetMagicThemes()
{
	static const TArray<FPBRMagicTheme> Themes = {
		{
			TEXT("graphite_cyan"), FText::FromString(TEXT("Graphite Cyan")),
			PBRLinearColorFromHex(TEXT("#11161c")), PBRLinearColorFromHex(TEXT("#18212a")), PBRLinearColorFromHex(TEXT("#202b35")),
			PBRLinearColorFromHex(TEXT("#151c23")), PBRLinearColorFromHex(TEXT("#1d2a34")),
			PBRLinearColorFromHex(TEXT("#f2a93b")), PBRLinearColorFromHex(TEXT("#101418")),
			PBRLinearColorFromHex(TEXT("#38c5d8")), PBRLinearColorFromHex(TEXT("#1c7180")),
			PBRLinearColorFromHex(TEXT("#55c58a")), PBRLinearColorFromHex(TEXT("#f2a93b")), PBRLinearColorFromHex(TEXT("#ff6b6b")),
			PBRLinearColorFromHex(TEXT("#edf4f8")), PBRLinearColorFromHex(TEXT("#96a8b5")),
			PBRLinearColorFromHex(TEXT("#2b3a45")), PBRLinearColorFromHex(TEXT("#173946")), PBRLinearColorFromHex(TEXT("#38c5d8"))
		},
		{
			TEXT("midnight_amber"), FText::FromString(TEXT("Midnight Amber")),
			PBRLinearColorFromHex(TEXT("#101217")), PBRLinearColorFromHex(TEXT("#1a1d25")), PBRLinearColorFromHex(TEXT("#242832")),
			PBRLinearColorFromHex(TEXT("#151820")), PBRLinearColorFromHex(TEXT("#232730")),
			PBRLinearColorFromHex(TEXT("#ffb347")), PBRLinearColorFromHex(TEXT("#111217")),
			PBRLinearColorFromHex(TEXT("#ffb347")), PBRLinearColorFromHex(TEXT("#8c5d21")),
			PBRLinearColorFromHex(TEXT("#6ccf91")), PBRLinearColorFromHex(TEXT("#ffcc66")), PBRLinearColorFromHex(TEXT("#ff6d5f")),
			PBRLinearColorFromHex(TEXT("#f3f0e8")), PBRLinearColorFromHex(TEXT("#a8a091")),
			PBRLinearColorFromHex(TEXT("#393226")), PBRLinearColorFromHex(TEXT("#342718")), PBRLinearColorFromHex(TEXT("#ffb347"))
		},
		{
			TEXT("slate_green"), FText::FromString(TEXT("Slate Green")),
			PBRLinearColorFromHex(TEXT("#111817")), PBRLinearColorFromHex(TEXT("#192321")), PBRLinearColorFromHex(TEXT("#22302d")),
			PBRLinearColorFromHex(TEXT("#151e1c")), PBRLinearColorFromHex(TEXT("#20312d")),
			PBRLinearColorFromHex(TEXT("#d7b95a")), PBRLinearColorFromHex(TEXT("#101512")),
			PBRLinearColorFromHex(TEXT("#62c99a")), PBRLinearColorFromHex(TEXT("#2b7557")),
			PBRLinearColorFromHex(TEXT("#62c99a")), PBRLinearColorFromHex(TEXT("#d7b95a")), PBRLinearColorFromHex(TEXT("#f06c64")),
			PBRLinearColorFromHex(TEXT("#edf5ef")), PBRLinearColorFromHex(TEXT("#9aaca3")),
			PBRLinearColorFromHex(TEXT("#2d4039")), PBRLinearColorFromHex(TEXT("#18372b")), PBRLinearColorFromHex(TEXT("#62c99a"))
		},
		{
			TEXT("light_frost"), FText::FromString(TEXT("Light Frost")),
			PBRLinearColorFromHex(TEXT("#eef3f7")), PBRLinearColorFromHex(TEXT("#f8fbfd")), PBRLinearColorFromHex(TEXT("#ffffff")),
			PBRLinearColorFromHex(TEXT("#f2f6f9")), PBRLinearColorFromHex(TEXT("#e7f2f7")),
			PBRLinearColorFromHex(TEXT("#2e8fb0")), PBRLinearColorFromHex(TEXT("#ffffff")),
			PBRLinearColorFromHex(TEXT("#247fa1")), PBRLinearColorFromHex(TEXT("#a9d8e8")),
			PBRLinearColorFromHex(TEXT("#2c9b69")), PBRLinearColorFromHex(TEXT("#b47a16")), PBRLinearColorFromHex(TEXT("#c84d4d")),
			PBRLinearColorFromHex(TEXT("#17212b")), PBRLinearColorFromHex(TEXT("#5f6f7a")),
			PBRLinearColorFromHex(TEXT("#ccd9e2")), PBRLinearColorFromHex(TEXT("#dceff6")), PBRLinearColorFromHex(TEXT("#247fa1"))
		}
	};
	return Themes;
}

static const FPBRMagicTheme& FindMagicTheme(FName ThemeId)
{
	for (const FPBRMagicTheme& Theme : GetMagicThemes())
	{
		if (Theme.Id == ThemeId)
		{
			return Theme;
		}
	}
	return GetMagicThemes()[0];
}

static FString ActorLabel(AActor* Actor)
{
	return Actor ? Actor->GetActorLabel() : FString();
}

static bool HasComponentOfClass(AActor* Actor, UClass* ComponentClass)
{
	if (!Actor || !ComponentClass)
	{
		return false;
	}
	TArray<UActorComponent*> Components;
	Actor->GetComponents(ComponentClass, Components);
	return Components.Num() > 0;
}

static bool HasSceneMesh(AActor* Actor)
{
	return Actor && !Actor->IsA<ALight>() && !Actor->IsA<ACameraActor>() && HasComponentOfClass(Actor, UMeshComponent::StaticClass());
}

static bool IsCJKCharacter(TCHAR Character)
{
	return (Character >= 0x4E00 && Character <= 0x9FFF)
		|| (Character >= 0x3400 && Character <= 0x4DBF)
		|| (Character >= 0xF900 && Character <= 0xFAFF);
}

static FString BuildNameCheckKeyFromLabel(const FString& Label)
{
	FString Key = Label;
	Key.TrimStartAndEndInline();
	if (Key.IsEmpty())
	{
		return Key;
	}

	int32 FirstCJK = INDEX_NONE;
	int32 LastCJK = INDEX_NONE;
	for (int32 Index = 0; Index < Key.Len(); ++Index)
	{
		if (IsCJKCharacter(Key[Index]))
		{
			if (FirstCJK == INDEX_NONE)
			{
				FirstCJK = Index;
			}
			LastCJK = Index;
		}
	}
	if (FirstCJK == INDEX_NONE || LastCJK == INDEX_NONE)
	{
		return FString();
	}

	Key = Key.Mid(FirstCJK, LastCJK - FirstCJK + 1);
	Key.TrimStartAndEndInline();
	return Key;
}

static ULightComponent* GetPrimaryLightComponent(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	TArray<ULightComponent*> LightComponents;
	Actor->GetComponents<ULightComponent>(LightComponents);
	for (ULightComponent* LightComponent : LightComponents)
	{
		if (Cast<URectLightComponent>(LightComponent))
		{
			return LightComponent;
		}
	}
	for (ULightComponent* LightComponent : LightComponents)
	{
		if (Cast<USpotLightComponent>(LightComponent))
		{
			return LightComponent;
		}
	}
	for (ULightComponent* LightComponent : LightComponents)
	{
		if (Cast<UPointLightComponent>(LightComponent))
		{
			return LightComponent;
		}
	}
	return LightComponents.Num() > 0 ? LightComponents[0] : nullptr;
}

static FString MaterialParentName(UMaterialInterface* Material)
{
	UMaterialInterface* Current = Material;
	while (Current)
	{
		const FString Name = Current->GetName();
		if (Name.StartsWith(TEXT("M_PBR_")) || Name.StartsWith(TEXT("SM_")))
		{
			return Name;
		}
		UMaterialInstance* Instance = Cast<UMaterialInstance>(Current);
		Current = Instance ? Instance->Parent : nullptr;
	}
	return FString();
}

static FString PBRMaterialFamilyLabel(const FString& ParentName)
{
	if (ParentName.Contains(TEXT("Wood"))) { return TEXT("PBR母材质 / 木材"); }
	if (ParentName.Contains(TEXT("Stone"))) { return TEXT("PBR母材质 / 石材"); }
	if (ParentName.Contains(TEXT("Tile"))) { return TEXT("PBR母材质 / 瓷砖"); }
	if (ParentName.Contains(TEXT("Fabric"))) { return TEXT("PBR母材质 / 布料"); }
	if (ParentName.Contains(TEXT("Leather"))) { return TEXT("PBR母材质 / 皮革"); }
	if (ParentName.Contains(TEXT("Plastic"))) { return TEXT("PBR母材质 / 塑料"); }
	if (ParentName.Contains(TEXT("Metal"))) { return TEXT("PBR母材质 / 金属"); }
	if (ParentName.Contains(TEXT("Transparent"))) { return TEXT("PBR母材质 / 半透明"); }
	if (ParentName.Contains(TEXT("Glass"))) { return TEXT("PBR母材质 / 玻璃"); }
	if (ParentName.Contains(TEXT("Water"))) { return TEXT("PBR母材质 / 水"); }
	if (ParentName.Contains(TEXT("Emissive"))) { return TEXT("PBR母材质 / 自发光"); }
	if (ParentName.StartsWith(TEXT("SM_"))) { return TEXT("PBR特殊母材质 / ") + ParentName; }
	if (ParentName.StartsWith(TEXT("M_PBR_"))) { return TEXT("PBR母材质 / 标准"); }
	return FString();
}
}

enum class EPBRMagicEditableMaterialParameterKind : uint8
{
	Scalar,
	Color,
	Switch
};

struct FPBRMagicEditableMaterialParameter
{
	EPBRMagicEditableMaterialParameterKind Kind = EPBRMagicEditableMaterialParameterKind::Scalar;
	FName ParameterName;
	const TCHAR* LabelKey = TEXT("");
	const TCHAR* LabelChinese = TEXT("");
	const TCHAR* LabelEnglish = TEXT("");
	const TCHAR* GroupKey = TEXT("");
	const TCHAR* GroupChinese = TEXT("");
	const TCHAR* GroupEnglish = TEXT("");
	uint32 TypeMask = 0;
	float MinValue = 0.0f;
	float MaxValue = 1.0f;
	float DefaultValue = 0.0f;
	float StepValue = 0.05f;
	FLinearColor DefaultColor = FLinearColor::White;
	bool bDefaultSwitchValue = false;
	bool bShowOnPaintSurface = true;

	FText GetLabel() const
	{
		return PBRText(LabelKey, LabelChinese, LabelEnglish);
	}

	FText GetGroupLabel() const
	{
		return PBRText(GroupKey, GroupChinese, GroupEnglish);
	}
};

enum class EPBRMagicDynamicMaterialParameterKind : uint8
{
	Scalar,
	Color,
	Switch,
	Texture
};

struct FPBRMagicDynamicMaterialParameter
{
	EPBRMagicDynamicMaterialParameterKind Kind = EPBRMagicDynamicMaterialParameterKind::Scalar;
	FName ParameterName;
	FString Group;
	int32 SortPriority = 0;
	float ScalarValue = 0.0f;
	float ScalarMin = 0.0f;
	float ScalarMax = 1.0f;
	float ScalarStep = 0.01f;
	FLinearColor ColorValue = FLinearColor::White;
	bool bSwitchValue = false;
	TWeakObjectPtr<UTexture> TextureValue;
};

static FString GetMagicDynamicParameterGroup(const FMaterialParameterMetadata& Metadata)
{
#if WITH_EDITORONLY_DATA
	if (!Metadata.Group.IsNone())
	{
		const FString GroupName = Metadata.Group.ToString();
		if (!GroupName.IsEmpty() && !GroupName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			return GroupName;
		}
	}
#endif
	return PBRText(TEXT("MagicParamGroupUngrouped"), TEXT("未分组"), TEXT("Ungrouped")).ToString();
}

static FString NormalizeMagicDynamicParameterGroup(const FString& ParameterName, const FString& GroupName)
{
	if (ParameterName == TEXT("法线贴图") ||
		ParameterName == TEXT("使用法线贴图") ||
		ParameterName == TEXT("法线强度") ||
		ParameterName == TEXT("法线影响折射") ||
		ParameterName == TEXT("法线影响折射强度") ||
		ParameterName == TEXT("使用高质量法线"))
	{
		return TEXT("03 - 法线");
	}

	return GroupName.IsEmpty()
		? PBRText(TEXT("MagicParamGroupUngrouped"), TEXT("未分组"), TEXT("Ungrouped")).ToString()
		: GroupName;
}

static int32 GetMagicDynamicParameterSortPriority(const FMaterialParameterMetadata& Metadata)
{
#if WITH_EDITORONLY_DATA
	return Metadata.SortPriority;
#else
	return 0;
#endif
}

static bool IsMagicLikelyNormalizedScalar(const FString& ParameterName)
{
	return ParameterName.Contains(TEXT("Opacity"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Alpha"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Roughness"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Metallic"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Specular"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Mask"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Fresnel"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("ClearCoat"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Anisotropy"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("透明"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("粗糙"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("金属"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("高光"), ESearchCase::IgnoreCase);
}

static bool IsMagicUVTilingScalar(const FString& ParameterName)
{
	return ParameterName.Contains(TEXT("Tiling"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Tile"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Scale"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("平铺"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("缩放"), ESearchCase::IgnoreCase);
}

static bool IsMagicUVOffsetScalar(const FString& ParameterName)
{
	return ParameterName.Contains(TEXT("Offset"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("偏移"), ESearchCase::IgnoreCase);
}

static bool IsMagicUVRotationScalar(const FString& ParameterName)
{
	return ParameterName.Contains(TEXT("Rotation"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Rotate"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("Angle"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("旋转"), ESearchCase::IgnoreCase) ||
		ParameterName.Contains(TEXT("角度"), ESearchCase::IgnoreCase);
}

static void NormalizeMagicDynamicScalarRange(FPBRMagicDynamicMaterialParameter& Parameter)
{
	const FString ParameterName = Parameter.ParameterName.ToString();
	if (IsMagicUVRotationScalar(ParameterName))
	{
		Parameter.ScalarMin = -360.0f;
		Parameter.ScalarMax = 360.0f;
		Parameter.ScalarStep = 1.0f;
		return;
	}
	if (IsMagicUVOffsetScalar(ParameterName))
	{
		Parameter.ScalarMin = -10.0f;
		Parameter.ScalarMax = 10.0f;
		Parameter.ScalarStep = 0.01f;
		return;
	}
	if (IsMagicUVTilingScalar(ParameterName))
	{
		Parameter.ScalarMin = 0.01f;
		Parameter.ScalarMax = 100.0f;
		Parameter.ScalarStep = 0.1f;
		return;
	}

	if (Parameter.ScalarMax <= Parameter.ScalarMin || FMath::IsNearlyEqual(Parameter.ScalarMin, Parameter.ScalarMax))
	{
		if (IsMagicLikelyNormalizedScalar(ParameterName))
		{
			Parameter.ScalarMin = Parameter.ScalarValue < 0.0f ? -1.0f : 0.0f;
			Parameter.ScalarMax = 1.0f;
		}
		else
		{
			const float AbsValue = FMath::Abs(Parameter.ScalarValue);
			Parameter.ScalarMin = Parameter.ScalarValue < 0.0f ? -FMath::Max(AbsValue * 2.0f, 1.0f) : 0.0f;
			Parameter.ScalarMax = FMath::Max(AbsValue * 2.0f, 1.0f);
		}
	}

	if (Parameter.ScalarMax <= Parameter.ScalarMin)
	{
		Parameter.ScalarMin = 0.0f;
		Parameter.ScalarMax = 1.0f;
	}

	const float Range = Parameter.ScalarMax - Parameter.ScalarMin;
	Parameter.ScalarStep = Range > 50.0f ? 1.0f : (Range > 5.0f ? 0.1f : 0.01f);
}

static void AddMagicDynamicParameter(TArray<FPBRMagicDynamicMaterialParameter>& Items, FPBRMagicDynamicMaterialParameter&& Parameter)
{
	if (Parameter.ParameterName.IsNone())
	{
		return;
	}

	const bool bAlreadyAdded = Items.ContainsByPredicate([&Parameter](const FPBRMagicDynamicMaterialParameter& Existing)
	{
		return Existing.ParameterName == Parameter.ParameterName && Existing.Kind == Parameter.Kind;
	});

	if (!bAlreadyAdded)
	{
		Items.Add(MoveTemp(Parameter));
	}
}

static constexpr uint32 MagicEditableMaterialTypeBit(EPBRMaterialType MaterialType)
{
	return 1u << static_cast<uint8>(MaterialType);
}

static constexpr uint32 MagicEditableMaterialAllTypesMask =
	MagicEditableMaterialTypeBit(EPBRMaterialType::Standard) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Wood) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Stone) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Tile) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Fabric) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Leather) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Plastic) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Metal) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Transparent) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Glass) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Water) |
	MagicEditableMaterialTypeBit(EPBRMaterialType::Emissive);

static FPBRMagicEditableMaterialParameter MakeMagicScalarParameter(
	const FName& ParameterName,
	const TCHAR* LabelKey,
	const TCHAR* LabelChinese,
	const TCHAR* LabelEnglish,
	const TCHAR* GroupKey,
	const TCHAR* GroupChinese,
	const TCHAR* GroupEnglish,
	uint32 TypeMask,
	float MinValue,
	float MaxValue,
	float DefaultValue,
	float StepValue,
	bool bShowOnPaintSurface = true)
{
	FPBRMagicEditableMaterialParameter Parameter;
	Parameter.Kind = EPBRMagicEditableMaterialParameterKind::Scalar;
	Parameter.ParameterName = ParameterName;
	Parameter.LabelKey = LabelKey;
	Parameter.LabelChinese = LabelChinese;
	Parameter.LabelEnglish = LabelEnglish;
	Parameter.GroupKey = GroupKey;
	Parameter.GroupChinese = GroupChinese;
	Parameter.GroupEnglish = GroupEnglish;
	Parameter.TypeMask = TypeMask;
	Parameter.MinValue = MinValue;
	Parameter.MaxValue = MaxValue;
	Parameter.DefaultValue = DefaultValue;
	Parameter.StepValue = StepValue;
	Parameter.bShowOnPaintSurface = bShowOnPaintSurface;
	return Parameter;
}

static FPBRMagicEditableMaterialParameter MakeMagicColorParameter(
	const FName& ParameterName,
	const TCHAR* LabelKey,
	const TCHAR* LabelChinese,
	const TCHAR* LabelEnglish,
	const TCHAR* GroupKey,
	const TCHAR* GroupChinese,
	const TCHAR* GroupEnglish,
	uint32 TypeMask,
	const FLinearColor& DefaultColor,
	bool bShowOnPaintSurface = false)
{
	FPBRMagicEditableMaterialParameter Parameter;
	Parameter.Kind = EPBRMagicEditableMaterialParameterKind::Color;
	Parameter.ParameterName = ParameterName;
	Parameter.LabelKey = LabelKey;
	Parameter.LabelChinese = LabelChinese;
	Parameter.LabelEnglish = LabelEnglish;
	Parameter.GroupKey = GroupKey;
	Parameter.GroupChinese = GroupChinese;
	Parameter.GroupEnglish = GroupEnglish;
	Parameter.TypeMask = TypeMask;
	Parameter.DefaultColor = DefaultColor;
	Parameter.bShowOnPaintSurface = bShowOnPaintSurface;
	return Parameter;
}

static FPBRMagicEditableMaterialParameter MakeMagicSwitchParameter(
	const FName& ParameterName,
	const TCHAR* LabelKey,
	const TCHAR* LabelChinese,
	const TCHAR* LabelEnglish,
	const TCHAR* GroupKey,
	const TCHAR* GroupChinese,
	const TCHAR* GroupEnglish,
	uint32 TypeMask,
	bool bDefaultValue,
	bool bShowOnPaintSurface = false)
{
	FPBRMagicEditableMaterialParameter Parameter;
	Parameter.Kind = EPBRMagicEditableMaterialParameterKind::Switch;
	Parameter.ParameterName = ParameterName;
	Parameter.LabelKey = LabelKey;
	Parameter.LabelChinese = LabelChinese;
	Parameter.LabelEnglish = LabelEnglish;
	Parameter.GroupKey = GroupKey;
	Parameter.GroupChinese = GroupChinese;
	Parameter.GroupEnglish = GroupEnglish;
	Parameter.TypeMask = TypeMask;
	Parameter.bDefaultSwitchValue = bDefaultValue;
	Parameter.bShowOnPaintSurface = bShowOnPaintSurface;
	return Parameter;
}

static bool IsMagicEditableParameterVisibleForType(const FPBRMagicEditableMaterialParameter& Parameter, EPBRMaterialType MaterialType)
{
	return (Parameter.TypeMask & MagicEditableMaterialTypeBit(MaterialType)) != 0;
}

static const TArray<FPBRMagicEditableMaterialParameter>& GetMagicEditableMaterialParameters()
{
	constexpr uint32 StandardAndMetal =
		MagicEditableMaterialTypeBit(EPBRMaterialType::Standard) |
		MagicEditableMaterialTypeBit(EPBRMaterialType::Metal);
	constexpr uint32 HeightTypes =
		MagicEditableMaterialTypeBit(EPBRMaterialType::Standard) |
		MagicEditableMaterialTypeBit(EPBRMaterialType::Wood) |
		MagicEditableMaterialTypeBit(EPBRMaterialType::Stone) |
		MagicEditableMaterialTypeBit(EPBRMaterialType::Tile);
	constexpr uint32 TransparentTypes =
		MagicEditableMaterialTypeBit(EPBRMaterialType::Transparent) |
		MagicEditableMaterialTypeBit(EPBRMaterialType::Glass) |
		MagicEditableMaterialTypeBit(EPBRMaterialType::Water);
	constexpr uint32 GlassOnly = MagicEditableMaterialTypeBit(EPBRMaterialType::Glass);
	constexpr uint32 WaterOnly = MagicEditableMaterialTypeBit(EPBRMaterialType::Water);
	constexpr uint32 FabricOnly = MagicEditableMaterialTypeBit(EPBRMaterialType::Fabric);
	constexpr uint32 LeatherOnly = MagicEditableMaterialTypeBit(EPBRMaterialType::Leather);
	constexpr uint32 MetalOnly = MagicEditableMaterialTypeBit(EPBRMaterialType::Metal);
	constexpr uint32 EmissiveOnly = MagicEditableMaterialTypeBit(EPBRMaterialType::Emissive);

	static const TArray<FPBRMagicEditableMaterialParameter> Parameters = {
		MakeMagicColorParameter(FPBRMaterialParameters::BaseColorTint, TEXT("MagicParamBaseTint"), TEXT("基础色调"), TEXT("Base Tint"), TEXT("MagicParamGroupBase"), TEXT("基础 / 贴图"), TEXT("Base / Textures"), MagicEditableMaterialAllTypesMask, FLinearColor::White, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::BaseColorIntensity, TEXT("MagicParamBaseIntensity"), TEXT("基础色强度"), TEXT("Base Intensity"), TEXT("MagicParamGroupBase"), TEXT("基础 / 贴图"), TEXT("Base / Textures"), MagicEditableMaterialAllTypesMask, 0.0f, 5.0f, 1.0f, 0.05f, true),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseBaseColorTexture, TEXT("MagicParamUseBaseTexture"), TEXT("启用基础色贴图"), TEXT("Use Base Texture"), TEXT("MagicParamGroupBase"), TEXT("基础 / 贴图"), TEXT("Base / Textures"), MagicEditableMaterialAllTypesMask, false),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseNormalTexture, TEXT("MagicParamUseNormalTexture"), TEXT("启用法线贴图"), TEXT("Use Normal Texture"), TEXT("MagicParamGroupBase"), TEXT("基础 / 贴图"), TEXT("Base / Textures"), MagicEditableMaterialAllTypesMask, false),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseRoughnessTexture, TEXT("MagicParamUseRoughnessTexture"), TEXT("启用粗糙度贴图"), TEXT("Use Roughness Texture"), TEXT("MagicParamGroupBase"), TEXT("基础 / 贴图"), TEXT("Base / Textures"), MagicEditableMaterialAllTypesMask, false),
		MakeMagicScalarParameter(FPBRMaterialParameters::RoughnessValue, TEXT("MagicParamRoughness"), TEXT("粗糙度"), TEXT("Roughness"), TEXT("MagicParamGroupSurface"), TEXT("表面响应"), TEXT("Surface Response"), MagicEditableMaterialAllTypesMask, 0.0f, 1.0f, 0.5f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::RoughnessMultiplier, TEXT("MagicParamRoughnessMultiplier"), TEXT("粗糙度强度"), TEXT("Roughness Multiplier"), TEXT("MagicParamGroupSurface"), TEXT("表面响应"), TEXT("Surface Response"), MagicEditableMaterialAllTypesMask, 0.0f, 4.0f, 1.0f, 0.05f),
		MakeMagicScalarParameter(FPBRMaterialParameters::SpecularLevel, TEXT("MagicParamSpecular"), TEXT("高光强度"), TEXT("Specular"), TEXT("MagicParamGroupSurface"), TEXT("表面响应"), TEXT("Surface Response"), MagicEditableMaterialAllTypesMask, 0.0f, 1.0f, 0.5f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::NormalStrength, TEXT("MagicParamNormalStrength"), TEXT("法线强度"), TEXT("Normal Strength"), TEXT("MagicParamGroupSurface"), TEXT("表面响应"), TEXT("Surface Response"), MagicEditableMaterialAllTypesMask, 0.0f, 5.0f, 1.0f, 0.05f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::AOMultiplier, TEXT("MagicParamAOMultiplier"), TEXT("环境遮蔽强度"), TEXT("AO Multiplier"), TEXT("MagicParamGroupSurface"), TEXT("表面响应"), TEXT("Surface Response"), MagicEditableMaterialAllTypesMask, 0.0f, 4.0f, 1.0f, 0.05f),
		MakeMagicScalarParameter(FPBRMaterialParameters::UVUTiling, TEXT("MagicParamUVUTiling"), TEXT("U 平铺"), TEXT("U Tiling"), TEXT("MagicParamGroupUV"), TEXT("UV 调整"), TEXT("UV Adjust"), MagicEditableMaterialAllTypesMask, 0.01f, 100.0f, 1.0f, 0.1f),
		MakeMagicScalarParameter(FPBRMaterialParameters::UVVTiling, TEXT("MagicParamUVVTiling"), TEXT("V 平铺"), TEXT("V Tiling"), TEXT("MagicParamGroupUV"), TEXT("UV 调整"), TEXT("UV Adjust"), MagicEditableMaterialAllTypesMask, 0.01f, 100.0f, 1.0f, 0.1f),
		MakeMagicScalarParameter(FPBRMaterialParameters::UVUOffset, TEXT("MagicParamUVUOffset"), TEXT("U 偏移"), TEXT("U Offset"), TEXT("MagicParamGroupUV"), TEXT("UV 调整"), TEXT("UV Adjust"), MagicEditableMaterialAllTypesMask, -10.0f, 10.0f, 0.0f, 0.01f),
		MakeMagicScalarParameter(FPBRMaterialParameters::UVVOffset, TEXT("MagicParamUVVOffset"), TEXT("V 偏移"), TEXT("V Offset"), TEXT("MagicParamGroupUV"), TEXT("UV 调整"), TEXT("UV Adjust"), MagicEditableMaterialAllTypesMask, -10.0f, 10.0f, 0.0f, 0.01f),
		MakeMagicScalarParameter(FPBRMaterialParameters::UVRotationDegrees, TEXT("MagicParamUVRotation"), TEXT("UV 旋转角度"), TEXT("UV Rotation"), TEXT("MagicParamGroupUV"), TEXT("UV 调整"), TEXT("UV Adjust"), MagicEditableMaterialAllTypesMask, -360.0f, 360.0f, 0.0f, 1.0f),

		MakeMagicSwitchParameter(FPBRMaterialParameters::UseMetallicTexture, TEXT("MagicParamUseMetallicTexture"), TEXT("启用金属度贴图"), TEXT("Use Metallic Texture"), TEXT("MagicParamGroupMetal"), TEXT("金属 / 标准"), TEXT("Metal / Standard"), StandardAndMetal, false),
		MakeMagicScalarParameter(FPBRMaterialParameters::MetallicValue, TEXT("MagicParamMetallic"), TEXT("金属度"), TEXT("Metallic"), TEXT("MagicParamGroupMetal"), TEXT("金属 / 标准"), TEXT("Metal / Standard"), StandardAndMetal, 0.0f, 1.0f, 0.0f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::MetallicMultiplier, TEXT("MagicParamMetallicMultiplier"), TEXT("金属度强度"), TEXT("Metallic Multiplier"), TEXT("MagicParamGroupMetal"), TEXT("金属 / 标准"), TEXT("Metal / Standard"), StandardAndMetal, 0.0f, 2.0f, 0.0f, 0.05f),
		MakeMagicScalarParameter(FPBRMaterialParameters::Anisotropy, TEXT("MagicParamAnisotropy"), TEXT("各向异性"), TEXT("Anisotropy"), TEXT("MagicParamGroupMetal"), TEXT("金属 / 标准"), TEXT("Metal / Standard"), MetalOnly, -1.0f, 1.0f, 0.22f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::FlakeScale, TEXT("MagicParamFlakeScale"), TEXT("金属颗粒缩放"), TEXT("Flake Scale"), TEXT("MagicParamGroupMetal"), TEXT("金属 / 标准"), TEXT("Metal / Standard"), MetalOnly, 1.0f, 200.0f, 35.0f, 1.0f),
		MakeMagicScalarParameter(FPBRMaterialParameters::FlakeIntensity, TEXT("MagicParamFlakeIntensity"), TEXT("金属颗粒强度"), TEXT("Flake Intensity"), TEXT("MagicParamGroupMetal"), TEXT("金属 / 标准"), TEXT("Metal / Standard"), MetalOnly, 0.0f, 2.0f, 0.0f, 0.02f, true),

		MakeMagicSwitchParameter(FPBRMaterialParameters::UseHeightTexture, TEXT("MagicParamUseHeightTexture"), TEXT("启用高度贴图"), TEXT("Use Height Texture"), TEXT("MagicParamGroupHeight"), TEXT("高度 / 置换"), TEXT("Height / Displacement"), HeightTypes, false),
		MakeMagicScalarParameter(FPBRMaterialParameters::HeightStrength, TEXT("MagicParamHeightStrength"), TEXT("置换强度"), TEXT("Height Strength"), TEXT("MagicParamGroupHeight"), TEXT("高度 / 置换"), TEXT("Height / Displacement"), HeightTypes, 0.0f, 1.0f, 0.0f, 0.01f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::PixelDepthOffsetStrength, TEXT("MagicParamPixelDepthOffset"), TEXT("深度偏移强度"), TEXT("Pixel Depth Offset"), TEXT("MagicParamGroupHeight"), TEXT("高度 / 置换"), TEXT("Height / Displacement"), HeightTypes, 0.0f, 1.0f, 0.0f, 0.01f),

		MakeMagicColorParameter(FPBRMaterialParameters::FabricFuzzColor, TEXT("MagicParamFabricFuzzColor"), TEXT("织物绒毛颜色"), TEXT("Fabric Fuzz Color"), TEXT("MagicParamGroupFabric"), TEXT("布料绒毛"), TEXT("Fabric Fuzz"), FabricOnly, FLinearColor::White),
		MakeMagicScalarParameter(FPBRMaterialParameters::FabricFuzzStrength, TEXT("MagicParamFabricFuzzStrength"), TEXT("织物绒毛强度"), TEXT("Fabric Fuzz Strength"), TEXT("MagicParamGroupFabric"), TEXT("布料绒毛"), TEXT("Fabric Fuzz"), FabricOnly, 0.0f, 2.0f, 0.42f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::ClearCoat, TEXT("MagicParamClearCoat"), TEXT("清漆强度"), TEXT("Clear Coat"), TEXT("MagicParamGroupLeather"), TEXT("皮革清漆"), TEXT("Leather Clear Coat"), LeatherOnly, 0.0f, 1.0f, 0.35f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::ClearCoatRoughness, TEXT("MagicParamClearCoatRoughness"), TEXT("清漆粗糙度"), TEXT("Clear Coat Roughness"), TEXT("MagicParamGroupLeather"), TEXT("皮革清漆"), TEXT("Leather Clear Coat"), LeatherOnly, 0.0f, 1.0f, 0.22f, 0.02f, true),

		MakeMagicSwitchParameter(FPBRMaterialParameters::UseOpacityTexture, TEXT("MagicParamUseOpacityTexture"), TEXT("启用透明贴图"), TEXT("Use Opacity Texture"), TEXT("MagicParamGroupTransparent"), TEXT("透明 / 折射"), TEXT("Transparency / Refraction"), TransparentTypes, false, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::Opacity, TEXT("MagicParamOpacity"), TEXT("透明度"), TEXT("Opacity"), TEXT("MagicParamGroupTransparent"), TEXT("透明 / 折射"), TEXT("Transparency / Refraction"), TransparentTypes, 0.0f, 1.0f, 0.35f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::RefractionAmount, TEXT("MagicParamRefraction"), TEXT("折射率"), TEXT("Refraction"), TEXT("MagicParamGroupTransparent"), TEXT("透明 / 折射"), TEXT("Transparency / Refraction"), TransparentTypes, 1.0f, 2.4f, 1.45f, 0.01f, true),

		MakeMagicColorParameter(FPBRMaterialParameters::GlassAbsorptionColor, TEXT("MagicParamGlassAbsorptionColor"), TEXT("玻璃吸收颜色"), TEXT("Absorption Color"), TEXT("MagicParamGroupGlass"), TEXT("玻璃光学"), TEXT("Glass Optics"), GlassOnly, FLinearColor(0.78f, 0.92f, 1.0f, 1.0f), true),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassOpacityFresnelStrength, TEXT("MagicParamGlassOpacityFresnel"), TEXT("透明菲涅尔强度"), TEXT("Opacity Fresnel"), TEXT("MagicParamGroupGlass"), TEXT("玻璃光学"), TEXT("Glass Optics"), GlassOnly, 0.0f, 1.0f, 0.35f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassFresnelBaseReflection, TEXT("MagicParamGlassFresnelBase"), TEXT("菲涅尔基础反射"), TEXT("Fresnel Base"), TEXT("MagicParamGroupGlass"), TEXT("玻璃光学"), TEXT("Glass Optics"), GlassOnly, 0.0f, 1.0f, 0.02f, 0.01f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassFresnelExp, TEXT("MagicParamGlassFresnelExp"), TEXT("菲涅尔指数"), TEXT("Fresnel Exponent"), TEXT("MagicParamGroupGlass"), TEXT("玻璃光学"), TEXT("Glass Optics"), GlassOnly, 0.5f, 12.0f, 5.0f, 0.1f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassFrostedStrength, TEXT("MagicParamGlassFrosted"), TEXT("毛玻璃强度"), TEXT("Frosted Strength"), TEXT("MagicParamGroupGlass"), TEXT("玻璃光学"), TEXT("Glass Optics"), GlassOnly, 0.0f, 1.0f, 0.0f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassAbsorptionStrength, TEXT("MagicParamGlassAbsorptionStrength"), TEXT("玻璃吸收强度"), TEXT("Absorption Strength"), TEXT("MagicParamGroupGlass"), TEXT("玻璃光学"), TEXT("Glass Optics"), GlassOnly, 0.0f, 2.0f, 0.15f, 0.02f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassEdgeTintStrength, TEXT("MagicParamGlassEdgeTint"), TEXT("玻璃边缘染色"), TEXT("Edge Tint"), TEXT("MagicParamGroupGlass"), TEXT("玻璃光学"), TEXT("Glass Optics"), GlassOnly, 0.0f, 2.0f, 0.25f, 0.02f),

		MakeMagicColorParameter(FPBRMaterialParameters::GlassDirtColor, TEXT("MagicParamGlassDirtColor"), TEXT("玻璃污渍颜色"), TEXT("Dirt Color"), TEXT("MagicParamGroupGlassDirt"), TEXT("玻璃污渍"), TEXT("Glass Dirt"), GlassOnly, FLinearColor(0.35f, 0.32f, 0.26f, 1.0f)),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseGlassDirtTexture, TEXT("MagicParamUseGlassDirtTexture"), TEXT("启用污渍贴图"), TEXT("Use Dirt Texture"), TEXT("MagicParamGroupGlassDirt"), TEXT("玻璃污渍"), TEXT("Glass Dirt"), GlassOnly, false),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassDirtIntensity, TEXT("MagicParamGlassDirtIntensity"), TEXT("玻璃污渍强度"), TEXT("Dirt Intensity"), TEXT("MagicParamGroupGlassDirt"), TEXT("玻璃污渍"), TEXT("Glass Dirt"), GlassOnly, 0.0f, 2.0f, 0.0f, 0.02f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassDirtOpacity, TEXT("MagicParamGlassDirtOpacity"), TEXT("玻璃污渍透明度"), TEXT("Dirt Opacity"), TEXT("MagicParamGroupGlassDirt"), TEXT("玻璃污渍"), TEXT("Glass Dirt"), GlassOnly, 0.0f, 1.0f, 0.35f, 0.02f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassDirtRoughness, TEXT("MagicParamGlassDirtRoughness"), TEXT("玻璃污渍粗糙度"), TEXT("Dirt Roughness"), TEXT("MagicParamGroupGlassDirt"), TEXT("玻璃污渍"), TEXT("Glass Dirt"), GlassOnly, 0.0f, 1.0f, 0.65f, 0.02f),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseGlassDistortionTexture, TEXT("MagicParamUseGlassDistortionTexture"), TEXT("启用扭曲贴图"), TEXT("Use Distortion Texture"), TEXT("MagicParamGroupGlassDistortion"), TEXT("玻璃扭曲 / 阴影"), TEXT("Glass Distortion / Shadow"), GlassOnly, false),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassDistortionIntensity, TEXT("MagicParamGlassDistortion"), TEXT("玻璃扭曲强度"), TEXT("Distortion"), TEXT("MagicParamGroupGlassDistortion"), TEXT("玻璃扭曲 / 阴影"), TEXT("Glass Distortion / Shadow"), GlassOnly, 0.0f, 2.0f, 0.0f, 0.02f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassDistortionIORIntensity, TEXT("MagicParamGlassDistortionIOR"), TEXT("玻璃 IOR 扭曲强度"), TEXT("IOR Distortion"), TEXT("MagicParamGroupGlassDistortion"), TEXT("玻璃扭曲 / 阴影"), TEXT("Glass Distortion / Shadow"), GlassOnly, 0.0f, 2.0f, 0.0f, 0.02f),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseGlassFrostedTexture, TEXT("MagicParamUseGlassFrostedTexture"), TEXT("启用磨砂贴图"), TEXT("Use Frosted Texture"), TEXT("MagicParamGroupGlassDistortion"), TEXT("玻璃扭曲 / 阴影"), TEXT("Glass Distortion / Shadow"), GlassOnly, false),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassShadowOpacity, TEXT("MagicParamGlassShadowOpacity"), TEXT("玻璃投影强度"), TEXT("Shadow Opacity"), TEXT("MagicParamGroupGlassDistortion"), TEXT("玻璃扭曲 / 阴影"), TEXT("Glass Distortion / Shadow"), GlassOnly, 0.0f, 1.0f, 0.55f, 0.02f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassShadowHighlightClamp, TEXT("MagicParamGlassShadowHighlightClamp"), TEXT("玻璃阴影高光裁剪"), TEXT("Shadow Highlight Clamp"), TEXT("MagicParamGroupGlassDistortion"), TEXT("玻璃扭曲 / 阴影"), TEXT("Glass Distortion / Shadow"), GlassOnly, 0.0f, 2.0f, 0.85f, 0.02f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassShadowNormalIntensity, TEXT("MagicParamGlassShadowNormal"), TEXT("玻璃阴影法线强度"), TEXT("Shadow Normal"), TEXT("MagicParamGroupGlassDistortion"), TEXT("玻璃扭曲 / 阴影"), TEXT("Glass Distortion / Shadow"), GlassOnly, 0.0f, 2.0f, 0.25f, 0.02f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassCausticsIntensity, TEXT("MagicParamGlassCaustics"), TEXT("玻璃焦散强度"), TEXT("Caustics Intensity"), TEXT("MagicParamGroupGlassRay"), TEXT("玻璃焦散 / 光追"), TEXT("Glass Caustics / RT"), GlassOnly, 0.0f, 10.0f, 0.0f, 0.05f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassCausticsScale, TEXT("MagicParamGlassCausticsScale"), TEXT("玻璃焦散大小"), TEXT("Caustics Scale"), TEXT("MagicParamGroupGlassRay"), TEXT("玻璃焦散 / 光追"), TEXT("Glass Caustics / RT"), GlassOnly, 0.01f, 100.0f, 24.0f, 0.5f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassCausticsSpeed, TEXT("MagicParamGlassCausticsSpeed"), TEXT("玻璃焦散速度"), TEXT("Caustics Speed"), TEXT("MagicParamGroupGlassRay"), TEXT("玻璃焦散 / 光追"), TEXT("Glass Caustics / RT"), GlassOnly, -5.0f, 5.0f, 0.12f, 0.01f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassRTOpacity, TEXT("MagicParamGlassRTOpacity"), TEXT("光追玻璃透明度"), TEXT("RT Opacity"), TEXT("MagicParamGroupGlassRay"), TEXT("玻璃焦散 / 光追"), TEXT("Glass Caustics / RT"), GlassOnly, 0.0f, 1.0f, 0.35f, 0.02f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassRTRefractionAmount, TEXT("MagicParamGlassRTRefraction"), TEXT("光追玻璃折射"), TEXT("RT Refraction"), TEXT("MagicParamGroupGlassRay"), TEXT("玻璃焦散 / 光追"), TEXT("Glass Caustics / RT"), GlassOnly, 1.0f, 2.4f, 1.45f, 0.01f),
		MakeMagicScalarParameter(FPBRMaterialParameters::GlassRTFrostedStrength, TEXT("MagicParamGlassRTFrosted"), TEXT("光追毛玻璃强度"), TEXT("RT Frosted"), TEXT("MagicParamGroupGlassRay"), TEXT("玻璃焦散 / 光追"), TEXT("Glass Caustics / RT"), GlassOnly, 0.0f, 1.0f, 0.0f, 0.02f),

		MakeMagicColorParameter(FPBRMaterialParameters::WaterColor, TEXT("MagicParamWaterColor"), TEXT("水体颜色"), TEXT("Water Color"), TEXT("MagicParamGroupWater"), TEXT("水体"), TEXT("Water"), WaterOnly, FLinearColor(0.12f, 0.42f, 0.72f, 1.0f), true),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseWaterRippleTexture, TEXT("MagicParamUseWaterRippleTexture"), TEXT("启用水纹贴图"), TEXT("Use Ripple Texture"), TEXT("MagicParamGroupWater"), TEXT("水体"), TEXT("Water"), WaterOnly, false),
		MakeMagicScalarParameter(FPBRMaterialParameters::WaterFlowSpeedU, TEXT("MagicParamWaterFlowU"), TEXT("水流 U 速度"), TEXT("Flow U"), TEXT("MagicParamGroupWater"), TEXT("水体"), TEXT("Water"), WaterOnly, -2.0f, 2.0f, 0.18f, 0.01f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::WaterFlowSpeedV, TEXT("MagicParamWaterFlowV"), TEXT("水流 V 速度"), TEXT("Flow V"), TEXT("MagicParamGroupWater"), TEXT("水体"), TEXT("Water"), WaterOnly, -2.0f, 2.0f, 0.09f, 0.01f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::WaterRippleScale, TEXT("MagicParamWaterRippleScale"), TEXT("水波缩放"), TEXT("Ripple Scale"), TEXT("MagicParamGroupWater"), TEXT("水体"), TEXT("Water"), WaterOnly, 0.01f, 50.0f, 18.0f, 0.5f, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::WaterRippleStrength, TEXT("MagicParamWaterRippleStrength"), TEXT("水波强度"), TEXT("Ripple Strength"), TEXT("MagicParamGroupWater"), TEXT("水体"), TEXT("Water"), WaterOnly, 0.0f, 2.0f, 0.8f, 0.02f, true),

		MakeMagicColorParameter(FPBRMaterialParameters::EmissiveColor, TEXT("MagicParamEmissiveColor"), TEXT("自发光颜色"), TEXT("Emissive Color"), TEXT("MagicParamGroupEmissive"), TEXT("自发光"), TEXT("Emissive"), EmissiveOnly, FLinearColor::White, true),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseEmissiveTexture, TEXT("MagicParamUseEmissiveTexture"), TEXT("启用自发光贴图"), TEXT("Use Emissive Texture"), TEXT("MagicParamGroupEmissive"), TEXT("自发光"), TEXT("Emissive"), EmissiveOnly, false),
		MakeMagicSwitchParameter(FPBRMaterialParameters::UseEmissiveTemperature, TEXT("MagicParamUseEmissiveTemperature"), TEXT("使用自发光色温"), TEXT("Use Emissive Temperature"), TEXT("MagicParamGroupEmissive"), TEXT("自发光"), TEXT("Emissive"), EmissiveOnly, false, true),
		MakeMagicScalarParameter(FPBRMaterialParameters::EmissiveTemperatureKelvin, TEXT("MagicParamEmissiveKelvin"), TEXT("自发光色温"), TEXT("Emissive Kelvin"), TEXT("MagicParamGroupEmissive"), TEXT("自发光"), TEXT("Emissive"), EmissiveOnly, 1000.0f, 20000.0f, 6500.0f, 250.0f),
		MakeMagicScalarParameter(FPBRMaterialParameters::EmissiveIntensity, TEXT("MagicParamEmissiveIntensity"), TEXT("自发光强度"), TEXT("Emissive Intensity"), TEXT("MagicParamGroupEmissive"), TEXT("自发光"), TEXT("Emissive"), EmissiveOnly, 0.0f, 100.0f, 2.0f, 0.25f, true)
	};
	return Parameters;
}

static bool HasMagicEditableGroupVisibleForType(const TCHAR* GroupKey, EPBRMaterialType MaterialType)
{
	for (const FPBRMagicEditableMaterialParameter& Parameter : GetMagicEditableMaterialParameters())
	{
		if (FCString::Strcmp(Parameter.GroupKey, GroupKey) == 0 && IsMagicEditableParameterVisibleForType(Parameter, MaterialType))
		{
			return true;
		}
	}
	return false;
}

static FString FormatMagicEditableMaterialScalar(float Value, float StepValue)
{
	if (StepValue >= 100.0f)
	{
		return FString::Printf(TEXT("%.0f"), Value);
	}
	if (StepValue >= 1.0f)
	{
		return FString::Printf(TEXT("%.0f"), Value);
	}
	if (StepValue >= 0.1f)
	{
		return FString::Printf(TEXT("%.1f"), Value);
	}
	return FString::Printf(TEXT("%.2f"), Value);
}

static FString GetMagicMaterialParameterDisplayName(const FName& ParameterName)
{
	const FString RawName = ParameterName.ToString();
	for (const FPBRMagicEditableMaterialParameter& Parameter : GetMagicEditableMaterialParameters())
	{
		if (Parameter.ParameterName == ParameterName)
		{
			return Parameter.GetLabel().ToString();
		}
	}

	struct FKnownParameterName
	{
		const TCHAR* Raw;
		const TCHAR* Display;
	};

	static const FKnownParameterName KnownNames[] =
	{
		{ TEXT("BaseColorTexture"), TEXT("基础色贴图") },
		{ TEXT("BaseColorMap"), TEXT("基础色贴图") },
		{ TEXT("BaseColorTint"), TEXT("基础色调") },
		{ TEXT("BaseColorIntensity"), TEXT("基础色强度") },
		{ TEXT("UseBaseColorTexture"), TEXT("使用基础色贴图") },
		{ TEXT("NormalTexture"), TEXT("法线贴图") },
		{ TEXT("NormalMap"), TEXT("法线贴图") },
		{ TEXT("NormalStrength"), TEXT("法线强度") },
		{ TEXT("UseNormalTexture"), TEXT("使用法线贴图") },
		{ TEXT("RoughnessTexture"), TEXT("粗糙度贴图") },
		{ TEXT("RoughnessMap"), TEXT("粗糙度贴图") },
		{ TEXT("RoughnessIntensity"), TEXT("粗糙度强度") },
		{ TEXT("RoughnessValue"), TEXT("粗糙度数值") },
		{ TEXT("UseRoughnessTexture"), TEXT("使用粗糙度贴图") },
		{ TEXT("SpecularTexture"), TEXT("高光贴图") },
		{ TEXT("SpecularIntensity"), TEXT("高光强度") },
		{ TEXT("UseSpecularTexture"), TEXT("使用高光贴图") },
		{ TEXT("AOTexture"), TEXT("环境遮蔽贴图") },
		{ TEXT("OcclusionTexture"), TEXT("环境遮蔽贴图") },
		{ TEXT("UseAOTexture"), TEXT("使用环境遮蔽贴图") },
		{ TEXT("MetallicTexture"), TEXT("金属度贴图") },
		{ TEXT("MetallicValue"), TEXT("金属度数值") },
		{ TEXT("UseMetallicTexture"), TEXT("使用金属度贴图") },
		{ TEXT("OpacityTexture"), TEXT("透明贴图") },
		{ TEXT("OpacityValue"), TEXT("透明度") },
		{ TEXT("UseOpacityTexture"), TEXT("使用透明贴图") },
		{ TEXT("EmissiveTexture"), TEXT("自发光贴图") },
		{ TEXT("EmissiveIntensity"), TEXT("自发光强度") },
		{ TEXT("UseEmissiveTexture"), TEXT("使用自发光贴图") },
		{ TEXT("HeightTexture"), TEXT("高度贴图") },
		{ TEXT("HeightStrength"), TEXT("高度强度") },
		{ TEXT("UseHeightTexture"), TEXT("使用高度贴图") },
		{ TEXT("IOR"), TEXT("折射率") },
		{ TEXT("Refraction"), TEXT("折射强度") },
		{ TEXT("GlassTint"), TEXT("玻璃颜色") },
		{ TEXT("GlassDirtTexture"), TEXT("玻璃污渍贴图") },
		{ TEXT("GlassDistortionTexture"), TEXT("玻璃扭曲贴图") },
		{ TEXT("GlassFrostedTexture"), TEXT("玻璃磨砂贴图") },
		{ TEXT("WaterRippleTexture"), TEXT("水纹贴图") }
	};

	for (const FKnownParameterName& KnownName : KnownNames)
	{
		if (RawName.Equals(KnownName.Raw, ESearchCase::IgnoreCase))
		{
			return KnownName.Display;
		}
	}

	FString DisplayName;
	DisplayName.Reserve(RawName.Len() + 8);
	for (int32 Index = 0; Index < RawName.Len(); ++Index)
	{
		const TCHAR Character = RawName[Index];
		if (Character == TCHAR('_') || Character == TCHAR('-'))
		{
			DisplayName.AppendChar(TEXT(' '));
			continue;
		}
		if (Index > 0 && FChar::IsUpper(Character))
		{
			const TCHAR Previous = RawName[Index - 1];
			if (FChar::IsLower(Previous) || FChar::IsDigit(Previous))
			{
				DisplayName.AppendChar(TEXT(' '));
			}
		}
		DisplayName.AppendChar(Character);
	}
	DisplayName.TrimStartAndEndInline();
	return DisplayName.IsEmpty() ? RawName : DisplayName;
}

static FName GetMagicTextureUsageSwitchName(const FName& TextureParameterName)
{
	const FString Name = TextureParameterName.ToString();
	for (const FPBRMagicEditableMaterialParameter& Parameter : GetMagicEditableMaterialParameters())
	{
		if (Parameter.Kind == EPBRMagicEditableMaterialParameterKind::Switch)
		{
			const FString SwitchName = Parameter.ParameterName.ToString();
			if (SwitchName.StartsWith(TEXT("Use"), ESearchCase::IgnoreCase) &&
				SwitchName.Contains(Name.Replace(TEXT("Texture"), TEXT(""), ESearchCase::IgnoreCase), ESearchCase::IgnoreCase))
			{
				return Parameter.ParameterName;
			}
		}
	}

	if (Name.StartsWith(TEXT("Use"), ESearchCase::IgnoreCase))
	{
		return NAME_None;
	}
	if (Name.EndsWith(TEXT("Texture"), ESearchCase::IgnoreCase))
	{
		return FName(*FString::Printf(TEXT("Use%s"), *Name));
	}
	if (Name.EndsWith(TEXT("Map"), ESearchCase::IgnoreCase))
	{
		FString Base = Name.LeftChop(3);
		return FName(*FString::Printf(TEXT("Use%sTexture"), *Base));
	}
	return FName(*FString::Printf(TEXT("Use%sTexture"), *Name));
}

static const TArray<FName>& GetMagicIndependentUVChannels()
{
	static const TArray<FName> Channels =
	{
		FName(TEXT("基础色")),
		FName(TEXT("法线")),
		FName(TEXT("粗糙度")),
		FName(TEXT("高光")),
		FName(TEXT("金属度")),
		FName(TEXT("环境遮蔽")),
		FName(TEXT("透明")),
		FName(TEXT("高度")),
		FName(TEXT("自发光")),
		FName(TEXT("水纹"))
	};
	return Channels;
}

static bool GetMagicChannelUVInfo(const FName& ParameterName, FName& OutChannelName, bool& bOutSwitch, bool& bOutScalar)
{
	for (const FName& ChannelName : GetMagicIndependentUVChannels())
	{
		const FPBRChannelUVParameterNames ChannelUV = FPBRMaterialParameters::GetChannelUVNames(ChannelName);
		if (ParameterName == ChannelUV.UseIndependentUV)
		{
			OutChannelName = ChannelName;
			bOutSwitch = true;
			bOutScalar = false;
			return true;
		}
		if (ParameterName == ChannelUV.UTiling ||
			ParameterName == ChannelUV.VTiling ||
			ParameterName == ChannelUV.UOffset ||
			ParameterName == ChannelUV.VOffset ||
			ParameterName == ChannelUV.RotationDegrees)
		{
			OutChannelName = ChannelName;
			bOutSwitch = false;
			bOutScalar = true;
			return true;
		}
	}

	OutChannelName = NAME_None;
	bOutSwitch = false;
	bOutScalar = false;
	return false;
}

static FName InferMagicChannelFromMaterialParameter(const FName& ParameterName)
{
	const FString Name = ParameterName.ToString();
	if (Name.Contains(TEXT("BaseColor"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("基础色")) || Name.Contains(TEXT("基础颜色")))
	{
		return FName(TEXT("基础色"));
	}
	if (Name.Contains(TEXT("Normal"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("法线")))
	{
		return FName(TEXT("法线"));
	}
	if (Name.Contains(TEXT("Roughness"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("粗糙度")))
	{
		return FName(TEXT("粗糙度"));
	}
	if (Name.Contains(TEXT("Specular"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("高光")))
	{
		return FName(TEXT("高光"));
	}
	if (Name.Contains(TEXT("Metallic"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("金属度")))
	{
		return FName(TEXT("金属度"));
	}
	if (Name.Contains(TEXT("AO"), ESearchCase::CaseSensitive) || Name.Contains(TEXT("Occlusion"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("环境遮蔽")))
	{
		return FName(TEXT("环境遮蔽"));
	}
	if (Name.Contains(TEXT("Opacity"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("透明")))
	{
		return FName(TEXT("透明"));
	}
	if (Name.Contains(TEXT("Height"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("高度")))
	{
		return FName(TEXT("高度"));
	}
	if (Name.Contains(TEXT("Emissive"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("自发光")))
	{
		return FName(TEXT("自发光"));
	}
	if (Name.Contains(TEXT("WaterRipple"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("水纹")))
	{
		return FName(TEXT("水纹"));
	}
	return NAME_None;
}

class SPBRMagicOutlinerRow : public STableRow<TSharedPtr<FPBRMagicOutlinerItem>>
{
public:
	SLATE_BEGIN_ARGS(SPBRMagicOutlinerRow) {}
		SLATE_ARGUMENT(TSharedPtr<FPBRMagicOutlinerItem>, Item)
		SLATE_ARGUMENT(SPBRMagicOutlinerWindow*, OwnerWindow)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		OwnerWindow = InArgs._OwnerWindow;
		STableRow<TSharedPtr<FPBRMagicOutlinerItem>>::Construct(
			STableRow<TSharedPtr<FPBRMagicOutlinerItem>>::FArguments()
			.Padding(FMargin(2, 2))
			[
				InArgs._Content.Widget
			],
			OwnerTable);
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (!OwnerWindow)
		{
			return FReply::Unhandled();
		}
		if (OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Models)
		{
			return OwnerWindow->OnModelReplacementDrop(MyGeometry, DragDropEvent, Item);
		}
		return OwnerWindow->OnMaterialItemDrop(MyGeometry, DragDropEvent, Item);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (!OwnerWindow)
		{
			return FReply::Unhandled();
		}
		if (OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Models && OwnerWindow->GetDraggedStaticMesh(DragDropEvent))
		{
			return FReply::Handled();
		}
		if (OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Materials && OwnerWindow->GetDraggedMaterial(DragDropEvent))
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OwnerWindow)
		{
			OwnerWindow->HandleTreeItemClicked(Item, MouseEvent.IsControlDown() || MouseEvent.IsShiftDown());
			return FReply::Handled();
		}
		return STableRow<TSharedPtr<FPBRMagicOutlinerItem>>::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OwnerWindow)
		{
			OwnerWindow->OpenMaterialEditor(Item);
			return FReply::Handled();
		}
		return STableRow<TSharedPtr<FPBRMagicOutlinerItem>>::OnMouseButtonDoubleClick(MyGeometry, MouseEvent);
	}

private:
	TSharedPtr<FPBRMagicOutlinerItem> Item;
	SPBRMagicOutlinerWindow* OwnerWindow = nullptr;
};

enum class EPBRMagicPaintHitAction : uint8
{
	None,
	TopTab,
	ModeTab,
	ThemeCycle,
	ToggleCompact,
	ToggleClassicSkin,
	Refresh,
	SelectChecked,
	AddCurrentSelection,
	ToggleIsolation,
	ToggleCheckedVisibility,
	InvertChecked,
	ShortcutSettings,
	ToggleAutoSelect,
	ClearChecked,
	SearchBox,
	NameGroup,
	RowExpand,
	RowToggle,
	RowSelect,
	OpenMaterial,
	AdjustMaterial,
	SelectActors,
	MaterialTypePrevious,
	MaterialTypeNext,
	MaterialScalarDown,
	MaterialScalarUp,
	MaterialSwitchToggle,
	ModelBatchRename,
	ModelReplaceActors,
	ModelGroupToFolder,
	CheckedActorSelect,
	SelectCheckedList,
	ClearCheckedList,
	LightIntensityDown,
	LightIntensityUp,
	LightTemperatureDown,
	LightTemperatureUp,
	LightColor,
	ApplyPostSuggested,
	TogglePostUnbound,
	ExposureManual,
	ExposureBasic,
	ExposureHistogram,
	ExposureReset,
	ExposureBiasDown,
	ExposureBiasUp,
	BloomIntensityDown,
	BloomIntensityUp,
	WhiteTempDown,
	WhiteTempUp
};

struct FPBRMagicPaintHitRegion
{
	FSlateRect Rect;
	EPBRMagicPaintHitAction Action = EPBRMagicPaintHitAction::None;
	EPBRMagicOutlinerCategory Category = EPBRMagicOutlinerCategory::All;
	EPBRMagicOutlinerMode Mode = EPBRMagicOutlinerMode::Type;
	TSharedPtr<FPBRMagicOutlinerItem> Item;
	TSharedPtr<FPBRNameCheckListItem> NameItem;
	TSharedPtr<FPBRCheckedActorListItem> CheckedActorItem;
	int32 MaterialSlotIndex = INDEX_NONE;
	FName MaterialParameterName;
	float MaterialParameterMin = 0.0f;
	float MaterialParameterMax = 1.0f;
	float MaterialParameterDefault = 0.0f;
	float MaterialParameterStep = 0.05f;
	bool bMaterialSwitchDefault = false;
};

class SPBRMagicOutlinerPaintSurface : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRMagicOutlinerPaintSurface) {}
		SLATE_ARGUMENT(SPBRMagicOutlinerWindow*, OwnerWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OwnerWindow = InArgs._OwnerWindow;
		HoveredAction = EPBRMagicPaintHitAction::None;
		PressedAction = EPBRMagicPaintHitAction::None;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(1280.0f, 720.0f);
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!OwnerWindow || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		for (int32 Index = HitRegions.Num() - 1; Index >= 0; --Index)
		{
			const FPBRMagicPaintHitRegion& Hit = HitRegions[Index];
			if (!Hit.Rect.ContainsPoint(LocalPosition))
			{
				continue;
			}

			PressedAction = Hit.Action;
			PressedRect = Hit.Rect;
			switch (Hit.Action)
			{
			case EPBRMagicPaintHitAction::SearchBox:
				bSearchActive = true;
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse).CaptureMouse(AsShared());
			case EPBRMagicPaintHitAction::TopTab:
				bSearchActive = false;
				OwnerWindow->ActiveCategory = Hit.Category;
				OwnerWindow->ActiveMode = EPBRMagicOutlinerMode::Type;
				OwnerWindow->ActiveTreeItem.Reset();
				OwnerWindow->DetailsActor.Reset();
				OwnerWindow->PaintSelectedCheckedActors.Reset();
				RowScrollOffset = 0.0f;
				NameScrollOffset = 0.0f;
				InspectorScrollOffset = 0.0f;
				OwnerWindow->RebuildItems();
				break;
			case EPBRMagicPaintHitAction::ModeTab:
				bSearchActive = false;
				OwnerWindow->ActiveMode = Hit.Mode;
				OwnerWindow->ActiveTreeItem.Reset();
				OwnerWindow->DetailsActor.Reset();
				RowScrollOffset = 0.0f;
				InspectorScrollOffset = 0.0f;
				OwnerWindow->RebuildItems();
				break;
			case EPBRMagicPaintHitAction::ThemeCycle:
				{
					const TArray<FPBRMagicTheme>& Themes = GetMagicThemes();
					int32 ThemeIndex = 0;
					for (int32 ThemeLoopIndex = 0; ThemeLoopIndex < Themes.Num(); ++ThemeLoopIndex)
					{
						if (Themes[ThemeLoopIndex].Id == OwnerWindow->ActiveThemeId)
						{
							ThemeIndex = ThemeLoopIndex;
							break;
						}
					}
					OwnerWindow->OnThemeSelected(Themes[(ThemeIndex + 1) % Themes.Num()].Id);
				}
				break;
			case EPBRMagicPaintHitAction::ToggleCompact:
				OwnerWindow->OnToggleCompactModeClicked();
				break;
			case EPBRMagicPaintHitAction::ToggleClassicSkin:
				OwnerWindow->OnToggleClassicSkinClicked();
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled().CaptureMouse(AsShared());
			case EPBRMagicPaintHitAction::Refresh:
				RowScrollOffset = 0.0f;
				NameScrollOffset = 0.0f;
				InspectorScrollOffset = 0.0f;
				OwnerWindow->OnRefreshClicked();
				break;
			case EPBRMagicPaintHitAction::SelectChecked:
				OwnerWindow->OnSelectCheckedClicked();
				break;
			case EPBRMagicPaintHitAction::AddCurrentSelection:
				OwnerWindow->OnToggleSelectedCheckedClicked();
				break;
			case EPBRMagicPaintHitAction::ToggleIsolation:
				OwnerWindow->OnToggleIsolationClicked();
				break;
			case EPBRMagicPaintHitAction::ToggleCheckedVisibility:
				OwnerWindow->OnToggleCheckedVisibilityClicked();
				break;
			case EPBRMagicPaintHitAction::InvertChecked:
				OwnerWindow->OnInvertCheckedClicked();
				break;
			case EPBRMagicPaintHitAction::ShortcutSettings:
				OwnerWindow->OnShortcutSettingsClicked();
				break;
			case EPBRMagicPaintHitAction::ToggleAutoSelect:
				OwnerWindow->bAutoSelectCheckedActors = !OwnerWindow->bAutoSelectCheckedActors;
				OwnerWindow->SaveMagicOutlinerSettings();
				if (OwnerWindow->bAutoSelectCheckedActors)
				{
					OwnerWindow->SyncAutoSelectCheckedActors();
				}
				break;
			case EPBRMagicPaintHitAction::ClearChecked:
				OwnerWindow->OnClearCheckedClicked();
				break;
			case EPBRMagicPaintHitAction::NameGroup:
				if (Hit.NameItem.IsValid())
				{
					OwnerWindow->SetNameChecked(Hit.NameItem->Name, OwnerWindow->GetNameCheckState(Hit.NameItem->Name) != ECheckBoxState::Checked);
					if (OwnerWindow->bAutoSelectCheckedActors)
					{
						OwnerWindow->SyncAutoSelectCheckedActors();
					}
				}
				break;
			case EPBRMagicPaintHitAction::RowExpand:
				if (Hit.Item.IsValid() && !Hit.Item->Children.IsEmpty())
				{
					Hit.Item->bExpanded = !Hit.Item->bExpanded;
				}
				break;
			case EPBRMagicPaintHitAction::RowToggle:
				OwnerWindow->SetItemChecked(Hit.Item, OwnerWindow->GetItemCheckState(Hit.Item) != ECheckBoxState::Checked);
				if (OwnerWindow->bAutoSelectCheckedActors)
				{
					OwnerWindow->SyncAutoSelectCheckedActors();
				}
				break;
			case EPBRMagicPaintHitAction::RowSelect:
				OwnerWindow->HandleTreeItemClicked(Hit.Item, MouseEvent.IsControlDown() || MouseEvent.IsShiftDown());
				break;
			case EPBRMagicPaintHitAction::OpenMaterial:
				if (OwnerWindow->ActiveTreeItem.IsValid())
				{
					OwnerWindow->OpenMaterialEditor(OwnerWindow->ActiveTreeItem);
				}
				else if (!OwnerWindow->SelectedMaterialItems.IsEmpty())
				{
					OwnerWindow->OpenMaterialEditor(OwnerWindow->SelectedMaterialItems[0]);
				}
				break;
			case EPBRMagicPaintHitAction::AdjustMaterial:
				if (Hit.Item.IsValid())
				{
					OwnerWindow->ActiveTreeItem = Hit.Item;
					OwnerWindow->OnEditSelectedMaterialSlot(Hit.Item, Hit.MaterialSlotIndex);
				}
				else if (OwnerWindow->ActiveTreeItem.IsValid())
				{
					OwnerWindow->OnEditSelectedMaterialSlot(OwnerWindow->ActiveTreeItem);
				}
				else if (!OwnerWindow->SelectedMaterialItems.IsEmpty())
				{
					OwnerWindow->OnEditSelectedMaterialSlot(OwnerWindow->SelectedMaterialItems[0]);
				}
				break;
			case EPBRMagicPaintHitAction::SelectActors:
				if (OwnerWindow->ActiveTreeItem.IsValid())
				{
					OwnerWindow->SelectItemActors(OwnerWindow->ActiveTreeItem, false);
					if (OwnerWindow->ActiveTreeItem->Material.IsValid())
					{
						OwnerWindow->SyncMaterialListSelectionFromEditor();
					}
				}
				else
				{
					OwnerWindow->OnSelectCheckedClicked();
				}
				break;
			case EPBRMagicPaintHitAction::MaterialTypePrevious:
				OwnerWindow->CycleEditableMaterialType(-1);
				break;
			case EPBRMagicPaintHitAction::MaterialTypeNext:
				OwnerWindow->CycleEditableMaterialType(1);
				break;
			case EPBRMagicPaintHitAction::MaterialScalarDown:
			case EPBRMagicPaintHitAction::MaterialScalarUp:
				if (!Hit.MaterialParameterName.IsNone())
				{
					const float SignedStep = Hit.Action == EPBRMagicPaintHitAction::MaterialScalarUp ? Hit.MaterialParameterStep : -Hit.MaterialParameterStep;
					OwnerWindow->StepEditableMaterialScalar(Hit.MaterialParameterName, SignedStep, Hit.MaterialParameterMin, Hit.MaterialParameterMax, Hit.MaterialParameterDefault);
				}
				break;
			case EPBRMagicPaintHitAction::MaterialSwitchToggle:
				if (!Hit.MaterialParameterName.IsNone())
				{
					const bool bCurrentValue = OwnerWindow->GetEditableMaterialSwitch(Hit.MaterialParameterName, Hit.bMaterialSwitchDefault);
					OwnerWindow->CommitEditableMaterialSwitch(Hit.MaterialParameterName, !bCurrentValue);
				}
				break;
			case EPBRMagicPaintHitAction::ModelBatchRename:
				OwnerWindow->OnModelBatchRenameClicked();
				break;
			case EPBRMagicPaintHitAction::ModelReplaceActors:
				OwnerWindow->OnModelReplaceActorsClicked();
				break;
			case EPBRMagicPaintHitAction::ModelGroupToFolder:
				OwnerWindow->OnModelGroupToFolderClicked();
				break;
			case EPBRMagicPaintHitAction::CheckedActorSelect:
				if (Hit.CheckedActorItem.IsValid() && Hit.CheckedActorItem->Actor.IsValid())
				{
					OwnerWindow->DetailsActor = Hit.CheckedActorItem->Actor;
					if (OwnerWindow->PaintSelectedCheckedActors.Contains(Hit.CheckedActorItem->Actor))
					{
						OwnerWindow->PaintSelectedCheckedActors.Remove(Hit.CheckedActorItem->Actor);
					}
					else
					{
						OwnerWindow->PaintSelectedCheckedActors.Add(Hit.CheckedActorItem->Actor);
					}
					OwnerWindow->StatusMessage = FString::Printf(TEXT("右侧列表已选中 %d 项"), OwnerWindow->PaintSelectedCheckedActors.Num());
				}
				break;
			case EPBRMagicPaintHitAction::SelectCheckedList:
				OwnerWindow->OnSelectCheckedListClicked();
				break;
			case EPBRMagicPaintHitAction::ClearCheckedList:
				OwnerWindow->OnRemoveCheckedListSelectionClicked();
				break;
			case EPBRMagicPaintHitAction::LightIntensityDown:
				OwnerWindow->LightIntensityMultiplier = FMath::Clamp(OwnerWindow->LightIntensityMultiplier - 0.10f, 0.0f, 5.0f);
				OwnerWindow->ApplyLightAdjustmentsRealtime();
				break;
			case EPBRMagicPaintHitAction::LightIntensityUp:
				OwnerWindow->LightIntensityMultiplier = FMath::Clamp(OwnerWindow->LightIntensityMultiplier + 0.10f, 0.0f, 5.0f);
				OwnerWindow->ApplyLightAdjustmentsRealtime();
				break;
			case EPBRMagicPaintHitAction::LightTemperatureDown:
				OwnerWindow->LightTemperature = FMath::Clamp(OwnerWindow->LightTemperature - 250.0f, 1700.0f, 12000.0f);
				OwnerWindow->ApplyLightAdjustmentsRealtime();
				break;
			case EPBRMagicPaintHitAction::LightTemperatureUp:
				OwnerWindow->LightTemperature = FMath::Clamp(OwnerWindow->LightTemperature + 250.0f, 1700.0f, 12000.0f);
				OwnerWindow->ApplyLightAdjustmentsRealtime();
				break;
			case EPBRMagicPaintHitAction::LightColor:
				OwnerWindow->OnLightColorBlockClicked();
				break;
			case EPBRMagicPaintHitAction::ApplyPostSuggested:
				OwnerWindow->ApplySuggestedPostProcessSettings();
				break;
			case EPBRMagicPaintHitAction::TogglePostUnbound:
				if (APostProcessVolume* Volume = OwnerWindow->GetDetailsPostProcessVolume())
				{
					Volume->Modify();
					Volume->bUnbound = !Volume->bUnbound;
					OwnerWindow->NotifyPostProcessSettingsChanged();
				}
				break;
			case EPBRMagicPaintHitAction::ExposureManual:
			case EPBRMagicPaintHitAction::ExposureBasic:
			case EPBRMagicPaintHitAction::ExposureHistogram:
				if (FPostProcessSettings* Settings = OwnerWindow->GetDetailsPostProcessSettings())
				{
					Settings->bOverride_AutoExposureMethod = true;
					Settings->AutoExposureMethod = Hit.Action == EPBRMagicPaintHitAction::ExposureManual ? AEM_Manual : (Hit.Action == EPBRMagicPaintHitAction::ExposureBasic ? AEM_Basic : AEM_Histogram);
					OwnerWindow->NotifyPostProcessSettingsChanged();
				}
				break;
			case EPBRMagicPaintHitAction::ExposureReset:
				if (FPostProcessSettings* Settings = OwnerWindow->GetDetailsPostProcessSettings())
				{
					FPostProcessSettings Defaults;
					Settings->bOverride_AutoExposureMethod = false;
					Settings->AutoExposureMethod = Defaults.AutoExposureMethod;
					OwnerWindow->NotifyPostProcessSettingsChanged();
				}
				break;
			case EPBRMagicPaintHitAction::ExposureBiasDown:
			case EPBRMagicPaintHitAction::ExposureBiasUp:
				if (FPostProcessSettings* Settings = OwnerWindow->GetDetailsPostProcessSettings())
				{
					Settings->bOverride_AutoExposureBias = true;
					const float Delta = Hit.Action == EPBRMagicPaintHitAction::ExposureBiasUp ? 0.1f : -0.1f;
					Settings->AutoExposureBias = FMath::Clamp(Settings->AutoExposureBias + Delta, -10.0f, 10.0f);
					OwnerWindow->NotifyPostProcessSettingsChanged();
				}
				break;
			case EPBRMagicPaintHitAction::BloomIntensityDown:
			case EPBRMagicPaintHitAction::BloomIntensityUp:
				if (FPostProcessSettings* Settings = OwnerWindow->GetDetailsPostProcessSettings())
				{
					Settings->bOverride_BloomIntensity = true;
					const float Delta = Hit.Action == EPBRMagicPaintHitAction::BloomIntensityUp ? 0.1f : -0.1f;
					Settings->BloomIntensity = FMath::Clamp(Settings->BloomIntensity + Delta, 0.0f, 8.0f);
					OwnerWindow->NotifyPostProcessSettingsChanged();
				}
				break;
			case EPBRMagicPaintHitAction::WhiteTempDown:
			case EPBRMagicPaintHitAction::WhiteTempUp:
				if (FPostProcessSettings* Settings = OwnerWindow->GetDetailsPostProcessSettings())
				{
					Settings->bOverride_WhiteTemp = true;
					const float Delta = Hit.Action == EPBRMagicPaintHitAction::WhiteTempUp ? 250.0f : -250.0f;
					Settings->WhiteTemp = FMath::Clamp(Settings->WhiteTemp + Delta, 1500.0f, 15000.0f);
					OwnerWindow->NotifyPostProcessSettingsChanged();
				}
				break;
			default:
				break;
			}

			bSearchActive = false;
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled().CaptureMouse(AsShared());
		}

		bSearchActive = false;
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PressedAction != EPBRMagicPaintHitAction::None)
		{
			PressedAction = EPBRMagicPaintHitAction::None;
			PressedRect = FSlateRect();
			UpdateHoveredAction(MyGeometry, MouseEvent.GetScreenSpacePosition());
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (UpdateHoveredAction(MyGeometry, MouseEvent.GetScreenSpacePosition()))
		{
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		HoveredAction = EPBRMagicPaintHitAction::None;
		PressedAction = EPBRMagicPaintHitAction::None;
		HoveredRect = FSlateRect();
		PressedRect = FSlateRect();
		Invalidate(EInvalidateWidgetReason::Paint);
		SLeafWidget::OnMouseLeave(MouseEvent);
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!OwnerWindow || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		for (int32 Index = HitRegions.Num() - 1; Index >= 0; --Index)
		{
			const FPBRMagicPaintHitRegion& Hit = HitRegions[Index];
			if (!Hit.Rect.ContainsPoint(LocalPosition))
			{
				continue;
			}

			if (Hit.Item.IsValid() && Hit.Item->Material.IsValid())
			{
				OwnerWindow->ActiveTreeItem = Hit.Item;
				OwnerWindow->OpenMaterialEditor(Hit.Item);
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled();
			}
			if (Hit.Item.IsValid())
			{
				OwnerWindow->HandleTreeItemClicked(Hit.Item, false);
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled();
			}
			if (Hit.CheckedActorItem.IsValid() && Hit.CheckedActorItem->Actor.IsValid())
			{
				TArray<AActor*> Actors;
				Actors.Add(Hit.CheckedActorItem->Actor.Get());
				OwnerWindow->DetailsActor = Hit.CheckedActorItem->Actor;
				OwnerWindow->SelectActors(Actors, false);
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled();
			}
		}

		return FReply::Unhandled();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (!bSearchActive)
		{
			return FReply::Unhandled();
		}

		if (InKeyEvent.GetKey() == EKeys::BackSpace)
		{
			if (!SearchText.IsEmpty())
			{
				SearchText.LeftChopInline(1);
				RowScrollOffset = 0.0f;
				Invalidate(EInvalidateWidgetReason::Paint);
			}
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			bSearchActive = false;
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Enter)
		{
			bSearchActive = false;
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}

		return FReply::Handled();
	}

	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override
	{
		if (!bSearchActive)
		{
			return FReply::Unhandled();
		}

		const TCHAR Character = InCharacterEvent.GetCharacter();
		if (Character >= 32 && Character != 127)
		{
			SearchText.AppendChar(Character);
			RowScrollOffset = 0.0f;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Handled();
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!OwnerWindow)
		{
			return FReply::Unhandled();
		}

		const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		if (NameListRect.ContainsPoint(LocalPosition))
		{
			const float MaxOffset = FMath::Max(0.0f, static_cast<float>(NameItemCount) - NameVisibleCapacity);
			NameScrollOffset = FMath::Clamp(NameScrollOffset - MouseEvent.GetWheelDelta() * 3.0f, 0.0f, MaxOffset);
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}
		if (MainRowsRect.ContainsPoint(LocalPosition))
		{
			const float MaxOffset = FMath::Max(0.0f, static_cast<float>(VisibleRowCount) - VisibleRowCapacity);
			RowScrollOffset = FMath::Clamp(RowScrollOffset - MouseEvent.GetWheelDelta() * 3.0f, 0.0f, MaxOffset);
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}
		if (InspectorListRect.ContainsPoint(LocalPosition))
		{
			const float MaxOffset = FMath::Max(0.0f, static_cast<float>(InspectorItemCount) - InspectorVisibleCapacity);
			InspectorScrollOffset = FMath::Clamp(InspectorScrollOffset - MouseEvent.GetWheelDelta() * 3.0f, 0.0f, MaxOffset);
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (!OwnerWindow)
		{
			return FReply::Unhandled();
		}
		const bool bMaterialDrag = OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Materials && OwnerWindow->GetDraggedMaterial(DragDropEvent);
		const bool bModelDrag = OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Models && OwnerWindow->GetDraggedStaticMesh(DragDropEvent);
		if (!bMaterialDrag && !bModelDrag)
		{
			return FReply::Unhandled();
		}

		const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition());
		for (int32 Index = HitRegions.Num() - 1; Index >= 0; --Index)
		{
			const FPBRMagicPaintHitRegion& Hit = HitRegions[Index];
			if (Hit.Item.IsValid() && Hit.Rect.ContainsPoint(LocalPosition)
				&& ((bMaterialDrag && Hit.Item->Material.IsValid()) || (bModelDrag && (Hit.Item->Actor.IsValid() || !Hit.Item->Children.IsEmpty()))))
			{
				return FReply::Handled();
			}
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (!OwnerWindow)
		{
			return FReply::Unhandled();
		}

		const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition());
		for (int32 Index = HitRegions.Num() - 1; Index >= 0; --Index)
		{
			const FPBRMagicPaintHitRegion& Hit = HitRegions[Index];
			if (Hit.Item.IsValid() && Hit.Rect.ContainsPoint(LocalPosition))
			{
				FReply Reply = FReply::Unhandled();
				if (OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Materials && Hit.Item->Material.IsValid())
				{
					Reply = Hit.MaterialSlotIndex != INDEX_NONE
						? OwnerWindow->OnMaterialSlotDrop(MyGeometry, DragDropEvent, Hit.Item, Hit.MaterialSlotIndex)
						: OwnerWindow->OnMaterialItemDrop(MyGeometry, DragDropEvent, Hit.Item);
				}
				else if (OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Models)
				{
					Reply = OwnerWindow->OnModelReplacementDrop(MyGeometry, DragDropEvent, Hit.Item);
				}
				Invalidate(EInvalidateWidgetReason::Paint);
				return Reply;
			}
		}
		return FReply::Unhandled();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		HitRegions.Reset();
		if (!OwnerWindow)
		{
			return LayerId;
		}

		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FPBRMagicTheme& Theme = FindMagicTheme(OwnerWindow->ActiveThemeId);
		int32 Layer = LayerId;

		DrawBox(OutDrawElements, AllottedGeometry, Layer++, FVector2D::ZeroVector, Size, Theme.Background);
		DrawTopBar(OutDrawElements, AllottedGeometry, Layer, Size, Theme);
		DrawWorkspace(OutDrawElements, AllottedGeometry, Layer, Size, Theme);
		DrawStatusBar(OutDrawElements, AllottedGeometry, Layer, Size, Theme);

		return Layer + 12;
	}

private:
	bool UpdateHoveredAction(const FGeometry& Geometry, const FVector2D& ScreenPosition)
	{
		const FVector2D LocalPosition = Geometry.AbsoluteToLocal(ScreenPosition);
		EPBRMagicPaintHitAction NewHoveredAction = EPBRMagicPaintHitAction::None;
		FSlateRect NewHoveredRect;
		for (int32 Index = HitRegions.Num() - 1; Index >= 0; --Index)
		{
			const FPBRMagicPaintHitRegion& Hit = HitRegions[Index];
			if (Hit.Rect.ContainsPoint(LocalPosition))
			{
				NewHoveredAction = Hit.Action;
				NewHoveredRect = Hit.Rect;
				break;
			}
		}
		if (HoveredAction == NewHoveredAction && RectMatches(HoveredRect, NewHoveredRect))
		{
			return false;
		}
		HoveredAction = NewHoveredAction;
		HoveredRect = NewHoveredRect;
		return true;
	}

	static bool RectMatches(const FSlateRect& A, const FSlateRect& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left)
			&& FMath::IsNearlyEqual(A.Top, B.Top)
			&& FMath::IsNearlyEqual(A.Right, B.Right)
			&& FMath::IsNearlyEqual(A.Bottom, B.Bottom);
	}

	void AddHit(const FVector2D& Position, const FVector2D& Size, EPBRMagicPaintHitAction Action, EPBRMagicOutlinerCategory Category = EPBRMagicOutlinerCategory::All, TSharedPtr<FPBRMagicOutlinerItem> Item = nullptr, TSharedPtr<FPBRNameCheckListItem> NameItem = nullptr, TSharedPtr<FPBRCheckedActorListItem> CheckedActorItem = nullptr, int32 MaterialSlotIndex = INDEX_NONE) const
	{
		FPBRMagicPaintHitRegion Region;
		Region.Rect = FSlateRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y);
		Region.Action = Action;
		Region.Category = Category;
		Region.Item = Item;
		Region.NameItem = NameItem;
		Region.CheckedActorItem = CheckedActorItem;
		Region.MaterialSlotIndex = MaterialSlotIndex;
		HitRegions.Add(Region);
	}

	void AddMaterialScalarHit(const FVector2D& Position, const FVector2D& Size, EPBRMagicPaintHitAction Action, const FPBRMagicEditableMaterialParameter& Parameter) const
	{
		AddMaterialScalarHit(Position, Size, Action, Parameter.ParameterName, Parameter.MinValue, Parameter.MaxValue, Parameter.DefaultValue, Parameter.StepValue);
	}

	void AddMaterialScalarHit(const FVector2D& Position, const FVector2D& Size, EPBRMagicPaintHitAction Action, const FName& ParameterName, float MinValue, float MaxValue, float DefaultValue, float StepValue) const
	{
		FPBRMagicPaintHitRegion Region;
		Region.Rect = FSlateRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y);
		Region.Action = Action;
		Region.MaterialParameterName = ParameterName;
		Region.MaterialParameterMin = MinValue;
		Region.MaterialParameterMax = MaxValue;
		Region.MaterialParameterDefault = DefaultValue;
		Region.MaterialParameterStep = StepValue;
		HitRegions.Add(Region);
	}

	void AddMaterialSwitchHit(const FVector2D& Position, const FVector2D& Size, const FPBRMagicEditableMaterialParameter& Parameter) const
	{
		AddMaterialSwitchHit(Position, Size, Parameter.ParameterName, Parameter.bDefaultSwitchValue);
	}

	void AddMaterialSwitchHit(const FVector2D& Position, const FVector2D& Size, const FName& ParameterName, bool bDefaultValue) const
	{
		FPBRMagicPaintHitRegion Region;
		Region.Rect = FSlateRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y);
		Region.Action = EPBRMagicPaintHitAction::MaterialSwitchToggle;
		Region.MaterialParameterName = ParameterName;
		Region.bMaterialSwitchDefault = bDefaultValue;
		HitRegions.Add(Region);
	}

	void AddModeHit(const FVector2D& Position, const FVector2D& Size, EPBRMagicOutlinerMode Mode) const
	{
		FPBRMagicPaintHitRegion Region;
		Region.Rect = FSlateRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y);
		Region.Action = EPBRMagicPaintHitAction::ModeTab;
		Region.Mode = Mode;
		HitRegions.Add(Region);
	}

	static FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A = Alpha;
		return Color;
	}

	static FLinearColor MixColor(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(
			FMath::Lerp(A.R, B.R, Alpha),
			FMath::Lerp(A.G, B.G, Alpha),
			FMath::Lerp(A.B, B.B, Alpha),
			FMath::Lerp(A.A, B.A, Alpha));
	}

	static void DrawBox(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		if (Size.X <= 0.0f || Size.Y <= 0.0f || Color.A <= 0.0f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			Layer,
			Geometry.ToPaintGeometry(FVector2f(Size.X, Size.Y), FSlateLayoutTransform(FVector2f(Position.X, Position.Y))),
			FAppStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			Color);
	}

	static void DrawRoundedFill(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, float Radius, const FLinearColor& Color)
	{
		if (Size.X <= 0.0f || Size.Y <= 0.0f || Color.A <= 0.0f)
		{
			return;
		}
		const float ClampedRadius = FMath::Clamp(Radius, 0.0f, FMath::Min(Size.X, Size.Y) * 0.5f);
		if (ClampedRadius <= 1.0f || Size.X <= 2.0f || Size.Y <= 2.0f)
		{
			DrawBox(OutDrawElements, Geometry, Layer, Position, Size, Color);
			return;
		}

		const float R = FMath::RoundToFloat(ClampedRadius);
		const float EdgeH = FMath::Min(R, Size.Y * 0.5f);
		const float CenterH = FMath::Max(0.0f, Size.Y - EdgeH * 2.0f);
		DrawBox(OutDrawElements, Geometry, Layer, Position + FVector2D(0.0f, EdgeH), FVector2D(Size.X, CenterH), Color);

		const float StepH = FMath::Max(1.0f, FMath::CeilToFloat(EdgeH / 6.0f));
		const float Ratios[6] = { 0.42f, 0.24f, 0.13f, 0.06f, 0.02f, 0.0f };
		for (int32 Step = 0; Step < 6; ++Step)
		{
			const float StripY = static_cast<float>(Step) * StepH;
			if (StripY >= EdgeH)
			{
				break;
			}
			const float StripH = FMath::Min(StepH, EdgeH - StripY);
			const float Inset = FMath::RoundToFloat(R * Ratios[Step]);
			const float StripW = FMath::Max(0.0f, Size.X - Inset * 2.0f);
			DrawBox(OutDrawElements, Geometry, Layer, Position + FVector2D(Inset, StripY), FVector2D(StripW, StripH), Color);
			DrawBox(OutDrawElements, Geometry, Layer, Position + FVector2D(Inset, Size.Y - StripY - StripH), FVector2D(StripW, StripH), Color);
		}
	}

	void DrawRoundedBox(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FLinearColor& FillColor, float Radius, const FLinearColor& OutlineColor = FLinearColor::Transparent, float OutlineWidth = 0.0f) const
	{
		if (OutlineWidth > 0.0f && OutlineColor.A > 0.0f)
		{
			DrawRoundedFill(OutDrawElements, Geometry, Layer, Position, Size, Radius, OutlineColor);
			const FVector2D InnerPos = Position + FVector2D(OutlineWidth, OutlineWidth);
			const FVector2D InnerSize = Size - FVector2D(OutlineWidth * 2.0f, OutlineWidth * 2.0f);
			DrawRoundedFill(OutDrawElements, Geometry, Layer + 1, InnerPos, InnerSize, FMath::Max(0.0f, Radius - OutlineWidth), FillColor);
		}
		else
		{
			DrawRoundedFill(OutDrawElements, Geometry, Layer, Position, Size, Radius, FillColor);
		}
	}

	void DrawPanelSurface(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FLinearColor& FillColor, float Radius, const FLinearColor& OutlineColor, float OutlineWidth = 1.0f) const
	{
		DrawBox(OutDrawElements, Geometry, Layer, Position, Size, FillColor);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, Position, Size, FillColor, Radius, OutlineColor, OutlineWidth);
	}

	void DrawInsetPanelFrame(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		const FVector2D Inset(6.0f, 6.0f);
		const FVector2D FrameSize = Size - Inset * 2.0f;
		if (FrameSize.X > 24.0f && FrameSize.Y > 24.0f)
		{
			DrawRoundedBox(OutDrawElements, Geometry, Layer, Position + Inset, FrameSize, Theme.Panel, 6.0f, WithAlpha(Theme.Border, 0.92f), 1.0f);
		}
	}

	static void DrawIcon(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, FName BrushName, const FLinearColor& Color)
	{
		const FSlateBrush* Brush = BrushName.IsNone() ? nullptr : FPBRStudioStyle::Get().GetBrush(BrushName);
		if (!Brush)
		{
			return;
		}
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			Layer,
			Geometry.ToPaintGeometry(FVector2f(Size.X, Size.Y), FSlateLayoutTransform(FVector2f(Position.X, Position.Y))),
			Brush,
			ESlateDrawEffect::None,
			Color);
	}

	static void DrawText(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			Layer,
			Geometry.ToPaintGeometry(FVector2f(1.0f, 1.0f), FSlateLayoutTransform(FVector2f(Position.X, Position.Y))),
			Text,
			Font,
			ESlateDrawEffect::None,
			Color);
	}

	static float EstimateTextWidth(const FString& Text, const FSlateFontInfo& Font)
	{
		float Width = 0.0f;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Character = Text[Index];
			const bool bWide = Character > 0x2E80;
			Width += bWide ? Font.Size * 0.95f : Font.Size * 0.56f;
		}
		return Width;
	}

	static FString EllipsizeText(const FString& Text, float MaxWidth, const FSlateFontInfo& Font)
	{
		if (MaxWidth <= 8.0f || Text.IsEmpty())
		{
			return FString();
		}
		if (EstimateTextWidth(Text, Font) <= MaxWidth)
		{
			return Text;
		}

		const FString Suffix = TEXT("...");
		const float SuffixWidth = EstimateTextWidth(Suffix, Font) + 8.0f;
		FString Result;
		float Width = 0.0f;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const FString NextChar = Text.Mid(Index, 1);
			const float NextWidth = EstimateTextWidth(NextChar, Font);
			if (Width + NextWidth + SuffixWidth > MaxWidth)
			{
				break;
			}
			Result += NextChar;
			Width += NextWidth;
		}
		return Result.IsEmpty() ? Suffix : Result + Suffix;
	}

	static void DrawTextInRect(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, float MaxWidth, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		const float SafeWidth = FMath::Max(0.0f, MaxWidth - 8.0f);
		if (SafeWidth <= 8.0f)
		{
			return;
		}
		DrawText(OutDrawElements, Geometry, Layer, Position, EllipsizeText(Text, SafeWidth, Font), Font, Color);
	}

	static FLinearColor ColorFromText(const FString& Text, const FPBRMagicTheme& Theme)
	{
		if (Text.IsEmpty())
		{
			return Theme.Primary;
		}
		const uint32 Hash = GetTypeHash(Text);
		const uint8 Hue = static_cast<uint8>(Hash % 255);
		return FLinearColor::MakeFromHSV8(Hue, 122, 214);
	}

	void DrawMaterialSwatch(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FString& Seed, const FPBRMagicTheme& Theme) const
	{
		const FLinearColor Base = ColorFromText(Seed, Theme);
		DrawBox(OutDrawElements, Geometry, Layer, Position, Size, Theme.Background);
		DrawBox(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(2, 2), Size - FVector2D(4, 4), Base);
		DrawBox(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(5, 5), FVector2D(Size.X * 0.34f, Size.Y * 0.32f), WithAlpha(FLinearColor::White, 0.32f));
		DrawBox(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(Size.X * 0.55f, Size.Y * 0.58f), FVector2D(Size.X * 0.30f, Size.Y * 0.22f), WithAlpha(Theme.Background, 0.42f));
	}

	void DrawMaterialPreview(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, TSharedPtr<FPBRMagicOutlinerItem> Item, const FPBRMagicTheme& Theme) const
	{
		DrawRoundedBox(OutDrawElements, Geometry, Layer, Position, Size, Theme.Background, 6.0f, Theme.ThumbnailBorder, 1.0f);

		UMaterialInterface* Material = Item.IsValid() ? Item->Material.Get() : nullptr;
		TSharedPtr<FSlateDynamicImageBrush> ThumbnailBrush = OwnerWindow && Material
			? OwnerWindow->GetOrCreateMaterialThumbnailBrush(Material, Size)
			: nullptr;
		if (ThumbnailBrush.IsValid())
		{
			const FVector2D InnerPos = Position + FVector2D(2.0f, 2.0f);
			const FVector2D InnerSize = Size - FVector2D(4.0f, 4.0f);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				Layer + 2,
				Geometry.ToPaintGeometry(FVector2f(InnerSize.X, InnerSize.Y), FSlateLayoutTransform(FVector2f(InnerPos.X, InnerPos.Y))),
				ThumbnailBrush.Get(),
				ESlateDrawEffect::None,
				FLinearColor::White);
			return;
		}

		DrawMaterialSwatch(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(2, 2), Size - FVector2D(4, 4), Item.IsValid() ? Item->DisplayName : FString(), Theme);
	}

	EPBRMagicOutlinerCategory ResolveGlyphCategory(TSharedPtr<FPBRMagicOutlinerItem> Item) const
	{
		if (OwnerWindow && OwnerWindow->ActiveCategory != EPBRMagicOutlinerCategory::All)
		{
			return OwnerWindow->ActiveCategory;
		}
		const FString Type = Item.IsValid() ? Item->TypeText : FString();
		const FString Name = Item.IsValid() ? Item->DisplayName : FString();
		if (Type.Contains(TEXT("Light")) || Type.Contains(TEXT("光")) || Type.Contains(TEXT("灯")))
		{
			return EPBRMagicOutlinerCategory::Lights;
		}
		if (Type.Contains(TEXT("Camera")) || Type.Contains(TEXT("相机")) || Type.Contains(TEXT("PostProcess")) || Type.Contains(TEXT("后期")))
		{
			return EPBRMagicOutlinerCategory::Cameras;
		}
		if (Type.Contains(TEXT("Blueprint")) || Type.Contains(TEXT("蓝图")))
		{
			return EPBRMagicOutlinerCategory::Blueprints;
		}
		if (Type.Contains(TEXT("Level")) || Type.Contains(TEXT("关卡")))
		{
			return EPBRMagicOutlinerCategory::Levels;
		}
		if (Type.Contains(TEXT("StaticMesh")) || Type.Contains(TEXT("Mesh")) || Type.Contains(TEXT("模型"))
			|| Name.Contains(TEXT("SM_")) || Name.Contains(TEXT("StaticMesh")))
		{
			return EPBRMagicOutlinerCategory::Models;
		}
		return EPBRMagicOutlinerCategory::All;
	}

	void DrawItemGlyph(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, TSharedPtr<FPBRMagicOutlinerItem> Item, const FPBRMagicTheme& Theme) const
	{
		const EPBRMagicOutlinerCategory Category = ResolveGlyphCategory(Item);
		const bool bIsGroup = Item.IsValid() && (!Item->Children.IsEmpty() || Item->TypeText == TEXT("组"));
		const FLinearColor Accent = Category == EPBRMagicOutlinerCategory::Lights
			? Theme.Warning
			: (Category == EPBRMagicOutlinerCategory::Cameras ? Theme.Primary : ColorFromText(Item.IsValid() ? Item->TypeText : FString(), Theme));
		DrawRoundedBox(OutDrawElements, Geometry, Layer, Position, Size, Theme.Background, 4.0f, Theme.Border, 1.0f);
		const FVector2D P = Position + FVector2D(7.0f, 7.0f);
		const float W = FMath::Max(10.0f, Size.X - 14.0f);
		const float H = FMath::Max(10.0f, Size.Y - 14.0f);
		if (bIsGroup)
		{
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(1.0f, 4.0f), FVector2D(W * 0.46f, 3.0f), Theme.Warning);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(1.0f, 7.0f), FVector2D(W - 2.0f, H - 10.0f), WithAlpha(Theme.Primary, 0.30f));
			DrawBox(OutDrawElements, Geometry, Layer + 2, P + FVector2D(4.0f, 10.0f), FVector2D(W - 8.0f, 3.0f), Theme.Primary);
			DrawBox(OutDrawElements, Geometry, Layer + 2, P + FVector2D(W * 0.55f, 10.0f), FVector2D(3.0f, H - 13.0f), WithAlpha(Theme.Warning, 0.70f));
		}
		else if (Category == EPBRMagicOutlinerCategory::Lights)
		{
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(2.0f, 2.0f), FVector2D(W - 4.0f, 3.0f), Accent);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(W * 0.28f, 7.0f), FVector2D(W * 0.44f, H * 0.28f), WithAlpha(Accent, 0.55f));
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(W * 0.46f, H * 0.55f), FVector2D(3.0f, H * 0.26f), Theme.Primary);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(W * 0.30f, H - 4.0f), FVector2D(W * 0.40f, 3.0f), Theme.Primary);
		}
		else if (Category == EPBRMagicOutlinerCategory::Cameras)
		{
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(1.0f, 5.0f), FVector2D(W * 0.58f, H - 9.0f), Theme.Primary);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(W * 0.62f, 7.0f), FVector2D(W * 0.30f, H - 13.0f), WithAlpha(Theme.Primary, 0.65f));
			DrawBox(OutDrawElements, Geometry, Layer + 2, P + FVector2D(W * 0.22f, H * 0.40f), FVector2D(4.0f, 4.0f), Theme.Background);
		}
		else if (Category == EPBRMagicOutlinerCategory::Blueprints)
		{
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(1.0f, 1.0f), FVector2D(7.0f, 7.0f), Theme.Primary);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(W - 8.0f, H - 8.0f), FVector2D(7.0f, 7.0f), Theme.Selection);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(8.0f, 5.0f), FVector2D(W - 16.0f, 2.0f), Accent);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(W - 6.0f, 7.0f), FVector2D(2.0f, H - 15.0f), Accent);
		}
		else if (Category == EPBRMagicOutlinerCategory::Levels)
		{
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(3.0f, 3.0f), FVector2D(W - 6.0f, 3.0f), Accent);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(1.0f, H * 0.46f), FVector2D(W - 2.0f, 3.0f), Theme.Primary);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(5.0f, H - 5.0f), FVector2D(W - 10.0f, 3.0f), Theme.Selection);
		}
		else
		{
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(2.0f, 3.0f), FVector2D(5.0f, 5.0f), Theme.Primary);
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(W - 7.0f, 3.0f), FVector2D(5.0f, 5.0f), WithAlpha(Accent, 0.85f));
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(2.0f, H - 8.0f), FVector2D(5.0f, 5.0f), WithAlpha(Theme.Warning, 0.80f));
			DrawBox(OutDrawElements, Geometry, Layer + 1, P + FVector2D(W - 7.0f, H - 8.0f), FVector2D(5.0f, 5.0f), WithAlpha(Theme.Primary, 0.48f));
			DrawBox(OutDrawElements, Geometry, Layer + 2, P + FVector2D(7.0f, H * 0.5f), FVector2D(W - 14.0f, 2.0f), WithAlpha(Theme.Primary, 0.55f));
		}
	}

	bool ItemPassesSearch(const TSharedPtr<FPBRMagicOutlinerItem>& Item) const
	{
		if (SearchText.IsEmpty())
		{
			return true;
		}
		if (!Item.IsValid())
		{
			return false;
		}

		const FString Needle = SearchText.ToLower();
		return Item->DisplayName.ToLower().Contains(Needle)
			|| Item->TypeText.ToLower().Contains(Needle)
			|| Item->DetailText.ToLower().Contains(Needle)
			|| (Item->MeshComponent.IsValid() && Item->MeshComponent->GetName().ToLower().Contains(Needle));
	}

	bool ItemOrChildPassesSearch(const TSharedPtr<FPBRMagicOutlinerItem>& Item) const
	{
		if (ItemPassesSearch(Item))
		{
			return true;
		}
		if (!Item.IsValid())
		{
			return false;
		}
		for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
		{
			if (ItemOrChildPassesSearch(Child))
			{
				return true;
			}
		}
		return false;
	}

	void BuildVisibleRows(TArray<TPair<TSharedPtr<FPBRMagicOutlinerItem>, int32>>& OutRows) const
	{
		TFunction<void(const TSharedPtr<FPBRMagicOutlinerItem>&, int32)> AddVisibleItem =
			[this, &OutRows, &AddVisibleItem](const TSharedPtr<FPBRMagicOutlinerItem>& Item, int32 Depth)
		{
			if (!ItemOrChildPassesSearch(Item))
			{
				return;
			}
			OutRows.Add(TPair<TSharedPtr<FPBRMagicOutlinerItem>, int32>(Item, Depth));
			const bool bShowChildren = Item.IsValid() && (Item->bExpanded || !SearchText.IsEmpty());
			if (!bShowChildren)
			{
				return;
			}
			for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
			{
				AddVisibleItem(Child, Depth + 1);
			}
		};

		for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : OwnerWindow->RootItems)
		{
			AddVisibleItem(Root, 0);
		}
	}

	void SyncRowScrollToSceneSelection(const TArray<TPair<TSharedPtr<FPBRMagicOutlinerItem>, int32>>& VisibleItems) const
	{
		if (!OwnerWindow)
		{
			return;
		}

		const FString& SelectionSignature = OwnerWindow->LastEditorSelectionSignature;
		if (SelectionSignature.IsEmpty() || SelectionSignature == LastPaintSelectionSignature)
		{
			return;
		}

		LastPaintSelectionSignature = SelectionSignature;
		if (OwnerWindow->ActiveTreeItem.IsValid())
		{
			for (int32 Index = 0; Index < VisibleItems.Num(); ++Index)
			{
				if (VisibleItems[Index].Key == OwnerWindow->ActiveTreeItem)
				{
					const float HalfPage = FMath::Max(0.0f, FMath::FloorToFloat(VisibleRowCapacity * 0.5f));
					const float MaxOffset = FMath::Max(0.0f, static_cast<float>(VisibleItems.Num()) - VisibleRowCapacity);
					RowScrollOffset = FMath::Clamp(static_cast<float>(Index) - HalfPage, 0.0f, MaxOffset);
					return;
				}
			}
		}
		for (int32 Index = 0; Index < VisibleItems.Num(); ++Index)
		{
			if (OwnerWindow->IsItemRepresentedInEditorSelection(VisibleItems[Index].Key))
			{
				const float HalfPage = FMath::Max(0.0f, FMath::FloorToFloat(VisibleRowCapacity * 0.5f));
				const float MaxOffset = FMath::Max(0.0f, static_cast<float>(VisibleItems.Num()) - VisibleRowCapacity);
				RowScrollOffset = FMath::Clamp(static_cast<float>(Index) - HalfPage, 0.0f, MaxOffset);
				return;
			}
		}
	}

	static FName GetToolbarIconBrush(EPBRMagicPaintHitAction Action)
	{
		switch (Action)
		{
		case EPBRMagicPaintHitAction::Refresh:
		case EPBRMagicPaintHitAction::ExposureReset:
			return TEXT("PBRStudio.Toolbar.Refresh");
		case EPBRMagicPaintHitAction::SelectChecked:
		case EPBRMagicPaintHitAction::SelectActors:
		case EPBRMagicPaintHitAction::SelectCheckedList:
			return TEXT("PBRStudio.Toolbar.Select");
		case EPBRMagicPaintHitAction::AddCurrentSelection:
			return TEXT("PBRStudio.Toolbar.Add");
		case EPBRMagicPaintHitAction::ToggleIsolation:
			return TEXT("PBRStudio.Toolbar.Show");
		case EPBRMagicPaintHitAction::ToggleCheckedVisibility:
			return TEXT("PBRStudio.Toolbar.Hide");
		case EPBRMagicPaintHitAction::ShortcutSettings:
			return TEXT("PBRStudio.Toolbar.Settings");
		case EPBRMagicPaintHitAction::OpenMaterial:
			return TEXT("PBRStudio.Toolbar.Open");
		case EPBRMagicPaintHitAction::ClearChecked:
		case EPBRMagicPaintHitAction::ClearCheckedList:
			return TEXT("PBRStudio.Toolbar.Clear");
		default:
			return NAME_None;
		}
	}

	void DrawButton(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FString& Icon, const FString& Label, EPBRMagicPaintHitAction Action, const FPBRMagicTheme& Theme, bool bActive = false) const
	{
		const FSlateRect ButtonRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y);
		const bool bHovered = HoveredAction == Action && Action != EPBRMagicPaintHitAction::None && RectMatches(HoveredRect, ButtonRect);
		const bool bPressed = PressedAction == Action && Action != EPBRMagicPaintHitAction::None && RectMatches(PressedRect, ButtonRect);
		const FVector2D PressOffset = bPressed ? FVector2D(1.0f, 1.0f) : FVector2D::ZeroVector;
		const FVector2D DrawPos = Position + PressOffset;
		const FVector2D DrawSize = Size - (bPressed ? FVector2D(1.0f, 1.0f) : FVector2D::ZeroVector);
		const FLinearColor BaseFill = bActive ? Theme.DropZone : Theme.PanelRaised;
		const FLinearColor HoverFill = MixColor(BaseFill, Theme.Primary, 0.12f);
		const FLinearColor PressFill = MixColor(BaseFill, Theme.Background, 0.28f);
		const FLinearColor FillColor = bPressed ? PressFill : (bHovered ? HoverFill : BaseFill);
		const FLinearColor BorderColor = bActive ? Theme.Selection : (bPressed ? Theme.Primary : (bHovered ? Theme.PrimarySoft : Theme.Border));
		DrawRoundedBox(OutDrawElements, Geometry, Layer, DrawPos, DrawSize, FillColor, 5.0f, BorderColor, bHovered || bPressed || bActive ? 1.25f : 1.0f);
		const FLinearColor IconColor = bActive || bHovered || bPressed ? Theme.Selection : Theme.Primary;
		const FName IconBrush = GetToolbarIconBrush(Action);
		if (!IconBrush.IsNone())
		{
			DrawIcon(OutDrawElements, Geometry, Layer + 1, DrawPos + FVector2D(9.0f, (Size.Y - 16.0f) * 0.5f), FVector2D(16.0f, 16.0f), IconBrush, IconColor);
		}
		else
		{
			DrawText(OutDrawElements, Geometry, Layer + 1, DrawPos + FVector2D(9.0f, 7.0f), Icon, FAppStyle::GetFontStyle("NormalFontBold"), IconColor);
		}
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, DrawPos + FVector2D(31.0f, 8.0f), Size.X - 38.0f, Label, FAppStyle::GetFontStyle("SmallFont"), bHovered || bPressed ? Theme.Text : Theme.TextMuted);
		AddHit(Position, Size, Action);
	}

	void DrawTopBar(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32& Layer, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		const float HeaderH = 78.0f;
		DrawBox(OutDrawElements, Geometry, Layer++, FVector2D(0.0f, 0.0f), Size, Theme.Background);
		DrawBox(OutDrawElements, Geometry, Layer++, FVector2D(0, 0), FVector2D(Size.X, HeaderH), Theme.Background);
		DrawBox(OutDrawElements, Geometry, Layer++, FVector2D(18, 16), FVector2D(28, 24), Theme.PanelRaised);
		DrawText(OutDrawElements, Geometry, Layer, FVector2D(25, 20), TEXT("AR"), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawText(OutDrawElements, Geometry, Layer, FVector2D(58, 20), TEXT("AR Studio / PBR Studio"), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		DrawText(OutDrawElements, Geometry, Layer, FVector2D(210, 22), TEXT("魔法大纲 / Magic Outliner"), FAppStyle::GetFontStyle("SmallFontBold"), Theme.TextMuted);
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(Size.X - 330.0f, 16.0f), FVector2D(92.0f, 28.0f), TEXT("UI"), OwnerWindow->bUseClassicSkin ? TEXT("经典") : TEXT("自绘"), EPBRMagicPaintHitAction::ToggleClassicSkin, Theme, OwnerWindow->bUseClassicSkin);
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(Size.X - 230.0f, 16.0f), FVector2D(92.0f, 28.0f), TEXT("◎"), OwnerWindow->bCompactMode ? TEXT("精简") : TEXT("标准"), EPBRMagicPaintHitAction::ToggleCompact, Theme, OwnerWindow->bCompactMode);
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(Size.X - 130.0f, 16.0f), FVector2D(112.0f, 28.0f), TEXT("●"), OwnerWindow->GetThemeButtonText().ToString(), EPBRMagicPaintHitAction::ThemeCycle, Theme);

		const TArray<FPBRMagicTopTab> Tabs = {
			{ EPBRMagicOutlinerCategory::Models, PBRText(TEXT("ModelsTab"), TEXT("模型"), TEXT("Models")), "PBRStudio.Icon.TextureSuite" },
			{ EPBRMagicOutlinerCategory::Materials, PBRText(TEXT("MaterialsTab"), TEXT("材质"), TEXT("Materials")), "PBRStudio.Icon.MaterialVault" },
			{ EPBRMagicOutlinerCategory::Lights, PBRText(TEXT("LightsTab"), TEXT("灯光"), TEXT("Lights")), "PBRStudio.Icon.BatchAdjust" },
			{ EPBRMagicOutlinerCategory::Cameras, PBRText(TEXT("CamerasTabShort"), TEXT("相机/后期"), TEXT("Camera/Post")), "PBRStudio.Icon.CameraPost" },
			{ EPBRMagicOutlinerCategory::Blueprints, PBRText(TEXT("BlueprintsTab"), TEXT("蓝图"), TEXT("Blueprints")), "PBRStudio.Icon.BatchAdjust" },
			{ EPBRMagicOutlinerCategory::Levels, PBRText(TEXT("LevelsTab"), TEXT("关卡"), TEXT("Levels")), "PBRStudio.Icon.MagicOutliner" },
			{ EPBRMagicOutlinerCategory::All, PBRText(TEXT("AllTab"), TEXT("全部"), TEXT("All")), "PBRStudio.Icon.MagicOutliner" }
		};

		const FVector2D NavPos(20.0f, 50.0f);
		DrawBox(OutDrawElements, Geometry, Layer++, FVector2D(0.0f, NavPos.Y - 2.0f), FVector2D(Size.X, 48.0f), Theme.Panel);
		float X = NavPos.X + 22.0f;
		for (const FPBRMagicTopTab& Tab : Tabs)
		{
			const bool bActive = OwnerWindow->ActiveCategory == Tab.Category;
			const FVector2D TabPos(X, NavPos.Y + 6.0f);
			const FVector2D TabSize(132.0f, 32.0f);
			if (bActive)
			{
				DrawRoundedBox(OutDrawElements, Geometry, Layer, TabPos, TabSize, Theme.DropZone, 5.0f, Theme.PrimarySoft, 1.0f);
			}
			DrawIcon(OutDrawElements, Geometry, Layer + 1, TabPos + FVector2D(12.0f, 8.0f), FVector2D(16.0f, 16.0f), Tab.Icon, bActive ? Theme.Selection : Theme.Primary);
			DrawTextInRect(OutDrawElements, Geometry, Layer + 1, TabPos + FVector2D(38.0f, 8.0f), TabSize.X - 46.0f, Tab.Label.ToString(), FAppStyle::GetFontStyle("SmallFontBold"), bActive ? Theme.Text : Theme.TextMuted);
			AddHit(TabPos, TabSize, EPBRMagicPaintHitAction::TopTab, Tab.Category);
			X += TabSize.X + 12.0f;
		}
		Layer += 2;
	}

	void DrawWorkspace(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32& Layer, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		const float Top = 110.0f;
		const float Bottom = 30.0f;
		const float Gap = 8.0f;
		const float LeftW = 220.0f;
		const float RightW = 370.0f;
		const float WorkH = FMath::Max(240.0f, Size.Y - Top - Bottom - 8.0f);
		const FVector2D LeftPos(10.0f, Top);
		const FVector2D MainPos(LeftPos.X + LeftW + Gap, Top);
		const FVector2D RightPos(Size.X - RightW - 10.0f, Top);
		const float MainW = FMath::Max(420.0f, RightPos.X - MainPos.X - Gap);
		const float ShellTop = Top - 8.0f;
		const float ShellBottom = Size.Y - Bottom + 1.0f;
		DrawBox(OutDrawElements, Geometry, Layer++, FVector2D(0.0f, ShellTop), FVector2D(Size.X, ShellBottom - ShellTop), Theme.Panel);

		DrawLeftPanel(OutDrawElements, Geometry, Layer, LeftPos, FVector2D(LeftW, WorkH), Theme);
		DrawMainPanel(OutDrawElements, Geometry, Layer, MainPos, FVector2D(MainW, WorkH), Theme);
		DrawInspector(OutDrawElements, Geometry, Layer, RightPos, FVector2D(RightW, WorkH), Theme);
	}

	void DrawLeftPanel(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32& Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		DrawBox(OutDrawElements, Geometry, Layer++, Position - FVector2D(2.0f, 2.0f), Size + FVector2D(4.0f, 4.0f), Theme.Panel);
		DrawInsetPanelFrame(OutDrawElements, Geometry, Layer++, Position, Size, Theme);
		DrawText(OutDrawElements, Geometry, Layer, Position + FVector2D(12, 12), TEXT("智能分组"), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		DrawText(OutDrawElements, Geometry, Layer, Position + FVector2D(12, 30), TEXT("按中文关键词快速勾选"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawText(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 32, 18), TEXT("⟳"), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Primary);
		AddHit(Position + FVector2D(Size.X - 40, 10), FVector2D(30, 30), EPBRMagicPaintHitAction::Refresh);

		float Y = Position.Y + 58.0f;
		NameListRect = FSlateRect(Position.X + 12.0f, Y, Position.X + Size.X - 12.0f, Position.Y + Size.Y - 12.0f);
		NameItemCount = OwnerWindow->NameCheckListItems.Num();
		NameVisibleCapacity = FMath::Max(0.0f, FMath::FloorToFloat((Size.Y - 78.0f) / 36.0f));
		NameScrollOffset = FMath::Clamp(NameScrollOffset, 0.0f, FMath::Max(0.0f, static_cast<float>(NameItemCount) - NameVisibleCapacity));
		const int32 StartIndex = FMath::Clamp(FMath::FloorToInt(NameScrollOffset), 0, FMath::Max(0, NameItemCount - 1));
		const int32 MaxGroups = FMath::Min(NameItemCount - StartIndex, FMath::FloorToInt(NameVisibleCapacity));
		for (int32 RowIndex = 0; RowIndex < MaxGroups; ++RowIndex)
		{
			const int32 Index = StartIndex + RowIndex;
			const TSharedPtr<FPBRNameCheckListItem>& Item = OwnerWindow->NameCheckListItems[Index];
			const bool bChecked = Item.IsValid() && OwnerWindow->GetNameCheckState(Item->Name) == ECheckBoxState::Checked;
			const FVector2D RowPos(Position.X + 12.0f, Y);
			const FVector2D RowSize(Size.X - 24.0f, 30.0f);
			DrawRoundedBox(OutDrawElements, Geometry, Layer, RowPos, RowSize, bChecked ? Theme.DropZone : Theme.TableRow, 4.0f, bChecked ? Theme.PrimarySoft : Theme.Border, 1.0f);
			DrawRoundedBox(OutDrawElements, Geometry, Layer + 1, RowPos + FVector2D(8, 7), FVector2D(16, 16), bChecked ? Theme.Selection : Theme.Background, 3.0f, bChecked ? Theme.Selection : Theme.Border, 1.0f);
			DrawBox(OutDrawElements, Geometry, Layer + 1, RowPos + FVector2D(32, 8), FVector2D(14, 14), Theme.Primary);
			DrawTextInRect(OutDrawElements, Geometry, Layer + 2, RowPos + FVector2D(54, 8), RowSize.X - 92.0f, Item.IsValid() ? Item->Name : FString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
			DrawText(OutDrawElements, Geometry, Layer + 2, RowPos + FVector2D(RowSize.X - 34, 8), Item.IsValid() ? FString::FromInt(Item->MatchCount) : FString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Primary);
			AddHit(RowPos, RowSize, EPBRMagicPaintHitAction::NameGroup, EPBRMagicOutlinerCategory::All, nullptr, Item);
			Y += 36.0f;
		}
		if (NameItemCount > FMath::FloorToInt(NameVisibleCapacity))
		{
			DrawTextInRect(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(12.0f, Size.Y - 20.0f), Size.X - 24.0f, FString::Printf(TEXT("%d-%d / %d"), StartIndex + 1, StartIndex + MaxGroups, NameItemCount), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		}
		Layer += 3;
	}

	void DrawMainPanel(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32& Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		DrawBox(OutDrawElements, Geometry, Layer++, Position - FVector2D(2.0f, 2.0f), Size + FVector2D(4.0f, 4.0f), Theme.Panel);
		DrawInsetPanelFrame(OutDrawElements, Geometry, Layer++, Position, Size, Theme);
		float X = Position.X + 12.0f;
		const float ToolY = Position.Y + 10.0f;
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(X, ToolY), FVector2D(104, 30), TEXT("⟳"), TEXT("刷新场景"), EPBRMagicPaintHitAction::Refresh, Theme); X += 112.0f;
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(X, ToolY), FVector2D(132, 30), TEXT("□"), TEXT("场景中选择勾选"), EPBRMagicPaintHitAction::SelectChecked, Theme); X += 140.0f;
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(X, ToolY), FVector2D(118, 30), TEXT("+"), TEXT("添加当前选择"), EPBRMagicPaintHitAction::AddCurrentSelection, Theme); X += 126.0f;
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(X, ToolY), FVector2D(118, 30), TEXT("◎"), TEXT("孤立/退出"), EPBRMagicPaintHitAction::ToggleIsolation, Theme, OwnerWindow->bIsolationActive); X += 126.0f;
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(X, ToolY), FVector2D(116, 30), TEXT("◌"), TEXT("隐藏/显示"), EPBRMagicPaintHitAction::ToggleCheckedVisibility, Theme, OwnerWindow->bCheckedActorsHidden); X += 124.0f;
		if (X + 184.0f < Position.X + Size.X - 170.0f)
		{
			DrawButton(OutDrawElements, Geometry, Layer, FVector2D(X, ToolY), FVector2D(86, 30), TEXT("⇄"), TEXT("反向"), EPBRMagicPaintHitAction::InvertChecked, Theme); X += 94.0f;
			DrawButton(OutDrawElements, Geometry, Layer, FVector2D(X, ToolY), FVector2D(90, 30), TEXT("⚙"), TEXT("快捷键"), EPBRMagicPaintHitAction::ShortcutSettings, Theme);
		}
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(Position.X + Size.X - 196.0f, ToolY), FVector2D(100, 30), OwnerWindow->bAutoSelectCheckedActors ? TEXT("✓") : TEXT("×"), TEXT("自动选择"), EPBRMagicPaintHitAction::ToggleAutoSelect, Theme, OwnerWindow->bAutoSelectCheckedActors);
		DrawButton(OutDrawElements, Geometry, Layer, FVector2D(Position.X + Size.X - 88.0f, ToolY), FVector2D(76, 30), TEXT("×"), TEXT("清空"), EPBRMagicPaintHitAction::ClearChecked, Theme);

		const FVector2D ModePos(Position.X + 12.0f, Position.Y + 48.0f);
		const TArray<EPBRMagicOutlinerMode> Modes = {
			EPBRMagicOutlinerMode::Group,
			EPBRMagicOutlinerMode::Type,
			EPBRMagicOutlinerMode::Material,
			EPBRMagicOutlinerMode::Reference,
			EPBRMagicOutlinerMode::State
		};
		float ModeX = ModePos.X;
		for (EPBRMagicOutlinerMode Mode : Modes)
		{
			const bool bActiveMode = OwnerWindow->ActiveMode == Mode;
			const FVector2D ModeSize(92.0f, 26.0f);
			DrawRoundedBox(OutDrawElements, Geometry, Layer, FVector2D(ModeX, ModePos.Y), ModeSize, bActiveMode ? Theme.DropZone : Theme.PanelRaised, 5.0f, bActiveMode ? Theme.Primary : Theme.Border, 1.0f);
			DrawTextInRect(OutDrawElements, Geometry, Layer + 1, FVector2D(ModeX + 10.0f, ModePos.Y + 7.0f), ModeSize.X - 20.0f, OwnerWindow->GetModeLabel(Mode).ToString(), FAppStyle::GetFontStyle("SmallFont"), bActiveMode ? Theme.Text : Theme.TextMuted);
			AddModeHit(FVector2D(ModeX, ModePos.Y), ModeSize, Mode);
			ModeX += ModeSize.X + 6.0f;
		}

		const FVector2D SearchPos(Position.X + 12.0f, Position.Y + 82.0f);
		const FVector2D SearchSize(FMath::Max(260.0f, Size.X - 190.0f), 30.0f);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, SearchPos, SearchSize, bSearchActive ? Theme.DropZone : Theme.Background, 5.0f, bSearchActive ? Theme.Primary : Theme.Border, 1.0f);
		DrawText(OutDrawElements, Geometry, Layer + 1, SearchPos + FVector2D(12, 8), TEXT("⌕"), FAppStyle::GetFontStyle("SmallFontBold"), bSearchActive ? Theme.Selection : Theme.Primary);
		const FString SearchDisplay = SearchText.IsEmpty() ? TEXT("搜索名称、类型、使用、路径...") : SearchText;
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, SearchPos + FVector2D(34, 8), SearchSize.X - 48.0f, SearchDisplay, FAppStyle::GetFontStyle("SmallFont"), SearchText.IsEmpty() ? Theme.TextMuted : Theme.Text);
		if (bSearchActive)
		{
			const float CaretX = FMath::Clamp(34.0f + EstimateTextWidth(SearchText, FAppStyle::GetFontStyle("SmallFont")), 34.0f, SearchSize.X - 12.0f);
			DrawBox(OutDrawElements, Geometry, Layer + 2, SearchPos + FVector2D(CaretX, 7), FVector2D(1.0f, 16.0f), Theme.Selection);
		}
		AddHit(SearchPos, SearchSize, EPBRMagicPaintHitAction::SearchBox);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 164, 82), FVector2D(152, 30), Theme.PanelRaised, 5.0f, Theme.Border, 1.0f);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(Size.X - 150, 90), 132.0f, FString::Printf(TEXT("已选中 %d 项"), OwnerWindow->CachedCheckedCount), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Primary);

		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12, 128), Size.X - 116.0f, OwnerWindow->GetScenePanelTitleText().ToString(), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12, 147), Size.X - 116.0f, OwnerWindow->GetScenePanelSummaryText().ToString(), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 90, 128), FVector2D(78, 24), Theme.Background, 4.0f, Theme.Border, 1.0f);
		DrawText(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(Size.X - 80, 133), FString::Printf(TEXT("%d checked"), OwnerWindow->CachedCheckedCount), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Primary);

		const FVector2D HeadPos(Position.X + 12.0f, Position.Y + 170.0f);
		const FVector2D HeadSize(Size.X - 24.0f, 38.0f);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, HeadPos, HeadSize, Theme.PanelRaised, 5.0f, Theme.Border, 1.0f);
		DrawText(OutDrawElements, Geometry, Layer + 1, HeadPos + FVector2D(44, 10), TEXT("预览"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawText(OutDrawElements, Geometry, Layer + 1, HeadPos + FVector2D(118, 10), TEXT("名称"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawText(OutDrawElements, Geometry, Layer + 1, HeadPos + FVector2D(Size.X * 0.52f, 10), TEXT("类型"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawText(OutDrawElements, Geometry, Layer + 1, HeadPos + FVector2D(Size.X * 0.68f, 10), TEXT("数量"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawText(OutDrawElements, Geometry, Layer + 1, HeadPos + FVector2D(Size.X * 0.76f, 10), TEXT("使用 / 路径"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);

		float RowY = HeadPos.Y + 48.0f;
		const float RowH = OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Materials ? 58.0f : 42.0f;
		VisibleRowCapacity = FMath::Max(0.0f, FMath::FloorToFloat((Position.Y + Size.Y - RowY - 8.0f) / RowH));
		MainRowsRect = FSlateRect(Position.X + 12.0f, RowY, Position.X + Size.X - 12.0f, Position.Y + Size.Y - 8.0f);
		TArray<TPair<TSharedPtr<FPBRMagicOutlinerItem>, int32>> VisibleItems;
		BuildVisibleRows(VisibleItems);
		VisibleRowCount = VisibleItems.Num();
		SyncRowScrollToSceneSelection(VisibleItems);
		const int32 StartIndex = FMath::Clamp(FMath::FloorToInt(RowScrollOffset), 0, FMath::Max(0, VisibleItems.Num() - 1));
		const int32 MaxRows = FMath::Min(VisibleItems.Num() - StartIndex, FMath::FloorToInt(VisibleRowCapacity));
		for (int32 RowIndex = 0; RowIndex < MaxRows; ++RowIndex)
		{
			const TSharedPtr<FPBRMagicOutlinerItem>& Item = VisibleItems[StartIndex + RowIndex].Key;
			const int32 Depth = VisibleItems[StartIndex + RowIndex].Value;
			DrawRow(OutDrawElements, Geometry, Layer, FVector2D(Position.X + 12.0f, RowY + RowIndex * RowH), FVector2D(Size.X - 24.0f, RowH - 5.0f), Item, Depth, Theme);
		}
		if (VisibleItems.IsEmpty())
		{
			DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(30, HeadPos.Y + 66.0f - Position.Y), Size.X - 60.0f, SearchText.IsEmpty() ? TEXT("当前分类没有可显示项目。") : TEXT("没有匹配的项目。"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		}
		if (VisibleItems.Num() > MaxRows)
		{
			DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(Size.X - 126, Size.Y - 24), 108.0f, FString::Printf(TEXT("%d / %d"), StartIndex + MaxRows, VisibleItems.Num()), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		}
		Layer += 5;
	}

	void DrawRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, TSharedPtr<FPBRMagicOutlinerItem> Item, int32 Depth, const FPBRMagicTheme& Theme) const
	{
		const bool bChecked = OwnerWindow->GetItemCheckState(Item) == ECheckBoxState::Checked;
		const bool bActive = OwnerWindow->ActiveTreeItem == Item;
		const bool bSceneSelected = OwnerWindow->IsItemRepresentedInEditorSelection(Item);
		const float Indent = static_cast<float>(Depth) * 28.0f;
		const bool bHasChildren = Item.IsValid() && !Item->Children.IsEmpty();
		const float ContentIndent = Indent + (bHasChildren ? 18.0f : 0.0f);
		const FLinearColor RowFill = bChecked
			? Theme.DropZone
			: (bActive || bSceneSelected ? Theme.TableRowHover : Theme.TableRow);
		const FLinearColor RowBorder = bActive
			? Theme.Selection
			: (bChecked ? Theme.PrimarySoft : Theme.Border);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, Position, Size, RowFill, 4.0f, RowBorder, 1.0f);
		DrawBox(OutDrawElements, Geometry, Layer + 1, Position, FVector2D(bSceneSelected ? 4.0f : 3.0f, Size.Y), bSceneSelected ? Theme.Selection : (bChecked ? Theme.Selection : (bActive ? Theme.Primary : Theme.Border)));
		if (bHasChildren)
		{
			DrawText(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(12.0f + Indent, 10.0f), Item->bExpanded || !SearchText.IsEmpty() ? TEXT("▼") : TEXT("▶"), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Primary);
			AddHit(Position + FVector2D(8.0f + Indent, 4.0f), FVector2D(18.0f, Size.Y - 8.0f), EPBRMagicPaintHitAction::RowExpand, EPBRMagicOutlinerCategory::All, Item);
		}
		const bool bMaterialRow = Item.IsValid() && Item->Material.IsValid();
		const float RowCenterY = Position.Y + Size.Y * 0.5f;
		DrawRoundedBox(OutDrawElements, Geometry, Layer + 1, FVector2D(Position.X + 20 + ContentIndent, RowCenterY - 8.0f), FVector2D(16, 16), bChecked ? Theme.Selection : Theme.Background, 3.0f, bChecked ? Theme.Selection : Theme.Border, 1.0f);
		if (Item.IsValid() && Item->Material.IsValid())
		{
			DrawMaterialPreview(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(52 + ContentIndent, 6), FVector2D(46, Size.Y - 12), Item, Theme);
		}
		else
		{
			DrawItemGlyph(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(52 + ContentIndent, 5), FVector2D(34, Size.Y - 10), Item, Theme);
		}
		const float TextY = bMaterialRow ? 13.0f : 11.0f;
		const float NameX = bMaterialRow ? 112.0f + ContentIndent : 94.0f + ContentIndent;
		DrawTextInRect(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(NameX, TextY), Size.X * 0.50f - NameX - 10.0f, Item.IsValid() ? Item->DisplayName : FString(), FAppStyle::GetFontStyle(Depth == 0 ? "SmallFontBold" : "SmallFont"), Theme.Text);
		if (bMaterialRow)
		{
			DrawTextInRect(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(NameX, TextY + 19.0f), Size.X * 0.50f - NameX - 10.0f, Item.IsValid() ? Item->DetailText : FString(), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		}
		DrawTextInRect(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(Size.X * 0.50f, TextY), Size.X * 0.15f, Item.IsValid() ? Item->TypeText : FString(), FAppStyle::GetFontStyle("SmallFont"), Theme.Primary);
		DrawText(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(Size.X * 0.66f, TextY), Item.IsValid() ? FString::FromInt(Item->ActorCount) : FString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
		const bool bShowSceneSelectedText = bSceneSelected && bActive;
		const float DetailWidth = Size.X * 0.25f - (bShowSceneSelectedText ? 92.0f : 20.0f);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(Size.X * 0.74f, TextY), FMath::Max(40.0f, DetailWidth), Item.IsValid() ? Item->DetailText : FString(), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		if (bShowSceneSelectedText)
		{
			DrawTextInRect(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(Size.X - 58.0f, TextY), 50.0f, TEXT("已选"), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
		}
		AddHit(Position + FVector2D(14 + ContentIndent, 6), FVector2D(30, Size.Y - 12), EPBRMagicPaintHitAction::RowToggle, EPBRMagicOutlinerCategory::All, Item);
		AddHit(Position + FVector2D(46 + ContentIndent, 0), FVector2D(Size.X - 46 - ContentIndent, Size.Y), EPBRMagicPaintHitAction::RowSelect, EPBRMagicOutlinerCategory::All, Item);
	}

	void DrawValueStepper(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FString& Label, const FString& Value, EPBRMagicPaintHitAction DownAction, EPBRMagicPaintHitAction UpAction, const FPBRMagicTheme& Theme) const
	{
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position, Size.X - 110.0f, Label, FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 172.0f, 0), 72.0f, Value, FAppStyle::GetFontStyle("SmallFont"), Theme.Selection);
		DrawButton(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 92.0f, -7.0f), FVector2D(38.0f, 28.0f), TEXT("-"), TEXT(""), DownAction, Theme);
		DrawButton(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 46.0f, -7.0f), FVector2D(38.0f, 28.0f), TEXT("+"), TEXT(""), UpAction, Theme);
	}

	void DrawMaterialScalarStepper(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicEditableMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const TOptional<float> OptionalValue = OwnerWindow->GetEditableMaterialScalar(Parameter.ParameterName, Parameter.DefaultValue);
		const float Value = OptionalValue.IsSet() ? OptionalValue.GetValue() : Parameter.DefaultValue;
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position, Size.X - 112.0f, Parameter.GetLabel().ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 174.0f, 0), 76.0f, FormatMagicEditableMaterialScalar(Value, Parameter.StepValue), FAppStyle::GetFontStyle("SmallFont"), Theme.Selection);

		const FVector2D ButtonSize(34.0f, 24.0f);
		const FVector2D DownPos = Position + FVector2D(Size.X - 88.0f, -5.0f);
		const FVector2D UpPos = Position + FVector2D(Size.X - 44.0f, -5.0f);
		auto DrawParamButton = [this, &OutDrawElements, &Geometry, Layer, &Theme, ButtonSize](const FVector2D& ButtonPos, const FString& Text, EPBRMagicPaintHitAction Action, const FPBRMagicEditableMaterialParameter& Param)
		{
			const FSlateRect ButtonRect(ButtonPos.X, ButtonPos.Y, ButtonPos.X + ButtonSize.X, ButtonPos.Y + ButtonSize.Y);
			const bool bHovered = HoveredAction == Action && RectMatches(HoveredRect, ButtonRect);
			const bool bPressed = PressedAction == Action && RectMatches(PressedRect, ButtonRect);
			const FLinearColor Fill = bPressed ? MixColor(Theme.PanelRaised, Theme.Background, 0.35f) : (bHovered ? MixColor(Theme.PanelRaised, Theme.Primary, 0.12f) : Theme.PanelRaised);
			DrawRoundedBox(OutDrawElements, Geometry, Layer, ButtonPos, ButtonSize, Fill, 4.0f, bHovered || bPressed ? Theme.PrimarySoft : Theme.Border, 1.0f);
			DrawTextInRect(OutDrawElements, Geometry, Layer + 1, ButtonPos + FVector2D(0.0f, 5.0f), ButtonSize.X, Text, FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
			AddMaterialScalarHit(ButtonPos, ButtonSize, Action, Param);
		};
		DrawParamButton(DownPos, TEXT("-"), EPBRMagicPaintHitAction::MaterialScalarDown, Parameter);
		DrawParamButton(UpPos, TEXT("+"), EPBRMagicPaintHitAction::MaterialScalarUp, Parameter);
	}

	void DrawMaterialSwitchRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicEditableMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const bool bEnabled = OwnerWindow->GetEditableMaterialSwitch(Parameter.ParameterName, Parameter.bDefaultSwitchValue);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position, Size.X - 92.0f, Parameter.GetLabel().ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		const FVector2D TogglePos = Position + FVector2D(Size.X - 82.0f, -5.0f);
		const FVector2D ToggleSize(74.0f, 24.0f);
		const FSlateRect ToggleRect(TogglePos.X, TogglePos.Y, TogglePos.X + ToggleSize.X, TogglePos.Y + ToggleSize.Y);
		const bool bHovered = HoveredAction == EPBRMagicPaintHitAction::MaterialSwitchToggle && RectMatches(HoveredRect, ToggleRect);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, TogglePos, ToggleSize, bEnabled ? Theme.DropZone : Theme.PanelRaised, 4.0f, bHovered ? Theme.Primary : Theme.Border, 1.0f);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, TogglePos + FVector2D(8.0f, 5.0f), ToggleSize.X - 16.0f, bEnabled ? TEXT("ON") : TEXT("OFF"), FAppStyle::GetFontStyle("SmallFontBold"), bEnabled ? Theme.Selection : Theme.TextMuted);
		AddMaterialSwitchHit(TogglePos, ToggleSize, Parameter);
	}

	void DrawMaterialColorRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicEditableMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const TOptional<float> OptionalR = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 0, Parameter.DefaultColor);
		const TOptional<float> OptionalG = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 1, Parameter.DefaultColor);
		const TOptional<float> OptionalB = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 2, Parameter.DefaultColor);
		const float R = OptionalR.IsSet() ? OptionalR.GetValue() : Parameter.DefaultColor.R;
		const float G = OptionalG.IsSet() ? OptionalG.GetValue() : Parameter.DefaultColor.G;
		const float B = OptionalB.IsSet() ? OptionalB.GetValue() : Parameter.DefaultColor.B;
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position, Size.X - 62.0f, Parameter.GetLabel().ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 50.0f, -3.0f), FVector2D(42.0f, 20.0f), FLinearColor(R, G, B, 1.0f), 4.0f, Theme.Border, 1.0f);
	}

	void DrawDynamicMaterialScalarStepper(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicDynamicMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const TOptional<float> OptionalValue = OwnerWindow->GetEditableMaterialScalar(Parameter.ParameterName, Parameter.ScalarValue);
		const float Value = OptionalValue.IsSet() ? OptionalValue.GetValue() : Parameter.ScalarValue;
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position, Size.X - 112.0f, Parameter.ParameterName.ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 174.0f, 0), 76.0f, FormatMagicEditableMaterialScalar(Value, Parameter.ScalarStep), FAppStyle::GetFontStyle("SmallFont"), Theme.Selection);

		const FVector2D ButtonSize(34.0f, 24.0f);
		const FVector2D DownPos = Position + FVector2D(Size.X - 88.0f, -5.0f);
		const FVector2D UpPos = Position + FVector2D(Size.X - 44.0f, -5.0f);
		auto DrawParamButton = [this, &OutDrawElements, &Geometry, Layer, &Theme, ButtonSize, &Parameter](const FVector2D& ButtonPos, const FString& Text, EPBRMagicPaintHitAction Action)
		{
			const FSlateRect ButtonRect(ButtonPos.X, ButtonPos.Y, ButtonPos.X + ButtonSize.X, ButtonPos.Y + ButtonSize.Y);
			const bool bHovered = HoveredAction == Action && RectMatches(HoveredRect, ButtonRect);
			const bool bPressed = PressedAction == Action && RectMatches(PressedRect, ButtonRect);
			const FLinearColor Fill = bPressed ? MixColor(Theme.PanelRaised, Theme.Background, 0.35f) : (bHovered ? MixColor(Theme.PanelRaised, Theme.Primary, 0.12f) : Theme.PanelRaised);
			DrawRoundedBox(OutDrawElements, Geometry, Layer, ButtonPos, ButtonSize, Fill, 4.0f, bHovered || bPressed ? Theme.PrimarySoft : Theme.Border, 1.0f);
			DrawTextInRect(OutDrawElements, Geometry, Layer + 1, ButtonPos + FVector2D(0.0f, 5.0f), ButtonSize.X, Text, FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
			AddMaterialScalarHit(ButtonPos, ButtonSize, Action, Parameter.ParameterName, Parameter.ScalarMin, Parameter.ScalarMax, Parameter.ScalarValue, Parameter.ScalarStep);
		};
		DrawParamButton(DownPos, TEXT("-"), EPBRMagicPaintHitAction::MaterialScalarDown);
		DrawParamButton(UpPos, TEXT("+"), EPBRMagicPaintHitAction::MaterialScalarUp);
	}

	void DrawDynamicMaterialSwitchRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicDynamicMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const bool bEnabled = OwnerWindow->GetEditableMaterialSwitch(Parameter.ParameterName, Parameter.bSwitchValue);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position, Size.X - 92.0f, Parameter.ParameterName.ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		const FVector2D TogglePos = Position + FVector2D(Size.X - 82.0f, -5.0f);
		const FVector2D ToggleSize(74.0f, 24.0f);
		const FSlateRect ToggleRect(TogglePos.X, TogglePos.Y, TogglePos.X + ToggleSize.X, TogglePos.Y + ToggleSize.Y);
		const bool bHovered = HoveredAction == EPBRMagicPaintHitAction::MaterialSwitchToggle && RectMatches(HoveredRect, ToggleRect);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, TogglePos, ToggleSize, bEnabled ? Theme.DropZone : Theme.PanelRaised, 4.0f, bHovered ? Theme.Primary : Theme.Border, 1.0f);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, TogglePos + FVector2D(8.0f, 5.0f), ToggleSize.X - 16.0f, bEnabled ? TEXT("ON") : TEXT("OFF"), FAppStyle::GetFontStyle("SmallFontBold"), bEnabled ? Theme.Selection : Theme.TextMuted);
		AddMaterialSwitchHit(TogglePos, ToggleSize, Parameter.ParameterName, Parameter.bSwitchValue);
	}

	void DrawDynamicMaterialColorRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicDynamicMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const TOptional<float> OptionalR = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 0, Parameter.ColorValue);
		const TOptional<float> OptionalG = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 1, Parameter.ColorValue);
		const TOptional<float> OptionalB = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 2, Parameter.ColorValue);
		const float R = OptionalR.IsSet() ? OptionalR.GetValue() : Parameter.ColorValue.R;
		const float G = OptionalG.IsSet() ? OptionalG.GetValue() : Parameter.ColorValue.G;
		const float B = OptionalB.IsSet() ? OptionalB.GetValue() : Parameter.ColorValue.B;
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position, Size.X - 62.0f, Parameter.ParameterName.ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawRoundedBox(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 50.0f, -3.0f), FVector2D(42.0f, 20.0f), FLinearColor(R, G, B, 1.0f), 4.0f, Theme.Border, 1.0f);
	}

	void DrawDynamicMaterialTextureRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicDynamicMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const UTexture* Texture = OwnerWindow->GetEditableMaterialTexture(Parameter.ParameterName);
		const FString TextureName = Texture ? Texture->GetName() : TEXT("未指定");
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position, Size.X * 0.52f, Parameter.ParameterName.ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X * 0.54f, 0.0f), Size.X * 0.44f, TextureName, FAppStyle::GetFontStyle("SmallFont"), Texture ? Theme.Selection : Theme.TextMuted);
	}

	void DrawMaterialControls(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		DrawPanelSurface(OutDrawElements, Geometry, Layer, Position, Size, Theme.PanelRaised, 6.0f, Theme.Border, 1.0f);
		DrawText(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 10.0f), TEXT("材质调参"), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		if (!OwnerWindow->GetEditableMaterialInstance())
		{
			DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 36.0f), Size.X - 24.0f, TEXT("点材质槽右侧“调参”打开弹窗编辑当前材质。"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
			return;
		}

		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 34.0f), Size.X - 24.0f, OwnerWindow->GetEditableMaterialNameText().ToString(), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawRoundedBox(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 58.0f), FVector2D(Size.X - 24.0f, 26.0f), Theme.Background, 4.0f, Theme.Border, 1.0f);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(20.0f, 64.0f), Size.X - 40.0f, OwnerWindow->GetEditableMaterialTypeText().ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
		DrawButton(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 96.0f), FVector2D(Size.X - 24.0f, 30.0f), TEXT("打开调参弹窗"), TEXT(""), EPBRMagicPaintHitAction::AdjustMaterial, Theme);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 138.0f), Size.X - 24.0f, TEXT("所有材质参数、贴图、颜色和类型切换都在弹窗中统一调整。"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
	}

	void DrawLightControls(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		DrawPanelSurface(OutDrawElements, Geometry, Layer, Position, Size, Theme.PanelRaised, 6.0f, Theme.Border, 1.0f);
		DrawText(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 10.0f), TEXT("灯光参数 / Light Parameters"), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 32.0f), Size.X - 24.0f, TEXT("勾选灯光后可批量调节，默认不会自动选中场景对象。"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawValueStepper(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 66.0f), FVector2D(Size.X - 24.0f, 24.0f), TEXT("亮度倍率"), FString::Printf(TEXT("%.2fx"), OwnerWindow->LightIntensityMultiplier), EPBRMagicPaintHitAction::LightIntensityDown, EPBRMagicPaintHitAction::LightIntensityUp, Theme);
		DrawValueStepper(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 104.0f), FVector2D(Size.X - 24.0f, 24.0f), TEXT("色温"), FString::Printf(TEXT("%.0fK"), OwnerWindow->LightTemperature), EPBRMagicPaintHitAction::LightTemperatureDown, EPBRMagicPaintHitAction::LightTemperatureUp, Theme);
		DrawBox(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 144.0f), FVector2D(42.0f, 24.0f), OwnerWindow->LightColor);
		DrawButton(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(64.0f, 140.0f), FVector2D(120.0f, 32.0f), TEXT("■"), TEXT("灯光颜色"), EPBRMagicPaintHitAction::LightColor, Theme);
	}

	void DrawCameraControls(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		DrawPanelSurface(OutDrawElements, Geometry, Layer, Position, Size, Theme.PanelRaised, 6.0f, Theme.Border, 1.0f);
		DrawText(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 10.0f), TEXT("相机 / 后期参数"), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		const FString ActorName = OwnerWindow->GetDetailsActor() ? ActorLabel(OwnerWindow->GetDetailsActor()) : TEXT("选择列表中的相机或后期体积盒子。");
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 32.0f), Size.X - 24.0f, ActorName, FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawButton(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 62.0f), FVector2D(112.0f, 32.0f), TEXT("✓"), TEXT("建议调整"), EPBRMagicPaintHitAction::ApplyPostSuggested, Theme);

		const bool bUnbound = OwnerWindow->GetDetailsPostProcessVolume() ? OwnerWindow->GetDetailsPostProcessVolume()->bUnbound : false;
		DrawButton(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(136.0f, 62.0f), FVector2D(150.0f, 32.0f), bUnbound ? TEXT("✓") : TEXT("□"), TEXT("Infinite Extent"), EPBRMagicPaintHitAction::TogglePostUnbound, Theme, bUnbound);

		DrawText(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 112.0f), TEXT("Exposure 曝光"), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		EAutoExposureMethod Method = AEM_Histogram;
		if (FPostProcessSettings* Settings = OwnerWindow->GetDetailsPostProcessSettings())
		{
			Method = Settings->AutoExposureMethod;
		}
		DrawButton(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 136.0f), FVector2D(82.0f, 30.0f), TEXT("M"), TEXT("Manual"), EPBRMagicPaintHitAction::ExposureManual, Theme, Method == AEM_Manual);
		DrawButton(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(104.0f, 136.0f), FVector2D(74.0f, 30.0f), TEXT("B"), TEXT("Basic"), EPBRMagicPaintHitAction::ExposureBasic, Theme, Method == AEM_Basic);
		DrawButton(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(188.0f, 136.0f), FVector2D(106.0f, 30.0f), TEXT("H"), TEXT("Histogram"), EPBRMagicPaintHitAction::ExposureHistogram, Theme, Method == AEM_Histogram);
		DrawButton(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(Size.X - 78.0f, 136.0f), FVector2D(66.0f, 30.0f), TEXT("↺"), TEXT("重置"), EPBRMagicPaintHitAction::ExposureReset, Theme);
		FPostProcessSettings* Settings = OwnerWindow->GetDetailsPostProcessSettings();
		DrawValueStepper(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 180.0f), FVector2D(Size.X - 24.0f, 24.0f), TEXT("Exposure Bias"), Settings ? FString::Printf(TEXT("%.1f"), Settings->AutoExposureBias) : TEXT("--"), EPBRMagicPaintHitAction::ExposureBiasDown, EPBRMagicPaintHitAction::ExposureBiasUp, Theme);
		DrawValueStepper(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 216.0f), FVector2D(Size.X - 24.0f, 24.0f), TEXT("Bloom Intensity"), Settings ? FString::Printf(TEXT("%.1f"), Settings->BloomIntensity) : TEXT("--"), EPBRMagicPaintHitAction::BloomIntensityDown, EPBRMagicPaintHitAction::BloomIntensityUp, Theme);
		DrawValueStepper(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(12.0f, 252.0f), FVector2D(Size.X - 24.0f, 24.0f), TEXT("White Temp"), Settings ? FString::Printf(TEXT("%.0fK"), Settings->WhiteTemp) : TEXT("--"), EPBRMagicPaintHitAction::WhiteTempDown, EPBRMagicPaintHitAction::WhiteTempUp, Theme);
	}

	void DrawInspector(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32& Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		DrawBox(OutDrawElements, Geometry, Layer++, Position - FVector2D(2.0f, 2.0f), Size + FVector2D(4.0f, 4.0f), Theme.Panel);
		DrawInsetPanelFrame(OutDrawElements, Geometry, Layer++, Position, Size, Theme);
		DrawPanelSurface(OutDrawElements, Geometry, Layer, Position + FVector2D(12, 12), FVector2D(Size.X - 24, 66), Theme.PanelRaised, 5.0f, Theme.Border, 1.0f);
		const bool bModelInspector = OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Models;
		const bool bMaterialInspector = OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Materials;
		const FString InspectorTitle = bModelInspector ? TEXT("模型批量工具") : (bMaterialInspector ? TEXT("所选对象材质") : TEXT("所选对象"));
		const FString InspectorSubTitle = bModelInspector ? TEXT("Model Batch Tools") : (bMaterialInspector ? TEXT("Selected Object Materials") : TEXT("Selected Objects"));
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(24, 22), Size.X - 48.0f, InspectorTitle, FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(24, 40), Size.X - 48.0f, InspectorSubTitle, FAppStyle::GetFontStyle("SmallFontBold"), Theme.TextMuted);
		const FString ShortSummary = bModelInspector
			? FString::Printf(TEXT("已勾选 %d 个模型；可重命名、替换或成组。"), OwnerWindow->CachedCheckedCount)
			: FString::Printf(TEXT("已勾选 %d 个对象；切换分类不会清空。"), OwnerWindow->CachedCheckedCount);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(24, 56), Size.X - 48.0f, ShortSummary, FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);

		const FVector2D ActionsPos = Position + FVector2D(12, 90);
		DrawPanelSurface(OutDrawElements, Geometry, Layer, ActionsPos, FVector2D(Size.X - 24, 78), Theme.PanelRaised, 5.0f, Theme.Border, 1.0f);
		if (bModelInspector)
		{
			DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(12, 22), FVector2D(102, 34), TEXT("R"), TEXT("批量重命名"), EPBRMagicPaintHitAction::ModelBatchRename, Theme);
			DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(126, 22), FVector2D(112, 34), TEXT("⇆"), TEXT("替换物体"), EPBRMagicPaintHitAction::ModelReplaceActors, Theme);
			DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(250, 22), FVector2D(94, 34), TEXT("▣"), TEXT("成组"), EPBRMagicPaintHitAction::ModelGroupToFolder, Theme);
		}
		else
		{
			if (bMaterialInspector)
			{
				DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(12, 22), FVector2D(72, 34), TEXT("↗"), TEXT("打开"), EPBRMagicPaintHitAction::OpenMaterial, Theme);
				DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(94, 22), FVector2D(104, 34), TEXT("□"), TEXT("选Actor"), EPBRMagicPaintHitAction::SelectActors, Theme);
				DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(208, 22), FVector2D(72, 34), TEXT("◇"), TEXT("调参"), EPBRMagicPaintHitAction::AdjustMaterial, Theme);
				DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(290, 22), FVector2D(54, 34), TEXT("⟳"), TEXT("重置"), EPBRMagicPaintHitAction::ClearChecked, Theme);
			}
			else
			{
				DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(12, 22), FVector2D(100, 34), TEXT("↗"), TEXT("打开材质"), EPBRMagicPaintHitAction::OpenMaterial, Theme);
				DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(124, 22), FVector2D(122, 34), TEXT("□"), TEXT("选择关联Actor"), EPBRMagicPaintHitAction::SelectActors, Theme);
				DrawButton(OutDrawElements, Geometry, Layer + 1, ActionsPos + FVector2D(258, 22), FVector2D(86, 34), TEXT("⟳"), TEXT("重置"), EPBRMagicPaintHitAction::ClearChecked, Theme);
			}
		}

		const FVector2D ListPos = Position + FVector2D(12, 188);
		const int32 InspectorCount = bMaterialInspector ? OwnerWindow->SelectedMaterialItems.Num() : OwnerWindow->CheckedListItems.Num();
		DrawText(OutDrawElements, Geometry, Layer + 1, ListPos, bMaterialInspector ? TEXT("被选择的材质") : (bModelInspector ? TEXT("待处理模型") : TEXT("勾选列表")), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		DrawText(OutDrawElements, Geometry, Layer + 1, ListPos + FVector2D(Size.X - 46, 0), FString::FromInt(InspectorCount), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);

		const FVector2D FooterPos(Position.X + 12.0f, Position.Y + Size.Y - 72.0f);
		const bool bShowMaterialParameterPanel = bMaterialInspector && OwnerWindow->GetEditableMaterialInstance() != nullptr;
		const bool bShowParameterPanel = OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Lights || OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Cameras || bShowMaterialParameterPanel;
		const float ParameterHeight = OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Cameras ? 292.0f : (bShowMaterialParameterPanel ? 250.0f : (bShowParameterPanel ? 188.0f : 0.0f));
		const FVector2D ParameterPos(Position.X + 12.0f, FooterPos.Y - ParameterHeight - 8.0f);
		const FVector2D ListBoxPos = ListPos + FVector2D(0, 24);
		const float ListBoxBottom = bShowParameterPanel ? ParameterPos.Y - 12.0f : FooterPos.Y - 18.0f;
		const FVector2D ListBoxSize(Size.X - 24, FMath::Max(128.0f, ListBoxBottom - ListBoxPos.Y));
		InspectorListRect = FSlateRect(ListBoxPos.X, ListBoxPos.Y, ListBoxPos.X + ListBoxSize.X, ListBoxPos.Y + ListBoxSize.Y);
		InspectorItemCount = InspectorCount;
		DrawPanelSurface(OutDrawElements, Geometry, Layer, ListBoxPos, ListBoxSize, Theme.TableRow, 5.0f, Theme.Border, 1.0f);
		if (bMaterialInspector)
		{
			const float RowUnit = 30.0f;
			const float HeaderH = 92.0f;
			const float SlotH = 30.0f;
			const float MaterialGap = 8.0f;
			float TotalHeight = 0.0f;
			for (const TSharedPtr<FPBRMagicOutlinerItem>& Item : OwnerWindow->SelectedMaterialItems)
			{
				TotalHeight += HeaderH + FMath::Max(1, Item.IsValid() ? Item->MaterialSlots.Num() : 0) * SlotH + MaterialGap;
			}
			InspectorItemCount = FMath::CeilToInt(TotalHeight / RowUnit);
			InspectorVisibleCapacity = FMath::Max(0.0f, (ListBoxSize.Y - 18.0f) / RowUnit);
			InspectorScrollOffset = FMath::Clamp(InspectorScrollOffset, 0.0f, FMath::Max(0.0f, static_cast<float>(InspectorItemCount) - InspectorVisibleCapacity));
			const float ScrollY = InspectorScrollOffset * RowUnit;
			const float ClipTop = ListBoxPos.Y + 10.0f;
			const float ClipBottom = ListBoxPos.Y + ListBoxSize.Y - 14.0f;
			float CardY = ClipTop - ScrollY;
			for (int32 Index = 0; Index < OwnerWindow->SelectedMaterialItems.Num(); ++Index)
			{
				const TSharedPtr<FPBRMagicOutlinerItem>& Item = OwnerWindow->SelectedMaterialItems[Index];
				const FVector2D CardPos(ListBoxPos.X + 10.0f, CardY);
				const int32 SlotCount = Item.IsValid() ? Item->MaterialSlots.Num() : 0;
				const FVector2D CardSize(ListBoxSize.X - 20.0f, HeaderH);
				const bool bActive = OwnerWindow->ActiveTreeItem == Item;
				if (CardPos.Y + HeaderH >= ClipTop && CardPos.Y <= ClipBottom)
				{
					DrawRoundedBox(OutDrawElements, Geometry, Layer + 1, CardPos, CardSize, bActive ? Theme.DropZone : Theme.Background, 6.0f, bActive ? Theme.Selection : Theme.Border, 1.0f);
					DrawBox(OutDrawElements, Geometry, Layer + 2, CardPos, FVector2D(3.0f, CardSize.Y), bActive ? Theme.Selection : Theme.Border);
					DrawMaterialPreview(OutDrawElements, Geometry, Layer + 2, CardPos + FVector2D(12, 12), FVector2D(62, 62), Item, Theme);
					DrawBox(OutDrawElements, Geometry, Layer + 3, CardPos + FVector2D(57, 63), FVector2D(12, 12), Theme.Success);
					const bool bShowDropZone = CardSize.X >= 310.0f;
					const float DropW = bShowDropZone ? 92.0f : 0.0f;
					const float TextX = CardPos.X + 88.0f;
					const float TextWidth = FMath::Max(48.0f, CardSize.X - 112.0f - (bShowDropZone ? DropW + 12.0f : 0.0f));
					DrawTextInRect(OutDrawElements, Geometry, Layer + 3, FVector2D(TextX, CardPos.Y + 13), TextWidth, Item.IsValid() ? Item->DisplayName : FString(), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
					DrawTextInRect(OutDrawElements, Geometry, Layer + 3, FVector2D(TextX, CardPos.Y + 35), TextWidth, Item.IsValid() ? Item->TypeText : FString(), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
					DrawTextInRect(OutDrawElements, Geometry, Layer + 3, FVector2D(TextX, CardPos.Y + 55), TextWidth, Item.IsValid() ? FString::Printf(TEXT("%d 个对象 / %d 个槽位"), Item->ActorCount, SlotCount) : FString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
					if (bShowDropZone)
					{
						const FVector2D DropPos(CardPos.X + CardSize.X - DropW - 12.0f, CardPos.Y + 16.0f);
						const FVector2D DropSize(DropW, 60.0f);
						DrawRoundedBox(OutDrawElements, Geometry, Layer + 2, DropPos, DropSize, Theme.PanelRaised, 5.0f, Theme.PrimarySoft, 1.0f);
						DrawTextInRect(OutDrawElements, Geometry, Layer + 3, DropPos + FVector2D(8, 13), DropSize.X - 16.0f, TEXT("拖入替换"), FAppStyle::GetFontStyle("SmallFontBold"), Theme.TextMuted);
						DrawTextInRect(OutDrawElements, Geometry, Layer + 3, DropPos + FVector2D(8, 34), DropSize.X - 16.0f, TEXT("All Slots"), FAppStyle::GetFontStyle("SmallFont"), Theme.Primary);
					}
					AddHit(CardPos, CardSize, EPBRMagicPaintHitAction::RowSelect, EPBRMagicOutlinerCategory::All, Item);
				}
				CardY += HeaderH;
				if (Item.IsValid() && SlotCount > 0)
				{
					for (int32 SlotLine = 0; SlotLine < SlotCount; ++SlotLine)
					{
						const FVector2D SlotPos(ListBoxPos.X + 16.0f, CardY);
						const FVector2D SlotSize(ListBoxSize.X - 32.0f, SlotH - 4.0f);
						if (SlotPos.Y + SlotSize.Y >= ClipTop && SlotPos.Y <= ClipBottom)
						{
							const FPBRMaterialSlotReference& SlotRef = Item->MaterialSlots[SlotLine];
							const FString ActorName = SlotRef.Actor.IsValid() ? SlotRef.Actor->GetActorLabel() : TEXT("Actor");
							const FString ComponentName = SlotRef.MeshComponent.IsValid() ? SlotRef.MeshComponent->GetName() : TEXT("Component");
							DrawRoundedBox(OutDrawElements, Geometry, Layer + 1, SlotPos, SlotSize, Theme.PanelRaised, 4.0f, Theme.Border, 1.0f);
							DrawBox(OutDrawElements, Geometry, Layer + 2, SlotPos + FVector2D(8.0f, 8.0f), FVector2D(6.0f, 6.0f), Theme.Primary);
							DrawTextInRect(OutDrawElements, Geometry, Layer + 3, SlotPos + FVector2D(22.0f, 6.0f), 38.0f, FString::Printf(TEXT("[%d]"), SlotRef.SlotIndex), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
							const float ActionW = 44.0f;
							const float AvailableTextW = FMath::Max(120.0f, SlotSize.X - 122.0f - ActionW);
							const float ActorW = FMath::Clamp(SlotSize.X * 0.38f, 78.0f, AvailableTextW * 0.58f);
							const float ComponentW = FMath::Max(52.0f, AvailableTextW - ActorW - 10.0f);
							DrawTextInRect(OutDrawElements, Geometry, Layer + 3, SlotPos + FVector2D(62.0f, 6.0f), ActorW, ActorName, FAppStyle::GetFontStyle("SmallFont"), Theme.Text);
							DrawTextInRect(OutDrawElements, Geometry, Layer + 3, SlotPos + FVector2D(72.0f + ActorW, 6.0f), ComponentW, ComponentName, FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
							DrawTextInRect(OutDrawElements, Geometry, Layer + 3, SlotPos + FVector2D(SlotSize.X - 54.0f, 6.0f), ActionW, TEXT("调参"), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Primary);
							AddHit(SlotPos, SlotSize, EPBRMagicPaintHitAction::RowSelect, EPBRMagicOutlinerCategory::All, Item, nullptr, nullptr, SlotLine);
							AddHit(SlotPos + FVector2D(SlotSize.X - 62.0f, 0.0f), FVector2D(56.0f, SlotSize.Y), EPBRMagicPaintHitAction::AdjustMaterial, EPBRMagicOutlinerCategory::All, Item, nullptr, nullptr, SlotRef.SlotIndex);
						}
						CardY += SlotH;
					}
				}
				else
				{
					const FVector2D SlotPos(ListBoxPos.X + 16.0f, CardY);
					const FVector2D SlotSize(ListBoxSize.X - 32.0f, SlotH - 4.0f);
					if (SlotPos.Y + SlotSize.Y >= ClipTop && SlotPos.Y <= ClipBottom)
					{
						DrawRoundedBox(OutDrawElements, Geometry, Layer + 1, SlotPos, SlotSize, Theme.PanelRaised, 4.0f, Theme.Border, 1.0f);
						DrawTextInRect(OutDrawElements, Geometry, Layer + 3, SlotPos + FVector2D(12.0f, 6.0f), SlotSize.X - 24.0f, TEXT("没有可替换的材质槽"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
					}
					CardY += SlotH;
				}
				CardY += MaterialGap;
			}
			if (OwnerWindow->SelectedMaterialItems.IsEmpty())
			{
				DrawTextInRect(OutDrawElements, Geometry, Layer + 1, ListBoxPos + FVector2D(18, 20), ListBoxSize.X - 36.0f, TEXT("选择场景物体后，这里会显示它们使用的材质。"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
			}
		}
		else
		{
			InspectorVisibleCapacity = FMath::Max(0.0f, FMath::FloorToFloat((ListBoxSize.Y - 18.0f) / 46.0f));
			InspectorScrollOffset = FMath::Clamp(InspectorScrollOffset, 0.0f, FMath::Max(0.0f, static_cast<float>(InspectorCount) - InspectorVisibleCapacity));
			const int32 StartIndex = FMath::Clamp(FMath::FloorToInt(InspectorScrollOffset), 0, FMath::Max(0, InspectorCount - 1));
			const int32 MaxChecked = FMath::Min(InspectorCount - StartIndex, FMath::FloorToInt(InspectorVisibleCapacity));
			float CardY = ListBoxPos.Y + 10.0f;
			for (int32 RowIndex = 0; RowIndex < MaxChecked; ++RowIndex)
			{
				const int32 Index = StartIndex + RowIndex;
				const TSharedPtr<FPBRCheckedActorListItem>& Item = OwnerWindow->CheckedListItems[Index];
				const FVector2D CardPos(ListBoxPos.X + 10.0f, CardY);
				const FVector2D CardSize(ListBoxSize.X - 20.0f, 38.0f);
				const bool bFocused = Item.IsValid() && OwnerWindow->DetailsActor == Item->Actor;
				const bool bSelected = Item.IsValid() && OwnerWindow->PaintSelectedCheckedActors.Contains(Item->Actor);
				DrawRoundedBox(OutDrawElements, Geometry, Layer + 1, CardPos, CardSize, bSelected ? Theme.DropZone : (bFocused ? Theme.PanelRaised : Theme.Background), 5.0f, bSelected ? Theme.Selection : Theme.Border, 1.0f);
				DrawRoundedBox(OutDrawElements, Geometry, Layer + 2, CardPos + FVector2D(8, 11), FVector2D(14, 14), bSelected ? Theme.Selection : Theme.Panel, 3.0f, bSelected ? Theme.Selection : Theme.Border, 1.0f);
				DrawItemGlyph(OutDrawElements, Geometry, Layer + 2, CardPos + FVector2D(30, 7), FVector2D(24, 24), nullptr, Theme);
				DrawTextInRect(OutDrawElements, Geometry, Layer + 3, CardPos + FVector2D(64, 5), CardSize.X - 72.0f, Item.IsValid() ? Item->DisplayName : FString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
				DrawTextInRect(OutDrawElements, Geometry, Layer + 3, CardPos + FVector2D(64, 21), CardSize.X - 72.0f, Item.IsValid() ? Item->TypeText + TEXT(" | ") + Item->DetailText : FString(), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
				AddHit(CardPos, CardSize, EPBRMagicPaintHitAction::CheckedActorSelect, EPBRMagicOutlinerCategory::All, nullptr, nullptr, Item);
				CardY += 46.0f;
			}
			if (OwnerWindow->CheckedListItems.IsEmpty())
			{
				DrawTextInRect(OutDrawElements, Geometry, Layer + 1, ListBoxPos + FVector2D(18, 20), ListBoxSize.X - 36.0f, TEXT("勾选对象后，这里会显示批量操作列表。"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
			}
		}
		if (InspectorCount > FMath::FloorToInt(InspectorVisibleCapacity))
		{
			const int32 PageStart = FMath::Clamp(FMath::FloorToInt(InspectorScrollOffset) + 1, 1, InspectorCount);
			const int32 PageEnd = FMath::Clamp(PageStart + FMath::FloorToInt(InspectorVisibleCapacity) - 1, PageStart, InspectorCount);
			DrawTextInRect(OutDrawElements, Geometry, Layer + 2, ListBoxPos + FVector2D(12.0f, ListBoxSize.Y - 18.0f), ListBoxSize.X - 24.0f, FString::Printf(TEXT("%d-%d / %d"), PageStart, PageEnd, InspectorCount), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		}

		if (OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Lights)
		{
			DrawLightControls(OutDrawElements, Geometry, Layer + 1, ParameterPos, FVector2D(Size.X - 24.0f, ParameterHeight), Theme);
		}
		else if (OwnerWindow->ActiveCategory == EPBRMagicOutlinerCategory::Cameras)
		{
			DrawCameraControls(OutDrawElements, Geometry, Layer + 1, ParameterPos, FVector2D(Size.X - 24.0f, ParameterHeight), Theme);
		}
		else if (bShowMaterialParameterPanel)
		{
			DrawMaterialControls(OutDrawElements, Geometry, Layer + 1, ParameterPos, FVector2D(Size.X - 24.0f, ParameterHeight), Theme);
		}

		DrawPanelSurface(OutDrawElements, Geometry, Layer, FooterPos, FVector2D(Size.X - 24.0f, 56.0f), Theme.PanelRaised, 5.0f, Theme.Border, 1.0f);
		DrawButton(OutDrawElements, Geometry, Layer + 1, FooterPos + FVector2D(12, 11), FVector2D(132, 34), TEXT("□"), TEXT("选中列表项"), EPBRMagicPaintHitAction::SelectCheckedList, Theme);
		DrawButton(OutDrawElements, Geometry, Layer + 1, FooterPos + FVector2D(156, 11), FVector2D(118, 34), TEXT("×"), TEXT("移除列表项"), EPBRMagicPaintHitAction::ClearCheckedList, Theme);
		Layer += 4;
	}

	void DrawStatusBar(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32& Layer, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		const float H = 24.0f;
		const FVector2D Pos(8.0f, Size.Y - H - 5.0f);
		DrawBox(OutDrawElements, Geometry, Layer++, FVector2D(0.0f, Size.Y - H - 8.0f), FVector2D(Size.X, H + 8.0f), Theme.Panel);
		DrawBox(OutDrawElements, Geometry, Layer++, Pos, FVector2D(Size.X - 16.0f, H), Theme.Background);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Pos + FVector2D(10, 5), Size.X - 230.0f, OwnerWindow->GetStatusText().ToString(), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Pos + FVector2D(Size.X - 190.0f, 5), 174.0f, TEXT("批量操作支持 Ctrl+Z"), FAppStyle::GetFontStyle("SmallFont"), Theme.Primary);
	}

	SPBRMagicOutlinerWindow* OwnerWindow = nullptr;
	mutable TArray<FPBRMagicPaintHitRegion> HitRegions;
	mutable FSlateRect NameListRect;
	mutable float NameVisibleCapacity = 0.0f;
	mutable int32 NameItemCount = 0;
	mutable float NameScrollOffset = 0.0f;
	mutable FSlateRect MainRowsRect;
	mutable float VisibleRowCapacity = 0.0f;
	mutable int32 VisibleRowCount = 0;
	mutable float RowScrollOffset = 0.0f;
	mutable FSlateRect InspectorListRect;
	mutable float InspectorVisibleCapacity = 0.0f;
	mutable int32 InspectorItemCount = 0;
	mutable float InspectorScrollOffset = 0.0f;
	mutable FString LastPaintSelectionSignature;
	EPBRMagicPaintHitAction HoveredAction = EPBRMagicPaintHitAction::None;
	EPBRMagicPaintHitAction PressedAction = EPBRMagicPaintHitAction::None;
	FSlateRect HoveredRect;
	FSlateRect PressedRect;
	FString SearchText;
	bool bSearchActive = false;
};

enum class EPBRMagicMaterialPopupAction : uint8
{
	None,
	Search,
	TypePrevious,
	TypeNext,
	ScalarDown,
	ScalarUp,
	ScalarTrack,
	VectorTrack,
	SwitchToggle,
	TextureUseSelected,
	TextureClear
};

struct FPBRMagicMaterialPopupHit
{
	FSlateRect Rect;
	EPBRMagicMaterialPopupAction Action = EPBRMagicMaterialPopupAction::None;
	FName ParameterName;
	float ScalarMin = 0.0f;
	float ScalarMax = 1.0f;
	float ScalarDefault = 0.0f;
	float ScalarStep = 0.01f;
	int32 VectorChannel = 0;
	FLinearColor VectorDefault = FLinearColor::White;
	bool bSwitchDefault = false;
};

class SPBRMagicMaterialParameterPopupSurface : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRMagicMaterialParameterPopupSurface) {}
		SLATE_ARGUMENT(SPBRMagicOutlinerWindow*, OwnerWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OwnerWindow = InArgs._OwnerWindow;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(820.0f, 760.0f);
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (!OwnerWindow)
		{
			return LayerId;
		}

		HitRegions.Reset();
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FPBRMagicTheme& Theme = FindMagicTheme(OwnerWindow->ActiveThemeId);
		DrawBox(OutDrawElements, AllottedGeometry, LayerId, FVector2D::ZeroVector, Size, Theme.Background);
		DrawBox(OutDrawElements, AllottedGeometry, LayerId + 1, FVector2D(0.0f, 0.0f), FVector2D(Size.X, 94.0f), Theme.Panel);

		DrawText(OutDrawElements, AllottedGeometry, LayerId + 2, FVector2D(20.0f, 16.0f), TEXT("材质参数调节"), FAppStyle::GetFontStyle("NormalFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, AllottedGeometry, LayerId + 2, FVector2D(20.0f, 40.0f), Size.X - 40.0f, OwnerWindow->GetEditableMaterialNameText().ToString(), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		DrawTextInRect(OutDrawElements, AllottedGeometry, LayerId + 2, FVector2D(20.0f, 62.0f), Size.X - 40.0f, OwnerWindow->GetEditableMaterialSlotText().ToString(), FAppStyle::GetFontStyle("SmallFont"), Theme.Primary);

		const FVector2D TypeY(20.0f, 108.0f);
		DrawButton(OutDrawElements, AllottedGeometry, LayerId + 2, TypeY, FVector2D(42.0f, 30.0f), TEXT("<"), EPBRMagicMaterialPopupAction::TypePrevious, Theme);
		DrawBox(OutDrawElements, AllottedGeometry, LayerId + 2, TypeY + FVector2D(50.0f, 0.0f), FVector2D(220.0f, 30.0f), Theme.PanelRaised);
		DrawTextInRect(OutDrawElements, AllottedGeometry, LayerId + 3, TypeY + FVector2D(60.0f, 7.0f), 200.0f, OwnerWindow->GetEditableMaterialTypeText().ToString(), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
		DrawButton(OutDrawElements, AllottedGeometry, LayerId + 2, TypeY + FVector2D(278.0f, 0.0f), FVector2D(42.0f, 30.0f), TEXT(">"), EPBRMagicMaterialPopupAction::TypeNext, Theme);

		const FVector2D SearchPos(Size.X - 340.0f, 108.0f);
		const FVector2D SearchSize(320.0f, 30.0f);
		DrawBox(OutDrawElements, AllottedGeometry, LayerId + 2, SearchPos, SearchSize, bSearchActive ? Theme.DropZone : Theme.PanelRaised);
		DrawText(OutDrawElements, AllottedGeometry, LayerId + 3, SearchPos + FVector2D(10.0f, 7.0f), TEXT("⌕"), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Primary);
		const FString SearchDisplay = SearchText.IsEmpty() ? TEXT("搜索参数 / 分组 / 贴图") : SearchText;
		DrawTextInRect(OutDrawElements, AllottedGeometry, LayerId + 3, SearchPos + FVector2D(34.0f, 7.0f), SearchSize.X - 46.0f, SearchDisplay, FAppStyle::GetFontStyle("SmallFont"), SearchText.IsEmpty() ? Theme.TextMuted : Theme.Text);
		AddHit(SearchPos, SearchSize, EPBRMagicMaterialPopupAction::Search);

		TArray<FPBRMagicDynamicMaterialParameter> Parameters = OwnerWindow->CollectEditableDynamicMaterialParameters();
		FilterParameters(Parameters);
		const float ListTop = 154.0f;
		const float ListBottom = Size.Y - 44.0f;
		const float ListHeight = FMath::Max(60.0f, ListBottom - ListTop);
		const float ContentHeight = ComputeContentHeight(Parameters);
		MaxScrollOffset = FMath::Max(0.0f, ContentHeight - ListHeight);
		ScrollOffset = FMath::Clamp(ScrollOffset, 0.0f, MaxScrollOffset);

		DrawBox(OutDrawElements, AllottedGeometry, LayerId + 1, FVector2D(14.0f, ListTop - 8.0f), FVector2D(Size.X - 28.0f, ListHeight + 16.0f), Theme.Panel);

		if (Parameters.IsEmpty())
		{
			DrawTextInRect(OutDrawElements, AllottedGeometry, LayerId + 2, FVector2D(34.0f, ListTop + 20.0f), Size.X - 68.0f, SearchText.IsEmpty() ? TEXT("当前材质实例没有可编辑参数。") : TEXT("没有匹配的参数。"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);
		}
		else
		{
			DrawParameterList(OutDrawElements, AllottedGeometry, LayerId + 2, FVector2D(24.0f, ListTop), FVector2D(Size.X - 62.0f, ListHeight), Parameters, Theme);
		}

		DrawScrollBar(OutDrawElements, AllottedGeometry, LayerId + 3, FVector2D(Size.X - 30.0f, ListTop), FVector2D(8.0f, ListHeight), Theme);
		DrawTextInRect(OutDrawElements, AllottedGeometry, LayerId + 2, FVector2D(20.0f, Size.Y - 28.0f), Size.X - 40.0f, TEXT("贴图参数：先在内容浏览器选择贴图，再点“用选中”；标量和颜色可点击滑条快速调整。"), FAppStyle::GetFontStyle("SmallFont"), Theme.TextMuted);

		return LayerId + 6;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!OwnerWindow || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		for (int32 Index = HitRegions.Num() - 1; Index >= 0; --Index)
		{
			const FPBRMagicMaterialPopupHit& Hit = HitRegions[Index];
			if (!Hit.Rect.ContainsPoint(LocalPosition))
			{
				continue;
			}

			PressedAction = Hit.Action;
			PressedRect = Hit.Rect;
			HandleHit(Hit, LocalPosition);
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse).CaptureMouse(AsShared());
		}

		bSearchActive = false;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PressedAction != EPBRMagicMaterialPopupAction::None)
		{
			PressedAction = EPBRMagicMaterialPopupAction::None;
			PressedRect = FSlateRect();
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		EPBRMagicMaterialPopupAction NewHoveredAction = EPBRMagicMaterialPopupAction::None;
		FSlateRect NewHoveredRect;
		for (int32 Index = HitRegions.Num() - 1; Index >= 0; --Index)
		{
			const FPBRMagicMaterialPopupHit& Hit = HitRegions[Index];
			if (Hit.Rect.ContainsPoint(LocalPosition))
			{
				NewHoveredAction = Hit.Action;
				NewHoveredRect = Hit.Rect;
				break;
			}
		}
		if (HoveredAction != NewHoveredAction || !RectMatches(HoveredRect, NewHoveredRect))
		{
			HoveredAction = NewHoveredAction;
			HoveredRect = NewHoveredRect;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		ScrollOffset = FMath::Clamp(ScrollOffset - MouseEvent.GetWheelDelta() * 72.0f, 0.0f, MaxScrollOffset);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override
	{
		if (!bSearchActive)
		{
			return FReply::Unhandled();
		}

		const TCHAR Character = InCharacterEvent.GetCharacter();
		if (Character >= 32)
		{
			SearchText.AppendChar(Character);
			ScrollOffset = 0.0f;
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::F)
		{
			bSearchActive = true;
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}

		if (bSearchActive)
		{
			if (InKeyEvent.GetKey() == EKeys::BackSpace)
			{
				if (!SearchText.IsEmpty())
				{
					SearchText.LeftChopInline(1);
					ScrollOffset = 0.0f;
					Invalidate(EInvalidateWidgetReason::Paint);
				}
				return FReply::Handled();
			}
			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				bSearchActive = false;
				SearchText.Reset();
				ScrollOffset = 0.0f;
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled();
			}
		}

		return FReply::Unhandled();
	}

private:
	static float EstimateTextWidth(const FString& Text, const FSlateFontInfo& Font)
	{
		float Width = 0.0f;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			Width += Text[Index] > 0x2E80 ? Font.Size * 0.95f : Font.Size * 0.56f;
		}
		return Width;
	}

	static FString EllipsizeText(const FString& Text, float MaxWidth, const FSlateFontInfo& Font)
	{
		if (MaxWidth <= 8.0f || Text.IsEmpty())
		{
			return FString();
		}
		if (EstimateTextWidth(Text, Font) <= MaxWidth)
		{
			return Text;
		}

		const FString Suffix = TEXT("...");
		const float SuffixWidth = EstimateTextWidth(Suffix, Font) + 8.0f;
		FString Result;
		float Width = 0.0f;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const FString NextChar = Text.Mid(Index, 1);
			const float NextWidth = EstimateTextWidth(NextChar, Font);
			if (Width + NextWidth + SuffixWidth > MaxWidth)
			{
				break;
			}
			Result += NextChar;
			Width += NextWidth;
		}
		return Result.IsEmpty() ? Suffix : Result + Suffix;
	}

	static bool RectMatches(const FSlateRect& A, const FSlateRect& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left)
			&& FMath::IsNearlyEqual(A.Top, B.Top)
			&& FMath::IsNearlyEqual(A.Right, B.Right)
			&& FMath::IsNearlyEqual(A.Bottom, B.Bottom);
	}

	static void DrawBox(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			Layer,
			Geometry.ToPaintGeometry(FVector2f(Size.X, Size.Y), FSlateLayoutTransform(FVector2f(Position.X, Position.Y))),
			FAppStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			Color);
	}

	static void DrawText(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			Layer,
			Geometry.ToPaintGeometry(FVector2f(1.0f, 1.0f), FSlateLayoutTransform(FVector2f(Position.X, Position.Y))),
			Text,
			Font,
			ESlateDrawEffect::None,
			Color);
	}

	static void DrawTextInRect(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, float MaxWidth, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		const float SafeWidth = FMath::Max(0.0f, MaxWidth - 8.0f);
		if (SafeWidth <= 8.0f)
		{
			return;
		}
		DrawText(OutDrawElements, Geometry, Layer, Position, EllipsizeText(Text, SafeWidth, Font), Font, Color);
	}

	static FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A = Alpha;
		return Color;
	}

	static FLinearColor MixColor(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(
			FMath::Lerp(A.R, B.R, Alpha),
			FMath::Lerp(A.G, B.G, Alpha),
			FMath::Lerp(A.B, B.B, Alpha),
			FMath::Lerp(A.A, B.A, Alpha));
	}

	static FString GetParameterDisplayName(const FName& ParameterName)
	{
		const FString RawName = ParameterName.ToString();
		for (const FPBRMagicEditableMaterialParameter& Parameter : GetMagicEditableMaterialParameters())
		{
			if (Parameter.ParameterName == ParameterName)
			{
				return Parameter.GetLabel().ToString();
			}
		}

		struct FKnownParameterName
		{
			const TCHAR* Raw;
			const TCHAR* Display;
		};

		static const FKnownParameterName KnownNames[] =
		{
			{ TEXT("BaseColorTexture"), TEXT("基础色贴图") },
			{ TEXT("BaseColorMap"), TEXT("基础色贴图") },
			{ TEXT("BaseColorTint"), TEXT("基础色调") },
			{ TEXT("BaseColorIntensity"), TEXT("基础色强度") },
			{ TEXT("UseBaseColorTexture"), TEXT("使用基础色贴图") },
			{ TEXT("NormalTexture"), TEXT("法线贴图") },
			{ TEXT("NormalMap"), TEXT("法线贴图") },
			{ TEXT("NormalStrength"), TEXT("法线强度") },
			{ TEXT("UseNormalTexture"), TEXT("使用法线贴图") },
			{ TEXT("RoughnessTexture"), TEXT("粗糙度贴图") },
			{ TEXT("RoughnessMap"), TEXT("粗糙度贴图") },
			{ TEXT("RoughnessIntensity"), TEXT("粗糙度强度") },
			{ TEXT("RoughnessValue"), TEXT("粗糙度数值") },
			{ TEXT("UseRoughnessTexture"), TEXT("使用粗糙度贴图") },
			{ TEXT("SpecularTexture"), TEXT("高光贴图") },
			{ TEXT("SpecularIntensity"), TEXT("高光强度") },
			{ TEXT("UseSpecularTexture"), TEXT("使用高光贴图") },
			{ TEXT("AOTexture"), TEXT("环境遮蔽贴图") },
			{ TEXT("OcclusionTexture"), TEXT("环境遮蔽贴图") },
			{ TEXT("UseAOTexture"), TEXT("使用环境遮蔽贴图") },
			{ TEXT("MetallicTexture"), TEXT("金属度贴图") },
			{ TEXT("MetallicValue"), TEXT("金属度数值") },
			{ TEXT("UseMetallicTexture"), TEXT("使用金属度贴图") },
			{ TEXT("OpacityTexture"), TEXT("透明贴图") },
			{ TEXT("OpacityValue"), TEXT("透明度") },
			{ TEXT("UseOpacityTexture"), TEXT("使用透明贴图") },
			{ TEXT("EmissiveTexture"), TEXT("自发光贴图") },
			{ TEXT("EmissiveIntensity"), TEXT("自发光强度") },
			{ TEXT("UseEmissiveTexture"), TEXT("使用自发光贴图") },
			{ TEXT("HeightTexture"), TEXT("高度贴图") },
			{ TEXT("HeightStrength"), TEXT("高度强度") },
			{ TEXT("UseHeightTexture"), TEXT("使用高度贴图") },
			{ TEXT("IOR"), TEXT("折射率") },
			{ TEXT("Refraction"), TEXT("折射强度") },
			{ TEXT("GlassTint"), TEXT("玻璃颜色") },
			{ TEXT("GlassDirtTexture"), TEXT("玻璃污渍贴图") },
			{ TEXT("GlassDistortionTexture"), TEXT("玻璃扭曲贴图") },
			{ TEXT("GlassFrostedTexture"), TEXT("玻璃磨砂贴图") },
			{ TEXT("WaterRippleTexture"), TEXT("水纹贴图") }
		};

		for (const FKnownParameterName& KnownName : KnownNames)
		{
			if (RawName.Equals(KnownName.Raw, ESearchCase::IgnoreCase))
			{
				return KnownName.Display;
			}
		}

		FString DisplayName;
		DisplayName.Reserve(RawName.Len() + 8);
		for (int32 Index = 0; Index < RawName.Len(); ++Index)
		{
			const TCHAR Character = RawName[Index];
			if (Character == TCHAR('_') || Character == TCHAR('-'))
			{
				DisplayName.AppendChar(TEXT(' '));
				continue;
			}
			if (Index > 0 && FChar::IsUpper(Character))
			{
				const TCHAR Previous = RawName[Index - 1];
				if (FChar::IsLower(Previous) || FChar::IsDigit(Previous))
				{
					DisplayName.AppendChar(TEXT(' '));
				}
			}
			DisplayName.AppendChar(Character);
		}
		DisplayName.TrimStartAndEndInline();
		return DisplayName.IsEmpty() ? RawName : DisplayName;
	}

	static FString GetParameterKindLabel(const FPBRMagicDynamicMaterialParameter& Parameter)
	{
		switch (Parameter.Kind)
		{
		case EPBRMagicDynamicMaterialParameterKind::Color:
			return TEXT("颜色");
		case EPBRMagicDynamicMaterialParameterKind::Texture:
			return TEXT("贴图");
		case EPBRMagicDynamicMaterialParameterKind::Switch:
			return TEXT("开关");
		case EPBRMagicDynamicMaterialParameterKind::Scalar:
		default:
			return TEXT("数值");
		}
	}

	static FLinearColor GetChannelColor(int32 Channel, const FPBRMagicTheme& Theme)
	{
		switch (Channel)
		{
		case 0:
			return FLinearColor(0.98f, 0.05f, 0.04f, 1.0f);
		case 1:
			return FLinearColor(0.05f, 0.86f, 0.12f, 1.0f);
		case 2:
			return FLinearColor(0.08f, 0.20f, 1.0f, 1.0f);
		case 3:
		default:
			return Theme.Selection;
		}
	}

	void DrawSurface(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Fill, const FLinearColor& Border) const
	{
		DrawBox(OutDrawElements, Geometry, Layer, Position, Size, Border);
		if (Size.X > 2.0f && Size.Y > 2.0f)
		{
			DrawBox(OutDrawElements, Geometry, Layer + 1, Position + FVector2D(1.0f, 1.0f), Size - FVector2D(2.0f, 2.0f), Fill);
		}
	}

	void DrawTrack(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, float Alpha, const FLinearColor& FillColor, const FPBRMagicTheme& Theme) const
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		DrawBox(OutDrawElements, Geometry, Layer, Position, Size, Theme.Background);
		DrawBox(OutDrawElements, Geometry, Layer + 1, Position, FVector2D(Size.X * ClampedAlpha, Size.Y), FillColor);
		DrawBox(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(Size.X * ClampedAlpha - 3.0f, -5.0f), FVector2D(6.0f, Size.Y + 10.0f), Theme.Primary);
	}

	void DrawTextureThumbnail(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, UTexture* Texture, const FPBRMagicTheme& Theme) const
	{
		DrawSurface(OutDrawElements, Geometry, Layer, Position, Size, Theme.Background, Theme.Border);
		const FVector2D InnerPos = Position + FVector2D(3.0f, 3.0f);
		const FVector2D InnerSize = Size - FVector2D(6.0f, 6.0f);
		TSharedPtr<FSlateDynamicImageBrush> Brush = OwnerWindow && Texture
			? OwnerWindow->GetOrCreateTextureThumbnailBrush(Texture, InnerSize)
			: nullptr;
		if (Brush.IsValid())
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				Layer + 2,
				Geometry.ToPaintGeometry(FVector2f(InnerSize.X, InnerSize.Y), FSlateLayoutTransform(FVector2f(InnerPos.X, InnerPos.Y))),
				Brush.Get(),
				ESlateDrawEffect::None,
				FLinearColor::White);
			return;
		}

		DrawBox(OutDrawElements, Geometry, Layer + 2, InnerPos, InnerSize, Texture ? MixColor(Theme.PrimarySoft, Theme.PanelRaised, 0.36f) : Theme.PanelRaised);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 3, InnerPos + FVector2D(4.0f, InnerSize.Y * 0.5f - 7.0f), InnerSize.X - 8.0f, Texture ? TEXT("贴图") : TEXT("无图"), FAppStyle::GetFontStyle("SmallFontBold"), Texture ? Theme.Selection : Theme.TextMuted);
	}

	void DrawButton(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FString& Label, EPBRMagicMaterialPopupAction Action, const FPBRMagicTheme& Theme, const FPBRMagicMaterialPopupHit* Payload = nullptr) const
	{
		const FSlateRect Rect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y);
		const bool bHovered = HoveredAction == Action && RectMatches(HoveredRect, Rect);
		const bool bPressed = PressedAction == Action && RectMatches(PressedRect, Rect);
		const FLinearColor Fill = bPressed ? MixColor(Theme.DropZone, Theme.Background, 0.18f) : (bHovered ? MixColor(Theme.PanelRaised, Theme.Primary, 0.14f) : Theme.PanelRaised);
		DrawSurface(OutDrawElements, Geometry, Layer, Position, Size, Fill, bHovered || bPressed ? Theme.PrimarySoft : Theme.Border);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 2, Position + FVector2D(8.0f, 7.0f), Size.X - 16.0f, Label, FAppStyle::GetFontStyle("SmallFontBold"), bPressed ? Theme.Selection : Theme.Text);
		FPBRMagicMaterialPopupHit Hit = Payload ? *Payload : FPBRMagicMaterialPopupHit();
		Hit.Rect = Rect;
		Hit.Action = Action;
		HitRegions.Add(Hit);
	}

	void AddHit(const FVector2D& Position, const FVector2D& Size, EPBRMagicMaterialPopupAction Action, const FPBRMagicMaterialPopupHit* Payload = nullptr) const
	{
		FPBRMagicMaterialPopupHit Hit = Payload ? *Payload : FPBRMagicMaterialPopupHit();
		Hit.Rect = FSlateRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y);
		Hit.Action = Action;
		HitRegions.Add(Hit);
	}

	static float GetRowHeight(const FPBRMagicDynamicMaterialParameter& Parameter)
	{
		switch (Parameter.Kind)
		{
		case EPBRMagicDynamicMaterialParameterKind::Color:
			return 104.0f;
		case EPBRMagicDynamicMaterialParameterKind::Texture:
			return 92.0f;
		case EPBRMagicDynamicMaterialParameterKind::Switch:
			return 52.0f;
		case EPBRMagicDynamicMaterialParameterKind::Scalar:
		default:
			return 58.0f;
		}
	}

	float ComputeContentHeight(const TArray<FPBRMagicDynamicMaterialParameter>& Parameters) const
	{
		float Height = 0.0f;
		FString LastGroup;
		for (const FPBRMagicDynamicMaterialParameter& Parameter : Parameters)
		{
			if (Parameter.Group != LastGroup)
			{
				Height += 38.0f;
				LastGroup = Parameter.Group;
			}
			Height += GetRowHeight(Parameter) + 8.0f;
		}
		return Height;
	}

	void FilterParameters(TArray<FPBRMagicDynamicMaterialParameter>& Parameters) const
	{
		if (SearchText.IsEmpty() || !OwnerWindow)
		{
			return;
		}

		const FString Needle = SearchText.ToLower();
		Parameters.RemoveAll([this, &Needle](const FPBRMagicDynamicMaterialParameter& Parameter)
		{
			FString Text = Parameter.ParameterName.ToString() + TEXT(" ") + GetParameterDisplayName(Parameter.ParameterName) + TEXT(" ") + Parameter.Group + TEXT(" ") + GetParameterKindLabel(Parameter);
			if (Parameter.Kind == EPBRMagicDynamicMaterialParameterKind::Texture)
			{
				if (UTexture* Texture = OwnerWindow->GetEditableMaterialTexture(Parameter.ParameterName))
				{
					Text += TEXT(" ") + Texture->GetName() + TEXT(" ") + Texture->GetPathName();
				}
			}
			return !Text.ToLower().Contains(Needle);
		});
	}

	void DrawParameterList(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const TArray<FPBRMagicDynamicMaterialParameter>& Parameters, const FPBRMagicTheme& Theme) const
	{
		float Y = Position.Y - ScrollOffset;
		FString LastGroup;
		const float ClipTop = Position.Y;
		const float ClipBottom = Position.Y + Size.Y;

		for (const FPBRMagicDynamicMaterialParameter& Parameter : Parameters)
		{
			if (Parameter.Group != LastGroup)
			{
				if (Y + 30.0f >= ClipTop && Y <= ClipBottom)
				{
					const FVector2D GroupPos(Position.X, Y);
					const FVector2D GroupSize(Size.X, 30.0f);
					DrawSurface(OutDrawElements, Geometry, Layer, GroupPos, GroupSize, MixColor(Theme.DropZone, Theme.PanelRaised, 0.18f), Theme.Border);
					DrawBox(OutDrawElements, Geometry, Layer + 2, GroupPos + FVector2D(0.0f, 0.0f), FVector2D(4.0f, GroupSize.Y), Theme.Primary);
					DrawTextInRect(OutDrawElements, Geometry, Layer + 3, GroupPos + FVector2D(14.0f, 7.0f), Size.X - 28.0f, Parameter.Group, FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);
				}
				Y += 38.0f;
				LastGroup = Parameter.Group;
			}

			const float RowHeight = GetRowHeight(Parameter);
			if (Y + RowHeight >= ClipTop && Y <= ClipBottom)
			{
				const FVector2D RowPos(Position.X, Y);
				const FVector2D RowSize(Size.X, RowHeight);
				const FLinearColor RowFill = Parameter.Kind == EPBRMagicDynamicMaterialParameterKind::Texture
					? MixColor(Theme.PanelRaised, Theme.PrimarySoft, 0.08f)
					: Theme.PanelRaised;
				DrawSurface(OutDrawElements, Geometry, Layer, RowPos, RowSize, RowFill, Theme.Border);
				DrawBox(OutDrawElements, Geometry, Layer + 2, RowPos, FVector2D(4.0f, RowSize.Y), Parameter.Kind == EPBRMagicDynamicMaterialParameterKind::Texture ? Theme.Primary : Theme.PrimarySoft);
				switch (Parameter.Kind)
				{
				case EPBRMagicDynamicMaterialParameterKind::Color:
					DrawVectorParameterRow(OutDrawElements, Geometry, Layer + 2, RowPos, RowSize, Parameter, Theme);
					break;
				case EPBRMagicDynamicMaterialParameterKind::Switch:
					DrawSwitchParameterRow(OutDrawElements, Geometry, Layer + 2, RowPos, RowSize, Parameter, Theme);
					break;
				case EPBRMagicDynamicMaterialParameterKind::Texture:
					DrawTextureParameterRow(OutDrawElements, Geometry, Layer + 2, RowPos, RowSize, Parameter, Theme);
					break;
				case EPBRMagicDynamicMaterialParameterKind::Scalar:
				default:
					DrawScalarParameterRow(OutDrawElements, Geometry, Layer + 2, RowPos, RowSize, Parameter, Theme);
					break;
				}
			}
			Y += RowHeight + 8.0f;
		}
	}

	void DrawScalarParameterRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicDynamicMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const TOptional<float> OptionalValue = OwnerWindow->GetEditableMaterialScalar(Parameter.ParameterName, Parameter.ScalarValue);
		const float Value = OptionalValue.IsSet() ? OptionalValue.GetValue() : Parameter.ScalarValue;
		const float LabelW = FMath::Clamp(Size.X * 0.28f, 150.0f, 250.0f);
		const float ValueW = 82.0f;
		const FVector2D TextPos(Position.X + 18.0f, Position.Y + 10.0f);
		DrawTextInRect(OutDrawElements, Geometry, Layer, TextPos, LabelW, GetParameterDisplayName(Parameter.ParameterName), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer, TextPos + FVector2D(0.0f, 21.0f), LabelW, FString::Printf(TEXT("%.2f - %.2f"), Parameter.ScalarMin, Parameter.ScalarMax), FAppStyle::GetFontStyle("TinyText"), Theme.TextMuted);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(22.0f + LabelW, 10.0f), ValueW, FormatMagicEditableMaterialScalar(Value, Parameter.ScalarStep), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Selection);

		const FVector2D TrackPos(Position.X + 34.0f + LabelW + ValueW, Position.Y + 26.0f);
		const FVector2D TrackSize(FMath::Max(90.0f, Size.X - (TrackPos.X - Position.X) - 138.0f), 9.0f);
		const float Alpha = FMath::Clamp((Value - Parameter.ScalarMin) / FMath::Max(KINDA_SMALL_NUMBER, Parameter.ScalarMax - Parameter.ScalarMin), 0.0f, 1.0f);
		DrawTrack(OutDrawElements, Geometry, Layer, TrackPos, TrackSize, Alpha, Theme.Selection, Theme);

		FPBRMagicMaterialPopupHit TrackHit;
		TrackHit.ParameterName = Parameter.ParameterName;
		TrackHit.ScalarMin = Parameter.ScalarMin;
		TrackHit.ScalarMax = Parameter.ScalarMax;
		TrackHit.ScalarDefault = Parameter.ScalarValue;
		TrackHit.ScalarStep = Parameter.ScalarStep;
		AddHit(TrackPos - FVector2D(0.0f, 8.0f), TrackSize + FVector2D(0.0f, 16.0f), EPBRMagicMaterialPopupAction::ScalarTrack, &TrackHit);
		DrawButton(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 112.0f, 15.0f), FVector2D(42.0f, 28.0f), TEXT("-"), EPBRMagicMaterialPopupAction::ScalarDown, Theme, &TrackHit);
		DrawButton(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 58.0f, 15.0f), FVector2D(42.0f, 28.0f), TEXT("+"), EPBRMagicMaterialPopupAction::ScalarUp, Theme, &TrackHit);
	}

	void DrawSwitchParameterRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicDynamicMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		const bool bValue = OwnerWindow->GetEditableMaterialSwitch(Parameter.ParameterName, Parameter.bSwitchValue);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(18.0f, 10.0f), Size.X - 134.0f, GetParameterDisplayName(Parameter.ParameterName), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(18.0f, 31.0f), Size.X - 134.0f, TEXT("静态开关，切换后会更新当前材质实例"), FAppStyle::GetFontStyle("TinyText"), Theme.TextMuted);
		const FVector2D TogglePos(Position.X + Size.X - 110.0f, Position.Y + 12.0f);
		const FVector2D ToggleSize(90.0f, 28.0f);
		DrawSurface(OutDrawElements, Geometry, Layer, TogglePos, ToggleSize, bValue ? Theme.DropZone : Theme.Background, bValue ? Theme.Primary : Theme.Border);
		DrawTextInRect(OutDrawElements, Geometry, Layer + 2, TogglePos + FVector2D(10.0f, 7.0f), ToggleSize.X - 20.0f, bValue ? TEXT("ON") : TEXT("OFF"), FAppStyle::GetFontStyle("SmallFontBold"), bValue ? Theme.Selection : Theme.TextMuted);
		FPBRMagicMaterialPopupHit Hit;
		Hit.ParameterName = Parameter.ParameterName;
		Hit.bSwitchDefault = Parameter.bSwitchValue;
		AddHit(TogglePos, ToggleSize, EPBRMagicMaterialPopupAction::SwitchToggle, &Hit);
	}

	void DrawVectorParameterRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicDynamicMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		FLinearColor Value = Parameter.ColorValue;
		if (OwnerWindow)
		{
			const TOptional<float> R = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 0, Parameter.ColorValue);
			const TOptional<float> G = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 1, Parameter.ColorValue);
			const TOptional<float> B = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 2, Parameter.ColorValue);
			const TOptional<float> A = OwnerWindow->GetEditableMaterialVectorChannel(Parameter.ParameterName, 3, Parameter.ColorValue);
			Value.R = R.IsSet() ? R.GetValue() : Parameter.ColorValue.R;
			Value.G = G.IsSet() ? G.GetValue() : Parameter.ColorValue.G;
			Value.B = B.IsSet() ? B.GetValue() : Parameter.ColorValue.B;
			Value.A = A.IsSet() ? A.GetValue() : Parameter.ColorValue.A;
		}

		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(18.0f, 10.0f), Size.X - 178.0f, GetParameterDisplayName(Parameter.ParameterName), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer, Position + FVector2D(18.0f, 31.0f), Size.X - 126.0f, TEXT("颜色通过色块拾取器调整；强度使用对应数值参数"), FAppStyle::GetFontStyle("TinyText"), Theme.TextMuted);
		DrawSurface(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 102.0f, 12.0f), FVector2D(78.0f, 40.0f), FLinearColor(Value.R, Value.G, Value.B, 1.0f), Theme.Border);
	}

	void DrawTextureParameterRow(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicDynamicMaterialParameter& Parameter, const FPBRMagicTheme& Theme) const
	{
		UTexture* Texture = OwnerWindow->GetEditableMaterialTexture(Parameter.ParameterName);
		const FString TextureName = Texture ? Texture->GetName() : TEXT("未指定");
		const FString TexturePath = Texture ? Texture->GetPathName() : FString();
		const FVector2D ThumbPos(Position.X + 18.0f, Position.Y + 17.0f);
		DrawTextureThumbnail(OutDrawElements, Geometry, Layer, ThumbPos, FVector2D(58.0f, 58.0f), Texture, Theme);
		const float TextX = Position.X + 88.0f;
		const float ButtonAreaW = 212.0f;
		const float TextW = FMath::Max(120.0f, Size.X - 106.0f - ButtonAreaW);
		DrawTextInRect(OutDrawElements, Geometry, Layer, FVector2D(TextX, Position.Y + 13.0f), TextW, GetParameterDisplayName(Parameter.ParameterName), FAppStyle::GetFontStyle("SmallFontBold"), Theme.Text);
		DrawTextInRect(OutDrawElements, Geometry, Layer, FVector2D(TextX, Position.Y + 36.0f), TextW, TextureName, FAppStyle::GetFontStyle("SmallFont"), Texture ? Theme.Selection : Theme.TextMuted);
		DrawTextInRect(OutDrawElements, Geometry, Layer, FVector2D(TextX, Position.Y + 58.0f), TextW, TexturePath.IsEmpty() ? TEXT("内容浏览器选择贴图后可一键替换") : TexturePath, FAppStyle::GetFontStyle("TinyText"), Theme.TextMuted);

		FPBRMagicMaterialPopupHit Hit;
		Hit.ParameterName = Parameter.ParameterName;
		DrawButton(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 212.0f, 24.0f), FVector2D(120.0f, 30.0f), TEXT("用选中贴图"), EPBRMagicMaterialPopupAction::TextureUseSelected, Theme, &Hit);
		DrawButton(OutDrawElements, Geometry, Layer, Position + FVector2D(Size.X - 82.0f, 24.0f), FVector2D(64.0f, 30.0f), TEXT("清空"), EPBRMagicMaterialPopupAction::TextureClear, Theme, &Hit);
	}

	void DrawScrollBar(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 Layer, const FVector2D& Position, const FVector2D& Size, const FPBRMagicTheme& Theme) const
	{
		DrawBox(OutDrawElements, Geometry, Layer, Position, Size, Theme.Background);
		if (MaxScrollOffset <= KINDA_SMALL_NUMBER)
		{
			DrawBox(OutDrawElements, Geometry, Layer + 1, Position, Size, Theme.Border);
			return;
		}

		const float ThumbH = FMath::Clamp(Size.Y * (Size.Y / (Size.Y + MaxScrollOffset)), 32.0f, Size.Y);
		const float ThumbY = Position.Y + (Size.Y - ThumbH) * (ScrollOffset / MaxScrollOffset);
		DrawBox(OutDrawElements, Geometry, Layer + 1, FVector2D(Position.X, ThumbY), FVector2D(Size.X, ThumbH), Theme.Primary);
	}

	void HandleHit(const FPBRMagicMaterialPopupHit& Hit, const FVector2D& LocalPosition)
	{
		switch (Hit.Action)
		{
		case EPBRMagicMaterialPopupAction::Search:
			bSearchActive = true;
			break;
		case EPBRMagicMaterialPopupAction::TypePrevious:
			bSearchActive = false;
			OwnerWindow->CycleEditableMaterialType(-1);
			ScrollOffset = 0.0f;
			break;
		case EPBRMagicMaterialPopupAction::TypeNext:
			bSearchActive = false;
			OwnerWindow->CycleEditableMaterialType(1);
			ScrollOffset = 0.0f;
			break;
		case EPBRMagicMaterialPopupAction::ScalarDown:
			bSearchActive = false;
			OwnerWindow->StepEditableMaterialScalar(Hit.ParameterName, -Hit.ScalarStep, Hit.ScalarMin, Hit.ScalarMax, Hit.ScalarDefault);
			break;
		case EPBRMagicMaterialPopupAction::ScalarUp:
			bSearchActive = false;
			OwnerWindow->StepEditableMaterialScalar(Hit.ParameterName, Hit.ScalarStep, Hit.ScalarMin, Hit.ScalarMax, Hit.ScalarDefault);
			break;
		case EPBRMagicMaterialPopupAction::ScalarTrack:
			bSearchActive = false;
			ApplyScalarTrack(Hit, LocalPosition.X);
			break;
		case EPBRMagicMaterialPopupAction::VectorTrack:
			bSearchActive = false;
			ApplyVectorTrack(Hit, LocalPosition.X);
			break;
		case EPBRMagicMaterialPopupAction::SwitchToggle:
			bSearchActive = false;
			OwnerWindow->CommitEditableMaterialSwitch(Hit.ParameterName, !OwnerWindow->GetEditableMaterialSwitch(Hit.ParameterName, Hit.bSwitchDefault));
			break;
		case EPBRMagicMaterialPopupAction::TextureUseSelected:
			bSearchActive = false;
			UseSelectedTexture(Hit.ParameterName);
			break;
		case EPBRMagicMaterialPopupAction::TextureClear:
			bSearchActive = false;
			OwnerWindow->CommitEditableMaterialTexture(Hit.ParameterName, nullptr);
			break;
		case EPBRMagicMaterialPopupAction::None:
		default:
			break;
		}
	}

	void ApplyScalarTrack(const FPBRMagicMaterialPopupHit& Hit, float LocalX)
	{
		const float Alpha = FMath::Clamp((LocalX - Hit.Rect.Left) / FMath::Max(1.0f, Hit.Rect.Right - Hit.Rect.Left), 0.0f, 1.0f);
		const float Value = FMath::Lerp(Hit.ScalarMin, Hit.ScalarMax, Alpha);
		OwnerWindow->CommitEditableMaterialScalar(Hit.ParameterName, Value, Hit.ScalarMin, Hit.ScalarMax);
	}

	void ApplyVectorTrack(const FPBRMagicMaterialPopupHit& Hit, float LocalX)
	{
		const float Value = FMath::Clamp((LocalX - Hit.Rect.Left) / FMath::Max(1.0f, Hit.Rect.Right - Hit.Rect.Left), 0.0f, 1.0f);
		OwnerWindow->CommitEditableMaterialVectorChannel(Hit.ParameterName, Hit.VectorChannel, Value, Hit.VectorDefault);
	}

	void UseSelectedTexture(const FName& ParameterName)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> SelectedAssets;
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
		for (const FAssetData& AssetData : SelectedAssets)
		{
			if (UTexture* Texture = Cast<UTexture>(AssetData.GetAsset()))
			{
				OwnerWindow->CommitEditableMaterialTexture(ParameterName, Texture);
				return;
			}
		}
		OwnerWindow->StatusMessage = TEXT("请先在内容浏览器中选择一个贴图资产");
	}

	SPBRMagicOutlinerWindow* OwnerWindow = nullptr;
	mutable TArray<FPBRMagicMaterialPopupHit> HitRegions;
	mutable float ScrollOffset = 0.0f;
	mutable float MaxScrollOffset = 0.0f;
	mutable EPBRMagicMaterialPopupAction HoveredAction = EPBRMagicMaterialPopupAction::None;
	mutable EPBRMagicMaterialPopupAction PressedAction = EPBRMagicMaterialPopupAction::None;
	mutable FSlateRect HoveredRect;
	mutable FSlateRect PressedRect;
	FString SearchText;
	bool bSearchActive = false;
};

SPBRMagicOutlinerWindow::~SPBRMagicOutlinerWindow()
{
	FEditorDelegates::PostUndoRedo.RemoveAll(this);
	if (TSharedPtr<SWindow> Window = MaterialParameterWindow.Pin())
	{
		Window->RequestDestroyWindow();
		MaterialParameterWindow.Reset();
	}
	if (MaterialThumbnailPool.IsValid())
	{
		MaterialThumbnailPool->OnThumbnailRendered().RemoveAll(this);
		MaterialThumbnailPool->OnThumbnailRenderFailed().RemoveAll(this);
	}
}

void SPBRMagicOutlinerWindow::Construct(const FArguments& InArgs)
{
	IsolationShortcut = FInputChord(EKeys::Q, EModifierKey::Alt);
	LoadMagicOutlinerSettings();
	MaterialThumbnailPool = MakeShared<FAssetThumbnailPool>(128);
	MaterialThumbnailPool->OnThumbnailRendered().AddSP(this, &SPBRMagicOutlinerWindow::HandleMaterialThumbnailUpdated);
	MaterialThumbnailPool->OnThumbnailRenderFailed().AddSP(this, &SPBRMagicOutlinerWindow::HandleMaterialThumbnailUpdated);
	StatusMessage = TEXT("准备就绪");
	FEditorDelegates::PostUndoRedo.AddSP(this, &SPBRMagicOutlinerWindow::HandlePostUndoRedo);

	RebuildRootContent();

	RegisterActiveTimer(0.35f, FWidgetActiveTimerDelegate::CreateSP(this, &SPBRMagicOutlinerWindow::SyncMaterialSelectionTimer));
	RebuildItems();
}

void SPBRMagicOutlinerWindow::RebuildRootContent()
{
	TreeView.Reset();
	NameCheckListView.Reset();
	CheckedListView.Reset();
	SelectedMaterialListView.Reset();
	ChildSlot
	[
		bUseClassicSkin ? BuildClassicRoot() : BuildPaintRoot()
	];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildPaintRoot()
{
	return SNew(SPBRMagicOutlinerPaintSurface)
		.OwnerWindow(this);
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildClassicRoot()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildTopTabs()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
		[
			BuildToolbar()
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 0, 8, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				BuildNameCheckPanel()
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
			[
				BuildSceneTreePanel()
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				BuildDetailsPanel()
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildStatusBar()
		];
}

FReply SPBRMagicOutlinerWindow::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (DoesKeyEventMatchShortcut(InKeyEvent))
	{
		return OnToggleIsolationClicked();
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

FReply SPBRMagicOutlinerWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (DoesKeyEventMatchShortcut(InKeyEvent))
	{
		return OnToggleIsolationClicked();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildTopTabs()
{
	const TArray<FPBRMagicTopTab> Tabs = {
		{ EPBRMagicOutlinerCategory::Models, PBRText(TEXT("ModelsTab"), TEXT("模型"), TEXT("Models")), "PBRStudio.Icon.TextureSuite" },
		{ EPBRMagicOutlinerCategory::Materials, PBRText(TEXT("MaterialsTab"), TEXT("材质"), TEXT("Materials")), "PBRStudio.Icon.MaterialVault" },
		{ EPBRMagicOutlinerCategory::Lights, PBRText(TEXT("LightsTab"), TEXT("灯光"), TEXT("Lights")), "PBRStudio.Icon.BatchAdjust" },
		{ EPBRMagicOutlinerCategory::Cameras, PBRText(TEXT("CamerasTabShort"), TEXT("相机/后期"), TEXT("Camera/Post")), "PBRStudio.Icon.CameraPost" },
		{ EPBRMagicOutlinerCategory::Blueprints, PBRText(TEXT("BlueprintsTab"), TEXT("蓝图"), TEXT("Blueprints")), "PBRStudio.Icon.BatchAdjust" },
		{ EPBRMagicOutlinerCategory::Levels, PBRText(TEXT("LevelsTab"), TEXT("关卡"), TEXT("Levels")), "PBRStudio.Icon.MagicOutliner" },
		{ EPBRMagicOutlinerCategory::All, PBRText(TEXT("AllTab"), TEXT("全部"), TEXT("All")), "PBRStudio.Icon.MagicOutliner" }
	};

	TSharedRef<SHorizontalBox> TabBox = SNew(SHorizontalBox);
	for (const FPBRMagicTopTab& Tab : Tabs)
	{
		TabBox->AddSlot().AutoWidth().Padding(0, 0, 22, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(14, 8))
			.ButtonColorAndOpacity_Lambda([this, Tab]()
			{
				return ActiveCategory == Tab.Category
					? FSlateColor(GetThemeColor(TEXT("DropZone")))
					: FSlateColor(FLinearColor::Transparent);
			})
		.OnClicked_Lambda([this, Tab]()
		{
			ActiveCategory = Tab.Category;
			ActiveMode = EPBRMagicOutlinerMode::Type;
			RebuildItems();
			return FReply::Handled();
		})
			[                    
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
				[
					SNew(SBox)
					.WidthOverride(32)
					.HeightOverride(32)
					[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image(FPBRStudioStyle::Get().GetBrush(Tab.Icon))
						.ColorAndOpacity_Lambda([this, Tab]()
						{
							return ActiveCategory == Tab.Category
								? FSlateColor(FLinearColor::White)
								: FSlateColor(FLinearColor(0.74f, 0.76f, 0.78f, 1.0f));
						})
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					[
						SNew(SBox)
						.WidthOverride(10)
						.HeightOverride(10)
						.Visibility_Lambda([this, Tab]()
						{
							return ActiveCategory == Tab.Category ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Primary")); })
							.Padding(1)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush("Icons.Check"))
								.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("SelectionText"))); })
							]
						]
					]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Tab.Label)
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity_Lambda([this, Tab]()
					{
						return ActiveCategory == Tab.Category
							? FSlateColor(GetThemeColor(TEXT("Text")))
							: FSlateColor(GetThemeColor(TEXT("TextMuted")));
					})
				]
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Background")); })
		.Padding(FMargin(18, 12, 18, 10))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("PanelRaised")); })
					.Padding(FMargin(8, 5))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MagicOutlinerARBadge", "AR"))
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MagicOutlinerShellTitle", "AR Studio / PBR Studio"))
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor::Transparent)
					.Padding(FMargin(0, 6))
					[
						SNew(STextBlock)
						.Text(PBRText(TEXT("MagicOutlinerTitleBilingual"), TEXT("魔法大纲 / Magic Outliner"), TEXT("Magic Outliner")))
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.ContentPadding(FMargin(10, 5))
					.OnClicked(this, &SPBRMagicOutlinerWindow::OnToggleCompactModeClicked)
					[
						SNew(STextBlock)
						.Text(this, &SPBRMagicOutlinerWindow::GetCompactModeText)
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					BuildThemeSelector()
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
				.Padding(FMargin(10, 8))
				[
					TabBox
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildToolbar()
{
	auto MakeButton = [this](const FText& Text, const FName Icon, const FOnClicked& OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(9, 5))
			.ButtonColorAndOpacity(FLinearColor::Transparent)
			.OnClicked(OnClicked)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(SImage)
					.Image(FPBRStudioStyle::Get().GetBrush(Icon))
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Text)
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
				]
			];
	};
	auto MakeStateButton = [this](const FText& Text, const FName IconOff, const FName IconOn, TAttribute<bool> IsActive, const FOnClicked& OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(9, 5))
			.ButtonColorAndOpacity_Lambda([this, IsActive]()
			{
				return IsActive.Get() ? FSlateColor(GetThemeColor(TEXT("DropZone"))) : FSlateColor(FLinearColor::Transparent);
			})
			.OnClicked(OnClicked)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(SImage)
					.Image_Lambda([IconOff, IconOn, IsActive]()
					{
						return FPBRStudioStyle::Get().GetBrush(IsActive.Get() ? IconOn : IconOff);
					})
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Text)
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity_Lambda([this, IsActive]()
					{
						return FSlateColor(IsActive.Get() ? GetThemeColor(TEXT("Text")) : GetThemeColor(TEXT("TextMuted")));
					})
				]
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(FMargin(8, 6))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeButton(PBRText(TEXT("RefreshScene"), TEXT("刷新场景"), TEXT("Refresh Scene")), "PBRStudio.Toolbar.Refresh", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnRefreshClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeButton(PBRText(TEXT("SelectChecked"), TEXT("场景中选择勾选"), TEXT("Select Checked in Scene")), "PBRStudio.Toolbar.Select", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnSelectCheckedClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeButton(PBRText(TEXT("ToggleSelectedChecked"), TEXT("添加当前选择"), TEXT("Add Current Selection")), "PBRStudio.Toolbar.Add", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnToggleSelectedCheckedClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeStateButton(
						PBRText(TEXT("ToggleIsolation"), TEXT("孤立/退出孤立"), TEXT("Isolate / Exit Isolate")),
						"PBRStudio.Toolbar.Show",
						"PBRStudio.Toolbar.Hide",
						TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this]() { return bIsolationActive; })),
						FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnToggleIsolationClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeStateButton(
						PBRText(TEXT("ToggleCheckedVisibility"), TEXT("隐藏/显示勾选"), TEXT("Hide / Show Checked")),
						"PBRStudio.Toolbar.Show",
						"PBRStudio.Toolbar.Hide",
						TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this]() { return bCheckedActorsHidden; })),
						FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnToggleCheckedVisibilityClicked))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					MakeStateButton(
						PBRText(TEXT("AutoSelectCheckedActors"), TEXT("勾选后自动选择"), TEXT("Auto Select Checked")),
						"PBRStudio.Toolbar.Clear",
						"PBRStudio.Toolbar.Add",
						TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this]() { return bAutoSelectCheckedActors; })),
						FOnClicked::CreateLambda([this]()
						{
							bAutoSelectCheckedActors = !bAutoSelectCheckedActors;
							SaveMagicOutlinerSettings();
							if (bAutoSelectCheckedActors)
							{
								SyncAutoSelectCheckedActors();
							}
							return FReply::Handled();
						}))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeButton(PBRText(TEXT("ShortcutSettings"), TEXT("快捷键"), TEXT("Shortcuts")), "PBRStudio.Toolbar.Settings", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnShortcutSettingsClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeButton(PBRText(TEXT("FilterPlaceholderButton"), TEXT("过滤"), TEXT("Filter")), "PBRStudio.Toolbar.Filter", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnInvertCheckedClicked))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(SEditableTextBox)
					.HintText(PBRText(TEXT("MagicSearchHint"), TEXT("搜索名称、类型、使用、路径..."), TEXT("Search name, type, usage, path...")))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.Text_Lambda([this]() { return FText::FromString(ClassicSearchText); })
					.OnTextChanged_Lambda([this](const FText& NewText)
					{
						ClassicSearchText = NewText.ToString();
						RebuildItems();
					})
					.ForegroundColor_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					.BackgroundColor_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("PanelRaised"))); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("DropZone")); })
					.Padding(FMargin(10, 5))
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(FString::Printf(TEXT("已选中 %d 项"), CachedCheckedCount));
						})
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					MakeStateButton(
						PBRText(TEXT("ToggleClassicSkin"), TEXT("经典皮肤"), TEXT("Classic Skin")),
						"PBRStudio.Toolbar.Settings",
						"PBRStudio.Toolbar.Settings",
						TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this]() { return bUseClassicSkin; })),
						FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnToggleClassicSkinClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(PBRText(TEXT("ClearChecked"), TEXT("清空"), TEXT("Clear")), "PBRStudio.Toolbar.Clear", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnClearCheckedClicked))
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildThemeSelector()
{
	return SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton")
		.ContentPadding(FMargin(10, 5))
		.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("PanelRaised"))); })
		.MenuContent()
		[
			BuildThemeMenu()
		]
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.Color"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SPBRMagicOutlinerWindow::GetThemeButtonText)
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildThemeMenu()
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FPBRMagicTheme& Theme : GetMagicThemes())
	{
		Menu->AddSlot().AutoHeight().Padding(6, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(8, 6))
			.ButtonColorAndOpacity_Lambda([this, Theme]()
			{
				return FSlateColor(ActiveThemeId == Theme.Id ? Theme.Selection : Theme.PanelRaised);
			})
			.OnClicked(this, &SPBRMagicOutlinerWindow::OnThemeSelected, Theme.Id)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(SColorBlock)
					.Color(Theme.Primary)
					.Size(FVector2D(14.0f, 14.0f))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
				[
					SNew(SColorBlock)
					.Color(Theme.Selection)
					.Size(FVector2D(14.0f, 14.0f))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Theme.Label)
					.ColorAndOpacity_Lambda([this, Theme]()
					{
						return FSlateColor(ActiveThemeId == Theme.Id ? Theme.SelectionText : Theme.Text);
					})
				]
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(6)
		[
			Menu
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildSceneTreePanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(this, &SPBRMagicOutlinerWindow::GetScenePanelTitleText)
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(STextBlock)
						.Text(this, &SPBRMagicOutlinerWindow::GetScenePanelSummaryText)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
					.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("DropZone")); })
					.Padding(FMargin(9, 4))
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(FString::Printf(TEXT("%d checked"), CachedCheckedCount));
						})
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
					]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
				.Padding(0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("PanelRaised")); })
						.Padding(FMargin(12, 8))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
							[
								SNew(SCheckBox)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
							[
								SNew(SBox).WidthOverride(64)
								[
									SNew(STextBlock)
									.Text(PBRText(TEXT("PreviewColumnCustom"), TEXT("预览\nPreview"), TEXT("Preview")))
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
								]
							]
							+ SHorizontalBox::Slot().FillWidth(0.42f).VAlign(VAlign_Center).Padding(0, 0, 12, 0)
							[
								SNew(STextBlock)
								.Text(PBRText(TEXT("NameColumnCustom"), TEXT("名称\nName"), TEXT("Name")))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
							]
							+ SHorizontalBox::Slot().FillWidth(0.22f).VAlign(VAlign_Center).Padding(0, 0, 12, 0)
							[
								SNew(STextBlock)
								.Text(PBRText(TEXT("TypeColumnCustom"), TEXT("类型\nType"), TEXT("Type")))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
							[
								SNew(SBox).WidthOverride(64)
								[
									SNew(STextBlock)
									.Text(PBRText(TEXT("CountColumnCustom"), TEXT("数量\nCount"), TEXT("Count")))
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
								]
							]
							+ SHorizontalBox::Slot().FillWidth(0.36f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(PBRText(TEXT("UsageColumnCustom"), TEXT("使用 / 路径\nUsage / Path"), TEXT("Usage / Path")))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
							]
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0, 6, 0, 0)
					[
						SAssignNew(TreeView, STreeView<TSharedPtr<FPBRMagicOutlinerItem>>)
						.TreeItemsSource(&RootItems)
						.SelectionMode(ESelectionMode::Multi)
						.OnGenerateRow(this, &SPBRMagicOutlinerWindow::GenerateRow)
						.OnGetChildren(this, &SPBRMagicOutlinerWindow::GetItemChildren)
						.OnMouseButtonDoubleClick(this, &SPBRMagicOutlinerWindow::OnTreeItemDoubleClicked)
					]
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildStatusBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(FMargin(10, 5))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SPBRMagicOutlinerWindow::GetStatusText)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(PBRText(TEXT("UndoSafeHint"), TEXT("批量操作支持 Ctrl+Z"), TEXT("Bulk operations support Ctrl+Z")))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildDetailsPanel()
{
	auto MakeInspectorButton = [this](const FText& Text, const FName Icon, const FOnClicked& OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(10, 7))
			.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("PanelRaised"))); })
			.OnClicked(OnClicked)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
				[
					SNew(SImage)
					.Image(FPBRStudioStyle::Get().GetBrush(Icon))
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(Text)
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
				]
			];
	};

	return SNew(SBox)
		.WidthOverride(380)
		.Visibility(this, &SPBRMagicOutlinerWindow::GetDetailsPanelVisibility)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
			.Padding(12)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("PanelRaised")); })
					.Padding(FMargin(12, 10))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("MagicInspectorHeader"), TEXT("所选对象材质 / Selected Object Materials"), TEXT("Selected Object Materials")))
							.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
						[
							SNew(STextBlock)
							.Text(this, &SPBRMagicOutlinerWindow::GetCheckedSummaryText)
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.AutoWrapText(true)
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("TableRow")); })
					.Padding(10)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("BatchActionsHeader"), TEXT("批量操作 / Batch Actions"), TEXT("Batch Actions")))
							.Font(FAppStyle::GetFontStyle("SmallFontBold"))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 6, 0)
							[
								MakeInspectorButton(PBRText(TEXT("InspectorOpen"), TEXT("打开材质"), TEXT("Open")), "PBRStudio.Toolbar.Open", FOnClicked::CreateLambda([this]()
								{
									if (SelectedMaterialItems.Num() > 0)
									{
										OpenMaterialEditor(SelectedMaterialItems[0]);
									}
									return FReply::Handled();
								}))
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 6, 0)
							[
								MakeInspectorButton(PBRText(TEXT("InspectorSelectActors"), TEXT("选择关联Actor"), TEXT("Select Actors")), "PBRStudio.Toolbar.Select", FOnClicked::CreateLambda([this]()
								{
									if (SelectedMaterialItems.Num() > 0)
									{
										SelectItemActors(SelectedMaterialItems[0], false);
									}
									return FReply::Handled();
								}))
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								MakeInspectorButton(PBRText(TEXT("InspectorReset"), TEXT("重置为默认"), TEXT("Reset")), "PBRStudio.Toolbar.Refresh", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnClearCheckedClicked))
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					BuildModelToolsPanel()
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					BuildSelectedMaterialPanel()
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SVerticalBox)
					.Visibility(this, &SPBRMagicOutlinerWindow::GetCheckedActionsVisibility)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
					[
						SNew(STextBlock)
						.Text(PBRText(TEXT("OperationPanel"), TEXT("勾选对象操作区"), TEXT("Checked Object Actions")))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(this, &SPBRMagicOutlinerWindow::GetCheckedSummaryText)
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
					[
						BuildCheckedListPanel()
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
				[
					BuildLightAdjustPanel()
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
				[
					BuildCameraPostProcessPanel()
				]
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildNameCheckPanel()
{
	return SNew(SBox)
		.WidthOverride(200)
		.Visibility(this, &SPBRMagicOutlinerWindow::GetStandardControlsVisibility)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
			.Padding(12)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("SmartGroupsHeader"), TEXT("智能分组"), TEXT("Smart Groups")))
							.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("SmartGroupsSubHeader"), TEXT("按中文关键词快速勾选"), TEXT("Chinese keyword quick checks")))
							.Font(FAppStyle::GetFontStyle("TinyText"))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.ContentPadding(FMargin(6, 3))
						.ToolTipText(PBRText(TEXT("SmartGroupsRefreshTip"), TEXT("刷新智能分组"), TEXT("Refresh smart groups")))
						.OnClicked(this, &SPBRMagicOutlinerWindow::OnRefreshClicked)
						[
							SNew(SImage)
							.Image(FPBRStudioStyle::Get().GetBrush("PBRStudio.Toolbar.Refresh"))
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
						]
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(NameCheckListView, SListView<TSharedPtr<FPBRNameCheckListItem>>)
					.ListItemsSource(&NameCheckListItems)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SPBRMagicOutlinerWindow::GenerateNameCheckRow)
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildCheckedListPanel()
{
	auto MakeButton = [](const FText& Text, const FName Icon, const FOnClicked& OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(8, 5))
			.OnClicked(OnClicked)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
				[
					SNew(SImage)
					.Image(FPBRStudioStyle::Get().GetBrush(Icon))
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Text).Font(FAppStyle::GetFontStyle("SmallFontBold"))
				]
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(PBRText(TEXT("CheckedListHeader"), TEXT("勾选列表"), TEXT("Checked List")))
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::AsNumber(CachedCheckedCount); })
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SBox)
				.HeightOverride(190)
				[
					SAssignNew(CheckedListView, SListView<TSharedPtr<FPBRCheckedActorListItem>>)
					.ListItemsSource(&CheckedListItems)
					.SelectionMode(ESelectionMode::Multi)
					.OnGenerateRow(this, &SPBRMagicOutlinerWindow::GenerateCheckedActorRow)
					.OnSelectionChanged(this, &SPBRMagicOutlinerWindow::OnCheckedActorSelectionChanged)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					MakeButton(PBRText(TEXT("SelectCheckedList"), TEXT("选中列表项"), TEXT("Select List Items")), "PBRStudio.Toolbar.Select", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnSelectCheckedListClicked))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(PBRText(TEXT("RemoveCheckedListSelection"), TEXT("移除列表项"), TEXT("Remove List Items")), "PBRStudio.Toolbar.Clear", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnRemoveCheckedListSelectionClicked))
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildSelectedMaterialPanel()
{
	return SNew(SBorder)
		.Visibility(this, &SPBRMagicOutlinerWindow::GetSelectedMaterialVisibility)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(PBRText(TEXT("SelectedMaterialHeader"), TEXT("被选择的材质"), TEXT("Selected Material")))
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::AsNumber(SelectedMaterialItems.Num());
					})
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SPBRMagicOutlinerWindow::GetSelectedMaterialSummaryText)
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(260)
				[
					SAssignNew(SelectedMaterialListView, SListView<TSharedPtr<FPBRMagicOutlinerItem>>)
					.ListItemsSource(&SelectedMaterialItems)
					.SelectionMode(ESelectionMode::Multi)
					.OnGenerateRow(this, &SPBRMagicOutlinerWindow::GenerateSelectedMaterialRow)
					.OnMouseButtonDoubleClick(this, &SPBRMagicOutlinerWindow::OnTreeItemDoubleClicked)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[
				SNew(STextBlock)
				.Text(PBRText(TEXT("MaterialReplaceHint"), TEXT("拖入其它材质到左侧材质行，可替换场景中所有使用旧材质的材质槽；点“调参”可把当前槽位接管成 PBRStudio 可编辑材质。"), TEXT("Drop another material onto the material row to replace every matching scene slot. Use Adjust to make the current slot an editable PBRStudio material.")))
				.AutoWrapText(true)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildMaterialParameterPopupContent()
{
	TSharedRef<SVerticalBox> ParameterBox = SNew(SVerticalBox);
	const TArray<FPBRMagicDynamicMaterialParameter> DynamicParameters = CollectEditableDynamicMaterialParameters();
	if (DynamicParameters.Num() > 0)
	{
		FString LastGroupName;
		TArray<FPBRMagicDynamicMaterialParameter> GroupParameters;
		auto FlushDynamicGroup = [this, &ParameterBox, &LastGroupName, &GroupParameters]()
		{
			if (GroupParameters.Num() == 0)
			{
				return;
			}

			ParameterBox->AddSlot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				BuildDynamicMaterialParameterGroup(LastGroupName, GroupParameters)
			];
			GroupParameters.Reset();
		};

		for (const FPBRMagicDynamicMaterialParameter& Parameter : DynamicParameters)
		{
			if (Parameter.Group != LastGroupName)
			{
				FlushDynamicGroup();
				LastGroupName = Parameter.Group;
			}
			GroupParameters.Add(Parameter);
		}
		FlushDynamicGroup();
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("PanelRaised")); })
		.Padding(12)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(PBRText(TEXT("MagicMaterialPopupHeader"), TEXT("材质参数调节"), TEXT("Material Parameters")))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(STextBlock)
						.Text(this, &SPBRMagicOutlinerWindow::GetEditableMaterialNameText)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(STextBlock)
						.Text(this, &SPBRMagicOutlinerWindow::GetEditableMaterialSlotText)
						.Font(FAppStyle::GetFontStyle("TinyText"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.ContentPadding(FMargin(10, 5))
					.OnClicked(this, &SPBRMagicOutlinerWindow::OnApplyAIMaterialSuggestionClicked)
					[
						SNew(STextBlock)
						.Text(PBRText(TEXT("MagicMaterialAISuggest"), TEXT("AI 建议"), TEXT("AI Suggest")))
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.ContentPadding(FMargin(10, 5))
					.OnClicked(this, &SPBRMagicOutlinerWindow::OnMaterialAISettingsClicked)
					[
						SNew(STextBlock)
						.Text(PBRText(TEXT("MagicMaterialAISettings"), TEXT("AI 设置"), TEXT("AI Settings")))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(240.0f)
					[
						SNew(SComboButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.ContentPadding(FMargin(10, 5))
						.MenuContent()
						[
							BuildEditableMaterialTypeMenu()
						]
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(this, &SPBRMagicOutlinerWindow::GetEditableMaterialTypeText)
							.Font(FAppStyle::GetFontStyle("SmallFontBold"))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
						]
					]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					ParameterBox
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(STextBlock)
				.Text(PBRText(TEXT("MagicMaterialPopupHint"), TEXT("数值可直接输入；颜色点色块打开拾色器；贴图用勾选控制启用，并可通过资产框选择或清空。"), TEXT("Enter numbers directly; click color swatches for picker; use checkboxes and asset fields for textures.")))
				.Font(FAppStyle::GetFontStyle("TinyText"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildEditableMaterialTypeMenu()
{
	TSharedRef<SVerticalBox> MenuBox = SNew(SVerticalBox);
	for (const FPBRMagicMaterialTypeOption& Option : GetMagicMaterialTypeOptions())
	{
		MenuBox->AddSlot()
		.AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "Menu.Button")
			.OnClicked_Lambda([this, MaterialType = Option.Type]()
			{
				SelectEditableMaterialType(MaterialType);
				if (TSharedPtr<SWindow> ExistingWindow = MaterialParameterWindow.Pin())
				{
					ExistingWindow->SetContent(BuildMaterialParameterPopupContent());
				}
				FSlateApplication::Get().DismissAllMenus();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(PBRText(Option.Key, Option.Chinese, Option.English))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
	}
	return MenuBox;
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildMaterialAISettingsContent()
{
	const FString Provider = PBRMagicGetAIConfigString(TEXT("material_ai_provider"), TEXT("LocalRules"));
	const FString Endpoint = PBRMagicGetAIConfigString(TEXT("material_ai_endpoint"), TEXT(""));
	const FString Model = PBRMagicGetAIConfigString(TEXT("material_ai_model"), TEXT("gpt-4.1-mini"));
	const FString ApiKey = PBRMagicGetAIConfigString(TEXT("material_ai_api_key_or_env"), TEXT("PBRSTUDIO_AI_API_KEY"));
	AIModelOptions.Reset();
	AIModelOptions.Add(MakeShared<FString>(Model));

	auto ReadSettingsFromBoxes = [this]() -> FPBRMagicAIProviderSettings
	{
		FPBRMagicAIProviderSettings Settings;
		Settings.Provider = AIProviderBox.IsValid() ? AIProviderBox->GetText().ToString().TrimStartAndEnd() : TEXT("LocalRules");
		Settings.EndpointUrl = AIEndpointBox.IsValid() ? AIEndpointBox->GetText().ToString().TrimStartAndEnd() : FString();
		Settings.Model = AIModelBox.IsValid() ? AIModelBox->GetText().ToString().TrimStartAndEnd() : TEXT("gpt-4.1-mini");
		Settings.ApiKeyOrEnvironmentVariable = AIKeyBox.IsValid() ? AIKeyBox->GetText().ToString().TrimStartAndEnd() : TEXT("PBRSTUDIO_AI_API_KEY");
		return Settings;
	};

	auto SaveSettingsFromBoxes = [this, ReadSettingsFromBoxes]()
	{
		const FPBRMagicAIProviderSettings Settings = ReadSettingsFromBoxes();
		PBRMagicSaveAIConfigString(TEXT("material_ai_provider"), Settings.Provider.IsEmpty() ? TEXT("LocalRules") : Settings.Provider);
		PBRMagicSaveAIConfigString(TEXT("material_ai_endpoint"), Settings.EndpointUrl);
		PBRMagicSaveAIConfigString(TEXT("material_ai_model"), Settings.Model.IsEmpty() ? TEXT("gpt-4.1-mini") : Settings.Model);
		PBRMagicSaveAIConfigString(TEXT("material_ai_api_key_or_env"), Settings.ApiKeyOrEnvironmentVariable.IsEmpty() ? TEXT("PBRSTUDIO_AI_API_KEY") : Settings.ApiKeyOrEnvironmentVariable);
	};

	auto FetchModelsToBox = [this, ReadSettingsFromBoxes]() -> bool
	{
		FPBRMagicAIProviderSettings Settings = ReadSettingsFromBoxes();
		if (Settings.EndpointUrl.IsEmpty())
		{
			StatusMessage = TEXT("请先填写 API 接口地址");
			return false;
		}
		if (!Settings.Provider.Equals(TEXT("OpenAICompatible"), ESearchCase::IgnoreCase))
		{
			Settings.Provider = TEXT("OpenAICompatible");
			if (AIProviderBox.IsValid())
			{
				AIProviderBox->SetText(FText::FromString(Settings.Provider));
			}
		}

		TArray<FString> Models;
		FString Status;
		if (PBRMagicGetRemoteAIModels(Settings, Models, Status))
		{
			AIModelOptions.Reset();
			for (const FString& ModelId : Models)
			{
				AIModelOptions.Add(MakeShared<FString>(ModelId));
			}
			if (AIModelComboBox.IsValid())
			{
				AIModelComboBox->RefreshOptions();
			}
			if (AIModelBox.IsValid())
			{
				const FString CurrentModel = AIModelBox->GetText().ToString().TrimStartAndEnd();
				AIModelBox->SetText(FText::FromString(Models.Contains(CurrentModel) ? CurrentModel : Models[0]));
			}
			StatusMessage = Status + FString::Printf(TEXT("，当前模型：%s"), AIModelBox.IsValid() ? *AIModelBox->GetText().ToString() : TEXT(""));
			return true;
		}
		StatusMessage = Status;
		return false;
	};

	auto MakeRow = [this](const FText& Label, const TSharedRef<SWidget>& Control) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SBox)
				.WidthOverride(120.0f)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				Control
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(PBRText(TEXT("MaterialAISettingsTitle"), TEXT("AI 材质建议设置"), TEXT("AI Material Suggestion Settings")))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SComboButton)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(PBRText(TEXT("MaterialAICommonProviders"), TEXT("常用 AI"), TEXT("Common AI")))
					]
					.MenuContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("MaterialAICommonProvidersHint"), TEXT("选择后自动填入兼容接口"), TEXT("Pick one to fill compatible endpoint")))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("OpenAI")))
								.OnClicked_Lambda([this]()
								{
									const FPBRMagicAIProviderPreset& Preset = GetMagicAIProviderPresets()[0];
									if (AIProviderBox.IsValid()) { AIProviderBox->SetText(FText::FromString(TEXT("OpenAICompatible"))); }
									if (AIEndpointBox.IsValid()) { AIEndpointBox->SetText(FText::FromString(Preset.EndpointUrl)); }
									if (AIModelBox.IsValid()) { AIModelBox->SetText(FText::FromString(Preset.DefaultModel)); }
									if (AIKeyBox.IsValid()) { AIKeyBox->SetHintText(FText::FromString(Preset.KeyHint)); }
									StatusMessage = TEXT("已选择 OpenAI，可点击获取 API 打开密钥页面");
									return FReply::Handled();
								})
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("DeepSeek")))
								.OnClicked_Lambda([this]()
								{
									const FPBRMagicAIProviderPreset& Preset = GetMagicAIProviderPresets()[1];
									if (AIProviderBox.IsValid()) { AIProviderBox->SetText(FText::FromString(TEXT("OpenAICompatible"))); }
									if (AIEndpointBox.IsValid()) { AIEndpointBox->SetText(FText::FromString(Preset.EndpointUrl)); }
									if (AIModelBox.IsValid()) { AIModelBox->SetText(FText::FromString(Preset.DefaultModel)); }
									if (AIKeyBox.IsValid()) { AIKeyBox->SetHintText(FText::FromString(Preset.KeyHint)); }
									StatusMessage = TEXT("已选择 DeepSeek，可点击获取 API 打开密钥页面");
									return FReply::Handled();
								})
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("SiliconFlow")))
								.OnClicked_Lambda([this]()
								{
									const FPBRMagicAIProviderPreset& Preset = GetMagicAIProviderPresets()[2];
									if (AIProviderBox.IsValid()) { AIProviderBox->SetText(FText::FromString(TEXT("OpenAICompatible"))); }
									if (AIEndpointBox.IsValid()) { AIEndpointBox->SetText(FText::FromString(Preset.EndpointUrl)); }
									if (AIModelBox.IsValid()) { AIModelBox->SetText(FText::FromString(Preset.DefaultModel)); }
									if (AIKeyBox.IsValid()) { AIKeyBox->SetHintText(FText::FromString(Preset.KeyHint)); }
									StatusMessage = TEXT("已选择 SiliconFlow，可点击获取 API 打开密钥页面");
									return FReply::Handled();
								})
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("智谱/Z.AI")))
								.OnClicked_Lambda([this]()
								{
									const FPBRMagicAIProviderPreset& Preset = GetMagicAIProviderPresets()[3];
									if (AIProviderBox.IsValid()) { AIProviderBox->SetText(FText::FromString(TEXT("OpenAICompatible"))); }
									if (AIEndpointBox.IsValid()) { AIEndpointBox->SetText(FText::FromString(Preset.EndpointUrl)); }
									if (AIModelBox.IsValid()) { AIModelBox->SetText(FText::FromString(Preset.DefaultModel)); }
									if (AIKeyBox.IsValid()) { AIKeyBox->SetHintText(FText::FromString(Preset.KeyHint)); }
									StatusMessage = TEXT("已选择智谱/Z.AI，可点击获取 API 打开密钥页面");
									return FReply::Handled();
								})
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("OpenRouter")))
								.OnClicked_Lambda([this]()
								{
									const FPBRMagicAIProviderPreset& Preset = GetMagicAIProviderPresets()[4];
									if (AIProviderBox.IsValid()) { AIProviderBox->SetText(FText::FromString(TEXT("OpenAICompatible"))); }
									if (AIEndpointBox.IsValid()) { AIEndpointBox->SetText(FText::FromString(Preset.EndpointUrl)); }
									if (AIModelBox.IsValid()) { AIModelBox->SetText(FText::FromString(Preset.DefaultModel)); }
									if (AIKeyBox.IsValid()) { AIKeyBox->SetHintText(FText::FromString(Preset.KeyHint)); }
									StatusMessage = TEXT("已选择 OpenRouter，可点击获取 API 打开密钥页面");
									return FReply::Handled();
								})
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("Ollama 本地")))
								.OnClicked_Lambda([this]()
								{
									const FPBRMagicAIProviderPreset& Preset = GetMagicAIProviderPresets()[5];
									if (AIProviderBox.IsValid()) { AIProviderBox->SetText(FText::FromString(TEXT("OpenAICompatible"))); }
									if (AIEndpointBox.IsValid()) { AIEndpointBox->SetText(FText::FromString(Preset.EndpointUrl)); }
									if (AIModelBox.IsValid()) { AIModelBox->SetText(FText::FromString(Preset.DefaultModel)); }
									if (AIKeyBox.IsValid()) { AIKeyBox->SetHintText(FText::FromString(Preset.KeyHint)); }
									StatusMessage = TEXT("已选择本地 Ollama，可点击获取 API 打开下载页面");
									return FReply::Handled();
								})
							]
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(PBRText(TEXT("MaterialAIOpenApiPage"), TEXT("获取 API"), TEXT("Get API")))
					.OnClicked_Lambda([this]()
					{
						const FString EndpointText = AIEndpointBox.IsValid() ? AIEndpointBox->GetText().ToString() : FString();
						const FPBRMagicAIProviderPreset* Preset = FindMagicAIProviderPresetByEndpoint(EndpointText);
						const FString Url = Preset ? FString(Preset->ApiPageUrl) : FString(TEXT("https://platform.openai.com/api-keys"));
						FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
						StatusMessage = FString::Printf(TEXT("已打开 API 页面：%s"), *Url);
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(PBRText(TEXT("MaterialAIFetchModels"), TEXT("获取模型"), TEXT("Fetch Models")))
					.OnClicked_Lambda([FetchModelsToBox]()
					{
						FetchModelsToBox();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(PBRText(TEXT("MaterialAIValidate"), TEXT("验证"), TEXT("Validate")))
					.OnClicked_Lambda([this, ReadSettingsFromBoxes]()
					{
						FString Status;
						const bool bOk = PBRMagicValidateRemoteAIProvider(ReadSettingsFromBoxes(), Status);
						StatusMessage = bOk ? Status : FString::Printf(TEXT("AI 接口验证失败：%s"), *Status);
						return FReply::Handled();
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeRow(PBRText(TEXT("MaterialAIProvider"), TEXT("接口类型"), TEXT("Provider")),
					SAssignNew(AIProviderBox, SEditableTextBox).Text(FText::FromString(Provider)))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeRow(PBRText(TEXT("MaterialAIEndpoint"), TEXT("接口地址"), TEXT("Endpoint")),
					SAssignNew(AIEndpointBox, SEditableTextBox)
					.Text(FText::FromString(Endpoint))
					.HintText(FText::FromString(TEXT("https://api.openai.com/v1 或兼容地址")))
					.OnTextCommitted_Lambda([FetchModelsToBox](const FText&, ETextCommit::Type CommitType)
					{
						if (CommitType != ETextCommit::OnCleared)
						{
							FetchModelsToBox();
						}
					}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeRow(PBRText(TEXT("MaterialAIModel"), TEXT("模型"), TEXT("Model")),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
					[
						SAssignNew(AIModelBox, SEditableTextBox).Text(FText::FromString(Model))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(AIModelComboBox, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&AIModelOptions)
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
						{
							return SNew(STextBlock)
								.Text(FText::FromString(Item.IsValid() ? *Item : FString()));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
						{
							if (Item.IsValid() && AIModelBox.IsValid())
							{
								AIModelBox->SetText(FText::FromString(*Item));
							}
						})
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("MaterialAIModelDropdown"), TEXT("选择"), TEXT("Pick")))
						]
					])
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				MakeRow(PBRText(TEXT("MaterialAIKey"), TEXT("密钥/环境变量"), TEXT("Key / Env")),
					SAssignNew(AIKeyBox, SEditableTextBox).Text(FText::FromString(ApiKey)))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(PBRText(TEXT("MaterialAISettingsHint"), TEXT("接口类型填 LocalRules 使用本地规则；填 OpenAICompatible 时会请求兼容 /chat/completions 的接口。密钥栏可填环境变量名，也可直接填 sk- / glm- 开头的密钥。接口失败时会自动回退本地规则。"), TEXT("Use LocalRules for local suggestions. Use OpenAICompatible for a /chat/completions compatible endpoint. The key field accepts an environment variable name or an inline sk-/glm- key. Failed remote calls fall back to local rules.")))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{
					return FText::FromString(StatusMessage.IsEmpty() ? TEXT("状态：等待设置") : FString::Printf(TEXT("状态：%s"), *StatusMessage));
				})
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(PBRText(TEXT("MaterialAIUseLocal"), TEXT("使用本地规则"), TEXT("Use Local Rules")))
					.OnClicked_Lambda([this]()
					{
						if (AIProviderBox.IsValid())
						{
							AIProviderBox->SetText(FText::FromString(TEXT("LocalRules")));
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(PBRText(TEXT("MaterialAISaveSettings"), TEXT("保存"), TEXT("Save")))
					.OnClicked_Lambda([this, SaveSettingsFromBoxes]()
					{
						SaveSettingsFromBoxes();
						StatusMessage = TEXT("AI 材质建议设置已保存");
						if (TSharedPtr<SWindow> ExistingWindow = MaterialAISettingsWindow.Pin())
						{
							ExistingWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildMaterialParameterControl(const FPBRMagicEditableMaterialParameter& Parameter)
{
	switch (Parameter.Kind)
	{
	case EPBRMagicEditableMaterialParameterKind::Color:
		return BuildMaterialVectorControl(Parameter.GetLabel(), Parameter.ParameterName, Parameter.DefaultColor);
	case EPBRMagicEditableMaterialParameterKind::Switch:
		return BuildMaterialSwitchControl(Parameter.GetLabel(), Parameter.ParameterName, Parameter.bDefaultSwitchValue);
	case EPBRMagicEditableMaterialParameterKind::Scalar:
	default:
		return BuildMaterialScalarControl(Parameter.GetLabel(), Parameter.ParameterName, Parameter.MinValue, Parameter.MaxValue, Parameter.DefaultValue, Parameter.StepValue);
	}
}

TArray<FPBRMagicDynamicMaterialParameter> SPBRMagicOutlinerWindow::CollectEditableDynamicMaterialParameters() const
{
	TArray<FPBRMagicDynamicMaterialParameter> Parameters;
	const UMaterialInstanceConstant* Instance = GetEditableMaterialInstance();
	if (!Instance)
	{
		return Parameters;
	}

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> ScalarParameters;
	Instance->GetAllParametersOfType(EMaterialParameterType::Scalar, ScalarParameters);
	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : ScalarParameters)
	{
		float Value = Pair.Value.Value.Type == EMaterialParameterType::Scalar ? Pair.Value.Value.AsScalar() : 0.0f;
		Instance->GetScalarParameterValue(Pair.Key, Value);

		FPBRMagicDynamicMaterialParameter Parameter;
		Parameter.Kind = EPBRMagicDynamicMaterialParameterKind::Scalar;
		Parameter.ParameterName = Pair.Key.Name;
		Parameter.Group = NormalizeMagicDynamicParameterGroup(Parameter.ParameterName.ToString(), GetMagicDynamicParameterGroup(Pair.Value));
		Parameter.SortPriority = GetMagicDynamicParameterSortPriority(Pair.Value);
		Parameter.ScalarValue = Value;
#if WITH_EDITORONLY_DATA
		Parameter.ScalarMin = Pair.Value.ScalarMin;
		Parameter.ScalarMax = Pair.Value.ScalarMax;
#endif
		NormalizeMagicDynamicScalarRange(Parameter);
		AddMagicDynamicParameter(Parameters, MoveTemp(Parameter));
	}

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> VectorParameters;
	Instance->GetAllParametersOfType(EMaterialParameterType::Vector, VectorParameters);
	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : VectorParameters)
	{
		FLinearColor Value = Pair.Value.Value.Type == EMaterialParameterType::Vector ? Pair.Value.Value.AsLinearColor() : FLinearColor::White;
		Instance->GetVectorParameterValue(Pair.Key, Value);

		FPBRMagicDynamicMaterialParameter Parameter;
		Parameter.Kind = EPBRMagicDynamicMaterialParameterKind::Color;
		Parameter.ParameterName = Pair.Key.Name;
		Parameter.Group = NormalizeMagicDynamicParameterGroup(Parameter.ParameterName.ToString(), GetMagicDynamicParameterGroup(Pair.Value));
		Parameter.SortPriority = GetMagicDynamicParameterSortPriority(Pair.Value);
		Parameter.ColorValue = Value;
		AddMagicDynamicParameter(Parameters, MoveTemp(Parameter));
	}

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> TextureParameters;
	Instance->GetAllParametersOfType(EMaterialParameterType::Texture, TextureParameters);
	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : TextureParameters)
	{
		UTexture* Texture = nullptr;
		Instance->GetTextureParameterValue(Pair.Key, Texture);

		FPBRMagicDynamicMaterialParameter Parameter;
		Parameter.Kind = EPBRMagicDynamicMaterialParameterKind::Texture;
		Parameter.ParameterName = Pair.Key.Name;
		Parameter.Group = NormalizeMagicDynamicParameterGroup(Parameter.ParameterName.ToString(), GetMagicDynamicParameterGroup(Pair.Value));
		Parameter.SortPriority = GetMagicDynamicParameterSortPriority(Pair.Value);
		Parameter.TextureValue = Texture;
		AddMagicDynamicParameter(Parameters, MoveTemp(Parameter));
	}

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> SwitchParameters;
	Instance->GetAllParametersOfType(EMaterialParameterType::StaticSwitch, SwitchParameters);
	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : SwitchParameters)
	{
		bool bValue = Pair.Value.Value.Type == EMaterialParameterType::StaticSwitch ? Pair.Value.Value.AsStaticSwitch() : false;
		FGuid ExpressionGuid;
		Instance->GetStaticSwitchParameterValue(Pair.Key, bValue, ExpressionGuid);

		FPBRMagicDynamicMaterialParameter Parameter;
		Parameter.Kind = EPBRMagicDynamicMaterialParameterKind::Switch;
		Parameter.ParameterName = Pair.Key.Name;
		Parameter.Group = NormalizeMagicDynamicParameterGroup(Parameter.ParameterName.ToString(), GetMagicDynamicParameterGroup(Pair.Value));
		Parameter.SortPriority = GetMagicDynamicParameterSortPriority(Pair.Value);
		Parameter.bSwitchValue = bValue;
		AddMagicDynamicParameter(Parameters, MoveTemp(Parameter));
	}

	TMap<FName, FString> ChannelGroups;
	for (const FPBRMagicDynamicMaterialParameter& Parameter : Parameters)
	{
		FName ChannelName = InferMagicChannelFromMaterialParameter(Parameter.ParameterName);
		if (!ChannelName.IsNone())
		{
			FName UVChannelName;
			bool bIsUVSwitch = false;
			bool bIsUVScalar = false;
			if (!GetMagicChannelUVInfo(Parameter.ParameterName, UVChannelName, bIsUVSwitch, bIsUVScalar))
			{
				ChannelGroups.FindOrAdd(ChannelName) = Parameter.Group;
			}
		}
	}

	for (FPBRMagicDynamicMaterialParameter& Parameter : Parameters)
	{
		FName UVChannelName;
		bool bIsUVSwitch = false;
		bool bIsUVScalar = false;
		if (GetMagicChannelUVInfo(Parameter.ParameterName, UVChannelName, bIsUVSwitch, bIsUVScalar))
		{
			if (const FString* GroupName = ChannelGroups.Find(UVChannelName))
			{
				Parameter.Group = *GroupName;
			}
			Parameter.SortPriority += bIsUVSwitch ? 900 : 910;
		}
	}

	Parameters.Sort([](const FPBRMagicDynamicMaterialParameter& A, const FPBRMagicDynamicMaterialParameter& B)
	{
		if (A.Group != B.Group)
		{
			return A.Group < B.Group;
		}
		if (A.SortPriority != B.SortPriority)
		{
			return A.SortPriority < B.SortPriority;
		}
		if (A.ParameterName != B.ParameterName)
		{
			return A.ParameterName.ToString() < B.ParameterName.ToString();
		}
		return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
	});

	return Parameters;
}

bool SPBRMagicOutlinerWindow::IsDynamicMaterialParameterVisible(const FPBRMagicDynamicMaterialParameter& Parameter) const
{
	FName UVChannelName;
	bool bIsUVSwitch = false;
	bool bIsUVScalar = false;
	if (GetMagicChannelUVInfo(Parameter.ParameterName, UVChannelName, bIsUVSwitch, bIsUVScalar) && bIsUVScalar)
	{
		const FPBRChannelUVParameterNames ChannelUV = FPBRMaterialParameters::GetChannelUVNames(UVChannelName);
		return GetEditableMaterialSwitch(ChannelUV.UseIndependentUV, false);
	}
	return true;
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildDynamicMaterialParameterGroup(const FString& GroupName, const TArray<FPBRMagicDynamicMaterialParameter>& Parameters)
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	for (const FPBRMagicDynamicMaterialParameter& Parameter : Parameters)
	{
		Body->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 5)
		[
			SNew(SBox)
			.Visibility_Lambda([this, Parameter]()
			{
				return IsDynamicMaterialParameterVisible(Parameter) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				BuildDynamicMaterialParameterControl(Parameter)
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(6)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.AreaTitle(FText::FromString(GroupName))
			.HeaderPadding(FMargin(0, 2))
			.Padding(FMargin(0, 6, 0, 0))
			.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
			.BodyContent()
			[
				Body
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildDynamicMaterialParameterControl(const FPBRMagicDynamicMaterialParameter& Parameter)
{
	const FText Label = FText::FromString(GetMagicMaterialParameterDisplayName(Parameter.ParameterName));
	switch (Parameter.Kind)
	{
	case EPBRMagicDynamicMaterialParameterKind::Color:
		return BuildMaterialVectorControl(Label, Parameter.ParameterName, Parameter.ColorValue);
	case EPBRMagicDynamicMaterialParameterKind::Switch:
		return BuildMaterialSwitchControl(Label, Parameter.ParameterName, Parameter.bSwitchValue);
	case EPBRMagicDynamicMaterialParameterKind::Texture:
		return BuildMaterialTextureControl(Label, Parameter.ParameterName);
	case EPBRMagicDynamicMaterialParameterKind::Scalar:
	default:
		return BuildMaterialScalarControl(Label, Parameter.ParameterName, Parameter.ScalarMin, Parameter.ScalarMax, Parameter.ScalarValue, Parameter.ScalarStep);
	}
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildMaterialScalarControl(const FText& Label, const FName& ParameterName, float MinValue, float MaxValue, float DefaultValue, float StepValue)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.36f).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("SmallFont"))
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.64f)
		[
			SNew(SNumericEntryBox<float>)
			.AllowSpin(true)
			.MinValue(MinValue)
			.MaxValue(MaxValue)
			.MinSliderValue(MinValue)
			.MaxSliderValue(MaxValue)
			.Delta(StepValue)
			.Value_Lambda([this, ParameterName, DefaultValue]()
			{
				return GetEditableMaterialScalar(ParameterName, DefaultValue);
			})
			.OnValueChanged_Lambda([this, ParameterName, MinValue, MaxValue](float NewValue)
			{
				CommitEditableMaterialScalar(ParameterName, NewValue, MinValue, MaxValue);
			})
			.OnValueCommitted_Lambda([this, ParameterName, MinValue, MaxValue](float NewValue, ETextCommit::Type)
			{
				CommitEditableMaterialScalar(ParameterName, NewValue, MinValue, MaxValue);
			})
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildMaterialVectorControl(const FText& Label, const FName& ParameterName, const FLinearColor& DefaultValue)
{
	auto GetCurrentColor = [this, ParameterName, DefaultValue]()
	{
		FLinearColor Value = DefaultValue;
		if (UMaterialInstanceConstant* Instance = GetEditableMaterialInstance())
		{
			Instance->GetVectorParameterValue(FMaterialParameterInfo(ParameterName), Value);
		}
		return Value;
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("SmallFont"))
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2))
			.OnClicked_Lambda([this, ParameterName, DefaultValue]()
			{
				OpenEditableMaterialColorPicker(ParameterName, DefaultValue);
				return FReply::Handled();
			})
			[
				SNew(SColorBlock)
				.Color_Lambda([GetCurrentColor]() { return GetCurrentColor(); })
				.Size(FVector2D(64.0f, 24.0f))
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildMaterialTextureControl(const FText& Label, const FName& ParameterName)
{
	const FName SwitchParameterName = GetMagicTextureUsageSwitchName(ParameterName);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Visibility(SwitchParameterName.IsNone() ? EVisibility::Collapsed : EVisibility::Visible)
				.IsChecked_Lambda([this, SwitchParameterName]()
				{
					return GetEditableMaterialSwitch(SwitchParameterName, true) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, SwitchParameterName](ECheckBoxState NewState)
				{
					if (!SwitchParameterName.IsNone())
					{
						CommitEditableMaterialSwitch(SwitchParameterName, NewState == ECheckBoxState::Checked);
					}
				})
				[
					SNew(STextBlock)
					.Text(PBRText(TEXT("MagicTextureUseCheckbox"), TEXT("使用贴图"), TEXT("Use Texture")))
					.Font(FAppStyle::GetFontStyle("TinyText"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UTexture::StaticClass())
			.AllowClear(true)
			.AllowCreate(false)
			.DisplayUseSelected(true)
			.DisplayBrowse(true)
			.DisplayThumbnail(true)
			.ThumbnailPool(MaterialThumbnailPool)
			.ObjectPath_Lambda([this, ParameterName]()
			{
				if (UTexture* Texture = GetEditableMaterialTexture(ParameterName))
				{
					return Texture->GetPathName();
				}
				return FString();
			})
			.OnObjectChanged_Lambda([this, ParameterName](const FAssetData& AssetData)
			{
				CommitEditableMaterialTexture(ParameterName, Cast<UTexture>(AssetData.GetAsset()));
			})
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildMaterialSwitchControl(const FText& Label, const FName& ParameterName, bool bDefaultValue)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.65f).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("SmallFont"))
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this, ParameterName, bDefaultValue]()
			{
				return GetEditableMaterialSwitch(ParameterName, bDefaultValue) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, ParameterName](ECheckBoxState NewState)
			{
				CommitEditableMaterialSwitch(ParameterName, NewState == ECheckBoxState::Checked);
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this, ParameterName, bDefaultValue]()
			{
				return GetEditableMaterialSwitch(ParameterName, bDefaultValue)
					? PBRText(TEXT("MagicSwitchEnabled"), TEXT("启用"), TEXT("On"))
					: PBRText(TEXT("MagicSwitchDisabled"), TEXT("关闭"), TEXT("Off"));
			})
			.Font(FAppStyle::GetFontStyle("TinyText"))
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildModelToolsPanel()
{
	auto MakeButton = [](const FText& Text, const FName Icon, const FOnClicked& OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(8, 6))
			.OnClicked(OnClicked)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(SImage)
					.Image(FPBRStudioStyle::Get().GetBrush(Icon))
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Text).Font(FAppStyle::GetFontStyle("SmallFontBold"))
				]
			];
	};

	return SNew(SBorder)
		.Visibility_Lambda([this]() { return ActiveCategory == EPBRMagicOutlinerCategory::Models ? EVisibility::Visible : EVisibility::Collapsed; })
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(PBRText(TEXT("ModelBatchToolsClassic"), TEXT("模型批量工具"), TEXT("Model Batch Tools")))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(PBRText(TEXT("ModelBatchToolsHint"), TEXT("对勾选模型或当前场景选择执行批量重命名、成组和 Static Mesh 替换。"), TEXT("Batch rename, group, and Static Mesh replace checked or selected models.")))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					MakeButton(PBRText(TEXT("ModelBatchRename"), TEXT("批量重命名"), TEXT("Batch Rename")), "PBRStudio.Toolbar.Settings", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnModelBatchRenameClicked))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					MakeButton(PBRText(TEXT("ModelReplaceActors"), TEXT("替换物体"), TEXT("Replace Meshes")), "PBRStudio.Toolbar.Select", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnModelReplaceActorsClicked))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeButton(PBRText(TEXT("ModelGroupToFolder"), TEXT("成组到文件夹"), TEXT("Group to Folder")), "PBRStudio.Toolbar.Add", FOnClicked::CreateSP(this, &SPBRMagicOutlinerWindow::OnModelGroupToFolderClicked))
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildLightAdjustPanel()
{
	auto MakeSliderRow = [this](const FText& Label, const FText& ValueText, float Value, const FOnFloatValueChanged& OnChanged)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text(ValueText)
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSlider).Value(Value).OnValueChanged(OnChanged)
			];
	};

	return SNew(SBorder)
		.Visibility(this, &SPBRMagicOutlinerWindow::GetLightAdjustVisibility)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(PBRText(TEXT("LightAdjustHeader"), TEXT("灯光参数"), TEXT("Light Parameters")))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				MakeSliderRow(
					PBRText(TEXT("LightIntensityMultiplier"), TEXT("亮度倍率"), TEXT("Intensity Multiplier")),
					FText::FromString(FString::Printf(TEXT("%.2fx"), LightIntensityMultiplier)),
					FMath::Clamp(LightIntensityMultiplier / 5.0f, 0.0f, 1.0f),
					FOnFloatValueChanged::CreateLambda([this](float Value)
					{
						LightIntensityMultiplier = FMath::Clamp(Value * 5.0f, 0.0f, 5.0f);
						ApplyLightAdjustmentsRealtime();
					}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				MakeSliderRow(
					PBRText(TEXT("LightTemperature"), TEXT("色温"), TEXT("Temperature")),
					FText::FromString(FString::Printf(TEXT("%.0fK"), LightTemperature)),
					FMath::Clamp((LightTemperature - 1700.0f) / 10300.0f, 0.0f, 1.0f),
					FOnFloatValueChanged::CreateLambda([this](float Value)
					{
						LightTemperature = FMath::Lerp(1700.0f, 12000.0f, FMath::Clamp(Value, 0.0f, 1.0f));
						ApplyLightAdjustmentsRealtime();
					}))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.ContentPadding(FMargin(8, 6))
				.OnClicked(this, &SPBRMagicOutlinerWindow::OnLightColorBlockClicked)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SNew(SColorBlock)
						.Color_Lambda([this]() { return LightColor; })
						.Size(FVector2D(36.0f, 18.0f))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(PBRText(TEXT("OpenLightColorPicker"), TEXT("灯光颜色"), TEXT("Light Color")))
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					]
				]
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildCameraPostProcessPanel()
{
	auto MakeButtonText = [](const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(FAppStyle::GetFontStyle("SmallFontBold"));
	};

	auto MakeResetButton = [](const FSimpleDelegate& OnReset)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(6, 3))
			.ToolTipText(PBRText(TEXT("ResetPostProcessParameterTooltip"), TEXT("恢复默认"), TEXT("Reset to default")))
			.OnClicked_Lambda([OnReset]()
			{
				OnReset.ExecuteIfBound();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(PBRText(TEXT("ResetPostProcessParameter"), TEXT("重置"), TEXT("Reset")))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			];
	};

	auto MakeNumericRow = [this, MakeResetButton](
		const FText& Label,
		const TFunction<float()>& Getter,
		const TFunction<void(float)>& Setter,
		const FSimpleDelegate& OnReset,
		float MinValue,
		float MaxValue)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
			[
				SNew(SBox)
				.WidthOverride(86)
				[
					SNew(SNumericEntryBox<float>)
					.MinValue(MinValue)
					.MaxValue(MaxValue)
					.MinSliderValue(MinValue)
					.MaxSliderValue(MaxValue)
					.AllowSpin(true)
					.Value_Lambda([Getter]() { return TOptional<float>(Getter()); })
					.OnValueChanged_Lambda([this, Setter](float NewValue)
					{
						Setter(NewValue);
						NotifyPostProcessSettingsChanged();
					})
					.OnValueCommitted_Lambda([this, Setter](float NewValue, ETextCommit::Type)
					{
						Setter(NewValue);
						NotifyPostProcessSettingsChanged();
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeResetButton(OnReset)
			];
	};

	auto MakeSection = [](const FText& Title, const TSharedRef<SWidget>& Body, bool bExpanded = true)
	{
		return SNew(SExpandableArea)
			.InitiallyCollapsed(!bExpanded)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			.Padding(8)
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
			]
			.BodyContent()
			[
				Body
			];
	};

	auto SetUniformVector = [](TFunction<void(FVector4)> Setter, float Value)
	{
		Setter(FVector4(Value, Value, Value, 1.0f));
	};

	return SNew(SBorder)
		.Visibility(this, &SPBRMagicOutlinerWindow::GetCameraPostProcessVisibility)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("Panel")); })
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(PBRText(TEXT("CameraPostProcessHeader"), TEXT("相机 / 后期参数"), TEXT("Camera / Post Process Parameters")))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{
					if (AActor* Actor = GetDetailsActor())
					{
						return FText::FromString(ActorLabel(Actor));
					}
					return PBRText(TEXT("PickCameraOrPostVolume"), TEXT("选择列表中的相机或后期体积盒子。"), TEXT("Select a camera or post process volume from the list."));
				})
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.ContentPadding(FMargin(8, 6))
				.OnClicked_Lambda([this]()
				{
					ApplySuggestedPostProcessSettings();
					return FReply::Handled();
				})
				[
					MakeButtonText(PBRText(TEXT("ApplySuggestedPostProcess"), TEXT("建议调整"), TEXT("Suggested Adjustments")))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeSection(PBRText(TEXT("PostVolumeSection"), TEXT("Post Process Volume"), TEXT("Post Process Volume")), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsEnabled_Lambda([this]() { return GetDetailsPostProcessVolume() != nullptr; })
							.IsChecked_Lambda([this]()
							{
								if (APostProcessVolume* Volume = GetDetailsPostProcessVolume())
								{
									return Volume->bUnbound ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								}
								return ECheckBoxState::Undetermined;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
							{
								if (APostProcessVolume* Volume = GetDetailsPostProcessVolume())
								{
									Volume->Modify();
									Volume->bUnbound = State == ECheckBoxState::Checked;
									NotifyPostProcessSettingsChanged();
								}
							})
							[
								SNew(STextBlock)
								.Text(PBRText(TEXT("PostVolumeUnbound"), TEXT("Infinite Extent / Unbound"), TEXT("Infinite Extent / Unbound")))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							MakeResetButton(FSimpleDelegate::CreateLambda([this]()
							{
								if (APostProcessVolume* Volume = GetDetailsPostProcessVolume())
								{
									Volume->Modify();
									Volume->bUnbound = false;
									NotifyPostProcessSettingsChanged();
								}
							}))
						]
					], true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeSection(PBRText(TEXT("ExposureSection"), TEXT("Exposure 曝光"), TEXT("Exposure")), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(PBRText(TEXT("MeteringMode"), TEXT("Metering Mode"), TEXT("Metering Mode"))).Font(FAppStyle::GetFontStyle("SmallFont"))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
						[
							SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton").ContentPadding(FMargin(6, 3))
							.OnClicked_Lambda([this]()
							{
								if (FPostProcessSettings* Settings = GetDetailsPostProcessSettings())
								{
									Settings->bOverride_AutoExposureMethod = true;
									Settings->AutoExposureMethod = AEM_Manual;
									NotifyPostProcessSettingsChanged();
								}
								return FReply::Handled();
							})
							[SNew(STextBlock).Text(LOCTEXT("MeterManual", "Manual")).Font(FAppStyle::GetFontStyle("SmallFont"))]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
						[
							SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton").ContentPadding(FMargin(6, 3))
							.OnClicked_Lambda([this]()
							{
								if (FPostProcessSettings* Settings = GetDetailsPostProcessSettings())
								{
									Settings->bOverride_AutoExposureMethod = true;
									Settings->AutoExposureMethod = AEM_Basic;
									NotifyPostProcessSettingsChanged();
								}
								return FReply::Handled();
							})
							[SNew(STextBlock).Text(LOCTEXT("MeterBasic", "Basic")).Font(FAppStyle::GetFontStyle("SmallFont"))]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
						[
							SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton").ContentPadding(FMargin(6, 3))
							.OnClicked_Lambda([this]()
							{
								if (FPostProcessSettings* Settings = GetDetailsPostProcessSettings())
								{
									Settings->bOverride_AutoExposureMethod = true;
									Settings->AutoExposureMethod = AEM_Histogram;
									NotifyPostProcessSettingsChanged();
								}
								return FReply::Handled();
							})
							[SNew(STextBlock).Text(LOCTEXT("MeterHistogram", "Histogram")).Font(FAppStyle::GetFontStyle("SmallFont"))]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MakeResetButton(FSimpleDelegate::CreateLambda([this]()
							{
								if (FPostProcessSettings* Settings = GetDetailsPostProcessSettings())
								{
									FPostProcessSettings Defaults;
									Settings->bOverride_AutoExposureMethod = false;
									Settings->AutoExposureMethod = Defaults.AutoExposureMethod;
									NotifyPostProcessSettingsChanged();
								}
							}))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("MinEV100", "Min EV100"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->AutoExposureMinBrightness : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_AutoExposureMinBrightness = true; S->AutoExposureMinBrightness = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_AutoExposureMinBrightness = false; S->AutoExposureMinBrightness = D.AutoExposureMinBrightness; NotifyPostProcessSettingsChanged(); } }), -10.0f, 20.0f)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("MaxEV100", "Max EV100"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->AutoExposureMaxBrightness : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_AutoExposureMaxBrightness = true; S->AutoExposureMaxBrightness = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_AutoExposureMaxBrightness = false; S->AutoExposureMaxBrightness = D.AutoExposureMaxBrightness; NotifyPostProcessSettingsChanged(); } }), -10.0f, 20.0f)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeNumericRow(LOCTEXT("ExposureCompensation", "Exposure Compensation"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->AutoExposureBias : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_AutoExposureBias = true; S->AutoExposureBias = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_AutoExposureBias = false; S->AutoExposureBias = D.AutoExposureBias; NotifyPostProcessSettingsChanged(); } }), -10.0f, 10.0f)
					], true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeSection(LOCTEXT("BloomSection", "Bloom 泛光"), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("BloomIntensity", "Intensity"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->BloomIntensity : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_BloomIntensity = true; S->BloomIntensity = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_BloomIntensity = false; S->BloomIntensity = D.BloomIntensity; NotifyPostProcessSettingsChanged(); } }), 0.0f, 8.0f)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeNumericRow(LOCTEXT("BloomThreshold", "Threshold"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->BloomThreshold : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_BloomThreshold = true; S->BloomThreshold = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_BloomThreshold = false; S->BloomThreshold = D.BloomThreshold; NotifyPostProcessSettingsChanged(); } }), -1.0f, 8.0f)
					], true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeSection(LOCTEXT("ColorGradingSection", "Color Grading 颜色调整"), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("WhiteTemperature", "Temperature"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->WhiteTemp : 6500.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_WhiteTemp = true; S->WhiteTemp = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_WhiteTemp = false; S->WhiteTemp = D.WhiteTemp; NotifyPostProcessSettingsChanged(); } }), 1500.0f, 15000.0f)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("ColorContrast", "Contrast"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->ColorContrast.X : 1.0f; }, [this, SetUniformVector](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_ColorContrast = true; SetUniformVector([S](FVector4 Vec) { S->ColorContrast = Vec; }, V); } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_ColorContrast = false; S->ColorContrast = D.ColorContrast; NotifyPostProcessSettingsChanged(); } }), 0.0f, 4.0f)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("ColorSaturation", "Saturation"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->ColorSaturation.X : 1.0f; }, [this, SetUniformVector](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_ColorSaturation = true; SetUniformVector([S](FVector4 Vec) { S->ColorSaturation = Vec; }, V); } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_ColorSaturation = false; S->ColorSaturation = D.ColorSaturation; NotifyPostProcessSettingsChanged(); } }), 0.0f, 4.0f)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("ColorGamma", "Gamma"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->ColorGamma.X : 1.0f; }, [this, SetUniformVector](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_ColorGamma = true; SetUniformVector([S](FVector4 Vec) { S->ColorGamma = Vec; }, V); } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_ColorGamma = false; S->ColorGamma = D.ColorGamma; NotifyPostProcessSettingsChanged(); } }), 0.1f, 4.0f)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeNumericRow(LOCTEXT("ColorGain", "Gain"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->ColorGain.X : 1.0f; }, [this, SetUniformVector](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_ColorGain = true; SetUniformVector([S](FVector4 Vec) { S->ColorGain = Vec; }, V); } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_ColorGain = false; S->ColorGain = D.ColorGain; NotifyPostProcessSettingsChanged(); } }), 0.0f, 4.0f)
					], false)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeSection(LOCTEXT("AOSection", "Ambient Occlusion 环境遮蔽"), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("AOIntensity", "Intensity"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->AmbientOcclusionIntensity : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_AmbientOcclusionIntensity = true; S->AmbientOcclusionIntensity = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_AmbientOcclusionIntensity = false; S->AmbientOcclusionIntensity = D.AmbientOcclusionIntensity; NotifyPostProcessSettingsChanged(); } }), 0.0f, 4.0f)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeNumericRow(LOCTEXT("AORadius", "Radius"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->AmbientOcclusionRadius : 100.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_AmbientOcclusionRadius = true; S->AmbientOcclusionRadius = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_AmbientOcclusionRadius = false; S->AmbientOcclusionRadius = D.AmbientOcclusionRadius; NotifyPostProcessSettingsChanged(); } }), 0.0f, 1000.0f)
					], false)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeSection(LOCTEXT("LensSection", "Lens 镜头效果"), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("VignetteIntensity", "Vignette 暗角"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->VignetteIntensity : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_VignetteIntensity = true; S->VignetteIntensity = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_VignetteIntensity = false; S->VignetteIntensity = D.VignetteIntensity; NotifyPostProcessSettingsChanged(); } }), 0.0f, 1.0f)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("ChromaticAberrationIntensity", "Chromatic Aberration"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->SceneFringeIntensity : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_SceneFringeIntensity = true; S->SceneFringeIntensity = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_SceneFringeIntensity = false; S->SceneFringeIntensity = D.SceneFringeIntensity; NotifyPostProcessSettingsChanged(); } }), 0.0f, 5.0f)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeNumericRow(LOCTEXT("FilmGrainIntensity", "Film Grain"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->FilmGrainIntensity : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_FilmGrainIntensity = true; S->FilmGrainIntensity = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_FilmGrainIntensity = false; S->FilmGrainIntensity = D.FilmGrainIntensity; NotifyPostProcessSettingsChanged(); } }), 0.0f, 1.0f)
					], false)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				MakeSection(LOCTEXT("DOFSection", "Depth of Field 景深"), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						MakeNumericRow(LOCTEXT("DOFFocalDistance", "Focal Distance"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->DepthOfFieldFocalDistance : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_DepthOfFieldFocalDistance = true; S->DepthOfFieldFocalDistance = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_DepthOfFieldFocalDistance = false; S->DepthOfFieldFocalDistance = D.DepthOfFieldFocalDistance; NotifyPostProcessSettingsChanged(); } }), 0.0f, 100000.0f)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeNumericRow(LOCTEXT("DOFFstop", "F-stop"), [this]() { return GetDetailsPostProcessSettings() ? GetDetailsPostProcessSettings()->DepthOfFieldFstop : 0.0f; }, [this](float V) { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_DepthOfFieldFstop = true; S->DepthOfFieldFstop = V; } }, FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_DepthOfFieldFstop = false; S->DepthOfFieldFstop = D.DepthOfFieldFstop; NotifyPostProcessSettingsChanged(); } }), 0.1f, 32.0f)
					], false)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeSection(LOCTEXT("GIRSection", "Global Illumination / Reflections"), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("GIMethod", "Global Illumination Method")).Font(FAppStyle::GetFontStyle("SmallFont"))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
						[
							SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton").ContentPadding(FMargin(6, 3))
							.Text(LOCTEXT("SetGILumen", "Lumen"))
							.OnClicked_Lambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_DynamicGlobalIlluminationMethod = true; S->DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen; NotifyPostProcessSettingsChanged(); } return FReply::Handled(); })
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MakeResetButton(FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_DynamicGlobalIlluminationMethod = false; S->DynamicGlobalIlluminationMethod = D.DynamicGlobalIlluminationMethod; NotifyPostProcessSettingsChanged(); } }))
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("ReflectionMethod", "Reflection Method")).Font(FAppStyle::GetFontStyle("SmallFont"))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
						[
							SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton").ContentPadding(FMargin(6, 3))
							.Text(LOCTEXT("SetReflectionLumen", "Lumen"))
							.OnClicked_Lambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { S->bOverride_ReflectionMethod = true; S->ReflectionMethod = EReflectionMethod::Lumen; NotifyPostProcessSettingsChanged(); } return FReply::Handled(); })
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MakeResetButton(FSimpleDelegate::CreateLambda([this]() { if (FPostProcessSettings* S = GetDetailsPostProcessSettings()) { FPostProcessSettings D; S->bOverride_ReflectionMethod = false; S->ReflectionMethod = D.ReflectionMethod; NotifyPostProcessSettingsChanged(); } }))
						]
					], false)
			]
		];
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildMaterialThumbnail(TSharedPtr<FPBRMagicOutlinerItem> Item, const FVector2D& Size)
{
	UMaterialInterface* Material = Item.IsValid() ? Item->Material.Get() : nullptr;
	if (TSharedPtr<FAssetThumbnail> Thumbnail = GetOrCreateMaterialThumbnail(Material, Size))
	{
		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.ThumbnailLabel = EThumbnailLabel::NoLabel;
		ThumbnailConfig.bAllowFadeIn = true;
		ThumbnailConfig.ShowAssetBorder = true;
		ThumbnailConfig.BorderPadding = FMargin(1.0f);
		return SNew(SBox)
			.WidthOverride(Size.X)
			.HeightOverride(Size.Y)
			[
				Thumbnail->MakeThumbnailWidget(ThumbnailConfig)
			];
	}

	return SNew(SBox)
		.WidthOverride(Size.X)
		.HeightOverride(Size.Y)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			.Padding(3)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("ClassThumbnail.Material"))
			]
		];
}

TSharedPtr<FAssetThumbnail> SPBRMagicOutlinerWindow::GetOrCreateMaterialThumbnail(UMaterialInterface* Material, const FVector2D& Size)
{
	if (!Material || !MaterialThumbnailPool.IsValid())
	{
		return nullptr;
	}

	const int32 Width = FMath::Max(1, FMath::RoundToInt(Size.X));
	const int32 Height = FMath::Max(1, FMath::RoundToInt(Size.Y));
	const FString CacheKey = Material->GetPathName() + FString::Printf(TEXT("_%dx%d"), Width, Height);
	TSharedPtr<FAssetThumbnail>& Thumbnail = MaterialThumbnailCache.FindOrAdd(CacheKey);
	if (!Thumbnail.IsValid())
	{
		Thumbnail = MakeShared<FAssetThumbnail>(Material, static_cast<uint32>(Width), static_cast<uint32>(Height), MaterialThumbnailPool);
		Thumbnail->GetViewportRenderTargetTexture();
	}
	return Thumbnail;
}

TSharedPtr<FSlateDynamicImageBrush> SPBRMagicOutlinerWindow::GetOrCreateMaterialThumbnailBrush(UMaterialInterface* Material, const FVector2D& Size)
{
	if (!Material)
	{
		return nullptr;
	}

	const int32 Width = FMath::Max(16, FMath::RoundToInt(Size.X));
	const int32 Height = FMath::Max(16, FMath::RoundToInt(Size.Y));
	const FString CacheKey = Material->GetPathName() + FString::Printf(TEXT("_paint_%dx%d"), Width, Height);
	TSharedPtr<FSlateDynamicImageBrush>& Brush = MaterialThumbnailBrushCache.FindOrAdd(CacheKey);
	if (Brush.IsValid())
	{
		return Brush;
	}

	FObjectThumbnail ObjectThumbnail;
	ThumbnailTools::RenderThumbnail(Material, static_cast<uint32>(Width), static_cast<uint32>(Height), ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush, nullptr, &ObjectThumbnail);

	const TArray<uint8>& ImageData = ObjectThumbnail.GetUncompressedImageData();
	const int32 ImageWidth = ObjectThumbnail.GetImageWidth();
	const int32 ImageHeight = ObjectThumbnail.GetImageHeight();
	if (ImageWidth <= 0 || ImageHeight <= 0 || ImageData.Num() < ImageWidth * ImageHeight * 4)
	{
		return nullptr;
	}

	TArray<uint8> BrushImageData = ImageData;
	for (int32 PixelOffset = 3; PixelOffset < BrushImageData.Num(); PixelOffset += 4)
	{
		BrushImageData[PixelOffset] = 255;
	}

	const FString ResourceName = FString::Printf(TEXT("PBRMagicMaterialThumb_%08x_%dx%d"), GetTypeHash(Material->GetPathName()), ImageWidth, ImageHeight);
	Brush = FSlateDynamicImageBrush::CreateWithImageData(FName(*ResourceName), FVector2D(ImageWidth, ImageHeight), BrushImageData);
	return Brush;
}

TSharedPtr<FSlateDynamicImageBrush> SPBRMagicOutlinerWindow::GetOrCreateTextureThumbnailBrush(UTexture* Texture, const FVector2D& Size)
{
	if (!Texture)
	{
		return nullptr;
	}

	const int32 Width = FMath::Max(16, FMath::RoundToInt(Size.X));
	const int32 Height = FMath::Max(16, FMath::RoundToInt(Size.Y));
	const FString CacheKey = Texture->GetPathName() + FString::Printf(TEXT("_texture_%dx%d"), Width, Height);
	TSharedPtr<FSlateDynamicImageBrush>& Brush = TextureThumbnailBrushCache.FindOrAdd(CacheKey);
	if (Brush.IsValid())
	{
		return Brush;
	}

	FObjectThumbnail ObjectThumbnail;
	ThumbnailTools::RenderThumbnail(Texture, static_cast<uint32>(Width), static_cast<uint32>(Height), ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush, nullptr, &ObjectThumbnail);

	const TArray<uint8>& ImageData = ObjectThumbnail.GetUncompressedImageData();
	const int32 ImageWidth = ObjectThumbnail.GetImageWidth();
	const int32 ImageHeight = ObjectThumbnail.GetImageHeight();
	if (ImageWidth <= 0 || ImageHeight <= 0 || ImageData.Num() < ImageWidth * ImageHeight * 4)
	{
		return nullptr;
	}

	TArray<uint8> BrushImageData = ImageData;
	for (int32 PixelOffset = 3; PixelOffset < BrushImageData.Num(); PixelOffset += 4)
	{
		BrushImageData[PixelOffset] = 255;
	}

	const FString ResourceName = FString::Printf(TEXT("PBRMagicTextureThumb_%08x_%dx%d"), GetTypeHash(Texture->GetPathName()), ImageWidth, ImageHeight);
	Brush = FSlateDynamicImageBrush::CreateWithImageData(FName(*ResourceName), FVector2D(ImageWidth, ImageHeight), BrushImageData);
	return Brush;
}

TSharedRef<ITableRow> SPBRMagicOutlinerWindow::GenerateRow(TSharedPtr<FPBRMagicOutlinerItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SPBRMagicOutlinerRow, OwnerTable)
		.Item(Item)
		.OwnerWindow(this)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this, Item]()
			{
				if (!Item.IsValid())
				{
					return FLinearColor::Transparent;
				}
				if (IsItemRepresentedInEditorSelection(Item))
				{
					return GetThemeColor(TEXT("DropZone"));
				}
				if (ActiveTreeItem == Item)
				{
					return GetThemeColor(TEXT("PanelRaised"));
				}
				if (GetItemCheckState(Item) == ECheckBoxState::Checked)
				{
					return GetThemeColor(TEXT("DropZone"));
				}
				return Item->Material.IsValid() ? GetThemeColor(TEXT("TableRowHover")) : GetThemeColor(TEXT("TableRow"));
			})
			.Padding(0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
				[
					SNew(SBox)
					.WidthOverride(3.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor_Lambda([this, Item]()
						{
							if (IsItemRepresentedInEditorSelection(Item))
							{
								return GetThemeColor(TEXT("Selection"));
							}
							if (Item.IsValid() && GetItemCheckState(Item) == ECheckBoxState::Checked)
							{
								return GetThemeColor(TEXT("Selection"));
							}
							if (Item.IsValid() && ActiveTreeItem == Item)
							{
								return GetThemeColor(TEXT("Primary"));
							}
							if (Item.IsValid() && Item->Material.IsValid())
							{
								return GetThemeColor(TEXT("Primary"));
							}
							return GetThemeColor(TEXT("Border"));
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.ContentPadding(0)
					.ButtonColorAndOpacity(FLinearColor::Transparent)
					.OnClicked_Lambda([this, Item]()
					{
						SetItemChecked(Item, GetItemCheckState(Item) != ECheckBoxState::Checked);
						return FReply::Handled();
					})
					[
						SNew(SBox)
						.WidthOverride(16)
						.HeightOverride(16)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor_Lambda([this, Item]()
							{
								return GetItemCheckState(Item) == ECheckBoxState::Checked
									? GetThemeColor(TEXT("Primary"))
									: GetThemeColor(TEXT("Background"));
							})
							.Padding(2)
							[
								SNew(SImage)
								.Visibility_Lambda([this, Item]()
								{
									return GetItemCheckState(Item) == ECheckBoxState::Checked
										? EVisibility::Visible
										: EVisibility::Hidden;
								})
								.Image(FAppStyle::Get().GetBrush("Icons.Check"))
								.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("SelectionText"))); })
							]
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 2, 10, 2)
				[
					SNew(SBox)
					.WidthOverride(42.0f)
					.HeightOverride(38.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor_Lambda([this, Item]()
						{
							return Item.IsValid() && Item->Material.IsValid() ? GetThemeColor(TEXT("DropZone")) : GetThemeColor(TEXT("TableRow"));
						})
						.Padding(2)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								SNew(SBox)
								.Visibility_Lambda([Item]()
								{
									return Item.IsValid() && Item->Material.IsValid() && Item->MaterialSlots.Num() > 0
										? EVisibility::Visible
										: EVisibility::Collapsed;
								})
								[
									BuildMaterialThumbnail(Item, FVector2D(34.0f, 34.0f))
								]
							]
							+ SOverlay::Slot()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Visibility_Lambda([Item]()
								{
									return Item.IsValid() && Item->Material.IsValid() && Item->MaterialSlots.Num() > 0
										? EVisibility::Collapsed
										: EVisibility::Visible;
								})
								.Image_Lambda([this, Item]()
								{
									if (!Item.IsValid())
									{
										return FAppStyle::Get().GetBrush("Icons.World");
									}
									if (ActiveCategory == EPBRMagicOutlinerCategory::Lights)
									{
										return FAppStyle::Get().GetBrush("ClassIcon.LightComponent");
									}
									if (ActiveCategory == EPBRMagicOutlinerCategory::Cameras)
									{
										return FAppStyle::Get().GetBrush("ClassIcon.CameraComponent");
									}
									if (ActiveCategory == EPBRMagicOutlinerCategory::Blueprints)
									{
										return FAppStyle::Get().GetBrush("ClassIcon.Blueprint");
									}
									return FAppStyle::Get().GetBrush("ClassIcon.StaticMeshActor");
								})
								.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
							]
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(0.46f).VAlign(VAlign_Center).Padding(2, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.IsValid() ? Item->DisplayName : FString()))
					.ToolTipText_Lambda([Item]()
					{
						if (Item.IsValid() && Item->Material.IsValid())
						{
							return FText::Format(LOCTEXT("MaterialRowTip", "双击打开材质参数；拖入其它材质会替换场景中 {0} 个使用位置。"), FText::AsNumber(Item->MaterialSlots.Num()));
						}
						return FText::GetEmpty();
					})
					.Font(Item.IsValid() && Item->bGroup ? FAppStyle::GetFontStyle("NormalFontBold") : FAppStyle::GetFontStyle("NormalFont"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
				]
				+ SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center).Padding(6, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.IsValid() ? Item->TypeText : FString()))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
				[
					SNew(SBox)
					.WidthOverride(56)
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(FText::AsNumber(Item.IsValid() ? Item->ActorCount : 0))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
					]
				]
				+ SHorizontalBox::Slot().FillWidth(0.36f).VAlign(VAlign_Center).Padding(6, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.IsValid() ? Item->DetailText : FString()))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 8, 0)
				[
					SNew(STextBlock)
					.Visibility_Lambda([this, Item]()
					{
						return IsItemRepresentedInEditorSelection(Item) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text(LOCTEXT("SceneSelectedMarker", "场景选中"))
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
				]
			]
		];
}

TSharedRef<ITableRow> SPBRMagicOutlinerWindow::GenerateSelectedMaterialRow(TSharedPtr<FPBRMagicOutlinerItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SPBRMagicOutlinerRow, OwnerTable)
		.Item(Item)
		.OwnerWindow(this)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			.BorderBackgroundColor_Lambda([this]() { return GetThemeColor(TEXT("PanelRaised")); })
			.Padding(6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					BuildMaterialThumbnail(Item, FVector2D(52.0f, 52.0f))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.IsValid() ? Item->DisplayName : FString()))
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.IsValid() ? FString::Printf(TEXT("%s | %d 个对象 / %d 个槽"), *Item->TypeText, Item->ActorCount, Item->MaterialSlots.Num()) : FString()))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.IsValid() ? Item->DetailText : FString()))
						.Font(FAppStyle::GetFontStyle("TinyText"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.ContentPadding(FMargin(7, 4))
						.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("DropZone"))); })
						.OnClicked_Lambda([this, Item]()
						{
							OpenMaterialEditor(Item);
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("OpenSelectedMaterialSmall"), TEXT("打开"), TEXT("Open")))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.ContentPadding(FMargin(7, 4))
						.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("DropZone"))); })
						.OnClicked_Lambda([this, Item]()
						{
							return OnEditSelectedMaterialSlot(Item);
						})
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("AdjustSelectedMaterialSmall"), TEXT("调参"), TEXT("Adjust")))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.ContentPadding(FMargin(7, 4))
						.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("DropZone"))); })
						.OnClicked_Lambda([this, Item]()
						{
							SelectItemActors(Item, false);
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(PBRText(TEXT("SelectMaterialUsersSmall"), TEXT("选择"), TEXT("Select")))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
						]
					]
				]
			]
		];
}

FReply SPBRMagicOutlinerWindow::OnEditSelectedMaterialSlot(TSharedPtr<FPBRMagicOutlinerItem> Item, int32 MaterialSlotIndex)
{
	if (!Item.IsValid() || Item->MaterialSlots.IsEmpty())
	{
		StatusMessage = TEXT("没有可调节的材质槽");
		return FReply::Handled();
	}

	const FPBRMaterialSlotReference* PreferredSlot = nullptr;
	if (MaterialSlotIndex != INDEX_NONE)
	{
		for (const FPBRMaterialSlotReference& SlotRef : Item->MaterialSlots)
		{
			if (SlotRef.SlotIndex == MaterialSlotIndex && SlotRef.MeshComponent.IsValid())
			{
				PreferredSlot = &SlotRef;
				break;
			}
		}
	}
	if (!PreferredSlot)
	{
		for (const FPBRMaterialSlotReference& SlotRef : Item->MaterialSlots)
		{
			if (SlotRef.MeshComponent.IsValid() && IsActorSelectedInEditor(SlotRef.Actor.Get()))
			{
				PreferredSlot = &SlotRef;
				break;
			}
		}
	}
	if (!PreferredSlot)
	{
		for (const FPBRMaterialSlotReference& SlotRef : Item->MaterialSlots)
		{
			if (SlotRef.MeshComponent.IsValid())
			{
				PreferredSlot = &SlotRef;
				break;
			}
		}
	}
	if (!PreferredSlot || !PreferredSlot->MeshComponent.IsValid() || PreferredSlot->SlotIndex == INDEX_NONE)
	{
		StatusMessage = TEXT("当前材质行没有有效的网格体槽位");
		return FReply::Handled();
	}

	UPrimitiveComponent* Component = PreferredSlot->MeshComponent.Get();
	const int32 SlotIndex = PreferredSlot->SlotIndex;
	FPBRSceneEditableMaterialResult Result = FPBRSceneMaterialReplacer::EnsureEditableMaterialForSlot(Component, SlotIndex);
	if (!Result.Instance)
	{
		EditableMaterialComponent.Reset();
		EditableMaterialSlotIndex = INDEX_NONE;
		StatusMessage = Result.Message.IsEmpty() ? TEXT("接管材质失败") : Result.Message;
		return FReply::Handled();
	}

	EditableMaterialComponent = Component;
	EditableMaterialSlotIndex = SlotIndex;
	StatusMessage = Result.Message;
	RebuildItems();
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	OpenEditableMaterialParameterWindow();
	return FReply::Handled();
}

void SPBRMagicOutlinerWindow::OpenEditableMaterialParameterWindow()
{
	if (!GetEditableMaterialInstance())
	{
		StatusMessage = TEXT("没有可弹出调参的材质实例");
		return;
	}

	if (TSharedPtr<SWindow> ExistingWindow = MaterialParameterWindow.Pin())
	{
		ExistingWindow->BringToFront();
		ExistingWindow->SetContent(BuildMaterialParameterPopupContent());
		return;
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(PBRText(TEXT("MagicMaterialParameterPopupTitle"), TEXT("PBRStudio 材质参数调节"), TEXT("PBRStudio Material Parameters")))
		.ClientSize(FVector2D(840.0f, 780.0f))
		.SizingRule(ESizingRule::UserSized)
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		[
			BuildMaterialParameterPopupContent()
		];

	MaterialParameterWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
}

bool SPBRMagicOutlinerWindow::ResolveEditableMaterialSlot(UPrimitiveComponent*& OutComponent, int32& OutSlotIndex) const
{
	OutComponent = EditableMaterialComponent.Get();
	OutSlotIndex = EditableMaterialSlotIndex;
	return OutComponent && OutSlotIndex >= 0 && OutSlotIndex < OutComponent->GetNumMaterials();
}

UMaterialInstanceConstant* SPBRMagicOutlinerWindow::GetEditableMaterialInstance() const
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		return nullptr;
	}

	UMaterialInterface* Material = Component->GetMaterial(SlotIndex);
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Material);
	return Instance;
}

EPBRMaterialType SPBRMagicOutlinerWindow::GetEditableMaterialType() const
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		return EPBRMaterialType::Standard;
	}
	return GuessMagicMaterialTypeFromMaterial(Component->GetMaterial(SlotIndex));
}

FText SPBRMagicOutlinerWindow::GetEditableMaterialNameText() const
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		return PBRText(TEXT("MagicNoEditableMaterial"), TEXT("未选择可编辑材质槽"), TEXT("No editable material slot selected"));
	}

	UMaterialInterface* Material = Component->GetMaterial(SlotIndex);
	return FText::FromString(Material ? Material->GetName() : TEXT("None"));
}

FText SPBRMagicOutlinerWindow::GetEditableMaterialSlotText() const
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		return FText::GetEmpty();
	}

	const AActor* Owner = Component->GetOwner();
	return FText::FromString(FString::Printf(
		TEXT("%s / %s / Slot %d"),
		Owner ? *ActorLabel(const_cast<AActor*>(Owner)) : TEXT("Actor"),
		*Component->GetName(),
		SlotIndex));
}

FText SPBRMagicOutlinerWindow::GetEditableMaterialTypeText() const
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		return PBRText(TEXT("MagicMaterialTypeUnset"), TEXT("未设置"), TEXT("Unset"));
	}

	return GetMagicMaterialTypeLabel(GuessMagicMaterialTypeFromMaterial(Component->GetMaterial(SlotIndex)));
}

TOptional<float> SPBRMagicOutlinerWindow::GetEditableMaterialScalar(const FName& ParameterName, float DefaultValue) const
{
	if (UMaterialInstanceConstant* Instance = GetEditableMaterialInstance())
	{
		float Value = DefaultValue;
		if (Instance->GetScalarParameterValue(FMaterialParameterInfo(ParameterName), Value))
		{
			return Value;
		}
	}
	return DefaultValue;
}

TOptional<float> SPBRMagicOutlinerWindow::GetEditableMaterialVectorChannel(const FName& ParameterName, int32 ChannelIndex, const FLinearColor& DefaultValue) const
{
	FLinearColor Value = DefaultValue;
	if (UMaterialInstanceConstant* Instance = GetEditableMaterialInstance())
	{
		Instance->GetVectorParameterValue(FMaterialParameterInfo(ParameterName), Value);
	}

	switch (ChannelIndex)
	{
	case 0:
		return Value.R;
	case 1:
		return Value.G;
	case 2:
		return Value.B;
	case 3:
		return Value.A;
	default:
		return 0.0f;
	}
}

bool SPBRMagicOutlinerWindow::GetEditableMaterialSwitch(const FName& ParameterName, bool bDefaultValue) const
{
	if (UMaterialInstanceConstant* Instance = GetEditableMaterialInstance())
	{
		bool bValue = bDefaultValue;
		FGuid ExpressionGuid;
		if (Instance->GetStaticSwitchParameterValue(FMaterialParameterInfo(ParameterName), bValue, ExpressionGuid))
		{
			return bValue;
		}
	}
	return bDefaultValue;
}

UTexture* SPBRMagicOutlinerWindow::GetEditableMaterialTexture(const FName& ParameterName) const
{
	if (UMaterialInstanceConstant* Instance = GetEditableMaterialInstance())
	{
		UTexture* Texture = nullptr;
		if (Instance->GetTextureParameterValue(FMaterialParameterInfo(ParameterName), Texture))
		{
			return Texture;
		}
	}
	return nullptr;
}

void SPBRMagicOutlinerWindow::CommitEditableMaterialScalar(const FName& ParameterName, float Value, float MinValue, float MaxValue)
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		StatusMessage = TEXT("没有可编辑的材质槽");
		return;
	}

	FString Message;
	const float ClampedValue = FMath::Clamp(Value, MinValue, MaxValue);
	if (FPBRSceneMaterialReplacer::SetScalarParameterForSlot(Component, SlotIndex, ParameterName, ClampedValue, Message))
	{
		StatusMessage = Message;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	else
	{
		StatusMessage = Message.IsEmpty() ? TEXT("参数更新失败") : Message;
	}
}

void SPBRMagicOutlinerWindow::CommitEditableMaterialVector(const FName& ParameterName, const FLinearColor& Value)
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		StatusMessage = TEXT("没有可编辑的材质槽");
		return;
	}

	const FLinearColor ClampedValue(
		FMath::Clamp(Value.R, 0.0f, 1.0f),
		FMath::Clamp(Value.G, 0.0f, 1.0f),
		FMath::Clamp(Value.B, 0.0f, 1.0f),
		FMath::Clamp(Value.A, 0.0f, 1.0f));

	FString Message;
	if (FPBRSceneMaterialReplacer::SetVectorParameterForSlot(Component, SlotIndex, ParameterName, ClampedValue, Message))
	{
		StatusMessage = Message;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	else
	{
		StatusMessage = Message.IsEmpty() ? TEXT("颜色更新失败") : Message;
	}
}

void SPBRMagicOutlinerWindow::CommitEditableMaterialVectorChannel(const FName& ParameterName, int32 ChannelIndex, float Value, const FLinearColor& DefaultValue)
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		StatusMessage = TEXT("没有可编辑的材质槽");
		return;
	}

	FLinearColor NewColor = DefaultValue;
	if (UMaterialInstanceConstant* Instance = GetEditableMaterialInstance())
	{
		Instance->GetVectorParameterValue(FMaterialParameterInfo(ParameterName), NewColor);
	}

	const float ClampedValue = FMath::Clamp(Value, 0.0f, 1.0f);
	switch (ChannelIndex)
	{
	case 0:
		NewColor.R = ClampedValue;
		break;
	case 1:
		NewColor.G = ClampedValue;
		break;
	case 2:
		NewColor.B = ClampedValue;
		break;
	case 3:
		NewColor.A = ClampedValue;
		break;
	default:
		break;
	}

	FString Message;
	if (FPBRSceneMaterialReplacer::SetVectorParameterForSlot(Component, SlotIndex, ParameterName, NewColor, Message))
	{
		StatusMessage = Message;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	else
	{
		StatusMessage = Message.IsEmpty() ? TEXT("颜色更新失败") : Message;
	}
}

void SPBRMagicOutlinerWindow::OpenEditableMaterialColorPicker(const FName& ParameterName, const FLinearColor& DefaultValue)
{
	FLinearColor InitialColor = DefaultValue;
	if (UMaterialInstanceConstant* Instance = GetEditableMaterialInstance())
	{
		Instance->GetVectorParameterValue(FMaterialParameterInfo(ParameterName), InitialColor);
	}

	FColorPickerArgs PickerArgs;
	PickerArgs.bUseAlpha = true;
	PickerArgs.bOnlyRefreshOnOk = false;
	PickerArgs.InitialColor = InitialColor;
	PickerArgs.ParentWidget = AsShared();
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([this, ParameterName](FLinearColor NewColor)
	{
		CommitEditableMaterialVector(ParameterName, NewColor);
	});
	OpenColorPicker(PickerArgs);
}

void SPBRMagicOutlinerWindow::CommitEditableMaterialSwitch(const FName& ParameterName, bool bValue)
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		StatusMessage = TEXT("没有可编辑的材质槽");
		return;
	}

	FString Message;
	if (FPBRSceneMaterialReplacer::SetStaticSwitchParameterForSlot(Component, SlotIndex, ParameterName, bValue, Message))
	{
		StatusMessage = Message;
		Invalidate(EInvalidateWidgetReason::Paint);
		if (TSharedPtr<SWindow> ExistingWindow = MaterialParameterWindow.Pin())
		{
			ExistingWindow->SetContent(BuildMaterialParameterPopupContent());
		}
	}
	else
	{
		StatusMessage = Message.IsEmpty() ? TEXT("开关更新失败") : Message;
	}
}

void SPBRMagicOutlinerWindow::CommitEditableMaterialTexture(const FName& ParameterName, UTexture* Texture)
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		StatusMessage = TEXT("没有可编辑的材质槽");
		return;
	}

	FString Message;
	if (FPBRSceneMaterialReplacer::SetTextureParameterForSlot(Component, SlotIndex, ParameterName, Texture, Message))
	{
		StatusMessage = Message;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	else
	{
		StatusMessage = Message.IsEmpty() ? TEXT("贴图更新失败") : Message;
	}
}

void SPBRMagicOutlinerWindow::StepEditableMaterialScalar(const FName& ParameterName, float DeltaValue, float MinValue, float MaxValue, float DefaultValue)
{
	const TOptional<float> CurrentValue = GetEditableMaterialScalar(ParameterName, DefaultValue);
	CommitEditableMaterialScalar(ParameterName, (CurrentValue.IsSet() ? CurrentValue.GetValue() : DefaultValue) + DeltaValue, MinValue, MaxValue);
}

void SPBRMagicOutlinerWindow::SelectEditableMaterialType(EPBRMaterialType MaterialType)
{
	UPrimitiveComponent* Component = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!ResolveEditableMaterialSlot(Component, SlotIndex))
	{
		StatusMessage = TEXT("没有可切换类型的材质槽");
		return;
	}

	FString Message;
	if (FPBRSceneMaterialReplacer::SetMaterialTypeForSlot(Component, SlotIndex, MaterialType, Message))
	{
		StatusMessage = Message;
		RebuildRootContent();
	}
	else
	{
		StatusMessage = Message.IsEmpty() ? TEXT("材质类型切换失败") : Message;
	}
}

FReply SPBRMagicOutlinerWindow::OnApplyAIMaterialSuggestionClicked()
{
	UMaterialInstanceConstant* Instance = GetEditableMaterialInstance();
	if (!Instance)
	{
		StatusMessage = TEXT("没有可分析的材质实例");
		return FReply::Handled();
	}

	FScopedSlowTask SlowTask(5.0f, PBRText(TEXT("MaterialAIProgressTitle"), TEXT("AI 正在识别材质贴图..."), TEXT("AI is analyzing material textures...")));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, PBRText(TEXT("MaterialAIProgressLocal"), TEXT("整理材质名称、父材质和贴图参数"), TEXT("Collecting material names and texture parameters")));
	FPBRMagicAISuggestion Suggestion = PBRMagicBuildLocalAISuggestion(Instance);

	FString AIStatus;
	FPBRMagicAISuggestion RemoteSuggestion;
	SlowTask.EnterProgressFrame(1.0f, PBRText(TEXT("MaterialAIProgressImages"), TEXT("压缩贴图缩略图，准备发送给识图模型"), TEXT("Compressing texture thumbnails for vision model")));
	SlowTask.EnterProgressFrame(1.0f, PBRText(TEXT("MaterialAIProgressRemote"), TEXT("请求 AI 接口并等待识别结果"), TEXT("Requesting AI provider and waiting for recognition result")));
	const bool bUsedRemoteAI = PBRMagicTryRemoteAISuggestion(Instance, Suggestion, RemoteSuggestion, AIStatus);
	if (bUsedRemoteAI)
	{
		Suggestion = MoveTemp(RemoteSuggestion);
	}

	SlowTask.EnterProgressFrame(1.0f, PBRText(TEXT("MaterialAIProgressParse"), TEXT("整理 AI 返回的材质类型和参数"), TEXT("Parsing AI material type and parameter suggestions")));
	StatusMessage = Suggestion.Summary;
	if (!AIStatus.IsEmpty())
	{
		StatusMessage += TEXT(" ");
		StatusMessage += AIStatus;
	}

	SlowTask.EnterProgressFrame(1.0f, PBRText(TEXT("MaterialAIProgressPreview"), TEXT("打开建议预览，等待用户确认"), TEXT("Opening suggestion preview for confirmation")));
	const FPBRMagicAISuggestion PreviewSuggestion = Suggestion;
	const FString PreviewText = PBRMagicBuildAISuggestionPreviewText(PreviewSuggestion);
	TSharedRef<TWeakPtr<SWindow>> PreviewWindowWeak = MakeShared<TWeakPtr<SWindow>>();
	TSharedRef<SWindow> PreviewWindow = SNew(SWindow)
		.Title(PBRText(TEXT("MaterialAIPreviewTitle"), TEXT("AI 材质建议预览"), TEXT("AI Material Suggestion Preview")))
		.ClientSize(FVector2D(620.0f, 460.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SBorder)
			.Padding(14)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(STextBlock)
					.Text(PBRText(TEXT("MaterialAIPreviewHeader"), TEXT("AI 已给出建议，确认后才会写入当前材质实例"), TEXT("AI produced suggestions. They are written only after confirmation.")))
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Text(FText::FromString(PreviewText))
						.AutoWrapText(true)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
					[
						SNew(SButton)
						.Text(PBRText(TEXT("MaterialAIPreviewCancel"), TEXT("不应用"), TEXT("Cancel")))
						.OnClicked_Lambda([PreviewWindowWeak]()
						{
							if (TSharedPtr<SWindow> Window = PreviewWindowWeak->Pin())
							{
								Window->RequestDestroyWindow();
							}
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(PBRText(TEXT("MaterialAIPreviewApply"), TEXT("应用建议"), TEXT("Apply Suggestions")))
						.OnClicked_Lambda([this, PreviewWindowWeak, PreviewSuggestion]()
						{
							SelectEditableMaterialType(PreviewSuggestion.MaterialType);
							for (const TPair<FName, float>& Pair : PreviewSuggestion.Scalars)
							{
								float MinValue = 0.0f;
								float MaxValue = 1.0f;
								PBRMagicGetAIScalarRange(Pair.Key, MinValue, MaxValue);
								CommitEditableMaterialScalar(Pair.Key, Pair.Value, MinValue, MaxValue);
							}
							for (const TPair<FName, FLinearColor>& Pair : PreviewSuggestion.Colors)
							{
								CommitEditableMaterialVector(Pair.Key, Pair.Value);
							}
							for (const TPair<FName, bool>& Pair : PreviewSuggestion.Switches)
							{
								CommitEditableMaterialSwitch(Pair.Key, Pair.Value);
							}
							StatusMessage = PreviewSuggestion.Summary;
							Invalidate(EInvalidateWidgetReason::Paint);
							if (TSharedPtr<SWindow> ExistingWindow = MaterialParameterWindow.Pin())
							{
								ExistingWindow->SetContent(BuildMaterialParameterPopupContent());
							}
							if (TSharedPtr<SWindow> Window = PreviewWindowWeak->Pin())
							{
								Window->RequestDestroyWindow();
							}
							return FReply::Handled();
						})
					]
				]
			]
		];
	*PreviewWindowWeak = PreviewWindow;
	FSlateApplication::Get().AddWindow(PreviewWindow);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnMaterialAISettingsClicked()
{
	if (TSharedPtr<SWindow> ExistingWindow = MaterialAISettingsWindow.Pin())
	{
		ExistingWindow->BringToFront();
		return FReply::Handled();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(PBRText(TEXT("MaterialAISettingsWindowTitle"), TEXT("PBRStudio AI 材质建议设置"), TEXT("PBRStudio AI Material Settings")))
		.ClientSize(FVector2D(560.0f, 300.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			BuildMaterialAISettingsContent()
		];

	MaterialAISettingsWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
	return FReply::Handled();
}

void SPBRMagicOutlinerWindow::CycleEditableMaterialType(int32 Direction)
{
	const TArray<FPBRMagicMaterialTypeOption>& Options = GetMagicMaterialTypeOptions();
	if (Options.IsEmpty())
	{
		return;
	}

	const EPBRMaterialType CurrentType = GetEditableMaterialType();
	int32 CurrentIndex = 0;
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		if (Options[Index].Type == CurrentType)
		{
			CurrentIndex = Index;
			break;
		}
	}

	const int32 NextIndex = (CurrentIndex + Direction + Options.Num()) % Options.Num();
	SelectEditableMaterialType(Options[NextIndex].Type);
}

TSharedRef<ITableRow> SPBRMagicOutlinerWindow::GenerateNameCheckRow(TSharedPtr<FPBRNameCheckListItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FPBRNameCheckListItem>>, OwnerTable)
		.Padding(FMargin(2, 3))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			.BorderBackgroundColor_Lambda([this, Item]()
			{
				return Item.IsValid() && GetNameCheckState(Item->Name) == ECheckBoxState::Checked
					? GetThemeColor(TEXT("DropZone"))
					: GetThemeColor(TEXT("TableRow"));
			})
			.Padding(FMargin(7, 6))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, Item]()
					{
						return Item.IsValid() ? GetNameCheckState(Item->Name) : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState State)
					{
						if (Item.IsValid())
						{
							SetNameChecked(Item->Name, State == ECheckBoxState::Checked);
						}
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Layers"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.IsValid() ? Item->Name : FString()))
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 2, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([Item]() { return FText::AsNumber(Item.IsValid() ? Item->MatchCount : 0); })
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Primary"))); })
				]
			]
		];
}

TSharedRef<ITableRow> SPBRMagicOutlinerWindow::GenerateCheckedActorRow(TSharedPtr<FPBRCheckedActorListItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FPBRCheckedActorListItem>>, OwnerTable)
		.Padding(FMargin(2, 3))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this, Item]()
			{
				return Item.IsValid() && IsActorSelectedInEditor(Item->Actor.Get())
					? GetThemeColor(TEXT("DropZone"))
					: FLinearColor::Transparent;
			})
			.Padding(FMargin(6, 5))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(0, 0, 6, 0)
				[
					SNew(SBox)
					.WidthOverride(3.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor_Lambda([this, Item]()
						{
							return Item.IsValid() && IsActorSelectedInEditor(Item->Actor.Get())
								? GetThemeColor(TEXT("Selection"))
								: GetThemeColor(TEXT("Border"));
						})
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.IsValid() ? Item->DisplayName : FString()))
						.Font(FAppStyle::GetFontStyle("SmallFontBold"))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Text"))); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.IsValid() ? Item->TypeText + TEXT(" | ") + Item->DetailText : FString()))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("TextMuted"))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(STextBlock)
					.Visibility_Lambda([this, Item]()
					{
						return Item.IsValid() && IsActorSelectedInEditor(Item->Actor.Get()) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text(LOCTEXT("CheckedActorSceneSelectedMarker", "场景选中"))
					.Font(FAppStyle::GetFontStyle("SmallFontBold"))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(GetThemeColor(TEXT("Selection"))); })
				]
			]
		];
}

void SPBRMagicOutlinerWindow::GetItemChildren(TSharedPtr<FPBRMagicOutlinerItem> Item, TArray<TSharedPtr<FPBRMagicOutlinerItem>>& OutChildren) const
{
	if (Item.IsValid())
	{
		OutChildren.Append(Item->Children);
	}
}

void SPBRMagicOutlinerWindow::OnTreeSelectionChanged(TSharedPtr<FPBRMagicOutlinerItem> Item, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		HandleTreeItemClicked(Item, false);
	}
}

void SPBRMagicOutlinerWindow::HandleTreeItemClicked(TSharedPtr<FPBRMagicOutlinerItem> Item, bool bAddToSelection)
{
	ActiveTreeItem = Item;
	DetailsActor = (Item.IsValid() && Item->Actor.IsValid()) ? Item->Actor : nullptr;
	SelectItemActors(Item, bAddToSelection);
	if (Item.IsValid() && Item->Material.IsValid())
	{
		SyncMaterialListSelectionFromEditor();
	}
	if (TreeView.IsValid())
	{
		if (!bAddToSelection)
		{
			TreeView->ClearSelection();
		}
		if (Item.IsValid())
		{
			TreeView->SetItemSelection(Item, true, ESelectInfo::Direct);
		}
		TreeView->RequestTreeRefresh();
	}
}

void SPBRMagicOutlinerWindow::OnTreeItemDoubleClicked(TSharedPtr<FPBRMagicOutlinerItem> Item)
{
	OpenMaterialEditor(Item);
}

void SPBRMagicOutlinerWindow::OnCheckedActorSelectionChanged(TSharedPtr<FPBRCheckedActorListItem> Item, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct || !Item.IsValid() || !Item->Actor.IsValid())
	{
		return;
	}
	TArray<AActor*> Actors;
	Actors.Add(Item->Actor.Get());
	DetailsActor = Item->Actor;
	SelectActors(Actors, false);
	StatusMessage = FString::Printf(TEXT("已在场景中选中: %s"), *Item->DisplayName);
}

EActiveTimerReturnType SPBRMagicOutlinerWindow::SyncMaterialSelectionTimer(double InCurrentTime, float InDeltaTime)
{
	TArray<AActor*> SelectedActors;
	GetEditorSelectedActors(SelectedActors);
	TArray<FString> SelectionKeys;
	SelectionKeys.Reserve(SelectedActors.Num());
	for (AActor* Actor : SelectedActors)
	{
		if (Actor)
		{
			SelectionKeys.Add(FString::Printf(TEXT("%p"), Actor));
		}
	}
	SelectionKeys.Sort();
	const FString CurrentSelectionSignature = FString::Join(SelectionKeys, TEXT("|"));
	if (CurrentSelectionSignature != LastEditorSelectionSignature)
	{
		LastEditorSelectionSignature = CurrentSelectionSignature;
		FocusFirstEditorSelectedActor(SelectedActors);
		if (TreeView.IsValid())
		{
			TreeView->RequestTreeRefresh();
		}
		if (CheckedListView.IsValid())
		{
			CheckedListView->RequestListRefresh();
		}
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	SyncMaterialListSelectionFromEditor();
	return EActiveTimerReturnType::Continue;
}

void SPBRMagicOutlinerWindow::HandleMaterialThumbnailUpdated(const FAssetData& AssetData)
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SPBRMagicOutlinerWindow::RefreshSelectedMaterialItems(const TSet<TWeakObjectPtr<UMaterialInterface>>& SelectedMaterials)
{
	SelectedMaterialItems.Reset();
	for (const TWeakObjectPtr<UMaterialInterface>& MaterialPtr : SelectedMaterials)
	{
		if (!MaterialPtr.IsValid())
		{
			continue;
		}
		if (const TSharedPtr<FPBRMagicOutlinerItem>* Item = MaterialItemsByMaterial.Find(MaterialPtr))
		{
			SelectedMaterialItems.Add(*Item);
		}
	}
	SelectedMaterialItems.Sort([](const TSharedPtr<FPBRMagicOutlinerItem>& A, const TSharedPtr<FPBRMagicOutlinerItem>& B)
	{
		if (A->ActorCount != B->ActorCount)
		{
			return A->ActorCount > B->ActorCount;
		}
		return A->DisplayName < B->DisplayName;
	});

	if (EditableMaterialComponent.IsValid())
	{
		bool bStillSelected = false;
		for (const TSharedPtr<FPBRMagicOutlinerItem>& Item : SelectedMaterialItems)
		{
			if (!Item.IsValid())
			{
				continue;
			}
			for (const FPBRMaterialSlotReference& SlotRef : Item->MaterialSlots)
			{
				if (SlotRef.MeshComponent.Get() == EditableMaterialComponent.Get() && SlotRef.SlotIndex == EditableMaterialSlotIndex)
				{
					bStillSelected = true;
					break;
				}
			}
			if (bStillSelected)
			{
				break;
			}
		}
		if (!bStillSelected)
		{
			EditableMaterialComponent.Reset();
			EditableMaterialSlotIndex = INDEX_NONE;
		}
	}

	if (SelectedMaterialListView.IsValid())
	{
		SelectedMaterialListView->RequestListRefresh();
	}
}

FString SPBRMagicOutlinerWindow::BuildMaterialFocusSignature(const TSet<TWeakObjectPtr<UMaterialInterface>>& Materials) const
{
	TArray<FString> Keys;
	for (const TWeakObjectPtr<UMaterialInterface>& MaterialPtr : Materials)
	{
		if (UMaterialInterface* Material = MaterialPtr.Get())
		{
			Keys.Add(FString::Printf(TEXT("%p"), Material));
		}
	}
	Keys.Sort();
	return FString::Join(Keys, TEXT("|"));
}

void SPBRMagicOutlinerWindow::FocusMaterialItemsFromSet(const TSet<TWeakObjectPtr<UMaterialInterface>>& Materials, bool bForceScroll)
{
	if (!TreeView.IsValid())
	{
		return;
	}

	const FString FocusSignature = BuildMaterialFocusSignature(Materials);
	if (!bForceScroll && FocusSignature == LastMaterialFocusSignature)
	{
		return;
	}
	LastMaterialFocusSignature = FocusSignature;

	TreeView->ClearSelection();
	TSharedPtr<FPBRMagicOutlinerItem> FirstItemToFocus;
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
	{
		if (Root.IsValid() && Root->Material.IsValid() && Materials.Contains(Root->Material))
		{
			TreeView->SetItemSelection(Root, true, ESelectInfo::Direct);
			if (!FirstItemToFocus.IsValid())
			{
				FirstItemToFocus = Root;
			}
		}
	}

	if (FirstItemToFocus.IsValid())
	{
		TreeView->RequestScrollIntoView(FirstItemToFocus);
	}
}

void SPBRMagicOutlinerWindow::RebuildItems()
{
	RootItems.Reset();
	MaterialItemsByMaterial.Reset();
	SelectedMaterialItems.Reset();
	if (SelectedMaterialListView.IsValid())
	{
		SelectedMaterialListView->RequestListRefresh();
	}
	CachedActorCount = 0;
	CachedCheckedCount = 0;

	TArray<AActor*> Actors;
	GatherActors(Actors);
	CachedActorCount = Actors.Num();

	if (ActiveCategory == EPBRMagicOutlinerCategory::Materials)
	{
		RebuildMaterialItems(Actors);
		return;
	}

	TMap<FString, TSharedPtr<FPBRMagicOutlinerItem>> GroupMap;
	for (AActor* Actor : Actors)
	{
		if (!PassesCategory(Actor) || !PassesClassicSearch(Actor))
		{
			continue;
		}

		AddActorToGroup(GetModeKey(Actor), Actor, GroupMap);
	}

	GroupMap.GenerateValueArray(RootItems);
	RootItems.Sort([](const TSharedPtr<FPBRMagicOutlinerItem>& A, const TSharedPtr<FPBRMagicOutlinerItem>& B)
	{
		return A->DisplayName < B->DisplayName;
	});

	for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
	{
		Root->bExpanded = Root->bExpandedByDefault;
		Root->Children.Sort([](const TSharedPtr<FPBRMagicOutlinerItem>& A, const TSharedPtr<FPBRMagicOutlinerItem>& B)
		{
			return A->DisplayName < B->DisplayName;
		});
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		TFunction<void(const TSharedPtr<FPBRMagicOutlinerItem>&)> SyncExpansion =
			[this, &SyncExpansion](const TSharedPtr<FPBRMagicOutlinerItem>& Item)
		{
			if (!Item.IsValid())
			{
				return;
			}
			TreeView->SetItemExpansion(Item, Item->bExpanded);
			for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
			{
				SyncExpansion(Child);
			}
		};
		for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
		{
			SyncExpansion(Root);
		}
	}

	UpdateCachedCheckedCount();
	RefreshNameCheckList();
	RefreshCheckedList();
	StatusMessage = FString::Printf(TEXT("已扫描 %d 个 Actor，当前显示 %d 组，已勾选 %d 个"), CachedActorCount, RootItems.Num(), CachedCheckedCount);
}

void SPBRMagicOutlinerWindow::RebuildMaterialItems(const TArray<AActor*>& Actors)
{
	TMap<UMaterialInterface*, TSharedPtr<FPBRMagicOutlinerItem>> MaterialMap;
	for (AActor* Actor : Actors)
	{
		if (!Actor || !HasSceneMesh(Actor))
		{
			continue;
		}

		TArray<UMeshComponent*> MeshComponents;
		Actor->GetComponents<UMeshComponent>(MeshComponents);
		for (UMeshComponent* MeshComponent : MeshComponents)
		{
			if (!MeshComponent)
			{
				continue;
			}
			for (int32 SlotIndex = 0; SlotIndex < MeshComponent->GetNumMaterials(); ++SlotIndex)
			{
				if (UMaterialInterface* Material = MeshComponent->GetMaterial(SlotIndex))
				{
					if ((Material->IsA<UMaterial>() || Material->IsA<UMaterialInstance>()) && PassesClassicMaterialSearch(Actor, Material, MeshComponent, SlotIndex))
					{
						AddMaterialSlotToList(Material, Actor, MeshComponent, SlotIndex, MaterialMap);
					}
				}
			}
		}
	}

	MaterialMap.GenerateValueArray(RootItems);
	RootItems.Sort([](const TSharedPtr<FPBRMagicOutlinerItem>& A, const TSharedPtr<FPBRMagicOutlinerItem>& B)
	{
		return A->DisplayName < B->DisplayName;
	});

	for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
	{
		if (!Root.IsValid())
		{
			continue;
		}
		Root->bExpanded = Root->bExpandedByDefault;
		Root->Children.Sort([](const TSharedPtr<FPBRMagicOutlinerItem>& A, const TSharedPtr<FPBRMagicOutlinerItem>& B)
		{
			return A->DisplayName < B->DisplayName;
		});
		if (Root->Material.IsValid())
		{
			MaterialItemsByMaterial.Add(Root->Material, Root);
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}

	UpdateCachedCheckedCount();
	RefreshNameCheckList();
	RefreshCheckedList();
	TSet<TWeakObjectPtr<UMaterialInterface>> SelectedMaterials;
	CollectSelectedActorMaterials(SelectedMaterials);
	RefreshSelectedMaterialItems(SelectedMaterials);
	FocusMaterialItemsFromSet(SelectedMaterials, false);
	StatusMessage = FString::Printf(TEXT("已扫描 %d 个 Actor，当前材质 %d 个，已勾选 %d 个"), CachedActorCount, RootItems.Num(), CachedCheckedCount);
}

void SPBRMagicOutlinerWindow::RefreshNameCheckList()
{
	NameCheckListItems.Reset();

	TArray<AActor*> Actors;
	GetDisplayedActors(Actors);

	TMap<FString, int32> MatchCounts;
	for (AActor* Actor : Actors)
	{
		const FString Key = MakeNameCheckKey(Actor);
		if (!Key.IsEmpty())
		{
			MatchCounts.FindOrAdd(Key)++;
		}
	}

	for (const TPair<FString, int32>& Pair : MatchCounts)
	{
		if (Pair.Value < 4)
		{
			continue;
		}
		TSharedPtr<FPBRNameCheckListItem> Item = MakeShared<FPBRNameCheckListItem>();
		Item->Name = Pair.Key;
		Item->MatchCount = Pair.Value;
		NameCheckListItems.Add(Item);
	}
	NameCheckListItems.Sort([](const TSharedPtr<FPBRNameCheckListItem>& A, const TSharedPtr<FPBRNameCheckListItem>& B)
	{
		if (A->MatchCount != B->MatchCount)
		{
			return A->MatchCount > B->MatchCount;
		}
		return A->Name < B->Name;
	});

	if (NameCheckListView.IsValid())
	{
		NameCheckListView->RequestListRefresh();
	}
}

void SPBRMagicOutlinerWindow::RefreshCheckedList()
{
	CheckedListItems.Reset();
	TSet<TWeakObjectPtr<AActor>>& ActiveCheckedActors = GetActiveCheckedActors();
	for (auto It = ActiveCheckedActors.CreateIterator(); It; ++It)
	{
		AActor* Actor = It->Get();
		if (!Actor)
		{
			It.RemoveCurrent();
			continue;
		}

		TSharedPtr<FPBRCheckedActorListItem> Item = MakeShared<FPBRCheckedActorListItem>();
		Item->Actor = Actor;
		Item->DisplayName = ActorLabel(Actor);
		Item->TypeText = GetActorTypeText(Actor);
		Item->DetailText = GetActorDetailText(Actor);
		CheckedListItems.Add(Item);
	}
	for (auto It = PaintSelectedCheckedActors.CreateIterator(); It; ++It)
	{
		if (!It->IsValid() || !ActiveCheckedActors.Contains(*It))
		{
			It.RemoveCurrent();
		}
	}
	CheckedListItems.Sort([](const TSharedPtr<FPBRCheckedActorListItem>& A, const TSharedPtr<FPBRCheckedActorListItem>& B)
	{
		return A->DisplayName < B->DisplayName;
	});
	CachedCheckedCount = CheckedListItems.Num();
	if (CheckedListView.IsValid())
	{
		CheckedListView->RequestListRefresh();
	}
	if (bAutoSelectCheckedActors)
	{
		SyncAutoSelectCheckedActors();
	}
}

void SPBRMagicOutlinerWindow::LoadMagicOutlinerSettings()
{
	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		return;
	}

	bAutoSelectCheckedActors = false;
	Config->TryGetBoolField(MagicOutlinerCompactModeConfigKey, bCompactMode);
	Config->TryGetBoolField(MagicOutlinerClassicSkinConfigKey, bUseClassicSkin);
	FString ThemeIdText;
	if (Config->TryGetStringField(MagicOutlinerThemeConfigKey, ThemeIdText))
	{
		ActiveThemeId = FName(*ThemeIdText);
	}
	FString ShortcutText;
	if (Config->TryGetStringField(MagicOutlinerIsolationShortcutConfigKey, ShortcutText))
	{
		FInputChord ParsedChord;
		if (TryParseShortcut(ShortcutText, ParsedChord))
		{
			IsolationShortcut = ParsedChord;
		}
	}
}

void SPBRMagicOutlinerWindow::SaveMagicOutlinerSettings() const
{
	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		Config = MakeShared<FJsonObject>();
	}
	Config->SetBoolField(MagicOutlinerAutoSelectConfigKey, bAutoSelectCheckedActors);
	Config->SetBoolField(MagicOutlinerCompactModeConfigKey, bCompactMode);
	Config->SetBoolField(MagicOutlinerClassicSkinConfigKey, bUseClassicSkin);
	Config->SetStringField(MagicOutlinerThemeConfigKey, ActiveThemeId.ToString());
	Config->SetStringField(MagicOutlinerIsolationShortcutConfigKey, ShortcutToString(IsolationShortcut));
	FPBRDataStore::SaveConfig(Config);
}

TSet<TWeakObjectPtr<AActor>>& SPBRMagicOutlinerWindow::GetActiveCheckedActors()
{
	return CheckedActorsByCategory.FindOrAdd(ActiveCategory);
}

const TSet<TWeakObjectPtr<AActor>>& SPBRMagicOutlinerWindow::GetActiveCheckedActors() const
{
	if (const TSet<TWeakObjectPtr<AActor>>* Actors = CheckedActorsByCategory.Find(ActiveCategory))
	{
		return *Actors;
	}
	static const TSet<TWeakObjectPtr<AActor>> EmptyActors;
	return EmptyActors;
}

void SPBRMagicOutlinerWindow::GatherActors(TArray<AActor*>& OutActors) const
{
	if (!GEditor)
	{
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}
	for (ULevel* Level : World->GetLevels())
	{
		if (!Level)
		{
			continue;
		}
		for (AActor* Actor : Level->Actors)
		{
			if (Actor && !Actor->IsPendingKillPending() && !Actor->IsTemplate())
			{
				OutActors.Add(Actor);
			}
		}
	}
}

void SPBRMagicOutlinerWindow::GetDisplayedActors(TArray<AActor*>& OutActors) const
{
	TSet<AActor*> AddedActors;
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
	{
		CollectItemActors(Root, OutActors, AddedActors);
	}
}

void SPBRMagicOutlinerWindow::CollectItemActors(TSharedPtr<FPBRMagicOutlinerItem> Item, TArray<AActor*>& OutActors, TSet<AActor*>& AddedActors) const
{
	if (!Item.IsValid())
	{
		return;
	}
	if (AActor* Actor = Item->Actor.Get())
	{
		if (!AddedActors.Contains(Actor))
		{
			AddedActors.Add(Actor);
			OutActors.Add(Actor);
		}
	}
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
	{
		CollectItemActors(Child, OutActors, AddedActors);
	}
}

TSharedPtr<FPBRMagicOutlinerItem> SPBRMagicOutlinerWindow::FindFirstItemForActor(AActor* Actor, TArray<TSharedPtr<FPBRMagicOutlinerItem>>* OutAncestors) const
{
	if (!Actor)
	{
		return nullptr;
	}

	TSharedPtr<FPBRMagicOutlinerItem> FallbackItem;
	TArray<TSharedPtr<FPBRMagicOutlinerItem>> FallbackAncestors;
	TSharedPtr<FPBRMagicOutlinerItem> FoundItem;
	TArray<TSharedPtr<FPBRMagicOutlinerItem>> FoundAncestors;
	TArray<TSharedPtr<FPBRMagicOutlinerItem>> CurrentAncestors;
	TFunction<bool(const TSharedPtr<FPBRMagicOutlinerItem>&)> VisitItem =
		[Actor, &FallbackItem, &FallbackAncestors, &FoundItem, &FoundAncestors, &CurrentAncestors, &VisitItem](const TSharedPtr<FPBRMagicOutlinerItem>& Item)
	{
		if (!Item.IsValid())
		{
			return false;
		}

		if (Item->Actor.Get() == Actor)
		{
			if (!Item->MeshComponent.IsValid())
			{
				FoundItem = Item;
				FoundAncestors = CurrentAncestors;
				return true;
			}
			if (!FallbackItem.IsValid())
			{
				FallbackItem = Item;
				FallbackAncestors = CurrentAncestors;
			}
		}

		CurrentAncestors.Add(Item);
		for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
		{
			if (VisitItem(Child))
			{
				return true;
			}
		}
		CurrentAncestors.Pop();
		return false;
	};

	for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
	{
		if (VisitItem(Root))
		{
			break;
		}
	}

	if (FoundItem.IsValid())
	{
		if (OutAncestors)
		{
			*OutAncestors = FoundAncestors;
		}
		return FoundItem;
	}

	if (OutAncestors && FallbackItem.IsValid())
	{
		*OutAncestors = FallbackAncestors;
	}
	return FallbackItem;
}

void SPBRMagicOutlinerWindow::FocusFirstEditorSelectedActor(const TArray<AActor*>& SelectedActors)
{
	if (SelectedActors.IsEmpty())
	{
		return;
	}

	for (AActor* Actor : SelectedActors)
	{
		if (!Actor)
		{
			continue;
		}

		TArray<TSharedPtr<FPBRMagicOutlinerItem>> Ancestors;
		TSharedPtr<FPBRMagicOutlinerItem> ItemToFocus = FindFirstItemForActor(Actor, &Ancestors);
		if (!ItemToFocus.IsValid())
		{
			continue;
		}

		for (const TSharedPtr<FPBRMagicOutlinerItem>& Ancestor : Ancestors)
		{
			if (Ancestor.IsValid())
			{
				Ancestor->bExpanded = true;
				if (TreeView.IsValid())
				{
					TreeView->SetItemExpansion(Ancestor, true);
				}
			}
		}

		ActiveTreeItem = ItemToFocus;
		DetailsActor = Actor;
		if (TreeView.IsValid())
		{
			TreeView->RequestScrollIntoView(ItemToFocus);
		}
		return;
	}
}

void SPBRMagicOutlinerWindow::SyncAutoSelectCheckedActors()
{
	TArray<AActor*> Actors;
	for (const TWeakObjectPtr<AActor>& ActorPtr : GetActiveCheckedActors())
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			Actors.Add(Actor);
		}
	}
	SelectActors(Actors, false);
}

bool SPBRMagicOutlinerWindow::PassesCategory(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	switch (ActiveCategory)
	{
	case EPBRMagicOutlinerCategory::Models:
		return HasSceneMesh(Actor);
	case EPBRMagicOutlinerCategory::Materials:
		return HasComponentOfClass(Actor, UMeshComponent::StaticClass());
	case EPBRMagicOutlinerCategory::Lights:
		return Actor->IsA<ALight>();
	case EPBRMagicOutlinerCategory::Cameras:
		return Actor->IsA<ACameraActor>() || Actor->IsA<APostProcessVolume>();
	case EPBRMagicOutlinerCategory::Blueprints:
		return Actor->GetClass() && Actor->GetClass()->ClassGeneratedBy;
	case EPBRMagicOutlinerCategory::Levels:
	case EPBRMagicOutlinerCategory::All:
		return true;
	}
	return true;
}

bool SPBRMagicOutlinerWindow::PassesClassicSearch(AActor* Actor) const
{
	if (!bUseClassicSkin || ClassicSearchText.IsEmpty())
	{
		return true;
	}
	if (!Actor)
	{
		return false;
	}
	const FString Needle = ClassicSearchText.ToLower();
	return ActorLabel(Actor).ToLower().Contains(Needle)
		|| GetActorTypeText(Actor).ToLower().Contains(Needle)
		|| GetActorDetailText(Actor).ToLower().Contains(Needle)
		|| GetModeKey(Actor).ToLower().Contains(Needle);
}

bool SPBRMagicOutlinerWindow::PassesClassicMaterialSearch(AActor* Actor, UMaterialInterface* Material, UMeshComponent* MeshComponent, int32 SlotIndex) const
{
	if (!bUseClassicSkin || ClassicSearchText.IsEmpty())
	{
		return true;
	}
	if (!Actor || !Material)
	{
		return false;
	}
	const FString Needle = ClassicSearchText.ToLower();
	return ActorLabel(Actor).ToLower().Contains(Needle)
		|| GetActorTypeText(Actor).ToLower().Contains(Needle)
		|| GetActorDetailText(Actor).ToLower().Contains(Needle)
		|| Material->GetName().ToLower().Contains(Needle)
		|| Material->GetPathName().ToLower().Contains(Needle)
		|| (MeshComponent && MeshComponent->GetName().ToLower().Contains(Needle))
		|| FString::FromInt(SlotIndex).Contains(Needle);
}

FText SPBRMagicOutlinerWindow::GetModeLabel(EPBRMagicOutlinerMode Mode) const
{
	if (ActiveCategory == EPBRMagicOutlinerCategory::Lights)
	{
		switch (Mode)
		{
		case EPBRMagicOutlinerMode::Group: return LOCTEXT("LightModeGroup", "层级/关卡");
		case EPBRMagicOutlinerMode::Type: return LOCTEXT("LightModeType", "灯光类型");
		case EPBRMagicOutlinerMode::Material: return LOCTEXT("LightModeColor", "颜色/色温");
		case EPBRMagicOutlinerMode::Reference: return LOCTEXT("LightModeReference", "附加关系");
		case EPBRMagicOutlinerMode::State: return LOCTEXT("LightModeState", "可见状态");
		}
	}
	if (ActiveCategory == EPBRMagicOutlinerCategory::Materials)
	{
		switch (Mode)
		{
		case EPBRMagicOutlinerMode::Group: return LOCTEXT("MaterialModeGroup", "层级/关卡");
		case EPBRMagicOutlinerMode::Type: return LOCTEXT("MaterialModeMesh", "模型类型");
		case EPBRMagicOutlinerMode::Material: return LOCTEXT("MaterialModeParent", "PBR母材质");
		case EPBRMagicOutlinerMode::Reference: return LOCTEXT("MaterialModeSlot", "材质槽");
		case EPBRMagicOutlinerMode::State: return LOCTEXT("MaterialModeState", "可见状态");
		}
	}
	if (ActiveCategory == EPBRMagicOutlinerCategory::Cameras)
	{
		switch (Mode)
		{
		case EPBRMagicOutlinerMode::Group: return LOCTEXT("CameraModeGroup", "层级/关卡");
		case EPBRMagicOutlinerMode::Type: return LOCTEXT("CameraModeType", "相机类型");
		case EPBRMagicOutlinerMode::Material: return LOCTEXT("CameraModeLens", "镜头状态");
		case EPBRMagicOutlinerMode::Reference: return LOCTEXT("CameraModeReference", "附加关系");
		case EPBRMagicOutlinerMode::State: return LOCTEXT("CameraModeState", "可见状态");
		}
	}

	switch (Mode)
	{
	case EPBRMagicOutlinerMode::Group: return LOCTEXT("GroupMode", "组");
	case EPBRMagicOutlinerMode::Type: return LOCTEXT("TypeMode", "类型");
	case EPBRMagicOutlinerMode::Material: return LOCTEXT("MaterialMode", "材质");
	case EPBRMagicOutlinerMode::Reference: return LOCTEXT("ReferenceMode", "参照");
	case EPBRMagicOutlinerMode::State: return LOCTEXT("StateMode", "状态");
	}
	return FText::GetEmpty();
}

FString SPBRMagicOutlinerWindow::GetModeKey(AActor* Actor) const
{
	switch (ActiveMode)
	{
	case EPBRMagicOutlinerMode::Group:
		return GetGroupKey(Actor);
	case EPBRMagicOutlinerMode::Type:
		if (ActiveCategory == EPBRMagicOutlinerCategory::Lights) { return GetLightTypeKey(Actor); }
		if (ActiveCategory == EPBRMagicOutlinerCategory::Cameras) { return GetCameraTypeKey(Actor); }
		return GetTypeKey(Actor);
	case EPBRMagicOutlinerMode::Material:
		if (ActiveCategory == EPBRMagicOutlinerCategory::Lights)
		{
			if (ULightComponent* LightComponent = GetPrimaryLightComponent(Actor))
			{
				return FString::Printf(TEXT("色温 %.0fK / 颜色"), LightComponent->Temperature);
			}
			return TEXT("无灯光组件");
		}
		if (ActiveCategory == EPBRMagicOutlinerCategory::Materials) { return GetPBRParentMaterialKey(Actor); }
		return GetMaterialKey(Actor);
	case EPBRMagicOutlinerMode::Reference:
		return GetReferenceKey(Actor);
	case EPBRMagicOutlinerMode::State:
		return GetStateKey(Actor);
	}
	return TEXT("未分组");
}

FString SPBRMagicOutlinerWindow::GetGroupKey(AActor* Actor) const
{
	if (!Actor)
	{
		return TEXT("未分组");
	}

	auto GetReadableFolderPath = [](AActor* SourceActor)
	{
		if (!SourceActor)
		{
			return FString();
		}
		const FString FolderPath = SourceActor->GetFolderPath().ToString();
		return (!FolderPath.IsEmpty() && FolderPath != TEXT("None")) ? FolderPath : FString();
	};

	if (const FString FolderPath = GetReadableFolderPath(Actor); !FolderPath.IsEmpty())
	{
		return FString::Printf(TEXT("文件夹 / %s"), *FolderPath);
	}

	if (ActiveCategory == EPBRMagicOutlinerCategory::Models)
	{
		AActor* RootActor = Actor;
		while (RootActor)
		{
			AActor* ParentActor = RootActor->GetAttachParentActor();
			if (!ParentActor)
			{
				break;
			}
			RootActor = ParentActor;
		}

		if (RootActor && RootActor != Actor)
		{
			if (const FString RootFolderPath = GetReadableFolderPath(RootActor); !RootFolderPath.IsEmpty())
			{
				return FString::Printf(TEXT("文件夹 / %s"), *RootFolderPath);
			}
			return FString::Printf(TEXT("根组 / %s"), *ActorLabel(RootActor));
		}

		if (AActor* ParentActor = Actor->GetAttachParentActor())
		{
			return FString::Printf(TEXT("父级 / %s"), *ActorLabel(ParentActor));
		}
	}

	if (Actor->GetLevel())
	{
		return FString::Printf(TEXT("关卡 / %s"), *Actor->GetLevel()->GetOuter()->GetName());
	}
	return TEXT("未分组");
}

FString SPBRMagicOutlinerWindow::GetTypeKey(AActor* Actor) const
{
	return Actor && Actor->GetClass() ? Actor->GetClass()->GetName() : TEXT("未知类型");
}

FString SPBRMagicOutlinerWindow::GetLightTypeKey(AActor* Actor) const
{
	ULightComponent* LightComponent = GetPrimaryLightComponent(Actor);
	if (Cast<URectLightComponent>(LightComponent)) { return TEXT("矩形光源"); }
	if (Cast<USpotLightComponent>(LightComponent)) { return TEXT("聚光灯"); }
	if (Cast<UPointLightComponent>(LightComponent)) { return TEXT("点光源"); }
	if (Cast<UDirectionalLightComponent>(LightComponent)) { return TEXT("方向光"); }
	return Actor && Actor->IsA<ALight>() ? TEXT("其它灯光") : TEXT("无灯光组件");
}

FString SPBRMagicOutlinerWindow::GetCameraTypeKey(AActor* Actor) const
{
	if (Actor && Actor->IsA<APostProcessVolume>())
	{
		return TEXT("后期体积盒子");
	}
	if (Actor && Actor->IsA<ACameraActor>())
	{
		return TEXT("相机");
	}
	return Actor && Actor->GetClass() ? Actor->GetClass()->GetName() : TEXT("未知相机");
}

FString SPBRMagicOutlinerWindow::GetMaterialKey(AActor* Actor) const
{
	TArray<UMeshComponent*> MeshComponents;
	if (Actor)
	{
		Actor->GetComponents<UMeshComponent>(MeshComponents);
	}
	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}
		for (int32 SlotIndex = 0; SlotIndex < MeshComponent->GetNumMaterials(); ++SlotIndex)
		{
			if (UMaterialInterface* Material = MeshComponent->GetMaterial(SlotIndex))
			{
				return Material->GetName();
			}
		}
	}
	return TEXT("无材质");
}

FString SPBRMagicOutlinerWindow::GetPBRParentMaterialKey(AActor* Actor) const
{
	TArray<UMeshComponent*> MeshComponents;
	if (Actor)
	{
		Actor->GetComponents<UMeshComponent>(MeshComponents);
	}
	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}
		for (int32 SlotIndex = 0; SlotIndex < MeshComponent->GetNumMaterials(); ++SlotIndex)
		{
			if (UMaterialInterface* Material = MeshComponent->GetMaterial(SlotIndex))
			{
				const FString Family = PBRMaterialFamilyLabel(MaterialParentName(Material));
				return Family.IsEmpty() ? TEXT("非PBR母材质") : Family;
			}
		}
	}
	return TEXT("无材质");
}

FString SPBRMagicOutlinerWindow::GetReferenceKey(AActor* Actor) const
{
	if (!Actor)
	{
		return TEXT("无参照");
	}
	if (AActor* Parent = Actor->GetAttachParentActor())
	{
		return TEXT("附加到: ") + ActorLabel(Parent);
	}
	return TEXT("无父级参照");
}

FString SPBRMagicOutlinerWindow::GetStateKey(AActor* Actor) const
{
	if (!Actor)
	{
		return TEXT("未知状态");
	}
	if (Actor->IsHiddenEd())
	{
		return TEXT("编辑器隐藏");
	}
	return TEXT("可见");
}

FString SPBRMagicOutlinerWindow::GetActorTypeText(AActor* Actor) const
{
	if (ActiveCategory == EPBRMagicOutlinerCategory::Lights)
	{
		return GetLightTypeKey(Actor);
	}
	if (ActiveCategory == EPBRMagicOutlinerCategory::Materials)
	{
		return GetPBRParentMaterialKey(Actor);
	}
	if (ActiveCategory == EPBRMagicOutlinerCategory::Cameras)
	{
		return GetCameraTypeKey(Actor);
	}
	return GetTypeKey(Actor);
}

FString SPBRMagicOutlinerWindow::GetActorDetailText(AActor* Actor) const
{
	if (!Actor)
	{
		return FString();
	}
	if (ActiveCategory == EPBRMagicOutlinerCategory::Lights)
	{
		if (ULightComponent* LightComponent = GetPrimaryLightComponent(Actor))
		{
			return FString::Printf(TEXT("%.1f cd/lm | %.0fK"), LightComponent->Intensity, LightComponent->Temperature);
		}
	}
	if (ActiveCategory == EPBRMagicOutlinerCategory::Materials)
	{
		return GetMaterialKey(Actor);
	}
	if (ActiveCategory == EPBRMagicOutlinerCategory::Cameras)
	{
		if (APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor))
		{
			return FString::Printf(TEXT("%s | Priority %.1f"), Volume->bUnbound ? TEXT("Unbound") : TEXT("Bound"), Volume->Priority);
		}
		if (UCameraComponent* CameraComponent = Actor->FindComponentByClass<UCameraComponent>())
		{
			return FString::Printf(TEXT("PostProcess %.2f | FOV %.1f"), CameraComponent->PostProcessBlendWeight, CameraComponent->FieldOfView);
		}
	}
	return Actor->GetLevel() ? Actor->GetLevel()->GetOuter()->GetName() : FString();
}

void SPBRMagicOutlinerWindow::AddModelMeshChildren(TSharedPtr<FPBRMagicOutlinerItem> ActorItem, AActor* Actor)
{
	if (!ActorItem.IsValid() || !Actor || ActiveCategory != EPBRMagicOutlinerCategory::Models)
	{
		return;
	}

	TArray<UMeshComponent*> MeshComponents;
	Actor->GetComponents<UMeshComponent>(MeshComponents);
	MeshComponents.RemoveAll([](UMeshComponent* MeshComponent)
	{
		return MeshComponent == nullptr;
	});
	MeshComponents.Sort([](const UMeshComponent& A, const UMeshComponent& B)
	{
		return A.GetName() < B.GetName();
	});

	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		TSharedPtr<FPBRMagicOutlinerItem> MeshItem = MakeShared<FPBRMagicOutlinerItem>();
		MeshItem->Actor = Actor;
		MeshItem->MeshComponent = MeshComponent;
		MeshItem->bChecked = Actor && GetActiveCheckedActors().Contains(Actor);
		MeshItem->ActorCount = 1;
		MeshItem->TypeText = MeshComponent->GetClass() ? MeshComponent->GetClass()->GetName() : TEXT("MeshComponent");
		MeshItem->DisplayName = MeshComponent->GetName();
		MeshItem->DetailText = FString::Printf(TEXT("%d 个材质槽"), MeshComponent->GetNumMaterials());

		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
		{
			if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				MeshItem->DisplayName = FString::Printf(TEXT("%s / %s"), *MeshComponent->GetName(), *StaticMesh->GetName());
				MeshItem->DetailText = StaticMesh->GetPathName();
			}
		}

		ActorItem->Children.Add(MeshItem);
	}
}

void SPBRMagicOutlinerWindow::AddActorToGroup(const FString& GroupKey, AActor* Actor, TMap<FString, TSharedPtr<FPBRMagicOutlinerItem>>& GroupMap)
{
	TSharedPtr<FPBRMagicOutlinerItem>& Group = GroupMap.FindOrAdd(GroupKey);
	if (!Group.IsValid())
	{
		Group = MakeShared<FPBRMagicOutlinerItem>();
		Group->DisplayName = GroupKey.IsEmpty() ? TEXT("未分组") : GroupKey;
		Group->TypeText = TEXT("组");
		Group->bGroup = true;
		Group->bExpandedByDefault = GroupMap.Num() <= 12;
	}

	TSharedPtr<FPBRMagicOutlinerItem> Item = MakeShared<FPBRMagicOutlinerItem>();
	Item->DisplayName = ActorLabel(Actor);
	Item->TypeText = GetActorTypeText(Actor);
	Item->DetailText = GetActorDetailText(Actor);
	Item->Actor = Actor;
	Item->bChecked = Actor && GetActiveCheckedActors().Contains(Actor);
	Item->ActorCount = 1;
	Item->bExpandedByDefault = ActiveCategory == EPBRMagicOutlinerCategory::Models;
	Item->bExpanded = Item->bExpandedByDefault;
	AddModelMeshChildren(Item, Actor);
	Group->Children.Add(Item);
	Group->ActorCount = Group->Children.Num();
	Group->DetailText = FString::Printf(TEXT("%d 个对象"), Group->ActorCount);
}

void SPBRMagicOutlinerWindow::AddMaterialSlotToList(UMaterialInterface* Material, AActor* Actor, UMeshComponent* MeshComponent, int32 SlotIndex, TMap<UMaterialInterface*, TSharedPtr<FPBRMagicOutlinerItem>>& MaterialMap)
{
	if (!Material || !Actor || !MeshComponent || SlotIndex == INDEX_NONE)
	{
		return;
	}

	TSharedPtr<FPBRMagicOutlinerItem>& MaterialItem = MaterialMap.FindOrAdd(Material);
	if (!MaterialItem.IsValid())
	{
		MaterialItem = MakeShared<FPBRMagicOutlinerItem>();
		MaterialItem->DisplayName = Material->GetName();
		MaterialItem->TypeText = Material->IsA<UMaterialInstance>()
			? PBRText(TEXT("MaterialInstanceType"), TEXT("材质实例"), TEXT("Material Instance")).ToString()
			: PBRText(TEXT("ParentMaterialType"), TEXT("母材质"), TEXT("Parent Material")).ToString();
		MaterialItem->DetailText = Material->GetPathName();
		MaterialItem->Material = Material;
		MaterialItem->bGroup = true;
		MaterialItem->bChecked = Actor && GetActiveCheckedActors().Contains(Actor);
	}

	FPBRMaterialSlotReference SlotRef;
	SlotRef.Actor = Actor;
	SlotRef.MeshComponent = MeshComponent;
	SlotRef.SlotIndex = SlotIndex;
	MaterialItem->MaterialSlots.Add(SlotRef);

	TSharedPtr<FPBRMagicOutlinerItem> ActorItem;
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : MaterialItem->Children)
	{
		if (Child.IsValid() && Child->Actor.Get() == Actor)
		{
			ActorItem = Child;
			break;
		}
	}

	if (!ActorItem.IsValid())
	{
		ActorItem = MakeShared<FPBRMagicOutlinerItem>();
		ActorItem->DisplayName = ActorLabel(Actor);
		ActorItem->TypeText = GetTypeKey(Actor);
		ActorItem->DetailText = FString::Printf(TEXT("%s[%d]"), *MeshComponent->GetName(), SlotIndex);
		ActorItem->Actor = Actor;
		ActorItem->Material = Material;
		ActorItem->bChecked = GetActiveCheckedActors().Contains(Actor);
		ActorItem->ActorCount = 1;
		MaterialItem->Children.Add(ActorItem);
	}
	ActorItem->MaterialSlots.Add(SlotRef);

	TSet<TWeakObjectPtr<AActor>> UniqueActors;
	for (const FPBRMaterialSlotReference& ExistingSlot : MaterialItem->MaterialSlots)
	{
		if (ExistingSlot.Actor.IsValid())
		{
			UniqueActors.Add(ExistingSlot.Actor);
		}
	}
	MaterialItem->ActorCount = UniqueActors.Num();
	MaterialItem->DetailText = FString::Printf(TEXT("%d 个对象 / %d 个材质槽 | %s"),
		MaterialItem->ActorCount,
		MaterialItem->MaterialSlots.Num(),
		*Material->GetPathName());
}

FString SPBRMagicOutlinerWindow::MakeNameCheckKey(AActor* Actor) const
{
	return BuildNameCheckKeyFromLabel(ActorLabel(Actor));
}

ECheckBoxState SPBRMagicOutlinerWindow::GetNameCheckState(const FString& Name) const
{
	if (Name.IsEmpty())
	{
		return ECheckBoxState::Unchecked;
	}

	TArray<AActor*> Actors;
	GetDisplayedActors(Actors);

	int32 MatchCount = 0;
	int32 CheckedMatchCount = 0;
	for (AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}
		if (ActorLabel(Actor).Contains(Name, ESearchCase::IgnoreCase))
		{
			++MatchCount;
			if (GetActiveCheckedActors().Contains(Actor))
			{
				++CheckedMatchCount;
			}
		}
	}
	if (MatchCount == 0 || CheckedMatchCount == 0)
	{
		return ECheckBoxState::Unchecked;
	}
	return MatchCount == CheckedMatchCount ? ECheckBoxState::Checked : ECheckBoxState::Undetermined;
}

void SPBRMagicOutlinerWindow::SetNameChecked(const FString& Name, bool bChecked)
{
	if (Name.IsEmpty())
	{
		return;
	}

	TArray<AActor*> Actors;
	GetDisplayedActors(Actors);

	int32 ChangedCount = 0;
	int32 MatchCount = 0;
	for (AActor* Actor : Actors)
	{
		if (!Actor || !ActorLabel(Actor).Contains(Name, ESearchCase::IgnoreCase))
		{
			continue;
		}
		++MatchCount;
		const bool bWasChecked = GetActiveCheckedActors().Contains(Actor);
		SetActorChecked(Actor, bChecked);
		if (bWasChecked != bChecked)
		{
			++ChangedCount;
		}
	}

	UpdateCachedCheckedCount();
	RefreshNameCheckList();
	RefreshCheckedList();
	RefreshLightAdjustmentBaselines();
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
	StatusMessage = FString::Printf(TEXT("已%s名称包含“%s”的 %d 个对象，变化 %d 个，当前共勾选 %d 个"),
		bChecked ? TEXT("勾选") : TEXT("取消勾选"),
		*Name,
		MatchCount,
		ChangedCount,
		CachedCheckedCount);
}

void SPBRMagicOutlinerWindow::SetItemChecked(TSharedPtr<FPBRMagicOutlinerItem> Item, bool bChecked)
{
	if (!Item.IsValid())
	{
		return;
	}
	if (Item->MeshComponent.IsValid() && Item->Actor.IsValid())
	{
		SetActorChecked(Item->Actor.Get(), bChecked);
	}
	else
	{
		Item->bChecked = bChecked;
		if (Item->Actor.IsValid())
		{
			if (bChecked)
			{
				GetActiveCheckedActors().Add(Item->Actor);
			}
			else
			{
				GetActiveCheckedActors().Remove(Item->Actor);
			}
		}
		for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
		{
			SetItemChecked(Child, bChecked);
		}
	}
	UpdateCachedCheckedCount();
	RefreshNameCheckList();
	RefreshCheckedList();
	RefreshLightAdjustmentBaselines();
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
}

void SPBRMagicOutlinerWindow::SetActorChecked(AActor* Actor, bool bChecked)
{
	if (!Actor)
	{
		return;
	}
	if (bChecked)
	{
		GetActiveCheckedActors().Add(Actor);
	}
	else
	{
		GetActiveCheckedActors().Remove(Actor);
	}
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
	{
		TFunction<void(const TSharedPtr<FPBRMagicOutlinerItem>&)> UpdateItem =
			[Actor, bChecked, &UpdateItem](const TSharedPtr<FPBRMagicOutlinerItem>& Item)
		{
			if (!Item.IsValid())
			{
				return;
			}
			if (Item->Actor.Get() == Actor)
			{
				Item->bChecked = bChecked;
			}
			for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
			{
				UpdateItem(Child);
			}
		};
		UpdateItem(Root);
	}
}

ECheckBoxState SPBRMagicOutlinerWindow::GetItemCheckState(TSharedPtr<FPBRMagicOutlinerItem> Item) const
{
	if (!Item.IsValid())
	{
		return ECheckBoxState::Unchecked;
	}
	if (Item->Children.IsEmpty())
	{
		return Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	int32 CheckedChildren = 0;
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
	{
		if (Child.IsValid() && Child->bChecked)
		{
			++CheckedChildren;
		}
	}
	if (CheckedChildren == 0)
	{
		return ECheckBoxState::Unchecked;
	}
	return CheckedChildren == Item->Children.Num() ? ECheckBoxState::Checked : ECheckBoxState::Undetermined;
}

void SPBRMagicOutlinerWindow::CollectCheckedActors(TSharedPtr<FPBRMagicOutlinerItem> Item, TArray<AActor*>& OutActors) const
{
	if (!Item.IsValid())
	{
		return;
	}
	if (Item->Actor.IsValid() && Item->bChecked)
	{
		OutActors.Add(Item->Actor.Get());
	}
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
	{
		CollectCheckedActors(Child, OutActors);
	}
}

void SPBRMagicOutlinerWindow::CollectCheckedLightComponents(TArray<ULightComponent*>& OutLightComponents) const
{
	for (const TWeakObjectPtr<AActor>& ActorPtr : GetActiveCheckedActors())
	{
		AActor* Actor = ActorPtr.Get();
		if (!Actor)
		{
			continue;
		}
		TArray<ULightComponent*> LightComponents;
		Actor->GetComponents<ULightComponent>(LightComponents);
		OutLightComponents.Append(LightComponents);
	}
}

void SPBRMagicOutlinerWindow::RefreshLightAdjustmentBaselines()
{
	TArray<ULightComponent*> LightComponents;
	CollectCheckedLightComponents(LightComponents);
	TSet<TWeakObjectPtr<ULightComponent>> LiveComponents;
	for (ULightComponent* LightComponent : LightComponents)
	{
		if (!LightComponent)
		{
			continue;
		}
		TWeakObjectPtr<ULightComponent> Key(LightComponent);
		LiveComponents.Add(Key);
		if (!LightBaseIntensities.Contains(Key))
		{
			LightBaseIntensities.Add(Key, LightComponent->Intensity);
		}
	}
	for (auto It = LightBaseIntensities.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid() || !LiveComponents.Contains(It->Key))
		{
			It.RemoveCurrent();
		}
	}
}

int32 SPBRMagicOutlinerWindow::UpdateCachedCheckedCount()
{
	for (auto It = GetActiveCheckedActors().CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
	CachedCheckedCount = GetActiveCheckedActors().Num();
	return CachedCheckedCount;
}

void SPBRMagicOutlinerWindow::SelectActors(const TArray<AActor*>& Actors, bool bAddToSelection) const
{
	if (!GEditor)
	{
		return;
	}
	if (!bAddToSelection)
	{
		GEditor->SelectNone(false, true);
	}
	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			GEditor->SelectActor(Actor, true, true);
		}
	}
	GEditor->NoteSelectionChange();
}

void SPBRMagicOutlinerWindow::SelectItemActors(TSharedPtr<FPBRMagicOutlinerItem> Item, bool bAddToSelection) const
{
	TArray<AActor*> Actors;
	if (Item.IsValid())
	{
		TSet<AActor*> AddedActors;
		if (Item->Material.IsValid())
		{
			for (const FPBRMaterialSlotReference& SlotRef : Item->MaterialSlots)
			{
				if (AActor* SlotActor = SlotRef.Actor.Get())
				{
					if (!AddedActors.Contains(SlotActor))
					{
						AddedActors.Add(SlotActor);
						Actors.Add(SlotActor);
					}
				}
			}
		}
		CollectItemActors(Item, Actors, AddedActors);
	}
	SelectActors(Actors, bAddToSelection);
}

void SPBRMagicOutlinerWindow::GetModelToolTargetActors(TArray<AActor*>& OutActors) const
{
	TSet<AActor*> AddedActors;
	for (const TWeakObjectPtr<AActor>& ActorPtr : GetActiveCheckedActors())
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			if (PassesCategory(Actor) && !AddedActors.Contains(Actor))
			{
				AddedActors.Add(Actor);
				OutActors.Add(Actor);
			}
		}
	}
	if (!OutActors.IsEmpty())
	{
		return;
	}

	TArray<AActor*> SelectedActors;
	GetEditorSelectedActors(SelectedActors);
	for (AActor* Actor : SelectedActors)
	{
		if (Actor && HasSceneMesh(Actor) && !AddedActors.Contains(Actor))
		{
			AddedActors.Add(Actor);
			OutActors.Add(Actor);
		}
	}
	if (!OutActors.IsEmpty())
	{
		return;
	}

	CollectItemActors(ActiveTreeItem, OutActors, AddedActors);
}

bool SPBRMagicOutlinerWindow::IsActorSelectedInEditor(AActor* Actor) const
{
	if (!Actor || !GEditor)
	{
		return false;
	}

	USelection* Selection = GEditor->GetSelectedActors();
	return Selection && Selection->IsSelected(Actor);
}

bool SPBRMagicOutlinerWindow::IsItemRepresentedInEditorSelection(TSharedPtr<FPBRMagicOutlinerItem> Item) const
{
	if (!Item.IsValid())
	{
		return false;
	}

	if (Item->Actor.IsValid() && IsActorSelectedInEditor(Item->Actor.Get()))
	{
		return true;
	}

	for (const FPBRMaterialSlotReference& SlotRef : Item->MaterialSlots)
	{
		if (SlotRef.Actor.IsValid() && IsActorSelectedInEditor(SlotRef.Actor.Get()))
		{
			return true;
		}
	}

	for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Item->Children)
	{
		if (IsItemRepresentedInEditorSelection(Child))
		{
			return true;
		}
	}

	return false;
}

void SPBRMagicOutlinerWindow::SyncMaterialListSelectionFromEditor()
{
	if (ActiveCategory != EPBRMagicOutlinerCategory::Materials || RootItems.IsEmpty())
	{
		return;
	}

	TSet<TWeakObjectPtr<UMaterialInterface>> SelectedMaterials;
	CollectSelectedActorMaterials(SelectedMaterials);
	RefreshSelectedMaterialItems(SelectedMaterials);
	FocusMaterialItemsFromSet(SelectedMaterials, false);
}

void SPBRMagicOutlinerWindow::CollectSelectedActorMaterials(TSet<TWeakObjectPtr<UMaterialInterface>>& OutMaterials) const
{
	TArray<AActor*> SelectedActors;
	GetEditorSelectedActors(SelectedActors);
	for (AActor* Actor : SelectedActors)
	{
		if (!Actor)
		{
			continue;
		}
		TArray<UMeshComponent*> MeshComponents;
		Actor->GetComponents<UMeshComponent>(MeshComponents);
		for (UMeshComponent* MeshComponent : MeshComponents)
		{
			if (!MeshComponent)
			{
				continue;
			}
			for (int32 SlotIndex = 0; SlotIndex < MeshComponent->GetNumMaterials(); ++SlotIndex)
			{
				if (UMaterialInterface* Material = MeshComponent->GetMaterial(SlotIndex))
				{
					OutMaterials.Add(Material);
				}
			}
		}
	}
}

void SPBRMagicOutlinerWindow::GetEditorSelectedActors(TArray<AActor*>& OutActors) const
{
	if (!GEditor)
	{
		return;
	}

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection)
	{
		return;
	}

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			OutActors.Add(Actor);
		}
	}
}

void SPBRMagicOutlinerWindow::HandlePostUndoRedo()
{
	if (ActiveCategory == EPBRMagicOutlinerCategory::Materials)
	{
		RebuildItems();
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

UMaterialInterface* SPBRMagicOutlinerWindow::GetDraggedMaterial(const FDragDropEvent& DragDropEvent) const
{
	TSharedPtr<FAssetDragDropOp> AssetOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!AssetOp.IsValid())
	{
		return nullptr;
	}

	for (const FAssetData& AssetData : AssetOp->GetAssets())
	{
		if (UObject* Asset = AssetData.GetAsset())
		{
			if (UMaterialInterface* Material = Cast<UMaterialInterface>(Asset))
			{
				return Material;
			}
		}
	}
	return nullptr;
}

UStaticMesh* SPBRMagicOutlinerWindow::GetDraggedStaticMesh(const FDragDropEvent& DragDropEvent) const
{
	TSharedPtr<FAssetDragDropOp> AssetOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!AssetOp.IsValid())
	{
		return nullptr;
	}

	for (const FAssetData& AssetData : AssetOp->GetAssets())
	{
		if (UObject* Asset = AssetData.GetAsset())
		{
			if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
			{
				return StaticMesh;
			}
		}
	}
	return nullptr;
}

FReply SPBRMagicOutlinerWindow::OnModelReplacementDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, TSharedPtr<FPBRMagicOutlinerItem> Item)
{
	if (ActiveCategory != EPBRMagicOutlinerCategory::Models)
	{
		return FReply::Unhandled();
	}

	UStaticMesh* NewMesh = GetDraggedStaticMesh(DragDropEvent);
	if (!NewMesh)
	{
		StatusMessage = TEXT("请拖入 Static Mesh 资产用于替换模型");
		return FReply::Unhandled();
	}

	TArray<AActor*> Actors;
	if (Item.IsValid())
	{
		TSet<AActor*> AddedActors;
		CollectItemActors(Item, Actors, AddedActors);
	}
	if (Actors.IsEmpty())
	{
		GetModelToolTargetActors(Actors);
	}

	const int32 ReplacedComponents = ReplaceModelActorsMesh(Actors, NewMesh);
	if (ReplacedComponents <= 0)
	{
		StatusMessage = FString::Printf(TEXT("没有找到可替换的 StaticMeshComponent: %s"), *NewMesh->GetName());
		return FReply::Handled();
	}

	StatusMessage = FString::Printf(TEXT("已用 %s 替换 %d 个模型组件"), *NewMesh->GetName(), ReplacedComponents);
	SelectActors(Actors, false);
	RebuildItems();
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnMaterialItemDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, TSharedPtr<FPBRMagicOutlinerItem> Item)
{
	if (ActiveCategory != EPBRMagicOutlinerCategory::Materials || !Item.IsValid() || !Item->Material.IsValid())
	{
		return FReply::Unhandled();
	}

	UMaterialInterface* NewMaterial = GetDraggedMaterial(DragDropEvent);
	if (!NewMaterial)
	{
		StatusMessage = TEXT("请拖入材质或材质实例资产");
		return FReply::Unhandled();
	}

	TArray<AActor*> AffectedActors;
	for (const FPBRMaterialSlotReference& SlotRef : Item->MaterialSlots)
	{
		if (AActor* Actor = SlotRef.Actor.Get())
		{
			AffectedActors.AddUnique(Actor);
		}
	}

	const int32 ReplacedSlots = ReplaceMaterialItem(Item, NewMaterial);
	if (ReplacedSlots <= 0)
	{
		StatusMessage = FString::Printf(TEXT("%s 没有可替换的场景材质槽"), *Item->DisplayName);
		return FReply::Handled();
	}

	StatusMessage = FString::Printf(TEXT("已用 %s 替换 %s，更新 %d 个材质槽"),
		*NewMaterial->GetName(),
		*Item->DisplayName,
		ReplacedSlots);
	SelectActors(AffectedActors, false);
	RebuildItems();
	TSet<TWeakObjectPtr<UMaterialInterface>> NewMaterials;
	NewMaterials.Add(NewMaterial);
	RefreshSelectedMaterialItems(NewMaterials);
	FocusMaterialItemsFromSet(NewMaterials, true);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnMaterialSlotDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, TSharedPtr<FPBRMagicOutlinerItem> Item, int32 MaterialSlotIndex)
{
	if (ActiveCategory != EPBRMagicOutlinerCategory::Materials || !Item.IsValid() || !Item->Material.IsValid() || !Item->MaterialSlots.IsValidIndex(MaterialSlotIndex))
	{
		return FReply::Unhandled();
	}

	UMaterialInterface* NewMaterial = GetDraggedMaterial(DragDropEvent);
	if (!NewMaterial)
	{
		StatusMessage = TEXT("请拖入材质或材质实例资产");
		return FReply::Unhandled();
	}

	const int32 ReplacedSlots = ReplaceMaterialSlot(Item, MaterialSlotIndex, NewMaterial);
	if (ReplacedSlots <= 0)
	{
		StatusMessage = FString::Printf(TEXT("%s 的材质槽没有可替换内容"), *Item->DisplayName);
		return FReply::Handled();
	}

	const FPBRMaterialSlotReference& SlotRef = Item->MaterialSlots[MaterialSlotIndex];
	StatusMessage = FString::Printf(TEXT("已用 %s 替换 %s 的 %d 号槽"),
		*NewMaterial->GetName(),
		*Item->DisplayName,
		SlotRef.SlotIndex);
	RebuildItems();
	TSet<TWeakObjectPtr<UMaterialInterface>> NewMaterials;
	NewMaterials.Add(NewMaterial);
	RefreshSelectedMaterialItems(NewMaterials);
	FocusMaterialItemsFromSet(NewMaterials, true);
	return FReply::Handled();
}

void SPBRMagicOutlinerWindow::OpenMaterialEditor(TSharedPtr<FPBRMagicOutlinerItem> Item)
{
	if (!Item.IsValid() || !Item->Material.IsValid() || !GEditor)
	{
		return;
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(Item->Material.Get());
		StatusMessage = FString::Printf(TEXT("已打开材质参数: %s"), *Item->Material->GetName());
	}
}

int32 SPBRMagicOutlinerWindow::ReplaceMaterialItem(TSharedPtr<FPBRMagicOutlinerItem> Item, UMaterialInterface* NewMaterial)
{
	if (!Item.IsValid() || !Item->Material.IsValid() || !NewMaterial || Item->Material.Get() == NewMaterial)
	{
		return 0;
	}

	UMaterialInterface* OldMaterial = Item->Material.Get();
	const FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ReplaceSceneMaterialTransaction", "Replace scene material {0}"),
		FText::FromString(OldMaterial->GetName())));

	int32 ReplacedSlots = 0;
	for (const FPBRMaterialSlotReference& SlotRef : Item->MaterialSlots)
	{
		UMeshComponent* MeshComponent = SlotRef.MeshComponent.Get();
		AActor* Actor = SlotRef.Actor.Get();
		if (!MeshComponent || SlotRef.SlotIndex == INDEX_NONE || MeshComponent->GetMaterial(SlotRef.SlotIndex) != OldMaterial)
		{
			continue;
		}

		if (Actor)
		{
			Actor->SetFlags(RF_Transactional);
			Actor->Modify();
		}
		MeshComponent->SetFlags(RF_Transactional);
		MeshComponent->Modify();
		MeshComponent->SetMaterial(SlotRef.SlotIndex, NewMaterial);
		MeshComponent->MarkRenderStateDirty();
		++ReplacedSlots;
	}

	if (GEditor && ReplacedSlots > 0)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return ReplacedSlots;
}

int32 SPBRMagicOutlinerWindow::ReplaceMaterialSlot(TSharedPtr<FPBRMagicOutlinerItem> Item, int32 MaterialSlotIndex, UMaterialInterface* NewMaterial)
{
	if (!Item.IsValid() || !Item->Material.IsValid() || !NewMaterial || !Item->MaterialSlots.IsValidIndex(MaterialSlotIndex) || Item->Material.Get() == NewMaterial)
	{
		return 0;
	}

	UMaterialInterface* OldMaterial = Item->Material.Get();
	const FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ReplaceSceneMaterialSlotTransaction", "Replace scene material slot {0}"),
		FText::AsNumber(MaterialSlotIndex)));

	const FPBRMaterialSlotReference& SlotRef = Item->MaterialSlots[MaterialSlotIndex];
	UMeshComponent* MeshComponent = SlotRef.MeshComponent.Get();
	AActor* Actor = SlotRef.Actor.Get();
	if (!MeshComponent || SlotRef.SlotIndex == INDEX_NONE || MeshComponent->GetMaterial(SlotRef.SlotIndex) != OldMaterial)
	{
		return 0;
	}

	if (Actor)
	{
		Actor->SetFlags(RF_Transactional);
		Actor->Modify();
	}
	MeshComponent->SetFlags(RF_Transactional);
	MeshComponent->Modify();
	MeshComponent->SetMaterial(SlotRef.SlotIndex, NewMaterial);
	MeshComponent->MarkRenderStateDirty();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return 1;
}

int32 SPBRMagicOutlinerWindow::ReplaceModelActorsMesh(const TArray<AActor*>& Actors, UStaticMesh* NewMesh)
{
	if (!NewMesh || Actors.IsEmpty())
	{
		return 0;
	}

	const FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ReplaceModelActorsMeshTransaction", "Replace model meshes with {0}"),
		FText::FromString(NewMesh->GetName())));

	int32 ReplacedComponents = 0;
	for (AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}
		TArray<UStaticMeshComponent*> StaticMeshComponents;
		Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
		for (UStaticMeshComponent* MeshComponent : StaticMeshComponents)
		{
			if (!MeshComponent || MeshComponent->GetStaticMesh() == NewMesh)
			{
				continue;
			}
			Actor->SetFlags(RF_Transactional);
			Actor->Modify();
			MeshComponent->SetFlags(RF_Transactional);
			MeshComponent->Modify();
			MeshComponent->SetStaticMesh(NewMesh);
			MeshComponent->MarkRenderStateDirty();
			++ReplacedComponents;
		}
	}

	if (GEditor && ReplacedComponents > 0)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return ReplacedComponents;
}

FReply SPBRMagicOutlinerWindow::OnModelReplaceActorsClicked()
{
	TArray<AActor*> Actors;
	GetModelToolTargetActors(Actors);
	StatusMessage = FString::Printf(TEXT("替换物体：请把 Static Mesh 资产拖到模型行或右侧模型工具区。当前目标 %d 个模型。"), Actors.Num());
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnModelGroupToFolderClicked()
{
	MoveModelTargetsToFolder(TEXT("MagicOutliner_Group"));
	return FReply::Handled();
}

int32 SPBRMagicOutlinerWindow::MoveActorsToFolder(const TArray<AActor*>& Actors, const FString& FolderName, bool bUseAttachmentRoot)
{
	FString NormalizedFolderName = FolderName;
	NormalizedFolderName.TrimStartAndEndInline();
	NormalizedFolderName.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (NormalizedFolderName.StartsWith(TEXT("/")))
	{
		NormalizedFolderName.RightChopInline(1);
	}
	while (NormalizedFolderName.EndsWith(TEXT("/")))
	{
		NormalizedFolderName.LeftChopInline(1);
	}
	if (NormalizedFolderName.IsEmpty())
	{
		return 0;
	}

	const FName FolderPath(*NormalizedFolderName);
	TSet<AActor*> MovedRoots;
	for (AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}

		AActor* MoveRoot = Actor;
		if (bUseAttachmentRoot)
		{
			while (AActor* ParentActor = MoveRoot->GetAttachParentActor())
			{
				MoveRoot = ParentActor;
			}
		}
		if (!MoveRoot || MovedRoots.Contains(MoveRoot))
		{
			continue;
		}

		MovedRoots.Add(MoveRoot);
		MoveRoot->SetFlags(RF_Transactional);
		MoveRoot->Modify();
		MoveRoot->SetFolderPath_Recursively(FolderPath);
	}

	if (GEditor && !MovedRoots.IsEmpty())
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return MovedRoots.Num();
}

int32 SPBRMagicOutlinerWindow::MoveActorsToNewParentActor(const TArray<AActor*>& Actors, const FString& NewActorName)
{
	TArray<AActor*> ValidActors;
	for (AActor* Actor : Actors)
	{
		if (Actor && !ValidActors.Contains(Actor))
		{
			ValidActors.Add(Actor);
		}
	}
	if (ValidActors.IsEmpty())
	{
		return 0;
	}

	UWorld* World = ValidActors[0]->GetWorld();
	if (!World)
	{
		return 0;
	}

	FString SafeActorName = NewActorName;
	SafeActorName.TrimStartAndEndInline();
	if (SafeActorName.IsEmpty())
	{
		SafeActorName = TEXT("MagicOutliner_GroupActor");
	}

	FVector Center = FVector::ZeroVector;
	for (AActor* Actor : ValidActors)
	{
		Center += Actor->GetActorLocation();
	}
	Center /= static_cast<float>(ValidActors.Num());

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), FName(TEXT("MagicOutliner_GroupActor")));
	SpawnParams.ObjectFlags = RF_Transactional;
	SpawnParams.OverrideLevel = ValidActors[0]->GetLevel();
	AActor* ParentActor = World->SpawnActor<AActor>(AActor::StaticClass(), Center, FRotator::ZeroRotator, SpawnParams);
	if (!ParentActor)
	{
		return 0;
	}

	ParentActor->SetFlags(RF_Transactional);
	ParentActor->Modify();
	USceneComponent* RootComponent = NewObject<USceneComponent>(ParentActor, USceneComponent::StaticClass(), TEXT("Root"));
	if (RootComponent)
	{
		RootComponent->SetFlags(RF_Transactional);
		ParentActor->SetRootComponent(RootComponent);
		ParentActor->AddInstanceComponent(RootComponent);
		RootComponent->RegisterComponent();
	}
	ParentActor->SetActorLocation(Center);
	ParentActor->SetActorLabel(SafeActorName, true);

	int32 AttachedCount = 0;
	for (AActor* Actor : ValidActors)
	{
		if (!Actor || Actor == ParentActor)
		{
			continue;
		}
		Actor->SetFlags(RF_Transactional);
		Actor->Modify();
		if (Actor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform))
		{
			++AttachedCount;
		}
	}

	if (GEditor && AttachedCount > 0)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return AttachedCount;
}

void SPBRMagicOutlinerWindow::MoveModelTargetsToFolder(const FString& FolderName)
{
	TArray<AActor*> Actors;
	GetModelToolTargetActors(Actors);
	if (Actors.IsEmpty())
	{
		StatusMessage = TEXT("没有可成组的模型目标");
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Move magic outliner models to folder")));
	const int32 MovedRoots = MoveActorsToFolder(Actors, FolderName, false);
	if (MovedRoots <= 0)
	{
		StatusMessage = TEXT("文件夹名称无效，未移动模型");
		return;
	}
	StatusMessage = FString::Printf(TEXT("已将 %d 个所选模型移动到文件夹: %s"), MovedRoots, *FolderName);
	RebuildItems();
}

void SPBRMagicOutlinerWindow::ApplyModelBatchRename(const FString& Prefix, int32 StartIndex, bool bKeepOriginalName, bool bMoveToFolder, const FString& FolderName, bool bMoveToNewActor, const FString& NewActorName)
{
	TArray<AActor*> Actors;
	GetModelToolTargetActors(Actors);
	if (Actors.IsEmpty())
	{
		StatusMessage = TEXT("没有可重命名的模型目标");
		return;
	}

	const FString SafePrefix = Prefix.IsEmpty() ? TEXT("Model") : Prefix;
	const FScopedTransaction Transaction(FText::FromString(TEXT("Batch rename magic outliner models")));
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		AActor* Actor = Actors[Index];
		if (!Actor)
		{
			continue;
		}
		const FString OldLabel = Actor->GetActorLabel();
		const FString NewLabel = bKeepOriginalName
			? FString::Printf(TEXT("%s_%03d_%s"), *SafePrefix, StartIndex + Index, *OldLabel)
			: FString::Printf(TEXT("%s_%03d"), *SafePrefix, StartIndex + Index);
		Actor->SetFlags(RF_Transactional);
		Actor->Modify();
		Actor->SetActorLabel(NewLabel, true);
	}
	int32 AttachedActors = 0;
	if (bMoveToNewActor)
	{
		AttachedActors = MoveActorsToNewParentActor(Actors, NewActorName);
	}
	int32 MovedRoots = 0;
	if (bMoveToFolder)
	{
		MovedRoots = MoveActorsToFolder(Actors, FolderName, false);
	}
	if (bMoveToFolder && bMoveToNewActor)
	{
		StatusMessage = FString::Printf(TEXT("已批量重命名 %d 个模型，挂到新 Actor %d 个，并移动 %d 个所选模型到文件夹: %s"),
			Actors.Num(),
			AttachedActors,
			MovedRoots,
			*FolderName);
	}
	else if (bMoveToNewActor)
	{
		StatusMessage = FString::Printf(TEXT("已批量重命名 %d 个模型，并挂到新 Actor %d 个"), Actors.Num(), AttachedActors);
	}
	else if (bMoveToFolder)
	{
		StatusMessage = FString::Printf(TEXT("已批量重命名 %d 个模型，并移动 %d 个所选模型到文件夹: %s"), Actors.Num(), MovedRoots, *FolderName);
	}
	else
	{
		StatusMessage = FString::Printf(TEXT("已批量重命名 %d 个模型"), Actors.Num());
	}
	RebuildItems();
}

FReply SPBRMagicOutlinerWindow::OnModelBatchRenameClicked()
{
	TArray<AActor*> Actors;
	GetModelToolTargetActors(Actors);
	if (Actors.IsEmpty())
	{
		StatusMessage = TEXT("没有可重命名的模型目标");
		return FReply::Handled();
	}

	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(PBRText(TEXT("ModelBatchRenameWindow"), TEXT("模型批量重命名"), TEXT("Model Batch Rename")))
		.ClientSize(FVector2D(500.0f, 390.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedPtr<SEditableTextBox> PrefixBox;
	TSharedPtr<SEditableTextBox> FolderBox;
	TSharedPtr<SEditableTextBox> NewActorBox;
	struct FModelRenameDialogState
	{
		bool bKeepOriginalName = false;
		bool bMoveToFolder = false;
		bool bMoveToNewActor = false;
		int32 StartIndex = 1;
	};
	TSharedRef<FModelRenameDialogState> DialogState = MakeShared<FModelRenameDialogState>();

	Dialog->SetContent(
		SNew(SBorder)
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("将处理 %d 个模型"), Actors.Num())))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SAssignNew(PrefixBox, SEditableTextBox)
				.Text(FText::FromString(TEXT("Model")))
				.HintText(PBRText(TEXT("RenamePrefixHint"), TEXT("名称前缀"), TEXT("Name Prefix")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SNumericEntryBox<int32>)
				.Value_Lambda([DialogState]() { return DialogState->StartIndex; })
				.OnValueChanged_Lambda([DialogState](int32 NewValue) { DialogState->StartIndex = FMath::Max(0, NewValue); })
				.LabelVAlign(VAlign_Center)
				.Label()[SNew(STextBlock).Text(PBRText(TEXT("RenameStartIndex"), TEXT("起始编号"), TEXT("Start Index")))]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([DialogState](ECheckBoxState State) { DialogState->bKeepOriginalName = State == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(PBRText(TEXT("RenameKeepOriginal"), TEXT("保留原名称作为后缀"), TEXT("Keep original name as suffix")))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([DialogState](ECheckBoxState State) { DialogState->bMoveToFolder = State == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(PBRText(TEXT("RenameMoveFolder"), TEXT("同时移动到文件夹"), TEXT("Move to folder")))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SAssignNew(FolderBox, SEditableTextBox)
				.Text(FText::FromString(TEXT("MagicOutliner_Group")))
				.HintText(PBRText(TEXT("RenameFolderHint"), TEXT("文件夹名称"), TEXT("Folder Name")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([DialogState](ECheckBoxState State) { DialogState->bMoveToNewActor = State == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(PBRText(TEXT("RenameMoveNewActor"), TEXT("同时移动到新的 Actor 父级"), TEXT("Move under new parent Actor")))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SAssignNew(NewActorBox, SEditableTextBox)
				.Text(FText::FromString(TEXT("MagicOutliner_GroupActor")))
				.HintText(PBRText(TEXT("RenameNewActorHint"), TEXT("新的 Actor 名称"), TEXT("New Actor Name")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(PBRText(
					TEXT("RenameDatasmithWarning"),
					TEXT("Datasmith 提示：移动到文件夹只整理所选模型 Actor；创建新的 Actor 父级会改变所选模型的附加层级，后续 Datasmith 重新同步可能按源文件层级恢复或覆盖这类层级调整。"),
					TEXT("Datasmith note: moving to a folder only organizes the selected model Actors; a new parent Actor changes their attachment hierarchy and may be restored or overwritten by later Datasmith re-sync.")))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.72f, 0.32f)))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(PBRText(TEXT("Cancel"), TEXT("取消"), TEXT("Cancel")))
					.OnClicked_Lambda([Dialog]() { Dialog->RequestDestroyWindow(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(PBRText(TEXT("Apply"), TEXT("应用"), TEXT("Apply")))
					.OnClicked_Lambda([this, Dialog, PrefixBox, FolderBox, NewActorBox, DialogState]()
					{
						if (DialogState->bMoveToNewActor)
						{
							const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(
								EAppMsgType::YesNo,
								PBRText(
									TEXT("ConfirmDatasmithHierarchyChange"),
									TEXT("创建新的 Actor 父级会把所选模型直接重新挂到这个 Actor 下。Datasmith 重新同步时可能按源文件层级恢复或覆盖这个层级调整。仍然继续吗？"),
									TEXT("Creating a new parent Actor directly reparents the selected model Actors under it. A Datasmith re-sync may restore or overwrite this hierarchy change. Continue?")));
							if (ConfirmResult != EAppReturnType::Yes)
							{
								return FReply::Handled();
							}
						}
						ApplyModelBatchRename(
							PrefixBox.IsValid() ? PrefixBox->GetText().ToString() : FString(),
							DialogState->StartIndex,
							DialogState->bKeepOriginalName,
							DialogState->bMoveToFolder,
							FolderBox.IsValid() ? FolderBox->GetText().ToString() : FString(),
							DialogState->bMoveToNewActor,
							NewActorBox.IsValid() ? NewActorBox->GetText().ToString() : FString());
						Dialog->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]);

	FSlateApplication::Get().AddWindow(Dialog);
	return FReply::Handled();
}

bool SPBRMagicOutlinerWindow::IsIsolationProtectedActor(AActor* Actor) const
{
	if (!Actor || !Actor->GetClass())
	{
		return false;
	}

	const FString Label = ActorLabel(Actor);
	const FString ObjectName = Actor->GetName();
	const FString ClassName = Actor->GetClass()->GetName();
	return Label.Contains(TEXT("SunSky"), ESearchCase::IgnoreCase)
		|| ObjectName.Contains(TEXT("SunSky"), ESearchCase::IgnoreCase)
		|| ClassName.Contains(TEXT("SunSky"), ESearchCase::IgnoreCase);
}

bool SPBRMagicOutlinerWindow::DoesKeyEventMatchShortcut(const FKeyEvent& InKeyEvent) const
{
	return IsolationShortcut.IsValidChord()
		&& InKeyEvent.GetKey() == IsolationShortcut.Key
		&& InKeyEvent.IsAltDown() == IsolationShortcut.bAlt
		&& InKeyEvent.IsControlDown() == IsolationShortcut.bCtrl
		&& InKeyEvent.IsShiftDown() == IsolationShortcut.bShift
		&& InKeyEvent.IsCommandDown() == IsolationShortcut.bCmd;
}

FString SPBRMagicOutlinerWindow::ShortcutToString(const FInputChord& Chord) const
{
	if (!Chord.IsValidChord())
	{
		return FString();
	}
	TArray<FString> Parts;
	if (Chord.bCtrl) { Parts.Add(TEXT("Ctrl")); }
	if (Chord.bAlt) { Parts.Add(TEXT("Alt")); }
	if (Chord.bShift) { Parts.Add(TEXT("Shift")); }
	if (Chord.bCmd) { Parts.Add(TEXT("Cmd")); }
	Parts.Add(Chord.Key.GetFName().ToString());
	return FString::Join(Parts, TEXT("+"));
}

bool SPBRMagicOutlinerWindow::TryParseShortcut(const FString& Text, FInputChord& OutChord) const
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("+"), true);
	if (Parts.IsEmpty())
	{
		return false;
	}

	bool bCtrl = false;
	bool bAlt = false;
	bool bShift = false;
	bool bCmd = false;
	FKey Key;
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.Equals(TEXT("Ctrl"), ESearchCase::IgnoreCase) || Part.Equals(TEXT("Control"), ESearchCase::IgnoreCase))
		{
			bCtrl = true;
		}
		else if (Part.Equals(TEXT("Alt"), ESearchCase::IgnoreCase))
		{
			bAlt = true;
		}
		else if (Part.Equals(TEXT("Shift"), ESearchCase::IgnoreCase))
		{
			bShift = true;
		}
		else if (Part.Equals(TEXT("Cmd"), ESearchCase::IgnoreCase) || Part.Equals(TEXT("Command"), ESearchCase::IgnoreCase))
		{
			bCmd = true;
		}
		else
		{
			Key = FKey(*Part);
		}
	}

	OutChord = FInputChord(Key, bShift, bCtrl, bAlt, bCmd);
	return OutChord.IsValidChord();
}

FString SPBRMagicOutlinerWindow::BuildShortcutConflictText(const FInputChord& Chord) const
{
	if (!Chord.IsValidChord())
	{
		return TEXT("快捷键无效，请输入例如 Alt+Q。");
	}

	TArray<TSharedPtr<FBindingContext>> Contexts;
	FInputBindingManager::Get().GetKnownInputContexts(Contexts);
	TArray<FString> Conflicts;
	for (const TSharedPtr<FBindingContext>& Context : Contexts)
	{
		if (!Context.IsValid() || Context->GetContextName() == FName(TEXT("PBRStudio")))
		{
			continue;
		}
		const TSharedPtr<FUICommandInfo> Command = FInputBindingManager::Get().FindCommandInContext(Context->GetContextName(), Chord, false);
		if (Command.IsValid())
		{
			Conflicts.Add(FString::Printf(TEXT("%s / %s"), *Context->GetContextDesc().ToString(), *Command->GetLabel().ToString()));
		}
	}

	if (Conflicts.IsEmpty())
	{
		return FString::Printf(TEXT("%s 未发现 UE 已注册命令冲突。"), *ShortcutToString(Chord));
	}
	return FString::Printf(TEXT("%s 可能与以下 UE 快捷键冲突：%s"), *ShortcutToString(Chord), *FString::Join(Conflicts, TEXT("；")));
}

TSharedRef<SWidget> SPBRMagicOutlinerWindow::BuildShortcutSettingsContent()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ShortcutSettingsTitle", "魔法大纲快捷键"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("ShortcutSettingsNote", "这里只设置 UE 端插件自己的快捷键，不写入 UE 编辑器快捷键设置。"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SAssignNew(ShortcutSettingsEditBox, SEditableTextBox)
				.Text(FText::FromString(ShortcutToString(IsolationShortcut)))
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					FInputChord ParsedChord;
					if (TryParseShortcut(NewText.ToString(), ParsedChord))
					{
						IsolationShortcut = ParsedChord;
						SaveMagicOutlinerSettings();
					}
					if (ShortcutConflictTextBlock.IsValid())
					{
						ShortcutConflictTextBlock->SetText(FText::FromString(BuildShortcutConflictText(IsolationShortcut)));
					}
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SAssignNew(ShortcutConflictTextBlock, STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(BuildShortcutConflictText(IsolationShortcut)))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.84f, 0.34f, 1.0f)))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.Text(LOCTEXT("ResetShortcutAltQ", "恢复默认 Alt+Q"))
				.OnClicked_Lambda([this]()
				{
					IsolationShortcut = FInputChord(EKeys::Q, EModifierKey::Alt);
					SaveMagicOutlinerSettings();
					if (ShortcutSettingsEditBox.IsValid())
					{
						ShortcutSettingsEditBox->SetText(FText::FromString(ShortcutToString(IsolationShortcut)));
					}
					if (ShortcutConflictTextBlock.IsValid())
					{
						ShortcutConflictTextBlock->SetText(FText::FromString(BuildShortcutConflictText(IsolationShortcut)));
					}
					return FReply::Handled();
				})
			]
		];
}

AActor* SPBRMagicOutlinerWindow::GetDetailsActor() const
{
	return DetailsActor.Get();
}

APostProcessVolume* SPBRMagicOutlinerWindow::GetDetailsPostProcessVolume() const
{
	return Cast<APostProcessVolume>(GetDetailsActor());
}

UCameraComponent* SPBRMagicOutlinerWindow::GetDetailsCameraComponent() const
{
	if (AActor* Actor = GetDetailsActor())
	{
		return Actor->FindComponentByClass<UCameraComponent>();
	}
	return nullptr;
}

FPostProcessSettings* SPBRMagicOutlinerWindow::GetDetailsPostProcessSettings() const
{
	if (APostProcessVolume* Volume = GetDetailsPostProcessVolume())
	{
		return &Volume->Settings;
	}
	if (UCameraComponent* CameraComponent = GetDetailsCameraComponent())
	{
		return &CameraComponent->PostProcessSettings;
	}
	return nullptr;
}

void SPBRMagicOutlinerWindow::NotifyPostProcessSettingsChanged()
{
	if (APostProcessVolume* Volume = GetDetailsPostProcessVolume())
	{
		Volume->Modify();
		Volume->PostEditChange();
		Volume->MarkPackageDirty();
		StatusMessage = FString::Printf(TEXT("已更新后期体积盒子: %s"), *ActorLabel(Volume));
	}
	else if (UCameraComponent* CameraComponent = GetDetailsCameraComponent())
	{
		CameraComponent->Modify();
		CameraComponent->PostEditChange();
		if (AActor* Owner = CameraComponent->GetOwner())
		{
			Owner->Modify();
			Owner->MarkPackageDirty();
			StatusMessage = FString::Printf(TEXT("已更新相机后期参数: %s"), *ActorLabel(Owner));
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
}

void SPBRMagicOutlinerWindow::ApplySuggestedPostProcessSettings()
{
	FPostProcessSettings* Settings = GetDetailsPostProcessSettings();
	if (!Settings)
	{
		StatusMessage = TEXT("请先选择相机或后期体积盒子");
		return;
	}

	if (APostProcessVolume* Volume = GetDetailsPostProcessVolume())
	{
		Volume->Modify();
		Volume->bUnbound = true;
	}

	Settings->bOverride_AutoExposureMethod = true;
	Settings->AutoExposureMethod = AEM_Manual;
	Settings->bOverride_AutoExposureMinBrightness = false;
	Settings->AutoExposureMinBrightness = FPostProcessSettings().AutoExposureMinBrightness;
	Settings->bOverride_AutoExposureMaxBrightness = false;
	Settings->AutoExposureMaxBrightness = FPostProcessSettings().AutoExposureMaxBrightness;
	Settings->bOverride_AutoExposureBias = true;
	Settings->AutoExposureBias = 0.5f;

	Settings->bOverride_BloomIntensity = true;
	Settings->BloomIntensity = 0.25f;
	Settings->bOverride_BloomThreshold = true;
	Settings->BloomThreshold = 1.5f;

	Settings->bOverride_WhiteTemp = true;
	Settings->WhiteTemp = 6000.0f;
	Settings->bOverride_ColorContrast = true;
	Settings->ColorContrast = FVector4(1.1f, 1.1f, 1.1f, 1.0f);
	Settings->bOverride_ColorSaturation = true;
	Settings->ColorSaturation = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	Settings->bOverride_ColorGamma = true;
	Settings->ColorGamma = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	Settings->bOverride_ColorGain = true;
	Settings->ColorGain = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	Settings->bOverride_AmbientOcclusionIntensity = true;
	Settings->AmbientOcclusionIntensity = 0.5f;
	Settings->bOverride_AmbientOcclusionRadius = true;
	Settings->AmbientOcclusionRadius = 100.0f;

	Settings->bOverride_VignetteIntensity = true;
	Settings->VignetteIntensity = 0.15f;
	Settings->bOverride_SceneFringeIntensity = true;
	Settings->SceneFringeIntensity = 0.0f;
	Settings->bOverride_FilmGrainIntensity = true;
	Settings->FilmGrainIntensity = 0.0f;

	Settings->bOverride_DynamicGlobalIlluminationMethod = true;
	Settings->DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	Settings->bOverride_ReflectionMethod = true;
	Settings->ReflectionMethod = EReflectionMethod::Lumen;

	NotifyPostProcessSettingsChanged();
	StatusMessage = TEXT("已应用建议后期参数");
}

FReply SPBRMagicOutlinerWindow::OnRefreshClicked()
{
	RebuildItems();
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnSelectCheckedClicked()
{
	TArray<AActor*> Actors;
	for (const TWeakObjectPtr<AActor>& ActorPtr : GetActiveCheckedActors())
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			Actors.Add(Actor);
		}
	}
	SelectActors(Actors, false);
	StatusMessage = FString::Printf(TEXT("已在场景中选中 %d 个勾选对象"), Actors.Num());
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnClearCheckedClicked()
{
	GetActiveCheckedActors().Reset();
	PaintSelectedCheckedActors.Reset();
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
	{
		SetItemChecked(Root, false);
	}
	UpdateCachedCheckedCount();
	RefreshNameCheckList();
	RefreshCheckedList();
	RefreshLightAdjustmentBaselines();
	if (bAutoSelectCheckedActors)
	{
		SyncAutoSelectCheckedActors();
	}
	StatusMessage = TEXT("已清空勾选");
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnToggleSelectedCheckedClicked()
{
	TArray<AActor*> SelectedActorArray;
	GetEditorSelectedActors(SelectedActorArray);
	TSet<TWeakObjectPtr<AActor>> SelectedActors;
	for (AActor* Actor : SelectedActorArray)
	{
		if (Actor)
		{
			SelectedActors.Add(Actor);
		}
	}

	int32 AddedCount = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : SelectedActors)
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			const bool bWasChecked = GetActiveCheckedActors().Contains(Actor);
			SetActorChecked(Actor, true);
			if (!bWasChecked)
			{
				++AddedCount;
			}
		}
	}
	UpdateCachedCheckedCount();
	RefreshNameCheckList();
	RefreshCheckedList();
	RefreshLightAdjustmentBaselines();
	if (bAutoSelectCheckedActors)
	{
		SyncAutoSelectCheckedActors();
	}
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
	StatusMessage = FString::Printf(TEXT("已添加当前场景选择中的 %d 个对象，当前共勾选 %d 个"), AddedCount, CachedCheckedCount);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnToggleIsolationClicked()
{
	return bIsolationActive ? OnExitIsolationClicked() : OnIsolateSelectionClicked();
}

FReply SPBRMagicOutlinerWindow::OnIsolateSelectionClicked()
{
	TArray<AActor*> SelectedActors;
	GetEditorSelectedActors(SelectedActors);
	if (SelectedActors.IsEmpty())
	{
		StatusMessage = TEXT("请先在场景中选择要孤立显示的对象");
		return FReply::Handled();
	}

	TSet<TWeakObjectPtr<AActor>> SelectedSet;
	for (AActor* Actor : SelectedActors)
	{
		if (Actor)
		{
			SelectedSet.Add(Actor);
		}
	}

	TArray<AActor*> SceneActors;
	GatherActors(SceneActors);
	IsolateHiddenStates.Reset();

	int32 HiddenCount = 0;
	int32 ProtectedCount = 0;
	for (AActor* Actor : SceneActors)
	{
		if (!Actor)
		{
			continue;
		}
		IsolateHiddenStates.Add(Actor, Actor->IsTemporarilyHiddenInEditor());
		if (IsIsolationProtectedActor(Actor))
		{
			if (Actor->IsTemporarilyHiddenInEditor())
			{
				Actor->SetIsTemporarilyHiddenInEditor(false);
			}
			++ProtectedCount;
			continue;
		}
		const bool bShouldHide = !SelectedSet.Contains(Actor);
		if (Actor->IsTemporarilyHiddenInEditor() != bShouldHide)
		{
			Actor->SetIsTemporarilyHiddenInEditor(bShouldHide);
			++HiddenCount;
		}
	}

	bIsolationActive = true;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	StatusMessage = FString::Printf(TEXT("已孤立当前选择 %d 个对象，临时隐藏其它对象 %d 个，保护 SunSky %d 个"), SelectedActors.Num(), HiddenCount, ProtectedCount);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnExitIsolationClicked()
{
	if (!bIsolationActive && IsolateHiddenStates.IsEmpty())
	{
		StatusMessage = TEXT("当前没有正在孤立的选择");
		return FReply::Handled();
	}

	int32 RestoredCount = 0;
	for (auto It = IsolateHiddenStates.CreateIterator(); It; ++It)
	{
		AActor* Actor = It->Key.Get();
		if (!Actor)
		{
			continue;
		}
		const bool bWasHidden = It->Value;
		if (Actor->IsTemporarilyHiddenInEditor() != bWasHidden)
		{
			Actor->SetIsTemporarilyHiddenInEditor(bWasHidden);
			++RestoredCount;
		}
	}

	IsolateHiddenStates.Reset();
	bIsolationActive = false;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	StatusMessage = FString::Printf(TEXT("已退出孤立，恢复 %d 个对象的显示状态"), RestoredCount);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnToggleCheckedVisibilityClicked()
{
	TArray<AActor*> Actors;
	for (const TWeakObjectPtr<AActor>& ActorPtr : GetActiveCheckedActors())
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			Actors.Add(Actor);
		}
	}

	if (Actors.IsEmpty())
	{
		StatusMessage = TEXT("当前没有已勾选对象可隐藏或显示");
		return FReply::Handled();
	}

	bool bAnyVisible = false;
	for (AActor* Actor : Actors)
	{
		if (Actor && !Actor->IsTemporarilyHiddenInEditor())
		{
			bAnyVisible = true;
			break;
		}
	}

	const bool bHide = bAnyVisible;
	bCheckedActorsHidden = bHide;
	int32 ChangedCount = 0;
	for (AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}
		if (Actor->IsTemporarilyHiddenInEditor() != bHide)
		{
			Actor->SetIsTemporarilyHiddenInEditor(bHide);
			++ChangedCount;
		}
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	StatusMessage = FString::Printf(TEXT("已%s %d 个勾选对象，变化 %d 个"),
		bHide ? TEXT("隐藏") : TEXT("显示"),
		Actors.Num(),
		ChangedCount);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnShortcutSettingsClicked()
{
	TSharedRef<SWindow> SettingsWindow = SNew(SWindow)
		.Title(LOCTEXT("MagicOutlinerShortcutWindowTitle", "魔法大纲快捷键"))
		.ClientSize(FVector2D(430.0f, 210.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	SettingsWindow->SetContent(BuildShortcutSettingsContent());
	FSlateApplication::Get().AddWindow(SettingsWindow);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnToggleCompactModeClicked()
{
	bCompactMode = !bCompactMode;
	SaveMagicOutlinerSettings();
	StatusMessage = bCompactMode ? TEXT("魔法大纲已切换到精简模式") : TEXT("魔法大纲已切换到标准模式");
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnThemeSelected(FName ThemeId)
{
	ActiveThemeId = ThemeId;
	SaveMagicOutlinerSettings();
	StatusMessage = FString::Printf(TEXT("魔法大纲主题已切换为 %s"), *GetThemeButtonText().ToString());
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnToggleClassicSkinClicked()
{
	bUseClassicSkin = !bUseClassicSkin;
	SaveMagicOutlinerSettings();
	StatusMessage = bUseClassicSkin ? TEXT("已切换到经典 UE Slate 皮肤") : TEXT("已切换到自绘皮肤");
	RebuildRootContent();
	RebuildItems();
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnInvertCheckedClicked()
{
	int32 VisibleActorCount = 0;
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Root : RootItems)
	{
		if (!Root.IsValid())
		{
			continue;
		}
		for (const TSharedPtr<FPBRMagicOutlinerItem>& Child : Root->Children)
		{
			if (Child.IsValid() && Child->Actor.IsValid())
			{
				SetActorChecked(Child->Actor.Get(), !GetActiveCheckedActors().Contains(Child->Actor));
				++VisibleActorCount;
			}
		}
	}
	UpdateCachedCheckedCount();
	RefreshNameCheckList();
	RefreshCheckedList();
	RefreshLightAdjustmentBaselines();
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
	StatusMessage = FString::Printf(TEXT("已对当前显示的 %d 个对象反向勾选，当前共勾选 %d 个"), VisibleActorCount, CachedCheckedCount);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnSelectCheckedListClicked()
{
	TArray<AActor*> Actors;
	for (auto It = PaintSelectedCheckedActors.CreateIterator(); It; ++It)
	{
		if (AActor* Actor = It->Get())
		{
			Actors.Add(Actor);
		}
		else
		{
			It.RemoveCurrent();
		}
	}
	if (CheckedListView.IsValid())
	{
		TArray<TSharedPtr<FPBRCheckedActorListItem>> SelectedItems = CheckedListView->GetSelectedItems();
		for (const TSharedPtr<FPBRCheckedActorListItem>& Item : SelectedItems)
		{
			if (Item.IsValid() && Item->Actor.IsValid())
			{
				Actors.Add(Item->Actor.Get());
			}
		}
	}
	SelectActors(Actors, false);
	StatusMessage = FString::Printf(TEXT("已在场景中选中 %d 个列表对象"), Actors.Num());
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnRemoveCheckedListSelectionClicked()
{
	int32 RemovedCount = 0;
	for (auto It = PaintSelectedCheckedActors.CreateIterator(); It; ++It)
	{
		if (AActor* Actor = It->Get())
		{
			if (GetActiveCheckedActors().Contains(Actor))
			{
				SetActorChecked(Actor, false);
				++RemovedCount;
			}
		}
		It.RemoveCurrent();
	}
	if (CheckedListView.IsValid())
	{
		TArray<TSharedPtr<FPBRCheckedActorListItem>> SelectedItems = CheckedListView->GetSelectedItems();
		for (const TSharedPtr<FPBRCheckedActorListItem>& Item : SelectedItems)
		{
			if (Item.IsValid() && Item->Actor.IsValid() && GetActiveCheckedActors().Contains(Item->Actor))
			{
				SetActorChecked(Item->Actor.Get(), false);
				++RemovedCount;
			}
		}
		CheckedListView->ClearSelection();
	}
	UpdateCachedCheckedCount();
	RefreshNameCheckList();
	RefreshCheckedList();
	RefreshLightAdjustmentBaselines();
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
	StatusMessage = FString::Printf(TEXT("已从勾选列表移除 %d 个对象，当前共勾选 %d 个"), RemovedCount, CachedCheckedCount);
	return FReply::Handled();
}

FReply SPBRMagicOutlinerWindow::OnLightColorBlockClicked()
{
	FColorPickerArgs PickerArgs;
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnOk = false;
	PickerArgs.InitialColor = LightColor;
	PickerArgs.ParentWidget = AsShared();
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SPBRMagicOutlinerWindow::OnLightColorCommitted);
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

void SPBRMagicOutlinerWindow::OnLightColorCommitted(FLinearColor NewColor)
{
	LightColor = FLinearColor(
		FMath::Clamp(NewColor.R, 0.0f, 1.0f),
		FMath::Clamp(NewColor.G, 0.0f, 1.0f),
		FMath::Clamp(NewColor.B, 0.0f, 1.0f),
		1.0f);
	LightColor.A = 1.0f;
	ApplyLightAdjustmentsRealtime();
}

void SPBRMagicOutlinerWindow::ApplyLightAdjustmentsRealtime()
{
	RefreshLightAdjustmentBaselines();
	TArray<ULightComponent*> LightComponents;
	CollectCheckedLightComponents(LightComponents);
	for (ULightComponent* LightComponent : LightComponents)
	{
		if (!LightComponent)
		{
			continue;
		}
		const TWeakObjectPtr<ULightComponent> Key(LightComponent);
		const float BaseIntensity = LightBaseIntensities.Contains(Key) ? LightBaseIntensities[Key] : LightComponent->Intensity;
		LightComponent->Modify();
		LightComponent->SetIntensity(FMath::Max(0.0f, BaseIntensity * LightIntensityMultiplier));
		LightComponent->SetUseTemperature(true);
		LightComponent->SetTemperature(FMath::Clamp(LightTemperature, 1700.0f, 12000.0f));
		LightComponent->SetLightColor(LightColor);
		LightComponent->RecreateRenderState_Concurrent();
		LightComponent->PostEditChange();
	}
	StatusMessage = FString::Printf(TEXT("已实时调节 %d 个勾选灯光组件"), LightComponents.Num());
}

FText SPBRMagicOutlinerWindow::GetStatusText() const
{
	return FText::FromString(StatusMessage);
}

FText SPBRMagicOutlinerWindow::GetScenePanelTitleText() const
{
	switch (ActiveCategory)
	{
	case EPBRMagicOutlinerCategory::Models:
		return PBRText(TEXT("MagicScenePanelModels"), TEXT("场景模型"), TEXT("Scene Models"));
	case EPBRMagicOutlinerCategory::Materials:
		return PBRText(TEXT("MagicScenePanelMaterials"), TEXT("场景材质"), TEXT("Scene Materials"));
	case EPBRMagicOutlinerCategory::Lights:
		return PBRText(TEXT("MagicScenePanelLights"), TEXT("场景灯光"), TEXT("Scene Lights"));
	case EPBRMagicOutlinerCategory::Cameras:
		return PBRText(TEXT("MagicScenePanelCameras"), TEXT("相机 / 后期体积"), TEXT("Cameras / Post Process"));
	case EPBRMagicOutlinerCategory::Blueprints:
		return PBRText(TEXT("MagicScenePanelBlueprints"), TEXT("场景蓝图"), TEXT("Scene Blueprints"));
	case EPBRMagicOutlinerCategory::Levels:
		return PBRText(TEXT("MagicScenePanelLevels"), TEXT("关卡对象"), TEXT("Level Objects"));
	case EPBRMagicOutlinerCategory::All:
	default:
		return PBRText(TEXT("MagicScenePanelAll"), TEXT("场景大纲"), TEXT("Scene Outliner"));
	}
}

FText SPBRMagicOutlinerWindow::GetScenePanelSummaryText() const
{
	return FText::FromString(FString::Printf(TEXT("%d Actors · %d Rows · %d Checked"), CachedActorCount, RootItems.Num(), CachedCheckedCount));
}

FSlateColor SPBRMagicOutlinerWindow::GetCategoryColor(EPBRMagicOutlinerCategory Category) const
{
	return ActiveCategory == Category
		? FSlateColor(GetThemeColor(TEXT("SelectionText")))
		: FSlateColor(GetThemeColor(TEXT("Primary")));
}

EVisibility SPBRMagicOutlinerWindow::GetLightAdjustVisibility() const
{
	return ActiveCategory == EPBRMagicOutlinerCategory::Lights ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SPBRMagicOutlinerWindow::GetCameraPostProcessVisibility() const
{
	return ActiveCategory == EPBRMagicOutlinerCategory::Cameras ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SPBRMagicOutlinerWindow::GetCheckedActionsVisibility() const
{
	return ActiveCategory == EPBRMagicOutlinerCategory::Materials ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SPBRMagicOutlinerWindow::GetSelectedMaterialVisibility() const
{
	return ActiveCategory == EPBRMagicOutlinerCategory::Materials ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SPBRMagicOutlinerWindow::GetEditableMaterialPanelVisibility() const
{
	return ActiveCategory == EPBRMagicOutlinerCategory::Materials && GetEditableMaterialInstance()
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SPBRMagicOutlinerWindow::GetStandardControlsVisibility() const
{
	return bCompactMode ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SPBRMagicOutlinerWindow::GetDetailsPanelVisibility() const
{
	return bCompactMode ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SPBRMagicOutlinerWindow::GetCheckedSummaryText() const
{
	return FText::FromString(FString::Printf(TEXT("当前已勾选 %d 个对象。右侧参数只作用于勾选对象；切换分类或维度不会清空勾选。"), CachedCheckedCount));
}

FText SPBRMagicOutlinerWindow::GetSelectedMaterialSummaryText() const
{
	if (SelectedMaterialItems.IsEmpty())
	{
		return PBRText(TEXT("SelectMaterialUsageEmpty"), TEXT("选择场景物体后，这里会显示它们使用的所有材质。"), TEXT("Select scene objects to show all materials they use."));
	}

	int32 TotalSlots = 0;
	for (const TSharedPtr<FPBRMagicOutlinerItem>& Item : SelectedMaterialItems)
	{
		if (Item.IsValid())
		{
			TotalSlots += Item->MaterialSlots.Num();
		}
	}
	return FText::FromString(FString::Printf(TEXT("当前选择涉及 %d 个材质，合计 %d 个场景材质槽。"), SelectedMaterialItems.Num(), TotalSlots));
}

FText SPBRMagicOutlinerWindow::GetCompactModeText() const
{
	return bCompactMode
		? PBRText(TEXT("MagicOutlinerStandardMode"), TEXT("标准模式"), TEXT("Standard Mode"))
		: PBRText(TEXT("MagicOutlinerCompactMode"), TEXT("精简模式"), TEXT("Compact Mode"));
}

FText SPBRMagicOutlinerWindow::GetThemeButtonText() const
{
	return FindMagicTheme(ActiveThemeId).Label;
}

FLinearColor SPBRMagicOutlinerWindow::GetThemeColor(FName ColorName) const
{
	const FPBRMagicTheme& Theme = FindMagicTheme(ActiveThemeId);
	if (ColorName == TEXT("Background")) { return Theme.Background; }
	if (ColorName == TEXT("Panel")) { return Theme.Panel; }
	if (ColorName == TEXT("PanelRaised")) { return Theme.PanelRaised; }
	if (ColorName == TEXT("TableRow")) { return Theme.TableRow; }
	if (ColorName == TEXT("TableRowHover")) { return Theme.TableRowHover; }
	if (ColorName == TEXT("Selection")) { return Theme.Selection; }
	if (ColorName == TEXT("SelectionText")) { return Theme.SelectionText; }
	if (ColorName == TEXT("Primary")) { return Theme.Primary; }
	if (ColorName == TEXT("PrimarySoft")) { return Theme.PrimarySoft; }
	if (ColorName == TEXT("Success")) { return Theme.Success; }
	if (ColorName == TEXT("Warning")) { return Theme.Warning; }
	if (ColorName == TEXT("Danger")) { return Theme.Danger; }
	if (ColorName == TEXT("Text")) { return Theme.Text; }
	if (ColorName == TEXT("TextMuted")) { return Theme.TextMuted; }
	if (ColorName == TEXT("Border")) { return Theme.Border; }
	if (ColorName == TEXT("DropZone")) { return Theme.DropZone; }
	if (ColorName == TEXT("ThumbnailBorder")) { return Theme.ThumbnailBorder; }
	return Theme.Text;
}

#undef LOCTEXT_NAMESPACE
