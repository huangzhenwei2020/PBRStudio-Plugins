#include "Services/PBRTextureScanner.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// Allowed image extensions
static const TArray<FString> ImageExtensions = {
	TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg"), TEXT(".tif"), TEXT(".tiff"),
	TEXT(".exr"), TEXT(".hdr"), TEXT(".tga"), TEXT(".webp"), TEXT(".bmp")
};

const TArray<FString> FPBRTextureScanner::ChannelDisplayOrder = {
	TEXT("Preview"), TEXT("BaseColor"), TEXT("Roughness"), TEXT("Glossiness"),
	TEXT("Metallic"), TEXT("Normal"), TEXT("NormalDX"), TEXT("NormalGL"),
	TEXT("AO"), TEXT("Height"), TEXT("Displacement"), TEXT("Opacity"),
	TEXT("Emissive"), TEXT("Specular"), TEXT("ClearCoat"), TEXT("ClearCoatRoughness"),
	TEXT("Anisotropy"), TEXT("Thickness"), TEXT("ORM"), TEXT("ARM"), TEXT("Unknown")
};

const TMap<FString, TArray<FString>> FPBRTextureScanner::ChannelTokens = {
	{ TEXT("BaseColor"),    { TEXT("basecolor"), TEXT("base_color"), TEXT("base-colour"), TEXT("base_colour"), TEXT("albedo"), TEXT("diffuse"), TEXT("diff"), TEXT("diffusecolor"), TEXT("diffuse_color"), TEXT("color"), TEXT("colour"), TEXT("col"), TEXT("clr"), TEXT("base"), TEXT("basecol"), TEXT("bc"), TEXT("d") } },
	{ TEXT("Roughness"),    { TEXT("roughness"), TEXT("rough"), TEXT("roughmap"), TEXT("roughnessmap"), TEXT("rgh"), TEXT("r") } },
	{ TEXT("Glossiness"),   { TEXT("glossiness"), TEXT("gloss"), TEXT("glossy"), TEXT("glossmap"), TEXT("smoothness"), TEXT("smooth"), TEXT("g") } },
	{ TEXT("Metallic"),     { TEXT("metallic"), TEXT("metalness"), TEXT("metal"), TEXT("metallicmap"), TEXT("metalnessmap"), TEXT("met"), TEXT("mtl"), TEXT("m") } },
	{ TEXT("Normal"),       { TEXT("normal"), TEXT("normalmap"), TEXT("nrm"), TEXT("norm"), TEXT("nor"), TEXT("nrml"), TEXT("bump"), TEXT("bumpmap"), TEXT("b") } },
	{ TEXT("NormalDX"),     { TEXT("normaldx"), TEXT("dx"), TEXT("directx") } },
	{ TEXT("NormalGL"),     { TEXT("normalgl"), TEXT("gl"), TEXT("opengl"), TEXT("ogl") } },
	{ TEXT("AO"),           { TEXT("ao"), TEXT("ambientocclusion"), TEXT("ambient_occlusion"), TEXT("ambient-occlusion"), TEXT("occlusion"), TEXT("occ"), TEXT("cavity") } },
	{ TEXT("Height"),       { TEXT("height"), TEXT("heightmap"), TEXT("height_map"), TEXT("depth"), TEXT("depthmap"), TEXT("depth_map"), TEXT("bumpheight") } },
	{ TEXT("Displacement"), { TEXT("displacement"), TEXT("displace"), TEXT("disp"), TEXT("displ"), TEXT("dsp"), TEXT("displacementmap"), TEXT("displacement_map") } },
	{ TEXT("Opacity"),      { TEXT("opacity"), TEXT("alpha"), TEXT("transparent"), TEXT("transparency"), TEXT("cutout"), TEXT("mask"), TEXT("opacitymap"), TEXT("alpha_map"), TEXT("alphamap") } },
	{ TEXT("Emissive"),     { TEXT("emissive"), TEXT("emission"), TEXT("emit"), TEXT("glow"), TEXT("selfillum"), TEXT("self_illum"), TEXT("selfillumination"), TEXT("lightmap") } },
	{ TEXT("Specular"),     { TEXT("specular"), TEXT("spec"), TEXT("specularity"), TEXT("reflection"), TEXT("refl") } },
	{ TEXT("ClearCoat"),    { TEXT("clearcoat"), TEXT("clear_coat"), TEXT("coat"), TEXT("coating") } },
	{ TEXT("ClearCoatRoughness"), { TEXT("clearcoatroughness"), TEXT("clear_coat_roughness"), TEXT("coatroughness"), TEXT("coat_roughness") } },
	{ TEXT("Anisotropy"),   { TEXT("anisotropy"), TEXT("anisotropic"), TEXT("aniso") } },
	{ TEXT("Thickness"),    { TEXT("thickness"), TEXT("transmission"), TEXT("translucency") } },
	{ TEXT("ORM"),          { TEXT("orm"), TEXT("rma"), TEXT("mro"), TEXT("occlusionroughnessmetallic"), TEXT("occlusion_roughness_metallic") } },
	{ TEXT("ARM"),          { TEXT("arm"), TEXT("ambientroughnessmetallic"), TEXT("ambient_roughness_metallic") } },
};

