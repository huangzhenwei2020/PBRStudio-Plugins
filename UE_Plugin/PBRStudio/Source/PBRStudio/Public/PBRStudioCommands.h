#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FPBRStudioCommands : public TCommands<FPBRStudioCommands>
{
public:
	FPBRStudioCommands()
		: TCommands<FPBRStudioCommands>(
			TEXT("PBRStudio"),
			NSLOCTEXT("Contexts", "PBRStudio", "建筑可视化工作台插件"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenMainWindow;
};
