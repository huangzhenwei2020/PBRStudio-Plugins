#pragma once

#include "CoreMinimal.h"
#include "Common/UdpSocketReceiver.h"
#include "Containers/Ticker.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

class FSocket;

struct FPBRViewportSyncPacket
{
	FString Source;
	FString ViewportMode;
	int32 Sequence = 0;
	float Fov = 45.0f;
	FMatrix Transform = FMatrix::Identity;
};

struct FPBRViewportSyncSettings
{
	FString ListenAddress = TEXT("127.0.0.1");
	int32 ListenPort = 39876;
	bool bSyncLocation = true;
	bool bSyncRotation = true;
	bool bSyncFov = true;
	float TargetFrameRate = 60.0f;
	float PositionScale = 0.1f;
	int32 UEXSourceAxis = 0;
	float UEXSign = 1.0f;
	int32 UEYSourceAxis = 1;
	float UEYSign = -1.0f;
	int32 UEZSourceAxis = 2;
	float UEZSign = 1.0f;
	int32 ForwardSourceRow = 2;
	float ForwardSign = -1.0f;
	int32 UpSourceRow = 1;
	float UpSign = 1.0f;
	FVector PositionOffset = FVector::ZeroVector;
	FRotator RotationOffset = FRotator::ZeroRotator;
};

class FPBRViewportSyncReceiver
{
public:
	FPBRViewportSyncReceiver() = default;
	~FPBRViewportSyncReceiver();

	bool Start(const FPBRViewportSyncSettings& InSettings);
	void Stop();
	bool IsRunning() const;
	FString GetStatusText() const;
	FString GetLastError() const;

private:
	void HandleDataReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Sender);
	void SendHandshakeAck(const FIPv4Endpoint& Sender, int32 Sequence, const FString& Source);
	void SetStatus(const FString& InStatus);
	void SetError(const FString& InError);
	void ApplyRuntimeOverrides();
	void RestoreRuntimeOverrides();
	bool TickViewportRedraw(float DeltaTime);
	bool ConsumePendingPacket(FPBRViewportSyncPacket& OutPacket);

	FPBRViewportSyncSettings Settings;
	FSocket* Socket = nullptr;
	TUniquePtr<FUdpSocketReceiver> Receiver;
	FTSTicker::FDelegateHandle ViewportRedrawTickerHandle;
	bool bSlateThrottleDisabled = false;
	bool bBackgroundThrottleOverrideActive = false;
	bool bBackgroundThrottleWasEnabled = false;

	mutable FCriticalSection StateLock;
	FString StatusText = TEXT("当前状态：未接收");
	FString LastError;
	bool bRunning = false;
	FPBRViewportSyncPacket PendingPacket;
	bool bHasPendingPacket = false;
	int32 LastAppliedSequence = INDEX_NONE;
};
