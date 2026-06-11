#include "Services/PBRSceneLightConverter.h"

#include "Editor.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "DatasmithAssetUserData.h"
#include "DatasmithSceneActor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Light.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Engine/Selection.h"
#include "Engine/SpotLight.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

TArray<TWeakObjectPtr<AActor>> FPBRSceneLightConverter::LastCreatedLights;
static TArray<TWeakObjectPtr<AActor>> GLastScannedTargetPointHelpers;

static FString NormalizeLightText(const FString& Text)
{
	FString Result = Text.ToLower();
	Result.ReplaceInline(TEXT("-"), TEXT("_"));
	Result.ReplaceInline(TEXT(" "), TEXT("_"));
	return Result;
}

static bool TextHasAnyToken(const FString& Text, std::initializer_list<const TCHAR*> Tokens)
{
	const FString Normalized = NormalizeLightText(Text);
	for (const TCHAR* Token : Tokens)
	{
		if (Normalized.Contains(Token))
		{
			return true;
		}
	}
	return false;
}

static bool LooksLikeTargetPointHelper(const FString& Text)
{
	return TextHasAnyToken(Text, {
		TEXT("targetpoint"),
		TEXT("target_point"),
		TEXT("target-point"),
		TEXT("target point"),
		TEXT("target helper"),
		TEXT("targethelper"),
		TEXT("light target"),
		TEXT("light_target"),
		TEXT("spot target"),
		TEXT("spot_target"),
		TEXT("ies target"),
		TEXT("ies_target"),
		TEXT("aim point"),
		TEXT("aim_point"),
		TEXT("look at"),
		TEXT("look_at"),
		TEXT("\u76ee\u6807\u70b9"),
		TEXT("\u706f\u76ee\u6807"),
		TEXT("\u8f85\u52a9\u76ee\u6807")
	});
}

static bool IsTargetPointHelperActor(AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (Actor->IsA<ATargetPoint>())
	{
		return true;
	}

	const FString ActorText = Actor->GetActorLabel() + TEXT(" ") + Actor->GetName() + TEXT(" ") + Actor->GetPathName();
	if (LooksLikeTargetPointHelper(ActorText))
	{
		return true;
	}

	TArray<ULightComponent*> LightComponents;
	Actor->GetComponents<ULightComponent>(LightComponents);
	if (LightComponents.Num() == 0)
	{
		const FString Normalized = NormalizeLightText(ActorText);
		if (Normalized.StartsWith(TEXT("target")) || Normalized.Contains(TEXT("_target")))
		{
			return true;
		}
	}

	return false;
}

static bool IsDatasmithRelatedActor(AActor* Actor)
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

static int32 DestroyScannedTargetPointHelpers(UWorld* World)
{
	if (!World)
	{
		return 0;
	}

	int32 Removed = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : GLastScannedTargetPointHelpers)
	{
		AActor* Actor = ActorPtr.Get();
		if (!Actor)
		{
			continue;
		}

		Actor->Modify();
		if (World->EditorDestroyActor(Actor, true))
		{
			++Removed;
		}
	}

	GLastScannedTargetPointHelpers.Reset();
	return Removed;
}

static FLinearColor GetLightColorFromComponent(const ULightComponent* Component)
{
	return Component ? Component->GetLightColor() : FLinearColor::White;
}

static float GetSafeLightIntensity(const ULightComponent* Component, float Fallback)
{
	return Component ? FMath::Max(1.0f, Component->Intensity) : Fallback;
}

static bool IsIESLightByName(AActor* Actor, const ULightComponent* LightComponent)
{
	FString Text;
	if (Actor)
	{
		Text += Actor->GetActorLabel();
		Text += TEXT(" ");
		Text += Actor->GetName();
	}
	if (LightComponent)
	{
		Text += TEXT(" ");
		Text += LightComponent->GetName();
	}
	return Text.Contains(TEXT("IES"), ESearchCase::IgnoreCase);
}

static void ApplyIESLightOverride(AActor* Actor, const ULightComponent* LightComponent, FPBRSceneLightCandidate& Candidate)
{
	if (!IsIESLightByName(Actor, LightComponent))
	{
		return;
	}

	Candidate.bIsIESLight = true;
	Candidate.TargetType = EPBRSceneLightTargetType::Spot;
	Candidate.SourceTypeLabel = TEXT("IES 射灯");
	Candidate.InnerConeAngle = Candidate.InnerConeAngle > 0.0f ? Candidate.InnerConeAngle : 15.0f;
	Candidate.OuterConeAngle = FMath::Clamp(Candidate.OuterConeAngle > 1.0f ? Candidate.OuterConeAngle : 44.0f, 1.0f, 80.0f);
}

