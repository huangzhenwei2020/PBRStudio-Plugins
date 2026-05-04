#include "Services/PBRDownloadManager.h"
#include "Services/PBRDataStore.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "GenericPlatform/GenericPlatformHttp.h"

FPBRDownloadManager::FPBRDownloadManager()
{
	MaterialLibraryDir = FPaths::ProjectSavedDir() / TEXT("PBRStudio") / TEXT("Library");
	IFileManager::Get().MakeDirectory(*MaterialLibraryDir, true);
	LoadSites();
}

FPBRDownloadManager::~FPBRDownloadManager()
{
	for (TSharedPtr<IHttpRequest>& Req : ActiveRequests)
	{
		if (Req.IsValid())
		{
			Req->OnProcessRequestComplete().Unbind();
			Req->CancelRequest();
		}
	}
	ActiveRequests.Empty();
}

void FPBRDownloadManager::SetMaterialLibraryDir(const FString& Dir)
{
	MaterialLibraryDir = Dir;
	IFileManager::Get().MakeDirectory(*MaterialLibraryDir, true);
}

FString FPBRDownloadManager::GetMaterialLibraryDir() const
{
	return MaterialLibraryDir;
}

int32 FPBRDownloadManager::AddToQueue(const FString& URL, const FString& Name, const FString& Source)
{
	for (const FPBRDownloadEntry& E : Queue)
	{
		if (E.URL == URL)
		{
			return INDEX_NONE;
		}
	}

	FPBRDownloadEntry Entry;
	Entry.URL = URL;
	Entry.Name = Name.IsEmpty() ? FPaths::GetCleanFilename(URL) : Name;
	Entry.Source = Source;
	Entry.TargetDirectory = MaterialLibraryDir;
	Entry.Status = TEXT("等待");
	Entry.DetailStatus = TEXT("等待下载");
	Entry.Progress = 0.0f;

	const FString Lower = URL.ToLower();
	if (Lower.EndsWith(TEXT(".zip")) || Lower.EndsWith(TEXT(".rar")) || Lower.EndsWith(TEXT(".7z")) ||
		Lower.EndsWith(TEXT(".png")) || Lower.EndsWith(TEXT(".jpg")) || Lower.EndsWith(TEXT(".jpeg")) ||
		Lower.EndsWith(TEXT(".exr")) || Lower.EndsWith(TEXT(".hdr")) || Lower.EndsWith(TEXT(".fbx")))
	{
		Entry.bAutoStartDownload = true;
	}

	const int32 Index = Queue.Add(Entry);
	OnQueueChanged.ExecuteIfBound();
	return Index;
}

void FPBRDownloadManager::RemoveFromQueue(int32 Index)
{
	if (Index >= 0 && Index < Queue.Num())
	{
		Queue.RemoveAt(Index);
		OnQueueChanged.ExecuteIfBound();
	}
}

void FPBRDownloadManager::ClearQueue()
{
	Queue.Empty();
	OnQueueChanged.ExecuteIfBound();
}

void FPBRDownloadManager::DownloadEntry(int32 Index)
{
	if (Index < 0 || Index >= Queue.Num())
	{
		return;
	}

	FPBRDownloadEntry& Entry = Queue[Index];
	Entry.Status = TEXT("下载中");
	Entry.DetailStatus = TEXT("正在连接");
	Entry.Progress = 0.0f;
	OnQueueChanged.ExecuteIfBound();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Entry.URL);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
	Request->SetHeader(TEXT("Accept"), TEXT("*/*"));
	Request->OnRequestProgress64().BindRaw(this, &FPBRDownloadManager::UpdateDownloadProgress, Index);
	Request->OnProcessRequestComplete().BindRaw(this, &FPBRDownloadManager::OnDownloadFinished, Index);
	ActiveRequests.Add(Request);
	Request->ProcessRequest();
}

void FPBRDownloadManager::DownloadAllPending()
{
	for (int32 i = 0; i < Queue.Num(); ++i)
	{
		if (Queue[i].Status == TEXT("等待"))
		{
			DownloadEntry(i);
		}
	}
}

