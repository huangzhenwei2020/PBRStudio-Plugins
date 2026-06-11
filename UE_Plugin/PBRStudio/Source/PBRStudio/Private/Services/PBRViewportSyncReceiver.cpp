#include "Services/PBRViewportSyncReceiver.h"

#include "Application/ThrottleManager.h"
#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "EditorViewportClient.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Json.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogPBRViewportSync, Log, All);

namespace
{
	constexpr const TCHAR* HandshakeType = TEXT("viewport_handshake");
	constexpr const TCHAR* HandshakeAckType = TEXT("viewport_handshake_ack");
	constexpr const TCHAR* ViewportCameraType = TEXT("viewport_camera");

	FText GetRealtimeOverrideName()
	{
		return FText::FromString(TEXT("AR Studio 视口同步"));
	}

	bool ParseTransformArray(const TArray<TSharedPtr<FJsonValue>>& Values, FMatrix& OutMatrix)
	{
		if (Values.Num() != 16)
		{
			return false;
		}

		OutMatrix = FMatrix::Identity;
		int32 Index = 0;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				OutMatrix.M[Row][Col] = Values[Index++]->AsNumber();
			}
		}
		return true;
	}

	bool ParseViewportPacket(const FString& JsonText, FString& OutType, FPBRViewportSyncPacket& OutPacket)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			return false;
		}

		JsonObject->TryGetStringField(TEXT("type"), OutType);
		JsonObject->TryGetStringField(TEXT("source"), OutPacket.Source);
		JsonObject->TryGetStringField(TEXT("viewportMode"), OutPacket.ViewportMode);

		double SequenceValue = 0.0;
		if (JsonObject->TryGetNumberField(TEXT("sequence"), SequenceValue))
		{
			OutPacket.Sequence = static_cast<int32>(SequenceValue);
		}

		double FovValue = OutPacket.Fov;
		if (JsonObject->TryGetNumberField(TEXT("fov"), FovValue))
		{
			OutPacket.Fov = static_cast<float>(FovValue);
		}

		const TArray<TSharedPtr<FJsonValue>>* TransformValues = nullptr;
		if (JsonObject->TryGetArrayField(TEXT("transform"), TransformValues))
		{
			ParseTransformArray(*TransformValues, OutPacket.Transform);
		}

		return true;
	}

	float ReadVectorAxis(const FVector& InValue, int32 SourceAxis)
	{
		switch (FMath::Clamp(SourceAxis, 0, 2))
		{
		case 0:
			return InValue.X;
		case 1:
			return InValue.Y;
		default:
			return InValue.Z;
		}
	}

	FVector ReadMatrixRow(const FMatrix& InTransform, int32 SourceRow)
	{
		const int32 Row = FMath::Clamp(SourceRow, 0, 2);
		return FVector(InTransform.M[Row][0], InTransform.M[Row][1], InTransform.M[Row][2]);
	}

	FVector ConvertVectorFromMax(const FVector& InValue, const FPBRViewportSyncSettings& Settings)
	{
		return FVector(
			ReadVectorAxis(InValue, Settings.UEXSourceAxis) * Settings.UEXSign,
			ReadVectorAxis(InValue, Settings.UEYSourceAxis) * Settings.UEYSign,
			ReadVectorAxis(InValue, Settings.UEZSourceAxis) * Settings.UEZSign);
	}

	FVector ConvertLocationFromMax(const FVector& InValue, const FPBRViewportSyncSettings& Settings)
	{
		return ConvertVectorFromMax(InValue * Settings.PositionScale, Settings) + Settings.PositionOffset;
	}

	FRotator ConvertRotationFromMax(const FMatrix& InTransform, const FPBRViewportSyncSettings& Settings)
	{
		const FVector RawForward = ReadMatrixRow(InTransform, Settings.ForwardSourceRow) * Settings.ForwardSign;
		const FVector RawUp = ReadMatrixRow(InTransform, Settings.UpSourceRow) * Settings.UpSign;
		const FVector Forward = ConvertVectorFromMax(RawForward, Settings).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const FVector Up = ConvertVectorFromMax(RawUp, Settings).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		return FRotationMatrix::MakeFromXZ(Forward, Up).Rotator() + Settings.RotationOffset;
	}

	FLevelEditorViewportClient* FindPrimaryPerspectiveViewportClient()
	{
		if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsPerspective())
		{
			return GCurrentLevelEditingViewportClient;
		}

		if (!GEditor)
		{
			return nullptr;
		}

		for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
		{
			if (ViewportClient && ViewportClient->IsPerspective())
			{
				return ViewportClient;
			}
		}

		return nullptr;
	}

	void RequestViewportRedraw(FLevelEditorViewportClient* ViewportClient)
	{
		if (!ViewportClient)
		{
			return;
		}

		ViewportClient->RequestRealTimeFrames(1);
		ViewportClient->Invalidate(false, false);
	}

	void ApplyViewportPacketOnGameThread(const FPBRViewportSyncPacket& Packet, const FPBRViewportSyncSettings& Settings)
	{
		if (!GEditor)
		{
			return;
		}

		FLevelEditorViewportClient* ViewportClient = FindPrimaryPerspectiveViewportClient();
		if (!ViewportClient)
		{
			UE_LOG(LogPBRViewportSync, Warning, TEXT("Received viewport packet #%d but no perspective viewport is available."), Packet.Sequence);
			return;
		}

		const FVector RawLocation(Packet.Transform.M[3][0], Packet.Transform.M[3][1], Packet.Transform.M[3][2]);
		const FVector Location = ConvertLocationFromMax(RawLocation, Settings);
		const FRotator Rotation = ConvertRotationFromMax(Packet.Transform, Settings);

		if (Settings.bSyncLocation)
		{
			ViewportClient->SetViewLocation(Location);
		}
		if (Settings.bSyncRotation)
		{
			ViewportClient->SetViewRotation(Rotation);
		}
		if (Settings.bSyncFov)
		{
			ViewportClient->ViewFOV = Packet.Fov;
			ViewportClient->FOVAngle = Packet.Fov;
		}

		RequestViewportRedraw(ViewportClient);
	}
}

