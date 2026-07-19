#include "Services/PBRSceneCameraConverter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "CineCameraSettings.h"
#include "Components/LightComponent.h"
#include "DatasmithAssetUserData.h"
#include "DatasmithSceneActor.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Light.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

TArray<TWeakObjectPtr<AActor>> FPBRSceneCameraConverter::LastCreatedCameras;
TArray<TWeakObjectPtr<AActor>> FPBRSceneCameraConverter::LastHiddenOriginalActors;

static bool IsLightActorOrComponentHost(AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (Actor->IsA<ALight>())
	{
		return true;
	}

	TArray<ULightComponent*> LightComponents;
	Actor->GetComponents<ULightComponent>(LightComponents);
	return LightComponents.Num() > 0;
}

static bool IsDatasmithRelatedCameraActor(AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (Actor->IsA<ADatasmithSceneActor>())
	{
		return true;
	}

	if (AActor* OwnerActor = Actor->GetOwner())
	{
		if (OwnerActor->IsA<ADatasmithSceneActor>())
		{
			return true;
		}
	}

	if (AActor* ParentActor = Actor->GetAttachParentActor())
	{
		if (ParentActor->IsA<ADatasmithSceneActor>())
		{
			return true;
		}
	}

	if (UDatasmithAssetUserData::GetDatasmithUserData(Actor))
	{
		return true;
	}

	if (UActorComponent* RootComponent = Actor->GetRootComponent())
	{
		if (UDatasmithAssetUserData::GetDatasmithUserData(RootComponent))
		{
			return true;
		}
	}

	for (TActorIterator<ADatasmithSceneActor> It(Actor->GetWorld()); It; ++It)
	{
		ADatasmithSceneActor* SceneActor = *It;
		if (!SceneActor)
		{
			continue;
		}

		for (const TPair<FName, TSoftObjectPtr<AActor>>& RelatedActorPair : SceneActor->RelatedActors)
		{
			if (RelatedActorPair.Value.Get() == Actor)
			{
				return true;
			}
		}
	}

	return false;
}

static bool IsActorSelected(AActor* Actor)
{
	USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
	return Actor && Selection && Selection->IsSelected(Actor);
}

static FString CameraSourceTypeToLabel(EPBRSceneCameraSourceType Type)
{
	switch (Type)
	{
	case EPBRSceneCameraSourceType::CineCamera:
		return TEXT("UE \u7535\u5f71\u76f8\u673a");
	case EPBRSceneCameraSourceType::UECamera:
		return TEXT("UE \u666e\u901a\u76f8\u673a / Datasmith \u76f8\u673a");
	case EPBRSceneCameraSourceType::ImportedVRayCorona:
		return TEXT("V-Ray/Corona/Max \u76f8\u673a");
	case EPBRSceneCameraSourceType::DatasmithCamera:
	default:
		return TEXT("Datasmith \u76f8\u673a");
	}
}

static void ApplyPostProcessDefaults(UCameraComponent* CameraComponent, float ExposureCompensation, float WhiteTemp, float MotionBlurAmount, float VignetteIntensity)
{
	if (!CameraComponent)
	{
		return;
	}

	CameraComponent->SetPostProcessBlendWeight(1.0f);
	FPostProcessSettings& PP = CameraComponent->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = ExposureCompensation;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_WhiteTemp = true;
	PP.WhiteTemp = WhiteTemp;
	PP.bOverride_MotionBlurAmount = true;
	PP.MotionBlurAmount = MotionBlurAmount;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = VignetteIntensity;
}