void FPBRDownloadManager::CancelAll()
{
	for (TSharedPtr<IHttpRequest>& Req : ActiveRequests)
	{
		if (Req.IsValid())
		{
			Req->CancelRequest();
		}
	}
	ActiveRequests.Empty();
	for (FPBRDownloadEntry& E : Queue)
	{
		if (E.Status == TEXT("下载中"))
		{
			E.Status = TEXT("已取消");
			E.DetailStatus = TEXT("已取消");
		}
	}
	OnQueueChanged.ExecuteIfBound();
}

void FPBRDownloadManager::UpdateDownloadProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived, int32 Index)
{
	if (Index < 0 || Index >= Queue.Num())
	{
		return;
	}

	FPBRDownloadEntry& Entry = Queue[Index];
	int64 ContentLength = 0;
	if (Request.IsValid() && Request->GetResponse().IsValid())
	{
		ContentLength = FCString::Atoi64(*Request->GetResponse()->GetHeader(TEXT("Content-Length")));
	}

	if (ContentLength > 0)
	{
		Entry.Progress = FMath::Clamp(static_cast<float>(BytesReceived) / static_cast<float>(ContentLength), 0.0f, 1.0f);
		Entry.DetailStatus = FString::Printf(TEXT("下载中 %.0f%%"), Entry.Progress * 100.0f);
	}
	else
	{
		Entry.DetailStatus = FString::Printf(TEXT("已接收 %.2f MB"), static_cast<double>(BytesReceived) / 1024.0 / 1024.0);
	}
	OnProgress.ExecuteIfBound(Index);
	OnQueueChanged.ExecuteIfBound();
}

FString FPBRDownloadManager::ResolveContentDispositionFilename(const FString& Header) const
{
	FString Lower = Header.ToLower();
	int32 Idx = Lower.Find(TEXT("filename="));
	if (Idx == INDEX_NONE)
	{
		return FString();
	}

	Idx += 9;
	FString Filename;
	bool bInQuotes = false;
	for (int32 i = Idx; i < Header.Len(); ++i)
	{
		const TCHAR c = Header[i];
		if (c == '"')
		{
			bInQuotes = !bInQuotes;
			continue;
		}
		if (!bInQuotes && (c == ';' || c == ' '))
		{
			break;
		}
		Filename.AppendChar(c);
	}
	return Filename.TrimQuotes().TrimStartAndEnd();
}

static FString FilenameFromUrlQuery(const FString& URL)
{
	FString Query;
	if (!URL.Split(TEXT("?"), nullptr, &Query))
	{
		return FString();
	}

	int32 HashIndex = INDEX_NONE;
	if (Query.FindChar('#', HashIndex))
	{
		Query = Query.Left(HashIndex);
	}

	TArray<FString> Pairs;
	Query.ParseIntoArray(Pairs, TEXT("&"), true);
	const TArray<FString> PreferredKeys = {
		TEXT("file"), TEXT("filename"), TEXT("name"), TEXT("download"), TEXT("dl"), TEXT("path")
	};

	for (const FString& Key : PreferredKeys)
	{
		for (const FString& Pair : Pairs)
		{
			FString Left;
			FString Right;
			if (!Pair.Split(TEXT("="), &Left, &Right))
			{
				continue;
			}
			if (!Left.Equals(Key, ESearchCase::IgnoreCase))
			{
				continue;
			}
			FString Decoded = FGenericPlatformHttp::UrlDecode(Right).TrimStartAndEnd().TrimQuotes();
			Decoded = FPaths::GetCleanFilename(Decoded);
			if (!Decoded.IsEmpty() && Decoded.Contains(TEXT(".")))
			{
				return Decoded;
			}
		}
	}
	return FString();
}