static const TSet<FString> ResolutionTokens = {
	TEXT("512"), TEXT("1k"), TEXT("2k"), TEXT("3k"), TEXT("4k"), TEXT("6k"), TEXT("8k"), TEXT("12k"), TEXT("16k"),
	TEXT("1024"), TEXT("2048"), TEXT("4096"), TEXT("8192"), TEXT("16384"), TEXT("udim"),
	TEXT("1001"), TEXT("1002"), TEXT("1003"), TEXT("1004")
};

static const TSet<FString> PreviewKeys = {
	TEXT("preview"), TEXT("thumb"), TEXT("thumbnail"), TEXT("render"),
	TEXT("sphere"), TEXT("ball"), TEXT("materialpreview"), TEXT("matpreview"), TEXT("sample"),
	TEXT("cover"), TEXT("demo"), TEXT("beauty"), TEXT("icon"), TEXT("catalog"), TEXT("swatch")
};

bool FPBRTextureScanner::IsResolutionToken(const FString& Token)
{
	return ResolutionTokens.Contains(Token.ToLower());
}

void FPBRTextureScanner::SplitTextureNameTokens(const FString& Path, FString& OutBase, FString& OutCompact, TArray<FString>& OutTokens)
{
	OutBase = FPaths::GetBaseFilename(Path);

	// Insert underscores before uppercase letters (CamelCase split)
	FString WithUnderscores;
	for (int32 i = 0; i < OutBase.Len(); ++i)
	{
		TCHAR c = OutBase[i];
		if (i > 0 && FChar::IsUpper(c) && FChar::IsLower(OutBase[i - 1]))
		{
			WithUnderscores.AppendChar('_');
		}
		WithUnderscores.AppendChar(FChar::ToLower(c));
	}

	FString Lower = WithUnderscores.ToLower();

	// Also normalize hyphens to underscores for tokenization
	Lower.ReplaceInline(TEXT("-"), TEXT("_"));

	// Tokenize on underscore
	TArray<FString> Parts;
	Lower.ParseIntoArray(Parts, TEXT("_"), true);

	for (const FString& Part : Parts)
	{
		// Further split on transitions and remove empty
		FString Clean;
		for (int32 i = 0; i < Part.Len(); ++i)
		{
			if (FChar::IsAlnum(Part[i]))
			{
				Clean.AppendChar(Part[i]);
			}
		}
		if (!Clean.IsEmpty())
		{
			OutTokens.Add(Clean);
		}
	}

	// Compact version: all alphanumeric chars concatenated
	OutCompact.Empty();
	for (const TCHAR c : Lower)
	{
		if (FChar::IsAlnum(c))
		{
			OutCompact.AppendChar(c);
		}
	}
}