static void CopyCandidateFromLightComponent(
	const ULightComponent* LightComponent,
	FPBRSceneLightCandidate& Candidate)
{
	if (!LightComponent)
	{
		return;
	}

	Candidate.SourceLightComponent = const_cast<ULightComponent*>(LightComponent);
	Candidate.SourceTransform = LightComponent->GetComponentTransform();
	Candidate.LightColor = GetLightColorFromComponent(LightComponent);
	Candidate.Intensity = GetSafeLightIntensity(LightComponent, Candidate.Intensity);
	Candidate.bIsIESLight = LightComponent->IESTexture != nullptr;

	if (const ULocalLightComponent* LocalLight = Cast<ULocalLightComponent>(LightComponent))
	{
		Candidate.AttenuationRadius = FMath::Max(64.0f, LocalLight->AttenuationRadius);
	}

	if (const URectLightComponent* Rect = Cast<URectLightComponent>(LightComponent))
	{
		Candidate.TargetType = EPBRSceneLightTargetType::Rect;
		Candidate.SourceTypeLabel = Candidate.bIsIESLight ? TEXT("UE RectLight + IES") : TEXT("UE RectLight");
		Candidate.SourceWidth = FMath::Max(1.0f, Rect->SourceWidth);
		Candidate.SourceHeight = FMath::Max(1.0f, Rect->SourceHeight);
	}
	else if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(LightComponent))
	{
		Candidate.TargetType = EPBRSceneLightTargetType::Spot;
		Candidate.SourceTypeLabel = Candidate.bIsIESLight ? TEXT("UE SpotLight + IES") : TEXT("UE SpotLight");
		Candidate.InnerConeAngle = Spot->InnerConeAngle;
		Candidate.OuterConeAngle = Spot->OuterConeAngle;
		Candidate.SourceRadius = Spot->SourceRadius;
	}
	else if (const UPointLightComponent* Point = Cast<UPointLightComponent>(LightComponent))
	{
		Candidate.TargetType = EPBRSceneLightTargetType::Point;
		Candidate.SourceTypeLabel = Candidate.bIsIESLight ? TEXT("UE PointLight + IES") : TEXT("UE PointLight");
		Candidate.SourceRadius = Point->SourceRadius;
	}
}

static ULightComponent* GetPrimaryLightComponent(AActor* Actor, const TArray<ULightComponent*>& LightComponents)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (ARectLight* RectActor = Cast<ARectLight>(Actor))
	{
		return RectActor->RectLightComponent;
	}
	if (ASpotLight* SpotActor = Cast<ASpotLight>(Actor))
	{
		return SpotActor->SpotLightComponent;
	}
	if (APointLight* PointActor = Cast<APointLight>(Actor))
	{
		return PointActor->PointLightComponent;
	}

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

static FString TargetTypeToLabel(EPBRSceneLightTargetType Type)
{
	switch (Type)
	{
	case EPBRSceneLightTargetType::Point:
		return TEXT("\u70b9\u5149");
	case EPBRSceneLightTargetType::Spot:
		return TEXT("\u5c04\u706f");
	case EPBRSceneLightTargetType::Rect:
	default:
		return TEXT("\u77e9\u5f62\u706f");
	}
}

static FString SizeToLabel(const FPBRSceneLightCandidate& Candidate)
{
	switch (Candidate.TargetType)
	{
	case EPBRSceneLightTargetType::Point:
		return FString::Printf(TEXT("R %.0f / %.0f"), Candidate.SourceRadius, Candidate.AttenuationRadius);
	case EPBRSceneLightTargetType::Spot:
		return FString::Printf(TEXT("%.0f\u00b0 / %.0f"), Candidate.OuterConeAngle, Candidate.AttenuationRadius);
	case EPBRSceneLightTargetType::Rect:
	default:
		return FString::Printf(TEXT("%.0f x %.0f / %.0f"), Candidate.SourceWidth, Candidate.SourceHeight, Candidate.AttenuationRadius);
	}
}

