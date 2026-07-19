#include "Services/PBRSubstrateMaterialTemplateManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionRotator.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "RenderUtils.h"
#include "UObject/Package.h"

namespace PBRSubstrateTemplate
{
	struct FTemplateInfo
	{
		EPBRSubstrateTemplateType Type;
		const TCHAR* DisplayName;
		const TCHAR* AssetName;
		const TCHAR* ExampleName;
		const TCHAR* Description;
	};

	struct FSurfaceOutputs
	{
		UMaterialExpression* BaseColor = nullptr;
		UMaterialExpression* Roughness = nullptr;
		UMaterialExpression* Metallic = nullptr;
		UMaterialExpression* Specular = nullptr;
		UMaterialExpression* Normal = nullptr;
		UMaterialExpression* AmbientOcclusion = nullptr;
		UMaterialExpressionTextureSampleParameter2D* BaseTexture = nullptr;
		UMaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0* MetalnessToF0 = nullptr;
	};

	struct FSurfaceDefaults
	{
		FLinearColor BaseTint = FLinearColor::White;
		float BaseIntensity = 1.0f;
		float Roughness = 0.55f;
		float Metallic = 0.0f;
		float Specular = 0.5f;
		float NormalStrength = 1.0f;
		float AO = 1.0f;
		bool bUseBaseTexture = true;
		bool bUseRoughnessTexture = true;
		bool bUseMetallicTexture = false;
		bool bUseAOTexture = true;
		bool bUseSpecularTexture = false;
		bool bUseNormalTexture = true;
	};

	static const TCHAR* TemplateRoot = TEXT("/Game/PBRStudio/Substrate/Templates/");
	static const TCHAR* ExampleRoot = TEXT("/Game/PBRStudio/Substrate/Examples/");
	static const TCHAR* FunctionRoot = TEXT("/Game/PBRStudio/Substrate/Functions/");

	static const FName GroupBase(TEXT("01 基础色"));
	static const FName GroupSurface(TEXT("02 表面参数"));
	static const FName GroupNormal(TEXT("03 法线"));
	static const FName GroupTransparency(TEXT("04 透明/透射"));
	static const FName GroupCoat(TEXT("05 清漆"));
	static const FName GroupFuzz(TEXT("05 绒毛/次表面"));
	static const FName GroupEmission(TEXT("05 自发光"));
	static const FName GroupUV(TEXT("07 UV 调整"));
	static const FName GroupOutput(TEXT("06 Substrate输出"));

	static const FName UseBaseColorTexture(TEXT("使用基础色贴图"));
	static const FName BaseColorTexture(TEXT("基础色贴图"));
	static const FName BaseColorTint(TEXT("基础色调色"));
	static const FName BaseColorIntensity(TEXT("基础色强度"));
	static const FName UseNormalTexture(TEXT("使用法线贴图"));
	static const FName NormalTexture(TEXT("法线贴图"));
	static const FName NormalStrength(TEXT("法线强度"));
	static const FName UseRoughnessTexture(TEXT("使用粗糙度贴图"));
	static const FName RoughnessTexture(TEXT("粗糙度贴图"));
	static const FName RoughnessValue(TEXT("粗糙度数值"));
	static const FName RoughnessMultiplier(TEXT("粗糙度倍率"));
	static const FName UseMetallicTexture(TEXT("使用金属度贴图"));
	static const FName MetallicTexture(TEXT("金属度贴图"));
	static const FName MetallicValue(TEXT("金属度数值"));
	static const FName MetallicMultiplier(TEXT("金属度倍率"));
	static const FName UseAOTexture(TEXT("使用环境遮蔽贴图"));
	static const FName AOTexture(TEXT("环境遮蔽贴图"));
	static const FName AOValue(TEXT("环境遮蔽强度"));
	static const FName AOMultiplier(TEXT("环境遮蔽倍率"));
	static const FName UseSpecularTexture(TEXT("使用高光贴图"));
	static const FName SpecularTexture(TEXT("高光贴图"));
	static const FName SpecularValue(TEXT("高光强度"));
	static const FName UseOpacityTexture(TEXT("使用透明贴图"));
	static const FName OpacityTexture(TEXT("透明贴图"));
	static const FName OpacityValue(TEXT("透明度"));
	static const FName TransmissionColor(TEXT("透射颜色"));
	static const FName ThicknessCm(TEXT("厚度厘米"));
	static const FName MaskTexture(TEXT("遮罩贴图"));
	static const FName UseMaskTexture(TEXT("使用遮罩贴图"));
	static const FName ClearCoatCoverage(TEXT("清漆覆盖"));
	static const FName ClearCoatRoughness(TEXT("清漆粗糙度"));
	static const FName FuzzAmount(TEXT("绒毛强度"));
	static const FName FuzzColor(TEXT("绒毛颜色"));
	static const FName FuzzRoughness(TEXT("绒毛粗糙度"));
	static const FName SubsurfaceColor(TEXT("透光颜色"));
	static const FName UseEmissiveTexture(TEXT("使用自发光贴图"));
	static const FName EmissiveTexture(TEXT("自发光贴图"));
	static const FName EmissiveColor(TEXT("自发光颜色"));
	static const FName EmissiveIntensity(TEXT("自发光强度"));
	static const FName UVUTiling(TEXT("U 平铺"));
	static const FName UVVTiling(TEXT("V 平铺"));
	static const FName UVUOffset(TEXT("U 偏移"));
	static const FName UVVOffset(TEXT("V 偏移"));
	static const FName UVRotationDegrees(TEXT("旋转角度"));

	static const TArray<FTemplateInfo>& GetTemplateInfos()
	{
		static const TArray<FTemplateInfo> Infos = {
			{ EPBRSubstrateTemplateType::Standard, TEXT("标准 Slab"), TEXT("M_PBR_Substrate_Standard"), TEXT("MI_示例_Substrate_标准Slab"), TEXT("标准 PBR 表面，保持 Substrate Slab BSDF - Simple。") },
			{ EPBRSubstrateTemplateType::Wood, TEXT("木材"), TEXT("M_PBR_Substrate_Wood"), TEXT("MI_示例_Substrate_木材"), TEXT("木饰面和地板，偏暖色、低金属度、适中粗糙度。") },
			{ EPBRSubstrateTemplateType::Stone, TEXT("石材"), TEXT("M_PBR_Substrate_Stone"), TEXT("MI_示例_Substrate_石材"), TEXT("石材和混凝土，偏高粗糙度并启用 AO。") },
			{ EPBRSubstrateTemplateType::Tile, TEXT("瓷砖"), TEXT("M_PBR_Substrate_Tile"), TEXT("MI_示例_Substrate_瓷砖"), TEXT("瓷砖和铺装，适中高光、可调 UV 平铺。") },
			{ EPBRSubstrateTemplateType::Leather, TEXT("皮革"), TEXT("M_PBR_Substrate_Leather"), TEXT("MI_示例_Substrate_皮革"), TEXT("皮革材质，偏深色、细法线和柔和高光。") },
			{ EPBRSubstrateTemplateType::Plastic, TEXT("塑料"), TEXT("M_PBR_Substrate_Plastic"), TEXT("MI_示例_Substrate_塑料"), TEXT("塑料与涂层表面，低金属度、可调高光。") },
			{ EPBRSubstrateTemplateType::Metal, TEXT("金属"), TEXT("M_PBR_Substrate_Metal"), TEXT("MI_示例_Substrate_金属"), TEXT("金属材质，默认金属度较高，粗糙度较低。") },
			{ EPBRSubstrateTemplateType::Transparent, TEXT("透明"), TEXT("M_PBR_Substrate_Transparent"), TEXT("MI_示例_Substrate_透明"), TEXT("普通透明材质，保留基础贴图和透明控制。") },
			{ EPBRSubstrateTemplateType::Water, TEXT("水"), TEXT("M_PBR_Substrate_Water"), TEXT("MI_示例_Substrate_水"), TEXT("水面材质，低粗糙度、蓝绿色透射。") },
			{ EPBRSubstrateTemplateType::Glass, TEXT("玻璃"), TEXT("M_PBR_Substrate_Glass"), TEXT("MI_示例_Substrate_玻璃"), TEXT("透明玻璃，使用 Slab + Simple Volume + 彩色透射。") },
			{ EPBRSubstrateTemplateType::CarPaint, TEXT("车漆/清漆"), TEXT("M_PBR_Substrate_CarPaint"), TEXT("MI_示例_Substrate_车漆清漆"), TEXT("车漆和高光清漆，使用 Substrate Simple Clear Coat。") },
			{ EPBRSubstrateTemplateType::Leaf, TEXT("树叶"), TEXT("M_PBR_Substrate_Leaf"), TEXT("MI_示例_Substrate_树叶"), TEXT("双面遮罩树叶，保留遮罩贴图和透光颜色。") },
			{ EPBRSubstrateTemplateType::Fabric, TEXT("布料"), TEXT("M_PBR_Substrate_Fabric"), TEXT("MI_示例_Substrate_布料"), TEXT("带绒毛层的布料材质，方便调绒毛颜色和强度。") },
			{ EPBRSubstrateTemplateType::Emissive, TEXT("自发光"), TEXT("M_PBR_Substrate_Emissive"), TEXT("MI_示例_Substrate_自发光"), TEXT("灯带、屏幕和发光图案，使用 Substrate Unlit BSDF。") }
		};
		return Infos;
	}

	static const FTemplateInfo& GetInfo(EPBRSubstrateTemplateType TemplateType)
	{
		for (const FTemplateInfo& Info : GetTemplateInfos())
		{
			if (Info.Type == TemplateType)
			{
				return Info;
			}
		}
		return GetTemplateInfos()[0];
	}

	static UObject* LoadAssetIfExists(const FString& PackagePath)
	{
		return UEditorAssetLibrary::DoesAssetExist(PackagePath)
			? UEditorAssetLibrary::LoadAsset(PackagePath)
			: nullptr;
	}

