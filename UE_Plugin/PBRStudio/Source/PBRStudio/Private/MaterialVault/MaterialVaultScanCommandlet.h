#pragma once

#include "Commandlets/Commandlet.h"
#include "MaterialVaultScanCommandlet.generated.h"

UCLASS()
class PBRSTUDIO_API UMaterialVaultScanCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMaterialVaultScanCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	static FString JsonEscape(const FString& Value);
};
