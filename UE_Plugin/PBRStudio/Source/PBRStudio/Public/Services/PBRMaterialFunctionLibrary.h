#pragma once

#include "CoreMinimal.h"

class PBRSTUDIO_API FPBRMaterialFunctionLibrary
{
public:
	static int32 EnsureAllMaterialFunctions(TArray<FString>& OutMessages);
};
