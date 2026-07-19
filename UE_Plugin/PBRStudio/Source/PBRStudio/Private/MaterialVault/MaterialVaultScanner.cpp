#include "MaterialVaultScanner.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "MaterialVaultSettings.h"
#include "Misc/EngineVersion.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

namespace
{
bool AddUniqueEntryByPackage(TArray<FMaterialVaultAssetEntry>& Entries, const FMaterialVaultAssetEntry& Entry)
{
	for (const FMaterialVaultAssetEntry& ExistingEntry : Entries)
	{
		if (ExistingEntry.OriginalPackageName == Entry.OriginalPackageName)
		{
			return false;
		}
	}

	Entries.Add(Entry);
	return true;
}

FString MakeSafeItemName(FString Value, const FString& Fallback)
{
	Value.TrimStartAndEndInline();
	if (Value.IsEmpty())
	{
		Value = Fallback;
	}
	const TCHAR* InvalidChars = TEXT("\\/:*?\"<>|");
	for (const TCHAR* It = InvalidChars; *It; ++It)
	{
		Value.ReplaceCharInline(*It, TEXT('_'));
	}
	Value.ReplaceInline(TEXT("/"), TEXT("_"));
	Value.ReplaceInline(TEXT(" "), TEXT("_"));
	while (Value.ReplaceInline(TEXT("__"), TEXT("_")) > 0) {}
	Value.TrimStartAndEndInline();
	return Value.IsEmpty() ? Fallback : Value;
}

FString SimplifyMaterialName(FString Name)
{
	Name.RemoveFromStart(TEXT("M_PBR_Substrate_"));
	Name.RemoveFromStart(TEXT("M_PBR_"));
	Name.RemoveFromStart(TEXT("MI_PBR_"));
	Name.RemoveFromStart(TEXT("MI_"));
	Name.RemoveFromEnd(TEXT("_Family"));
	Name.RemoveFromEnd(TEXT("_材质族"));
	return MakeSafeItemName(Name, TEXT("Material"));
}

FString MakeFriendlyItemId(const FString& Category, const FString& ParentName, TSet<FString>& UsedIds)
{
	FString NormalizedCategory = Category;
	NormalizedCategory.ReplaceInline(TEXT("\\"), TEXT("/"));
	NormalizedCategory.TrimStartAndEndInline();

	FString BaseName;
	if (!NormalizedCategory.IsEmpty() &&
		NormalizedCategory != TEXT("Uncategorized") &&
		NormalizedCategory != TEXT("未分类"))
	{
		BaseName = TEXT("M_") + MakeSafeItemName(NormalizedCategory, TEXT("材质"));
	}
	else
	{
		BaseName = TEXT("M_未分类_") + SimplifyMaterialName(ParentName);
	}

	if (ParentName.Contains(TEXT("Substrate")) && !BaseName.Contains(TEXT("Substrate")))
	{
		const FString SubstrateName = MakeSafeItemName(BaseName + TEXT("_Substrate"), BaseName);
		if (!UsedIds.Contains(SubstrateName))
		{
			UsedIds.Add(SubstrateName);
			return SubstrateName;
		}
	}

	BaseName = MakeSafeItemName(BaseName, TEXT("M_材质"));
	if (!UsedIds.Contains(BaseName))
	{
		UsedIds.Add(BaseName);
		return BaseName;
	}

	for (int32 Suffix = 2; Suffix < 1000; ++Suffix)
	{
		const FString Candidate = FString::Printf(TEXT("%s_%02d"), *BaseName, Suffix);
		if (!UsedIds.Contains(Candidate))
		{
			UsedIds.Add(Candidate);
			return Candidate;
		}
	}

	const FString Fallback = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UsedIds.Add(Fallback);
	return Fallback;
}

UMaterialInterface* ResolveTopParentMaterial(UMaterialInstance* MaterialInstance)
{
	UMaterialInterface* Parent = MaterialInstance ? MaterialInstance->Parent : nullptr;
	while (UMaterialInstance* ParentInstance = Cast<UMaterialInstance>(Parent))
	{
		if (!ParentInstance->Parent)
		{
			break;
		}
		Parent = ParentInstance->Parent;
	}
	return Parent;
}

bool IsMaterialAssetClass(const FAssetData& AssetData)
{
	return AssetData.AssetClassPath == UMaterial::StaticClass()->GetClassPathName();
}

bool IsMaterialInstanceAssetClass(const FAssetData& AssetData)
{
	return AssetData.AssetClassPath == UMaterialInstance::StaticClass()->GetClassPathName() ||
		AssetData.AssetClassPath.GetAssetName().ToString().Contains(TEXT("MaterialInstance"));
}

FString InferCategoryFromAssetData(const FAssetData& AssetData)
{
	return FMaterialVaultScanner::InferCategory(AssetData.AssetName.ToString() + TEXT(" ") + AssetData.PackageName.ToString());
}

FString InferCategoryFromAssetGroup(const TArray<FAssetData>& Assets, int32 StartIndex, int32 EndIndex, const FAssetData* ParentAsset)
{
	TMap<FString, int32> CategoryScores;
	TMap<FString, int32> FirstSeen;
	int32 SeenIndex = 0;

	for (int32 Index = StartIndex; Index < EndIndex; ++Index)
	{
		if (!Assets.IsValidIndex(Index))
		{
			continue;
		}

		const FString Category = InferCategoryFromAssetData(Assets[Index]);
		if (Category == TEXT("未分类"))
		{
			continue;
		}

		++CategoryScores.FindOrAdd(Category);
		FirstSeen.FindOrAdd(Category, SeenIndex);
		++SeenIndex;
	}

	if (CategoryScores.Num() > 0)
	{
		FString BestCategory;
		int32 BestScore = 0;
		int32 BestFirstSeen = MAX_int32;
		for (const TPair<FString, int32>& Pair : CategoryScores)
		{
			const int32 CurrentFirstSeen = FirstSeen.FindRef(Pair.Key);
			if (Pair.Value > BestScore || (Pair.Value == BestScore && CurrentFirstSeen < BestFirstSeen))
			{
				BestCategory = Pair.Key;
				BestScore = Pair.Value;
				BestFirstSeen = CurrentFirstSeen;
			}
		}
		if (!BestCategory.IsEmpty())
		{
			return BestCategory;
		}
	}

	if (ParentAsset)
	{
		const FString ParentCategory = InferCategoryFromAssetData(*ParentAsset);
		if (ParentCategory != TEXT("未分类"))
		{
			return ParentCategory;
		}
	}

	return TEXT("未分类");
}

bool FindDirectMaterialParentPackage(
	IAssetRegistry& AssetRegistry,
	const FName InstancePackageName,
	const TSet<FName>& KnownMaterialPackages,
	const TSet<FName>& KnownMaterialInstancePackages,
	FName& OutParentPackageName)
{
	OutParentPackageName = NAME_None;

	TArray<FName> Dependencies;
	AssetRegistry.GetDependencies(
		InstancePackageName,
		Dependencies,
		UE::AssetRegistry::EDependencyCategory::Package,
		UE::AssetRegistry::EDependencyQuery::Hard);

	for (const FName& DependencyPackage : Dependencies)
	{
		if (KnownMaterialPackages.Contains(DependencyPackage) || KnownMaterialInstancePackages.Contains(DependencyPackage))
		{
			OutParentPackageName = DependencyPackage;
			return true;
		}
	}

	for (const FName& DependencyPackage : Dependencies)
	{
		TArray<FAssetData> DependencyAssets;
		AssetRegistry.GetAssetsByPackageName(DependencyPackage, DependencyAssets, true);
		for (const FAssetData& DependencyAsset : DependencyAssets)
		{
			if (IsMaterialAssetClass(DependencyAsset) || IsMaterialInstanceAssetClass(DependencyAsset))
			{
				OutParentPackageName = DependencyPackage;
				return true;
			}
		}
	}

	return false;
}

FName ResolveTopParentPackageFromRegistry(
	IAssetRegistry& AssetRegistry,
	const FName InstancePackageName,
	const TSet<FName>& KnownMaterialPackages,
	const TSet<FName>& KnownMaterialInstancePackages)
{
	FName CurrentPackage = InstancePackageName;
	TSet<FName> VisitedPackages;
	while (!CurrentPackage.IsNone() && !VisitedPackages.Contains(CurrentPackage))
	{
		VisitedPackages.Add(CurrentPackage);

		FName ParentPackage;
		if (!FindDirectMaterialParentPackage(
				AssetRegistry,
				CurrentPackage,
				KnownMaterialPackages,
				KnownMaterialInstancePackages,
				ParentPackage) ||
			ParentPackage.IsNone() ||
			ParentPackage == CurrentPackage)
		{
			return CurrentPackage == InstancePackageName ? NAME_None : CurrentPackage;
		}

		if (KnownMaterialPackages.Contains(ParentPackage))
		{
			return ParentPackage;
		}

		CurrentPackage = ParentPackage;
	}

	return NAME_None;
}
}