void FPBRDownloadManager::OnDownloadFinished(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, int32 Index)
{
	if (Index < 0 || Index >= Queue.Num())
	{
		return;
	}

	FPBRDownloadEntry& Entry = Queue[Index];
	if (!bSucceeded || !Response.IsValid())
	{
		Entry.Status = TEXT("失败");
		Entry.DetailStatus = TEXT("网络错误");
		OnQueueChanged.ExecuteIfBound();
		OnComplete.ExecuteIfBound(Index);
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		Entry.Status = TEXT("失败");
		Entry.DetailStatus = FString::Printf(TEXT("HTTP %d"), Code);
		OnQueueChanged.ExecuteIfBound();
		OnComplete.ExecuteIfBound(Index);
		return;
	}

	FString Filename = ResolveContentDispositionFilename(Response->GetHeader(TEXT("Content-Disposition")));
	if (Filename.IsEmpty())
	{
		Filename = FilenameFromUrlQuery(Entry.URL);
	}
	if (Filename.IsEmpty())
	{
		Filename = FPaths::GetCleanFilename(Entry.URL);
		int32 QueryIndex = INDEX_NONE;
		if (Filename.FindChar('?', QueryIndex))
		{
			Filename = Filename.Left(QueryIndex);
		}
	}
	if (Filename.IsEmpty())
	{
		Filename = TEXT("download");
	}

	const bool bArchiveDownload = IsArchiveFile(Filename);
	FString SaveDirectory = Entry.TargetDirectory;
	if (bArchiveDownload)
	{
		Entry.TargetDirectory = MakeMaterialFolderFromName(FPaths::GetBaseFilename(Filename));
		IFileManager::Get().MakeDirectory(*Entry.TargetDirectory, true);
		SaveDirectory = FPaths::Combine(Entry.TargetDirectory, TEXT("_source"));
		IFileManager::Get().MakeDirectory(*SaveDirectory, true);
	}

	const FString TargetPath = FPaths::Combine(SaveDirectory, Filename);
	const TArray<uint8> Data = Response->GetContent();
	if (!FFileHelper::SaveArrayToFile(Data, *TargetPath))
	{
		Entry.Status = TEXT("失败");
		Entry.DetailStatus = TEXT("保存文件失败");
		OnQueueChanged.ExecuteIfBound();
		OnComplete.ExecuteIfBound(Index);
		return;
	}

	Entry.DownloadedFile = TargetPath;
	Entry.Status = TEXT("下载完成");
	Entry.DetailStatus = TEXT("下载完成");
	Entry.Progress = 1.0f;
	OnQueueChanged.ExecuteIfBound();
	OnProgress.ExecuteIfBound(Index);

	ExtractZipIfNeeded(Index);
	RunPBRAnalysis(Index);
	PublishLibraryPathToTextureSuite();
	OnComplete.ExecuteIfBound(Index);
}

void FPBRDownloadManager::ExtractZipIfNeeded(int32 Index)
{
	if (Index < 0 || Index >= Queue.Num())
	{
		return;
	}

	FPBRDownloadEntry& Entry = Queue[Index];
	if (!IsArchiveFile(Entry.DownloadedFile))
	{
		return;
	}

	Entry.Status = TEXT("解压中");
	Entry.DetailStatus = TEXT("正在解压");
	OnQueueChanged.ExecuteIfBound();

	FString Message;
	const FString Ext = FPaths::GetExtension(Entry.DownloadedFile, true).ToLower();
	const bool bExtracted = Ext == TEXT(".zip")
		? ExtractZipWithPowerShell(Entry.DownloadedFile, Entry.TargetDirectory, Message)
		: ExtractArchiveWithExternalTool(Entry.DownloadedFile, Entry.TargetDirectory, Message);

	if (bExtracted)
	{
		NormalizeExtractedMaterialFolder(Entry.TargetDirectory, Message);
		Entry.Name = FPaths::GetCleanFilename(Entry.TargetDirectory);
		Entry.Status = TEXT("已解压");
		Entry.DetailStatus = Message;
		if (FPaths::FileExists(Entry.DownloadedFile))
		{
			IFileManager::Get().Delete(*Entry.DownloadedFile, false, true);
			Entry.DownloadedFile.Empty();
		}
		RemoveSourceFolder(Index);
		if (bCleanNonImage)
		{
			CleanNonImageContent(Entry.TargetDirectory);
			Message += TEXT(" | 已清理非图片文件");
			Entry.DetailStatus = Message;
		}
	}
	else
	{
		Entry.Status = TEXT("解压失败");
		Entry.DetailStatus = Message;
		Entry.PbrAnalysisMessage = Message;
	}
	OnQueueChanged.ExecuteIfBound();
}

