// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "EditorScriptBindings.h"
struct FEditorScriptRequest
{
	FString Language;
	FString ModulePath;
	FString Function = "Main";
	FJsonValue Arguments = FJsonValue::MakeObject();
	TArray<FString> Capabilities = {"API.Read", "Log.Write", "Editor.Read", "Editor.Write", "Material.Read", "Assets.Read"};
};
// Executes a trusted synchronous editor module through the same entry point as
// GenesisEditor -runscript. Always rolls back unfinished script transactions.
bool ExecuteEditorScript(const FEditorScriptRequest&, std::shared_ptr<FEditorScriptContext>,
						 FJsonValue& Result, FString& Error);