static void FinalizeLightCandidate(FPBRSceneLightCandidate& Candidate)
{
	Candidate.TargetTypeLabel = TargetTypeToLabel(Candidate.TargetType);
	Candidate.SizeLabel = SizeToLabel(Candidate);
	Candidate.bCanConvert = !Candidate.bIsPBRStudioLight;
	Candidate.bChecked = Candidate.bCanConvert;
	Candidate.Status = Candidate.bCanConvert ? TEXT("\u53ef\u8f6c\u6362") : TEXT("\u5df2\u662f PBRStudio \u706f\u5149");
}

static bool IsActorSelected(AActor* Actor)
{
	USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
	return Actor && Selection && Selection->IsSelected(Actor);
}

static FRotator GetDefaultSpotLightRotation()
{
	return FRotator(-90.0f, 0.0f, 0.0f);
}

static FRotator GetIESSpotLightRotation()
{
	return FRotator(-90.0f, 0.0f, 0.0f);
}

bool FPBRSceneLightConverter::IsPBRStudioGeneratedLight(AActor* Actor)
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

void FPBRSceneLightConverter::ScanCurrentLevel(
	TArray<TSharedPtr<FPBRSceneLightCandidate>>& OutCandidates,
	const FPBRSceneLightConvertSettings& Settings)
{
	OutCandidates.Reset();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}

	TSet<TWeakObjectPtr<AActor>> AddedActors;
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
		if (!IsDatasmithRelatedActor(Actor))
		{
			continue;
		}

		const bool bIsPBRStudioLight = IsPBRStudioGeneratedLight(Actor);
		if (Settings.bSkipPBRStudioLights && bIsPBRStudioLight)
		{
			continue;
		}

		if (IsTargetPointHelperActor(Actor))
		{
			GLastScannedTargetPointHelpers.Add(Actor);
			continue;
		}

		TArray<ULightComponent*> LightComponents;
		Actor->GetComponents<ULightComponent>(LightComponents);
		if (ULightComponent* PrimaryLightComponent = GetPrimaryLightComponent(Actor, LightComponents))
		{
			TSharedPtr<FPBRSceneLightCandidate> Candidate = MakeShared<FPBRSceneLightCandidate>();
			Candidate->SourceActor = Actor;
			Candidate->SourceName = Actor->GetActorLabel();
			Candidate->SourcePath = Actor->GetPathName();
			Candidate->OutputName = TEXT("PBR_") + Candidate->SourceName;
			Candidate->bIsPBRStudioLight = bIsPBRStudioLight;
			CopyCandidateFromLightComponent(PrimaryLightComponent, *Candidate);
			ApplyIESLightOverride(Actor, PrimaryLightComponent, *Candidate);
			FinalizeLightCandidate(*Candidate);
			OutCandidates.Add(Candidate);
			AddedActors.Add(Actor);
			continue;
		}
	}

	OutCandidates.Sort([](const TSharedPtr<FPBRSceneLightCandidate>& A, const TSharedPtr<FPBRSceneLightCandidate>& B)
	{
		return A.IsValid() && B.IsValid() ? A->SourceName < B->SourceName : A.IsValid();
	});
}

static void ApplyCommonLightSettings(
	ALight* LightActor,
	ULightComponent* LightComponent,
	const FPBRSceneLightCandidate& Candidate,
	const FPBRSceneLightConvertSettings& Settings)
{
	if (!LightActor || !LightComponent)
	{
		return;
	}

	LightActor->SetActorLabel(Candidate.OutputName.IsEmpty() ? TEXT("PBR_Light") : Candidate.OutputName);
	LightActor->SetFolderPath(Settings.FolderPath);
	LightComponent->SetMobility(Settings.bUseMovableLights ? EComponentMobility::Movable : EComponentMobility::Stationary);
	LightComponent->SetCastShadows(true);
	LightComponent->SetAffectTranslucentLighting(true);
	if (Settings.bClearProjectionSettings)
	{
		LightComponent->SetLightFunctionMaterial(nullptr);
		LightComponent->ClearLightFunctionMaterial();
		LightComponent->SetIESTexture(nullptr);
		LightComponent->SetUseIESBrightness(false);
		LightComponent->SetIESBrightnessScale(1.0f);
		LightComponent->SetLightFunctionDisabledBrightness(1.0f);
		if (URectLightComponent* RectLight = Cast<URectLightComponent>(LightComponent))
		{
			RectLight->SetSourceTexture(nullptr);
		}
	}

	if (ULocalLightComponent* LocalLight = Cast<ULocalLightComponent>(LightComponent))
	{
		LocalLight->SetAttenuationRadius(FMath::Max(64.0f, Candidate.AttenuationRadius));
	}

	LightComponent->PostEditChange();
	LightActor->MarkPackageDirty();
}

