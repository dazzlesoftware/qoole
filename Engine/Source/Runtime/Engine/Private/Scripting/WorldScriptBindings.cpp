// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/WorldScriptBindings.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "World/World.h"
#include "Logging/Log.h"
#include "Misc/ValidationContext.h"
DEFINE_LOG_CATEGORY(LogScript, Log);
namespace
{
	using J = FJsonValue;
	J MatrixJson(const Matrix44& M)
	{
		FJsonValue Result = J::MakeArray();
		for (int R = 0; R < 4; ++R)
			for (int C = 0; C < 4; ++C)
				Result.Push(M.GetMatrix(R, C));
		return Result;
	}
	std::shared_ptr<FWorld> GetWorld(FValidationContext& Validation, const std::weak_ptr<FWorld>& Weak)
	{
		TSharedPtr<FWorld> World = Weak.lock();
		if (!Validation.Check(World != nullptr, GENESIS_TEXT("Script world has expired.")))
		{
			return {};
		}
		return World;
	}
	const FActor* Actor(FValidationContext& Validation, const FWorld& World, const FString& Id)
	{
		const FActor* Found = World.FindActor(Id);
		if (!Validation.Check(Found != nullptr, GENESIS_TEXT("Actor is missing.")))
		{
			return {};
		}
		return Found;
	}
	bool Replace(FWorld& World, const J& Value, FString& Error)
	{
		TOptional<FWorld> Candidate = FWorld::FromJson(Value, Error);
		if (!Candidate)
			return false;
		World = std::move(*Candidate);
		Error.Empty();
		return true;
	}
	template <class F>
	bool EditActor(FValidationContext& Validation, FWorld& World, const FString& Id, F&& Edit, FString& Error)
	{
		FJsonValue Value = World.ToJson();
		FJsonValue& Actors = Value["Actors"];
		for (int I = 0; I < Actors.Num(); ++I)
			if (Actors[I].Get("Id").AsString() == Id)
			{
				Edit(Actors[I]);
				if (!Validation.IsValid())
				{
					return false;
				}
				return Replace(World, Value, Error);
			}
		Error = GENESIS_TEXT("Actor is missing.");
		return false;
	}
} // namespace
bool RegisterCoreScriptBindings(
    FScriptBindingRegistry& Registry, FString& Error, std::function<void(const FString&)> Log)
{
	TArray<FScriptBinding> Batch;
	Batch.push_back(
	    {"API.Describe", "API.Read", false, {}, EJsonType::Object, [&Registry](const J&, J& Result, FString&) -> bool
	{
		Result = Registry.Describe();
		return true;
	}});
	Batch.push_back({"Log.Write", "Log.Write", false, {{"Message", EJsonType::String, true}}, EJsonType::Null,
	    [Log = std::move(Log)](const J& A, J& R, FString&) -> bool
	{
		const FString Message = A.Get("Message").AsString();
		if (Log)
			Log(Message);
		else
			GENESIS_LOG(LogScript, Display, "{}", Message);
		R = {};
		return true;
	}});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
using FBindingCallback = TFunction<bool(FWorld&, const J&, J&, FString&)>;
static void AddBinding(TArray<FScriptBinding>& Batch, const std::weak_ptr<FWorld>& Weak, const char* Name, bool Write,
    TArray<FScriptArgument> Arguments, EJsonType Type, const FBindingCallback& Function)
{
	Batch.push_back({Name, Write ? "World.Write" : "World.Read", false, std::move(Arguments), Type,
	    [Weak, Function, Write](const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		std::shared_ptr<FWorld> World = GetWorld(Validation, Weak);
		if (!Validation.IsValid())
		{
			return false;
		}
		if (!Write)
			return Function(*World, A, R, E);
		FWorld Candidate = *World;
		if (!Function(Candidate, A, R, E) || !ValidateScriptValue(R, E))
			return false;
		*World = std::move(Candidate);
		return true;
	}});
}
bool RegisterWorldScriptBindings(FScriptBindingRegistry& Registry, std::weak_ptr<FWorld> Weak, FString& Error)
{
	TArray<FScriptBinding> Batch;
	AddBinding(Batch, Weak, "World.Serialize", false, {}, EJsonType::Object,
	    [](FWorld& W, const J&, J& R, FString&) -> bool
	{
		R = W.ToJson();
		return true;
	});
	AddBinding(Batch, Weak, "World.Replace", true, {{"World", EJsonType::Object}}, EJsonType::Bool,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		if (!Replace(W, A.Get("World"), E))
			return false;
		R = true;
		return true;
	});
	AddBinding(Batch, Weak, "World.GetActors", false, {}, EJsonType::Array,
	    [](FWorld& W, const J&, J& R, FString&) -> bool
	{
		R = J::MakeArray();
		for (const FActor& A : W.GetActors())
			R.Push(A.ToJson());
		return true;
	});
	AddBinding(Batch, Weak, "World.FindActor", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const FActor* ActorObject = Actor(Validation, W, A.Get("ActorId").AsString());
		if (!Validation.IsValid())
		{
			return false;
		}
		R = (*ActorObject).ToJson();
		return true;
	});
	AddBinding(Batch, Weak, "World.FindComponent", false,
	    {{"ActorId", EJsonType::String}, {"ComponentId", EJsonType::String}}, EJsonType::Object,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const FActor* ActorObject = Actor(Validation, W, A.Get("ActorId").AsString());
		if (!Validation.IsValid())
		{
			return false;
		}
		const FActor& Owner = (*ActorObject);
		const FActorComponent* C = W.FindComponent(Owner.Id, A.Get("ComponentId").AsString());
		if (!Validation.Check(C != nullptr, GENESIS_TEXT("Component is missing.")))
		{
			return false;
		}

		R = C->ToJson();
		if (C->Id == Owner.Root.Id)
			R["Transform"] = Owner.Root.Transform.ToJson();
		return true;
	});
	AddBinding(Batch, Weak, "World.Resolve", false,
	    {{"ActorId", EJsonType::String}, {"ComponentId", EJsonType::String, false}}, EJsonType::Object,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		const FActor* Owner = nullptr;
		const FActorComponent* C = nullptr;
		if (!W.Resolve({A.Get("ActorId").AsString(), A.Get("ComponentId").AsString()}, Owner, C, E))
			return false;
		R = J::MakeObject();
		R["Actor"] = Owner->ToJson();
		R["Component"] = C ? C->ToJson() : J();
		return true;
	});
	AddBinding(Batch, Weak, "World.SpawnActor", true,
	    {{"Name", EJsonType::String}, {"Transform", EJsonType::Object, false}, {"ParentId", EJsonType::String, false}},
	    EJsonType::Object, [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FActor New = FActor::New(A.Get("Name").AsString());
		if (A.Contains("Transform"))
		{
			TOptional<FTransform3D> T = FTransform3D::FromJson(A.Get("Transform"), E);
			if (!T)
				return false;
			New.Root.Transform = *T;
		}
		New.ParentId = A.Get("ParentId").AsString();
		R = New.ToJson();
		return W.AddActor(std::move(New), E);
	});
	AddBinding(Batch, Weak, "World.AddActor", true, {{"Actor", EJsonType::Object}}, EJsonType::String,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FJsonValue Value = W.ToJson();
		Value["Actors"].Push(A.Get("Actor"));
		if (!Replace(W, Value, E))
			return false;
		R = A.Get("Actor").Get("Id");
		return true;
	});
	AddBinding(Batch, Weak, "World.AddComponent", true,
	    {{"ActorId", EJsonType::String}, {"Component", EJsonType::Object}}, EJsonType::String,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		FJsonValue C = A.Get("Component");
		if (!C.Contains("Id"))
			C["Id"] = FAssetId::New();
		if (!C.Contains("Properties"))
			C["Properties"] = J::MakeObject();
		if (!C.Contains("References"))
			C["References"] = J::MakeArray();
		if (!C.Contains("Assets"))
			C["Assets"] = J::MakeArray();
		if (!EditActor(Validation, W, A.Get("ActorId").AsString(), [&](J& Owner) { Owner["Components"].Push(C); }, E))
			return false;
		if (!Validation.IsValid())
		{
			return false;
		}
		R = C.Get("Id");
		return true;
	});
	AddBinding(Batch, Weak, "World.SetComponent", true,
	    {{"ActorId", EJsonType::String}, {"Component", EJsonType::Object}}, EJsonType::Bool,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const FJsonValue C = A.Get("Component");
		const FString Id = C.Get("Id").AsString();
		const bool OK = EditActor(Validation, W, A.Get("ActorId").AsString(), [&](J& Owner)
		{
			if (Owner.Get("Root").Get("Id").AsString() == Id)
			{
				Owner["Root"] = C;
				return;
			}
			FJsonValue& Cs = Owner["Components"];
			for (int I = 0; I < Cs.Num(); ++I)
				if (Cs[I].Get("Id").AsString() == Id)
				{
					Cs[I] = C;
					return;
				}
			{
				Validation.Fail(GENESIS_TEXT("Component is missing."));
				return;
			}
		}, E);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = true;
		return OK;
	});
	AddBinding(Batch, Weak, "World.RemoveComponent", true,
	    {{"ActorId", EJsonType::String}, {"ComponentId", EJsonType::String}}, EJsonType::Bool,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const FString Id = A.Get("ComponentId").AsString();
		const bool OK = EditActor(Validation, W, A.Get("ActorId").AsString(), [&](J& Owner)
		{
			if (!Validation.Check(Owner.Get("Root").Get("Id").AsString() != Id,
			        GENESIS_TEXT("Cannot remove an actor root component.")))
			{
				return;
			}

			FJsonValue Cs = J::MakeArray();
			bool Found = false;
			for (const FJsonValue& C : Owner.Get("Components").GetItems())
				if (C.Get("Id").AsString() == Id)
					Found = true;
				else
					Cs.Push(C);
			if (!Validation.Check(Found, GENESIS_TEXT("Component is missing.")))
			{
				return;
			}

			Owner["Components"] = Cs;
		}, E);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = true;
		return OK;
	});
	AddBinding(Batch, Weak, "World.SetActorName", true, {{"ActorId", EJsonType::String}, {"Name", EJsonType::String}},
	    EJsonType::Bool, [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const bool OK =
		    EditActor(Validation, W, A.Get("ActorId").AsString(), [&](J& Owner) { Owner["Name"] = A.Get("Name"); }, E);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = true;
		return OK;
	});
	AddBinding(Batch, Weak, "World.GetLocalTransform", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const FActor* ActorObject = Actor(Validation, W, A.Get("ActorId").AsString());
		if (!Validation.IsValid())
		{
			return false;
		}
		R = (*ActorObject).Root.Transform.ToJson();
		return true;
	});
	AddBinding(Batch, Weak, "World.SetTransform", true,
	    {{"ActorId", EJsonType::String}, {"Transform", EJsonType::Object}}, EJsonType::Bool,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		TOptional<FTransform3D> T = FTransform3D::FromJson(A.Get("Transform"), E);
		if (!T || !W.SetTransform(A.Get("ActorId").AsString(), *T, E))
			return false;
		R = true;
		return true;
	});
	AddBinding(Batch, Weak, "World.SetParent", true, {{"ActorId", EJsonType::String}, {"ParentId", EJsonType::String}},
	    EJsonType::Bool, [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		if (!W.SetParent(A.Get("ActorId").AsString(), A.Get("ParentId").AsString(), E))
			return false;
		R = true;
		return true;
	});
	AddBinding(Batch, Weak, "World.GetWorldTransform", false, {{"ActorId", EJsonType::String}}, EJsonType::Array,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		Matrix44 M;
		if (!W.GetWorldTransform(A.Get("ActorId").AsString(), M, E))
			return false;
		R = MatrixJson(M);
		return true;
	});
	AddBinding(Batch, Weak, "World.DestroyActor", true, {{"ActorId", EJsonType::String}}, EJsonType::Bool,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		if (!W.RemoveActor(A.Get("ActorId").AsString(), E))
			return false;
		R = true;
		return true;
	});
	AddBinding(Batch, Weak, "World.DuplicateActor", true, {{"ActorId", EJsonType::String}}, EJsonType::String,
	    [](FWorld& W, const J& A, J& R, FString& E) -> bool
	{
		FString Id;
		if (!W.DuplicateActor(A.Get("ActorId").AsString(), Id, E))
			return false;
		R = Id;
		return true;
	});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
