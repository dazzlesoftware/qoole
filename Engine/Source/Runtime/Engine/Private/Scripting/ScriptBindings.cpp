// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/ScriptBindings.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
namespace
{
bool NameValid(const FString& Name)
{
	return !Name.IsEmpty() && Name.Len() <= 128 && Name.View().find('\0') == std::string_view::npos;
}
bool JsonValid(const FJsonValue& V, int Depth, int& Nodes, size_t& Bytes)
{
	if (Depth > 64 || ++Nodes > 100000 || (Bytes += 16) > 4 * 1024 * 1024)
		return false;
	if (V.GetType() == EJsonType::Number)
		return std::isfinite(V.AsNumber());
	if (V.GetType() == EJsonType::String)
		return (Bytes += V.AsString().Len()) <= 4 * 1024 * 1024;
	if (V.IsObject())
		for (const auto& Key : V.GetKeys())
			if ((Bytes += Key.Len()) > 4 * 1024 * 1024 ||
				!JsonValid(V.Get(Key.View()), Depth + 1, Nodes, Bytes))
				return false;
	if (V.IsArray())
		for (const auto& Item : V.GetItems())
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
		Error = "Script value exceeds finite JSON transfer limits.";
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
		Error = "Script bindings must register on the owning thread.";
		return false;
	}
	if (!NameValid(Binding.Name) || !NameValid(Binding.Capability) || !Binding.Invoke)
	{
		Error = "Script binding requires a name, capability and callback.";
		return false;
	}
	if (std::any_of(Bindings.begin(), Bindings.end(), [&](const auto& B) { return B.Name == Binding.Name; }))
	{
		Error = "Duplicate script binding: " + Binding.Name;
		return false;
	}
	std::set<FString> Names;
	for (const auto& A : Binding.Arguments)
		if (!NameValid(A.Name) || !Names.insert(A.Name).second)
		{
			Error = "Invalid or duplicate script argument.";
			return false;
		}
	Bindings.push_back(std::move(Binding));
	Error.Empty();
	return true;
}
bool FScriptBindingRegistry::RegisterBatch(TArray<FScriptBinding> Batch, FString& Error)
{
	auto Candidate = *this;
	for (auto& Binding : Batch)
		if (!Candidate.Register(std::move(Binding), Error))
			return false;
	Bindings = std::move(Candidate.Bindings);
	Error.Empty();
	return true;
}
bool FScriptBindingRegistry::Invoke(const FString& Name, const FJsonValue& Arguments, FJsonValue& Result,
									FString& Error)
{
	if (std::this_thread::get_id() != Owner)
	{
		Error = "Script host calls must run on the owning thread.";
		return false;
	}
	const auto It =
		std::find_if(Bindings.begin(), Bindings.end(), [&](const auto& B) { return B.Name == Name; });
	if (It == Bindings.end())
	{
		Error = "Unknown script binding: " + Name;
		return false;
	}
	const auto& B = *It;
	if ((B.bEditorOnly && Purpose != EScriptPurpose::Editor) ||
		std::find(Capabilities.begin(), Capabilities.end(), B.Capability) == Capabilities.end())
	{
		Error = "Script capability denied: " + B.Capability;
		return false;
	}
	if (!Arguments.IsObject() || !JsonValid(Arguments))
	{
		Error = "Script binding arguments must be a finite, bounded JSON object.";
		return false;
	}
	for (const auto& A : B.Arguments)
	{
		const auto* Value = Arguments.Find(A.Name.View());
		if ((A.bRequired && !Value) || (Value && Value->GetType() != A.Type))
		{
			Error = "Invalid script argument: " + A.Name;
			return false;
		}
	}
	for (const auto& Key : Arguments.GetKeys())
		if (std::none_of(B.Arguments.begin(), B.Arguments.end(),
						 [&](const auto& A) { return A.Name == Key; }))
		{
			Error = "Unknown script argument: " + Key;
			return false;
		}
	try
	{
		FJsonValue Candidate;
		// Copy the callback: a host may register additional bindings from a callback.
		const auto Callback = B.Invoke;
		const auto ResultType = B.ResultType;
		Error.Empty();
		if (!Callback(Arguments, Candidate, Error))
		{
			if (Error.IsEmpty())
				Error = "Script binding failed: " + Name;
			return false;
		}
		if (Candidate.GetType() != ResultType || !JsonValid(Candidate))
		{
			Error = "Script binding returned a value outside its declared contract.";
			return false;
		}
		Result = std::move(Candidate);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = "Script binding " + Name + ": " + E.what();
		return false;
	}
	catch (...)
	{
		Error = "Script binding " + Name + " failed with an unknown exception.";
		return false;
	}
}
FJsonValue FScriptBindingRegistry::Describe() const
{
	auto Result = FJsonValue::MakeObject();
	Result["Version"] = 1;
	Result["Bindings"] = FJsonValue::MakeArray();
	for (const auto& B : Bindings)
	{
		auto V = FJsonValue::MakeObject();
		V["Name"] = B.Name;
		V["Capability"] = B.Capability;
		V["EditorOnly"] = B.bEditorOnly;
		V["ResultType"] = static_cast<int>(B.ResultType);
		V["Arguments"] = FJsonValue::MakeArray();
		for (const auto& A : B.Arguments)
		{
			auto P = FJsonValue::MakeObject();
			P["Name"] = A.Name;
			P["Type"] = static_cast<int>(A.Type);
			P["Required"] = A.bRequired;
			V["Arguments"].Push(P);
		}
		Result["Bindings"].Push(V);
	}
	return Result;
}
