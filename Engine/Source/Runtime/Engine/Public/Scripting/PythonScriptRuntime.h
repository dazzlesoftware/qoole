// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptRuntime.h"
// Empty home uses GENESIS_PYTHON_HOME or Binaries/ThirdParty/Python.
bool RegisterPythonScriptProvider(FScriptRuntimeRegistry&, FString& Error, const FString& RuntimeHome = {});
