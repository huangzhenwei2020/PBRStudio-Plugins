#include "Services/PBRWorkspaceTemplate.h"

#include "Services/PBRDataStore.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

namespace
{
const TCHAR* ConfigProjectNameKey = TEXT("workspace_template_project_name");
const TCHAR* ConfigFolderNameKey = TEXT("workspace_template_folder_name");
const TCHAR* ConfigSceneEnvironmentKey = TEXT("workspace_template_create_scene_environment");

FString CollapseUnderscores(FString Value)
{
	while (Value.Contains(TEXT("__")))
	{
		Value = Value.Replace(TEXT("__"), TEXT("_"));
	}
	Value.RemoveFromStart(TEXT("_"));
	Value.RemoveFromEnd(TEXT("_"));
	return Value;
}

FString MakeShortHash(const FString& Value)
{
	const FString Hash = FMD5::HashAnsiString(*Value);
	return Hash.Left(8).ToUpper();
}

bool MatchChineseTerm(const FString& Value, int32 StartIndex, FString& OutEnglish, int32& OutMatchedLength)
{
	static const TArray<TPair<FString, FString>> Terms = {
		{ TEXT("建筑可视化"), TEXT("ArchViz") },
		{ TEXT("样板间"), TEXT("Showroom") },
		{ TEXT("办公室"), TEXT("Office") },
		{ TEXT("售楼处"), TEXT("SalesCenter") },
		{ TEXT("接待厅"), TEXT("Reception") },
		{ TEXT("会议室"), TEXT("MeetingRoom") },
		{ TEXT("卫生间"), TEXT("Bathroom") },
		{ TEXT("衣帽间"), TEXT("Cloakroom") },
		{ TEXT("地下室"), TEXT("Basement") },
		{ TEXT("可视化"), TEXT("Visualization") },
		{ TEXT("建筑"), TEXT("Architecture") },
		{ TEXT("项目"), TEXT("Project") },
		{ TEXT("方案"), TEXT("Design") },
		{ TEXT("场景"), TEXT("Scene") },
		{ TEXT("室内"), TEXT("Interior") },
		{ TEXT("室外"), TEXT("Exterior") },
		{ TEXT("住宅"), TEXT("Residential") },
		{ TEXT("别墅"), TEXT("Villa") },
		{ TEXT("公寓"), TEXT("Apartment") },
		{ TEXT("酒店"), TEXT("Hotel") },
		{ TEXT("商业"), TEXT("Commercial") },
		{ TEXT("办公"), TEXT("Office") },
		{ TEXT("展厅"), TEXT("Showroom") },
		{ TEXT("餐厅"), TEXT("Restaurant") },
		{ TEXT("商场"), TEXT("Mall") },
		{ TEXT("学校"), TEXT("School") },
		{ TEXT("医院"), TEXT("Hospital") },
		{ TEXT("景观"), TEXT("Landscape") },
		{ TEXT("园林"), TEXT("Garden") },
		{ TEXT("庭院"), TEXT("Courtyard") },
		{ TEXT("客厅"), TEXT("LivingRoom") },
		{ TEXT("卧室"), TEXT("Bedroom") },
		{ TEXT("厨房"), TEXT("Kitchen") },
		{ TEXT("阳台"), TEXT("Balcony") },
		{ TEXT("大厅"), TEXT("Hall") },
		{ TEXT("入口"), TEXT("Entrance") },
		{ TEXT("立面"), TEXT("Facade") },
		{ TEXT("材质"), TEXT("Material") },
		{ TEXT("灯光"), TEXT("Lighting") },
		{ TEXT("渲染"), TEXT("Render") },
		{ TEXT("测试"), TEXT("Test") },
		{ TEXT("演示"), TEXT("Demo") },
		{ TEXT("新版"), TEXT("New") },
		{ TEXT("旧版"), TEXT("Old") },
		{ TEXT("最终"), TEXT("Final") },
		{ TEXT("工作台"), TEXT("Studio") }
	};

	OutMatchedLength = 0;
	for (const TPair<FString, FString>& Term : Terms)
	{
		if (Value.Mid(StartIndex, Term.Key.Len()).Equals(Term.Key))
		{
			OutEnglish = Term.Value;
			OutMatchedLength = Term.Key.Len();
			return true;
		}
	}
	return false;
}

void AddPathWord(TArray<FString>& Words, const FString& Word)
{
	const FString SafeWord = CollapseUnderscores(Word);
	if (!SafeWord.IsEmpty())
	{
		Words.Add(SafeWord);
	}
}

template <typename ActorType>
ActorType* FindActorByName(UWorld* World, const FName Name)
{
	for (TActorIterator<ActorType> It(World); It; ++It)
	{
		if (It->GetFName() == Name)
		{
			return *It;
		}
	}
	return nullptr;
}
}

