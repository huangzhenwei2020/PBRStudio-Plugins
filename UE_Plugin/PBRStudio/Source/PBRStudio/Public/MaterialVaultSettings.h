#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/NoExportTypes.h"
#include "MaterialVaultSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="Material Vault"))
class PBRSTUDIO_API UMaterialVaultSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMaterialVaultSettings();

	UPROPERTY(Config, EditAnywhere, Category="Library Paths")
	FString LibraryMountRoot;

	UPROPERTY(Config, EditAnywhere, Category="Library Paths")
	FString StagingMountRoot;

	UPROPERTY(Config, EditAnywhere, Category="Library Paths")
	FDirectoryPath ExternalVaultRoot;

	UPROPERTY(Config, EditAnywhere, Category="Library Paths")
	FString ActiveLibraryName;

	UPROPERTY(Config, EditAnywhere, Category="Staging", meta=(ClampMin="512", UIMin="512", DisplayName="Staging Max Size GB"))
	int32 StagingMaxSizeMB;

	UPROPERTY(Config, EditAnywhere, Category="Staging", meta=(ClampMin="0", UIMin="0", DisplayName="Staging Keep Days"))
	int32 StagingKeepDays;

	UPROPERTY(Config, EditAnywhere, Category="Staging", meta=(DisplayName="Clean Staging After Pack"))
	bool bCleanStagingAfterPack;

	UPROPERTY(Config, EditAnywhere, Category="Staging", meta=(DisplayName="Auto Clean Staging When Full"))
	bool bAutoCleanStagingWhenFull;

	UPROPERTY(Config, EditAnywhere, Category="Generation", meta=(DisplayName="Skip Existing Packs"))
	bool bSkipExistingPacks;

	UPROPERTY(Config, EditAnywhere, Category="Naming")
	FString DefaultCategory;

	UPROPERTY(Config, EditAnywhere, Category="Categories")
	TArray<FString> CategoryOrder;

	UPROPERTY(Config, EditAnywhere, Category="Compatibility")
	bool bIncludeEngineContentDependencies;

	virtual FName GetCategoryName() const override;
};
