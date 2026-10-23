// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptRuntime.h"
// Registers only validated targets; no global VM or hidden language fallback.
bool RegisterLuaScriptProvider(FScriptRuntimeRegistry&, FString& Error);
