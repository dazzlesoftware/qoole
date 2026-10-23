// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Scripting/ScriptRuntime.h"
#include "World/World.h"
#include <thread>
// Attach FActorComponent Type="Genesis.Script" with Properties.Module. Every
// component owns an isolated runtime/module instance in the project's language.
// Modules export OnBeginPlay, OnTick, OnEvent and OnEndPlay. Callers supply cooked
// module bytes and a host with the project's capabilities; no editor dependency.
class FScriptWorldSubsystem
{
  public:
	using FModuleLoader = std::function<bool(const FString&, TArray<uint8>&, FString&)>;
	FScriptWorldSubsystem(FScriptRuntimeRequest, std::weak_ptr<FWorld>, FScriptRuntimeRegistry,
						  std::shared_ptr<IScriptHost>, FModuleLoader);
	~FScriptWorldSubsystem();
	FScriptWorldSubsystem(const FScriptWorldSubsystem&) = delete;
	FScriptWorldSubsystem& operator=(const FScriptWorldSubsystem&) = delete;
	bool BeginPlay(FString& Error);
	bool Tick(float DeltaSeconds, FString& Error);
	bool SendEvent(const FString& Name, const FJsonValue& Payload, FString& Error);
	bool EndPlay(FString& Error);
	int32 GetInstanceCount() const;

  private:
	struct FInstance;
	bool Check(FString& Error) const;
	bool Synchronize(FString& Error);
	bool Call(FInstance&, const char*, FJsonValue, FString&);
	FScriptRuntimeRequest Request;
	std::weak_ptr<FWorld> World;
	FScriptRuntimeRegistry Providers;
	std::shared_ptr<IScriptHost> Host;
	FModuleLoader Load;
	TArray<std::unique_ptr<FInstance>> Instances;
	std::thread::id Owner;
	bool bRunning = false, bBusy = false;
};