const TArray<FString>& FPBRWorkspaceTemplate::GetTemplateFolders()
{
	static const TArray<FString> Folders = {
		TEXT("00_Maps"),
		TEXT("01_Datasmith"),
		TEXT("02_Scene"),
		TEXT("03_Furniture"),
		TEXT("04_Materials"),
		TEXT("05_Lighting"),
		TEXT("06_Cameras"),
		TEXT("07_Render"),
		TEXT("08_Blueprints"),
		TEXT("09_UI"),
		TEXT("10_References"),
		TEXT("11_Library"),
		TEXT("99_Temp")
	};
	return Folders;
}

FPBRWorkspaceTemplateSettings FPBRWorkspaceTemplate::LoadSettings()
{
	FPBRWorkspaceTemplateSettings Settings;
	TSharedPtr<FJsonObject> Config;
	if (FPBRDataStore::LoadConfig(Config) && Config.IsValid())
	{
		Config->TryGetStringField(ConfigProjectNameKey, Settings.ProjectDisplayName);
		Config->TryGetStringField(ConfigFolderNameKey, Settings.ProjectFolderName);
		Config->TryGetBoolField(ConfigSceneEnvironmentKey, Settings.bCreateSceneEnvironment);
	}
	if (Settings.ProjectDisplayName.IsEmpty())
	{
		Settings.ProjectDisplayName = TEXT("新项目");
	}
	if (Settings.ProjectFolderName.IsEmpty())
	{
		Settings.ProjectFolderName = MakeSafeEnglishProjectFolderName(Settings.ProjectDisplayName);
	}
	return Settings;
}

void FPBRWorkspaceTemplate::SaveSettings(const FPBRWorkspaceTemplateSettings& Settings)
{
	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		Config = MakeShared<FJsonObject>();
	}
	Config->SetStringField(ConfigProjectNameKey, Settings.ProjectDisplayName);
	Config->SetStringField(ConfigFolderNameKey, Settings.ProjectFolderName);
	Config->SetBoolField(ConfigSceneEnvironmentKey, Settings.bCreateSceneEnvironment);
	FPBRDataStore::SaveConfig(Config);
}

