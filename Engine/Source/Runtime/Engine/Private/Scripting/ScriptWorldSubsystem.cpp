// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/ScriptWorldSubsystem.h"
#include <algorithm>
#include <cmath>
#include <exception>
namespace
{
using J = FJsonValue;
struct FBusy
{
	bool& Value;
	explicit FBusy(bool& B) : Value(B)
	{
		Value = true;
	}
	~FBusy()
	{
		Value = false;
	}
};
void Append(FString& Out, const FString& E)
{
	if (!Out.IsEmpty())
		Out += "\n";
	Out += E;
}
} // namespace
struct FScriptWorldSubsystem::FInstance
{
	FString Actor, Component, Module;
	J Properties;
	std::unique_ptr<IScriptRuntime> Runtime;
	bool bFaulted = false;
};
FScriptWorldSubsystem::FScriptWorldSubsystem(FScriptRuntimeRequest R, std::weak_ptr<FWorld> W,
											 FScriptRuntimeRegistry P, std::shared_ptr<IScriptHost> H,
											 FModuleLoader L)
	: Request(std::move(R)), World(std::move(W)), Providers(std::move(P)), Host(std::move(H)),
	  Load(std::move(L)), Owner(std::this_thread::get_id())
{
}
FScriptWorldSubsystem::~FScriptWorldSubsystem()
{
	FString Ignored;
	EndPlay(Ignored);
}
bool FScriptWorldSubsystem::Check(FString& E) const
{
	if (std::this_thread::get_id() != Owner)
	{
		E = "Script world lifecycle must run on its owning thread.";
		return false;
	}
	if (bBusy)
	{
		E = "Script world lifecycle cannot reenter itself.";
		return false;
	}
	return true;
}
int32 FScriptWorldSubsystem::GetInstanceCount() const
{
	return static_cast<int32>(Instances.size());
}
bool FScriptWorldSubsystem::Call(FInstance& I, const char* Function, J A, FString& E)
{
	A["ActorId"] = I.Actor;
	A["ComponentId"] = I.Component;
	A["Properties"] = I.Properties;
	J Result;
	try
	{
		if (I.Runtime->Call("main", Function, A, Result, E))
			return true;
	}
	catch (const std::exception& Failure)
	{
		E = Failure.what();
	}
	catch (...)
	{
		E = "Unknown script lifecycle exception.";
	}
	I.bFaulted = true;
	E = FString::Format("Script component {} / {}: {}", I.Actor, I.Component, E);
	return false;
}
bool FScriptWorldSubsystem::Synchronize(FString& E)
{
	auto W = World.lock();
	if (!W)
	{
		E = "Script world has expired.";
		return false;
	}
	struct FDescriptor
	{
		FString Actor, Component, Module;
		J Properties;
	};
	TArray<FDescriptor> Desired;
	for (const auto& A : W->GetActors())
		for (const auto& C : A.Components)
			if (C.Type == "Genesis.Script")
			{
				const auto& M = C.Properties.Get("Module");
				if (M.GetType() != EJsonType::String || M.AsString().IsEmpty() || M.AsString().Len() > 128)
				{
					E = "Script component requires a valid Module name.";
					return false;
				}
				Desired.push_back({A.Id, C.Id, M.AsString(), C.Properties});
				if (Desired.size() > 256)
				{
					E = "A script world supports at most 256 live script components.";
					return false;
				}
			}
	FString Failures;
	// Snapshot descriptors and resolve identities again after every callback.
	for (auto It = Instances.begin(); It != Instances.end();)
	{
		auto& I = **It;
		const auto Found = std::find_if(Desired.begin(), Desired.end(), [&](const auto& D) {
			return D.Actor == I.Actor && D.Component == I.Component && D.Module == I.Module;
		});
		if (Found == Desired.end())
		{
			auto A = J::MakeObject();
			A["Reason"] = "Removed";
			FString Failure;
			if (I.Runtime && !Call(I, "OnEndPlay", A, Failure))
				Append(Failures, Failure);
			It = Instances.erase(It);
		}
		else
		{
			I.Properties = Found->Properties;
			++It;
		}
	}
	for (const auto& D : Desired)
	{
		if (std::any_of(Instances.begin(), Instances.end(),
						[&](const auto& I) { return I->Actor == D.Actor && I->Component == D.Component; }))
			continue;
		const auto* C = W->FindComponent(D.Actor, D.Component);
		if (!C || C->Type != "Genesis.Script" || C->Properties.Get("Module").AsString() != D.Module)
			continue;
		auto I = std::make_unique<FInstance>();
		I->Actor = D.Actor;
		I->Component = D.Component;
		I->Module = D.Module;
		I->Properties = D.Properties;
		FString Failure;
		TArray<uint8> Payload;
		I->Runtime = Providers.Create(Request, Host, Failure);
		bool Loaded = false;
		try
		{
			Loaded = I->Runtime && Load(D.Module, Payload, Failure) &&
					 I->Runtime->LoadModule("main", Payload, Failure);
		}
		catch (const std::exception& Exception)
		{
			Failure = Exception.what();
		}
		catch (...)
		{
			Failure = "Unknown script module loader exception.";
		}
		if (!Loaded)
		{
			// Keep a fault marker so a broken module is not loaded every frame.
			I->bFaulted = true;
			I->Runtime.reset();
			Append(Failures, FString::Format("Cannot start script component {}: {}", D.Component, Failure));
		}
		else if (!Call(*I, "OnBeginPlay", J::MakeObject(), Failure))
			Append(Failures, Failure);
		Instances.push_back(std::move(I));
	}
	E = std::move(Failures);
	return E.IsEmpty();
}
bool FScriptWorldSubsystem::BeginPlay(FString& E)
{
	E.Empty();
	if (!Check(E))
		return false;
	if (bRunning)
	{
		E = "Script world has already begun play.";
		return false;
	}
	if (Request.Purpose != EScriptPurpose::Gameplay || !Host || !Load || !Providers.IsAvailable(Request))
	{
		E = "Script world needs a gameplay provider, module loader and host.";
		return false;
	}
	FBusy Busy(bBusy);
	bRunning = true;
	return Synchronize(E);
}
bool FScriptWorldSubsystem::Tick(float DeltaSeconds, FString& E)
{
	E.Empty();
	if (!Check(E))
		return false;
	if (!bRunning)
	{
		E = "Script world has not begun play.";
		return false;
	}
	if (!std::isfinite(DeltaSeconds) || DeltaSeconds < 0 || DeltaSeconds > 10)
	{
		E = "Script tick delta must be finite and between zero and ten seconds.";
		return false;
	}
	FBusy Busy(bBusy);
	FString Failures, Failure;
	if (!Synchronize(Failure))
		Append(Failures, Failure);
	auto W = World.lock();
	if (!W)
	{
		E = std::move(Failures);
		return false;
	}
	for (auto& I : Instances)
	{
		if (I->bFaulted || !W->FindComponent(I->Actor, I->Component))
			continue;
		auto A = J::MakeObject();
		A["DeltaSeconds"] = DeltaSeconds;
		if (!Call(*I, "OnTick", A, Failure))
			Append(Failures, Failure);
	}
	if (!Synchronize(Failure))
		Append(Failures, Failure);
	E = std::move(Failures);
	return E.IsEmpty();
}
bool FScriptWorldSubsystem::SendEvent(const FString& Name, const J& Payload, FString& E)
{
	E.Empty();
	if (!Check(E))
		return false;
	if (!bRunning)
	{
		E = "Script world has not begun play.";
		return false;
	}
	if (Name.IsEmpty() || Name.Len() > 128 || !ValidateScriptValue(Payload, E))
	{
		if (E.IsEmpty())
			E = "Script event needs a bounded name and payload.";
		return false;
	}
	FBusy Busy(bBusy);
	auto W = World.lock();
	if (!W)
	{
		E = "Script world has expired.";
		return false;
	}
	FString Failures, Failure;
	for (auto& I : Instances)
	{
		if (I->bFaulted || !W->FindComponent(I->Actor, I->Component))
			continue;
		auto A = J::MakeObject();
		A["Name"] = Name;
		A["Payload"] = Payload;
		if (!Call(*I, "OnEvent", A, Failure))
			Append(Failures, Failure);
	}
	if (!Synchronize(Failure))
		Append(Failures, Failure);
	E = std::move(Failures);
	return E.IsEmpty();
}
bool FScriptWorldSubsystem::EndPlay(FString& E)
{
	E.Empty();
	if (!Check(E))
		return false;
	if (!bRunning)
		return true;
	FBusy Busy(bBusy);
	FString Failures, Failure;
	for (auto& I : Instances)
		if (I->Runtime)
		{
			auto A = J::MakeObject();
			A["Reason"] = "Stopped";
			if (!Call(*I, "OnEndPlay", A, Failure))
				Append(Failures, Failure);
		}
	Instances.clear();
	bRunning = false;
	E = std::move(Failures);
	return E.IsEmpty();
}
