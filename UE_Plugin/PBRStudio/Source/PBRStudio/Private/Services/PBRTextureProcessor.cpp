#include "Services/PBRTextureProcessor.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

bool FPBRTextureProcessor::IsPowerOfTwo(int32 N)
{
	return N > 0 && (N & (N - 1)) == 0;
}

void FPBRTextureProcessor::ComputePowerOf2TargetSize(int32 SrcW, int32 SrcH, int32 MaxSize,
	int32& OutW, int32& OutH)
{
	// Find largest power-of-2 <= MaxSize
	int32 MaxP2 = 1;
	while (MaxP2 * 2 <= MaxSize) MaxP2 *= 2;

	float Aspect = (float)SrcW / (float)FMath::Max(SrcH, 1);

	if (SrcW >= SrcH)
	{
		OutW = MaxP2;
		OutH = FMath::Clamp(FMath::RoundToInt(MaxP2 / Aspect), 1, MaxP2);
	}
	else
	{
		OutH = MaxP2;
		OutW = FMath::Clamp(FMath::RoundToInt(MaxP2 * Aspect), 1, MaxP2);
	}

	// Ensure output dimensions are power-of-2
	while (!IsPowerOfTwo(OutW)) OutW = FMath::Max(1, OutW - 1);
	while (!IsPowerOfTwo(OutH)) OutH = FMath::Max(1, OutH - 1);

	// Don't upscale
	OutW = FMath::Min(OutW, SrcW);
	OutH = FMath::Min(OutH, SrcH);

	// Ensure minimum size
	OutW = FMath::Clamp(OutW, 1, MaxSize);
	OutH = FMath::Clamp(OutH, 1, MaxSize);
}

bool FPBRTextureProcessor::IsTextureUECompliant(const FString& Path, int32 MaxSize,
	bool bRequirePowerOf2, TArray<FString>& OutIssues, int32& OutWidth, int32& OutHeight)
{
	OutIssues.Empty();
	OutWidth = 0;
	OutHeight = 0;

	if (!FPaths::FileExists(Path))
	{
		OutIssues.Add(TEXT("文件不存在"));
		return false;
	}

	TArray<uint8> Pixels;
	if (!LoadImage(Path, Pixels, OutWidth, OutHeight))
	{
		OutIssues.Add(TEXT("无法读取图片尺寸"));
		return false;
	}

	bool bOK = true;

	if (OutWidth > MaxSize)
	{
		OutIssues.Add(FString::Printf(TEXT("宽度 %d > 最大 %d"), OutWidth, MaxSize));
		bOK = false;
	}
	if (OutHeight > MaxSize)
	{
		OutIssues.Add(FString::Printf(TEXT("高度 %d > 最大 %d"), OutHeight, MaxSize));
		bOK = false;
	}

	if (bRequirePowerOf2)
	{
		if (!IsPowerOfTwo(OutWidth))
		{
			OutIssues.Add(FString::Printf(TEXT("宽度 %d 不是二次幂"), OutWidth));
			bOK = false;
		}
		if (!IsPowerOfTwo(OutHeight))
		{
			OutIssues.Add(FString::Printf(TEXT("高度 %d 不是二次幂"), OutHeight));
			bOK = false;
		}
	}

	return bOK;
}

bool FPBRTextureProcessor::CopyTextureOnly(const FString& SrcPath, const FString& DstPath, bool bNoOverwrite)
{
	if (bNoOverwrite && FPaths::FileExists(DstPath))
	{
		return true;
	}

	// Ensure output directory
	FString Dir = FPaths::GetPath(DstPath);
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir);

	return FPlatformFileManager::Get().GetPlatformFile().CopyFile(*DstPath, *SrcPath);
}

bool FPBRTextureProcessor::LoadImage(const FString& Path, TArray<uint8>& OutPixels,
	int32& OutW, int32& OutH)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Path))
	{
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	EImageFormat Format = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);

	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		return false;
	}

	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutPixels))
	{
		return false;
	}

	OutW = Wrapper->GetWidth();
	OutH = Wrapper->GetHeight();
	return OutPixels.Num() > 0;
}