TArray<FAssetData> FMaterialVaultScanner::GatherMaterialAssetData(const FString& PackageRoot) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(*PackageRoot);
	Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	TArray<FAssetData> FilteredAssetDataList;
	FilteredAssetDataList.Reserve(AssetDataList.Num());
	TSet<FName> SeenPackages;
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (!AssetData.IsValid() || IsGeneratedMaterialVaultPackage(AssetData.PackageName.ToString()))
		{
			continue;
		}

		if (SeenPackages.Contains(AssetData.PackageName))
		{
			continue;
		}

		SeenPackages.Add(AssetData.PackageName);
		FilteredAssetDataList.Add(AssetData);
	}

	return FilteredAssetDataList;
}

TSharedPtr<FMaterialVaultItem> FMaterialVaultScanner::BuildItemLight(const FAssetData& AssetData) const
{
	TSharedPtr<FMaterialVaultItem> Item = MakeShared<FMaterialVaultItem>();
	Item->DisplayName = AssetData.AssetName.ToString();
	Item->Category = InferCategoryFromAssetData(AssetData);
	Item->Id = MakeItemId(AssetData);
	Item->SourceEngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Patch);
	Item->RootAsset = BuildEntry(AssetData, Item->Category, Item->Id, true);
	return Item;
}

TArray<TSharedPtr<FMaterialVaultItem>> FMaterialVaultScanner::ScanMaterials(const FString& PackageRoot, FMaterialVaultScanStats* OutStats) const
{
	TArray<FAssetData> AssetDataList = GatherMaterialAssetData(PackageRoot);
	TArray<TSharedPtr<FMaterialVaultItem>> Items;
	Items.Reserve(AssetDataList.Num());

	FMaterialVaultScanStats Stats;
	TArray<FAssetData> MaterialAssets;
	TMap<FName, FAssetData> MaterialAssetByPackage;
	TMap<FName, TArray<FAssetData>> InstanceAssetsByParentPackage;
	TSet<FName> MaterialPackages;
	TSet<FName> MaterialInstancePackages;
	TSet<FString> UsedItemIds;

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (IsMaterialAssetClass(AssetData))
		{
			MaterialPackages.Add(AssetData.PackageName);
		}
		else if (IsMaterialInstanceAssetClass(AssetData))
		{
			MaterialInstancePackages.Add(AssetData.PackageName);
		}
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	for (const FAssetData& AssetData : AssetDataList)
	{
		const FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
		if (IsMaterialAssetClass(AssetData))
		{
			++Stats.RawMaterialCount;
			MaterialAssets.Add(AssetData);
			MaterialAssetByPackage.Add(AssetData.PackageName, AssetData);
			continue;
		}

		if (IsMaterialInstanceAssetClass(AssetData))
		{
			++Stats.RawMaterialInstanceCount;
			const FName TopParentPackageName = ResolveTopParentPackageFromRegistry(
				AssetRegistry,
				AssetData.PackageName,
				MaterialPackages,
				MaterialInstancePackages);
			if (!TopParentPackageName.IsNone() && TopParentPackageName != AssetData.PackageName)
			{
				InstanceAssetsByParentPackage.FindOrAdd(TopParentPackageName).Add(AssetData);
			}
			else
			{
				TSharedPtr<FMaterialVaultItem> Item = BuildItemLight(AssetData);
				if (Item.IsValid())
				{
					++Stats.StandaloneMaterialInstanceCount;
					Items.Add(Item);
				}
			}
		}
	}

	for (const FAssetData& MaterialAsset : MaterialAssets)
	{
		if (InstanceAssetsByParentPackage.Contains(MaterialAsset.PackageName))
		{
			continue;
		}

		TSharedPtr<FMaterialVaultItem> Item = BuildItemLight(MaterialAsset);
		if (!Item.IsValid())
		{
			continue;
		}

		Item->Category = TEXT("\u6bcd\u6750\u8d28");
		Item->Id = MakeFriendlyItemId(Item->Category, MaterialAsset.AssetName.ToString(), UsedItemIds);
		Item->DisplayName = Item->Id;
		Item->RootAsset = BuildEntry(MaterialAsset, Item->Category, Item->Id, true);
		Items.Add(Item);
	}

	for (TPair<FName, TArray<FAssetData>>& Pair : InstanceAssetsByParentPackage)
	{
		TArray<FAssetData>& InstanceAssets = Pair.Value;
		if (InstanceAssets.IsEmpty())
		{
			continue;
		}

		InstanceAssets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.AssetName.LexicalLess(B.AssetName);
		});

		const FAssetData* ParentAsset = MaterialAssetByPackage.Find(Pair.Key);
		if (!ParentAsset)
		{
			TArray<FAssetData> ParentPackageAssets;
			AssetRegistry.GetAssetsByPackageName(Pair.Key, ParentPackageAssets, true);
			for (const FAssetData& CandidateParent : ParentPackageAssets)
			{
				if (CandidateParent.AssetClassPath == UMaterial::StaticClass()->GetClassPathName())
				{
					MaterialAssetByPackage.Add(Pair.Key, CandidateParent);
					ParentAsset = MaterialAssetByPackage.Find(Pair.Key);
					break;
				}
			}
		}

		const FString ParentName = ParentAsset ? ParentAsset->AssetName.ToString() : InstanceAssets[0].AssetName.ToString();
		constexpr int32 MaxInstancesPerFamilyPack = 20;
		const int32 ChunkCount = FMath::DivideAndRoundUp(InstanceAssets.Num(), MaxInstancesPerFamilyPack);
		for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			const int32 StartIndex = ChunkIndex * MaxInstancesPerFamilyPack;
			const int32 EndIndex = FMath::Min(StartIndex + MaxInstancesPerFamilyPack, InstanceAssets.Num());
			if (!InstanceAssets.IsValidIndex(StartIndex))
			{
				continue;
			}

			const FAssetData& RootInstanceAsset = InstanceAssets[StartIndex];
			TSharedPtr<FMaterialVaultItem> Item = MakeShared<FMaterialVaultItem>();
			Item->Category = InferCategoryFromAssetGroup(InstanceAssets, StartIndex, EndIndex, ParentAsset);
			const FString ChunkParentName = ChunkCount > 1
				? FString::Printf(TEXT("%s_%02d"), *ParentName, ChunkIndex + 1)
				: ParentName;
			Item->Id = MakeFriendlyItemId(Item->Category, ChunkParentName, UsedItemIds);
			Item->DisplayName = Item->Id;
			Item->SourceEngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Patch);
			Item->RootAsset = BuildEntry(RootInstanceAsset, Item->Category, Item->Id, true);

			if (ParentAsset)
			{
				AddUniqueEntryByPackage(Item->Dependencies, BuildEntry(*ParentAsset, TEXT("\u6bcd\u6750\u8d28"), Item->Id, false));
			}

			for (int32 InstanceIndex = StartIndex + 1; InstanceIndex < EndIndex; ++InstanceIndex)
			{
				AddUniqueEntryByPackage(Item->Dependencies, BuildEntry(InstanceAssets[InstanceIndex], Item->Category, Item->Id, false));
			}

			++Stats.MaterialFamilyCount;
			Items.Add(Item);
		}
		Stats.StandaloneMaterialInstanceCount += InstanceAssets.Num();
	}

	Stats.GeneratedItemCount = Items.Num();
	if (OutStats)
	{
		*OutStats = Stats;
	}

	Items.Sort([](const TSharedPtr<FMaterialVaultItem>& A, const TSharedPtr<FMaterialVaultItem>& B)
	{
		return A->DisplayName < B->DisplayName;
	});

	return Items;
}
bool FMaterialVaultScanner::IsGeneratedMaterialVaultPackage(const FString& PackageName)
{
	FString NormalizedPackageName = PackageName;
	NormalizedPackageName.ReplaceInline(TEXT("\\"), TEXT("/"));

	return NormalizedPackageName == TEXT("/Game/MaterialVault") ||
		NormalizedPackageName.StartsWith(TEXT("/Game/MaterialVault/")) ||
		NormalizedPackageName == TEXT("/Game/__MaterialVaultStaging") ||
		NormalizedPackageName.StartsWith(TEXT("/Game/__MaterialVaultStaging/")) ||
		NormalizedPackageName == TEXT("/Game/_MaterialVaultStaging") ||
		NormalizedPackageName.StartsWith(TEXT("/Game/_MaterialVaultStaging/"));
}