FString FPBRTextureScanner::DetectPBRChannelFromFilename(const FString& Path)
{
	FString Base, Compact;
	TArray<FString> Tokens;
	SplitTextureNameTokens(Path, Base, Compact, Tokens);

	TSet<FString> TokenSet(Tokens);

	// Check packed ORM first to avoid false matching individual channels
	if (Compact.Contains(TEXT("occlusionroughnessmetallic")) || Compact.Contains(TEXT("ambientroughnessmetallic")))
	{
		return Compact.Contains(TEXT("ambientroughnessmetallic")) ? TEXT("ARM") : TEXT("ORM");
	}
	if (TokenSet.Contains(TEXT("arm")))
	{
		return TEXT("ARM");
	}
	if (TokenSet.Contains(TEXT("orm")) || TokenSet.Contains(TEXT("rma")))
	{
		return TEXT("ORM");
	}

	// Combined words take priority
	if (Compact.Contains(TEXT("basecolor")) || Compact.Contains(TEXT("basecolour"))) return TEXT("BaseColor");
	if (Compact.Contains(TEXT("diffusecolor"))) return TEXT("BaseColor");
	if (Compact.Contains(TEXT("ambientocclusion"))) return TEXT("AO");
	if (Compact.Contains(TEXT("normaldx")) || Compact.Contains(TEXT("directx"))) return TEXT("NormalDX");
	if (Compact.Contains(TEXT("normalgl")) || Compact.Contains(TEXT("opengl"))) return TEXT("NormalGL");
	if (Compact.Contains(TEXT("normalmap"))) return TEXT("Normal");
	if (Compact.Contains(TEXT("metalness"))) return TEXT("Metallic");
	if (Compact.Contains(TEXT("roughness"))) return TEXT("Roughness");
	if (Compact.Contains(TEXT("glossiness"))) return TEXT("Glossiness");
	if (Compact.Contains(TEXT("smoothness"))) return TEXT("Glossiness");
	if (Compact.Contains(TEXT("displacement"))) return TEXT("Displacement");
	if (Compact.Contains(TEXT("heightmap")) || Compact.Contains(TEXT("depthmap"))) return TEXT("Height");
	if (Compact.Contains(TEXT("clearcoatroughness"))) return TEXT("ClearCoatRoughness");
	if (Compact.Contains(TEXT("clearcoat"))) return TEXT("ClearCoat");
	if (Compact.Contains(TEXT("anisotropy")) || Compact.Contains(TEXT("anisotropic"))) return TEXT("Anisotropy");

	// Token rules
	TMap<FString, TArray<FString>> StrongRules = {
		{ TEXT("BaseColor"),    { TEXT("albedo"), TEXT("diffuse"), TEXT("diff"), TEXT("colour"), TEXT("color"), TEXT("col"), TEXT("clr"), TEXT("base"), TEXT("basecol"), TEXT("bc") } },
		{ TEXT("Normal"),       { TEXT("normal"), TEXT("nrm"), TEXT("norm"), TEXT("nor"), TEXT("nrml"), TEXT("bump") } },
		{ TEXT("Roughness"),    { TEXT("rough"), TEXT("rgh") } },
		{ TEXT("Glossiness"),   { TEXT("gloss"), TEXT("glossy"), TEXT("smooth"), TEXT("smoothness") } },
		{ TEXT("Metallic"),     { TEXT("metallic"), TEXT("metal"), TEXT("metalness"), TEXT("met"), TEXT("mtl") } },
		{ TEXT("AO"),           { TEXT("ao"), TEXT("occlusion"), TEXT("occ"), TEXT("cavity") } },
		{ TEXT("Height"),       { TEXT("height"), TEXT("depth") } },
		{ TEXT("Displacement"), { TEXT("displacement"), TEXT("displace"), TEXT("disp"), TEXT("displ"), TEXT("dsp") } },
		{ TEXT("Opacity"),      { TEXT("opacity"), TEXT("alpha"), TEXT("transparent"), TEXT("transparency"), TEXT("cutout"), TEXT("mask") } },
		{ TEXT("Emissive"),     { TEXT("emissive"), TEXT("emission"), TEXT("emit"), TEXT("glow"), TEXT("selfillum"), TEXT("selfillumination") } },
		{ TEXT("Specular"),     { TEXT("specular"), TEXT("spec"), TEXT("specularity"), TEXT("reflection"), TEXT("refl") } },
		{ TEXT("ClearCoat"),    { TEXT("clearcoat"), TEXT("coat"), TEXT("coating") } },
		{ TEXT("ClearCoatRoughness"), { TEXT("clearcoatroughness"), TEXT("coatroughness") } },
		{ TEXT("Anisotropy"),   { TEXT("anisotropy"), TEXT("anisotropic"), TEXT("aniso") } },
		{ TEXT("Thickness"),    { TEXT("thickness"), TEXT("transmission"), TEXT("translucency") } },
	};

	for (const auto& Rule : StrongRules)
	{
		for (const FString& Key : Rule.Value)
		{
			if (TokenSet.Contains(Key))
			{
				return Rule.Key;
			}
		}
	}

	// Single-letter suffixes: only the last meaningful token
	TArray<FString> Meaningful;
	for (const FString& T : Tokens)
	{
		if (!IsResolutionToken(T) && T != TEXT("map") && T != TEXT("tex") && T != TEXT("texture"))
		{
			Meaningful.Add(T);
		}
	}
	if (Meaningful.Num() > 0)
	{
		FString Last = Meaningful.Last();
		if (Last == TEXT("r")) return TEXT("Roughness");
		if (Last == TEXT("m")) return TEXT("Metallic");
		if (Last == TEXT("g")) return TEXT("Glossiness");
		if (Last == TEXT("b")) return TEXT("Normal");
		if (Last == TEXT("n")) return TEXT("Normal");
		if (Last == TEXT("d")) return TEXT("BaseColor");
	}

	return TEXT("Unknown");
}