bool FPBRTextureProcessor::SaveImage(const FString& Path, const TArray<uint8>& Pixels,
	int32 W, int32 H)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	EImageFormat Format = EImageFormat::PNG;
	FString Ext = FPaths::GetExtension(Path, true).ToLower();
	if (Ext == TEXT(".jpg") || Ext == TEXT(".jpeg")) Format = EImageFormat::JPEG;
	else if (Ext == TEXT(".bmp")) Format = EImageFormat::BMP;
	else if (Ext == TEXT(".tga")) Format = EImageFormat::TGA;

	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);
	if (!Wrapper.IsValid()) return false;

	if (!Wrapper->SetRaw(Pixels.GetData(), Pixels.Num(), W, H, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	TArray64<uint8> Compressed = Wrapper->GetCompressed();
	return FFileHelper::SaveArrayToFile(
		TArrayView<const uint8>(Compressed.GetData(), Compressed.Num()),
		*Path
	);
}

bool FPBRTextureProcessor::CenterCropAndResize(const TArray<uint8>& SrcPixels,
	int32 SrcW, int32 SrcH, TArray<uint8>& DstPixels, int32 DstW, int32 DstH)
{
	DstPixels.SetNumZeroed(DstW * DstH * 4); // 4 channels BGRA

	float ScaleX = (float)SrcW / DstW;
	float ScaleY = (float)SrcH / DstH;

	// Center crop: if aspect ratios differ, crop the source first
	int32 CropW = SrcW;
	int32 CropH = SrcH;
	int32 CropX = 0;
	int32 CropY = 0;

	float SrcAspect = (float)SrcW / FMath::Max(SrcH, 1);
	float DstAspect = (float)DstW / FMath::Max(DstH, 1);

	if (SrcAspect > DstAspect)
	{
		// Source wider → crop horizontally
		CropW = FMath::RoundToInt(SrcH * DstAspect);
		CropX = (SrcW - CropW) / 2;
	}
	else if (DstAspect > SrcAspect)
	{
		// Source taller → crop vertically
		CropH = FMath::RoundToInt(SrcW / DstAspect);
		CropY = (SrcH - CropH) / 2;
	}

	// Nearest-neighbor sampling (simplest; in production we'd use bilinear)
	for (int32 y = 0; y < DstH; ++y)
	{
		for (int32 x = 0; x < DstW; ++x)
		{
			int32 SrcX = CropX + FMath::RoundToInt((float)x / DstW * CropW);
			int32 SrcY = CropY + FMath::RoundToInt((float)y / DstH * CropH);
			SrcX = FMath::Clamp(SrcX, 0, SrcW - 1);
			SrcY = FMath::Clamp(SrcY, 0, SrcH - 1);

			int32 SrcIdx = (SrcY * SrcW + SrcX) * 4;
			int32 DstIdx = (y * DstW + x) * 4;

			DstPixels[DstIdx]     = SrcPixels[SrcIdx];     // B
			DstPixels[DstIdx + 1] = SrcPixels[SrcIdx + 1]; // G
			DstPixels[DstIdx + 2] = SrcPixels[SrcIdx + 2]; // R
			DstPixels[DstIdx + 3] = SrcPixels[SrcIdx + 3]; // A
		}
	}

	return true;
}

bool FPBRTextureProcessor::ForceProcessTextureForUE(const FString& SrcPath,
	const FProcessSettings& Settings, FString& OutOutputPath, FString& OutMessage)
{
	if (!FPaths::FileExists(SrcPath))
	{
		OutMessage = TEXT("源文件不存在");
		return false;
	}

	int32 SrcW, SrcH;
	TArray<uint8> SrcPixels;
	if (!LoadImage(SrcPath, SrcPixels, SrcW, SrcH))
	{
		OutMessage = TEXT("加载源图片失败");
		return false;
	}

	// Compute target dimensions
	int32 DstW, DstH;
	ComputePowerOf2TargetSize(SrcW, SrcH, Settings.MaxSize, DstW, DstH);

	// Generate output filename
	FString BaseName = FPaths::GetBaseFilename(SrcPath);
	FString Ext = FPaths::GetExtension(SrcPath, true);
	if (Settings.bUECompliantNaming)
	{
		// Ensure T_ prefix
		if (!BaseName.StartsWith(TEXT("T_")))
		{
			BaseName = TEXT("T_") + BaseName;
		}
		// Replace spaces/hyphens with underscores
		BaseName.ReplaceInline(TEXT(" "), TEXT("_"));
		BaseName.ReplaceInline(TEXT("-"), TEXT("_"));
	}
	FString OutputName = FString::Printf(TEXT("%s_%dx%d"), *BaseName, DstW, DstH) + TEXT(".png");
	OutOutputPath = FPaths::Combine(Settings.OutputDir, OutputName);

	if (Settings.bNoOverwrite && FPaths::FileExists(OutOutputPath))
	{
		OutMessage = TEXT("输出已存在，已跳过");
		return true;
	}

	// Process
	TArray<uint8> DstPixels;
	if (!CenterCropAndResize(SrcPixels, SrcW, SrcH, DstPixels, DstW, DstH))
	{
		OutMessage = TEXT("居中裁剪和缩放失败");
		return false;
	}

	// Ensure output directory
	FString OutDir = FPaths::GetPath(OutOutputPath);
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutDir);

	if (!SaveImage(OutOutputPath, DstPixels, DstW, DstH))
	{
		OutMessage = TEXT("保存输出图片失败");
		return false;
	}

	OutMessage = FString::Printf(TEXT("OK: %dx%d -> %dx%d"), SrcW, SrcH, DstW, DstH);
	return true;
}
