// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptBindings.h"
#include "Templates/SharedPointer.h"
class FMapDocument;
class FUICommandList;
class FAssetRegistry;
// Resolve the live document for every call. The host owns this context and must
// cancel an outstanding script transaction before replacing its document.
struct FEditorScriptContext
{
	std::function<FMapDocument*()> GetDocument;
	std::function<TSharedPtr<FUICommandList>()> GetCommands;
    std::weak_ptr<FAssetRegistry> Assets;
	FMapDocument* TransactionDocument = nullptr;
	uint64 TransactionId = 0;
};
bool RegisterEditorScriptBindings(FScriptBindingRegistry&, std::weak_ptr<FEditorScriptContext>,
								  FString& Error);
void CancelEditorScriptTransaction(FEditorScriptContext&);