static void ApplyRectLightConversionSettings(URectLightComponent* RectLight, const FPBRSceneLightCandidate& Candidate)
{
	if (!RectLight)
	{
		return;
	}

	RectLight->SetIntensityUnits(ELightUnits::Lumens);
	RectLight->SetIntensity(10.0f);
	RectLight->SetLightColor(FLinearColor::MakeFromColorTemperature(4500.0f), false);
	RectLight->SetUseTemperature(true);
	RectLight->SetTemperature(4500.0f);
	RectLight->SetSourceWidth(FMath::Max(1.0f, Candidate.SourceWidth));
	RectLight->SetSourceHeight(FMath::Max(1.0f, Candidate.SourceHeight));
	RectLight->SetBarnDoorAngle(0.0f);
	RectLight->SetBarnDoorLength(1.0f);
}

static void ApplyPointLightConversionSettings(UPointLightComponent* PointLight, const FPBRSceneLightCandidate& Candidate)
{
	if (!PointLight)
	{
		return;
	}

	PointLight->SetIntensityUnits(ELightUnits::Lumens);
	PointLight->SetIntensity(10.0f);
	PointLight->SetLightColor(FLinearColor::MakeFromColorTemperature(4500.0f), false);
	PointLight->SetUseTemperature(true);
	PointLight->SetTemperature(4500.0f);
	PointLight->SetSourceRadius(FMath::Max(0.0f, Candidate.SourceRadius));
}

static void ApplySpotLightConversionSettings(USpotLightComponent* SpotLight, const FPBRSceneLightCandidate& Candidate)
{
	if (!SpotLight)
	{
		return;
	}

	SpotLight->SetIntensityUnits(ELightUnits::Lumens);
	SpotLight->SetIntensity(10.0f);
	SpotLight->SetLightColor(FLinearColor::MakeFromColorTemperature(4500.0f), false);
	SpotLight->SetUseTemperature(true);
	SpotLight->SetTemperature(4500.0f);
	SpotLight->SetSourceRadius(FMath::Max(0.0f, Candidate.SourceRadius));
	SpotLight->SetInnerConeAngle(FMath::Clamp(Candidate.InnerConeAngle, 0.0f, 80.0f));
	SpotLight->SetOuterConeAngle(FMath::Clamp(Candidate.OuterConeAngle, 1.0f, 80.0f));
}

