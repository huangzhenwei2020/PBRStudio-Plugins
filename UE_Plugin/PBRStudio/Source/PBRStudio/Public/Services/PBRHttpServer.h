#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpResultCallback.h"

DECLARE_DELEGATE_TwoParams(FOnPBRPushReceived, const TArray<FString>& /* URLs */, bool /* bAutoStartDownload */);

class PBRSTUDIO_API FPBRHttpServer
{
public:
	FPBRHttpServer();
	~FPBRHttpServer();

	bool Start(int32 Port = 19528);
	void Stop();
	bool IsRunning() const { return bIsRunning; }
	int32 GetPort() const { return BoundPort; }

	FOnPBRPushReceived OnPBRPush;

private:
	bool HandlePing(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePush(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	mutable FCriticalSection ThreadLock;
	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
	int32 BoundPort = 0;
	bool bIsRunning = false;
};