void FMaterialVaultScanner::PopulateDependencies(FMaterialVaultItem& Item) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	TArray<FName> PackageNamesToGather;
	PackageNamesToGather.AddUnique(*Item.RootAsset.OriginalPackageName);
	for (const FMaterialVaultAssetEntry& Dependency : Item.Dependencies)
	{
		PackageNamesToGather.AddUnique(*Dependency.OriginalPackageName);
	}

	TSet<FName> ParentPackagesGathered;
	TSet<FName> FunctionPackagesGathered;
	auto GatherMaterialInstanceParentChain = [this, &AssetRegistryModule, &Item, &Settings, &ParentPackagesGathered](const FAssetData& AssetData)
	{
		UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(AssetData.GetAsset());
		UMaterialInterface* Parent = MaterialInstance ? MaterialInstance->Parent : nullptr;
		while (Parent)
		{
			const FName ParentPackageName = Parent->GetOutermost()->GetFName();
			const FString ParentPackageString = ParentPackageName.ToString();
			if (ParentPackageName == AssetData.PackageName || IsGeneratedMaterialVaultPackage(ParentPackageString))
			{
				break;
			}
			if (!Settings->bIncludeEngineContentDependencies &&
				(ParentPackageString.StartsWith(TEXT("/Engine")) || ParentPackageString.StartsWith(TEXT("/Script"))))
			{
				break;
			}

			TArray<FAssetData> ParentPackageAssets;
			AssetRegistryModule.Get().GetAssetsByPackageName(ParentPackageName, ParentPackageAssets, true);
			for (const FAssetData& ParentAsset : ParentPackageAssets)
			{
				if (!ParentAsset.IsValid())
				{
					continue;
				}

				AddUniqueEntryByPackage(Item.Dependencies, BuildEntry(ParentAsset, Item.Category, Item.Id, false));
				if (!ParentPackagesGathered.Contains(ParentAsset.PackageName))
				{
					ParentPackagesGathered.Add(ParentAsset.PackageName);
					GatherDependencies(ParentAsset, Item);
				}
			}

			UMaterialInstance* ParentInstance = Cast<UMaterialInstance>(Parent);
			Parent = ParentInstance ? ParentInstance->Parent : nullptr;
		}
	};

	TFunction<void(UMaterialFunctionInterface*)> GatherFunctionAndDependencies;
	GatherFunctionAndDependencies = [this, &AssetRegistryModule, &Item, &Settings, &FunctionPackagesGathered, &GatherFunctionAndDependencies](UMaterialFunctionInterface* Function)
	{
		if (!Function)
		{
			return;
		}

		const FName FunctionPackageName = Function->GetOutermost()->GetFName();
		const FString FunctionPackageString = FunctionPackageName.ToString();
		if (FunctionPackagesGathered.Contains(FunctionPackageName) || IsGeneratedMaterialVaultPackage(FunctionPackageString))
		{
			return;
		}
		if (!Settings->bIncludeEngineContentDependencies &&
			(FunctionPackageString.StartsWith(TEXT("/Engine")) || FunctionPackageString.StartsWith(TEXT("/Script"))))
		{
			return;
		}

		FunctionPackagesGathered.Add(FunctionPackageName);

		TArray<FAssetData> FunctionPackageAssets;
		AssetRegistryModule.Get().GetAssetsByPackageName(FunctionPackageName, FunctionPackageAssets, true);
		for (const FAssetData& FunctionAsset : FunctionPackageAssets)
		{
			if (!FunctionAsset.IsValid())
			{
				continue;
			}

			AddUniqueEntryByPackage(Item.Dependencies, BuildEntry(FunctionAsset, Item.Category, Item.Id, false));
			GatherDependencies(FunctionAsset, Item);
		}

		TArray<UMaterialFunctionInterface*> NestedFunctions;
		Function->GetDependentFunctions(NestedFunctions);
		for (UMaterialFunctionInterface* NestedFunction : NestedFunctions)
		{
			GatherFunctionAndDependencies(NestedFunction);
		}
	};

	auto GatherMaterialDependentFunctions = [&GatherFunctionAndDependencies](const FAssetData& AssetData)
	{
		UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(AssetData.GetAsset());
		if (!MaterialInterface)
		{
			return;
		}

		TArray<UMaterialFunctionInterface*> DependentFunctions;
		MaterialInterface->GetDependentFunctions(DependentFunctions);
		for (UMaterialFunctionInterface* Function : DependentFunctions)
		{
			GatherFunctionAndDependencies(Function);
		}
	};

	for (const FName PackageName : PackageNamesToGather)
	{
		TArray<FAssetData> PackageAssets;
		AssetRegistryModule.Get().GetAssetsByPackageName(PackageName, PackageAssets, true);
		if (!PackageAssets.IsEmpty())
		{
			GatherDependencies(PackageAssets[0], Item);
			GatherMaterialInstanceParentChain(PackageAssets[0]);
			GatherMaterialDependentFunctions(PackageAssets[0]);
		}
	}
}

