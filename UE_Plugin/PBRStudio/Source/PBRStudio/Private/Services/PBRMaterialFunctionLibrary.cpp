#include "Services/PBRMaterialFunctionLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "FileHelpers.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionRotator.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialFunction.h"
#include "UObject/Package.h"

static constexpr const TCHAR* FunctionRoot = TEXT("/PBRStudio/Functions");
static constexpr float PBRDefaultWaterFlowSpeedU = 0.18f;
static constexpr float PBRDefaultWaterFlowSpeedV = 0.09f;
static constexpr float PBRDefaultWaterRippleScale = 18.0f;
static constexpr float PBRDefaultWaterRippleStrength = 0.8f;

static UObject* LoadFunctionAsset(const FString& PackagePath)
{
	return UEditorAssetLibrary::DoesAssetExist(PackagePath)
		? UEditorAssetLibrary::LoadAsset(PackagePath)
		: nullptr;
}

static UMaterialFunction* CreateOrResetFunction(const FString& Folder, const FString& AssetName, const FString& Description, const FString& Category)
{
	const FString PackagePath = FString(FunctionRoot) + TEXT("/") + Folder + TEXT("/") + AssetName;
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return nullptr;
	}

	UMaterialFunction* Function = Cast<UMaterialFunction>(LoadFunctionAsset(PackagePath));
	if (!Function)
	{
		Function = NewObject<UMaterialFunction>(Package, FName(*AssetName), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Function);
	}

	Function->Description = Description;
	Function->UserExposedCaption = AssetName;
	Function->bExposeToLibrary = true;
	Function->LibraryCategoriesText.Empty();
	Function->LibraryCategoriesText.Add(FText::FromString(TEXT("PBRStudio/") + Category));
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

static void AddComment(UMaterialFunction* Function, const FString& Text, int32 X, int32 Y, int32 SizeX, int32 SizeY)
{
	UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Function);
	Comment->Text = Text;
	Comment->MaterialExpressionEditorX = X;
	Comment->MaterialExpressionEditorY = Y;
	Comment->SizeX = SizeX;
	Comment->SizeY = SizeY;
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
	int32 Y,
	const FString& Description = FString())
{
	UMaterialExpressionFunctionInput* Input = NewObject<UMaterialExpressionFunctionInput>(Function);
	Input->InputName = Name;
	Input->Description = Description;
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
	int32 Y,
	const FString& Description = FString())
{
	UMaterialExpressionFunctionOutput* Output = NewObject<UMaterialExpressionFunctionOutput>(Function);
	Output->OutputName = Name;
	Output->Description = Description;
	Output->SortPriority = SortPriority;
	Output->A.Connect(0, Expression);
	Output->MaterialExpressionEditorX = X;
	Output->MaterialExpressionEditorY = Y;
	Output->ConditionallyGenerateId(true);
	Function->GetExpressionCollection().AddExpression(Output);
	return Output;
}

template <typename T>
static T* AddNode(UMaterialFunction* Function, int32 X, int32 Y)
{
	T* Node = NewObject<T>(Function);
	Node->MaterialExpressionEditorX = X;
	Node->MaterialExpressionEditorY = Y;
	Function->GetExpressionCollection().AddExpression(Node);
	return Node;
}