void FPBRDownloadManager::RunPBRAnalysis(int32 Index)
{
	if (Index < 0 || Index >= Queue.Num())
	{
		return;
	}

	FPBRDownloadEntry& Entry = Queue[Index];
	const FString TargetDir = IFileManager::Get().DirectoryExists(*Entry.TargetDirectory)
		? Entry.TargetDirectory
		: FPaths::GetPath(Entry.DownloadedFile);

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *TargetDir, TEXT("*.*"), true, false);

	Entry.bPbrOk = false;
	Entry.bPbrComplete = false;

	int32 ImageCount = 0;
	for (const FString& F : Files)
	{
		const FString Ext = FPaths::GetExtension(F, true).ToLower();
		if (Ext == TEXT(".png") || Ext == TEXT(".jpg") || Ext == TEXT(".jpeg") ||
			Ext == TEXT(".tga") || Ext == TEXT(".exr") || Ext == TEXT(".tif") ||
			Ext == TEXT(".tiff") || Ext == TEXT(".bmp"))
		{
			++ImageCount;
		}
	}

	if (ImageCount >= 3)
	{
		Entry.bPbrOk = true;
		Entry.bPbrComplete = true;
		Entry.PbrAnalysisMessage = FString::Printf(TEXT("发现 %d 个图片文件，可作为贴图套件扫描"), ImageCount);
	}
	else
	{
		Entry.PbrAnalysisMessage = FString::Printf(TEXT("发现 %d 个图片文件，请检查是否为完整材质包"), ImageCount);
	}
}

bool FPBRDownloadManager::IsArchiveFile(const FString& FilePath) const
{
	const FString Ext = FPaths::GetExtension(FilePath, true).ToLower();
	return Ext == TEXT(".zip") || Ext == TEXT(".rar") || Ext == TEXT(".7z");
}

FString FPBRDownloadManager::MakeMaterialFolderFromName(const FString& RawName) const
{
	FString BaseName = RawName;
	const TCHAR* InvalidChars = TEXT("\\/:*?\"<>|");
	for (int32 i = 0; InvalidChars[i] != 0; ++i)
	{
		BaseName.ReplaceCharInline(InvalidChars[i], TEXT('_'));
	}
	BaseName = BaseName.TrimStartAndEnd();
	if (BaseName.IsEmpty())
	{
		BaseName = TEXT("ImportedMaterial");
	}

	FString Candidate = FPaths::Combine(MaterialLibraryDir, BaseName);
	if (!IFileManager::Get().DirectoryExists(*Candidate))
	{
		return Candidate;
	}

	for (int32 Suffix = 2; Suffix < 10000; ++Suffix)
	{
		Candidate = FPaths::Combine(MaterialLibraryDir, FString::Printf(TEXT("%s_%02d"), *BaseName, Suffix));
		if (!IFileManager::Get().DirectoryExists(*Candidate))
		{
			return Candidate;
		}
	}
	return FPaths::Combine(MaterialLibraryDir, BaseName + TEXT("_New"));
}

FString FPBRDownloadManager::MakeMaterialFolderFromArchive(const FString& ArchivePath) const
{
	return MakeMaterialFolderFromName(FPaths::GetBaseFilename(ArchivePath));
}

static bool IsPBRJunkExtractName(const FString& Name)
{
	const FString Lower = Name.ToLower();
	return Lower == TEXT("__macosx")
		|| Lower == TEXT(".ds_store")
		|| Lower == TEXT("thumbs.db")
		|| Lower == TEXT("desktop.ini")
		|| Lower.StartsWith(TEXT("._"));
}

