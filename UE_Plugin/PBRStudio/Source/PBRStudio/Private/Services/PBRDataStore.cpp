#include "Services/PBRDataStore.h"
#include "Models/PBRDownloadSite.h"
#include "Models/PBRMaterialSet.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

FString FPBRDataStore::GetDataDirectory()
{
	FString Dir = FPaths::ProjectSavedDir() / TEXT("PBRStudio");
	IFileManager::Get().MakeDirectory(*Dir, true);
	return Dir;
}

FString FPBRDataStore::GetFilePath(const FString& FileName)
{
	return GetDataDirectory() / FileName;
}

bool FPBRDataStore::ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *Path))
	{
		return false;
	}
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	return FJsonSerializer::Deserialize(Reader, OutObject);
}

bool FPBRDataStore::WriteJsonFile(const FString& Path, const TSharedPtr<FJsonObject>& Object)
{
	FString Content;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Content);
	if (!FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(Content, *Path);
}

// -- Download Sites -----------------------------------------------------------

static TArray<FPBRDownloadSite> DefaultDownloadSites()
{
	return {
		{ TEXT("ambientCG"), TEXT("CC0"), TEXT("https://ambientcg.com/"), TEXT("High-quality PBR textures, CC0 license") },
		{ TEXT("Poly Haven Textures"), TEXT("CC0"), TEXT("https://polyhaven.com/textures"), TEXT("Free PBR textures, all CC0") },
		{ TEXT("Poly Haven HDRIs"), TEXT("CC0"), TEXT("https://polyhaven.com/hdris"), TEXT("Free HDR environment maps") },
		{ TEXT("Poly Haven Models"), TEXT("CC0"), TEXT("https://polyhaven.com/models"), TEXT("Free 3D models") },
		{ TEXT("ShareTextures"), TEXT("CC0"), TEXT("https://www.sharetextures.com/"), TEXT("Free PBR textures") },
		{ TEXT("3DTextures.me"), TEXT("Free"), TEXT("https://3dtextures.me/"), TEXT("Free PBR textures") },
		{ TEXT("CC0Textures"), TEXT("CC0"), TEXT("https://cc0-textures.com/"), TEXT("CC0 PBR textures") },
		{ TEXT("CGBookcase"), TEXT("Free"), TEXT("https://www.cgbookcase.com/"), TEXT("Free PBR textures; check each asset license") },
		{ TEXT("TextureCan"), TEXT("Free"), TEXT("https://www.texturecan.com/"), TEXT("Free PBR textures and models") },
		{ TEXT("FreePBR"), TEXT("Free"), TEXT("https://freepbr.com/"), TEXT("Free PBR material sets") },
		{ TEXT("Texture Box"), TEXT("Free/Paid"), TEXT("https://texturebox.com/"), TEXT("PBR textures; check license before use") },
		{ TEXT("LotPixel"), TEXT("Free/Paid"), TEXT("https://www.lotpixel.com/"), TEXT("PBR textures and assets") },
		{ TEXT("Poliigon Free"), TEXT("Free/Paid"), TEXT("https://www.poliigon.com/search/free"), TEXT("Free section from Poliigon") },
		{ TEXT("Quixel Megascans"), TEXT("Account"), TEXT("https://quixel.com/megascans/"), TEXT("Megascans assets; use according to Epic/Quixel terms") },
		{ TEXT("BlenderKit Materials"), TEXT("Free/Paid"), TEXT("https://www.blenderkit.com/asset-gallery?query=category_subtree:material"), TEXT("Material library; check individual license") },
		{ TEXT("Matlib"), TEXT("Free"), TEXT("https://matlib.gpuopen.com/"), TEXT("AMD material library") },
	};
}

