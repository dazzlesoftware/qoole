// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptBindings.h"
class FAssetRegistry;
bool RegisterMaterialScriptBindings(FScriptBindingRegistry&, FString& Error,
									std::weak_ptr<FAssetRegistry> Assets = {});
