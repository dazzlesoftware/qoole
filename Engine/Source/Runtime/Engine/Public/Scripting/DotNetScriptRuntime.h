// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptRuntime.h"
bool RegisterDotNetScriptProvider(FScriptRuntimeRegistry&, FString& Error,
								  const FString& BridgeDirectory = {});
