#pragma once

#include "CoreMinimal.h"
#include "Models/PBRMaterialSet.h"
#include "Models/PBRMaterialTypes.h"

class PBRSTUDIO_API FPBRMaterialFactory
{
public:
	struct FCreateSettings
	{
		FString TargetMode = TEXT("PBR Material Metal/Rough");
		FString MaterialPrefix = TEXT("M_PBR_");
		FString NormalPreference = TEXT("DirectX");
		FString GlossMode = TEXT("InvertToRoughness");
		FString PackagePath = TEXT("/Game/Materials/PBR/");
		FString MaterialTypeMode = TEXT("自动");
		bool bConnectHeightAsDisplacement = false;
	};

	static UMaterialInterface* CreateMaterialFromPBRSet(
		const FPBRMaterialSet& Set,
		const FCreateSettings& Settings,
		FString& OutNotes);

	static EPBRMaterialType ResolveMaterialTypeForSet(const FPBRMaterialSet& Set, const FString& MaterialTypeMode);
	static FString MaterialTypeToDisplayName(EPBRMaterialType Type);
	static void GetUnusedChannelsForMaterialType(
		const FPBRMaterialSet& Set,
		EPBRMaterialType MaterialType,
		TArray<FString>& OutUnusedChannels);

	static void LearnSlot(const FString& TargetMode, UMaterialInterface* Mat,
		const FString& Channel, const FString& PropertyName);

	static FString GetMaterialPropertyForChannel(const FString& Channel, const FString& TargetMode);

private:
	static class UTexture2D* ImportTextureToAsset(const FString& FilePath, const FString& PackagePath, const FString& AssetName);

	static class UMaterialExpressionTextureSample* CreateTextureSampleNode(
		UMaterial* Material, UTexture2D* Texture, const FString& Channel);

	static bool ConnectToMaterialInput(UMaterial* Material,
		class UMaterialExpression* Expression, const FString& Channel, const FString& TargetMode);

	static FString TryGetSlotOverride(const FString& TargetMode, const FString& Channel);

	// ORM channel splitting
	static void SplitORMChannels(UMaterial* Material, UTexture2D* ORMTexture,
		FPBRMaterialSet& Set, const FString& NormalPreference, TArray<FString>& OutNotes);
};