static void ApplyCineCameraDefaults(
	UCineCameraComponent* CineComponent,
	const FPBRSceneCameraConvertSettings& Settings,
	const FPBRSceneCameraCandidate* SourceCandidate)
{
	if (!CineComponent)
	{
		return;
	}

	FCameraFilmbackSettings Filmback;
	Filmback.SensorWidth = FMath::Max(1.0f, Settings.SensorWidth);
	Filmback.SensorHeight = FMath::Max(1.0f, Settings.SensorHeight);
	if (SourceCandidate && SourceCandidate->SourceAspectRatio > 0.1f)
	{
		Filmback.SensorHeight = Filmback.SensorWidth / SourceCandidate->SourceAspectRatio;
	}
	Filmback.SensorHorizontalOffset = 0.0f;
	Filmback.SensorVerticalOffset = 0.0f;
	Filmback.RecalcSensorAspectRatio();
	CineComponent->SetFilmback(Filmback);

	FCameraLensSettings Lens;
	Lens.MinFocalLength = 12.0f;
	Lens.MaxFocalLength = 200.0f;
	Lens.MinFStop = 1.2f;
	Lens.MaxFStop = 22.0f;
	Lens.MinimumFocusDistance = 10.0f;
	Lens.SqueezeFactor = 1.0f;
	Lens.DiaphragmBladeCount = 7;
	CineComponent->SetLensSettings(Lens);

	CineComponent->SetCurrentFocalLength(FMath::Clamp(Settings.FocalLength, Lens.MinFocalLength, Lens.MaxFocalLength));
	CineComponent->SetCurrentAperture(FMath::Clamp(Settings.Aperture, Lens.MinFStop, Lens.MaxFStop));

	FCameraFocusSettings Focus;
	Focus.FocusMethod = ECameraFocusMethod::Manual;
	Focus.ManualFocusDistance = FMath::Max(1.0f, Settings.ManualFocusDistance);
	Focus.bSmoothFocusChanges = false;
	Focus.FocusSmoothingInterpSpeed = 8.0f;
	Focus.FocusOffset = 0.0f;
	CineComponent->SetFocusSettings(Focus);

	CineComponent->ExposureMethod = ECameraExposureMethod::DoNotOverride;
	CineComponent->ProjectionMode = ECameraProjectionMode::Perspective;
	CineComponent->SetConstraintAspectRatio(false);
	ApplyPostProcessDefaults(CineComponent, Settings.ExposureCompensation, Settings.WhiteTemp, Settings.MotionBlurAmount, Settings.VignetteIntensity);
	CineComponent->PostEditChange();
}

static void ApplyCineCameraBatchSettings(UCineCameraComponent* CineComponent, const FPBRSceneCameraBatchAdjustSettings& Settings)
{
	if (!CineComponent)
	{
		return;
	}

	if (Settings.bSetFilmback)
	{
		FCameraFilmbackSettings Filmback = CineComponent->Filmback;
		Filmback.SensorWidth = FMath::Max(1.0f, Settings.SensorWidth);
		Filmback.SensorHeight = FMath::Max(1.0f, Settings.SensorHeight);
		Filmback.RecalcSensorAspectRatio();
		CineComponent->SetFilmback(Filmback);
	}
	if (Settings.bSetFocalLength)
	{
		CineComponent->SetCurrentFocalLength(FMath::Clamp(Settings.FocalLength, 1.0f, 1000.0f));
	}
	if (Settings.bSetAperture)
	{
		CineComponent->SetCurrentAperture(FMath::Clamp(Settings.Aperture, 0.1f, 64.0f));
	}
	if (Settings.bSetFocusDistance)
	{
		FCameraFocusSettings Focus = CineComponent->FocusSettings;
		Focus.FocusMethod = ECameraFocusMethod::Manual;
		Focus.ManualFocusDistance = FMath::Max(1.0f, Settings.ManualFocusDistance);
		CineComponent->SetFocusSettings(Focus);
	}
	if (Settings.bSetExposure || Settings.bSetWhiteTemp || Settings.bSetMotionBlur || Settings.bSetVignette)
	{
		CineComponent->SetPostProcessBlendWeight(1.0f);
		FPostProcessSettings& PP = CineComponent->PostProcessSettings;
		if (Settings.bSetExposure)
		{
			PP.bOverride_AutoExposureMethod = true;
			PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			PP.bOverride_AutoExposureBias = true;
			PP.AutoExposureBias = Settings.ExposureCompensation;
			PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			PP.AutoExposureApplyPhysicalCameraExposure = false;
		}
		if (Settings.bSetWhiteTemp)
		{
			PP.bOverride_WhiteTemp = true;
			PP.WhiteTemp = Settings.WhiteTemp;
		}
		if (Settings.bSetMotionBlur)
		{
			PP.bOverride_MotionBlurAmount = true;
			PP.MotionBlurAmount = Settings.MotionBlurAmount;
		}
		if (Settings.bSetVignette)
		{
			PP.bOverride_VignetteIntensity = true;
			PP.VignetteIntensity = Settings.VignetteIntensity;
		}
	}
	CineComponent->PostEditChange();
}

