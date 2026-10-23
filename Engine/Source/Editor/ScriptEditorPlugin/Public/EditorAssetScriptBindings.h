// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "EditorScriptBindings.h"
bool RegisterEditorAssetScriptBindings(FScriptBindingRegistry&, std::weak_ptr<FEditorScriptContext>,
									   FString&);