FString FPBRWorkspaceTemplate::MakeSafeEnglishProjectFolderName(const FString& DisplayName)
{
	TArray<FString> Words;
	FString CurrentAsciiWord;
	bool bSkippedUnknownNonAscii = false;
	for (int32 Index = 0; Index < DisplayName.Len();)
	{
		const TCHAR Ch = DisplayName[Index];
		if (Ch >= TEXT('A') && Ch <= TEXT('Z'))
		{
			CurrentAsciiWord.AppendChar(Ch);
			++Index;
		}
		else if (Ch >= TEXT('a') && Ch <= TEXT('z'))
		{
			CurrentAsciiWord.AppendChar(Ch);
			++Index;
		}
		else if (Ch >= TEXT('0') && Ch <= TEXT('9'))
		{
			CurrentAsciiWord.AppendChar(Ch);
			++Index;
		}
		else if (Ch == TEXT(' ') || Ch == TEXT('-') || Ch == TEXT('_'))
		{
			AddPathWord(Words, CurrentAsciiWord);
			CurrentAsciiWord.Reset();
			++Index;
		}
		else
		{
			AddPathWord(Words, CurrentAsciiWord);
			CurrentAsciiWord.Reset();

			FString EnglishTerm;
			int32 MatchedLength = 0;
			if (MatchChineseTerm(DisplayName, Index, EnglishTerm, MatchedLength))
			{
				AddPathWord(Words, EnglishTerm);
				Index += MatchedLength;
			}
			else
			{
				bSkippedUnknownNonAscii = true;
				++Index;
			}
		}
	}
	AddPathWord(Words, CurrentAsciiWord);

	FString Result = FString::Join(Words, TEXT("_"));
	Result = CollapseUnderscores(Result);
	if (Result.IsEmpty())
	{
		Result = FString::Printf(TEXT("Project_%s"), *MakeShortHash(DisplayName));
	}
	else if (FChar::IsDigit(Result[0]))
	{
		Result = FString::Printf(TEXT("Project_%s"), *Result);
	}
	else if (bSkippedUnknownNonAscii)
	{
		Result = FString::Printf(TEXT("%s_%s"), *Result, *MakeShortHash(DisplayName).Left(4));
	}
	return Result.Left(64);
}

bool FPBRWorkspaceTemplate::CreateTemplate(const FPBRWorkspaceTemplateSettings& Settings, FPBRWorkspaceTemplateResult& OutResult)
{
	FPBRWorkspaceTemplateSettings EffectiveSettings = Settings;
	if (EffectiveSettings.ProjectFolderName.IsEmpty())
	{
		EffectiveSettings.ProjectFolderName = MakeSafeEnglishProjectFolderName(EffectiveSettings.ProjectDisplayName);
	}
	EffectiveSettings.ProjectFolderName = MakeSafeEnglishProjectFolderName(EffectiveSettings.ProjectFolderName);
	SaveSettings(EffectiveSettings);

	OutResult.PackageRoot = FString::Printf(TEXT("/Game/%s"), *EffectiveSettings.ProjectFolderName);
	OutResult.ContentRootDirectory = FPaths::Combine(FPaths::ProjectContentDir(), EffectiveSettings.ProjectFolderName);

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.MakeDirectory(*OutResult.ContentRootDirectory, true))
	{
		OutResult.Message = FString::Printf(TEXT("无法创建项目目录：%s"), *OutResult.ContentRootDirectory);
		return false;
	}

	TArray<FString> PackagePathsToScan;
	PackagePathsToScan.Add(OutResult.PackageRoot);
	for (const FString& Folder : GetTemplateFolders())
	{
		const FString Directory = FPaths::Combine(OutResult.ContentRootDirectory, Folder);
		if (!FileManager.MakeDirectory(*Directory, true))
		{
			OutResult.Message = FString::Printf(TEXT("无法创建目录：%s"), *Directory);
			return false;
		}
		OutResult.CreatedDirectories.Add(Directory);
		PackagePathsToScan.Add(OutResult.PackageRoot / Folder);
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().ScanPathsSynchronous(PackagePathsToScan, true);

	FString EnvironmentMessage;
	if (EffectiveSettings.bCreateSceneEnvironment)
	{
		OutResult.bCreatedSceneEnvironment = CreateSceneEnvironment(EnvironmentMessage);
	}

	OutResult.Message = FString::Printf(
		TEXT("已创建工作模板：%s\n生成目录：%s%s"),
		*EffectiveSettings.ProjectDisplayName,
		*OutResult.PackageRoot,
		EnvironmentMessage.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("\n%s"), *EnvironmentMessage));
	return true;
}