FPBRViewportSyncReceiver::~FPBRViewportSyncReceiver()
{
	Stop();
}

bool FPBRViewportSyncReceiver::Start(const FPBRViewportSyncSettings& InSettings)
{
	Stop();

	if (InSettings.ListenPort < 1 || InSettings.ListenPort > 65535)
	{
		SetError(TEXT("端口必须在 1-65535 之间"));
		return false;
	}

	FIPv4Address ListenIp;
	if (!FIPv4Address::Parse(InSettings.ListenAddress, ListenIp))
	{
		SetError(FString::Printf(TEXT("监听地址无效：%s"), *InSettings.ListenAddress));
		return false;
	}

	Settings = InSettings;
	Socket = FUdpSocketBuilder(TEXT("PBRViewportSync"))
		.AsReusable()
		.AsNonBlocking()
		.BoundToEndpoint(FIPv4Endpoint(ListenIp, static_cast<uint16>(Settings.ListenPort)))
		.WithReceiveBufferSize(2 * 1024 * 1024);

	if (!Socket)
	{
		SetError(FString::Printf(TEXT("绑定失败：%s:%d，可能端口被占用"), *Settings.ListenAddress, Settings.ListenPort));
		return false;
	}

	Receiver = MakeUnique<FUdpSocketReceiver>(Socket, FTimespan::FromMilliseconds(10), TEXT("PBRViewportSyncReceiver"));
	Receiver->OnDataReceived().BindRaw(this, &FPBRViewportSyncReceiver::HandleDataReceived);
	Receiver->Start();

	if (IsInGameThread())
	{
		ApplyRuntimeOverrides();
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			ApplyRuntimeOverrides();
		});
	}

	{
		FScopeLock Lock(&StateLock);
		bRunning = true;
		LastError.Empty();
		StatusText = FString::Printf(TEXT("当前状态：接收中，监听 %s:%d，等待 3ds Max 连接"), *Settings.ListenAddress, Settings.ListenPort);
	}

	UE_LOG(LogPBRViewportSync, Log, TEXT("Viewport sync listening on %s:%d"), *Settings.ListenAddress, Settings.ListenPort);
	return true;
}

void FPBRViewportSyncReceiver::Stop()
{
	if (IsInGameThread())
	{
		RestoreRuntimeOverrides();
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			RestoreRuntimeOverrides();
		});
	}

	if (Receiver.IsValid())
	{
		Receiver->Stop();
		Receiver.Reset();
	}

	if (Socket)
	{
		Socket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}

	FScopeLock Lock(&StateLock);
	bRunning = false;
	bHasPendingPacket = false;
	LastAppliedSequence = INDEX_NONE;
	if (LastError.IsEmpty())
	{
		StatusText = TEXT("当前状态：未接收");
	}
}

void FPBRViewportSyncReceiver::ApplyRuntimeOverrides()
{
	if (!GEditor)
	{
		return;
	}

	if (!bSlateThrottleDisabled)
	{
		FSlateThrottleManager::Get().DisableThrottle(true);
		bSlateThrottleDisabled = true;
	}

	if (!bBackgroundThrottleOverrideActive)
	{
		if (UEditorPerformanceSettings* PerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>())
		{
			bBackgroundThrottleWasEnabled = PerformanceSettings->bThrottleCPUWhenNotForeground;
			bBackgroundThrottleOverrideActive = true;
			PerformanceSettings->bThrottleCPUWhenNotForeground = false;
		}
	}

	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (!ViewportClient || !ViewportClient->IsPerspective())
		{
			continue;
		}

		if (!ViewportClient->HasRealtimeOverride(GetRealtimeOverrideName()))
		{
			ViewportClient->AddRealtimeOverride(true, GetRealtimeOverrideName());
		}
		RequestViewportRedraw(ViewportClient);
	}

	if (!ViewportRedrawTickerHandle.IsValid())
	{
		const float ClampedFrameRate = FMath::Clamp(Settings.TargetFrameRate, 5.0f, 120.0f);
		ViewportRedrawTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FPBRViewportSyncReceiver::TickViewportRedraw),
			1.0f / ClampedFrameRate);
	}

	if (ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>())
	{
		LevelEditorSubsystem->EditorInvalidateViewports();
	}
}