void FMaterialVaultScanner::GatherDependencies(const FAssetData& RootAsset, FMaterialVaultItem& OutItem) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();

	TSet<FName> VisitedPackages;
	TArray<FName> PendingPackages;
	PendingPackages.Add(RootAsset.PackageName);
	VisitedPackages.Add(RootAsset.PackageName);

	while (!PendingPackages.IsEmpty())
	{
		const FName CurrentPackage = PendingPackages.Pop(EAllowShrinking::No);

		TArray<FName> DependencyPackages;
		AssetRegistry.GetDependencies(
			CurrentPackage,
			DependencyPackages,
			UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::EDependencyQuery::Hard);

		for (const FName DependencyPackage : DependencyPackages)
		{
			if (VisitedPackages.Contains(DependencyPackage))
			{
				continue;
			}

			const FString DependencyPackageString = DependencyPackage.ToString();
			if (IsGeneratedMaterialVaultPackage(DependencyPackageString))
			{
				continue;
			}

			if (!Settings->bIncludeEngineContentDependencies &&
				(DependencyPackageString.StartsWith(TEXT("/Engine")) || DependencyPackageString.StartsWith(TEXT("/Script"))))
			{
				continue;
			}

			VisitedPackages.Add(DependencyPackage);
			PendingPackages.Add(DependencyPackage);

			TArray<FAssetData> PackageAssets;
			AssetRegistry.GetAssetsByPackageName(DependencyPackage, PackageAssets, true);
			for (const FAssetData& DependencyAsset : PackageAssets)
			{
				if (!DependencyAsset.IsValid())
				{
					continue;
				}

				AddUniqueEntryByPackage(OutItem.Dependencies, BuildEntry(DependencyAsset, OutItem.Category, OutItem.Id, false));
			}
		}
	}
}

