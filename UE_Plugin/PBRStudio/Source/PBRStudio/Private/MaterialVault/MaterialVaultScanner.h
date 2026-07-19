#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "MaterialVaultTypes.h"

class FAssetRegistryModule;

struct FMaterialVaultScanStats
{
	int32 RawMaterialCount = 0;
	int32 RawMaterialInstanceCount = 0;
	int32 MaterialFamilyCount = 0;
	int32 StandaloneMaterialInstanceCount = 0;
	int32 SkippedAssetCount = 0;
	int32 GeneratedItemCount = 0;
};

class FMaterialVaultScanner
{
public:
	TArray<TSharedPtr<FMaterialVaultItem>> ScanMaterials(const FString& PackageRoot, FMaterialVaultScanStats* OutStats = nullptr) const;
	void PopulateDependencies(FMaterialVaultItem& Item) const;
	TArray<FAssetData> GatherMaterialAssetData(const FString& PackageRoot) const;
	TSharedPtr<FMaterialVaultItem> BuildItemLight(const FAssetData& AssetData) const;
	FMaterialVaultAssetEntry BuildEntry(const FAssetData& AssetData, const FString& Category, const FString& ItemId, bool bIsRoot) const;
	static FString InferCategory(const FString& Name);

private:
	static bool IsGeneratedMaterialVaultPackage(const FString& PackageName);
	void GatherDependencies(const FAssetData& RootAsset, FMaterialVaultItem& OutItem) const;
	EMaterialVaultAssetRole ResolveRole(const FAssetData& AssetData, bool bIsRoot) const;
	FString ResolveFolderForRole(EMaterialVaultAssetRole Role) const;
	static FString MakeSafeSegment(const FString& Value, const FString& Fallback);
	static FString MakeItemId(const FAssetData& AssetData);
};