static FString MakeUniqueExtractDestination(const FString& Folder, const FString& FileName)
{
	FString Candidate = FPaths::Combine(Folder, FileName);
	if (!FPaths::FileExists(Candidate) && !FPaths::DirectoryExists(Candidate))
	{
		return Candidate;
	}

	const FString Base = FPaths::GetBaseFilename(FileName);
	const FString Ext = FPaths::GetExtension(FileName, true);
	for (int32 Index = 2; Index < 10000; ++Index)
	{
		Candidate = FPaths::Combine(Folder, FString::Printf(TEXT("%s_%02d%s"), *Base, Index, *Ext));
		if (!FPaths::FileExists(Candidate) && !FPaths::DirectoryExists(Candidate))
		{
			return Candidate;
		}
	}
	return FPaths::Combine(Folder, Base + TEXT("_New") + Ext);
}

static void DeleteKnownJunkExtractFiles(const FString& Folder)
{
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.*"), true, false);
	for (const FString& File : Files)
	{
		if (IsPBRJunkExtractName(FPaths::GetCleanFilename(File)))
		{
			IFileManager::Get().Delete(*File, false, true);
		}
	}

	TArray<FString> Dirs;
	IFileManager::Get().FindFilesRecursive(Dirs, *Folder, TEXT("*"), false, true);
	for (const FString& Dir : Dirs)
	{
		if (IsPBRJunkExtractName(FPaths::GetCleanFilename(Dir)))
		{
			IFileManager::Get().DeleteDirectory(*Dir, false, true);
		}
	}
}

void FPBRDownloadManager::NormalizeExtractedMaterialFolder(const FString& Folder, FString& InOutMessage) const
{
	if (!FPaths::DirectoryExists(Folder))
	{
		return;
	}

	DeleteKnownJunkExtractFiles(Folder);

	bool bFlattened = false;
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		TArray<FString> RootFiles;
		IFileManager::Get().FindFiles(RootFiles, *(Folder / TEXT("*")), true, false);
		RootFiles.RemoveAll([](const FString& FileName)
		{
			return IsPBRJunkExtractName(FileName);
		});

		TArray<FString> RootDirs;
		IFileManager::Get().FindFiles(RootDirs, *(Folder / TEXT("*")), false, true);
		RootDirs.RemoveAll([](const FString& DirName)
		{
			return IsPBRJunkExtractName(DirName) || DirName.Equals(TEXT("_source"), ESearchCase::IgnoreCase);
		});

		if (RootFiles.Num() > 0 || RootDirs.Num() != 1)
		{
			break;
		}

		const FString NestedDir = FPaths::Combine(Folder, RootDirs[0]);

		TArray<FString> ChildFiles;
		IFileManager::Get().FindFiles(ChildFiles, *(NestedDir / TEXT("*")), true, false);
		for (const FString& FileName : ChildFiles)
		{
			if (IsPBRJunkExtractName(FileName))
			{
				continue;
			}
			const FString Src = FPaths::Combine(NestedDir, FileName);
			const FString Dst = MakeUniqueExtractDestination(Folder, FileName);
			IFileManager::Get().Move(*Dst, *Src, false, true);
		}

		TArray<FString> ChildDirs;
		IFileManager::Get().FindFiles(ChildDirs, *(NestedDir / TEXT("*")), false, true);
		for (const FString& DirName : ChildDirs)
		{
			if (IsPBRJunkExtractName(DirName))
			{
				continue;
			}
			const FString Src = FPaths::Combine(NestedDir, DirName);
			const FString Dst = MakeUniqueExtractDestination(Folder, DirName);
			IFileManager::Get().Move(*Dst, *Src, false, true);
		}

		IFileManager::Get().DeleteDirectory(*NestedDir, false, true);
		bFlattened = true;
	}

	if (bFlattened)
	{
		InOutMessage += TEXT(" | Normalized nested folders");
	}
}

