#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"

class AActor;
class ACineCameraActor;
class UCameraComponent;
class UCineCameraComponent;

enum class EPBRSceneCameraSourceType : uint8
{
	UECamera,
	CineCamera,
	ImportedVRayCorona,
	DatasmithCamera
};

struct PBRSTUDIO_API FPBRSceneCameraCandidate
{
	FString SourceName;
	FString SourcePath;
	FString OutputName;
	FString SourceTypeLabel;
	FString Status;
	TWeakObjectPtr<AActor> SourceActor;
	TWeakObjectPtr<UCameraComponent> SourceCameraComponent;
	FTransform SourceTransform = FTransform::Identity;
	float SourceFOV = 90.0f;
	float SourceAspectRatio = 1.777778f;
	EPBRSceneCameraSourceType SourceType = EPBRSceneCameraSourceType::DatasmithCamera;
	bool bCanConvert = true;
	bool bChecked = true;
	bool bIsPBRStudioCamera = false;
};

struct PBRSTUDIO_API FPBRSceneCameraConvertSettings
{
	bool bOnlySelectedActors = false;
	bool bHideOriginalActors = true;
	bool bSkipPBRStudioCameras = true;
	bool bResetToDefaults = true;
	FName FolderPath = TEXT("PBRStudio/ConvertedCameras");
	float SensorWidth = 36.0f;
	float SensorHeight = 20.25f;
	float FocalLength = 35.0f;
	float Aperture = 5.6f;
	float ManualFocusDistance = 1000.0f;
	float ExposureCompensation = 0.0f;
	float WhiteTemp = 6500.0f;
	float MotionBlurAmount = 0.0f;
	float VignetteIntensity = 0.0f;
};

struct PBRSTUDIO_API FPBRSceneCameraBatchAdjustSettings
{
	bool bOnlySelectedActors = false;
	bool bAffectCineCameras = true;
	bool bAffectRegularCameras = true;
	bool bSkipPBRStudioCameras = false;
	bool bSetFilmback = false;
	bool bSetFocalLength = true;
	bool bSetAperture = true;
	bool bSetFocusDistance = false;
	bool bSetExposure = false;
	bool bSetWhiteTemp = false;
	bool bSetMotionBlur = false;
	bool bSetVignette = false;
	float SensorWidth = 36.0f;
	float SensorHeight = 20.25f;
	float FocalLength = 35.0f;
	float Aperture = 5.6f;
	float ManualFocusDistance = 1000.0f;
	float ExposureCompensation = 0.0f;
	float WhiteTemp = 6500.0f;
	float MotionBlurAmount = 0.0f;
	float VignetteIntensity = 0.0f;
};

struct PBRSTUDIO_API FPBRSceneCameraConvertResult
{
	int32 ScannedActors = 0;
	int32 CandidateCount = 0;
	int32 ConvertedCameras = 0;
	int32 AdjustedCameras = 0;
	TArray<FString> Messages;
};

class PBRSTUDIO_API FPBRSceneCameraConverter
{
public:
	static void ScanCurrentLevel(
		TArray<TSharedPtr<FPBRSceneCameraCandidate>>& OutCandidates,
		const FPBRSceneCameraConvertSettings& Settings);

	static bool ConvertCandidates(
		const TArray<TSharedPtr<FPBRSceneCameraCandidate>>& Candidates,
		const FPBRSceneCameraConvertSettings& Settings,
		FPBRSceneCameraConvertResult& OutResult);

	static int32 BatchAdjustSceneCameras(
		const FPBRSceneCameraBatchAdjustSettings& Settings,
		FPBRSceneCameraConvertResult& OutResult);

	static int32 UndoLastConversion(FPBRSceneCameraConvertResult& OutResult);
	static bool IsPBRStudioGeneratedCamera(AActor* Actor);

private:
	static TArray<TWeakObjectPtr<AActor>> LastCreatedCameras;
	static TArray<TWeakObjectPtr<AActor>> LastHiddenOriginalActors;
};
