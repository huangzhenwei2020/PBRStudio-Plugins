#pragma once

#include "CoreMinimal.h"

class PBRSTUDIO_API FPBRTextureProcessor
{
public:
	struct FProcessSettings
	{
		FString OutputDir;
		int32 MaxSize = 4096;
		bool bRequirePowerOf2 = true;
		bool bCenterCrop = true;
		bool bNoOverwrite = true;
		bool bUECompliantNaming = true;
	};

	// Check texture compliance
	static bool IsTextureUECompliant(const FString& Path, int32 MaxSize, bool bRequirePowerOf2,
		TArray<FString>& OutIssues, int32& OutWidth, int32& OutHeight);

	// Force process: center-crop + resize to power-of-2
	static bool ForceProcessTextureForUE(const FString& SrcPath, const FProcessSettings& Settings,
		FString& OutOutputPath, FString& OutMessage);

	// Copy-only mode
	static bool CopyTextureOnly(const FString& SrcPath, const FString& DstPath, bool bNoOverwrite = true);

	// Compute power-of-2 target preserving aspect ratio
	static void ComputePowerOf2TargetSize(int32 SrcW, int32 SrcH, int32 MaxSize,
		int32& OutW, int32& OutH);

	static bool IsPowerOfTwo(int32 N);

private:
	static bool LoadImage(const FString& Path, TArray<uint8>& OutPixels, int32& OutW, int32& OutH);
	static bool SaveImage(const FString& Path, const TArray<uint8>& Pixels, int32 W, int32 H);
	static bool CenterCropAndResize(const TArray<uint8>& SrcPixels, int32 SrcW, int32 SrcH,
		TArray<uint8>& DstPixels, int32 DstW, int32 DstH);
};
