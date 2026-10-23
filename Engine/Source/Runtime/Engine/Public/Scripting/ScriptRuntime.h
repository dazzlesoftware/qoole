// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Serialization/Json.h"
#include <memory>
#include <functional>
enum class EScriptPurpose
{
	Gameplay,
	Editor
};
// SDK providers own target validation. No assumption of desktop JIT/dynamic
// library loading is made by the shared gameplay API.
struct FScriptRuntimeRequest
{
	FString Language; // Lua, Python, DotNet, or another registered provider
	FString Target = "Win64";
	EScriptPurpose Purpose = EScriptPurpose::Gameplay;
	uint64 MemoryLimitBytes = 16 * 1024 * 1024;
	uint64 InstructionBudget = 1000000; // per module initialization or call
};
// API names and JSON values form an initial language-neutral binding boundary.
// Object references must be stable world/entity IDs, never native pointers.
class IScriptHost
{
  public:
	virtual ~IScriptHost() = default;
	virtual bool Invoke(const FString& API, const FJsonValue& Arguments, FJsonValue& Result,
						FString& Error) = 0;
};
class IScriptRuntime
{
  public:
	// Create, call and destroy a runtime on its owning scripting thread.
	virtual ~IScriptRuntime() = default;
	// Payload interpretation belongs to the provider (source, VM bytecode or
	// cooked AOT module). Runtime owns its VM and stops it on destruction.
	virtual bool LoadModule(const FString& Name, TArrayView<const uint8> Payload, FString& Error) = 0;
	virtual bool Call(const FString& Module, const FString& Function, const FJsonValue& Arguments,
					  FJsonValue& Result, FString& Error) = 0;
};
struct FScriptProvider
{
	FString Language;
	TArray<FString> GameplayTargets;
	TArray<FString> EditorTargets;
	std::function<std::unique_ptr<IScriptRuntime>(const FScriptRuntimeRequest&, std::shared_ptr<IScriptHost>,
												  FString&)>
		Create;
};
// Per-engine registry, not a process singleton. Each project/world creates an
// isolated runtime. An absent provider is an explicit error, never a fallback.
class FScriptRuntimeRegistry
{
  public:
	bool Register(FScriptProvider, FString& Error);
	bool IsAvailable(const FScriptRuntimeRequest&) const;
	std::unique_ptr<IScriptRuntime> Create(const FScriptRuntimeRequest&, std::shared_ptr<IScriptHost>,
										   FString& Error) const;

  private:
	TArray<FScriptProvider> Providers;
};

// Provider-independent finite JSON transfer limits: depth 64, 100,000 values, 4 MiB.
bool ValidateScriptValue(const FJsonValue&, FString& Error);

// Registers adapter factories; selecting a runtime performs dependency loading.
bool RegisterBuiltinScriptProviders(FScriptRuntimeRegistry&, FString& Error);