bool FPBRDataStore::LoadDownloadSites(TArray<FPBRDownloadSite>& OutSites)
{
	FString Path = GetFilePath(TEXT("PBRDownloadSites.json"));
	TSharedPtr<FJsonObject> Root;
	if (!ReadJsonFile(Path, Root))
	{
		OutSites = DefaultDownloadSites();
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* SitesArray;
	if (!Root->TryGetArrayField(TEXT("sites"), SitesArray))
	{
		OutSites = DefaultDownloadSites();
		return false;
	}

	OutSites.Empty();
	for (const auto& Val : *SitesArray)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val->TryGetObject(Obj)) continue;

		FPBRDownloadSite Site;
		(*Obj)->TryGetStringField(TEXT("name"), Site.Name);
		(*Obj)->TryGetStringField(TEXT("license"), Site.License);
		(*Obj)->TryGetStringField(TEXT("url"), Site.URL);
		(*Obj)->TryGetStringField(TEXT("note"), Site.Note);
		OutSites.Add(Site);
	}

	if (OutSites.Num() == 0) { OutSites = DefaultDownloadSites(); }
	TArray<FPBRDownloadSite> Defaults = DefaultDownloadSites();
	bool bAddedMissingDefault = false;
	for (const FPBRDownloadSite& DefaultSite : Defaults)
	{
		bool bExists = false;
		for (const FPBRDownloadSite& ExistingSite : OutSites)
		{
			if (ExistingSite.URL.Equals(DefaultSite.URL, ESearchCase::IgnoreCase))
			{
				bExists = true;
				break;
			}
		}
		if (!bExists)
		{
			OutSites.Add(DefaultSite);
			bAddedMissingDefault = true;
		}
	}
	if (bAddedMissingDefault)
	{
		SaveDownloadSites(OutSites);
	}
	return true;
}

bool FPBRDataStore::SaveDownloadSites(const TArray<FPBRDownloadSite>& Sites)
{
	TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const auto& S : Sites)
	{
		TSharedRef<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("name"), S.Name);
		Obj->SetStringField(TEXT("license"), S.License);
		Obj->SetStringField(TEXT("url"), S.URL);
		Obj->SetStringField(TEXT("note"), S.Note);
		Arr.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	Root->SetArrayField(TEXT("sites"), Arr);
	return WriteJsonFile(GetFilePath(TEXT("PBRDownloadSites.json")), Root);
}

// -- Config -------------------------------------------------------------------

bool FPBRDataStore::LoadConfig(TSharedPtr<FJsonObject>& OutConfig)
{
	return ReadJsonFile(GetFilePath(TEXT("PBRStudioConfig.json")), OutConfig);
}

bool FPBRDataStore::SaveConfig(const TSharedPtr<FJsonObject>& Config)
{
	return WriteJsonFile(GetFilePath(TEXT("PBRStudioConfig.json")), Config);
}

// -- Slot Learning ------------------------------------------------------------

bool FPBRDataStore::LoadSlotLearning(TMap<FString, FString>& OutSlots)
{
	FString Path = GetFilePath(TEXT("PBRSlotLearning.json"));
	TSharedPtr<FJsonObject> Root;
	if (!ReadJsonFile(Path, Root))
	{
		return false;
	}
	OutSlots.Empty();
	for (const auto& Pair : Root->Values)
	{
		OutSlots.Add(Pair.Key, Pair.Value->AsString());
	}
	return true;
}

bool FPBRDataStore::SaveSlotLearning(const TMap<FString, FString>& Slots)
{
	TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject);
	for (const auto& Pair : Slots)
	{
		Root->SetStringField(Pair.Key, Pair.Value);
	}
	return WriteJsonFile(GetFilePath(TEXT("PBRSlotLearning.json")), Root);
}

// -- Material Sets ------------------------------------------------------------

