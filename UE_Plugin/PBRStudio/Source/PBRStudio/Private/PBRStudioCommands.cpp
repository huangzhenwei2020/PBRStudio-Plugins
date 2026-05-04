#include "PBRStudioCommands.h"

#define LOCTEXT_NAMESPACE "FPBRStudioCommands"

void FPBRStudioCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenMainWindow,
		"PBR 工作室",
		"打开 PBR 工作室主窗口",
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE
