// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "EditorScriptExecution.h"
#include "EditorAssetScriptBindings.h"
#include "Scripting/WorldScriptBindings.h"
#include "HAL/File.h"
#include "MaterialScriptBindings.h"
#include "Scripting/AssetScriptBindings.h"
bool ExecuteEditorScript(const FEditorScriptRequest& Request, std::shared_ptr<FEditorScriptContext> Context,
						 FJsonValue& Result, FString& Error)
{
	if (!Context || !Context->GetDocument || !Context->GetDocument())
	{
		Error = "Editor script document has expired.";
		return false;
	}
	struct FCleanup
	{
		std::shared_ptr<FEditorScriptContext> Context;
		~FCleanup()
		{
			CancelEditorScriptTransaction(*Context);
		}
	} Cleanup{Context};
	auto Host = std::make_shared<FScriptBindingRegistry>(EScriptPurpose::Editor, Request.Capabilities);
	FScriptRuntimeRegistry Providers;
	if (!RegisterBuiltinScriptProviders(Providers, Error) || !RegisterCoreScriptBindings(*Host, Error) ||
		!RegisterEditorScriptBindings(*Host, Context, Error) ||
		!RegisterMaterialScriptBindings(*Host, Error, Context->Assets) ||
		!RegisterAssetScriptBindings(*Host, Context->Assets, Error) ||
		!RegisterEditorAssetScriptBindings(*Host, Context, Error))
		return false;
	auto Runtime = Providers.Create({Request.Language, "Win64", EScriptPurpose::Editor}, Host, Error);
	if (!Runtime)
		return false;
	TArray<uint8> Payload;
	if (!FFile::ReadAllBytes(Request.ModulePath, Payload))
	{
		Error = "Cannot read editor script module: " + Request.ModulePath;
		return false;
	}
	FJsonValue Candidate;
	if (!Runtime->LoadModule("main", Payload, Error) ||
		!Runtime->Call("main", Request.Function, Request.Arguments, Candidate, Error))
		return false;
	if (Context->TransactionId != 0)
	{
		Error = "Editor script returned with an unfinished transaction; its edits were rolled back.";
		return false;
	}
	Result = std::move(Candidate);
	Error.Empty();
	return true;
}
