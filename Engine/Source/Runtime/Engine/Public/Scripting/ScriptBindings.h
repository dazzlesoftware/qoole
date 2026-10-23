// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptRuntime.h"
#include <thread>
// Descriptors are shared by every language provider. Capability names are host
// contracts (e.g. World.Read, World.Write, Editor.Transactions), not language APIs.
struct FScriptArgument
{
	FString Name;
	EJsonType Type = EJsonType::Null;
	bool bRequired = true;
};
struct FScriptBinding
{
	FString Name;
	FString Capability;
	bool bEditorOnly = false;
	TArray<FScriptArgument> Arguments;
	EJsonType ResultType = EJsonType::Null;
	std::function<bool(const FJsonValue&, FJsonValue&, FString&)> Invoke;
};
class FScriptBindingRegistry final : public IScriptHost
{
  public:
	FScriptBindingRegistry(EScriptPurpose, TArray<FString> Capabilities);
	bool Register(FScriptBinding, FString& Error);
	bool RegisterBatch(TArray<FScriptBinding>, FString& Error);
	bool Invoke(const FString&, const FJsonValue&, FJsonValue&, FString& Error) override;
	// Stable, provider-neutral binding metadata for tooling and generated facades.
	FJsonValue Describe() const;

  private:
	EScriptPurpose Purpose;
	TArray<FString> Capabilities;
	TArray<FScriptBinding> Bindings;
	std::thread::id Owner;
};
