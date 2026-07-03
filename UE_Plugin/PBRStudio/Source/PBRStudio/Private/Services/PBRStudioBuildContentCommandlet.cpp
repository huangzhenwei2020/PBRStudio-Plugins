#include "PBRStudioBuildContentCommandlet.h"

#include "Services/PBRMaterialFunctionLibrary.h"
#include "Services/PBRMaterialTemplateManager.h"

UPBRStudioBuildContentCommandlet::UPBRStudioBuildContentCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UPBRStudioBuildContentCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Display, TEXT("PBRStudio content build started. Params: %s"), *Params);

	TArray<FString> FunctionMessages;
	TArray<FString> TemplateMessages;
	const int32 FunctionCount = FPBRMaterialFunctionLibrary::EnsureAllMaterialFunctions(FunctionMessages);
	const int32 TemplateCount = FPBRMaterialTemplateManager::EnsureAllTemplateMaterials(TemplateMessages);

	UE_LOG(LogTemp, Display, TEXT("PBRStudio content build finished. Functions: %d, Templates: %d"), FunctionCount, TemplateCount);
	for (const FString& Message : FunctionMessages)
	{
		UE_LOG(LogTemp, Display, TEXT("%s"), *Message);
	}
	for (const FString& Message : TemplateMessages)
	{
		UE_LOG(LogTemp, Display, TEXT("%s"), *Message);
	}

	return (FunctionCount > 0 && TemplateCount > 0) ? 0 : 1;
}
