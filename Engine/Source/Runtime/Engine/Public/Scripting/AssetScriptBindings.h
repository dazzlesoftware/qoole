// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptBindings.h"
#include "AssetRegistry.h"
// Runtime discovery and loading only. All references retain native ID authority.
bool RegisterAssetScriptBindings(FScriptBindingRegistry&, std::weak_ptr<FAssetRegistry>, FString& Error);