	static UTexture2D* LoadTexture(const TCHAR* AssetPath)
	{
		return LoadObject<UTexture2D>(nullptr, AssetPath);
	}

	static UTexture2D* GetDefaultTexture(EMaterialSamplerType SamplerType)
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

	static void ResetGraph(UMaterial* Material)
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
			EditorData->SurfaceThickness.Expression = nullptr;
			EditorData->MaterialAttributes.Expression = nullptr;
			EditorData->ShadingModelFromMaterialExpression.Expression = nullptr;
			EditorData->FrontMaterial.Expression = nullptr;
			EditorData->FrontMaterial.OutputIndex = 0;
			for (FVector2MaterialInput& CustomizedUV : EditorData->CustomizedUVs)
			{
				CustomizedUV.Expression = nullptr;
			}
			EditorData->ParameterGroupData.Empty();
		}

		Material->GetExpressionCollection().Empty();
	}

	static void ResetMaterialSettings(UMaterial* Material)
	{
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
		Material->TwoSided = false;
		Material->bIsThinSurface = false;
		Material->bScreenSpaceReflections = false;
		Material->TranslucencyLightingMode = TLM_Surface;
		Material->RefractionMethod = RM_None;
		Material->RefractionCoverageMode = RCM_CoverageIgnored;
		Material->OpacityMaskClipValue = 0.3333f;
	}

	static void AddGroup(UMaterial* Material, const FName& GroupName, int32 SortPriority)
	{
		if (UMaterialEditorOnlyData* EditorData = Material ? Material->GetEditorOnlyData() : nullptr)
		{
			EditorData->ParameterGroupData.Add(FParameterGroupData(GroupName.ToString(), SortPriority));
		}
	}

	static void AddCommonGroups(UMaterial* Material)
	{
		AddGroup(Material, GroupBase, 0);
		AddGroup(Material, GroupSurface, 1);
		AddGroup(Material, GroupNormal, 2);
		AddGroup(Material, GroupUV, 7);
		AddGroup(Material, GroupOutput, 9);
	}

	static void AddComment(UMaterial* Material, const FString& Text, int32 X, int32 Y, int32 Width, int32 Height)
	{
		UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Material);
		Comment->Text = Text;
		Comment->MaterialExpressionEditorX = X;
		Comment->MaterialExpressionEditorY = Y;
		Comment->SizeX = Width;
		Comment->SizeY = Height;
		Material->GetExpressionCollection().AddComment(Comment);
	}

	static UMaterialFunction* CreateOrResetFunction(const FString& AssetName, const FString& Description, const FString& Category)
	{
		const FString PackagePath = FString(FunctionRoot) + AssetName;
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			return nullptr;
		}

		UMaterialFunction* Function = Cast<UMaterialFunction>(LoadAssetIfExists(PackagePath));
		if (!Function)
		{
			Function = NewObject<UMaterialFunction>(Package, FName(*AssetName), RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Function);
		}

		Function->Description = Description;
		Function->UserExposedCaption = AssetName;
		Function->bExposeToLibrary = true;
		Function->LibraryCategoriesText.Empty();
		Function->LibraryCategoriesText.Add(FText::FromString(TEXT("PBRStudio/Substrate/") + Category));
		Function->GetExpressionCollection().Empty();
		Function->PreEditChange(nullptr);
		return Function;
	}

	static void SaveFunction(UMaterialFunction* Function)
	{
		if (!Function)
		{
			return;
		}

		Function->UpdateInputOutputTypes();
		Function->PostEditChange();
		Function->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({ Function->GetPackage() }, true);
	}

	static void AddComment(UMaterialFunction* Function, const FString& Text, int32 X, int32 Y, int32 Width, int32 Height)
	{
		UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Function);
		Comment->Text = Text;
		Comment->MaterialExpressionEditorX = X;
		Comment->MaterialExpressionEditorY = Y;
		Comment->SizeX = Width;
		Comment->SizeY = Height;
		Comment->FontSize = 22;
		Comment->CommentColor = FLinearColor(0.10f, 0.13f, 0.16f, 1.0f);
		Comment->bGroupMode = true;
		Function->GetExpressionCollection().AddExpression(Comment);
	}

	static UMaterialExpressionFunctionInput* AddInput(
		UMaterialFunction* Function,
		const FName& Name,
		EFunctionInputType Type,
		const FVector4f& Preview,
		int32 SortPriority,
		int32 X,
		int32 Y)
	{
		UMaterialExpressionFunctionInput* Input = NewObject<UMaterialExpressionFunctionInput>(Function);
		Input->InputName = Name;
		Input->InputType = Type;
		Input->PreviewValue = Preview;
		Input->bUsePreviewValueAsDefault = true;
		Input->SortPriority = SortPriority;
		Input->MaterialExpressionEditorX = X;
		Input->MaterialExpressionEditorY = Y;
		Input->ConditionallyGenerateId(true);
		Function->GetExpressionCollection().AddExpression(Input);
		return Input;
	}

	static UMaterialExpressionFunctionOutput* AddOutput(
		UMaterialFunction* Function,
		const FName& Name,
		UMaterialExpression* Expression,
		int32 SortPriority,
		int32 X,
		int32 Y)
	{
		UMaterialExpressionFunctionOutput* Output = NewObject<UMaterialExpressionFunctionOutput>(Function);
		Output->OutputName = Name;
		Output->SortPriority = SortPriority;
		Output->A.Connect(0, Expression);
		Output->MaterialExpressionEditorX = X;
		Output->MaterialExpressionEditorY = Y;
		Output->ConditionallyGenerateId(true);
		Function->GetExpressionCollection().AddExpression(Output);
		return Output;
	}

	template <typename T>
	static T* AddFunctionNode(UMaterialFunction* Function, int32 X, int32 Y)
	{
		T* Node = NewObject<T>(Function);
		Node->MaterialExpressionEditorX = X;
		Node->MaterialExpressionEditorY = Y;
		Function->GetExpressionCollection().AddExpression(Node);
		return Node;
	}

	static bool BuildSubstrateBaseColorFunction()
	{
		UMaterialFunction* Function = CreateOrResetFunction(
			TEXT("MF_PBRStudio_Substrate_BaseColor"),
			TEXT("Substrate 基础色: 贴图颜色、调色和强度。"),
			TEXT("01 基础色"));
		if (!Function)
		{
			return false;
		}

		AddComment(Function, TEXT("基础色: 贴图路径和纯色路径"), -980, -260, 1380, 560);
		UMaterialExpressionFunctionInput* TextureColor = AddInput(Function, TEXT("贴图颜色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 10, -920, -160);
		UMaterialExpressionFunctionInput* Tint = AddInput(Function, TEXT("调色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 20, -920, 0);
		UMaterialExpressionFunctionInput* Intensity = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 30, -920, 160);
		UMaterialExpressionMultiply* TintedTexture = AddFunctionNode<UMaterialExpressionMultiply>(Function, -560, -120);
		TintedTexture->A.Connect(0, TextureColor);
		TintedTexture->B.Connect(0, Tint);
		UMaterialExpressionMultiply* TexturedColor = AddFunctionNode<UMaterialExpressionMultiply>(Function, -280, -120);
		TexturedColor->A.Connect(0, TintedTexture);
		TexturedColor->B.Connect(0, Intensity);
		UMaterialExpressionMultiply* SolidColor = AddFunctionNode<UMaterialExpressionMultiply>(Function, -280, 80);
		SolidColor->A.Connect(0, Tint);
		SolidColor->B.Connect(0, Intensity);
		AddOutput(Function, TEXT("贴图颜色输出"), TexturedColor, 10, 120, -120);
		AddOutput(Function, TEXT("纯色输出"), SolidColor, 20, 120, 80);
		SaveFunction(Function);
		return true;
	}

	static bool BuildSubstrateScalarChannelFunction()
	{
		UMaterialFunction* Function = CreateOrResetFunction(
			TEXT("MF_PBRStudio_Substrate_ScalarChannel"),
			TEXT("Substrate 标量通道: 贴图通道、倍率和固定值。"),
			TEXT("02 标量通道"));
		if (!Function)
		{
			return false;
		}

		AddComment(Function, TEXT("标量通道: 贴图 * 倍率，或固定值"), -980, -240, 1320, 500);
		UMaterialExpressionFunctionInput* TextureValue = AddInput(Function, TEXT("贴图值"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 10, -920, -140);
		UMaterialExpressionFunctionInput* Multiplier = AddInput(Function, TEXT("倍率"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 20, -920, 0);
		UMaterialExpressionFunctionInput* SolidValue = AddInput(Function, TEXT("固定值"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 30, -920, 140);
		UMaterialExpressionMultiply* TexturedValue = AddFunctionNode<UMaterialExpressionMultiply>(Function, -520, -80);
		TexturedValue->A.Connect(0, TextureValue);
		TexturedValue->B.Connect(0, Multiplier);
		AddOutput(Function, TEXT("贴图输出"), TexturedValue, 10, -120, -80);
		AddOutput(Function, TEXT("固定输出"), SolidValue, 20, -120, 120);
		SaveFunction(Function);
		return true;
	}

	static bool BuildSubstrateNormalStrengthFunction()
	{
		UMaterialFunction* Function = CreateOrResetFunction(
			TEXT("MF_PBRStudio_Substrate_NormalStrength"),
			TEXT("Substrate 法线强度: 在平法线和贴图法线之间混合。"),
			TEXT("03 法线"));
		if (!Function)
		{
			return false;
		}

		AddComment(Function, TEXT("法线强度: lerp(平法线, 贴图法线, 强度)"), -980, -260, 1380, 540);
		UMaterialExpressionFunctionInput* Normal = AddInput(Function, TEXT("法线"), FunctionInput_Vector3, FVector4f(0, 0, 1.0f, 0), 10, -920, -160);
		UMaterialExpressionFunctionInput* Strength = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 20, -920, 20);
		UMaterialExpressionConstant3Vector* FlatNormal = AddFunctionNode<UMaterialExpressionConstant3Vector>(Function, -620, -170);
		FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
		UMaterialExpressionLinearInterpolate* Lerp = AddFunctionNode<UMaterialExpressionLinearInterpolate>(Function, -320, -120);
		Lerp->A.Connect(0, FlatNormal);
		Lerp->B.Connect(0, Normal);
		Lerp->Alpha.Connect(0, Strength);
		AddOutput(Function, TEXT("法线"), Lerp, 10, 80, -120);
		SaveFunction(Function);
		return true;
	}

	static bool BuildSubstrateEmissiveFunction()
	{
		UMaterialFunction* Function = CreateOrResetFunction(
			TEXT("MF_PBRStudio_Substrate_Emissive"),
			TEXT("Substrate 自发光: 贴图或纯色乘以强度。"),
			TEXT("05 自发光"));
		if (!Function)
		{
			return false;
		}

		AddComment(Function, TEXT("自发光: 贴图路径和纯色路径"), -980, -260, 1380, 560);
		UMaterialExpressionFunctionInput* TextureColor = AddInput(Function, TEXT("贴图颜色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 10, -920, -160);
		UMaterialExpressionFunctionInput* Color = AddInput(Function, TEXT("颜色"), FunctionInput_Vector3, FVector4f(1, 0.7f, 0.35f, 1), 20, -920, 0);
		UMaterialExpressionFunctionInput* Intensity = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(5, 0, 0, 0), 30, -920, 160);
		UMaterialExpressionMultiply* TintedTexture = AddFunctionNode<UMaterialExpressionMultiply>(Function, -560, -120);
		TintedTexture->A.Connect(0, TextureColor);
		TintedTexture->B.Connect(0, Color);
		UMaterialExpressionMultiply* TexturedEmission = AddFunctionNode<UMaterialExpressionMultiply>(Function, -280, -120);
		TexturedEmission->A.Connect(0, TintedTexture);
		TexturedEmission->B.Connect(0, Intensity);
		UMaterialExpressionMultiply* SolidEmission = AddFunctionNode<UMaterialExpressionMultiply>(Function, -280, 80);
		SolidEmission->A.Connect(0, Color);
		SolidEmission->B.Connect(0, Intensity);
		AddOutput(Function, TEXT("贴图自发光"), TexturedEmission, 10, 120, -120);
		AddOutput(Function, TEXT("纯色自发光"), SolidEmission, 20, 120, 80);
		SaveFunction(Function);
		return true;
	}

	static bool BuildSubstrateUVControlsFunction()
	{
		UMaterialFunction* Function = CreateOrResetFunction(
			TEXT("MF_PBRStudio_Substrate_UVControls"),
			TEXT("Substrate UV 调整: 平铺、偏移、旋转。"),
			TEXT("07 UV 调整"));
		if (!Function)
		{
			return false;
		}

		AddComment(Function, TEXT("UV 调整: TexCoord -> 平铺 -> 偏移 -> 旋转"), -1140, -360, 1700, 760);
		UMaterialExpressionFunctionInput* UTiling = AddInput(Function, TEXT("U 平铺"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 10, -1080, -260);
		UMaterialExpressionFunctionInput* VTiling = AddInput(Function, TEXT("V 平铺"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 20, -1080, -140);
		UMaterialExpressionFunctionInput* UOffset = AddInput(Function, TEXT("U 偏移"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 30, -1080, -20);
		UMaterialExpressionFunctionInput* VOffset = AddInput(Function, TEXT("V 偏移"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 40, -1080, 100);
		UMaterialExpressionFunctionInput* RotationDegrees = AddInput(Function, TEXT("旋转角度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 50, -1080, 220);

		UMaterialExpressionTextureCoordinate* TexCoord = AddFunctionNode<UMaterialExpressionTextureCoordinate>(Function, -1080, -420);
		UMaterialExpressionAppendVector* TilingUV = AddFunctionNode<UMaterialExpressionAppendVector>(Function, -760, -220);
		TilingUV->A.Connect(0, UTiling);
		TilingUV->B.Connect(0, VTiling);
		UMaterialExpressionAppendVector* OffsetUV = AddFunctionNode<UMaterialExpressionAppendVector>(Function, -760, 20);
		OffsetUV->A.Connect(0, UOffset);
		OffsetUV->B.Connect(0, VOffset);
		UMaterialExpressionMultiply* TiledUV = AddFunctionNode<UMaterialExpressionMultiply>(Function, -500, -300);
		TiledUV->A.Connect(0, TexCoord);
		TiledUV->B.Connect(0, TilingUV);
		UMaterialExpressionAdd* OffsetResult = AddFunctionNode<UMaterialExpressionAdd>(Function, -260, -300);
		OffsetResult->A.Connect(0, TiledUV);
		OffsetResult->B.Connect(0, OffsetUV);
		UMaterialExpressionMultiply* DegreesToRadians = AddFunctionNode<UMaterialExpressionMultiply>(Function, -260, -80);
		DegreesToRadians->A.Connect(0, RotationDegrees);
		DegreesToRadians->ConstB = UE_PI / 180.0f;
		UMaterialExpressionRotator* Rotator = AddFunctionNode<UMaterialExpressionRotator>(Function, 0, -300);
		Rotator->CenterX = 0.5f;
		Rotator->CenterY = 0.5f;
		Rotator->Speed = 1.0f;
		Rotator->Coordinate.Connect(0, OffsetResult);
		Rotator->Time.Connect(0, DegreesToRadians);
		AddOutput(Function, TEXT("UV"), Rotator, 10, 300, -300);
		SaveFunction(Function);
		return true;
	}

	static void EnsureSubstrateMaterialFunctions()
	{
		BuildSubstrateUVControlsFunction();
		BuildSubstrateBaseColorFunction();
		BuildSubstrateScalarChannelFunction();
		BuildSubstrateNormalStrengthFunction();
		BuildSubstrateEmissiveFunction();
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

	static UMaterialExpressionScalarParameter* AddScalar(
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

	static UMaterialExpressionVectorParameter* AddVector(
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

	static UMaterialExpressionConstant3Vector* AddConstant3(UMaterial* Material, const FLinearColor& Value, int32 X, int32 Y)
	{
		UMaterialExpressionConstant3Vector* Node = NewObject<UMaterialExpressionConstant3Vector>(Material);
		Node->Constant = Value;
		Node->MaterialExpressionEditorX = X;
		Node->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(Node);
		return Node;
	}

	static UMaterialExpressionTextureSampleParameter2D* AddTexture(
		UMaterial* Material,
		const FName& ParameterName,
		const FName& GroupName,
		int32 SortPriority,
		EMaterialSamplerType SamplerType,
		int32 X,
		int32 Y)
	{
		UMaterialExpressionTextureSampleParameter2D* Node = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
		Node->ParameterName = ParameterName;
		Node->Group = GroupName;
		Node->SortPriority = SortPriority;
		Node->SamplerType = SamplerType;
		Node->Texture = GetDefaultTexture(SamplerType);
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

	static UMaterialExpressionComponentMask* AddMask(UMaterial* Material, bool bR, bool bG, bool bB, bool bA, int32 X, int32 Y)
	{
		UMaterialExpressionComponentMask* Node = NewObject<UMaterialExpressionComponentMask>(Material);
		Node->R = bR;
		Node->G = bG;
		Node->B = bB;
		Node->A = bA;
		Node->MaterialExpressionEditorX = X;
		Node->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(Node);
		return Node;
	}

	static UMaterialExpressionLinearInterpolate* AddTextureSwitch(
		UMaterial* Material,
		const FName& ParameterName,
		const FName& GroupName,
		int32 SortPriority,
		bool bDefaultValue,
		UMaterialExpression* TrueExpression,
		UMaterialExpression* FalseExpression,
		int32 X,
		int32 Y)
	{
		UMaterialExpressionScalarParameter* Toggle = AddScalar(
			Material,
			ParameterName,
			GroupName,
			SortPriority,
			bDefaultValue ? 1.0f : 0.0f,
			X - 260,
			Y + 120);

		UMaterialExpressionLinearInterpolate* Lerp = NewObject<UMaterialExpressionLinearInterpolate>(Material);
		Lerp->MaterialExpressionEditorX = X;
		Lerp->MaterialExpressionEditorY = Y;
		Lerp->A.Connect(0, FalseExpression);
		Lerp->B.Connect(0, TrueExpression);
		Lerp->Alpha.Connect(0, Toggle);
		Material->GetExpressionCollection().AddExpression(Lerp);
		return Lerp;
	}

	static UMaterialExpressionMaterialFunctionCall* AddFunctionCall(
		UMaterial* Material,
		const TCHAR* FunctionPath,
		int32 X,
		int32 Y)
	{
		UMaterialFunctionInterface* Function = LoadObject<UMaterialFunctionInterface>(nullptr, FunctionPath);
		if (!Function)
		{
			const FString AssetPath(FunctionPath);
			const int32 DotIndex = AssetPath.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			const FString PackagePath = DotIndex == INDEX_NONE ? AssetPath : AssetPath.Left(DotIndex);
			Function = Cast<UMaterialFunctionInterface>(LoadAssetIfExists(PackagePath));
		}
		if (!Material || !Function)
		{
			return nullptr;
		}

		UMaterialExpressionMaterialFunctionCall* FunctionCall = NewObject<UMaterialExpressionMaterialFunctionCall>(Material);
		FunctionCall->SetMaterialFunction(Function);
		FunctionCall->UpdateFromFunctionResource();
		FunctionCall->MaterialExpressionEditorX = X;
		FunctionCall->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(FunctionCall);
		return FunctionCall;
	}

	static bool ConnectFunctionInput(
		UMaterialExpressionMaterialFunctionCall* FunctionCall,
		const TCHAR* InputName,
		UMaterialExpression* Expression,
		int32 OutputIndex = 0)
	{
		if (!FunctionCall || !Expression)
		{
			return false;
		}

		for (int32 InputIndex = 0; InputIndex < FunctionCall->FunctionInputs.Num(); ++InputIndex)
		{
			if (FunctionCall->GetInputName(InputIndex).ToString().Equals(InputName, ESearchCase::IgnoreCase))
			{
				FunctionCall->FunctionInputs[InputIndex].Input.Connect(OutputIndex, Expression);
				return true;
			}
		}
		return false;
	}

	static UMaterialExpression* BuildSharedUVControls(UMaterial* Material)
	{
		UMaterialExpressionScalarParameter* UTiling = AddScalar(Material, UVUTiling, GroupUV, 10, 1.0f, -1720, -760);
		UMaterialExpressionScalarParameter* VTiling = AddScalar(Material, UVVTiling, GroupUV, 20, 1.0f, -1720, -620);
		UMaterialExpressionScalarParameter* UOffset = AddScalar(Material, UVUOffset, GroupUV, 30, 0.0f, -1720, -480);
		UMaterialExpressionScalarParameter* VOffset = AddScalar(Material, UVVOffset, GroupUV, 40, 0.0f, -1720, -340);
		UMaterialExpressionScalarParameter* RotationDegrees = AddScalar(Material, UVRotationDegrees, GroupUV, 50, 0.0f, -1720, -200);

		UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
		TexCoord->MaterialExpressionEditorX = -1380;
		TexCoord->MaterialExpressionEditorY = -880;
		Material->GetExpressionCollection().AddExpression(TexCoord);

		UMaterialExpressionAppendVector* TilingUV = NewObject<UMaterialExpressionAppendVector>(Material);
		TilingUV->MaterialExpressionEditorX = -1380;
		TilingUV->MaterialExpressionEditorY = -650;
		TilingUV->A.Connect(0, UTiling);
		TilingUV->B.Connect(0, VTiling);
		Material->GetExpressionCollection().AddExpression(TilingUV);

		UMaterialExpressionAppendVector* OffsetUV = NewObject<UMaterialExpressionAppendVector>(Material);
		OffsetUV->MaterialExpressionEditorX = -1380;
		OffsetUV->MaterialExpressionEditorY = -420;
		OffsetUV->A.Connect(0, UOffset);
		OffsetUV->B.Connect(0, VOffset);
		Material->GetExpressionCollection().AddExpression(OffsetUV);

		UMaterialExpressionMultiply* TiledUV = AddMultiply(Material, -1120, -720);
		TiledUV->A.Connect(0, TexCoord);
		TiledUV->B.Connect(0, TilingUV);

		UMaterialExpressionAdd* ShiftedUV = NewObject<UMaterialExpressionAdd>(Material);
		ShiftedUV->MaterialExpressionEditorX = -900;
		ShiftedUV->MaterialExpressionEditorY = -720;
		ShiftedUV->A.Connect(0, TiledUV);
		ShiftedUV->B.Connect(0, OffsetUV);
		Material->GetExpressionCollection().AddExpression(ShiftedUV);

		UMaterialExpressionMultiply* DegreesToRadians = AddMultiply(Material, -900, -500);
		DegreesToRadians->A.Connect(0, RotationDegrees);
		DegreesToRadians->ConstB = UE_PI / 180.0f;

		UMaterialExpressionRotator* Rotator = NewObject<UMaterialExpressionRotator>(Material);
		Rotator->CenterX = 0.5f;
		Rotator->CenterY = 0.5f;
		Rotator->Speed = 1.0f;
		Rotator->Coordinate.Connect(0, ShiftedUV);
		Rotator->Time.Connect(0, DegreesToRadians);
		Rotator->MaterialExpressionEditorX = -640;
		Rotator->MaterialExpressionEditorY = -720;
		Material->GetExpressionCollection().AddExpression(Rotator);

		return Rotator;
	}

	static void ConnectTextureSamplesToUV(UMaterial* Material, UMaterialExpression* UVExpression)
	{
		if (!Material || !UVExpression)
		{
			return;
		}

		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (UMaterialExpressionTextureSampleParameter2D* TextureParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
			{
				TextureParameter->Coordinates.Connect(0, UVExpression);
			}
		}
	}

	static UMaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0* AddMetalnessToF0(
		UMaterial* Material,
		UMaterialExpression* BaseColor,
		UMaterialExpression* Metallic,
		UMaterialExpression* Specular,
		int32 X,
		int32 Y)
	{
		UMaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0* Node = NewObject<UMaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0>(Material);
		Node->MaterialExpressionEditorX = X;
		Node->MaterialExpressionEditorY = Y;
		Node->BaseColor.Connect(0, BaseColor);
		Node->Metallic.Connect(0, Metallic);
		Node->Specular.Connect(0, Specular);
		Node->Outputs.Reset();
		Node->Outputs.Add(FExpressionOutput(TEXT("DiffuseAlbedo"), 1, 1, 1, 1, 0));
		Node->Outputs.Add(FExpressionOutput(TEXT("F0"), 1, 1, 1, 1, 0));
		Material->GetExpressionCollection().AddExpression(Node);
		return Node;
	}

	static FSurfaceOutputs BuildSurfaceControls(UMaterial* Material, const FSurfaceDefaults& Defaults)
	{
		FSurfaceOutputs Result;

		Result.BaseTexture = AddTexture(Material, BaseColorTexture, GroupBase, 0, SAMPLERTYPE_Color, -1280, -520);
		UMaterialExpressionVectorParameter* BaseTint = AddVector(Material, BaseColorTint, GroupBase, 1, Defaults.BaseTint, -1280, -360);
		UMaterialExpressionScalarParameter* BaseIntensity = AddScalar(Material, BaseColorIntensity, GroupBase, 2, Defaults.BaseIntensity, -1280, -220);
		UMaterialExpressionMaterialFunctionCall* BaseColorFunction = AddFunctionCall(
			Material,
			TEXT("/Game/PBRStudio/Substrate/Functions/MF_PBRStudio_Substrate_BaseColor.MF_PBRStudio_Substrate_BaseColor"),
			-860,
			-420);
		ConnectFunctionInput(BaseColorFunction, TEXT("贴图颜色"), Result.BaseTexture);
		ConnectFunctionInput(BaseColorFunction, TEXT("调色"), BaseTint);
		ConnectFunctionInput(BaseColorFunction, TEXT("强度"), BaseIntensity);
		UMaterialExpressionMultiply* SolidBaseColor = AddMultiply(Material, -620, -240);
		SolidBaseColor->A.Connect(0, BaseTint);
		SolidBaseColor->B.Connect(0, BaseIntensity);
		Result.BaseColor = AddTextureSwitch(
			Material,
			UseBaseColorTexture,
			GroupBase,
			3,
			Defaults.bUseBaseTexture,
			BaseColorFunction ? static_cast<UMaterialExpression*>(BaseColorFunction) : static_cast<UMaterialExpression*>(SolidBaseColor),
			SolidBaseColor,
			-390,
			-370);

		UMaterialExpressionTextureSampleParameter2D* RoughnessTex = AddTexture(Material, RoughnessTexture, GroupSurface, 10, SAMPLERTYPE_Masks, -1280, -40);
		UMaterialExpressionComponentMask* RoughnessMask = AddMask(Material, true, false, false, false, -1010, -10);
		RoughnessMask->Input.Connect(0, RoughnessTex);
		UMaterialExpressionScalarParameter* RoughnessScalar = AddScalar(Material, RoughnessValue, GroupSurface, 11, Defaults.Roughness, -1280, 120);
		UMaterialExpressionScalarParameter* RoughnessScale = AddScalar(Material, RoughnessMultiplier, GroupSurface, 12, 1.0f, -1280, 250);
		UMaterialExpressionMaterialFunctionCall* RoughnessFunction = AddFunctionCall(
			Material,
			TEXT("/Game/PBRStudio/Substrate/Functions/MF_PBRStudio_Substrate_ScalarChannel.MF_PBRStudio_Substrate_ScalarChannel"),
			-760,
			-20);
		ConnectFunctionInput(RoughnessFunction, TEXT("贴图值"), RoughnessMask);
		ConnectFunctionInput(RoughnessFunction, TEXT("倍率"), RoughnessScale);
		ConnectFunctionInput(RoughnessFunction, TEXT("固定值"), RoughnessScalar);
		Result.Roughness = AddTextureSwitch(
			Material,
			UseRoughnessTexture,
			GroupSurface,
			13,
			Defaults.bUseRoughnessTexture,
			RoughnessFunction ? static_cast<UMaterialExpression*>(RoughnessFunction) : static_cast<UMaterialExpression*>(RoughnessScalar),
			RoughnessScalar,
			-390,
			0);

		UMaterialExpressionTextureSampleParameter2D* MetallicTex = AddTexture(Material, MetallicTexture, GroupSurface, 20, SAMPLERTYPE_Masks, -1280, 420);
		UMaterialExpressionComponentMask* MetallicMask = AddMask(Material, true, false, false, false, -1010, 450);
		MetallicMask->Input.Connect(0, MetallicTex);
		UMaterialExpressionScalarParameter* MetallicScalar = AddScalar(Material, MetallicValue, GroupSurface, 21, Defaults.Metallic, -1280, 580);
		UMaterialExpressionScalarParameter* MetallicScale = AddScalar(Material, MetallicMultiplier, GroupSurface, 22, 1.0f, -1280, 710);
		UMaterialExpressionMaterialFunctionCall* MetallicFunction = AddFunctionCall(
			Material,
			TEXT("/Game/PBRStudio/Substrate/Functions/MF_PBRStudio_Substrate_ScalarChannel.MF_PBRStudio_Substrate_ScalarChannel"),
			-760,
			450);
		ConnectFunctionInput(MetallicFunction, TEXT("贴图值"), MetallicMask);
		ConnectFunctionInput(MetallicFunction, TEXT("倍率"), MetallicScale);
		ConnectFunctionInput(MetallicFunction, TEXT("固定值"), MetallicScalar);
		Result.Metallic = AddTextureSwitch(
			Material,
			UseMetallicTexture,
			GroupSurface,
			23,
			Defaults.bUseMetallicTexture,
			MetallicFunction ? static_cast<UMaterialExpression*>(MetallicFunction) : static_cast<UMaterialExpression*>(MetallicScalar),
			MetallicScalar,
			-390,
			470);

		UMaterialExpressionTextureSampleParameter2D* AOTex = AddTexture(Material, AOTexture, GroupSurface, 30, SAMPLERTYPE_Masks, -1280, 860);
		UMaterialExpressionComponentMask* AOMask = AddMask(Material, true, false, false, false, -1010, 890);
		AOMask->Input.Connect(0, AOTex);
		UMaterialExpressionScalarParameter* AOScalar = AddScalar(Material, AOValue, GroupSurface, 31, Defaults.AO, -1280, 1020);
		UMaterialExpressionScalarParameter* AOScale = AddScalar(Material, AOMultiplier, GroupSurface, 32, 1.0f, -1280, 1150);
		UMaterialExpressionMaterialFunctionCall* AOFunction = AddFunctionCall(
			Material,
			TEXT("/Game/PBRStudio/Substrate/Functions/MF_PBRStudio_Substrate_ScalarChannel.MF_PBRStudio_Substrate_ScalarChannel"),
			-760,
			880);
		ConnectFunctionInput(AOFunction, TEXT("贴图值"), AOMask);
		ConnectFunctionInput(AOFunction, TEXT("倍率"), AOScale);
		ConnectFunctionInput(AOFunction, TEXT("固定值"), AOScalar);
		Result.AmbientOcclusion = AddTextureSwitch(
			Material,
			UseAOTexture,
			GroupSurface,
			33,
			Defaults.bUseAOTexture,
			AOFunction ? static_cast<UMaterialExpression*>(AOFunction) : static_cast<UMaterialExpression*>(AOScalar),
			AOScalar,
			-390,
			880);

		UMaterialExpressionTextureSampleParameter2D* SpecularTex = AddTexture(Material, SpecularTexture, GroupSurface, 40, SAMPLERTYPE_Masks, -1280, 1320);
		UMaterialExpressionComponentMask* SpecularMask = AddMask(Material, true, false, false, false, -1010, 1350);
		SpecularMask->Input.Connect(0, SpecularTex);
		UMaterialExpressionScalarParameter* SpecularScalar = AddScalar(Material, SpecularValue, GroupSurface, 41, Defaults.Specular, -1280, 1480);
		UMaterialExpressionMultiply* SpecularTextured = AddMultiply(Material, -760, 1360);
		SpecularTextured->A.Connect(0, SpecularMask);
		SpecularTextured->B.Connect(0, SpecularScalar);
		Result.Specular = AddTextureSwitch(
			Material,
			UseSpecularTexture,
			GroupSurface,
			42,
			Defaults.bUseSpecularTexture,
			SpecularTextured,
			SpecularScalar,
			-390,
			1360);

		UMaterialExpressionTextureSampleParameter2D* NormalTex = AddTexture(Material, NormalTexture, GroupNormal, 50, SAMPLERTYPE_Normal, -1280, 1780);
		UMaterialExpressionScalarParameter* NormalStrengthScalar = AddScalar(Material, NormalStrength, GroupNormal, 52, Defaults.NormalStrength, -1280, 1930);
		UMaterialExpressionMaterialFunctionCall* NormalFunction = AddFunctionCall(
			Material,
			TEXT("/Game/PBRStudio/Substrate/Functions/MF_PBRStudio_Substrate_NormalStrength.MF_PBRStudio_Substrate_NormalStrength"),
			-940,
			1810);
		ConnectFunctionInput(NormalFunction, TEXT("法线"), NormalTex);
		ConnectFunctionInput(NormalFunction, TEXT("强度"), NormalStrengthScalar);
		UMaterialExpressionConstant3Vector* FlatNormal = AddConstant3(Material, FLinearColor(0.0f, 0.0f, 1.0f), -940, 1950);
		Result.Normal = AddTextureSwitch(
			Material,
			UseNormalTexture,
			GroupNormal,
			41,
			Defaults.bUseNormalTexture,
			NormalFunction ? static_cast<UMaterialExpression*>(NormalFunction) : static_cast<UMaterialExpression*>(NormalTex),
			FlatNormal,
			-680,
			1780);

		Result.MetalnessToF0 = AddMetalnessToF0(Material, Result.BaseColor, Result.Metallic, Result.Specular, 80, -190);
		return Result;
	}

	static UMaterialExpressionSubstrateSlabBSDF* AddSlabOutput(
		UMaterial* Material,
		const FSurfaceOutputs& Surface,
		int32 X,
		int32 Y,
		EMaterialSubSurfaceType SubSurfaceType = MSS_None)
	{
		UMaterialExpressionSubstrateSlabBSDF* SlabNode = NewObject<UMaterialExpressionSubstrateSlabBSDF>(Material);
		SlabNode->MaterialExpressionEditorX = X;
		SlabNode->MaterialExpressionEditorY = Y;
		SlabNode->SubSurfaceType = SubSurfaceType;
		SlabNode->DiffuseAlbedo.Connect(0, Surface.MetalnessToF0);
		SlabNode->F0.Connect(1, Surface.MetalnessToF0);
		SlabNode->Roughness.Connect(0, Surface.Roughness);
		SlabNode->Normal.Connect(0, Surface.Normal);
		Material->GetExpressionCollection().AddExpression(SlabNode);
		return SlabNode;
	}

	static void ConnectCommonOutputs(UMaterial* Material, UMaterialExpressionSubstrateBSDF* FrontMaterial, const FSurfaceOutputs& Surface)
	{
		if (UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
		{
			if (Surface.AmbientOcclusion)
			{
				EditorData->AmbientOcclusion.Connect(0, Surface.AmbientOcclusion);
			}
			EditorData->FrontMaterial.Connect(0, FrontMaterial);
		}
	}

	static void FinalizeMaterialGraph(UMaterial* Material)
	{
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (!Expression || Cast<UMaterialExpressionComment>(Expression))
			{
				continue;
			}

			Expression->MaterialExpressionEditorX = FMath::GridSnap(Expression->MaterialExpressionEditorX, 20);
			Expression->MaterialExpressionEditorY = FMath::GridSnap(Expression->MaterialExpressionEditorY, 20);
		}

		Material->EditorX = 960;
		Material->EditorY = -260;
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
	}

	static FSurfaceDefaults MakeSurfaceDefaults(EPBRSubstrateTemplateType TemplateType)
	{
		FSurfaceDefaults Defaults;
		switch (TemplateType)
		{
		case EPBRSubstrateTemplateType::Wood:
			Defaults.BaseTint = FLinearColor(0.62f, 0.42f, 0.24f);
			Defaults.Roughness = 0.52f;
			Defaults.Specular = 0.38f;
			break;
		case EPBRSubstrateTemplateType::Stone:
			Defaults.BaseTint = FLinearColor(0.55f, 0.54f, 0.50f);
			Defaults.Roughness = 0.82f;
			Defaults.Specular = 0.26f;
			Defaults.NormalStrength = 0.85f;
			break;
		case EPBRSubstrateTemplateType::Tile:
			Defaults.BaseTint = FLinearColor(0.82f, 0.80f, 0.74f);
			Defaults.Roughness = 0.34f;
			Defaults.Specular = 0.55f;
			break;
		case EPBRSubstrateTemplateType::Leather:
			Defaults.BaseTint = FLinearColor(0.24f, 0.12f, 0.055f);
			Defaults.Roughness = 0.58f;
			Defaults.Specular = 0.44f;
			Defaults.NormalStrength = 0.7f;
			break;
		case EPBRSubstrateTemplateType::Plastic:
			Defaults.BaseTint = FLinearColor(0.72f, 0.72f, 0.68f);
			Defaults.Roughness = 0.38f;
			Defaults.Specular = 0.55f;
			break;
		case EPBRSubstrateTemplateType::Metal:
			Defaults.BaseTint = FLinearColor(0.72f, 0.70f, 0.66f);
			Defaults.Roughness = 0.28f;
			Defaults.Metallic = 1.0f;
			Defaults.Specular = 0.75f;
			Defaults.bUseMetallicTexture = true;
			break;
		case EPBRSubstrateTemplateType::Standard:
		default:
			Defaults.BaseTint = FLinearColor(0.72f, 0.70f, 0.66f);
			Defaults.Roughness = 0.58f;
			break;
		}
		return Defaults;
	}

	static void BuildSurfaceGraph(UMaterial* Material, EPBRSubstrateTemplateType TemplateType)
	{
		ResetGraph(Material);
		ResetMaterialSettings(Material);
		AddCommonGroups(Material);

		AddComment(Material, TEXT("基础色"), -1320, -580, 900, 410);
		AddComment(Material, TEXT("粗糙度 / 金属度 / 环境遮蔽 / 高光"), -1320, -130, 900, 1580);
		AddComment(Material, TEXT("法线"), -1320, 1700, 900, 360);
		AddComment(Material, TEXT("Substrate Slab BSDF - Simple 输出"), 20, -360, 820, 520);

		FSurfaceOutputs Surface = BuildSurfaceControls(Material, MakeSurfaceDefaults(TemplateType));
		UMaterialExpression* SharedUV = BuildSharedUVControls(Material);
		ConnectTextureSamplesToUV(Material, SharedUV);
		UMaterialExpressionSubstrateSlabBSDF* SlabNode = AddSlabOutput(Material, Surface, 460, -250);
		ConnectCommonOutputs(Material, SlabNode, Surface);
		FinalizeMaterialGraph(Material);
	}

	static void BuildStandardGraph(UMaterial* Material)
	{
		BuildSurfaceGraph(Material, EPBRSubstrateTemplateType::Standard);
	}

	static void BuildGlassGraph(UMaterial* Material)
	{
		ResetGraph(Material);
		ResetMaterialSettings(Material);
		Material->BlendMode = BLEND_TranslucentColoredTransmittance;
		Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
		Material->RefractionMethod = RM_IndexOfRefractionFromF0;
		Material->RefractionCoverageMode = RCM_CoverageAccountedFor;
		Material->bScreenSpaceReflections = true;
		AddCommonGroups(Material);
		AddGroup(Material, GroupTransparency, 3);

		AddComment(Material, TEXT("玻璃基础色和表面"), -1320, -580, 900, 1620);
		AddComment(Material, TEXT("透明 / 透射"), -1320, 1120, 900, 380);
		AddComment(Material, TEXT("Substrate Slab + Simple Volume"), 20, -360, 920, 640);

		FSurfaceDefaults Defaults;
		Defaults.BaseTint = FLinearColor(0.86f, 0.96f, 1.0f);
		Defaults.Roughness = 0.03f;
		Defaults.Specular = 0.85f;
		Defaults.bUseBaseTexture = false;
		Defaults.bUseRoughnessTexture = false;
		Defaults.bUseMetallicTexture = false;
		Defaults.bUseNormalTexture = false;
		FSurfaceOutputs Surface = BuildSurfaceControls(Material, Defaults);
		UMaterialExpression* SharedUV = BuildSharedUVControls(Material);

		UMaterialExpressionTextureSampleParameter2D* OpacityTex = AddTexture(Material, OpacityTexture, GroupTransparency, 49, SAMPLERTYPE_Masks, -1280, 1040);
		UMaterialExpressionComponentMask* OpacityMask = AddMask(Material, true, false, false, false, -1010, 1070);
		OpacityMask->Input.Connect(0, OpacityTex);
		UMaterialExpressionScalarParameter* Opacity = AddScalar(Material, OpacityValue, GroupTransparency, 50, 0.38f, -1280, 1120);
		UMaterialExpression* OpacitySwitch = AddTextureSwitch(
			Material,
			UseOpacityTexture,
			GroupTransparency,
			53,
			false,
			OpacityMask,
			Opacity,
			-740,
			1100);
		UMaterialExpressionVectorParameter* Transmittance = AddVector(Material, TransmissionColor, GroupTransparency, 51, FLinearColor(0.82f, 0.95f, 1.0f), -1280, 1260);
		UMaterialExpressionScalarParameter* Thickness = AddScalar(Material, ThicknessCm, GroupTransparency, 52, 0.25f, -1280, 1410);
		ConnectTextureSamplesToUV(Material, SharedUV);
		UMaterialExpressionSubstrateTransmittanceToMFP* MFP = NewObject<UMaterialExpressionSubstrateTransmittanceToMFP>(Material);
		MFP->MaterialExpressionEditorX = -760;
		MFP->MaterialExpressionEditorY = 1240;
		MFP->TransmittanceColor.Connect(0, Transmittance);
		MFP->Thickness.Connect(0, Thickness);
		MFP->Outputs.Reset();
		MFP->Outputs.Add(FExpressionOutput(TEXT("MFP"), 1, 1, 1, 1, 0));
		MFP->Outputs.Add(FExpressionOutput(TEXT("Thickness"), 1, 1, 0, 0, 0));
		Material->GetExpressionCollection().AddExpression(MFP);

		UMaterialExpressionSubstrateSlabBSDF* SlabNode = AddSlabOutput(Material, Surface, 460, -250, MSS_SimpleVolume);
		SlabNode->SSSMFP.Connect(0, MFP);

		if (UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
		{
			EditorData->Opacity.Connect(0, OpacitySwitch);
			EditorData->SurfaceThickness.Connect(0, Thickness);
		}
		ConnectCommonOutputs(Material, SlabNode, Surface);
		FinalizeMaterialGraph(Material);
	}

	static void BuildCarPaintGraph(UMaterial* Material)
	{
		ResetGraph(Material);
		ResetMaterialSettings(Material);
		AddCommonGroups(Material);
		AddGroup(Material, GroupCoat, 3);

		AddComment(Material, TEXT("车漆底层"), -1320, -580, 900, 1620);
		AddComment(Material, TEXT("清漆层"), -1320, 1120, 900, 320);
		AddComment(Material, TEXT("Substrate Simple Clear Coat"), 20, -360, 920, 620);

		FSurfaceDefaults Defaults;
		Defaults.BaseTint = FLinearColor(0.12f, 0.22f, 0.62f);
		Defaults.Roughness = 0.22f;
		Defaults.Specular = 0.7f;
		Defaults.bUseBaseTexture = false;
		Defaults.bUseRoughnessTexture = false;
		Defaults.bUseNormalTexture = false;
		FSurfaceOutputs Surface = BuildSurfaceControls(Material, Defaults);
		UMaterialExpression* SharedUV = BuildSharedUVControls(Material);
		ConnectTextureSamplesToUV(Material, SharedUV);

		UMaterialExpressionScalarParameter* CoatCoverage = AddScalar(Material, ClearCoatCoverage, GroupCoat, 50, 1.0f, -1280, 1120);
		UMaterialExpressionScalarParameter* CoatRoughness = AddScalar(Material, ClearCoatRoughness, GroupCoat, 51, 0.06f, -1280, 1260);

		UMaterialExpressionSubstrateSimpleClearCoatBSDF* ClearCoatNode = NewObject<UMaterialExpressionSubstrateSimpleClearCoatBSDF>(Material);
		ClearCoatNode->MaterialExpressionEditorX = 460;
		ClearCoatNode->MaterialExpressionEditorY = -250;
		ClearCoatNode->DiffuseAlbedo.Connect(0, Surface.MetalnessToF0);
		ClearCoatNode->F0.Connect(1, Surface.MetalnessToF0);
		ClearCoatNode->Roughness.Connect(0, Surface.Roughness);
		ClearCoatNode->Normal.Connect(0, Surface.Normal);
		ClearCoatNode->ClearCoatCoverage.Connect(0, CoatCoverage);
		ClearCoatNode->ClearCoatRoughness.Connect(0, CoatRoughness);
		Material->GetExpressionCollection().AddExpression(ClearCoatNode);
		ConnectCommonOutputs(Material, ClearCoatNode, Surface);
		FinalizeMaterialGraph(Material);
	}

	static void BuildLeafGraph(UMaterial* Material)
	{
		ResetGraph(Material);
		ResetMaterialSettings(Material);
		Material->BlendMode = BLEND_Masked;
		Material->TwoSided = true;
		Material->OpacityMaskClipValue = 0.35f;
		AddCommonGroups(Material);
		AddGroup(Material, GroupTransparency, 3);
		AddGroup(Material, GroupFuzz, 4);

		AddComment(Material, TEXT("树叶基础色和表面"), -1320, -580, 900, 1620);
		AddComment(Material, TEXT("遮罩和透光"), -1320, 1120, 900, 520);
		AddComment(Material, TEXT("Substrate 双面树叶"), 20, -360, 920, 640);

		FSurfaceDefaults Defaults;
		Defaults.BaseTint = FLinearColor(0.24f, 0.54f, 0.16f);
		Defaults.Roughness = 0.62f;
		Defaults.Specular = 0.32f;
		Defaults.bUseBaseTexture = false;
		Defaults.bUseRoughnessTexture = false;
		Defaults.bUseNormalTexture = false;
		FSurfaceOutputs Surface = BuildSurfaceControls(Material, Defaults);
		UMaterialExpression* SharedUV = BuildSharedUVControls(Material);

		UMaterialExpressionTextureSampleParameter2D* OpacityMaskTexture = AddTexture(Material, MaskTexture, GroupTransparency, 50, SAMPLERTYPE_Masks, -1280, 1120);
		UMaterialExpressionComponentMask* MaskR = AddMask(Material, true, false, false, false, -1010, 1150);
		MaskR->Input.Connect(0, OpacityMaskTexture);
		UMaterialExpressionConstant* FullMask = AddConstant(Material, 1.0f, -1010, 1280);
		UMaterialExpression* MaskSwitch = AddTextureSwitch(
			Material,
			UseMaskTexture,
			GroupTransparency,
			51,
			true,
			MaskR,
			FullMask,
			-740,
			1160);
		UMaterialExpressionVectorParameter* Translucency = AddVector(Material, SubsurfaceColor, GroupFuzz, 60, FLinearColor(0.25f, 0.62f, 0.18f), -1280, 1460);
		ConnectTextureSamplesToUV(Material, SharedUV);

		UMaterialExpressionSubstrateSlabBSDF* SlabNode = AddSlabOutput(Material, Surface, 460, -250, MSS_TwoSidedWrap);
		SlabNode->SSSMFP.Connect(0, Translucency);

		if (UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
		{
			EditorData->OpacityMask.Connect(0, MaskSwitch);
		}
		ConnectCommonOutputs(Material, SlabNode, Surface);
		FinalizeMaterialGraph(Material);
	}

	static void BuildFabricGraph(UMaterial* Material)
	{
		ResetGraph(Material);
		ResetMaterialSettings(Material);
		AddCommonGroups(Material);
		AddGroup(Material, GroupFuzz, 3);

		AddComment(Material, TEXT("布料基础色和表面"), -1320, -580, 900, 1620);
		AddComment(Material, TEXT("绒毛层"), -1320, 1120, 900, 420);
		AddComment(Material, TEXT("Substrate Slab + Fuzz"), 20, -360, 920, 640);

		FSurfaceDefaults Defaults;
		Defaults.BaseTint = FLinearColor(0.52f, 0.49f, 0.44f);
		Defaults.Roughness = 0.86f;
		Defaults.Specular = 0.35f;
		Defaults.NormalStrength = 0.65f;
		Defaults.bUseBaseTexture = false;
		Defaults.bUseRoughnessTexture = false;
		Defaults.bUseNormalTexture = false;
		FSurfaceOutputs Surface = BuildSurfaceControls(Material, Defaults);
		UMaterialExpression* SharedUV = BuildSharedUVControls(Material);
		ConnectTextureSamplesToUV(Material, SharedUV);

		UMaterialExpressionScalarParameter* Fuzz = AddScalar(Material, FuzzAmount, GroupFuzz, 50, 0.35f, -1280, 1120);
		UMaterialExpressionVectorParameter* FuzzTint = AddVector(Material, FuzzColor, GroupFuzz, 51, FLinearColor(0.72f, 0.68f, 0.60f), -1280, 1260);
		UMaterialExpressionScalarParameter* FuzzRough = AddScalar(Material, FuzzRoughness, GroupFuzz, 52, 0.82f, -1280, 1410);

		UMaterialExpressionSubstrateSlabBSDF* SlabNode = AddSlabOutput(Material, Surface, 460, -250, MSS_None);
		SlabNode->FuzzAmount.Connect(0, Fuzz);
		SlabNode->FuzzColor.Connect(0, FuzzTint);
		SlabNode->FuzzRoughness.Connect(0, FuzzRough);

		ConnectCommonOutputs(Material, SlabNode, Surface);
		FinalizeMaterialGraph(Material);
	}

	static void BuildEmissiveGraph(UMaterial* Material)
	{
		ResetGraph(Material);
		ResetMaterialSettings(Material);
		Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
		AddGroup(Material, GroupEmission, 0);
		AddGroup(Material, GroupOutput, 1);

		AddComment(Material, TEXT("自发光颜色"), -1320, -420, 900, 620);
		AddComment(Material, TEXT("Substrate Unlit BSDF"), 20, -260, 720, 360);

		UMaterialExpressionTextureSampleParameter2D* EmitTex = AddTexture(Material, EmissiveTexture, GroupEmission, 0, SAMPLERTYPE_Color, -1280, -360);
		UMaterialExpression* SharedUV = BuildSharedUVControls(Material);
		ConnectTextureSamplesToUV(Material, SharedUV);
		UMaterialExpressionVectorParameter* EmitColor = AddVector(Material, EmissiveColor, GroupEmission, 1, FLinearColor(1.0f, 0.72f, 0.38f), -1280, -200);
		UMaterialExpressionScalarParameter* EmitIntensity = AddScalar(Material, EmissiveIntensity, GroupEmission, 2, 5.0f, -1280, -50);
		UMaterialExpressionMaterialFunctionCall* EmissiveFunction = AddFunctionCall(
			Material,
			TEXT("/Game/PBRStudio/Substrate/Functions/MF_PBRStudio_Substrate_Emissive.MF_PBRStudio_Substrate_Emissive"),
			-860,
			-260);
		ConnectFunctionInput(EmissiveFunction, TEXT("贴图颜色"), EmitTex);
		ConnectFunctionInput(EmissiveFunction, TEXT("颜色"), EmitColor);
		ConnectFunctionInput(EmissiveFunction, TEXT("强度"), EmitIntensity);
		UMaterialExpressionMultiply* SolidEmission = AddMultiply(Material, -620, -60);
		SolidEmission->A.Connect(0, EmitColor);
		SolidEmission->B.Connect(0, EmitIntensity);
		UMaterialExpression* UseTexture = AddTextureSwitch(
			Material,
			UseEmissiveTexture,
			GroupEmission,
			3,
			false,
			EmissiveFunction ? static_cast<UMaterialExpression*>(EmissiveFunction) : static_cast<UMaterialExpression*>(SolidEmission),
			SolidEmission,
			-390,
			-190);

		UMaterialExpressionSubstrateUnlitBSDF* UnlitNode = NewObject<UMaterialExpressionSubstrateUnlitBSDF>(Material);
		UnlitNode->MaterialExpressionEditorX = 320;
		UnlitNode->MaterialExpressionEditorY = -180;
		UnlitNode->EmissiveColor.Connect(0, UseTexture);
		Material->GetExpressionCollection().AddExpression(UnlitNode);

		if (UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
		{
			EditorData->FrontMaterial.Connect(0, UnlitNode);
		}
		FinalizeMaterialGraph(Material);
	}

	static void BuildTemplateGraph(UMaterial* Material, EPBRSubstrateTemplateType TemplateType)
	{
		switch (TemplateType)
		{
		case EPBRSubstrateTemplateType::Wood:
		case EPBRSubstrateTemplateType::Stone:
		case EPBRSubstrateTemplateType::Tile:
		case EPBRSubstrateTemplateType::Leather:
		case EPBRSubstrateTemplateType::Plastic:
		case EPBRSubstrateTemplateType::Metal:
			BuildSurfaceGraph(Material, TemplateType);
			break;
		case EPBRSubstrateTemplateType::Transparent:
		case EPBRSubstrateTemplateType::Water:
		case EPBRSubstrateTemplateType::Glass:
			BuildGlassGraph(Material);
			break;
		case EPBRSubstrateTemplateType::CarPaint:
			BuildCarPaintGraph(Material);
			break;
		case EPBRSubstrateTemplateType::Leaf:
			BuildLeafGraph(Material);
			break;
		case EPBRSubstrateTemplateType::Fabric:
			BuildFabricGraph(Material);
			break;
		case EPBRSubstrateTemplateType::Emissive:
			BuildEmissiveGraph(Material);
			break;
		case EPBRSubstrateTemplateType::Standard:
		default:
			BuildStandardGraph(Material);
			break;
		}
	}

	static void ApplyCommonExampleParameters(UMaterialInstanceConstant* Instance, const FSurfaceDefaults& Defaults)
	{
		Instance->SetScalarParameterValueEditorOnly(UseBaseColorTexture, Defaults.bUseBaseTexture ? 1.0f : 0.0f);
		Instance->SetScalarParameterValueEditorOnly(UseRoughnessTexture, Defaults.bUseRoughnessTexture ? 1.0f : 0.0f);
		Instance->SetScalarParameterValueEditorOnly(UseMetallicTexture, Defaults.bUseMetallicTexture ? 1.0f : 0.0f);
		Instance->SetScalarParameterValueEditorOnly(UseAOTexture, Defaults.bUseAOTexture ? 1.0f : 0.0f);
		Instance->SetScalarParameterValueEditorOnly(UseSpecularTexture, Defaults.bUseSpecularTexture ? 1.0f : 0.0f);
		Instance->SetScalarParameterValueEditorOnly(UseNormalTexture, Defaults.bUseNormalTexture ? 1.0f : 0.0f);
		Instance->SetVectorParameterValueEditorOnly(BaseColorTint, Defaults.BaseTint);
		Instance->SetScalarParameterValueEditorOnly(BaseColorIntensity, Defaults.BaseIntensity);
		Instance->SetScalarParameterValueEditorOnly(RoughnessValue, Defaults.Roughness);
		Instance->SetScalarParameterValueEditorOnly(RoughnessMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(MetallicValue, Defaults.Metallic);
		Instance->SetScalarParameterValueEditorOnly(MetallicMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(SpecularValue, Defaults.Specular);
		Instance->SetScalarParameterValueEditorOnly(AOValue, Defaults.AO);
		Instance->SetScalarParameterValueEditorOnly(AOMultiplier, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(NormalStrength, Defaults.NormalStrength);
		Instance->SetScalarParameterValueEditorOnly(UVUTiling, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(UVVTiling, 1.0f);
		Instance->SetScalarParameterValueEditorOnly(UVUOffset, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(UVVOffset, 0.0f);
		Instance->SetScalarParameterValueEditorOnly(UVRotationDegrees, 0.0f);
	}

	static void ApplyExampleParameters(UMaterialInstanceConstant* Instance, EPBRSubstrateTemplateType TemplateType)
	{
		FSurfaceDefaults Defaults = MakeSurfaceDefaults(TemplateType);
		Defaults.bUseBaseTexture = false;
		Defaults.bUseRoughnessTexture = false;
		Defaults.bUseMetallicTexture = false;
		Defaults.bUseAOTexture = false;
		Defaults.bUseSpecularTexture = false;
		Defaults.bUseNormalTexture = false;

		switch (TemplateType)
		{
		case EPBRSubstrateTemplateType::Wood:
		case EPBRSubstrateTemplateType::Stone:
		case EPBRSubstrateTemplateType::Tile:
		case EPBRSubstrateTemplateType::Leather:
		case EPBRSubstrateTemplateType::Plastic:
		case EPBRSubstrateTemplateType::Metal:
			ApplyCommonExampleParameters(Instance, Defaults);
			break;
		case EPBRSubstrateTemplateType::Transparent:
			Defaults.BaseTint = FLinearColor(0.8f, 0.95f, 1.0f);
			Defaults.Roughness = 0.12f;
			Defaults.Specular = 0.72f;
			ApplyCommonExampleParameters(Instance, Defaults);
			Instance->SetScalarParameterValueEditorOnly(UseOpacityTexture, 0.0f);
			Instance->SetScalarParameterValueEditorOnly(OpacityValue, 0.55f);
			Instance->SetVectorParameterValueEditorOnly(TransmissionColor, FLinearColor(0.86f, 0.96f, 1.0f));
			Instance->SetScalarParameterValueEditorOnly(ThicknessCm, 0.12f);
			break;
		case EPBRSubstrateTemplateType::Water:
			Defaults.BaseTint = FLinearColor(0.24f, 0.62f, 0.84f);
			Defaults.Roughness = 0.015f;
			Defaults.Specular = 0.92f;
			ApplyCommonExampleParameters(Instance, Defaults);
			Instance->SetScalarParameterValueEditorOnly(UseOpacityTexture, 0.0f);
			Instance->SetScalarParameterValueEditorOnly(OpacityValue, 0.48f);
			Instance->SetVectorParameterValueEditorOnly(TransmissionColor, FLinearColor(0.35f, 0.76f, 0.95f));
			Instance->SetScalarParameterValueEditorOnly(ThicknessCm, 0.32f);
			break;
		case EPBRSubstrateTemplateType::Glass:
			Defaults.BaseTint = FLinearColor(0.86f, 0.96f, 1.0f);
			Defaults.Roughness = 0.025f;
			Defaults.Specular = 0.9f;
			ApplyCommonExampleParameters(Instance, Defaults);
			Instance->SetScalarParameterValueEditorOnly(UseOpacityTexture, 0.0f);
			Instance->SetScalarParameterValueEditorOnly(OpacityValue, 0.34f);
			Instance->SetVectorParameterValueEditorOnly(TransmissionColor, FLinearColor(0.78f, 0.93f, 1.0f));
			Instance->SetScalarParameterValueEditorOnly(ThicknessCm, 0.22f);
			break;
		case EPBRSubstrateTemplateType::CarPaint:
			Defaults.BaseTint = FLinearColor(0.08f, 0.18f, 0.72f);
			Defaults.Roughness = 0.18f;
			Defaults.Specular = 0.75f;
			ApplyCommonExampleParameters(Instance, Defaults);
			Instance->SetScalarParameterValueEditorOnly(ClearCoatCoverage, 1.0f);
			Instance->SetScalarParameterValueEditorOnly(ClearCoatRoughness, 0.045f);
			break;
		case EPBRSubstrateTemplateType::Leaf:
			Defaults.BaseTint = FLinearColor(0.22f, 0.52f, 0.12f);
			Defaults.Roughness = 0.66f;
			Defaults.Specular = 0.28f;
			ApplyCommonExampleParameters(Instance, Defaults);
			Instance->SetScalarParameterValueEditorOnly(UseMaskTexture, 0.0f);
			Instance->SetVectorParameterValueEditorOnly(SubsurfaceColor, FLinearColor(0.24f, 0.62f, 0.12f));
			break;
		case EPBRSubstrateTemplateType::Fabric:
			Defaults.BaseTint = FLinearColor(0.50f, 0.46f, 0.40f);
			Defaults.Roughness = 0.88f;
			Defaults.Specular = 0.32f;
			Defaults.NormalStrength = 0.55f;
			ApplyCommonExampleParameters(Instance, Defaults);
			Instance->SetScalarParameterValueEditorOnly(FuzzAmount, 0.42f);
			Instance->SetVectorParameterValueEditorOnly(FuzzColor, FLinearColor(0.72f, 0.68f, 0.58f));
			Instance->SetScalarParameterValueEditorOnly(FuzzRoughness, 0.82f);
			break;
		case EPBRSubstrateTemplateType::Emissive:
			Instance->SetScalarParameterValueEditorOnly(UseEmissiveTexture, 0.0f);
			Instance->SetVectorParameterValueEditorOnly(EmissiveColor, FLinearColor(1.0f, 0.68f, 0.32f));
			Instance->SetScalarParameterValueEditorOnly(EmissiveIntensity, 6.0f);
			break;
		case EPBRSubstrateTemplateType::Standard:
		default:
			Defaults.BaseTint = FLinearColor(0.64f, 0.62f, 0.58f);
			Defaults.Roughness = 0.58f;
			ApplyCommonExampleParameters(Instance, Defaults);
			break;
		}
	}
}

const TArray<EPBRSubstrateTemplateType>& FPBRSubstrateMaterialTemplateManager::GetAllTemplateTypes()
{
	static const TArray<EPBRSubstrateTemplateType> Types = {
		EPBRSubstrateTemplateType::Standard,
		EPBRSubstrateTemplateType::Wood,
		EPBRSubstrateTemplateType::Stone,
		EPBRSubstrateTemplateType::Tile,
		EPBRSubstrateTemplateType::Leather,
		EPBRSubstrateTemplateType::Plastic,
		EPBRSubstrateTemplateType::Metal,
		EPBRSubstrateTemplateType::Transparent,
		EPBRSubstrateTemplateType::Water,
		EPBRSubstrateTemplateType::Glass,
		EPBRSubstrateTemplateType::CarPaint,
		EPBRSubstrateTemplateType::Leaf,
		EPBRSubstrateTemplateType::Fabric,
		EPBRSubstrateTemplateType::Emissive
	};
	return Types;
}

FString FPBRSubstrateMaterialTemplateManager::GetTemplateDisplayName(EPBRSubstrateTemplateType TemplateType)
{
	return PBRSubstrateTemplate::GetInfo(TemplateType).DisplayName;
}

FString FPBRSubstrateMaterialTemplateManager::GetTemplatePackagePath(EPBRSubstrateTemplateType TemplateType)
{
	return FString(PBRSubstrateTemplate::TemplateRoot) + PBRSubstrateTemplate::GetInfo(TemplateType).AssetName;
}

FString FPBRSubstrateMaterialTemplateManager::GetExampleMaterialInstancePackagePath(EPBRSubstrateTemplateType TemplateType)
{
	return FString(PBRSubstrateTemplate::ExampleRoot) + PBRSubstrateTemplate::GetInfo(TemplateType).ExampleName;
}

FString FPBRSubstrateMaterialTemplateManager::GetStandardTemplatePackagePath()
{
	return GetTemplatePackagePath(EPBRSubstrateTemplateType::Standard);
}

UMaterial* FPBRSubstrateMaterialTemplateManager::EnsureTemplateMaterial(EPBRSubstrateTemplateType TemplateType, FString& OutMessage)
{
	if (!Substrate::IsSubstrateEnabled())
	{
		OutMessage = TEXT("Substrate 未启用。请先启用 r.Substrate，再重建 Substrate 模板。");
		return nullptr;
	}

	const PBRSubstrateTemplate::FTemplateInfo& Info = PBRSubstrateTemplate::GetInfo(TemplateType);
	return CreateOrRebuildTemplate(TemplateType, GetTemplatePackagePath(TemplateType), Info.AssetName, OutMessage);
}

UMaterialInstanceConstant* FPBRSubstrateMaterialTemplateManager::EnsureExampleMaterialInstance(EPBRSubstrateTemplateType TemplateType, FString& OutMessage)
{
	FString ParentMessage;
	UMaterial* ParentMaterial = EnsureTemplateMaterial(TemplateType, ParentMessage);
	if (!ParentMaterial)
	{
		OutMessage = FString::Printf(TEXT("创建 Substrate 示例失败: %s"), *ParentMessage);
		return nullptr;
	}

	const PBRSubstrateTemplate::FTemplateInfo& Info = PBRSubstrateTemplate::GetInfo(TemplateType);
	const FString ExamplePath = GetExampleMaterialInstancePackagePath(TemplateType);
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(PBRSubstrateTemplate::LoadAssetIfExists(ExamplePath));
	if (!Instance)
	{
		UPackage* Package = CreatePackage(*ExamplePath);
		if (!Package)
		{
			OutMessage = TEXT("创建 Substrate 示例包失败。");
			return nullptr;
		}

		Instance = NewObject<UMaterialInstanceConstant>(Package, FName(Info.ExampleName), RF_Public | RF_Standalone);
		if (!Instance)
		{
			OutMessage = TEXT("创建 Substrate 示例资源失败。");
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(Instance);
		Package->SetDirtyFlag(true);
	}

	Instance->SetParentEditorOnly(ParentMaterial);
	Instance->ClearParameterValuesEditorOnly();
	PBRSubstrateTemplate::ApplyExampleParameters(Instance, TemplateType);
	Instance->PostEditChange();
	UEditorLoadingAndSavingUtils::SavePackages({ Instance->GetPackage() }, true);

	OutMessage = FString::Printf(TEXT("已创建/更新 Substrate 示例材质实例: %s"), Info.DisplayName);
	return Instance;
}

UMaterial* FPBRSubstrateMaterialTemplateManager::EnsureStandardTemplateMaterial(FString& OutMessage)
{
	return EnsureTemplateMaterial(EPBRSubstrateTemplateType::Standard, OutMessage);
}

UMaterialInstanceConstant* FPBRSubstrateMaterialTemplateManager::EnsureStandardExampleMaterialInstance(FString& OutMessage)
{
	return EnsureExampleMaterialInstance(EPBRSubstrateTemplateType::Standard, OutMessage);
}

int32 FPBRSubstrateMaterialTemplateManager::EnsureAllTemplateMaterials(TArray<FString>& OutMessages)
{
	int32 CreatedOrLoaded = 0;
	for (EPBRSubstrateTemplateType TemplateType : GetAllTemplateTypes())
	{
		const PBRSubstrateTemplate::FTemplateInfo& Info = PBRSubstrateTemplate::GetInfo(TemplateType);
		FString Message;
		if (EnsureTemplateMaterial(TemplateType, Message))
		{
			++CreatedOrLoaded;
			FString ExampleMessage;
			EnsureExampleMaterialInstance(TemplateType, ExampleMessage);
			OutMessages.Add(FString(Info.AssetName) + TEXT(": ") + Message + TEXT("; ") + ExampleMessage);
		}
		else
		{
			OutMessages.Add(FString(Info.AssetName) + TEXT(": 失败 - ") + Message);
		}
	}
	return CreatedOrLoaded;
}

UMaterial* FPBRSubstrateMaterialTemplateManager::CreateOrRebuildTemplate(EPBRSubstrateTemplateType TemplateType, const FString& PackagePath, const FString& AssetName, FString& OutMessage)
{
	PBRSubstrateTemplate::EnsureSubstrateMaterialFunctions();

	UMaterial* Material = Cast<UMaterial>(PBRSubstrateTemplate::LoadAssetIfExists(PackagePath));
	if (!Material)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			OutMessage = TEXT("创建 Substrate 母材质包失败。");
			return nullptr;
		}

		Material = NewObject<UMaterial>(Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!Material)
		{
			OutMessage = TEXT("创建 Substrate 母材质资源失败。");
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(Material);
		Package->SetDirtyFlag(true);
	}

	PBRSubstrateTemplate::BuildTemplateGraph(Material, TemplateType);
	SaveMaterial(Material);
	OutMessage = FString::Printf(TEXT("已创建/重建独立 Substrate 母材质: %s"), *GetTemplateDisplayName(TemplateType));
	return Material;
}

void FPBRSubstrateMaterialTemplateManager::SaveMaterial(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	TArray<UPackage*> PackagesToSave = { Material->GetPackage() };
	UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
}