bool FPBRSceneLightConverter::ConvertCandidates(
	const TArray<TSharedPtr<FPBRSceneLightCandidate>>& Candidates,
	const FPBRSceneLightConvertSettings& Settings,
	FPBRSceneLightConvertResult& OutResult)
{
	OutResult = FPBRSceneLightConvertResult();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutResult.Messages.Add(TEXT("\u6ca1\u6709\u627e\u5230\u5f53\u524d UE \u5173\u5361"));
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "ConvertSceneLights", "PBRStudio Convert Scene Lights"));
	LastCreatedLights.Reset();

	for (const TSharedPtr<FPBRSceneLightCandidate>& CandidatePtr : Candidates)
	{
		if (!CandidatePtr.IsValid())
		{
			continue;
		}

		const FPBRSceneLightCandidate& Candidate = *CandidatePtr;
		OutResult.CandidateCount++;
		if (!Candidate.bChecked || !Candidate.bCanConvert)
		{
			continue;
		}

		AActor* NewActor = nullptr;
		ULightComponent* NewLightComponent = nullptr;
		const FRotator SpotDefaultRotation = Candidate.bIsIESLight ? GetIESSpotLightRotation() : GetDefaultSpotLightRotation();
		const FTransform SpawnTransform(
			Candidate.bIsIESLight ? SpotDefaultRotation.Quaternion() : Candidate.SourceTransform.GetRotation(),
			Candidate.SourceTransform.GetLocation(),
			FVector::OneVector);

		if (Candidate.TargetType == EPBRSceneLightTargetType::Rect)
		{
			ARectLight* RectLight = World->SpawnActor<ARectLight>(ARectLight::StaticClass(), SpawnTransform);
			if (RectLight)
			{
				NewActor = RectLight;
				NewLightComponent = RectLight->RectLightComponent;
				ApplyRectLightConversionSettings(RectLight->RectLightComponent, Candidate);
			}
		}
		else if (Candidate.TargetType == EPBRSceneLightTargetType::Spot)
		{
			ASpotLight* SpotLight = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), SpawnTransform);
			if (SpotLight)
			{
				NewActor = SpotLight;
				NewLightComponent = SpotLight->SpotLightComponent;
				ApplySpotLightConversionSettings(SpotLight->SpotLightComponent, Candidate);
				if (Candidate.bIsIESLight)
				{
					SpotLight->SetActorRotation(SpotDefaultRotation);
				}
				else
				{
					SpotLight->SpotLightComponent->SetRelativeRotation(SpotDefaultRotation);
				}
			}
		}
		else
		{
			APointLight* PointLight = World->SpawnActor<APointLight>(APointLight::StaticClass(), SpawnTransform);
			if (PointLight)
			{
				NewActor = PointLight;
				NewLightComponent = PointLight->PointLightComponent;
				ApplyPointLightConversionSettings(PointLight->PointLightComponent, Candidate);
			}
		}

		if (!NewActor || !NewLightComponent)
		{
			OutResult.Messages.Add(Candidate.SourceName + TEXT(": \u521b\u5efa UE \u706f\u5149\u5931\u8d25"));
			continue;
		}

		ApplyCommonLightSettings(Cast<ALight>(NewActor), NewLightComponent, Candidate, Settings);
		LastCreatedLights.Add(NewActor);
		OutResult.ConvertedLights++;

		if (AActor* OriginalActor = Candidate.SourceActor.Get())
		{
			if (OriginalActor != NewActor)
			{
				OriginalActor->Modify();
				World->EditorDestroyActor(OriginalActor, true);
			}
		}

		CandidatePtr->Status = TEXT("\u5df2\u8f6c\u6362");
	}

	const int32 RemovedHelpers = DestroyScannedTargetPointHelpers(World);
	if (RemovedHelpers > 0)
	{
		OutResult.Messages.Add(FString::Printf(TEXT("\u5df2\u5220\u9664 %d \u4e2a\u76ee\u6807\u70b9/\u8f85\u52a9\u5bf9\u8c61"), RemovedHelpers));
	}
	OutResult.Messages.Add(FString::Printf(TEXT("\u5b8c\u6210: \u521b\u5efa %d \u4e2a UE \u706f\u5149"), OutResult.ConvertedLights));
	return OutResult.ConvertedLights > 0;
}

static bool ShouldBatchAdjustLight(AActor* Actor, ULightComponent* LightComponent, const FPBRSceneLightBatchAdjustSettings& Settings)
{
	if (!Actor || !LightComponent)
	{
		return false;
	}
	if (Settings.bOnlySelectedActors && !IsActorSelected(Actor))
	{
		return false;
	}
	if (Settings.bSkipPBRStudioLights && FPBRSceneLightConverter::IsPBRStudioGeneratedLight(Actor))
	{
		return false;
	}
	if (Cast<URectLightComponent>(LightComponent))
	{
		return Settings.bAffectRectLights;
	}
	if (Cast<USpotLightComponent>(LightComponent))
	{
		return Settings.bAffectSpotLights;
	}
	if (Cast<UPointLightComponent>(LightComponent))
	{
		return Settings.bAffectPointLights;
	}
	return false;
}

static void ClearLightProjectionSettings(ULightComponent* LightComponent)
{
	if (!LightComponent)
	{
		return;
	}
	LightComponent->SetLightFunctionMaterial(nullptr);
	LightComponent->ClearLightFunctionMaterial();
	LightComponent->SetIESTexture(nullptr);
	LightComponent->SetUseIESBrightness(false);
	LightComponent->SetIESBrightnessScale(1.0f);
	LightComponent->SetLightFunctionDisabledBrightness(1.0f);
	if (URectLightComponent* RectLight = Cast<URectLightComponent>(LightComponent))
	{
		RectLight->SetSourceTexture(nullptr);
	}
}

