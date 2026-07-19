#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"

class AActor;
class ALight;
class ULightComponent;

enum class EPBRSceneLightTargetType : uint8
{
	Rect,
	Point,
	Spot
};

struct PBRSTUDIO_API FPBRSceneLightCandidate
{
	FString SourceName;
	FString SourcePath;
	FString OutputName;
	FString SourceTypeLabel;
	FString TargetTypeLabel;
	FString SizeLabel;
	FString Status;
	EPBRSceneLightTargetType TargetType = EPBRSceneLightTargetType::Rect;
	TWeakObjectPtr<AActor> SourceActor;
	TWeakObjectPtr<ULightComponent> SourceLightComponent;
	FTransform SourceTransform = FTransform::Identity;
	FVector SourceBoundsExtent = FVector::ZeroVector;
	FLinearColor LightColor = FLinearColor::White;
	float Intensity = 5000.0f;
	float AttenuationRadius = 1000.0f;
	float SourceWidth = 64.0f;
	float SourceHeight = 64.0f;
	float SourceRadius = 0.0f;
	float InnerConeAngle = 0.0f;
	float OuterConeAngle = 44.0f;
	bool bCanConvert = false;
	bool bChecked = true;
	bool bIsPBRStudioLight = false;
	bool bIsIESLight = false;
};

struct PBRSTUDIO_API FPBRSceneLightConvertSettings
{
	float IntensityMultiplier = 1.0f;
	float MinRectSize = 8.0f;
	float DefaultRectSize = 64.0f;
	float DefaultAttenuationRadius = 1000.0f;
	bool bOnlySelectedActors = false;
	bool bUseMovableLights = true;
	bool bSkipPBRStudioLights = true;
	bool bClearProjectionSettings = true;
	FName FolderPath = TEXT("PBRStudio/ConvertedLights");
};

struct PBRSTUDIO_API FPBRSceneLightBatchAdjustSettings
{
	bool bOnlySelectedActors = false;
	bool bAffectRectLights = true;
	bool bAffectPointLights = true;
	bool bAffectSpotLights = true;
	bool bAffectDirectionalLights = false;
	bool bAffectSkyLights = false;
	bool bSkipPBRStudioLights = false;
	bool bClearProjectionSettings = true;
	bool bUseInteriorSunSkyPreset = false;
	bool bSetIntensityMultiplier = true;
	bool bSetLightColor = false;
	bool bSetTemperature = false;
	bool bSetAttenuationRadius = false;
	bool bSetSourceRadius = false;
	bool bSetRectSize = false;
	bool bSetSpotCone = false;
	float IntensityMultiplier = 1.0f;
	FLinearColor LightColor = FLinearColor::White;
	float Temperature = 6500.0f;
	float AttenuationRadius = 1000.0f;
	float SourceRadius = 0.0f;
	float SourceWidth = 64.0f;
	float SourceHeight = 64.0f;
	float InnerConeAngle = 15.0f;
	float OuterConeAngle = 44.0f;
	float InteriorDirectionalIntensity = 3.0f;
	float InteriorSkyLightIntensity = 0.25f;
};

struct PBRSTUDIO_API FPBRSceneLightConvertResult
{
	int32 ScannedActors = 0;
	int32 CandidateCount = 0;
	int32 ConvertedLights = 0;
	int32 AdjustedLights = 0;
	TArray<FString> Messages;
};

class PBRSTUDIO_API FPBRSceneLightConverter
{
public:
	static void ScanCurrentLevel(
		TArray<TSharedPtr<FPBRSceneLightCandidate>>& OutCandidates,
		const FPBRSceneLightConvertSettings& Settings);

	static bool ConvertCandidates(
		const TArray<TSharedPtr<FPBRSceneLightCandidate>>& Candidates,
		const FPBRSceneLightConvertSettings& Settings,
		FPBRSceneLightConvertResult& OutResult);

	static int32 BatchAdjustSceneLights(
		const FPBRSceneLightBatchAdjustSettings& Settings,
		FPBRSceneLightConvertResult& OutResult);

	static int32 UndoLastConversion(FPBRSceneLightConvertResult& OutResult);
	static bool IsPBRStudioGeneratedLight(AActor* Actor);

private:
	static TArray<TWeakObjectPtr<AActor>> LastCreatedLights;
};