void FPBRViewportSyncReceiver::RestoreRuntimeOverrides()
{
	if (ViewportRedrawTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ViewportRedrawTickerHandle);
		ViewportRedrawTickerHandle.Reset();
	}

	if (GEditor)
	{
		for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
		{
			if (!ViewportClient)
			{
				continue;
			}

			ViewportClient->RemoveRealtimeOverride(GetRealtimeOverrideName(), false);
			RequestViewportRedraw(ViewportClient);
		}

		if (ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>())
		{
			LevelEditorSubsystem->EditorInvalidateViewports();
		}
	}

	if (bSlateThrottleDisabled)
	{
		FSlateThrottleManager::Get().DisableThrottle(false);
		bSlateThrottleDisabled = false;
	}

	if (bBackgroundThrottleOverrideActive)
	{
		if (UEditorPerformanceSettings* PerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>())
		{
			PerformanceSettings->bThrottleCPUWhenNotForeground = bBackgroundThrottleWasEnabled;
		}
		bBackgroundThrottleOverrideActive = false;
	}
}

bool FPBRViewportSyncReceiver::TickViewportRedraw(float DeltaTime)
{
	(void)DeltaTime;

	if (!IsRunning() || !GEditor)
	{
		return false;
	}

	FPBRViewportSyncPacket Packet;
	if (ConsumePendingPacket(Packet))
	{
		ApplyViewportPacketOnGameThread(Packet, Settings);
	}

	return true;
}

bool FPBRViewportSyncReceiver::ConsumePendingPacket(FPBRViewportSyncPacket& OutPacket)
{
	FScopeLock Lock(&StateLock);
	if (!bHasPendingPacket)
	{
		return false;
	}

	OutPacket = PendingPacket;
	bHasPendingPacket = false;
	LastAppliedSequence = OutPacket.Sequence;
	return true;
}

bool FPBRViewportSyncReceiver::IsRunning() const
{
	FScopeLock Lock(&StateLock);
	return bRunning;
}

FString FPBRViewportSyncReceiver::GetStatusText() const
{
	FScopeLock Lock(&StateLock);
	return StatusText;
}

FString FPBRViewportSyncReceiver::GetLastError() const
{
	FScopeLock Lock(&StateLock);
	return LastError;
}

void FPBRViewportSyncReceiver::HandleDataReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Sender)
{
	if (!Data.IsValid() || Data->Num() <= 0)
	{
		return;
	}

	FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data->GetData()), Data->Num());
	const FString JsonText(Converter.Length(), Converter.Get());

	FString PacketType;
	FPBRViewportSyncPacket Packet;
	if (!ParseViewportPacket(JsonText, PacketType, Packet))
	{
		return;
	}

	if (PacketType == HandshakeType)
	{
		SendHandshakeAck(Sender, Packet.Sequence, Packet.Source);
		SetStatus(FString::Printf(TEXT("当前状态：接收中，已连接 3ds Max，来自 %s"), *Sender.ToString()));
		return;
	}

	if (PacketType != ViewportCameraType)
	{
		return;
	}

	const FVector RawLocation(Packet.Transform.M[3][0], Packet.Transform.M[3][1], Packet.Transform.M[3][2]);
	const FVector Location = ConvertLocationFromMax(RawLocation, Settings);
	const FRotator Rotation = ConvertRotationFromMax(Packet.Transform, Settings);
	{
		FScopeLock Lock(&StateLock);
		PendingPacket = Packet;
		bHasPendingPacket = true;
		StatusText = FString::Printf(
			TEXT("当前状态：接收中，最后包 #%d，%s，位置 %s，旋转 %s，FOV %.1f"),
			Packet.Sequence,
			*Packet.ViewportMode,
			*Location.ToCompactString(),
			*Rotation.ToCompactString(),
			Packet.Fov);
		LastError.Empty();
	}
}

void FPBRViewportSyncReceiver::SendHandshakeAck(const FIPv4Endpoint& Sender, int32 Sequence, const FString& Source)
{
	if (!Socket)
	{
		return;
	}

	const FString AckJson = FString::Printf(
		TEXT("{\"type\":\"%s\",\"source\":\"ue\",\"sequence\":%d,\"replyTo\":\"%s\"}"),
		HandshakeAckType,
		Sequence,
		*Source);

	FTCHARToUTF8 Utf8(*AckJson);
	int32 BytesSent = 0;
	const TSharedRef<FInternetAddr> SenderAddress = Sender.ToInternetAddr();
	Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), BytesSent, *SenderAddress);
}

void FPBRViewportSyncReceiver::SetStatus(const FString& InStatus)
{
	FScopeLock Lock(&StateLock);
	StatusText = InStatus;
	LastError.Empty();
}

void FPBRViewportSyncReceiver::SetError(const FString& InError)
{
	FScopeLock Lock(&StateLock);
	LastError = InError;
	StatusText = FString::Printf(TEXT("当前状态：启动失败，%s"), *InError);
	bRunning = false;
}
