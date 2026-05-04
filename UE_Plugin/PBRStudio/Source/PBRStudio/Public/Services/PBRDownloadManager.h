#pragma once

#include "CoreMinimal.h"
#include "Models/PBRDownloadEntry.h"
#include "Models/PBRDownloadSite.h"
#include "Interfaces/IHttpRequest.h"

DECLARE_DELEGATE(FOnDownloadQueueChanged);
DECLARE_DELEGATE_OneParam(FOnDownloadProgress, int32 /* Index */);
DECLARE_DELEGATE_OneParam(FOnDownloadComplete, int32 /* Index */);

class PBRSTUDIO_API FPBRDownloadManager
{
public:
	FPBRDownloadManager();
	~FPBRDownloadManager();

	void SetMaterialLibraryDir(const FString& Dir);
	FString GetMaterialLibraryDir() const;

	// Queue management
	int32 AddToQueue(const FString& URL, const FString& Name, const FString& Source);
	void RemoveFromQueue(int32 Index);
	void ClearQueue();
	TArray<FPBRDownloadEntry>& GetQueue() { return Queue; }

	// Download execution
	void DownloadEntry(int32 Index);
	void DownloadAllPending();
	void CancelAll();

	// Local archive import. This only organizes files into the material library
	// and publishes the folder to Texture Suite; UE asset import stays there.
	bool ImportLocalArchiveToLibrary(const FString& ArchivePath, FString& OutLibraryFolder, FString& OutMessage);
	bool IsArchiveFile(const FString& FilePath) const;
	void PublishLibraryPathToTextureSuite() const;

	// Sites management
	void LoadSites();
	void SaveSites();
	TArray<FPBRDownloadSite>& GetSites() { return Sites; }
	void AddSite(const FPBRDownloadSite& Site);
	void RemoveSite(int32 Index);

	// Rename an entry's display name and its folder on disk
	void RenameEntry(int32 Index, const FString& NewName);

	// Cleanup options
	bool bCleanNonImage = false;

	void RemoveSourceFolder(int32 Index);
	void CleanNonImageContent(const FString& TargetDir);

	FOnDownloadQueueChanged OnQueueChanged;
	FOnDownloadProgress OnProgress;
	FOnDownloadComplete OnComplete;

private:
	void UpdateDownloadProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived, int32 Index);
	void OnDownloadFinished(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, int32 Index);
	void ExtractZipIfNeeded(int32 Index);
	void RunPBRAnalysis(int32 Index);

	FString ResolveContentDispositionFilename(const FString& Header) const;
	FString MakeMaterialFolderFromName(const FString& RawName) const;
	FString MakeMaterialFolderFromArchive(const FString& ArchivePath) const;
	void NormalizeExtractedMaterialFolder(const FString& Folder, FString& InOutMessage) const;
	bool ExtractZipWithPowerShell(const FString& ArchivePath, const FString& DestinationDir, FString& OutMessage) const;
	bool ExtractArchiveWithExternalTool(const FString& ArchivePath, const FString& DestinationDir, FString& OutMessage) const;

	TArray<FPBRDownloadEntry> Queue;
	TArray<FPBRDownloadSite> Sites;
	TArray<TSharedPtr<IHttpRequest>> ActiveRequests;
	FString MaterialLibraryDir;
};
