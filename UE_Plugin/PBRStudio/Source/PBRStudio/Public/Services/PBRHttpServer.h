#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpResultCallback.h"
#include "Dom/JsonObject.h"

DECLARE_DELEGATE_TwoParams(FOnPBRPushReceived, const TArray<FString>& /* URLs */, bool /* bAutoStartDownload */);

class PBRSTUDIO_API FPBRHttpServer
{
public:
	FPBRHttpServer();
	~FPBRHttpServer();

	bool Start(int32 Port = 19528, const FString& InBridgeToken = FString());
	void Stop();
	bool IsRunning() const { return bIsRunning; }
	int32 GetPort() const { return BoundPort; }

	FOnPBRPushReceived OnPBRPush;

private:
	bool HandlePing(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePush(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool IsAuthorized(const FHttpServerRequest& Request) const;
	TUniquePtr<FHttpServerResponse> MakeJsonResponse(const TSharedRef<FJsonObject>& Json) const;
	TUniquePtr<FHttpServerResponse> MakeErrorResponse(const FString& Error) const;
	void AddCorsHeaders(FHttpServerResponse& Response) const;

	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
	FString BridgeToken;
	int32 BoundPort = 0;
	bool bIsRunning = false;
};
