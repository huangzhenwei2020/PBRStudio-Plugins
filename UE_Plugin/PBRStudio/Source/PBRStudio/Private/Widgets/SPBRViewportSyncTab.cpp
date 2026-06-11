#include "Widgets/SPBRViewportSyncTab.h"

#include "Services/PBRViewportSyncReceiver.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SPBRViewportSyncTab"

namespace
{
	FString MakeAxisSpecText(int32 SourceAxis, float Sign)
	{
		const TCHAR* AxisName = TEXT("X");
		switch (FMath::Clamp(SourceAxis, 0, 2))
		{
		case 1:
			AxisName = TEXT("Y");
			break;
		case 2:
			AxisName = TEXT("Z");
			break;
		default:
			AxisName = TEXT("X");
			break;
		}

		return Sign < 0.0f ? FString::Printf(TEXT("-%s"), AxisName) : FString(AxisName);
	}

	bool ParseAxisSpecText(const FString& RawText, int32& OutSourceAxis, float& OutSign)
	{
		FString Value = RawText.TrimStartAndEnd().ToUpper();
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ReplaceInline(TEXT("\t"), TEXT(""));
		Value.ReplaceInline(TEXT("＋"), TEXT("+"));
		Value.ReplaceInline(TEXT("－"), TEXT("-"));

		OutSign = 1.0f;
		if (Value.StartsWith(TEXT("+")))
		{
			Value = Value.RightChop(1);
		}
		else if (Value.StartsWith(TEXT("-")))
		{
			OutSign = -1.0f;
			Value = Value.RightChop(1);
		}

		if (Value == TEXT("X"))
		{
			OutSourceAxis = 0;
			return true;
		}
		if (Value == TEXT("Y"))
		{
			OutSourceAxis = 1;
			return true;
		}
		if (Value == TEXT("Z"))
		{
			OutSourceAxis = 2;
			return true;
		}

		return false;
	}

	TSharedRef<SWidget> MakeAxisMappingRow(
		const FText& Label,
		TFunction<int32()> GetSourceAxis,
		TFunction<void(int32)> SetSourceAxis,
		TFunction<float()> GetSign,
		TFunction<void(float)> SetSign)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("FromMaxAxis", "来自 Max"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 6, 0)
			[
				SNew(SBox)
				.WidthOverride(72)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([GetSourceAxis, GetSign]()
					{
						return FText::FromString(MakeAxisSpecText(GetSourceAxis(), GetSign()));
					})
					.OnTextCommitted_Lambda([SetSourceAxis, SetSign](const FText& NewText, ETextCommit::Type)
					{
						int32 ParsedAxis = 0;
						float ParsedSign = 1.0f;
						if (ParseAxisSpecText(NewText.ToString(), ParsedAxis, ParsedSign))
						{
							SetSourceAxis(ParsedAxis);
							SetSign(ParsedSign);
						}
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("AxisSpecHint", "可填 X / Y / Z / -X / -Y / -Z"))
			];
	}

	TSharedRef<SWidget> MakeRotationSourceRow(
		const FText& Label,
		TFunction<int32()> GetSourceRow,
		TFunction<void(int32)> SetSourceRow,
		TFunction<float()> GetSign,
		TFunction<void(float)> SetSign)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("FromMatrixRow", "来自 Max"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 6, 0)
			[
				SNew(SBox)
				.WidthOverride(72)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([GetSourceRow, GetSign]()
					{
						return FText::FromString(MakeAxisSpecText(GetSourceRow(), GetSign()));
					})
					.OnTextCommitted_Lambda([SetSourceRow, SetSign](const FText& NewText, ETextCommit::Type)
					{
						int32 ParsedRow = 0;
						float ParsedSign = 1.0f;
						if (ParseAxisSpecText(NewText.ToString(), ParsedRow, ParsedSign))
						{
							SetSourceRow(ParsedRow);
							SetSign(ParsedSign);
						}
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("RotationSpecHint", "可填 X / Y / Z / -X / -Y / -Z"))
			];
	}
}

SPBRViewportSyncTab::~SPBRViewportSyncTab()
{
	if (Receiver.IsValid())
	{
		Receiver->Stop();
		Receiver.Reset();
	}
}