int32 FPBRSceneLightConverter::BatchAdjustSceneLights(
	const FPBRSceneLightBatchAdjustSettings& Settings,
	FPBRSceneLightConvertResult& OutResult)
{
	OutResult = FPBRSceneLightConvertResult();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutResult.Messages.Add(TEXT("\u6ca1\u6709\u627e\u5230\u5f53\u524d UE \u5173\u5361"));
		return 0;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "BatchAdjustSceneLights", "PBRStudio Batch Adjust Scene Lights"));
	int32 Adjusted = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TArray<ULightComponent*> LightComponents;
		Actor->GetComponents<ULightComponent>(LightComponents);
		for (ULightComponent* LightComponent : LightComponents)
		{
			if (!ShouldBatchAdjustLight(Actor, LightComponent, Settings))
			{
				continue;
			}

			Actor->Modify();
			LightComponent->Modify();
			if (Settings.bSetIntensityMultiplier)
			{
				LightComponent->SetIntensity(FMath::Max(0.0f, LightComponent->Intensity * Settings.IntensityMultiplier));
			}
			if (Settings.bSetLightColor)
			{
				LightComponent->SetLightColor(Settings.LightColor);
			}
			if (Settings.bSetTemperature)
			{
				LightComponent->SetUseTemperature(true);
				LightComponent->SetTemperature(FMath::Clamp(Settings.Temperature, 1700.0f, 12000.0f));
			}
			if (Settings.bClearProjectionSettings)
			{
				ClearLightProjectionSettings(LightComponent);
			}

			if (ULocalLightComponent* LocalLight = Cast<ULocalLightComponent>(LightComponent))
			{
				if (Settings.bSetAttenuationRadius)
				{
					LocalLight->SetAttenuationRadius(FMath::Max(1.0f, Settings.AttenuationRadius));
				}
			}
			if (UPointLightComponent* PointLight = Cast<UPointLightComponent>(LightComponent))
			{
				if (Settings.bSetSourceRadius)
				{
					PointLight->SetSourceRadius(FMath::Max(0.0f, Settings.SourceRadius));
				}
			}
			if (USpotLightComponent* SpotLight = Cast<USpotLightComponent>(LightComponent))
			{
				if (Settings.bSetSourceRadius)
				{
					SpotLight->SetSourceRadius(FMath::Max(0.0f, Settings.SourceRadius));
				}
				if (Settings.bSetSpotCone)
				{
					const float Outer = FMath::Clamp(Settings.OuterConeAngle, 1.0f, 80.0f);
					SpotLight->SetOuterConeAngle(Outer);
					SpotLight->SetInnerConeAngle(FMath::Clamp(Settings.InnerConeAngle, 0.0f, Outer));
				}
			}
			if (URectLightComponent* RectLight = Cast<URectLightComponent>(LightComponent))
			{
				if (Settings.bSetRectSize)
				{
					RectLight->SetSourceWidth(FMath::Max(1.0f, Settings.SourceWidth));
					RectLight->SetSourceHeight(FMath::Max(1.0f, Settings.SourceHeight));
				}
			}

			LightComponent->RecreateRenderState_Concurrent();
			LightComponent->PostEditChange();
			Actor->MarkPackageDirty();
			Adjusted++;
		}
	}

	OutResult.AdjustedLights = Adjusted;
	OutResult.Messages.Add(FString::Printf(TEXT("\u5df2\u8c03\u8282 %d \u4e2a\u706f\u5149"), Adjusted));
	return Adjusted;
}

int32 FPBRSceneLightConverter::UndoLastConversion(FPBRSceneLightConvertResult& OutResult)
{
	OutResult = FPBRSceneLightConvertResult();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutResult.Messages.Add(TEXT("\u6ca1\u6709\u627e\u5230\u5f53\u524d UE \u5173\u5361"));
		return 0;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PBRStudio", "UndoSceneLightConversion", "PBRStudio Undo Scene Light Conversion"));
	int32 Removed = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : LastCreatedLights)
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
	LastCreatedLights.Reset();

	OutResult.ConvertedLights = Removed;
	OutResult.Messages.Add(FString::Printf(TEXT("\u5df2\u64a4\u56de: \u5220\u9664 %d \u4e2a\u706f\u5149"), Removed));
	return Removed;
}