static void ApplyRegularCameraBatchSettings(UCameraComponent* CameraComponent, const FPBRSceneCameraBatchAdjustSettings& Settings)
{
	if (!CameraComponent)
	{
		return;
	}
	if (Settings.bSetFocalLength)
	{
		const float SensorWidth = FMath::Max(1.0f, Settings.SensorWidth);
		const float FocalLength = FMath::Max(1.0f, Settings.FocalLength);
		CameraComponent->SetFieldOfView(FMath::RadiansToDegrees(2.0f * FMath::Atan(SensorWidth / (2.0f * FocalLength))));
	}
	if (Settings.bSetExposure || Settings.bSetWhiteTemp || Settings.bSetMotionBlur || Settings.bSetVignette)
	{
		ApplyPostProcessDefaults(
			CameraComponent,
			Settings.bSetExposure ? Settings.ExposureCompensation : CameraComponent->PostProcessSettings.AutoExposureBias,
			Settings.bSetWhiteTemp ? Settings.WhiteTemp : CameraComponent->PostProcessSettings.WhiteTemp,
			Settings.bSetMotionBlur ? Settings.MotionBlurAmount : CameraComponent->PostProcessSettings.MotionBlurAmount,
			Settings.bSetVignette ? Settings.VignetteIntensity : CameraComponent->PostProcessSettings.VignetteIntensity);
	}
	CameraComponent->PostEditChange();
}

bool FPBRSceneCameraConverter::IsPBRStudioGeneratedCamera(AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	const FString Name = Actor->GetName();
	const FString Label = Actor->GetActorLabel();
	const FString Path = Actor->GetPathName();
	return Name.StartsWith(TEXT("PBRStudio_")) ||
		Label.StartsWith(TEXT("PBRStudio_")) ||
		Label.StartsWith(TEXT("PBR_")) ||
		Path.Contains(TEXT("PBRStudio"));
}

