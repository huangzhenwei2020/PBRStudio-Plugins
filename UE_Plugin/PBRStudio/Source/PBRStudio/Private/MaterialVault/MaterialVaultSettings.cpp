#include "MaterialVaultSettings.h"

UMaterialVaultSettings::UMaterialVaultSettings()
	: LibraryMountRoot(TEXT("/Game/MaterialVault/Library"))
	, StagingMountRoot(TEXT("/Game/__MaterialVaultStaging"))
	, ActiveLibraryName(TEXT("\u9ed8\u8ba4\u6750\u8d28\u5e93"))
	, StagingMaxSizeMB(20480)
	, StagingKeepDays(3)
	, bCleanStagingAfterPack(true)
	, bAutoCleanStagingWhenFull(false)
	, bSkipExistingPacks(true)
	, DefaultCategory(TEXT("\u672a\u5206\u7c7b"))
	, bIncludeEngineContentDependencies(false)
{
	ExternalVaultRoot.Path = TEXT("H:/UE\u6750\u8d28");
}

FName UMaterialVaultSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