bool FPBRWorkspaceTemplate::CreateSceneEnvironment(FString& OutMessage)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutMessage = TEXT("当前没有可用编辑器场景，已跳过场景环境。");
		return false;
	}

	const FName SunSkyName(TEXT("PBR_WorkTemplate_SunSky"));
	if (!FindActorByName<AActor>(World, SunSkyName))
	{
		if (UClass* SunSkyClass = LoadClass<AActor>(nullptr, TEXT("/Script/SunPosition.SunSky")))
		{
			AActor* SunSkyActor = World->SpawnActor<AActor>(SunSkyClass, FVector::ZeroVector, FRotator::ZeroRotator);
			if (SunSkyActor)
			{
				SunSkyActor->SetActorLabel(TEXT("PBR SunSky"));
				SunSkyActor->Rename(*SunSkyName.ToString());
				OutMessage = TEXT("已生成 SunSky 场景环境。");
				return true;
			}
		}
	}

	bool bCreatedAny = false;
	if (!FindActorByName<ADirectionalLight>(World, TEXT("PBR_WorkTemplate_Sun")))
	{
		ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(-300.0, -300.0, 600.0), FRotator(-45.0, -35.0, 0.0));
		if (Sun)
		{
			Sun->Rename(TEXT("PBR_WorkTemplate_Sun"));
			Sun->SetActorLabel(TEXT("PBR SunSky Sun"));
			Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
			Sun->GetLightComponent()->SetIntensity(7.5f);
			bCreatedAny = true;
		}
	}
	if (!FindActorByName<ASkyLight>(World, TEXT("PBR_WorkTemplate_SkyLight")))
	{
		ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (SkyLight)
		{
			SkyLight->Rename(TEXT("PBR_WorkTemplate_SkyLight"));
			SkyLight->SetActorLabel(TEXT("PBR SunSky SkyLight"));
			SkyLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
			SkyLight->GetLightComponent()->SetIntensity(1.0f);
			bCreatedAny = true;
		}
	}
	if (!FindActorByName<AActor>(World, TEXT("PBR_WorkTemplate_SkyAtmosphere")))
	{
		if (UClass* SkyAtmosphereClass = LoadClass<AActor>(nullptr, TEXT("/Script/Engine.SkyAtmosphere")))
		{
			AActor* Atmosphere = World->SpawnActor<AActor>(SkyAtmosphereClass, FVector::ZeroVector, FRotator::ZeroRotator);
			if (Atmosphere)
			{
				Atmosphere->Rename(TEXT("PBR_WorkTemplate_SkyAtmosphere"));
				Atmosphere->SetActorLabel(TEXT("PBR SunSky Atmosphere"));
				bCreatedAny = true;
			}
		}
	}
	if (!FindActorByName<AActor>(World, TEXT("PBR_WorkTemplate_Clouds")))
	{
		if (UClass* VolumetricCloudClass = LoadClass<AActor>(nullptr, TEXT("/Script/Engine.VolumetricCloud")))
		{
			AActor* Clouds = World->SpawnActor<AActor>(VolumetricCloudClass, FVector::ZeroVector, FRotator::ZeroRotator);
			if (Clouds)
			{
				Clouds->Rename(TEXT("PBR_WorkTemplate_Clouds"));
				Clouds->SetActorLabel(TEXT("PBR SunSky Clouds"));
				bCreatedAny = true;
			}
		}
	}
	if (!FindActorByName<AExponentialHeightFog>(World, TEXT("PBR_WorkTemplate_Fog")))
	{
		AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Fog)
		{
			Fog->Rename(TEXT("PBR_WorkTemplate_Fog"));
			Fog->SetActorLabel(TEXT("PBR SunSky Fog"));
			Fog->GetComponent()->SetFogDensity(0.005f);
			bCreatedAny = true;
		}
	}

	if (bCreatedAny)
	{
		World->MarkPackageDirty();
		OutMessage = TEXT("未找到 SunSky 插件，已生成仿 SunSky 场景环境。");
	}
	else
	{
		OutMessage = TEXT("场景环境已存在，未重复生成。");
	}
	return bCreatedAny;
}