bool FPBRTextureScanner::IsProbablePBRPreviewImage(const FString& Path)
{
	FString Ext = FPaths::GetExtension(Path, true).ToLower();
	if (Ext != TEXT(".png") && Ext != TEXT(".jpg") && Ext != TEXT(".jpeg"))
	{
		return false;
	}

	FString Base, Compact;
	TArray<FString> Tokens;
	SplitTextureNameTokens(Path, Base, Compact, Tokens);
	TSet<FString> TokenSet(Tokens);

	for (const FString& Key : PreviewKeys)
	{
		if (Compact.Contains(Key) || TokenSet.Contains(Key))
		{
			return true;
		}
	}

	// Has channel tokens → let channel detection handle it
	if (DetectPBRChannelFromFilename(Path) != TEXT("Unknown"))
	{
		return false;
	}

	// No channel tokens — treat as preview
	return true;
}

FString FPBRTextureScanner::ChooseBetterMap(const FString& Existing, const FString& Candidate)
{
	// Prefer higher-resolution (larger file size as proxy)
	int64 ExistingSize = IFileManager::Get().FileSize(*Existing);
	int64 CandidateSize = IFileManager::Get().FileSize(*Candidate);
	return (CandidateSize > ExistingSize) ? Candidate : Existing;
}

TArray<FPBRMaterialSet> FPBRTextureScanner::ScanPBRTextureSets(const FScanSettings& Settings)
{
	TArray<FPBRMaterialSet> Result;
	if (Settings.RootDir.IsEmpty() || !FPaths::DirectoryExists(Settings.RootDir))
	{
		return Result;
	}

	// Build a mapping from key to material set
	TMap<FString, FPBRMaterialSet> Sets;

	auto ProcessFile = [&](const FString& Folder, const FString& FileName)
	{
		FString Ext = FPaths::GetExtension(FileName, true).ToLower();
		if (!ImageExtensions.Contains(Ext)) return;

		FString FullPath = FPaths::Combine(Folder, FileName);
		FString Channel = IsProbablePBRPreviewImage(FullPath)
			? TEXT("Preview")
			: DetectPBRChannelFromFilename(FullPath);

		FString FolderName = FPaths::GetCleanFilename(Folder);
		FString Key;
		FString SetName;

		if (Settings.bGroupByFolder)
		{
			Key = Folder + TEXT("|") + FolderName;
			SetName = FolderName;
		}
		else
		{
			// Derive key from cleaned filename (strip channel tokens, resolution tokens)
			FString Base = FPaths::GetBaseFilename(FullPath);
			FString CleanName = Base;
			// Simple: use base name without channel suffixes
			for (const auto& Pair : ChannelTokens)
			{
				for (const FString& Tk : Pair.Value)
				{
					CleanName = CleanName.Replace(*Tk, TEXT(""));
				}
			}
			CleanName = CleanName.TrimChar('_').TrimChar('-').TrimChar(' ');
			if (CleanName.IsEmpty()) CleanName = Base;
			Key = Folder + TEXT("|") + CleanName;
			SetName = CleanName;
		}

		if (!Sets.Contains(Key))
		{
			FPBRMaterialSet NewSet;
			NewSet.Name = SetName;
			NewSet.Folder = Folder;
			NewSet.Status = TEXT("等待");
			Sets.Add(Key, NewSet);
		}

		FPBRMaterialSet& Set = Sets[Key];

		if (Channel == TEXT("Preview"))
		{
			if (Set.PreviewPath.IsEmpty())
			{
				Set.PreviewPath = FullPath;
			}
			else
			{
				Set.Duplicates.Add(FullPath);
			}
		}
		else if (Channel == TEXT("Unknown"))
		{
			Set.Unknown.Add(FullPath);
		}
		else
		{
			if (Set.Channels.Contains(Channel))
			{
				FString Chosen = ChooseBetterMap(Set.Channels[Channel], FullPath);
				if (Chosen != Set.Channels[Channel])
				{
					Set.Duplicates.Add(Set.Channels[Channel]);
					Set.Channels[Channel] = Chosen;
				}
				else
				{
					Set.Duplicates.Add(FullPath);
				}
			}
			else
			{
				Set.Channels.Add(Channel, FullPath);
			}
		}
	};

	if (Settings.bRecursive)
	{
		TArray<FString> Dirs;
		Dirs.Add(Settings.RootDir);
		IFileManager::Get().FindFilesRecursive(Dirs, *Settings.RootDir, TEXT("*"), true, false);
		// Walk each unique directory
		TSet<FString> VisitedDirs;
		VisitedDirs.Add(Settings.RootDir);
		for (const FString& Full : Dirs)
		{
			FString Dir = FPaths::GetPath(Full);
			if (!VisitedDirs.Contains(Dir))
			{
				VisitedDirs.Add(Dir);
			}
		}
		for (const FString& Dir : VisitedDirs)
		{
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*")), true, false);
			for (const FString& F : Files)
			{
				ProcessFile(Dir, F);
			}
		}
	}
	else
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Settings.RootDir / TEXT("*")), true, false);
		for (const FString& F : Files)
		{
			ProcessFile(Settings.RootDir, F);
		}
	}

	Sets.GenerateValueArray(Result);
	for (FPBRMaterialSet& Set : Result)
	{
		if (Set.PreviewPath.IsEmpty())
		{
			if (const FString* BaseColorPath = Set.Channels.Find(TEXT("BaseColor")))
			{
				Set.PreviewPath = *BaseColorPath;
			}
		}
	}
	Result.Sort([](const FPBRMaterialSet& A, const FPBRMaterialSet& B)
	{
		int32 FolderCmp = A.Folder.Compare(B.Folder, ESearchCase::IgnoreCase);
		return FolderCmp != 0 ? FolderCmp < 0 : A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0;
	});

	return Result;
}