bool FPBRDownloadManager::ExtractZipWithPowerShell(const FString& ArchivePath, const FString& DestinationDir, FString& OutMessage) const
{
	FString NormalizedArchive = FPaths::ConvertRelativePathToFull(ArchivePath);
	FString NormalizedDestination = FPaths::ConvertRelativePathToFull(DestinationDir);
	NormalizedArchive.ReplaceInline(TEXT("'"), TEXT("''"));
	NormalizedDestination.ReplaceInline(TEXT("'"), TEXT("''"));

	const FString Args = FString::Printf(
		TEXT("-NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\""),
		*NormalizedArchive,
		*NormalizedDestination);

	int32 ReturnCode = 0;
	FString Output;
	FString Error;
	const bool bStarted = FPlatformProcess::ExecProcess(TEXT("powershell.exe"), *Args, &ReturnCode, &Output, &Error);
	if (!bStarted || ReturnCode != 0)
	{
		OutMessage = Error.IsEmpty() ? Output : Error;
		if (OutMessage.IsEmpty())
		{
			OutMessage = TEXT("ZIP 解压失败");
		}
		return false;
	}

	OutMessage = TEXT("ZIP 已解压");
	return true;
}

bool FPBRDownloadManager::ExtractArchiveWithExternalTool(const FString& ArchivePath, const FString& DestinationDir, FString& OutMessage) const
{
	TArray<FString> CandidateTools;
	CandidateTools.Add(TEXT("C:/Program Files/7-Zip/7z.exe"));
	CandidateTools.Add(TEXT("C:/Program Files (x86)/7-Zip/7z.exe"));
	CandidateTools.Add(TEXT("C:/Program Files/WinRAR/WinRAR.exe"));
	CandidateTools.Add(TEXT("C:/Program Files (x86)/WinRAR/WinRAR.exe"));

	FString ToolPath;
	for (const FString& Candidate : CandidateTools)
	{
		if (FPaths::FileExists(Candidate))
		{
			ToolPath = Candidate;
			break;
		}
	}

	if (ToolPath.IsEmpty())
	{
		OutMessage = TEXT("RAR/7Z 需要安装 7-Zip 或 WinRAR 后才能自动解压");
		return false;
	}

	const bool bSevenZip = FPaths::GetBaseFilename(ToolPath).Equals(TEXT("7z"), ESearchCase::IgnoreCase);
	const FString Args = bSevenZip
		? FString::Printf(TEXT("x \"%s\" -o\"%s\" -y"), *ArchivePath, *DestinationDir)
		: FString::Printf(TEXT("x -ibck -y \"%s\" \"%s\\\""), *ArchivePath, *DestinationDir);

	int32 ReturnCode = 0;
	FString Output;
	FString Error;
	const bool bStarted = FPlatformProcess::ExecProcess(*ToolPath, *Args, &ReturnCode, &Output, &Error);
	if (!bStarted || ReturnCode != 0)
	{
		OutMessage = Error.IsEmpty() ? Output : Error;
		if (OutMessage.IsEmpty())
		{
			OutMessage = TEXT("压缩包解压失败");
		}
		return false;
	}

	OutMessage = TEXT("压缩包已解压");
	return true;
}

void FPBRDownloadManager::PublishLibraryPathToTextureSuite() const
{
	TSharedPtr<FJsonObject> Config;
	if (!FPBRDataStore::LoadConfig(Config) || !Config.IsValid())
	{
		Config = MakeShareable(new FJsonObject);
	}
	Config->SetStringField(TEXT("texture_suite_last_folder"), MaterialLibraryDir);
	FPBRDataStore::SaveConfig(Config);
}

