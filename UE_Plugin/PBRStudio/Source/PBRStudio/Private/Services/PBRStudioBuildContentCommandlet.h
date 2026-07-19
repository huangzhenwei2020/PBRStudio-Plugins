#pragma once

#include "Commandlets/Commandlet.h"
#include "PBRStudioBuildContentCommandlet.generated.h"

UCLASS()
class PBRSTUDIO_API UPBRStudioBuildContentCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UPBRStudioBuildContentCommandlet();

	virtual int32 Main(const FString& Params) override;
};
