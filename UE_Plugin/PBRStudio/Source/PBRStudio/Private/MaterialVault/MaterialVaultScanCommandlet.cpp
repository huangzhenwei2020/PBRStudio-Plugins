#include "MaterialVaultScanCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetCompilingManager.h"
#include "MaterialVaultScanner.h"
#include "MaterialVaultSettings.h"
#include "SMaterialVaultWindow.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "MaterialExpressionIO.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "UObject/GarbageCollection.h"

static void ValidateMaterialVaultExpressions(
	const FString& OwnerPath,
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions,
	TSet<const UObject*>& VisitedFunctions,
	int32& ErrorCount)
{
	TSet<UMaterialExpression*> ExpressionSet;
	for (const TObjectPtr<UMaterialExpression>& ExpressionPtr : Expressions)
	{
		if (UMaterialExpression* Expression = ExpressionPtr.Get())
		{
			ExpressionSet.Add(Expression);
		}
	}

	for (const TObjectPtr<UMaterialExpression>& ExpressionPtr : Expressions)
	{
		UMaterialExpression* Expression = ExpressionPtr.Get();
		if (!Expression)
		{
			continue;
		}

		for (int32 InputIndex = 0;; ++InputIndex)
		{
			const FExpressionInput* Input = Expression->GetInput(InputIndex);
			if (!Input)
			{
				break;
			}

			const FName InputName = Expression->GetInputName(InputIndex);
			if (Input->Expression && !ExpressionSet.Contains(Input->Expression))
			{
				++ErrorCount;
				UE_LOG(LogTemp, Error, TEXT("Material Vault verification found stale/external input '%s' in %s: %s -> %s"),
					*InputName.ToString(),
					*OwnerPath,
					*Expression->GetPathName(),
					*Input->Expression->GetPathName());
			}
		}

		if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (!FunctionCall->MaterialFunction)
			{
				++ErrorCount;
				UE_LOG(LogTemp, Error, TEXT("Material Vault verification found missing material function in %s"), *OwnerPath);
			}
			else if (!VisitedFunctions.Contains(FunctionCall->MaterialFunction))
			{
				VisitedFunctions.Add(FunctionCall->MaterialFunction);
				FunctionCall->UpdateFromFunctionResource(false);
				ValidateMaterialVaultExpressions(
					FunctionCall->MaterialFunction->GetPathName(),
					FunctionCall->MaterialFunction->GetExpressions(),
					VisitedFunctions,
					ErrorCount);
			}
		}

		if (UMaterialExpressionStaticSwitchParameter* StaticSwitch = Cast<UMaterialExpressionStaticSwitchParameter>(Expression))
		{
			if (!StaticSwitch->A.Expression)
			{
				++ErrorCount;
				UE_LOG(LogTemp, Error, TEXT("Material Vault verification found StaticSwitchParameter missing A input in %s: %s"), *OwnerPath, *StaticSwitch->GetPathName());
			}
		}

		if (UMaterialExpressionTextureSampleParameter2D* TextureSample = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
		{
			if (!TextureSample->Texture && !TextureSample->TextureObject.Expression)
			{
				++ErrorCount;
				UE_LOG(LogTemp, Error, TEXT("Material Vault verification found TextureSampleParameter2D without Texture2D in %s: %s"), *OwnerPath, *TextureSample->GetPathName());
			}
		}
	}
}

static int32 ValidateMaterialVaultMaterialGraph(UMaterialInterface* MaterialInterface)
{
	int32 ErrorCount = 0;
	TSet<const UObject*> VisitedFunctions;

	if (UMaterial* Material = Cast<UMaterial>(MaterialInterface))
	{
		ValidateMaterialVaultExpressions(Material->GetPathName(), Material->GetExpressions(), VisitedFunctions, ErrorCount);
	}

	TArray<UMaterialFunctionInterface*> DependentFunctions;
	MaterialInterface->GetDependentFunctions(DependentFunctions);
	for (UMaterialFunctionInterface* Function : DependentFunctions)
	{
		if (!Function || VisitedFunctions.Contains(Function))
		{
			continue;
		}

		VisitedFunctions.Add(Function);
		Function->UpdateFromFunctionResource();
		ValidateMaterialVaultExpressions(Function->GetPathName(), Function->GetExpressions(), VisitedFunctions, ErrorCount);
	}

	return ErrorCount;
}