bool FPBRDownloadManager::ImportLocalArchiveToLibrary(const FString& ArchivePath, FString& OutLibraryFolder, FString& OutMessage)
{
	if (!FPaths::FileExists(ArchivePath))
	{
		OutMessage = TEXT("文件不存在");
		return false;
	}

	if (!IsArchiveFile(ArchivePath))
	{
		OutMessage = TEXT("只支持 zip、rar、7z 压缩包");
		return false;
	}

	IFileManager::Get().MakeDirectory(*MaterialLibraryDir, true);
	OutLibraryFolder = MakeMaterialFolderFromArchive(ArchivePath);
	IFileManager::Get().MakeDirectory(*OutLibraryFolder, true);

	const FString SourceFolder = FPaths::Combine(OutLibraryFolder, TEXT("_source"));
	IFileManager::Get().MakeDirectory(*SourceFolder, true);
	const FString CopiedArchive = FPaths::Combine(SourceFolder, FPaths::GetCleanFilename(ArchivePath));
	if (IFileManager::Get().Copy(*CopiedArchive, *ArchivePath, true, true) != COPY_OK)
	{
		OutMessage = TEXT("复制压缩包到材质库失败");
		return false;
	}

	FPBRDownloadEntry Entry;
	Entry.Name = FPaths::GetBaseFilename(ArchivePath);
	Entry.URL = ArchivePath;
	Entry.Source = TEXT("本地拖入");
	Entry.TargetDirectory = OutLibraryFolder;
	Entry.DownloadedFile = CopiedArchive;
	Entry.Status = TEXT("已整理");
	Entry.DetailStatus = TEXT("已复制到材质库");
	Entry.Progress = 1.0f;

	const int32 Index = Queue.Add(Entry);
	ExtractZipIfNeeded(Index);
	RunPBRAnalysis(Index);
	PublishLibraryPathToTextureSuite();
	OutMessage = Queue[Index].DetailStatus;
	OnQueueChanged.ExecuteIfBound();
	return true;
}

void FPBRDownloadManager::RenameEntry(int32 Index, const FString& NewName)
{
	if (Index < 0 || Index >= Queue.Num() || NewName.IsEmpty()) return;

	FPBRDownloadEntry& Entry = Queue[Index];
	const FString OldName = Entry.Name;
	Entry.Name = NewName;

	// Rename folder on disk if TargetDirectory exists and its basename matches the old name
	const FString OldDir = Entry.TargetDirectory;
	if (FPaths::DirectoryExists(OldDir) && FPaths::GetCleanFilename(OldDir) == OldName)
	{
		const FString ParentDir = FPaths::GetPath(OldDir);
		const FString NewDir = FPaths::Combine(ParentDir, NewName);
		if (!FPaths::DirectoryExists(NewDir))
		{
			IFileManager::Get().Move(*NewDir, *OldDir, true, true);
			Entry.TargetDirectory = NewDir;
		}
	}

	OnQueueChanged.ExecuteIfBound();
}

void FPBRDownloadManager::RemoveSourceFolder(int32 Index)
{
	if (Index < 0 || Index >= Queue.Num()) return;
	const FString SourceDir = FPaths::Combine(Queue[Index].TargetDirectory, TEXT("_source"));
	if (FPaths::DirectoryExists(SourceDir))
	{
		IFileManager::Get().DeleteDirectory(*SourceDir, false, true);
	}
}

void FPBRDownloadManager::CleanNonImageContent(const FString& TargetDir)
{
	if (!FPaths::DirectoryExists(TargetDir)) return;

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *TargetDir, TEXT("*.*"), true, false);
	for (const FString& File : Files)
	{
		const FString Ext = FPaths::GetExtension(File, true).ToLower();
		if (Ext != TEXT(".png") && Ext != TEXT(".jpg") && Ext != TEXT(".jpeg") &&
			Ext != TEXT(".tga") && Ext != TEXT(".exr") && Ext != TEXT(".tif") &&
			Ext != TEXT(".tiff") && Ext != TEXT(".bmp") && Ext != TEXT(".hdr") &&
			Ext != TEXT(".webp"))
		{
			IFileManager::Get().Delete(*File, false, true);
		}
	}
}

void FPBRDownloadManager::LoadSites()
{
	FPBRDataStore::LoadDownloadSites(Sites);
}

void FPBRDownloadManager::SaveSites()
{
	FPBRDataStore::SaveDownloadSites(Sites);
}

void FPBRDownloadManager::AddSite(const FPBRDownloadSite& Site)
{
	Sites.Add(Site);
	SaveSites();
}

void FPBRDownloadManager::RemoveSite(int32 Index)
{
	if (Index >= 0 && Index < Sites.Num())
	{
		Sites.RemoveAt(Index);
		SaveSites();
	}
}