static bool BuildUVControlsFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("01_UV"), TEXT("MF_PBRStudio_UVControls"), TEXT("统一 UV 平铺、偏移、旋转。"), TEXT("01 UV"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("UV: Input UV -> Tiling -> Offset -> Rotate(degrees)"), -1160, -360, 1720, 760);
	UMaterialExpressionFunctionInput* UV = AddInput(Function, TEXT("UV"), FunctionInput_Vector2, FVector4f(0, 0, 0, 0), 10, -1080, -260);
	UMaterialExpressionFunctionInput* UTiling = AddInput(Function, TEXT("U 平铺"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 20, -1080, -140);
	UMaterialExpressionFunctionInput* VTiling = AddInput(Function, TEXT("V 平铺"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 30, -1080, -20);
	UMaterialExpressionFunctionInput* UOffset = AddInput(Function, TEXT("U 偏移"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 40, -1080, 100);
	UMaterialExpressionFunctionInput* VOffset = AddInput(Function, TEXT("V 偏移"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 50, -1080, 220);
	UMaterialExpressionFunctionInput* RotationDegrees = AddInput(Function, TEXT("旋转角度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 60, -1080, 340);

	UMaterialExpressionAppendVector* TilingUV = AddNode<UMaterialExpressionAppendVector>(Function, -760, -160);
	TilingUV->A.Connect(0, UTiling);
	TilingUV->B.Connect(0, VTiling);
	UMaterialExpressionAppendVector* OffsetUV = AddNode<UMaterialExpressionAppendVector>(Function, -760, 120);
	OffsetUV->A.Connect(0, UOffset);
	OffsetUV->B.Connect(0, VOffset);
	UMaterialExpressionMultiply* TiledUV = AddNode<UMaterialExpressionMultiply>(Function, -500, -220);
	TiledUV->A.Connect(0, UV);
	TiledUV->B.Connect(0, TilingUV);
	UMaterialExpressionAdd* OffsetResult = AddNode<UMaterialExpressionAdd>(Function, -260, -220);
	OffsetResult->A.Connect(0, TiledUV);
	OffsetResult->B.Connect(0, OffsetUV);
	UMaterialExpressionDivide* DegreesToTurns = AddNode<UMaterialExpressionDivide>(Function, -260, 40);
	DegreesToTurns->A.Connect(0, RotationDegrees);
	DegreesToTurns->ConstB = 360.0f;
	UMaterialExpressionRotator* Rotator = AddNode<UMaterialExpressionRotator>(Function, 0, -220);
	Rotator->CenterX = 0.5f;
	Rotator->CenterY = 0.5f;
	Rotator->Coordinate.Connect(0, OffsetResult);
	Rotator->Time.Connect(0, DegreesToTurns);
	AddOutput(Function, TEXT("UV"), Rotator, 10, 320, -220);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_UVControls");
	return true;
}

static bool BuildDynamicPannerFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("02_Dynamic"), TEXT("MF_PBRStudio_DynamicPanner"), TEXT("动态 UV 平移和缩放。"), TEXT("02 Dynamic"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("动态控制: 输入 UV -> 缩放 -> Panner"), -1120, -280, 1640, 640);
	UMaterialExpressionFunctionInput* UV = AddInput(Function, TEXT("UV"), FunctionInput_Vector2, FVector4f(0, 0, 0, 0), 10, -1080, -200);
	UMaterialExpressionFunctionInput* SpeedU = AddInput(Function, TEXT("U 速度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 20, -1080, -80);
	UMaterialExpressionFunctionInput* SpeedV = AddInput(Function, TEXT("V 速度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 30, -1080, 40);
	UMaterialExpressionFunctionInput* Scale = AddInput(Function, TEXT("缩放"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 40, -1080, 160);
	UMaterialExpressionAppendVector* Speed = AddNode<UMaterialExpressionAppendVector>(Function, -760, -40);
	Speed->A.Connect(0, SpeedU);
	Speed->B.Connect(0, SpeedV);
	UMaterialExpressionMultiply* ScaledUV = AddNode<UMaterialExpressionMultiply>(Function, -520, -190);
	ScaledUV->A.Connect(0, UV);
	ScaledUV->B.Connect(0, Scale);
	UMaterialExpressionPanner* Panner = AddNode<UMaterialExpressionPanner>(Function, -240, -160);
	Panner->Coordinate.Connect(0, ScaledUV);
	Panner->Speed.Connect(0, Speed);
	AddOutput(Function, TEXT("动态 UV"), Panner, 10, 120, -160);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_DynamicPanner");
	return true;
}

static bool BuildTextureTintFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("03_Surface"), TEXT("MF_PBRStudio_TextureTintIntensity"), TEXT("贴图颜色乘以调色和强度。"), TEXT("03 Surface"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("表面颜色: TextureColor * Tint * Intensity"), -980, -260, 1320, 520);
	UMaterialExpressionFunctionInput* TextureColor = AddInput(Function, TEXT("贴图颜色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 10, -920, -180);
	UMaterialExpressionFunctionInput* Tint = AddInput(Function, TEXT("调色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 20, -920, -40);
	UMaterialExpressionFunctionInput* Intensity = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 30, -920, 100);
	UMaterialExpressionMultiply* Tinted = AddNode<UMaterialExpressionMultiply>(Function, -560, -150);
	Tinted->A.Connect(0, TextureColor);
	Tinted->B.Connect(0, Tint);
	UMaterialExpressionMultiply* Result = AddNode<UMaterialExpressionMultiply>(Function, -280, -120);
	Result->A.Connect(0, Tinted);
	Result->B.Connect(0, Intensity);
	AddOutput(Function, TEXT("颜色"), Result, 10, 80, -120);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_TextureTintIntensity");
	return true;
}

static bool BuildNormalStrengthFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("02_Surface"), TEXT("MF_PBRStudio_NormalStrength"), TEXT("Tangent-space normal intensity."), TEXT("02 Surface"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Normal: lerp(flat, normal, saturate(strength))"), -980, -260, 1380, 540);
	UMaterialExpressionFunctionInput* Normal = AddInput(Function, TEXT("法线"), FunctionInput_Vector3, FVector4f(0, 0, 1, 0), 10, -920, -160);
	UMaterialExpressionFunctionInput* Strength = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 20, -920, 20);
	UMaterialExpressionSaturate* Saturate = AddNode<UMaterialExpressionSaturate>(Function, -620, 20);
	Saturate->Input.Connect(0, Strength);
	UMaterialExpressionConstant3Vector* FlatNormal = AddNode<UMaterialExpressionConstant3Vector>(Function, -620, -170);
	FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
	UMaterialExpressionLinearInterpolate* Lerp = AddNode<UMaterialExpressionLinearInterpolate>(Function, -320, -120);
	Lerp->A.Connect(0, FlatNormal);
	Lerp->B.Connect(0, Normal);
	Lerp->Alpha.Connect(0, Saturate);
	AddOutput(Function, TEXT("法线"), Lerp, 10, 80, -120);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_NormalStrength");
	return true;
}

static bool BuildPBRBaseColorFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("02_Surface"), TEXT("MF_PBRStudio_BaseColor"), TEXT("Base color texture/tint/intensity paths."), TEXT("02 Surface"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("BaseColor: texture tint path and solid tint path"), -980, -260, 1380, 560);
	UMaterialExpressionFunctionInput* TextureColor = AddInput(Function, TEXT("贴图颜色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 10, -920, -160);
	UMaterialExpressionFunctionInput* Tint = AddInput(Function, TEXT("调色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 20, -920, 0);
	UMaterialExpressionFunctionInput* Intensity = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 30, -920, 160);
	UMaterialExpressionMultiply* TintedTexture = AddNode<UMaterialExpressionMultiply>(Function, -560, -120);
	TintedTexture->A.Connect(0, TextureColor);
	TintedTexture->B.Connect(0, Tint);
	UMaterialExpressionMultiply* TexturedColor = AddNode<UMaterialExpressionMultiply>(Function, -280, -120);
	TexturedColor->A.Connect(0, TintedTexture);
	TexturedColor->B.Connect(0, Intensity);
	UMaterialExpressionMultiply* SolidColor = AddNode<UMaterialExpressionMultiply>(Function, -280, 80);
	SolidColor->A.Connect(0, Tint);
	SolidColor->B.Connect(0, Intensity);
	AddOutput(Function, TEXT("贴图输出"), TexturedColor, 10, 120, -120);
	AddOutput(Function, TEXT("纯色输出"), SolidColor, 20, 120, 80);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_BaseColor");
	return true;
}

static bool BuildPBRScalarTextureFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("02_Surface"), TEXT("MF_PBRStudio_ScalarTexture"), TEXT("Scalar texture multiplied by strength, or solid value."), TEXT("02 Surface"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Scalar channel: TextureValue * Multiplier, or SolidValue"), -980, -240, 1320, 500);
	UMaterialExpressionFunctionInput* TextureValue = AddInput(Function, TEXT("贴图值"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 10, -920, -140);
	UMaterialExpressionFunctionInput* Multiplier = AddInput(Function, TEXT("倍增"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 20, -920, 0);
	UMaterialExpressionFunctionInput* SolidValue = AddInput(Function, TEXT("固定值"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 30, -920, 140);
	UMaterialExpressionMultiply* TexturedValue = AddNode<UMaterialExpressionMultiply>(Function, -520, -80);
	TexturedValue->A.Connect(0, TextureValue);
	TexturedValue->B.Connect(0, Multiplier);
	AddOutput(Function, TEXT("贴图输出"), TexturedValue, 10, -120, -80);
	AddOutput(Function, TEXT("固定输出"), SolidValue, 20, -120, 120);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_ScalarTexture");
	return true;
}

static bool BuildPBRHeightOffsetFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("02_Surface"), TEXT("MF_PBRStudio_HeightOffset"), TEXT("Centered height-map scalar offset."), TEXT("02 Surface"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Height Offset: (Height - 0.5) * Strength"), -980, -220, 1320, 420);
	UMaterialExpressionFunctionInput* Height = AddInput(Function, TEXT("高度值"), FunctionInput_Scalar, FVector4f(0.5f, 0, 0, 0), 10, -920, -120);
	UMaterialExpressionFunctionInput* Strength = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 20, -920, 40);
	UMaterialExpressionCustom* Offset = AddNode<UMaterialExpressionCustom>(Function, -520, -80);
	Offset->OutputType = CMOT_Float1;
	Offset->Description = TEXT("AR Height Offset");
	Offset->Code = TEXT("return (Height - 0.5) * Strength;");
	Offset->Inputs.AddDefaulted(2);
	Offset->Inputs[0].InputName = TEXT("Height");
	Offset->Inputs[0].Input.Connect(0, Height);
	Offset->Inputs[1].InputName = TEXT("Strength");
	Offset->Inputs[1].Input.Connect(0, Strength);
	AddOutput(Function, TEXT("偏移"), Offset, 10, -120, -80);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_HeightOffset");
	return true;
}

static bool BuildPBRGlassOpticsFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("04_Glass"), TEXT("MF_PBRStudio_GlassOptics"), TEXT("Compact glass opacity, refraction, and frosted roughness controls."), TEXT("04 Glass"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Glass Optics: opacity Fresnel, IOR refraction, and frosted roughness boost"), -1160, -360, 1760, 760);
	UMaterialExpressionFunctionInput* Opacity = AddInput(Function, TEXT("透明度"), FunctionInput_Scalar, FVector4f(0.35f, 0, 0, 0), 10, -1080, -260);
	UMaterialExpressionFunctionInput* RefractionIOR = AddInput(Function, TEXT("玻璃折射率"), FunctionInput_Scalar, FVector4f(1.45f, 0, 0, 0), 20, -1080, -140);
	UMaterialExpressionFunctionInput* OpacityFresnel = AddInput(Function, TEXT("透明菲涅尔强度"), FunctionInput_Scalar, FVector4f(0.35f, 0, 0, 0), 30, -1080, -20);
	UMaterialExpressionFunctionInput* FresnelBase = AddInput(Function, TEXT("菲涅尔基础反射"), FunctionInput_Scalar, FVector4f(0.02f, 0, 0, 0), 40, -1080, 100);
	UMaterialExpressionFunctionInput* FresnelExp = AddInput(Function, TEXT("菲涅尔指数"), FunctionInput_Scalar, FVector4f(5.0f, 0, 0, 0), 50, -1080, 220);
	UMaterialExpressionFunctionInput* FrostedStrength = AddInput(Function, TEXT("毛玻璃强度"), FunctionInput_Scalar, FVector4f(0.0f, 0, 0, 0), 60, -1080, 340);

	UMaterialExpressionSaturate* FresnelAmount = AddNode<UMaterialExpressionSaturate>(Function, -760, -20);
	FresnelAmount->Input.Connect(0, OpacityFresnel);
	UMaterialExpressionFresnel* Fresnel = AddNode<UMaterialExpressionFresnel>(Function, -760, 140);
	Fresnel->ExponentIn.Connect(0, FresnelExp);
	Fresnel->BaseReflectFractionIn.Connect(0, FresnelBase);
	UMaterialExpressionLinearInterpolate* OpacityWithFresnel = AddNode<UMaterialExpressionLinearInterpolate>(Function, -460, -180);
	OpacityWithFresnel->A.Connect(0, Opacity);
	OpacityWithFresnel->B.Connect(0, Fresnel);
	OpacityWithFresnel->Alpha.Connect(0, FresnelAmount);
	UMaterialExpressionSaturate* OpacityOutput = AddNode<UMaterialExpressionSaturate>(Function, -180, -180);
	OpacityOutput->Input.Connect(0, OpacityWithFresnel);
	UMaterialExpressionClamp* RefractionOutput = AddNode<UMaterialExpressionClamp>(Function, -180, -20);
	RefractionOutput->Input.Connect(0, RefractionIOR);
	RefractionOutput->MinDefault = 1.0f;
	RefractionOutput->MaxDefault = 2.4f;
	UMaterialExpressionSaturate* FrostedOutput = AddNode<UMaterialExpressionSaturate>(Function, -180, 160);
	FrostedOutput->Input.Connect(0, FrostedStrength);

	AddOutput(Function, TEXT("透明输出"), OpacityOutput, 10, 180, -180);
	AddOutput(Function, TEXT("折射输出"), RefractionOutput, 20, 180, -20);
	AddOutput(Function, TEXT("粗糙度叠加"), FrostedOutput, 30, 180, 160);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_GlassOptics");
	return true;
}

static bool BuildPBRWaterFlowUVFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("03_Water"), TEXT("MF_PBRStudio_WaterFlowUV"), TEXT("Water ripple UV panner."), TEXT("03 Water"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Water Flow UV: UV * Scale + Time * Speed"), -1120, -280, 1640, 640);
	UMaterialExpressionFunctionInput* UV = AddInput(Function, TEXT("UV"), FunctionInput_Vector2, FVector4f(0, 0, 0, 0), 10, -1080, -200);
	UMaterialExpressionFunctionInput* Time = AddInput(Function, TEXT("Time"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 20, -1080, -80);
	UMaterialExpressionFunctionInput* SpeedU = AddInput(Function, TEXT("U 速度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 30, -1080, 40);
	UMaterialExpressionFunctionInput* SpeedV = AddInput(Function, TEXT("V 速度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 40, -1080, 160);
	UMaterialExpressionFunctionInput* Scale = AddInput(Function, TEXT("缩放"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 50, -1080, 280);
	UMaterialExpressionAppendVector* Speed = AddNode<UMaterialExpressionAppendVector>(Function, -760, 20);
	Speed->A.Connect(0, SpeedU);
	Speed->B.Connect(0, SpeedV);
	UMaterialExpressionMultiply* ScaledUV = AddNode<UMaterialExpressionMultiply>(Function, -520, -170);
	ScaledUV->A.Connect(0, UV);
	ScaledUV->B.Connect(0, Scale);
	UMaterialExpressionMultiply* FlowOffset = AddNode<UMaterialExpressionMultiply>(Function, -520, 60);
	FlowOffset->A.Connect(0, Time);
	FlowOffset->B.Connect(0, Speed);
	UMaterialExpressionAdd* Result = AddNode<UMaterialExpressionAdd>(Function, -240, -120);
	Result->A.Connect(0, ScaledUV);
	Result->B.Connect(0, FlowOffset);
	AddOutput(Function, TEXT("UV"), Result, 10, 120, -120);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_WaterFlowUV");
	return true;
}

static bool BuildPBRWaterRippleMaskFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("03_Water"), TEXT("MF_PBRStudio_WaterRippleMask"), TEXT("Procedural water ripple brightness mask."), TEXT("03 Water"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Water Ripple Mask: procedural fallback when no ripple texture is used"), -1120, -320, 1680, 720);
	UMaterialExpressionFunctionInput* UV = AddInput(Function, TEXT("UV"), FunctionInput_Vector2, FVector4f(0, 0, 0, 0), 10, -1080, -220);
	UMaterialExpressionFunctionInput* Time = AddInput(Function, TEXT("Time"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 20, -1080, -100);
	UMaterialExpressionFunctionInput* FlowU = AddInput(Function, TEXT("U 速度"), FunctionInput_Scalar, FVector4f(PBRDefaultWaterFlowSpeedU, 0, 0, 0), 30, -1080, 20);
	UMaterialExpressionFunctionInput* FlowV = AddInput(Function, TEXT("V 速度"), FunctionInput_Scalar, FVector4f(PBRDefaultWaterFlowSpeedV, 0, 0, 0), 40, -1080, 140);
	UMaterialExpressionFunctionInput* Scale = AddInput(Function, TEXT("缩放"), FunctionInput_Scalar, FVector4f(PBRDefaultWaterRippleScale, 0, 0, 0), 50, -1080, 260);
	UMaterialExpressionFunctionInput* Strength = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(PBRDefaultWaterRippleStrength, 0, 0, 0), 60, -1080, 380);
	UMaterialExpressionCustom* Mask = AddNode<UMaterialExpressionCustom>(Function, -620, -80);
	Mask->OutputType = CMOT_Float1;
	Mask->Description = TEXT("PBRStudio Water Ripple Mask");
	Mask->Code = TEXT("float2 movedUV = UV + Time * float2(FlowU, FlowV);\nfloat waveScale = max(Scale, 0.01) * 6.2831853;\nfloat waves = (sin(movedUV.x * waveScale) + sin(movedUV.y * waveScale * 1.37)) * 0.25 + 0.5;\nreturn saturate(waves) * Strength * 1.5;");
	Mask->Inputs.AddDefaulted(6);
	Mask->Inputs[0].InputName = TEXT("UV");
	Mask->Inputs[0].Input.Connect(0, UV);
	Mask->Inputs[1].InputName = TEXT("Time");
	Mask->Inputs[1].Input.Connect(0, Time);
	Mask->Inputs[2].InputName = TEXT("FlowU");
	Mask->Inputs[2].Input.Connect(0, FlowU);
	Mask->Inputs[3].InputName = TEXT("FlowV");
	Mask->Inputs[3].Input.Connect(0, FlowV);
	Mask->Inputs[4].InputName = TEXT("Scale");
	Mask->Inputs[4].Input.Connect(0, Scale);
	Mask->Inputs[5].InputName = TEXT("Strength");
	Mask->Inputs[5].Input.Connect(0, Strength);
	AddOutput(Function, TEXT("水纹"), Mask, 10, -160, -80);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_WaterRippleMask");
	return true;
}

static bool BuildPBRWaterRippleNormalFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("03_Water"), TEXT("MF_PBRStudio_WaterRippleNormal"), TEXT("Procedural tangent-space water ripple normal."), TEXT("03 Water"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Water Ripple Normal: compact procedural normal for water material"), -1120, -320, 1680, 720);
	UMaterialExpressionFunctionInput* UV = AddInput(Function, TEXT("UV"), FunctionInput_Vector2, FVector4f(0, 0, 0, 0), 10, -1080, -220);
	UMaterialExpressionFunctionInput* Time = AddInput(Function, TEXT("Time"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 20, -1080, -100);
	UMaterialExpressionFunctionInput* FlowU = AddInput(Function, TEXT("U 速度"), FunctionInput_Scalar, FVector4f(PBRDefaultWaterFlowSpeedU, 0, 0, 0), 30, -1080, 20);
	UMaterialExpressionFunctionInput* FlowV = AddInput(Function, TEXT("V 速度"), FunctionInput_Scalar, FVector4f(PBRDefaultWaterFlowSpeedV, 0, 0, 0), 40, -1080, 140);
	UMaterialExpressionFunctionInput* Scale = AddInput(Function, TEXT("缩放"), FunctionInput_Scalar, FVector4f(PBRDefaultWaterRippleScale, 0, 0, 0), 50, -1080, 260);
	UMaterialExpressionFunctionInput* Strength = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(PBRDefaultWaterRippleStrength, 0, 0, 0), 60, -1080, 380);
	UMaterialExpressionCustom* Normal = AddNode<UMaterialExpressionCustom>(Function, -620, -80);
	Normal->OutputType = CMOT_Float3;
	Normal->Description = TEXT("PBRStudio Water Ripple Normal");
	Normal->Code = TEXT("float2 movedUV = UV + Time * float2(FlowU, FlowV);\nfloat waveScale = max(Scale, 0.01) * 6.2831853;\nfloat dx = cos(dot(movedUV, float2(1.0, 0.32)) * waveScale) * waveScale;\ndx += cos(dot(movedUV * 1.73 + 0.19, float2(-0.56, 0.83)) * waveScale * 0.61) * waveScale * 0.61;\nfloat dy = cos(dot(movedUV, float2(-0.27, 1.0)) * waveScale * 0.87) * waveScale * 0.87;\ndy += cos(dot(movedUV * 1.31 + 0.43, float2(0.72, 0.41)) * waveScale * 0.49) * waveScale * 0.49;\nfloat normalScale = Strength * 0.006;\nreturn normalize(float3(-dx * normalScale, dy * normalScale, 1.0));");
	Normal->Inputs.AddDefaulted(6);
	Normal->Inputs[0].InputName = TEXT("UV");
	Normal->Inputs[0].Input.Connect(0, UV);
	Normal->Inputs[1].InputName = TEXT("Time");
	Normal->Inputs[1].Input.Connect(0, Time);
	Normal->Inputs[2].InputName = TEXT("FlowU");
	Normal->Inputs[2].Input.Connect(0, FlowU);
	Normal->Inputs[3].InputName = TEXT("FlowV");
	Normal->Inputs[3].Input.Connect(0, FlowV);
	Normal->Inputs[4].InputName = TEXT("Scale");
	Normal->Inputs[4].Input.Connect(0, Scale);
	Normal->Inputs[5].InputName = TEXT("Strength");
	Normal->Inputs[5].Input.Connect(0, Strength);
	AddOutput(Function, TEXT("法线"), Normal, 10, -160, -80);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_WaterRippleNormal");
	return true;
}

static bool BuildBaseColorBlendFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("03_Surface"), TEXT("MF_PBRStudio_BaseColorBlend"), TEXT("Base color texture/tint/intensity outputs."), TEXT("03 Surface"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Base Color: texture path and solid color path"), -980, -260, 1380, 560);
	UMaterialExpressionFunctionInput* TextureColor = AddInput(Function, TEXT("贴图颜色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 10, -920, -160);
	UMaterialExpressionFunctionInput* Tint = AddInput(Function, TEXT("调色"), FunctionInput_Vector3, FVector4f(1, 1, 1, 1), 20, -920, 0);
	UMaterialExpressionFunctionInput* Intensity = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 30, -920, 160);
	UMaterialExpressionMultiply* TintedTexture = AddNode<UMaterialExpressionMultiply>(Function, -560, -120);
	TintedTexture->A.Connect(0, TextureColor);
	TintedTexture->B.Connect(0, Tint);
	UMaterialExpressionMultiply* TexturedColor = AddNode<UMaterialExpressionMultiply>(Function, -280, -120);
	TexturedColor->A.Connect(0, TintedTexture);
	TexturedColor->B.Connect(0, Intensity);
	UMaterialExpressionMultiply* SolidColor = AddNode<UMaterialExpressionMultiply>(Function, -280, 80);
	SolidColor->A.Connect(0, Tint);
	SolidColor->B.Connect(0, Intensity);
	AddOutput(Function, TEXT("贴图颜色输出"), TexturedColor, 10, 120, -120);
	AddOutput(Function, TEXT("纯色输出"), SolidColor, 20, 120, 80);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_BaseColorBlend");
	return true;
}

static bool BuildScalarTextureSwitchFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("04_Mask"), TEXT("MF_PBRStudio_ScalarTextureSwitch"), TEXT("Scalar texture multiplier and solid value outputs."), TEXT("04 Mask"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Scalar Channel: texture * multiplier, or solid value"), -980, -240, 1320, 500);
	UMaterialExpressionFunctionInput* TextureValue = AddInput(Function, TEXT("贴图值"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 10, -920, -140);
	UMaterialExpressionFunctionInput* Multiplier = AddInput(Function, TEXT("倍增"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 20, -920, 0);
	UMaterialExpressionFunctionInput* SolidValue = AddInput(Function, TEXT("固定值"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 30, -920, 140);
	UMaterialExpressionMultiply* TexturedValue = AddNode<UMaterialExpressionMultiply>(Function, -520, -80);
	TexturedValue->A.Connect(0, TextureValue);
	TexturedValue->B.Connect(0, Multiplier);
	AddOutput(Function, TEXT("贴图输出"), TexturedValue, 10, -120, -80);
	AddOutput(Function, TEXT("固定输出"), SolidValue, 20, -120, 120);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_ScalarTextureSwitch");
	return true;
}

static bool BuildHeightDisplacementFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("06_Displacement"), TEXT("MF_PBRStudio_HeightDisplacement"), TEXT("Height map to world offset and pixel depth offset."), TEXT("06 Displacement"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("Height: height * strength -> WPO and PDO"), -1040, -300, 1580, 620);
	UMaterialExpressionFunctionInput* Height = AddInput(Function, TEXT("高度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 10, -980, -180);
	UMaterialExpressionFunctionInput* HeightStrength = AddInput(Function, TEXT("高度强度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 20, -980, -40);
	UMaterialExpressionFunctionInput* PixelDepthStrength = AddInput(Function, TEXT("深度偏移强度"), FunctionInput_Scalar, FVector4f(0, 0, 0, 0), 30, -980, 100);
	UMaterialExpressionFunctionInput* VertexNormal = AddInput(Function, TEXT("顶点法线"), FunctionInput_Vector3, FVector4f(0, 0, 1, 0), 40, -980, 240);
	UMaterialExpressionMultiply* HeightAmount = AddNode<UMaterialExpressionMultiply>(Function, -600, -120);
	HeightAmount->A.Connect(0, Height);
	HeightAmount->B.Connect(0, HeightStrength);
	UMaterialExpressionMultiply* WorldOffset = AddNode<UMaterialExpressionMultiply>(Function, -280, -120);
	WorldOffset->A.Connect(0, VertexNormal);
	WorldOffset->B.Connect(0, HeightAmount);
	UMaterialExpressionMultiply* PixelDepthOffset = AddNode<UMaterialExpressionMultiply>(Function, -280, 80);
	PixelDepthOffset->A.Connect(0, HeightAmount);
	PixelDepthOffset->B.Connect(0, PixelDepthStrength);
	UMaterialExpressionConstant3Vector* NoWorldOffset = AddNode<UMaterialExpressionConstant3Vector>(Function, -280, 260);
	NoWorldOffset->Constant = FLinearColor::Black;
	AddOutput(Function, TEXT("世界偏移"), WorldOffset, 10, 120, -120);
	AddOutput(Function, TEXT("像素深度偏移"), PixelDepthOffset, 20, 120, 80);
	AddOutput(Function, TEXT("无世界偏移"), NoWorldOffset, 30, 120, 260);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_HeightDisplacement");
	return true;
}

static bool BuildMaskClipFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("04_Mask"), TEXT("MF_PBRStudio_MaskClip"), TEXT("遮罩阈值和软化控制。"), TEXT("04 Mask"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("遮罩: saturate((Mask - Threshold) / Softness)"), -1000, -260, 1460, 540);
	UMaterialExpressionFunctionInput* Mask = AddInput(Function, TEXT("遮罩"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 10, -940, -160);
	UMaterialExpressionFunctionInput* Threshold = AddInput(Function, TEXT("阈值"), FunctionInput_Scalar, FVector4f(0.5f, 0, 0, 0), 20, -940, -20);
	UMaterialExpressionFunctionInput* Softness = AddInput(Function, TEXT("软化"), FunctionInput_Scalar, FVector4f(0.05f, 0, 0, 0), 30, -940, 120);
	UMaterialExpressionSubtract* Subtract = AddNode<UMaterialExpressionSubtract>(Function, -620, -120);
	Subtract->A.Connect(0, Mask);
	Subtract->B.Connect(0, Threshold);
	UMaterialExpressionDivide* Divide = AddNode<UMaterialExpressionDivide>(Function, -340, -100);
	Divide->A.Connect(0, Subtract);
	Divide->B.Connect(0, Softness);
	UMaterialExpressionSaturate* Saturate = AddNode<UMaterialExpressionSaturate>(Function, -100, -100);
	Saturate->Input.Connect(0, Divide);
	AddOutput(Function, TEXT("遮罩"), Saturate, 10, 180, -100);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_MaskClip");
	return true;
}

static bool BuildFresnelGlowFunction(FString& OutMessage)
{
	UMaterialFunction* Function = CreateOrResetFunction(TEXT("05_SpecialFX"), TEXT("MF_PBRStudio_FresnelGlow"), TEXT("菲涅尔边缘光颜色和强度。"), TEXT("05 Special FX"));
	if (!Function) { return false; }

	AddComment(Function, TEXT("特殊效果: Fresnel * Color * Intensity"), -980, -260, 1380, 560);
	UMaterialExpressionFunctionInput* Color = AddInput(Function, TEXT("颜色"), FunctionInput_Vector3, FVector4f(0.1f, 0.75f, 1.0f, 1), 10, -920, -140);
	UMaterialExpressionFunctionInput* Intensity = AddInput(Function, TEXT("强度"), FunctionInput_Scalar, FVector4f(1, 0, 0, 0), 20, -920, 0);
	UMaterialExpressionFunctionInput* Exponent = AddInput(Function, TEXT("衰减"), FunctionInput_Scalar, FVector4f(5, 0, 0, 0), 30, -920, 140);
	UMaterialExpressionFresnel* Fresnel = AddNode<UMaterialExpressionFresnel>(Function, -580, -100);
	Fresnel->ExponentIn.Connect(0, Exponent);
	UMaterialExpressionMultiply* EdgeColor = AddNode<UMaterialExpressionMultiply>(Function, -300, -100);
	EdgeColor->A.Connect(0, Fresnel);
	EdgeColor->B.Connect(0, Color);
	UMaterialExpressionMultiply* Glow = AddNode<UMaterialExpressionMultiply>(Function, -40, -100);
	Glow->A.Connect(0, EdgeColor);
	Glow->B.Connect(0, Intensity);
	AddOutput(Function, TEXT("边缘光"), Glow, 10, 240, -100);
	AddOutput(Function, TEXT("菲涅尔"), Fresnel, 20, 240, 80);

	SaveFunction(Function);
	OutMessage = TEXT("已创建/更新函数: MF_PBRStudio_FresnelGlow");
	return true;
}

int32 FPBRMaterialFunctionLibrary::EnsureAllMaterialFunctions(TArray<FString>& OutMessages)
{
	int32 Count = 0;
	FString Message;
	if (BuildUVControlsFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildPBRBaseColorFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildPBRScalarTextureFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildDynamicPannerFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildTextureTintFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildNormalStrengthFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildPBRHeightOffsetFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildPBRGlassOpticsFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildPBRWaterFlowUVFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildPBRWaterRippleMaskFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildPBRWaterRippleNormalFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildBaseColorBlendFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildScalarTextureSwitchFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildMaskClipFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildFresnelGlowFunction(Message)) { ++Count; OutMessages.Add(Message); }
	if (BuildHeightDisplacementFunction(Message)) { ++Count; OutMessages.Add(Message); }
	return Count;
}
