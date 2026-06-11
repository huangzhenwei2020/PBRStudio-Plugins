#pragma once

#include "CoreMinimal.h"
#include "Models/PBRMaterialSet.h"
#include "UObject/SoftObjectPtr.h"

class AActor;
class UMaterial;
class UMaterialInterface;
class UPrimitiveComponent;
class UTexture2D;

enum class EPBRSceneReplacementKind : uint8
{
	NotReplaceable,
	BPR,
	Simple,
	Glass,
	Emissive
};

struct PBRSTUDIO_API FPBRSceneMaterialSlot
{
	TWeakObjectPtr<UPrimitiveComponent> Component;
	FSoftObjectPath ComponentPath;
	int32 MaterialIndex = INDEX_NONE;
	TWeakObjectPtr<UMaterialInterface> OriginalMaterial;
	FSoftObjectPath OriginalMaterialPath;
};

struct PBRSTUDIO_API FPBRSceneMaterialCandidate
{
	FString MaterialName;
	FString OutputMaterialName;
	FString MaterialPath;
	TWeakObjectPtr<UMaterialInterface> Material;
	FLinearColor InheritedBaseColor = FLinearColor::White;
	TWeakObjectPtr<UTexture2D> BaseColorTexture;
	TWeakObjectPtr<UTexture2D> NormalTexture;
	TWeakObjectPtr<UTexture2D> RoughnessTexture;
	TWeakObjectPtr<UTexture2D> MetallicTexture;
	TWeakObjectPtr<UTexture2D> AOTexture;
	TWeakObjectPtr<UTexture2D> SpecularTexture;
	TWeakObjectPtr<UTexture2D> HeightTexture;
	TWeakObjectPtr<UTexture2D> EmissiveTexture;
	TWeakObjectPtr<UTexture2D> OpacityTexture;
	FString BaseColorSourcePath;
	FString NormalSourcePath;
	FString RoughnessSourcePath;
	FString MetallicSourcePath;
	FString AOSourcePath;
	FString SpecularSourcePath;
	FString HeightSourcePath;
	FString EmissiveSourcePath;
	FString OpacitySourcePath;
	FString ReplaceMode;
	FString Status;
	EPBRSceneReplacementKind ReplacementKind = EPBRSceneReplacementKind::NotReplaceable;
	bool bCanReplace = false;
	bool bLooksEmissive = false;
	bool bLooksTransparent = false;
	bool bUseBPRReplacement = true;
	bool bIsPBRStudioMaterial = false;
	bool bChecked = true;
	TArray<FPBRSceneMaterialSlot> Slots;
};

struct PBRSTUDIO_API FPBRSceneReplaceSettings
{
	FString OutputRoot = TEXT("/Game/PBRStudio/SceneReplaced");
	int32 OutputSize = 2048;
	float NormalStrength = 2.0f;
	float RoughnessContrast = 1.2f;
	float MetallicThreshold = 0.5f;
	float AOStrength = 1.0f;
	bool bGenerateNormal = true;
	bool bGenerateRoughness = true;
	bool bGenerateMetallic = true;
	bool bGenerateAO = true;
	bool bGenerateSpecular = false;
	bool bGenerateOpacity = false;
	bool bGenerateEmissive = true;
	bool bGenerateHeight = false;
	bool bGenerateORM = false;
	bool bOnlyImageExtensions = true;
	TFunction<void(int32 Current, int32 Total, const FString& Status)> ProgressCallback;
};

struct PBRSTUDIO_API FPBRSceneReplaceResult
{
	int32 ScannedMaterials = 0;
	int32 ReplaceableMaterials = 0;
	int32 ReplacedMaterials = 0;
	int32 ReplacedSlots = 0;
	TArray<FString> Messages;
};

class PBRSTUDIO_API FPBRSceneMaterialReplacer
{
public:
	static bool IsSupportedImagePath(const FString& Path);
	static bool IsPBRStudioGeneratedMaterial(UMaterialInterface* Material);

	static void ScanCurrentLevel(TArray<TSharedPtr<FPBRSceneMaterialCandidate>>& OutCandidates);
	static bool ReplaceCandidates(
		const TArray<TSharedPtr<FPBRSceneMaterialCandidate>>& Candidates,
		const FPBRSceneReplaceSettings& Settings,
		FPBRSceneReplaceResult& OutResult);

	static void RefreshCurrentLevelMaterialAssignments();
	static int32 UndoLastReplacement(FPBRSceneReplaceResult& OutResult);

private:
	static TArray<FPBRSceneMaterialSlot> LastReplacementSlots;

	static UTexture2D* FindBaseColorTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static UTexture2D* FindNormalTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static UTexture2D* FindRoughnessTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static UTexture2D* FindMetallicTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static UTexture2D* FindAOTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static UTexture2D* FindSpecularTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static UTexture2D* FindHeightTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static UTexture2D* FindEmissiveTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static UTexture2D* FindOpacityTexture(UMaterialInterface* Material, FString& OutSourcePath);
	static bool IsEmissiveMaterial(UMaterialInterface* Material, UTexture2D* EmissiveTexture);
	static bool IsTransparentMaterial(UMaterialInterface* Material, UTexture2D* OpacityTexture);
	static UMaterial* EnsureSceneReplaceMasterMaterial(FString& OutMessage);
	static UMaterial* EnsureSceneGlassMasterMaterial(FString& OutMessage);
	static UMaterial* EnsureSceneEmissiveMasterMaterial(FString& OutMessage);
	static bool GeneratePBRSetFromTexture(UTexture2D* BaseColorTexture, const FString& MaterialName,
		const FPBRSceneReplaceSettings& Settings, const FPBRSceneMaterialCandidate& Candidate, FPBRMaterialSet& OutSet, FString& OutMessage);
	static bool SaveGeneratedImage(const FString& FilePath, const TArray<FColor>& Pixels, int32 Width, int32 Height);
	static bool LoadTexturePixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, int32 MaxDimension = 0);
};