UMaterialVaultScanCommandlet::UMaterialVaultScanCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}


int32 UMaterialVaultScanCommandlet::Main(const FString& Params)
{
	FString OutputPath;
	if (!FParse::Value(*Params, TEXT("MaterialVaultOutput="), OutputPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Missing -MaterialVaultOutput=<file>"));
		return 1;
	}

	FString PackageRoot = TEXT("/Game");
	FParse::Value(*Params, TEXT("MaterialVaultRoot="), PackageRoot);

	FString VaultRoot;
	const bool bGeneratePacks = FParse::Param(*Params, TEXT("MaterialVaultGeneratePacks"));
	if (FParse::Value(*Params, TEXT("MaterialVaultVaultRoot="), VaultRoot) && !VaultRoot.IsEmpty())
	{
		UMaterialVaultSettings* Settings = GetMutableDefault<UMaterialVaultSettings>();
		Settings->ExternalVaultRoot.Path = VaultRoot;
		FString ActiveLibraryName;
		if (FParse::Value(*Params, TEXT("MaterialVaultActiveLibraryName="), ActiveLibraryName) && !ActiveLibraryName.IsEmpty())
		{
			Settings->ActiveLibraryName = ActiveLibraryName;
		}
		Settings->SaveConfig();
	}

	FString RepairPackPath;
	if (FParse::Value(*Params, TEXT("MaterialVaultRepairPackThumbnail="), RepairPackPath) && !RepairPackPath.IsEmpty())
	{
		const bool bAllowRender = FParse::Param(*Params, TEXT("MaterialVaultRenderThumbnails"));
		UE_LOG(LogTemp, Display, TEXT("Material Vault repair thumbnail started. Pack: %s, Render: %s"),
			*RepairPackPath,
			bAllowRender ? TEXT("true") : TEXT("false"));

		FText RepairError;
		if (!SMaterialVaultWindow::RepairMvpackThumbnailInCurrentProcess(RepairPackPath, RepairError, bAllowRender))
		{
			UE_LOG(LogTemp, Error, TEXT("Material Vault failed to repair pack thumbnail: %s"), *RepairError.ToString());
			return 7;
		}

		UE_LOG(LogTemp, Display, TEXT("Material Vault repair thumbnail completed. Pack: %s"), *RepairPackPath);
		return 0;
	}

	const bool bRebuildIndexOnly = FParse::Param(*Params, TEXT("MaterialVaultRebuildIndex"));
	if (bRebuildIndexOnly)
	{
		FText IndexError;
		FString IndexPath;
		int32 IndexedPackCount = 0;
		const FString ActiveLibraryName = GetDefault<UMaterialVaultSettings>()->ActiveLibraryName.IsEmpty()
			? TEXT("\u9ed8\u8ba4\u6750\u8d28\u5e93")
			: GetDefault<UMaterialVaultSettings>()->ActiveLibraryName;
		const FString IndexVaultRoot = FPaths::Combine(VaultRoot, ActiveLibraryName);
		if (!SMaterialVaultWindow::RebuildPackIndex(IndexVaultRoot, IndexPath, IndexedPackCount, IndexError))
		{
			UE_LOG(LogTemp, Error, TEXT("Material Vault failed to rebuild pack index: %s"), *IndexError.ToString());
			return 4;
		}
		UE_LOG(LogTemp, Display, TEXT("Material Vault rebuilt pack index. Packs: %d, Index: %s"), IndexedPackCount, *IndexPath);
		return 0;
	}

	FString InstallPackPath;
	if (FParse::Value(*Params, TEXT("MaterialVaultInstallPack="), InstallPackPath) && !InstallPackPath.IsEmpty())
	{
		const FString StagingRoot = GetDefault<UMaterialVaultSettings>()->StagingMountRoot;
		FString StagingRelativeRoot = StagingRoot;
		StagingRelativeRoot.RemoveFromStart(TEXT("/Game/"));
		if (!StagingRelativeRoot.IsEmpty())
		{
			const FString StagingDirectory = FPaths::Combine(FPaths::ProjectContentDir(), StagingRelativeRoot);
			IFileManager::Get().DeleteDirectory(*StagingDirectory, false, true);
		}

		FString InstallRoot;
		FText InstallError;
		if (!SMaterialVaultWindow::InstallMvpack(InstallPackPath, InstallRoot, InstallError, EMaterialVaultInstallMode::Staging))
		{
			UE_LOG(LogTemp, Error, TEXT("Material Vault failed to install pack for verification: %s"), *InstallError.ToString());
			return 3;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		AssetRegistry.ScanPathsSynchronous({ StagingRoot }, true);
		AssetRegistry.WaitForCompletion();

		TArray<FAssetData> InstalledAssets;
		const FString VerifyRoot = StagingRoot;
		AssetRegistry.GetAssetsByPath(*VerifyRoot, InstalledAssets, true, false);
		int32 LoadedMaterialCount = 0;
		int32 LoadedMaterialInstanceCount = 0;
		int32 MissingParentCount = 0;
		int32 MaterialGraphErrorCount = 0;
		for (const FAssetData& AssetData : InstalledAssets)
		{
			UClass* AssetClass = AssetData.GetClass();
			if (AssetClass && AssetClass->IsChildOf(UMaterialInterface::StaticClass()))
			{
				UMaterialInterface* MaterialInterface = LoadObject<UMaterialInterface>(nullptr, *AssetData.GetSoftObjectPath().ToString());
				if (MaterialInterface)
				{
					++LoadedMaterialCount;
					if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface))
					{
						++LoadedMaterialInstanceCount;
						if (!MaterialInstance->Parent)
						{
							++MissingParentCount;
							UE_LOG(LogTemp, Error, TEXT("Material Vault verification found material instance without parent: %s"), *AssetData.GetSoftObjectPath().ToString());
						}
					}
					MaterialGraphErrorCount += ValidateMaterialVaultMaterialGraph(MaterialInterface);
				}
			}
		}

		if (MissingParentCount > 0 || MaterialGraphErrorCount > 0)
		{
			return MissingParentCount > 0 ? 5 : 6;
		}

		UE_LOG(LogTemp, Display, TEXT("Material Vault verification install completed. Loaded materials: %d, Loaded instances: %d, Missing parents: %d, Material graph errors: %d, Root: %s"), LoadedMaterialCount, LoadedMaterialInstanceCount, MissingParentCount, MaterialGraphErrorCount, *VerifyRoot);
		return 0;
	}

	FMaterialVaultScanner Scanner;

	// Phase 0: Ensure AssetRegistry has scanned all assets before querying it.
	// In commandlet mode the registry is normally lazy; force a full sync.
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		AssetRegistry.SearchAllAssets(true);
		AssetRegistry.WaitForCompletion();
	}

	// Phase 1: Build material entries. Material instances are grouped by parent material.
	UE_LOG(LogTemp, Display, TEXT("MV_PROGRESS:SCAN_START"));
	FMaterialVaultScanStats ScanStats;
	TArray<TSharedPtr<FMaterialVaultItem>> AllItems = Scanner.ScanMaterials(PackageRoot, &ScanStats);
	FString OnlyIdsText;
	if (FParse::Value(*Params, TEXT("MaterialVaultOnlyIds="), OnlyIdsText) && !OnlyIdsText.IsEmpty())
	{
		TArray<FString> OnlyIds;
		OnlyIdsText.ParseIntoArray(OnlyIds, TEXT(";"), true);
		TSet<FString> OnlyIdSet;
		for (FString& OnlyId : OnlyIds)
		{
			OnlyId.TrimStartAndEndInline();
			if (!OnlyId.IsEmpty())
			{
				OnlyIdSet.Add(OnlyId);
			}
		}

		if (!OnlyIdSet.IsEmpty())
		{
			AllItems.RemoveAll([&OnlyIdSet](const TSharedPtr<FMaterialVaultItem>& Item)
			{
				return !Item.IsValid() || !OnlyIdSet.Contains(Item->Id);
			});
		}
	}
	const int32 TotalMaterialCount = AllItems.Num();
	UE_LOG(LogTemp, Display, TEXT("MV_PROGRESS:SCAN_DONE:%d:%d:%d:%d:%d"),
		TotalMaterialCount,
		ScanStats.RawMaterialCount,
		ScanStats.RawMaterialInstanceCount,
		ScanStats.StandaloneMaterialInstanceCount,
		ScanStats.SkippedAssetCount);

	const int32 BatchSize = 200;
	const int32 TotalBatches = FMath::DivideAndRoundUp(TotalMaterialCount, BatchSize);

	int32 TotalGeneratedPackCount = 0;
	FString LastPackPath;
	int32 MaterialIndex = 0;

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	TUniquePtr<FArchive> OutputWriter(IFileManager::Get().CreateFileWriter(*OutputPath));
	if (!OutputWriter)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create Material Vault scan output: %s"), *OutputPath);
		return 2;
	}

	auto WriteJson = [&OutputWriter](const FString& Text) -> bool
	{
		FTCHARToUTF8 Converted(*Text);
		if (Converted.Length() > 0)
		{
			OutputWriter->Serialize(const_cast<ANSICHAR*>(Converted.Get()), Converted.Length());
		}
		return !OutputWriter->IsError();
	};

	if (!WriteJson(TEXT("{\n")) ||
		!WriteJson(TEXT("  \"schemaVersion\": 1,\n")) ||
		!WriteJson(FString::Printf(TEXT("  \"projectDir\": \"%s\",\n"), *JsonEscape(FPaths::ProjectDir()))) ||
		!WriteJson(FString::Printf(TEXT("  \"packageRoot\": \"%s\",\n"), *JsonEscape(PackageRoot))) ||
		!WriteJson(FString::Printf(TEXT("  \"libraryMountRoot\": \"%s\",\n"), *JsonEscape(GetDefault<UMaterialVaultSettings>()->LibraryMountRoot))) ||
		!WriteJson(FString::Printf(TEXT("  \"materialCount\": %d,\n"), TotalMaterialCount)) ||
		!WriteJson(TEXT("  \"materials\": [\n")))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to write Material Vault scan output header: %s"), *OutputPath);
		return 2;
	}

	for (int32 BatchIndex = 0; BatchIndex < TotalBatches; ++BatchIndex)
	{
		const int32 StartIdx = BatchIndex * BatchSize;
		const int32 EndIdx = FMath::Min(StartIdx + BatchSize, TotalMaterialCount);

		UE_LOG(LogTemp, Display, TEXT("MV_PROGRESS:BATCH:%d:%d"), BatchIndex + 1, TotalBatches);

		// Phase 2: Slice grouped items for this batch
		TArray<TSharedPtr<FMaterialVaultItem>> BatchItems;
		BatchItems.Reserve(EndIdx - StartIdx);
		for (int32 i = StartIdx; i < EndIdx; ++i)
		{
			BatchItems.Add(AllItems[i]);
		}

		// Phase 3: Generate packs and append JSON
		int32 BatchPackCount = 0;
		for (const TSharedPtr<FMaterialVaultItem>& Item : BatchItems)
		{
			if (!Item.IsValid())
			{
				continue;
			}

			++MaterialIndex;
			if (bGeneratePacks)
			{
				UE_LOG(LogTemp, Display, TEXT("MV_PROGRESS:PACK:%d:%d:%s"), MaterialIndex, TotalMaterialCount, *Item->DisplayName);
				FText Error;
				FString PackPath;
				if (SMaterialVaultWindow::GenerateMvpackForItem(*Item, PackPath, Error))
				{
					++BatchPackCount;
					++TotalGeneratedPackCount;
					LastPackPath = PackPath;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Material Vault failed to generate pack for %s: %s"), *Item->DisplayName, *Error.ToString());
				}
				FAssetCompilingManager::Get().ProcessAsyncTasks(false);
				CollectGarbage(RF_NoFlags);
				FPlatformProcess::Sleep(0.08f);
			}

			// Phase 4: Append material entry to JSON. Keep this per-item so large
			// projects do not retain the whole output document in memory.
			FString ItemJson;
			ItemJson.Reserve(1024 + Item->Dependencies.Num() * 256);
			ItemJson += TEXT("    {\n");
			ItemJson += FString::Printf(TEXT("      \"id\": \"%s\",\n"), *JsonEscape(Item->Id));
			ItemJson += FString::Printf(TEXT("      \"displayName\": \"%s\",\n"), *JsonEscape(Item->DisplayName));
			ItemJson += FString::Printf(TEXT("      \"category\": \"%s\",\n"), *JsonEscape(Item->Category));
			ItemJson += FString::Printf(TEXT("      \"sourceEngineVersion\": \"%s\",\n"), *JsonEscape(Item->SourceEngineVersion));
			ItemJson += FString::Printf(TEXT("      \"originalRoot\": \"%s\",\n"), *JsonEscape(Item->RootAsset.OriginalPackageName));
			ItemJson += FString::Printf(TEXT("      \"plannedRoot\": \"%s\",\n"), *JsonEscape(Item->RootAsset.PlannedPackageName));
			ItemJson += FString::Printf(TEXT("      \"dependencyCount\": %d,\n"), Item->Dependencies.Num());
			ItemJson += TEXT("      \"dependencies\": [\n");
			for (int32 DependencyIndex = 0; DependencyIndex < Item->Dependencies.Num(); ++DependencyIndex)
			{
				const FMaterialVaultAssetEntry& Dependency = Item->Dependencies[DependencyIndex];
				ItemJson += TEXT("        {\n");
				ItemJson += FString::Printf(TEXT("          \"originalPackageName\": \"%s\",\n"), *JsonEscape(Dependency.OriginalPackageName));
				ItemJson += FString::Printf(TEXT("          \"plannedPackageName\": \"%s\",\n"), *JsonEscape(Dependency.PlannedPackageName));
				ItemJson += FString::Printf(TEXT("          \"assetClass\": \"%s\"\n"), *JsonEscape(Dependency.AssetClass));
				ItemJson += DependencyIndex + 1 < Item->Dependencies.Num() ? TEXT("        },\n") : TEXT("        }\n");
			}
			ItemJson += TEXT("      ]\n");
			ItemJson += MaterialIndex < TotalMaterialCount ? TEXT("    },\n") : TEXT("    }\n");
			if (!WriteJson(ItemJson))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to write Material Vault scan output item: %s"), *OutputPath);
				return 2;
			}
		}

		UE_LOG(LogTemp, Display, TEXT("MV_PROGRESS:BATCH_DONE:%d:%d:%d"), BatchIndex + 1, TotalBatches, BatchPackCount);

		// Phase 5: Clear batch item handles. Avoid forced GC in commandlets here; UE 5.7 can
		// still hold typed-element references after thumbnail rendering and crash during GC.
		BatchItems.Empty();
	}

	if (!WriteJson(TEXT("  ],\n")) ||
		!WriteJson(FString::Printf(TEXT("  \"generatedPackCount\": %d,\n"), TotalGeneratedPackCount)) ||
		!WriteJson(FString::Printf(TEXT("  \"lastPackPath\": \"%s\"\n"), *JsonEscape(LastPackPath))) ||
		!WriteJson(TEXT("}\n")))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to write Material Vault scan output: %s"), *OutputPath);
		return 2;
	}
	OutputWriter->Close();

	UE_LOG(LogTemp, Display, TEXT("Material Vault scan completed. Total materials: %d, Packs: %d, Output: %s"), TotalMaterialCount, TotalGeneratedPackCount, *OutputPath);
	return 0;
}
FString UMaterialVaultScanCommandlet::JsonEscape(const FString& Value)
{
	FString Result;
	Result.Reserve(Value.Len());
	for (const TCHAR Character : Value)
	{
		switch (Character)
		{
		case TEXT('\\'):
			Result += TEXT("\\\\");
			break;
		case TEXT('"'):
			Result += TEXT("\\\"");
			break;
		case TEXT('\n'):
			Result += TEXT("\\n");
			break;
		case TEXT('\r'):
			Result += TEXT("\\r");
			break;
		case TEXT('\t'):
			Result += TEXT("\\t");
			break;
		default:
			Result.AppendChar(Character);
			break;
		}
	}
	return Result;
}