FMaterialVaultAssetEntry FMaterialVaultScanner::BuildEntry(const FAssetData& AssetData, const FString& Category, const FString& ItemId, bool bIsRoot) const
{
	const UMaterialVaultSettings* Settings = GetDefault<UMaterialVaultSettings>();
	const EMaterialVaultAssetRole Role = ResolveRole(AssetData, bIsRoot);
	const FString Folder = ResolveFolderForRole(Role);
	const FString SafeCategory = MakeSafeSegment(Category, TEXT("Uncategorized"));
	const FString SafeItemId = MakeSafeSegment(ItemId, TEXT("Material"));
	FString ObjectName = MakeSafeSegment(AssetData.AssetName.ToString(), TEXT("Asset"));
	FString DependencySubFolder;
	if (!bIsRoot)
	{
		FString DepPkgPath = AssetData.PackageName.ToString();
		DepPkgPath.RemoveFromStart(TEXT("/Game/"));
		FString DepParentPath = FPaths::GetPath(DepPkgPath);
		TArray<FString> ParentSegments;
		DepParentPath.ParseIntoArray(ParentSegments, TEXT("/"), true);
		for (FString& Segment : ParentSegments)
		{
			Segment = MakeSafeSegment(Segment, TEXT("Dep"));
		}
		DependencySubFolder = FString::Join(ParentSegments, TEXT("/"));
	}
	if (Role == EMaterialVaultAssetRole::MaterialFunction)
	{
		// Material functions can contain nested function call nodes that are sensitive to renames.
		// Keep the object name stable and rely on the preserved source folder path for isolation.
	}
	else if (Role == EMaterialVaultAssetRole::Material || Role == EMaterialVaultAssetRole::RootMaterial)
	{
		ObjectName = MakeSafeSegment(AssetData.AssetName.ToString(), TEXT("Material"));
	}
	else if (Role == EMaterialVaultAssetRole::MaterialInstance)
	{
		ObjectName = MakeSafeSegment(AssetData.AssetName.ToString(), TEXT("Instance"));
	}
	else if (Role == EMaterialVaultAssetRole::Texture)
	{
		ObjectName = MakeSafeSegment(AssetData.AssetName.ToString(), TEXT("Texture"));
	}

	FString PlannedPackage = FString::Printf(
		TEXT("%s/%s/%s/%s"),
		*Settings->LibraryMountRoot,
		*SafeCategory,
		*SafeItemId,
		*Folder);
	if (!DependencySubFolder.IsEmpty())
	{
		PlannedPackage /= DependencySubFolder;
	}
	PlannedPackage /= ObjectName;

	FMaterialVaultAssetEntry Entry;
	Entry.OriginalObjectPath = AssetData.GetSoftObjectPath();
	Entry.OriginalPackageName = AssetData.PackageName.ToString();
	Entry.PlannedPackageName = PlannedPackage;
	Entry.PlannedObjectPath = PlannedPackage + TEXT(".") + ObjectName;
	Entry.AssetClass = AssetData.AssetClassPath.ToString();
	Entry.Role = Role;
	return Entry;
}

