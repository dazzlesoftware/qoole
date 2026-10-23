// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptBindings.h"
class FWorld;
bool RegisterCoreScriptBindings(FScriptBindingRegistry&, FString& Error,
								std::function<void(const FString&)> Log = {});
// World lifetime is external. Calls resolve stable IDs against the live world;
// destroying that world makes these bindings fail without dereferencing it.
bool RegisterWorldScriptBindings(FScriptBindingRegistry&, std::weak_ptr<FWorld>, FString& Error);