bool FPBRDataStore::LoadMaterialSets(const FString& FileName, TArray<FPBRMaterialSet>& OutSets)
{
	FString Path = GetFilePath(FileName);
	TSharedPtr<FJsonObject> Root;
	if (!ReadJsonFile(Path, Root))
	{
		return false;
	}
	OutSets.Empty();
	const TArray<TSharedPtr<FJsonValue>>* SetsArray;
	if (!Root->TryGetArrayField(TEXT("material_sets"), SetsArray))
	{
		return false;
	}
	for (const auto& Val : *SetsArray)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val->TryGetObject(Obj)) continue;

		FPBRMaterialSet Set;
		(*Obj)->TryGetStringField(TEXT("name"), Set.Name);
		(*Obj)->TryGetStringField(TEXT("folder"), Set.Folder);
		(*Obj)->TryGetStringField(TEXT("preview_path"), Set.PreviewPath);
		(*Obj)->TryGetStringField(TEXT("status"), Set.Status);
		(*Obj)->TryGetStringField(TEXT("slot_overrides_key"), Set.SlotOverridesKey);
		(*Obj)->TryGetStringField(TEXT("created_signature"), Set.CreatedSignature);
		FString CreatedMaterialPath;
		if ((*Obj)->TryGetStringField(TEXT("created_material"), CreatedMaterialPath) && !CreatedMaterialPath.IsEmpty())
		{
			Set.CreatedMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(CreatedMaterialPath));
		}
		bool bChecked = true;
		if ((*Obj)->TryGetBoolField(TEXT("checked"), bChecked))
		{
			Set.bChecked = bChecked;
		}

		const TSharedPtr<FJsonObject>* ChObj;
		if ((*Obj)->TryGetObjectField(TEXT("channels"), ChObj))
		{
			for (const auto& Ch : (*ChObj)->Values)
			{
				Set.Channels.Add(Ch.Key, Ch.Value->AsString());
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* DuplicateArray;
		if ((*Obj)->TryGetArrayField(TEXT("duplicates"), DuplicateArray))
		{
			for (const TSharedPtr<FJsonValue>& Value : *DuplicateArray)
			{
				Set.Duplicates.Add(Value->AsString());
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* UnknownArray;
		if ((*Obj)->TryGetArrayField(TEXT("unknown"), UnknownArray))
		{
			for (const TSharedPtr<FJsonValue>& Value : *UnknownArray)
			{
				Set.Unknown.Add(Value->AsString());
			}
		}
		OutSets.Add(Set);
	}
	return true;
}

bool FPBRDataStore::SaveMaterialSets(const FString& FileName, const TArray<FPBRMaterialSet>& Sets)
{
	TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const auto& S : Sets)
	{
		TSharedRef<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("name"), S.Name);
		Obj->SetStringField(TEXT("folder"), S.Folder);
		Obj->SetStringField(TEXT("preview_path"), S.PreviewPath);
		Obj->SetStringField(TEXT("status"), S.Status);
		Obj->SetStringField(TEXT("slot_overrides_key"), S.SlotOverridesKey);
		Obj->SetStringField(TEXT("created_signature"), S.CreatedSignature);
		Obj->SetStringField(TEXT("created_material"), S.CreatedMaterial.ToSoftObjectPath().ToString());
		Obj->SetBoolField(TEXT("checked"), S.bChecked);

		TSharedRef<FJsonObject> ChObj = MakeShareable(new FJsonObject);
		for (const auto& Ch : S.Channels)
		{
			ChObj->SetStringField(Ch.Key, Ch.Value);
		}
		Obj->SetObjectField(TEXT("channels"), ChObj);
		TArray<TSharedPtr<FJsonValue>> DuplicateArray;
		for (const FString& Value : S.Duplicates)
		{
			DuplicateArray.Add(MakeShareable(new FJsonValueString(Value)));
		}
		Obj->SetArrayField(TEXT("duplicates"), DuplicateArray);
		TArray<TSharedPtr<FJsonValue>> UnknownArray;
		for (const FString& Value : S.Unknown)
		{
			UnknownArray.Add(MakeShareable(new FJsonValueString(Value)));
		}
		Obj->SetArrayField(TEXT("unknown"), UnknownArray);
		Arr.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	Root->SetArrayField(TEXT("material_sets"), Arr);
	return WriteJsonFile(GetFilePath(FileName), Root);
}