EMaterialVaultAssetRole FMaterialVaultScanner::ResolveRole(const FAssetData& AssetData, bool bIsRoot) const
{
	if (bIsRoot && AssetData.AssetClassPath == UMaterial::StaticClass()->GetClassPathName())
	{
		return EMaterialVaultAssetRole::RootMaterial;
	}

	const FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
	if (ClassPath == UMaterial::StaticClass()->GetClassPathName())
	{
		return EMaterialVaultAssetRole::Material;
	}

	if (ClassPath == UMaterialInstance::StaticClass()->GetClassPathName() ||
		AssetData.AssetClassPath.GetAssetName().ToString().Contains(TEXT("MaterialInstance")))
	{
		return EMaterialVaultAssetRole::MaterialInstance;
	}

	if (AssetData.AssetClassPath.GetAssetName().ToString().StartsWith(TEXT("Texture")))
	{
		return EMaterialVaultAssetRole::Texture;
	}

	if (ClassPath == UMaterialFunctionInterface::StaticClass()->GetClassPathName() ||
		AssetData.AssetClassPath.GetAssetName().ToString().Contains(TEXT("MaterialFunction")))
	{
		return EMaterialVaultAssetRole::MaterialFunction;
	}

	return EMaterialVaultAssetRole::Other;
}

FString FMaterialVaultScanner::ResolveFolderForRole(EMaterialVaultAssetRole Role) const
{
	switch (Role)
	{
	case EMaterialVaultAssetRole::RootMaterial:
	case EMaterialVaultAssetRole::Material:
		return TEXT("Materials");
	case EMaterialVaultAssetRole::MaterialInstance:
		return TEXT("Instances");
	case EMaterialVaultAssetRole::Texture:
		return TEXT("Textures");
	case EMaterialVaultAssetRole::MaterialFunction:
		return TEXT("Functions");
	default:
		return TEXT("Dependencies");
	}
}

FString FMaterialVaultScanner::MakeSafeSegment(const FString& Value, const FString& Fallback)
{
	FString Result = Value.IsEmpty() ? Fallback : Value;
	const TCHAR* InvalidChars = INVALID_LONGPACKAGE_CHARACTERS;
	for (; *InvalidChars; ++InvalidChars)
	{
		Result.ReplaceCharInline(*InvalidChars, TEXT('_'));
	}
	Result.ReplaceInline(TEXT(" "), TEXT("_"));
	return Result;
}

FString FMaterialVaultScanner::MakeItemId(const FAssetData& AssetData)
{
	FString PackagePath = AssetData.PackageName.ToString();
	if (PackagePath.StartsWith(TEXT("/Game/")))
	{
		PackagePath.RightChopInline(6);
	}
	PackagePath.ReplaceInline(TEXT("/"), TEXT("_"));
	return TEXT("MV_") + MakeSafeSegment(PackagePath, TEXT("Material"));
}

