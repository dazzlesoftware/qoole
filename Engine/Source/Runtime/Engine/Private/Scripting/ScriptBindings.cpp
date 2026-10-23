// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/ScriptBindings.h"
#include "Internationalization/TextLocalization.h"
#include "Scripting/ScriptLimits.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
namespace
{
	bool NameValid(const FString& Name)
	{
		return !Name.IsEmpty() && Name.Len() <= ScriptLimits::MaxNameLength &&
		    Name.View().find('\0') == std::string_view::npos;
	}
	bool JsonValid(const FJsonValue& V, int Depth, int& Nodes, size_t& Bytes)
	{
		if (Depth > ScriptLimits::MaxJsonDepth || ++Nodes > ScriptLimits::MaxJsonNodes ||
		    (Bytes += ScriptLimits::JsonNodeBytes) > ScriptLimits::MaxTransferBytes)
			return false;
		if (V.GetType() == EJsonType::Number)
			return std::isfinite(V.AsNumber());
		if (V.GetType() == EJsonType::String)
			return (Bytes += V.AsString().Len()) <= ScriptLimits::MaxTransferBytes;
		if (V.IsObject())
			for (const FString& Key : V.GetKeys())
				if ((Bytes += Key.Len()) > ScriptLimits::MaxTransferBytes ||
				    !JsonValid(V.Get(Key.View()), Depth + 1, Nodes, Bytes))
					return false;
		if (V.IsArray())
			for (const FJsonValue& Item : V.GetItems())
				if (!JsonValid(Item, Depth + 1, Nodes, Bytes))
					return false;
		return true;
	}
	bool JsonValid(const FJsonValue& V)
	{
		int Nodes = 0;
		size_t Bytes = 0;
		return JsonValid(V, 0, Nodes, Bytes);
	}
} // namespace
bool ValidateScriptValue(const FJsonValue& V, FString& Error)
{
	if (!JsonValid(V))
	{
		Error = GENESIS_TEXT("Script value exceeds finite JSON transfer limits.");
		return false;
	}
	Error.Empty();
	return true;
}
FScriptBindingRegistry::FScriptBindingRegistry(EScriptPurpose P, TArray<FString> C)
    : Purpose(P), Capabilities(std::move(C)), Owner(std::this_thread::get_id())
{
}
bool FScriptBindingRegistry::Register(FScriptBinding Binding, FString& Error)
{
	if (std::this_thread::get_id() != Owner)
	{
		Error = GENESIS_TEXT("Script bindings must register on the owning thread.");
		return false;
	}
	if (!NameValid(Binding.Name) || !NameValid(Binding.Capability) || !Binding.Invoke)
	{
		Error = GENESIS_TEXT("Script binding requires a name, capability and callback.");
		return false;
	}
	if (std::any_of(Bindings.begin(), Bindings.end(), [&](const FScriptBinding& B) { return B.Name == Binding.Name; }))
	{
		Error = GENESIS_TEXT("Duplicate script binding: ") + Binding.Name;
		return false;
	}
	std::set<FString> Names;
	for (const FScriptArgument& A : Binding.Arguments)
		if (!NameValid(A.Name) || !Names.insert(A.Name).second)
		{
			Error = GENESIS_TEXT("Invalid or duplicate script argument.");
			return false;
		}
	Bindings.push_back(std::move(Binding));
	Error.Empty();
	return true;
}
bool FScriptBindingRegistry::RegisterBatch(TArray<FScriptBinding> Batch, FString& Error)
{
	FScriptBindingRegistry Candidate = *this;
	for (FScriptBinding& Binding : Batch)
		if (!Candidate.Register(std::move(Binding), Error))
			return false;
	Bindings = std::move(Candidate.Bindings);
	Error.Empty();
	return true;
}
bool FScriptBindingRegistry::Invoke(
    const FString& Name, const FJsonValue& Arguments, FJsonValue& Result, FString& Error)
{
	if (std::this_thread::get_id() != Owner)
	{
		Error = GENESIS_TEXT("Script host calls must run on the owning thread.");
		return false;
	}
	if (!NameValid(Name))
	{
		Error = GENESIS_TEXT("Invalid host API name.");
		return false;
	}
	const TArray<FScriptBinding>::const_iterator It =
	    std::find_if(Bindings.begin(), Bindings.end(), [&](const FScriptBinding& B) { return B.Name == Name; });
	if (It == Bindings.end())
	{
		Error = GENESIS_TEXT("Unknown script binding: ") + Name;
		return false;
	}
	const FScriptBinding& B = *It;
	if ((B.bEditorOnly && Purpose != EScriptPurpose::Editor) ||
	    std::find(Capabilities.begin(), Capabilities.end(), B.Capability) == Capabilities.end())
	{
		Error = GENESIS_TEXT("Script capability denied: ") + B.Capability;
		return false;
	}
	if (!Arguments.IsObject() || !JsonValid(Arguments))
	{
		Error = GENESIS_TEXT("Script binding arguments must be a finite, bounded JSON object.");
		return false;
	}
	for (const FScriptArgument& A : B.Arguments)
	{
		const FJsonValue* Value = Arguments.Find(A.Name.View());
		if ((A.bRequired && !Value) || (Value && Value->GetType() != A.Type))
		{
			Error = GENESIS_TEXT("Invalid script argument: ") + A.Name;
			return false;
		}
	}
	for (const FString& Key : Arguments.GetKeys())
		if (std::none_of(
		        B.Arguments.begin(), B.Arguments.end(), [&](const FScriptArgument& A) { return A.Name == Key; }))
		{
			Error = GENESIS_TEXT("Unknown script argument: ") + Key;
			return false;
		}
	try
	{
		FJsonValue Candidate;
		// Copy the callback: a host may register additional bindings from a callback.
		const std::function<bool(const FJsonValue&, FJsonValue&, FString&)> Callback = B.Invoke;
		const EJsonType ResultType = B.ResultType;
		Error.Empty();
		if (!Callback(Arguments, Candidate, Error))
		{
			if (Error.IsEmpty())
				Error = GENESIS_TEXT("Script binding failed: ") + Name;
			return false;
		}
		if (Candidate.GetType() != ResultType || !JsonValid(Candidate))
		{
			Error = GENESIS_TEXT("Script binding returned a value outside its declared contract.");
			return false;
		}
		Result = std::move(Candidate);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = FTextLocalization::Format("Script binding {}: {}", Name, E.what());
		return false;
	}
	catch (...)
	{
		Error = FTextLocalization::Format("Script binding {} failed with an unknown exception.", Name);
		return false;
	}
}
FJsonValue FScriptBindingRegistry::Describe() const
{
	FJsonValue Result = FJsonValue::MakeObject();
	Result["Version"] = 1;
	Result["Bindings"] = FJsonValue::MakeArray();
	for (const FScriptBinding& B : Bindings)
	{
		FJsonValue V = FJsonValue::MakeObject();
		V["Name"] = B.Name;
		V["Capability"] = B.Capability;
		V["EditorOnly"] = B.bEditorOnly;
		V["ResultType"] = static_cast<int>(B.ResultType);
		V["Arguments"] = FJsonValue::MakeArray();
		for (const FScriptArgument& A : B.Arguments)
		{
			FJsonValue P = FJsonValue::MakeObject();
			P["Name"] = A.Name;
			P["Type"] = static_cast<int>(A.Type);
			P["Required"] = A.bRequired;
			V["Arguments"].Push(P);
		}
		Result["Bindings"].Push(V);
	}
	return Result;
}