void FPBRSceneCameraConverter::ScanCurrentLevel(
	TArray<TSharedPtr<FPBRSceneCameraCandidate>>& OutCandidates,
	const FPBRSceneCameraConvertSettings& Settings)
{
	OutCandidates.Reset();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsPendingKillPending())
		{
			continue;
		}
		if (Settings.bOnlySelectedActors && !IsActorSelected(Actor))
		{
			continue;
		}

		const bool bIsPBRStudioCamera = IsPBRStudioGeneratedCamera(Actor);
		if (Settings.bSkipPBRStudioCameras && bIsPBRStudioCamera)
		{
			continue;
		}
		if (!IsDatasmithRelatedCameraActor(Actor))
		{
			continue;
		}
		if (IsLightActorOrComponentHost(Actor))
		{
			continue;
		}

		UCameraComponent* CameraComponent = Actor->FindComponentByClass<UCameraComponent>();
		if (!CameraComponent)
		{
			continue;
		}

		TSharedPtr<FPBRSceneCameraCandidate> Candidate = MakeShared<FPBRSceneCameraCandidate>();
		Candidate->SourceActor = Actor;
		Candidate->SourceCameraComponent = CameraComponent;
		Candidate->SourceName = Actor->GetActorLabel();
		Candidate->SourcePath = Actor->GetPathName();
		Candidate->OutputName = TEXT("PBR_Cine_") + Candidate->SourceName;
		Candidate->SourceTransform = CameraComponent ? CameraComponent->GetComponentTransform() : Actor->GetActorTransform();
		Candidate->bIsPBRStudioCamera = bIsPBRStudioCamera;
		Candidate->bCanConvert = !bIsPBRStudioCamera;
		Candidate->bChecked = Candidate->bCanConvert;
		if (CameraComponent)
		{
			Candidate->SourceFOV = CameraComponent->FieldOfView;
			Candidate->SourceAspectRatio = CameraComponent->AspectRatio > 0.1f ? CameraComponent->AspectRatio : 1.777778f;
		}

		if (Cast<ACineCameraActor>(Actor) || Cast<UCineCameraComponent>(CameraComponent))
		{
			Candidate->SourceType = EPBRSceneCameraSourceType::CineCamera;
		}
		else if (CameraComponent)
		{
			Candidate->SourceType = EPBRSceneCameraSourceType::UECamera;
		}
		else
		{
			Candidate->SourceType = EPBRSceneCameraSourceType::DatasmithCamera;
		}

		Candidate->SourceTypeLabel = CameraSourceTypeToLabel(Candidate->SourceType);
		Candidate->Status = Candidate->bCanConvert ? TEXT("\u53ef\u8f6c\u6362") : TEXT("\u5df2\u662f PBRStudio \u76f8\u673a");
		OutCandidates.Add(Candidate);
	}

	OutCandidates.Sort([](const TSharedPtr<FPBRSceneCameraCandidate>& A, const TSharedPtr<FPBRSceneCameraCandidate>& B)
	{
		return A.IsValid() && B.IsValid() ? A->SourceName < B->SourceName : A.IsValid();
	});
}

bool FPBRSceneCameraConverter::ConvertCandidates(
	const TArray<TSharedPtr<FPBRSceneCameraCandidate>>& Candidates,
	const FPBRSceneCameraConvertSettings& Settings,
	FPBRSceneCameraConvertResult& OutResult)
{
	OutResult = FPBRSceneCameraConvertResult();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutResult.Messages.Add(TEXT("\u6ca1\u6709\u627e\u5230\u5f53\u524d UE \u5173\u5361"));
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "ConvertSceneCameras", "PBRStudio Convert Scene Cameras"));
	LastCreatedCameras.Reset();
	LastHiddenOriginalActors.Reset();

	for (const TSharedPtr<FPBRSceneCameraCandidate>& CandidatePtr : Candidates)
	{
		if (!CandidatePtr.IsValid())
		{
			continue;
		}

		OutResult.CandidateCount++;
		FPBRSceneCameraCandidate& Candidate = *CandidatePtr;
		if (!Candidate.bChecked || !Candidate.bCanConvert)
		{
			continue;
		}

		FTransform SpawnTransform(Candidate.SourceTransform.GetRotation(), Candidate.SourceTransform.GetLocation(), FVector::OneVector);
		ACineCameraActor* CineActor = World->SpawnActor<ACineCameraActor>(ACineCameraActor::StaticClass(), SpawnTransform);
		if (!CineActor || !CineActor->GetCineCameraComponent())
		{
			OutResult.Messages.Add(Candidate.SourceName + TEXT(": \u521b\u5efa\u7535\u5f71\u76f8\u673a\u5931\u8d25"));
			continue;
		}

		CineActor->SetActorLabel(Candidate.OutputName.IsEmpty() ? TEXT("PBR_CineCamera") : Candidate.OutputName);
		CineActor->SetFolderPath(Settings.FolderPath);
		ApplyCineCameraDefaults(CineActor->GetCineCameraComponent(), Settings, &Candidate);
		CineActor->MarkPackageDirty();
		LastCreatedCameras.Add(CineActor);
		OutResult.ConvertedCameras++;

		if (Settings.bHideOriginalActors)
		{
			if (AActor* OriginalActor = Candidate.SourceActor.Get())
			{
				if (OriginalActor != CineActor && !OriginalActor->IsTemporarilyHiddenInEditor())
				{
					OriginalActor->Modify();
					OriginalActor->SetIsTemporarilyHiddenInEditor(true);
					LastHiddenOriginalActors.Add(OriginalActor);
				}
			}
		}

		Candidate.Status = TEXT("\u5df2\u8f6c\u6362");
	}

	OutResult.Messages.Add(FString::Printf(TEXT("\u5b8c\u6210: \u521b\u5efa %d \u4e2a\u7535\u5f71\u76f8\u673a"), OutResult.ConvertedCameras));
	return OutResult.ConvertedCameras > 0;
}

