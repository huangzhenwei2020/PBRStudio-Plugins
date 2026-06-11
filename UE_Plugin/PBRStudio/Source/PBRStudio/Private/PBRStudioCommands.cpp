#include "PBRStudioCommands.h"

#define LOCTEXT_NAMESPACE "FPBRStudioCommands"

void FPBRStudioCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenMainWindow,
		"建筑可视化工作台",
		"打开 AR Studio 主窗口",
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE
