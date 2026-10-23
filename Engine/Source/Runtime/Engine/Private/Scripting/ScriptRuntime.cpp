// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/ScriptRuntime.h"
#include "Scripting/LuaScriptRuntime.h"
#include "Scripting/PythonScriptRuntime.h"
#include "Scripting/DotNetScriptRuntime.h"
#include <algorithm>
#include <exception>
bool FScriptRuntimeRegistry::Register(FScriptProvider P, FString& Error)
{
	if (P.Language.IsEmpty() || !P.Create)
	{
		Error = "Script provider requires a language and factory.";
		return false;
	}
	for (const auto& Existing : Providers)
		if (Existing.Language == P.Language)
		{
			Error = "Script provider already registered: " + P.Language;
			return false;
		}
	Providers.push_back(std::move(P));
	Error.Empty();
	return true;
}
bool FScriptRuntimeRegistry::IsAvailable(const FScriptRuntimeRequest& R) const
{
	for (const auto& P : Providers)
		if (P.Language == R.Language)
		{
			const auto& Targets = R.Purpose == EScriptPurpose::Gameplay ? P.GameplayTargets : P.EditorTargets;
			return std::find(Targets.begin(), Targets.end(), R.Target) != Targets.end();
		}
	return false;
}
std::unique_ptr<IScriptRuntime> FScriptRuntimeRegistry::Create(const FScriptRuntimeRequest& R,
															   std::shared_ptr<IScriptHost> Host,
															   FString& Error) const
{
	if (!Host)
	{
		Error = "Script runtime requires a host.";
		return {};
	}
	if (!IsAvailable(R))
	{
		Error = FString::Format("Script provider {} is not installed or validated for {} ({}).", R.Language,
								R.Target, R.Purpose == EScriptPurpose::Gameplay ? "gameplay" : "editor");
		return {};
	}
	try
	{
		for (const auto& P : Providers)
			if (P.Language == R.Language)
			{
				Error.Empty();
				auto Runtime = P.Create(R, std::move(Host), Error);
				if (!Runtime && Error.IsEmpty())
					Error = "Script provider failed to create a runtime.";
				return Runtime;
			}
	}
	catch (const std::exception& E)
	{
		Error = FString::Format("Script provider {} failed: {}", R.Language, E.what());
	}
	catch (...)
	{
		Error = "Script provider failed with an unknown exception.";
	}
	return {};
}

bool RegisterBuiltinScriptProviders(FScriptRuntimeRegistry& Registry, FString& Error)
{
	auto Candidate = Registry;
	if (!RegisterLuaScriptProvider(Candidate, Error) || !RegisterPythonScriptProvider(Candidate, Error) ||
		!RegisterDotNetScriptProvider(Candidate, Error))
		return false;
	Registry = std::move(Candidate);
	Error.Empty();
	return true;
}
