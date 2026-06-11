#pragma once

#include "CoreMinimal.h"
#include "Services/PBRSceneCameraConverter.h"
#include "Services/PBRSceneLightConverter.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class STextBlock;

class SPBRSceneBatchAdjustTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRSceneBatchAdjustTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<STextBlock> StatusText;

	TSharedPtr<SCheckBox> LightRectCheck;
	TSharedPtr<SCheckBox> LightPointCheck;
	TSharedPtr<SCheckBox> LightSpotCheck;
	TSharedPtr<SCheckBox> LightOnlySelectedCheck;
	TSharedPtr<SCheckBox> LightSkipPBRStudioCheck;
	TSharedPtr<SCheckBox> LightClearProjectionCheck;
	TSharedPtr<SCheckBox> LightIntensityCheck;
	TSharedPtr<SCheckBox> LightColorCheck;
	TSharedPtr<SCheckBox> LightTemperatureCheck;
	TSharedPtr<SCheckBox> LightAttenuationCheck;
	TSharedPtr<SCheckBox> LightSourceRadiusCheck;
	TSharedPtr<SCheckBox> LightRectSizeCheck;
	TSharedPtr<SCheckBox> LightSpotConeCheck;

	TSharedPtr<SCheckBox> CameraCineCheck;
	TSharedPtr<SCheckBox> CameraRegularCheck;
	TSharedPtr<SCheckBox> CameraOnlySelectedCheck;
	TSharedPtr<SCheckBox> CameraSkipPBRStudioCheck;
	TSharedPtr<SCheckBox> CameraFilmbackCheck;
	TSharedPtr<SCheckBox> CameraFocalLengthCheck;
	TSharedPtr<SCheckBox> CameraApertureCheck;
	TSharedPtr<SCheckBox> CameraFocusCheck;
	TSharedPtr<SCheckBox> CameraExposureCheck;
	TSharedPtr<SCheckBox> CameraWhiteTempCheck;
	TSharedPtr<SCheckBox> CameraMotionBlurCheck;
	TSharedPtr<SCheckBox> CameraVignetteCheck;

	float LightIntensityMultiplier = 1.0f;
	FLinearColor LightColor = FLinearColor::White;
	float LightTemperature = 6500.0f;
	float LightAttenuationRadius = 1000.0f;
	float LightSourceRadius = 0.0f;
	float LightRectWidth = 64.0f;
	float LightRectHeight = 64.0f;
	float LightSpotInnerCone = 15.0f;
	float LightSpotOuterCone = 44.0f;

	float SensorWidth = 36.0f;
	float SensorHeight = 20.25f;
	float FocalLength = 35.0f;
	float Aperture = 5.6f;
	float ManualFocusDistance = 1000.0f;
	float ExposureCompensation = 0.0f;
	float WhiteTemp = 6500.0f;
	float MotionBlurAmount = 0.0f;
	float VignetteIntensity = 0.0f;

	FReply OnAdjustLights();
	FReply OnAdjustCameras();
	FPBRSceneLightBatchAdjustSettings BuildLightSettings() const;
	FPBRSceneCameraBatchAdjustSettings BuildCameraSettings() const;
	void SetStatus(const FString& Message);
};