void SPBRViewportSyncTab::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 10, 12, 6)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "视口同步"))
				.Font(FAppStyle::GetFontStyle("HeadingMedium"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 10)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SPBRViewportSyncTab::GetStatusText)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 10)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("Address", "监听地址"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 12, 0)
				[
					SNew(SBox)
					.WidthOverride(130)
					[
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return FText::FromString(ListenAddress); })
						.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
						{
							ListenAddress = NewText.ToString().TrimStartAndEnd();
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("Port", "端口"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 12, 0)
				[
					SNew(SNumericEntryBox<int32>)
					.MinDesiredValueWidth(80)
					.MinValue(1)
					.MaxValue(65535)
					.Value_Lambda([this]() { return ListenPort; })
					.OnValueChanged_Lambda([this](int32 NewValue) { ListenPort = FMath::Clamp(NewValue, 1, 65535); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(this, &SPBRViewportSyncTab::GetStartStopText)
					.OnClicked(this, &SPBRViewportSyncTab::OnStartStopClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bSyncLocation ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bSyncLocation = State == ECheckBoxState::Checked; })
					[
						SNew(STextBlock).Text(LOCTEXT("SyncLocation", "同步位置"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bSyncRotation ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bSyncRotation = State == ECheckBoxState::Checked; })
					[
						SNew(STextBlock).Text(LOCTEXT("SyncRotation", "同步旋转"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bSyncFov ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bSyncFov = State == ECheckBoxState::Checked; })
					[
						SNew(STextBlock).Text(LOCTEXT("SyncFov", "同步FOV"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("Scale", "位置缩放"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 18, 0)
				[
					SNew(SNumericEntryBox<float>)
					.MinDesiredValueWidth(80)
					.MinValue(0.001f)
					.Value_Lambda([this]() { return PositionScale; })
					.OnValueChanged_Lambda([this](float NewValue) { PositionScale = FMath::Max(0.001f, NewValue); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("ScaleHint", "默认 0.1"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(18, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("TargetFrameRate", "刷新帧率"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SNumericEntryBox<float>)
					.MinDesiredValueWidth(80)
					.MinValue(5.0f)
					.MaxValue(120.0f)
					.Value_Lambda([this]() { return TargetFrameRate; })
					.OnValueChanged_Lambda([this](float NewValue) { TargetFrameRate = FMath::Clamp(NewValue, 5.0f, 120.0f); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("TargetFrameRateUnit", "FPS"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 4, 12, 4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AxisTitle", "位置轴向转换"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 4)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("AxisHint", "直接填写 X、Y、Z，反方向填写 -X、-Y、-Z。默认是 UE X=X，UE Y=-Y，UE Z=Z。"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 4)
			[
				MakeAxisMappingRow(
					LOCTEXT("UEXMapping", "UE X"),
					[this]() { return UEXSourceAxis; },
					[this](int32 NewValue) { UEXSourceAxis = NewValue; },
					[this]() { return UEXSign; },
					[this](float NewValue) { UEXSign = NewValue; })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 4)
			[
				MakeAxisMappingRow(
					LOCTEXT("UEYMapping", "UE Y"),
					[this]() { return UEYSourceAxis; },
					[this](int32 NewValue) { UEYSourceAxis = NewValue; },
					[this]() { return UEYSign; },
					[this](float NewValue) { UEYSign = NewValue; })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 8)
			[
				MakeAxisMappingRow(
					LOCTEXT("UEZMapping", "UE Z"),
					[this]() { return UEZSourceAxis; },
					[this](int32 NewValue) { UEZSourceAxis = NewValue; },
					[this]() { return UEZSign; },
					[this](float NewValue) { UEZSign = NewValue; })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("PresetDefault", "默认：保持 XYZ 只翻 Y"))
					.OnClicked_Lambda([this]()
					{
						UEXSourceAxis = 0;
						UEXSign = 1.0f;
						UEYSourceAxis = 1;
						UEYSign = -1.0f;
						UEZSourceAxis = 2;
						UEZSign = 1.0f;
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("PresetSwapXY", "备用：交换 X/Y"))
					.OnClicked_Lambda([this]()
					{
						UEXSourceAxis = 1;
						UEXSign = 1.0f;
						UEYSourceAxis = 0;
						UEYSign = -1.0f;
						UEZSourceAxis = 2;
						UEZSign = 1.0f;
						return FReply::Handled();
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 4, 12, 4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RotationTitle", "旋转方向转换"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 4)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("RotationHint", "旋转方向也直接填写 X、Y、Z 或 -X、-Y、-Z。默认前向=-Z，上方向=Y。"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 4)
			[
				MakeRotationSourceRow(
					LOCTEXT("ForwardMapping", "前向"),
					[this]() { return ForwardSourceRow; },
					[this](int32 NewValue) { ForwardSourceRow = NewValue; },
					[this]() { return ForwardSign; },
					[this](float NewValue) { ForwardSign = NewValue; })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 8)
			[
				MakeRotationSourceRow(
					LOCTEXT("UpMapping", "上方向"),
					[this]() { return UpSourceRow; },
					[this](int32 NewValue) { UpSourceRow = NewValue; },
					[this]() { return UpSign; },
					[this](float NewValue) { UpSign = NewValue; })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 8)
			[
				SNew(SButton)
				.Text(LOCTEXT("PresetRotationDefault", "重置旋转方向"))
				.OnClicked_Lambda([this]()
				{
					ForwardSourceRow = 2;
					ForwardSign = -1.0f;
					UpSourceRow = 1;
					UpSign = 1.0f;
					RotationOffset = FRotator::ZeroRotator;
					return FReply::Handled();
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("PositionOffset", "位置偏移"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SNumericEntryBox<float>).MinDesiredValueWidth(72).Value_Lambda([this]() { return static_cast<float>(PositionOffset.X); }).OnValueChanged_Lambda([this](float V) { PositionOffset.X = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(STextBlock).Text(LOCTEXT("X", "X"))]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SNumericEntryBox<float>).MinDesiredValueWidth(72).Value_Lambda([this]() { return static_cast<float>(PositionOffset.Y); }).OnValueChanged_Lambda([this](float V) { PositionOffset.Y = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(STextBlock).Text(LOCTEXT("Y", "Y"))]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SNumericEntryBox<float>).MinDesiredValueWidth(72).Value_Lambda([this]() { return static_cast<float>(PositionOffset.Z); }).OnValueChanged_Lambda([this](float V) { PositionOffset.Z = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(STextBlock).Text(LOCTEXT("Z", "Z"))]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12, 0, 12, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("RotationOffset", "旋转偏移"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SNumericEntryBox<float>).MinDesiredValueWidth(72).Value_Lambda([this]() { return RotationOffset.Pitch; }).OnValueChanged_Lambda([this](float V) { RotationOffset.Pitch = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(STextBlock).Text(LOCTEXT("Pitch", "Pitch"))]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SNumericEntryBox<float>).MinDesiredValueWidth(72).Value_Lambda([this]() { return RotationOffset.Yaw; }).OnValueChanged_Lambda([this](float V) { RotationOffset.Yaw = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(STextBlock).Text(LOCTEXT("Yaw", "Yaw"))]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SNumericEntryBox<float>).MinDesiredValueWidth(72).Value_Lambda([this]() { return RotationOffset.Roll; }).OnValueChanged_Lambda([this](float V) { RotationOffset.Roll = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(STextBlock).Text(LOCTEXT("Roll", "Roll"))]
			]
		]
	];
}

bool SPBRViewportSyncTab::IsReceiverRunning() const
{
	return Receiver.IsValid() && Receiver->IsRunning();
}

FReply SPBRViewportSyncTab::OnStartStopClicked()
{
	if (IsReceiverRunning())
	{
		Receiver->Stop();
		Receiver.Reset();
		StatusText = TEXT("当前状态：未接收");
		return FReply::Handled();
	}

	FPBRViewportSyncSettings Settings;
	Settings.ListenAddress = ListenAddress;
	Settings.ListenPort = ListenPort;
	Settings.bSyncLocation = bSyncLocation;
	Settings.bSyncRotation = bSyncRotation;
	Settings.bSyncFov = bSyncFov;
	Settings.TargetFrameRate = TargetFrameRate;
	Settings.PositionScale = PositionScale;
	Settings.UEXSourceAxis = UEXSourceAxis;
	Settings.UEXSign = UEXSign;
	Settings.UEYSourceAxis = UEYSourceAxis;
	Settings.UEYSign = UEYSign;
	Settings.UEZSourceAxis = UEZSourceAxis;
	Settings.UEZSign = UEZSign;
	Settings.ForwardSourceRow = ForwardSourceRow;
	Settings.ForwardSign = ForwardSign;
	Settings.UpSourceRow = UpSourceRow;
	Settings.UpSign = UpSign;
	Settings.PositionOffset = PositionOffset;
	Settings.RotationOffset = RotationOffset;

	Receiver = MakeUnique<FPBRViewportSyncReceiver>();
	if (!Receiver->Start(Settings))
	{
		StatusText = FString::Printf(TEXT("当前状态：启动失败，%s"), *Receiver->GetLastError());
		Receiver.Reset();
	}
	else
	{
		StatusText = TEXT("当前状态：接收中");
	}

	return FReply::Handled();
}

FText SPBRViewportSyncTab::GetStartStopText() const
{
	return IsReceiverRunning() ? LOCTEXT("StopReceiving", "停止接收") : LOCTEXT("StartReceiving", "开始接收");
}

FText SPBRViewportSyncTab::GetStatusText() const
{
	if (Receiver.IsValid())
	{
		return FText::FromString(Receiver->GetStatusText());
	}

	return FText::FromString(StatusText);
}

#undef LOCTEXT_NAMESPACE