FString FMaterialVaultScanner::InferCategory(const FString& Name)
{
	const FString LowerName = Name.ToLower();
	auto HasAny = [&LowerName](std::initializer_list<const TCHAR*> Keys)
	{
		for (const TCHAR* Key : Keys)
		{
			if (LowerName.Contains(Key))
			{
				return true;
			}
		}
		return false;
	};

	if (HasAny({TEXT("stain"), TEXT("dirt"), TEXT("wugou"), TEXT("mold"), TEXT("mildew"), TEXT("leak"),
		TEXT("rust_spot"), TEXT("grime"), TEXT("smudge"), TEXT("污垢"), TEXT("污渍"),
		TEXT("霉斑"), TEXT("水渍"), TEXT("脏"), TEXT("锈迹"), TEXT("油污")})) return TEXT("污垢");

	if (HasAny({TEXT("decal"), TEXT("graffiti"), TEXT("scratch"), TEXT("crack"),
		TEXT("贴花"), TEXT("涂鸦"), TEXT("破损"), TEXT("划痕"), TEXT("龟裂")})) return TEXT("贴花");

	if (HasAny({TEXT("leather"), TEXT("pige"), TEXT("suede"), TEXT("nubuck"), TEXT("patent"),
		TEXT("皮革"), TEXT("皮壳"), TEXT("真皮"), TEXT("人造革"), TEXT("麂皮"),
		TEXT("翻毛皮"), TEXT("漆皮"), TEXT("牛皮"), TEXT("羊皮"), TEXT("皮纹"),
		TEXT("鳄鱼皮"), TEXT("蛇皮")})) return TEXT("皮革");

	if (HasAny({TEXT("sheer"), TEXT("curtain"), TEXT("gauze"), TEXT("chuangsha"), TEXT("窗纱"),
		TEXT("纱帘"), TEXT("纱"), TEXT("薄纱")})) return TEXT("布料/窗纱");
	if (HasAny({TEXT("silk"), TEXT("satin"), TEXT("velvet"), TEXT("丝绸"),
		TEXT("缎"), TEXT("丝绒"), TEXT("天鹅绒")})) return TEXT("布料/丝绸");
	if (HasAny({TEXT("carpet"), TEXT("rug"), TEXT("ditan"), TEXT("mat"), TEXT("地毯"),
		TEXT("地垫"), TEXT("毯")})) return TEXT("布料/地毯");
	if (HasAny({TEXT("canvas"), TEXT("denim"), TEXT("jeans"), TEXT("帆布"),
		TEXT("牛仔布")})) return TEXT("布料/帆布");
	if (HasAny({TEXT("fabric"), TEXT("cloth"), TEXT("textile"), TEXT("linen"),
		TEXT("cotton"), TEXT("wool"), TEXT("polyester"), TEXT("nylon"),
		TEXT("weave"), TEXT("knit"), TEXT("twill"), TEXT("felt"), TEXT("buliao"),
		TEXT("布"), TEXT("织物"), TEXT("布艺"), TEXT("纺织"),
		TEXT("棉"), TEXT("麻"), TEXT("毛料"), TEXT("化纤"), TEXT("尼龙"),
		TEXT("绒布"), TEXT("网眼"), TEXT("刺绣")})) return TEXT("布料");

	if (HasAny({TEXT("rust"), TEXT("corrod"), TEXT("oxidiz"), TEXT("锈"),
		TEXT("腐蚀")})) return TEXT("金属/锈蚀");
	if (HasAny({TEXT("gold"), TEXT("silver"), TEXT("金银"), TEXT("黄金"),
		TEXT("白银"), TEXT("镀金")})) return TEXT("金属/金银");
	if (HasAny({TEXT("copper"), TEXT("bronze"), TEXT("brass"), TEXT("铜"),
		TEXT("黄铜"), TEXT("青铜")})) return TEXT("金属/铜");
	if (HasAny({TEXT("aluminum"), TEXT("aluminium"), TEXT("铝")})) return TEXT("金属/铝");
	if (HasAny({TEXT("chrome"), TEXT("chrom"), TEXT("铬"), TEXT("镀铬")})) return TEXT("金属/铬");
	if (HasAny({TEXT("metal"), TEXT("steel"), TEXT("iron"), TEXT("metallic"), TEXT("jinshu"),
		TEXT("金属"), TEXT("钢"), TEXT("铁"), TEXT("合金"),
		TEXT("不锈钢"), TEXT("铸铁"), TEXT("锌"), TEXT("钛"),
		TEXT("镍"), TEXT("锡")})) return TEXT("金属");

	if (HasAny({TEXT("woodfloor"), TEXT("wood_floor"), TEXT("floorwood"),
		TEXT("parquet"), TEXT("木地板"), TEXT("地板木")})) return TEXT("木材/木地板");
	if (HasAny({TEXT("plywood"), TEXT("veneer"), TEXT("胶合板"), TEXT("饰面板"),
		TEXT("木皮"), TEXT("贴面板")})) return TEXT("木材/饰面板");
	if (HasAny({TEXT("bamboo"), TEXT("竹子"), TEXT("竹"), TEXT("竹木")})) return TEXT("木材/竹");
	if (HasAny({TEXT("bark"), TEXT("树皮")})) return TEXT("木材/树皮");
	if (HasAny({TEXT("wood"), TEXT("timber"), TEXT("muwen"), TEXT("oak"), TEXT("walnut"),
		TEXT("maple"), TEXT("birch"), TEXT("pine"), TEXT("mahogany"),
		TEXT("cherry"), TEXT("teak"), TEXT("ebony"), TEXT("ash"),
		TEXT("beech"), TEXT("cedar"), TEXT("elm"), TEXT("hickory"),
		TEXT("木纹"), TEXT("木材"), TEXT("木"), TEXT("原木"),
		TEXT("实木"), TEXT("胡桃"), TEXT("橡木"), TEXT("松木"),
		TEXT("桦木"), TEXT("枫木"), TEXT("柚木"), TEXT("樱桃木"),
		TEXT("红木")})) return TEXT("木材");

	if (HasAny({TEXT("marble"), TEXT("travertine"), TEXT("onyx"), TEXT("大理石"),
		TEXT("洞石"), TEXT("玉石"), TEXT("玛瑙")})) return TEXT("石材/大理石");
	if (HasAny({TEXT("granite"), TEXT("花岗"), TEXT("花岗岩"), TEXT("玄武岩")})) return TEXT("石材/花岗岩");
	if (HasAny({TEXT("brick"), TEXT("tile_roof"), TEXT("zhuankuai"), TEXT("砖"), TEXT("瓦"),
		TEXT("红砖"), TEXT("石砖")})) return TEXT("石材/砖瓦");
	if (HasAny({TEXT("pebble"), TEXT("gravel"), TEXT("卵石"), TEXT("碎石"),
		TEXT("鹅卵石")})) return TEXT("石材/卵石");
	if (HasAny({TEXT("slate"), TEXT("板岩"), TEXT("石板")})) return TEXT("石材/板岩");
	if (HasAny({TEXT("stone"), TEXT("rock"), TEXT("shicai"), TEXT("limestone"), TEXT("sandstone"),
		TEXT("石"), TEXT("岩"), TEXT("石灰"), TEXT("砂岩"),
		TEXT("页岩"), TEXT("火山岩"), TEXT("毛石"), TEXT("礁石")})) return TEXT("石材");

	if (HasAny({TEXT("concrete"), TEXT("cement"), TEXT("hunningtu"), TEXT("shigao"), TEXT("plaster"), TEXT("mortar"),
		TEXT("grout"), TEXT("terrazzo"),
		TEXT("混凝土"), TEXT("水泥"), TEXT("灰泥"), TEXT("石膏"),
		TEXT("白水泥"), TEXT("砂浆"), TEXT("水磨石")})) return TEXT("混凝土");

	if (HasAny({TEXT("glass"), TEXT("glazing"), TEXT("boli"), TEXT("frosted"), TEXT("stainedglass"),
		TEXT("玻璃"), TEXT("磨砂"), TEXT("彩绘玻璃"),
		TEXT("玻璃门"), TEXT("玻璃窗"), TEXT("钢化玻璃"),
		TEXT("透明")})) return TEXT("玻璃");

	if (HasAny({TEXT("wallpaper"), TEXT("壁纸"), TEXT("墙纸"), TEXT("墙布")})) return TEXT("墙漆/壁纸");
	if (HasAny({TEXT("paint"), TEXT("stucco"), TEXT("render"), TEXT("涂料"),
		TEXT("油漆"), TEXT("乳胶漆"), TEXT("墙漆"), TEXT("刷墙"),
		TEXT("粉刷"), TEXT("肌理漆")})) return TEXT("墙漆/涂料");
	if (HasAny({TEXT("wall"), TEXT("墙面"), TEXT("墙"), TEXT("墙壁")})) return TEXT("墙漆");

	if (HasAny({TEXT("ceramic"), TEXT("porcelain"), TEXT("tile"), TEXT("瓷砖"),
		TEXT("地砖"), TEXT("墙砖"), TEXT("马赛克")})) return TEXT("地面/瓷砖");
	if (HasAny({TEXT("asphalt"), TEXT("tarmac"), TEXT("沥青"), TEXT("柏油")})) return TEXT("地面/沥青");
	if (HasAny({TEXT("floor"), TEXT("flooring"), TEXT("地面"), TEXT("铺地"),
		TEXT("自流平")})) return TEXT("地面");

	if (HasAny({TEXT("plastic"), TEXT("rubber"), TEXT("vinyl"), TEXT("polymer"),
		TEXT("acrylic"), TEXT("resin"), TEXT("epoxy"), TEXT("silicone"),
		TEXT("塑料"), TEXT("橡胶"), TEXT("树脂"), TEXT("亚克力"),
		TEXT("有机玻璃"), TEXT("pvc"), TEXT("硅胶")})) return TEXT("塑料");

	if (HasAny({TEXT("grass"), TEXT("lawn"), TEXT("turf"), TEXT("草"),
		TEXT("草坪")})) return TEXT("自然/草地");
	if (HasAny({TEXT("moss"), TEXT("lichen"), TEXT("苔藓"), TEXT("地衣")})) return TEXT("自然/苔藓");
	if (HasAny({TEXT("leaf"), TEXT("leaves"), TEXT("foliage"), TEXT("叶"),
		TEXT("树叶"), TEXT("植被"), TEXT("枝")})) return TEXT("自然/树叶");
	if (HasAny({TEXT("snow"), TEXT("ice"), TEXT("frost"), TEXT("雪"),
		TEXT("冰"), TEXT("霜"), TEXT("冰霜")})) return TEXT("自然/雪");
	if (HasAny({TEXT("water"), TEXT("liquid"), TEXT("fluid"), TEXT("水"),
		TEXT("液体")})) return TEXT("自然/水");
	if (HasAny({TEXT("sand"), TEXT("beach"), TEXT("desert"), TEXT("沙"),
		TEXT("沙滩"), TEXT("沙漠")})) return TEXT("自然/沙");
	if (HasAny({TEXT("soil"), TEXT("mud"), TEXT("earth"), TEXT("ground"),
		TEXT("土"), TEXT("泥"), TEXT("土壤"), TEXT("地面土"),
		TEXT("地表")})) return TEXT("自然/泥土");

	if (HasAny({TEXT("skin"), TEXT("pore"), TEXT("dermal"), TEXT("皮肤"),
		TEXT("毛孔"), TEXT("肤色")})) return TEXT("皮肤");

	if (HasAny({TEXT("emissive"), TEXT("glow"), TEXT("zifaguang"), TEXT("neon"), TEXT("led"),
		TEXT("发光"), TEXT("自发光"), TEXT("霓虹")})) return TEXT("自发光");

	if (HasAny({TEXT("skybox"), TEXT("sky"), TEXT("cloud"), TEXT("hdri"),
		TEXT("天空"), TEXT("云"), TEXT("环境")})) return TEXT("自然/天空");

	if (HasAny({TEXT("tech"), TEXT("scifi"), TEXT("hologram"), TEXT("cyber"),
		TEXT("科技"), TEXT("科幻"), TEXT("全息")})) return TEXT("科技");

	const FString DefaultCategory = GetDefault<UMaterialVaultSettings>()->DefaultCategory;
	return DefaultCategory.IsEmpty() || DefaultCategory.Equals(TEXT("Uncategorized"), ESearchCase::IgnoreCase)
		? TEXT("未分类")
		: DefaultCategory;
}
