#include "Widgets/SPBRSceneBatchAdjustTab.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SPBRSceneBatchAdjustTab"

void SPBRSceneBatchAdjustTab::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(8)
		[
			SNew(STextBlock).Text(LOCTEXT("Title", "批量调节"))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock).Text(LOCTEXT("Hint", "集中调节场景中的灯光和相机。转换功能仍在“灯光转换”和“相机转换”里。"))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8)
		[
			SNew(SBox)
			.MinDesiredHeight(150)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock).Text(LOCTEXT("LightHeader", "灯光批量调节"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(LightRectCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("LightRect", "矩形灯"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(LightPointCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("LightPoint", "点光"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(LightSpotCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("LightSpot", "射灯"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(LightDirectionalCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightDirectional", "太阳光"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(LightSkyCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightSky", "天空光"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(LightOnlySelectedCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightOnlySelected", "只调选中"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(LightSkipPBRStudioCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightSkipPBR", "跳过PBRStudio"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(LightClearProjectionCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("LightClearProjection", "清理投影/IES/Preview"))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SAssignNew(LightIntensityCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("LightIntensity", "亮度倍率"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(70).MinValue(0.0f).MaxValue(100.0f)
						.Value_Lambda([this]() { return LightIntensityMultiplier; })
						.OnValueChanged_Lambda([this](float Value) { LightIntensityMultiplier = FMath::Max(0.0f, Value); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SAssignNew(LightTemperatureCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightTemp", "色温K"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(74).MinValue(1700.0f).MaxValue(12000.0f)
						.Value_Lambda([this]() { return LightTemperature; })
						.OnValueChanged_Lambda([this](float Value) { LightTemperature = FMath::Clamp(Value, 1700.0f, 12000.0f); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SAssignNew(LightAttenuationCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightAttenuation", "照射范围"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(76).MinValue(1.0f).MaxValue(100000.0f)
						.Value_Lambda([this]() { return LightAttenuationRadius; })
						.OnValueChanged_Lambda([this](float Value) { LightAttenuationRadius = FMath::Max(1.0f, Value); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SAssignNew(LightSourceRadiusCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightSourceRadius", "源半径"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(70).MinValue(0.0f).MaxValue(10000.0f)
						.Value_Lambda([this]() { return LightSourceRadius; })
						.OnValueChanged_Lambda([this](float Value) { LightSourceRadius = FMath::Max(0.0f, Value); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SAssignNew(LightRectSizeCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightRectSize", "矩形尺寸"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(66).MinValue(1.0f).MaxValue(100000.0f)
						.Value_Lambda([this]() { return LightRectWidth; })
						.OnValueChanged_Lambda([this](float Value) { LightRectWidth = FMath::Max(1.0f, Value); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("LightRectX", "x"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(66).MinValue(1.0f).MaxValue(100000.0f)
						.Value_Lambda([this]() { return LightRectHeight; })
						.OnValueChanged_Lambda([this](float Value) { LightRectHeight = FMath::Max(1.0f, Value); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SAssignNew(LightSpotConeCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightSpotCone", "射灯内/外角"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(0.0f).MaxValue(80.0f)
						.Value_Lambda([this]() { return LightSpotInnerCone; })
						.OnValueChanged_Lambda([this](float Value) { LightSpotInnerCone = FMath::Clamp(Value, 0.0f, 80.0f); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("LightSpotSlash", "/"))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(1.0f).MaxValue(80.0f)
						.Value_Lambda([this]() { return LightSpotOuterCone; })
						.OnValueChanged_Lambda([this](float Value) { LightSpotOuterCone = FMath::Clamp(Value, 1.0f, 80.0f); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SAssignNew(LightColorCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("LightColor", "颜色 RGB"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(54).MinValue(0.0f).MaxValue(1.0f)
						.Value_Lambda([this]() { return LightColor.R; })
						.OnValueChanged_Lambda([this](float Value) { LightColor.R = FMath::Clamp(Value, 0.0f, 1.0f); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(54).MinValue(0.0f).MaxValue(1.0f)
						.Value_Lambda([this]() { return LightColor.G; })
						.OnValueChanged_Lambda([this](float Value) { LightColor.G = FMath::Clamp(Value, 0.0f, 1.0f); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(54).MinValue(0.0f).MaxValue(1.0f)
						.Value_Lambda([this]() { return LightColor.B; })
						.OnValueChanged_Lambda([this](float Value) { LightColor.B = FMath::Clamp(Value, 0.0f, 1.0f); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("ApplyLight", "应用灯光调节"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
						.OnClicked(this, &SPBRSceneBatchAdjustTab::OnAdjustLights)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("InteriorSunSky", "一键室内SunSky"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Warning")
						.OnClicked(this, &SPBRSceneBatchAdjustTab::OnApplyInteriorSunSky)
					]
				]
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8)
		[
			SNew(SBox)
			.MinDesiredHeight(140)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock).Text(LOCTEXT("CameraHeader", "相机批量调节"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(CameraCineCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraCine", "电影相机"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(CameraRegularCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraRegular", "普通相机"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SAssignNew(CameraOnlySelectedCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraOnlySelected", "只调选中"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(CameraSkipPBRStudioCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraSkipPBR", "跳过PBRStudio"))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
					[
						SAssignNew(CameraFilmbackCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraFilmback", "胶片"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(1.0f)
						.Value_Lambda([this]() { return SensorWidth; })
						.OnValueChanged_Lambda([this](float Value) { SensorWidth = FMath::Max(1.0f, Value); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("CameraFilmbackX", "x"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(1.0f)
						.Value_Lambda([this]() { return SensorHeight; })
						.OnValueChanged_Lambda([this](float Value) { SensorHeight = FMath::Max(1.0f, Value); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
					[
						SAssignNew(CameraFocalLengthCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraFocal", "焦距"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(1.0f)
						.Value_Lambda([this]() { return FocalLength; })
						.OnValueChanged_Lambda([this](float Value) { FocalLength = FMath::Max(1.0f, Value); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
					[
						SAssignNew(CameraApertureCheck, SCheckBox).IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraAperture", "光圈"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(0.1f)
						.Value_Lambda([this]() { return Aperture; })
						.OnValueChanged_Lambda([this](float Value) { Aperture = FMath::Max(0.1f, Value); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
					[
						SAssignNew(CameraFocusCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraFocus", "对焦cm"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(70).MinValue(1.0f)
						.Value_Lambda([this]() { return ManualFocusDistance; })
						.OnValueChanged_Lambda([this](float Value) { ManualFocusDistance = FMath::Max(1.0f, Value); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
					[
						SAssignNew(CameraExposureCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraExposure", "曝光"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(-15.0f).MaxValue(15.0f)
						.Value_Lambda([this]() { return ExposureCompensation; })
						.OnValueChanged_Lambda([this](float Value) { ExposureCompensation = FMath::Clamp(Value, -15.0f, 15.0f); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
					[
						SAssignNew(CameraWhiteTempCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraWhiteTemp", "色温K"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(70).MinValue(1500.0f).MaxValue(15000.0f)
						.Value_Lambda([this]() { return WhiteTemp; })
						.OnValueChanged_Lambda([this](float Value) { WhiteTemp = FMath::Clamp(Value, 1500.0f, 15000.0f); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
					[
						SAssignNew(CameraMotionBlurCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraMotionBlur", "动态模糊"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(0.0f).MaxValue(1.0f)
						.Value_Lambda([this]() { return MotionBlurAmount; })
						.OnValueChanged_Lambda([this](float Value) { MotionBlurAmount = FMath::Clamp(Value, 0.0f, 1.0f); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
					[
						SAssignNew(CameraVignetteCheck, SCheckBox)
						[
							SNew(STextBlock).Text(LOCTEXT("CameraVignette", "暗角"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
					[
						SNew(SNumericEntryBox<float>).MinDesiredValueWidth(58).MinValue(0.0f).MaxValue(1.0f)
						.Value_Lambda([this]() { return VignetteIntensity; })
						.OnValueChanged_Lambda([this](float Value) { VignetteIntensity = FMath::Clamp(Value, 0.0f, 1.0f); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("ApplyCamera", "应用相机调节"))
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
						.OnClicked(this, &SPBRSceneBatchAdjustTab::OnAdjustCameras)
					]
				]
			]
		]

		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SSpacer)
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SAssignNew(StatusText, STextBlock).Text(LOCTEXT("Ready", "准备就绪"))
		]
	];
}

FPBRSceneLightBatchAdjustSettings SPBRSceneBatchAdjustTab::BuildLightSettings() const
{
	FPBRSceneLightBatchAdjustSettings Settings;
	Settings.bOnlySelectedActors = LightOnlySelectedCheck.IsValid() && LightOnlySelectedCheck->IsChecked();
	Settings.bAffectRectLights = !LightRectCheck.IsValid() || LightRectCheck->IsChecked();
	Settings.bAffectPointLights = !LightPointCheck.IsValid() || LightPointCheck->IsChecked();
	Settings.bAffectSpotLights = !LightSpotCheck.IsValid() || LightSpotCheck->IsChecked();
	Settings.bAffectDirectionalLights = LightDirectionalCheck.IsValid() && LightDirectionalCheck->IsChecked();
	Settings.bAffectSkyLights = LightSkyCheck.IsValid() && LightSkyCheck->IsChecked();
	Settings.bSkipPBRStudioLights = LightSkipPBRStudioCheck.IsValid() && LightSkipPBRStudioCheck->IsChecked();
	Settings.bClearProjectionSettings = !LightClearProjectionCheck.IsValid() || LightClearProjectionCheck->IsChecked();
	Settings.bSetIntensityMultiplier = !LightIntensityCheck.IsValid() || LightIntensityCheck->IsChecked();
	Settings.bSetLightColor = LightColorCheck.IsValid() && LightColorCheck->IsChecked();
	Settings.bSetTemperature = LightTemperatureCheck.IsValid() && LightTemperatureCheck->IsChecked();
	Settings.bSetAttenuationRadius = LightAttenuationCheck.IsValid() && LightAttenuationCheck->IsChecked();
	Settings.bSetSourceRadius = LightSourceRadiusCheck.IsValid() && LightSourceRadiusCheck->IsChecked();
	Settings.bSetRectSize = LightRectSizeCheck.IsValid() && LightRectSizeCheck->IsChecked();
	Settings.bSetSpotCone = LightSpotConeCheck.IsValid() && LightSpotConeCheck->IsChecked();
	Settings.IntensityMultiplier = FMath::Max(0.0f, LightIntensityMultiplier);
	Settings.LightColor = LightColor;
	Settings.Temperature = FMath::Clamp(LightTemperature, 1700.0f, 12000.0f);
	Settings.AttenuationRadius = FMath::Max(1.0f, LightAttenuationRadius);
	Settings.SourceRadius = FMath::Max(0.0f, LightSourceRadius);
	Settings.SourceWidth = FMath::Max(1.0f, LightRectWidth);
	Settings.SourceHeight = FMath::Max(1.0f, LightRectHeight);
	Settings.InnerConeAngle = FMath::Clamp(LightSpotInnerCone, 0.0f, 80.0f);
	Settings.OuterConeAngle = FMath::Clamp(LightSpotOuterCone, 1.0f, 80.0f);
	return Settings;
}

FPBRSceneCameraBatchAdjustSettings SPBRSceneBatchAdjustTab::BuildCameraSettings() const
{
	FPBRSceneCameraBatchAdjustSettings Settings;
	Settings.bOnlySelectedActors = CameraOnlySelectedCheck.IsValid() && CameraOnlySelectedCheck->IsChecked();
	Settings.bAffectCineCameras = !CameraCineCheck.IsValid() || CameraCineCheck->IsChecked();
	Settings.bAffectRegularCameras = !CameraRegularCheck.IsValid() || CameraRegularCheck->IsChecked();
	Settings.bSkipPBRStudioCameras = CameraSkipPBRStudioCheck.IsValid() && CameraSkipPBRStudioCheck->IsChecked();
	Settings.bSetFilmback = CameraFilmbackCheck.IsValid() && CameraFilmbackCheck->IsChecked();
	Settings.bSetFocalLength = !CameraFocalLengthCheck.IsValid() || CameraFocalLengthCheck->IsChecked();
	Settings.bSetAperture = !CameraApertureCheck.IsValid() || CameraApertureCheck->IsChecked();
	Settings.bSetFocusDistance = CameraFocusCheck.IsValid() && CameraFocusCheck->IsChecked();
	Settings.bSetExposure = CameraExposureCheck.IsValid() && CameraExposureCheck->IsChecked();
	Settings.bSetWhiteTemp = CameraWhiteTempCheck.IsValid() && CameraWhiteTempCheck->IsChecked();
	Settings.bSetMotionBlur = CameraMotionBlurCheck.IsValid() && CameraMotionBlurCheck->IsChecked();
	Settings.bSetVignette = CameraVignetteCheck.IsValid() && CameraVignetteCheck->IsChecked();
	Settings.SensorWidth = FMath::Max(1.0f, SensorWidth);
	Settings.SensorHeight = FMath::Max(1.0f, SensorHeight);
	Settings.FocalLength = FMath::Max(1.0f, FocalLength);
	Settings.Aperture = FMath::Max(0.1f, Aperture);
	Settings.ManualFocusDistance = FMath::Max(1.0f, ManualFocusDistance);
	Settings.ExposureCompensation = FMath::Clamp(ExposureCompensation, -15.0f, 15.0f);
	Settings.WhiteTemp = FMath::Clamp(WhiteTemp, 1500.0f, 15000.0f);
	Settings.MotionBlurAmount = FMath::Clamp(MotionBlurAmount, 0.0f, 1.0f);
	Settings.VignetteIntensity = FMath::Clamp(VignetteIntensity, 0.0f, 1.0f);
	return Settings;
}

FReply SPBRSceneBatchAdjustTab::OnAdjustLights()
{
	FPBRSceneLightConvertResult Result;
	FPBRSceneLightConverter::BatchAdjustSceneLights(BuildLightSettings(), Result);
	SetStatus(Result.Messages.Num() > 0 ? Result.Messages.Last() : TEXT("已调节场景灯光"));
	return FReply::Handled();
}

FReply SPBRSceneBatchAdjustTab::OnApplyInteriorSunSky()
{
	FPBRSceneLightBatchAdjustSettings Settings;
	Settings.bAffectRectLights = false;
	Settings.bAffectPointLights = false;
	Settings.bAffectSpotLights = false;
	Settings.bAffectDirectionalLights = true;
	Settings.bAffectSkyLights = true;
	Settings.bSkipPBRStudioLights = false;
	Settings.bClearProjectionSettings = false;
	Settings.bSetIntensityMultiplier = false;
	Settings.bUseInteriorSunSkyPreset = true;
	Settings.InteriorDirectionalIntensity = 3.0f;
	Settings.InteriorSkyLightIntensity = 0.25f;

	FPBRSceneLightConvertResult Result;
	FPBRSceneLightConverter::BatchAdjustSceneLights(Settings, Result);
	SetStatus(Result.Messages.Num() > 0 ? Result.Messages.Last() : TEXT("已应用室内 SunSky 设置"));
	return FReply::Handled();
}

FReply SPBRSceneBatchAdjustTab::OnAdjustCameras()
{
	FPBRSceneCameraConvertResult Result;
	FPBRSceneCameraConverter::BatchAdjustSceneCameras(BuildCameraSettings(), Result);
	SetStatus(Result.Messages.Num() > 0 ? Result.Messages.Last() : TEXT("已调节场景相机"));
	return FReply::Handled();
}

void SPBRSceneBatchAdjustTab::SetStatus(const FString& Message)
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Message));
	}
}

#undef LOCTEXT_NAMESPACE
