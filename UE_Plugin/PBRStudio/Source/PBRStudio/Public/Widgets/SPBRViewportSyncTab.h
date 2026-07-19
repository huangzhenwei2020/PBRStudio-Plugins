#pragma once

#include "CoreMinimal.h"
#include "Services/PBRViewportSyncReceiver.h"
#include "Widgets/SCompoundWidget.h"

class SPBRViewportSyncTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPBRViewportSyncTab) {}
	SLATE_END_ARGS()

	~SPBRViewportSyncTab();
	void Construct(const FArguments& InArgs);

private:
	FReply OnStartStopClicked();
	FText GetStartStopText() const;
	FText GetStatusText() const;
	bool IsReceiverRunning() const;

	TUniquePtr<FPBRViewportSyncReceiver> Receiver;
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
	FString StatusText = TEXT("当前状态：未接收");
};