int32 FPBRSceneCameraConverter::BatchAdjustSceneCameras(
	const FPBRSceneCameraBatchAdjustSettings& Settings,
	FPBRSceneCameraConvertResult& OutResult)
{
	OutResult = FPBRSceneCameraConvertResult();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutResult.Messages.Add(TEXT("\u6ca1\u6709\u627e\u5230\u5f53\u524d UE \u5173\u5361"));
		return 0;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "BatchAdjustSceneCameras", "PBRStudio Batch Adjust Scene Cameras"));
	int32 Adjusted = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsPendingKillPending())
		{
			continue;
		}
		if (Settings.bOnlySelectedActors && !IsActorSelected(Actor))
		{
			continue;
		}
		if (Settings.bSkipPBRStudioCameras && IsPBRStudioGeneratedCamera(Actor))
		{
			continue;
		}

		UCineCameraComponent* CineComponent = Actor->FindComponentByClass<UCineCameraComponent>();
		UCameraComponent* CameraComponent = Actor->FindComponentByClass<UCameraComponent>();
		if (CineComponent && Settings.bAffectCineCameras)
		{
			Actor->Modify();
			CineComponent->Modify();
			ApplyCineCameraBatchSettings(CineComponent, Settings);
			CineComponent->RecreateRenderState_Concurrent();
			Actor->MarkPackageDirty();
			Adjusted++;
		}
		else if (CameraComponent && Settings.bAffectRegularCameras)
		{
			Actor->Modify();
			CameraComponent->Modify();
			ApplyRegularCameraBatchSettings(CameraComponent, Settings);
			CameraComponent->RecreateRenderState_Concurrent();
			Actor->MarkPackageDirty();
			Adjusted++;
		}
	}

	OutResult.AdjustedCameras = Adjusted;
	OutResult.Messages.Add(FString::Printf(TEXT("\u5df2\u8c03\u8282 %d \u4e2a\u76f8\u673a"), Adjusted));
	return Adjusted;
}

int32 FPBRSceneCameraConverter::UndoLastConversion(FPBRSceneCameraConvertResult& OutResult)
{
	OutResult = FPBRSceneCameraConvertResult();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutResult.Messages.Add(TEXT("\u6ca1\u6709\u627e\u5230\u5f53\u524d UE \u5173\u5361"));
		return 0;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "UndoSceneCameraConversion", "PBRStudio Undo Scene Camera Conversion"));
	int32 Removed = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : LastCreatedCameras)
	{
		AActor* Actor = ActorPtr.Get();
		if (!Actor)
		{
			continue;
		}
		Actor->Modify();
		if (World->EditorDestroyActor(Actor, true))
		{
			Removed++;
		}
	}
	LastCreatedCameras.Reset();

	for (const TWeakObjectPtr<AActor>& ActorPtr : LastHiddenOriginalActors)
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			Actor->Modify();
			Actor->SetIsTemporarilyHiddenInEditor(false);
		}
	}
	LastHiddenOriginalActors.Reset();

	OutResult.ConvertedCameras = Removed;
	OutResult.Messages.Add(FString::Printf(TEXT("\u5df2\u64a4\u56de: \u5220\u9664 %d \u4e2a\u7535\u5f71\u76f8\u673a"), Removed));
	return Removed;
}
