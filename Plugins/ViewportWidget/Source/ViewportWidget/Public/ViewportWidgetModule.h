// Copyright 2024 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "Modules/ModuleManager.h"

//------------------------------------------------------
// FViewportWidgetModule
//------------------------------------------------------

class FViewportWidgetModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};