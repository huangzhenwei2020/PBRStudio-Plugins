#include "Services/PBRHttpServer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* PBRBridgeTokenHeader = TEXT("X-PBRStudio-Token");

	FString RequestHeaderValue(const FHttpServerRequest& Request, const FString& HeaderName)
	{
		if (const TArray<FString>* Values = Request.Headers.Find(HeaderName))
		{
			return Values->Num() > 0 ? (*Values)[0] : FString();
		}

		for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
		{
			if (Header.Key.Equals(HeaderName, ESearchCase::IgnoreCase))
			{
				return Header.Value.Num() > 0 ? Header.Value[0] : FString();
			}
		}
		return FString();
	}
}

FPBRHttpServer::FPBRHttpServer() = default;

FPBRHttpServer::~FPBRHttpServer()
{
	Stop();
}

bool FPBRHttpServer::Start(int32 Port, const FString& InBridgeToken)
{
	if (bIsRunning) return true;

	BridgeToken = InBridgeToken.TrimStartAndEnd();
	if (BridgeToken.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[PBRHttpServer] Bridge token is empty"));
		return false;
	}

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
	BridgeToken.Empty();

	UE_LOG(LogTemp, Display, TEXT("[PBRHttpServer] Stopped"));
}

bool FPBRHttpServer::HandlePing(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!IsAuthorized(Request))
	{
		OnComplete(MakeErrorResponse(TEXT("Unauthorized")));
		return true;
	}

	TSharedRef<FJsonObject> Json = MakeShareable(new FJsonObject);
	Json->SetStringField(TEXT("status"), TEXT("ok"));
	Json->SetStringField(TEXT("service"), TEXT("PBRPushServer"));

	OnComplete(MakeJsonResponse(Json));
	return true;
}

bool FPBRHttpServer::HandlePush(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!IsAuthorized(Request))
	{
		OnComplete(MakeErrorResponse(TEXT("Unauthorized")));
		return true;
	}

	// Parse JSON body
	FString BodyStr;
	if (Request.Body.Num() > 0)
	{
		FUTF8ToTCHAR ConvertedBody(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		BodyStr = FString(ConvertedBody.Length(), ConvertedBody.Get());
	}

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		OnComplete(MakeErrorResponse(TEXT("Invalid JSON")));
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

	OnComplete(MakeJsonResponse(ResultJson));
	return true;
}

bool FPBRHttpServer::HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
	AddCorsHeaders(*Response);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FPBRHttpServer::IsAuthorized(const FHttpServerRequest& Request) const
{
	if (BridgeToken.IsEmpty())
	{
		return false;
	}
	return RequestHeaderValue(Request, PBRBridgeTokenHeader).TrimStartAndEnd() == BridgeToken;
}

TUniquePtr<FHttpServerResponse> FPBRHttpServer::MakeJsonResponse(const TSharedRef<FJsonObject>& Json) const
{
	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Json, Writer);

	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
	AddCorsHeaders(*Response);
	return Response;
}

TUniquePtr<FHttpServerResponse> FPBRHttpServer::MakeErrorResponse(const FString& Error) const
{
	TSharedRef<FJsonObject> Json = MakeShareable(new FJsonObject);
	Json->SetBoolField(TEXT("ok"), false);
	Json->SetStringField(TEXT("error"), Error);
	return MakeJsonResponse(Json);
}

void FPBRHttpServer::AddCorsHeaders(FHttpServerResponse& Response) const
{
	Response.Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response.Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	Response.Headers.Add(TEXT("Access-Control-Allow-Headers"), { FString::Printf(TEXT("Content-Type, %s"), PBRBridgeTokenHeader) });
}
