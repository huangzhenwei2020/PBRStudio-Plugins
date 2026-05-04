#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FPBRStudioCommands : public TCommands<FPBRStudioCommands>
{
public:
	FPBRStudioCommands()
		: TCommands<FPBRStudioCommands>(
			TEXT("PBRStudio"),
			NSLOCTEXT("Contexts", "PBRStudio", "PBR 工作室插件"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenMainWindow;
};
