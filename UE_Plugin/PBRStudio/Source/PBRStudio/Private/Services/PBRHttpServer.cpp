#include "Services/PBRHttpServer.h"
#include "HAL/CriticalSection.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FPBRHttpServer::FPBRHttpServer() = default;

FPBRHttpServer::~FPBRHttpServer()
{
	Stop();
}

bool FPBRHttpServer::Start(int32 Port)
{
	FScopeLock Lock(&ThreadLock);
	if (bIsRunning) return true;

	Router = FHttpServerModule::Get().GetHttpRouter(Port, /* bFailOnBindFailure */ false);
	if (!Router.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[PBRHttpServer] Failed to create HTTP router on port %d"), Port);
		return false;
	}

	FHttpRouteHandle PingHandle = Router->BindRoute(
		FHttpPath(TEXT("/ping")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FPBRHttpServer::HandlePing)
	);
	RouteHandles.Add(PingHandle);

	FHttpRouteHandle PushHandle = Router->BindRoute(
		FHttpPath(TEXT("/push")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FPBRHttpServer::HandlePush)
	);
	RouteHandles.Add(PushHandle);

	// CORS preflight for /push
	FHttpRouteHandle OptionsHandle = Router->BindRoute(
		FHttpPath(TEXT("/push")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FPBRHttpServer::HandleOptions)
	);
	RouteHandles.Add(OptionsHandle);

	FHttpServerModule::Get().StartAllListeners();
	BoundPort = Port;
	bIsRunning = true;

	UE_LOG(LogTemp, Display, TEXT("[PBRHttpServer] Started on port %d"), Port);
	return true;
}

void FPBRHttpServer::Stop()
{
	FScopeLock Lock(&ThreadLock);
	if (!bIsRunning) return;

	for (FHttpRouteHandle& Handle : RouteHandles)
	{
		if (Handle.IsValid() && Router.IsValid())
		{
			Router->UnbindRoute(Handle);
		}
	}
	RouteHandles.Empty();
	Router.Reset();
	bIsRunning = false;
	BoundPort = 0;

	UE_LOG(LogTemp, Display, TEXT("[PBRHttpServer] Stopped"));
}

bool FPBRHttpServer::HandlePing(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FScopeLock Lock(&ThreadLock);
	TSharedRef<FJsonObject> Json = MakeShareable(new FJsonObject);
	Json->SetStringField(TEXT("status"), TEXT("ok"));
	Json->SetStringField(TEXT("service"), TEXT("PBRPushServer"));

	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Json, Writer);

	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	OnComplete(MoveTemp(Response));
	return true;
}

bool FPBRHttpServer::HandlePush(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FScopeLock Lock(&ThreadLock);
	// Parse JSON body
	FString BodyStr = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Request.Body.GetData())));
	BodyStr = BodyStr.Left(Request.Body.Num());

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		TSharedRef<FJsonObject> ErrJson = MakeShareable(new FJsonObject);
		ErrJson->SetBoolField(TEXT("ok"), false);
		ErrJson->SetStringField(TEXT("error"), TEXT("Invalid JSON"));
		FString ErrBody;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ErrBody);
		FJsonSerializer::Serialize(ErrJson, Writer);
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ErrBody, TEXT("application/json"));
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		OnComplete(MoveTemp(Response));
		return true;
	}

	TArray<FString> Urls;
	const TArray<TSharedPtr<FJsonValue>>* UrlsArray;
	if (Json->TryGetArrayField(TEXT("urls"), UrlsArray))
	{
		for (const auto& Val : *UrlsArray)
		{
			Urls.Add(Val->AsString());
		}
	}

	bool bAutoStart = false;
	Json->TryGetBoolField(TEXT("auto_start_download"), bAutoStart);

	OnPBRPush.ExecuteIfBound(Urls, bAutoStart);

	TSharedRef<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
	ResultJson->SetBoolField(TEXT("ok"), true);
	ResultJson->SetNumberField(TEXT("count"), Urls.Num());

	FString ResultBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultBody);
	FJsonSerializer::Serialize(ResultJson, Writer);

	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResultBody, TEXT("application/json"));
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type") });
	OnComplete(MoveTemp(Response));
	return true;
}

bool FPBRHttpServer::HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type") });
	OnComplete(MoveTemp(Response));
	return true;
}
