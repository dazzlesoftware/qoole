// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/WorldScriptBindings.h"
#include "World/World.h"
#include "Logging/Log.h"
#include <stdexcept>
DEFINE_LOG_CATEGORY(LogScript, Log);
namespace
{
using J = FJsonValue;
J MatrixJson(const Matrix44& M)
{
	auto Result = J::MakeArray();
	for (int R = 0; R < 4; ++R)
		for (int C = 0; C < 4; ++C)
			Result.Push(M.GetMatrix(R, C));
	return Result;
}
void Require(bool Valid, const char* Message)
{
	if (!Valid)
		throw std::runtime_error(Message);
}
std::shared_ptr<FWorld> GetWorld(const std::weak_ptr<FWorld>& Weak)
{
	auto World = Weak.lock();
	Require(World != nullptr, "Script world has expired.");
	return World;
}
const FActor& Actor(const FWorld& World, const FString& Id)
{
	auto* Found = World.FindActor(Id);
	Require(Found != nullptr, "Actor is missing.");
	return *Found;
}
bool Replace(FWorld& World, const J& Value, FString& Error)
{
	auto Candidate = FWorld::FromJson(Value, Error);
	if (!Candidate)
		return false;
	World = std::move(*Candidate);
	Error.Empty();
	return true;
}
template <class F> bool EditActor(FWorld& World, const FString& Id, F&& Edit, FString& Error)
{
	auto Value = World.ToJson();
	auto& Actors = Value["Actors"];
	for (int I = 0; I < Actors.Num(); ++I)
		if (Actors[I].Get("Id").AsString() == Id)
		{
			Edit(Actors[I]);
			return Replace(World, Value, Error);
		}
	Error = "Actor is missing.";
	return false;
}
} // namespace
bool RegisterCoreScriptBindings(FScriptBindingRegistry& Registry, FString& Error,
								std::function<void(const FString&)> Log)
{
	TArray<FScriptBinding> Batch;
	Batch.push_back({"API.Describe",
					 "API.Read",
					 false,
					 {},
					 EJsonType::Object,
					 [&Registry](const J&, J& Result, FString&) {
						 Result = Registry.Describe();
						 return true;
					 }});
	Batch.push_back({"Log.Write",
					 "Log.Write",
					 false,
					 {{"Message", EJsonType::String, true}},
					 EJsonType::Null,
					 [Log = std::move(Log)](const J& A, J& R, FString&) {
						 const auto Message = A.Get("Message").AsString();
						 if (Log)
							 Log(Message);
						 else
							 UE_LOG(LogScript, Display, "{}", Message);
						 R = {};
						 return true;
					 }});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
bool RegisterWorldScriptBindings(FScriptBindingRegistry& Registry, std::weak_ptr<FWorld> Weak, FString& Error)
{
	TArray<FScriptBinding> Batch;
	const auto Add = [&](const char* Name, bool Write, TArray<FScriptArgument> Arguments, EJsonType Type,
						 auto Function) {
		Batch.push_back({Name, Write ? "World.Write" : "World.Read", false, std::move(Arguments), Type,
						 [Weak, Function, Write](const J& A, J& R, FString& E) {
							 auto World = GetWorld(Weak);
							 if (!Write)
								 return Function(*World, A, R, E);
							 auto Candidate = *World;
							 if (!Function(Candidate, A, R, E) || !ValidateScriptValue(R, E))
								 return false;
							 *World = std::move(Candidate);
							 return true;
						 }});
	};
	Add("World.Serialize", false, {}, EJsonType::Object, [](FWorld& W, const J&, J& R, FString&) {
		R = W.ToJson();
		return true;
	});
	Add("World.Replace", true, {{"World", EJsonType::Object}}, EJsonType::Bool,
		[](FWorld& W, const J& A, J& R, FString& E) {
			if (!Replace(W, A.Get("World"), E))
				return false;
			R = true;
			return true;
		});
	Add("World.GetActors", false, {}, EJsonType::Array, [](FWorld& W, const J&, J& R, FString&) {
		R = J::MakeArray();
		for (const auto& A : W.GetActors())
			R.Push(A.ToJson());
		return true;
	});
	Add("World.FindActor", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
		[](FWorld& W, const J& A, J& R, FString&) {
			R = Actor(W, A.Get("ActorId").AsString()).ToJson();
			return true;
		});
	Add("World.FindComponent", false, {{"ActorId", EJsonType::String}, {"ComponentId", EJsonType::String}},
		EJsonType::Object, [](FWorld& W, const J& A, J& R, FString&) {
			const auto& Owner = Actor(W, A.Get("ActorId").AsString());
			auto* C = W.FindComponent(Owner.Id, A.Get("ComponentId").AsString());
			Require(C != nullptr, "Component is missing.");
			R = C->ToJson();
			if (C->Id == Owner.Root.Id)
				R["Transform"] = Owner.Root.Transform.ToJson();
			return true;
		});
	Add("World.Resolve", false, {{"ActorId", EJsonType::String}, {"ComponentId", EJsonType::String, false}},
		EJsonType::Object, [](FWorld& W, const J& A, J& R, FString& E) {
			const FActor* Owner = nullptr;
			const FActorComponent* C = nullptr;
			if (!W.Resolve({A.Get("ActorId").AsString(), A.Get("ComponentId").AsString()}, Owner, C, E))
				return false;
			R = J::MakeObject();
			R["Actor"] = Owner->ToJson();
			R["Component"] = C ? C->ToJson() : J();
			return true;
		});
	Add("World.SpawnActor", true,
		{{"Name", EJsonType::String},
		 {"Transform", EJsonType::Object, false},
		 {"ParentId", EJsonType::String, false}},
		EJsonType::Object, [](FWorld& W, const J& A, J& R, FString& E) {
			auto New = FActor::New(A.Get("Name").AsString());
			if (A.Contains("Transform"))
			{
				auto T = FTransform3D::FromJson(A.Get("Transform"), E);
				if (!T)
					return false;
				New.Root.Transform = *T;
			}
			New.ParentId = A.Get("ParentId").AsString();
			R = New.ToJson();
			return W.AddActor(std::move(New), E);
		});
	Add("World.AddActor", true, {{"Actor", EJsonType::Object}}, EJsonType::String,
		[](FWorld& W, const J& A, J& R, FString& E) {
			auto Value = W.ToJson();
			Value["Actors"].Push(A.Get("Actor"));
			if (!Replace(W, Value, E))
				return false;
			R = A.Get("Actor").Get("Id");
			return true;
		});
	Add("World.AddComponent", true, {{"ActorId", EJsonType::String}, {"Component", EJsonType::Object}},
		EJsonType::String, [](FWorld& W, const J& A, J& R, FString& E) {
			auto C = A.Get("Component");
			if (!C.Contains("Id"))
				C["Id"] = FAssetId::New();
			if (!C.Contains("Properties"))
				C["Properties"] = J::MakeObject();
			if (!C.Contains("References"))
				C["References"] = J::MakeArray();
			if (!C.Contains("Assets"))
				C["Assets"] = J::MakeArray();
			if (!EditActor(W, A.Get("ActorId").AsString(), [&](J& Owner) { Owner["Components"].Push(C); }, E))
				return false;
			R = C.Get("Id");
			return true;
		});
	Add("World.SetComponent", true, {{"ActorId", EJsonType::String}, {"Component", EJsonType::Object}},
		EJsonType::Bool, [](FWorld& W, const J& A, J& R, FString& E) {
			const auto C = A.Get("Component");
			const auto Id = C.Get("Id").AsString();
			const bool OK = EditActor(
				W, A.Get("ActorId").AsString(),
				[&](J& Owner) {
					if (Owner.Get("Root").Get("Id").AsString() == Id)
					{
						Owner["Root"] = C;
						return;
					}
					auto& Cs = Owner["Components"];
					for (int I = 0; I < Cs.Num(); ++I)
						if (Cs[I].Get("Id").AsString() == Id)
						{
							Cs[I] = C;
							return;
						}
					throw std::runtime_error("Component is missing.");
				},
				E);
			R = true;
			return OK;
		});
	Add("World.RemoveComponent", true, {{"ActorId", EJsonType::String}, {"ComponentId", EJsonType::String}},
		EJsonType::Bool, [](FWorld& W, const J& A, J& R, FString& E) {
			const auto Id = A.Get("ComponentId").AsString();
			const bool OK = EditActor(
				W, A.Get("ActorId").AsString(),
				[&](J& Owner) {
					Require(Owner.Get("Root").Get("Id").AsString() != Id,
							"Cannot remove an actor root component.");
					auto Cs = J::MakeArray();
					bool Found = false;
					for (const auto& C : Owner.Get("Components").GetItems())
						if (C.Get("Id").AsString() == Id)
							Found = true;
						else
							Cs.Push(C);
					Require(Found, "Component is missing.");
					Owner["Components"] = Cs;
				},
				E);
			R = true;
			return OK;
		});
	Add("World.SetActorName", true, {{"ActorId", EJsonType::String}, {"Name", EJsonType::String}},
		EJsonType::Bool, [](FWorld& W, const J& A, J& R, FString& E) {
			const bool OK = EditActor(
				W, A.Get("ActorId").AsString(), [&](J& Owner) { Owner["Name"] = A.Get("Name"); }, E);
			R = true;
			return OK;
		});
	Add("World.GetLocalTransform", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
		[](FWorld& W, const J& A, J& R, FString&) {
			R = Actor(W, A.Get("ActorId").AsString()).Root.Transform.ToJson();
			return true;
		});
	Add("World.SetTransform", true, {{"ActorId", EJsonType::String}, {"Transform", EJsonType::Object}},
		EJsonType::Bool, [](FWorld& W, const J& A, J& R, FString& E) {
			auto T = FTransform3D::FromJson(A.Get("Transform"), E);
			if (!T || !W.SetTransform(A.Get("ActorId").AsString(), *T, E))
				return false;
			R = true;
			return true;
		});
	Add("World.SetParent", true, {{"ActorId", EJsonType::String}, {"ParentId", EJsonType::String}},
		EJsonType::Bool, [](FWorld& W, const J& A, J& R, FString& E) {
			if (!W.SetParent(A.Get("ActorId").AsString(), A.Get("ParentId").AsString(), E))
				return false;
			R = true;
			return true;
		});
	Add("World.GetWorldTransform", false, {{"ActorId", EJsonType::String}}, EJsonType::Array,
		[](FWorld& W, const J& A, J& R, FString& E) {
			Matrix44 M;
			if (!W.GetWorldTransform(A.Get("ActorId").AsString(), M, E))
				return false;
			R = MatrixJson(M);
			return true;
		});
	Add("World.DestroyActor", true, {{"ActorId", EJsonType::String}}, EJsonType::Bool,
		[](FWorld& W, const J& A, J& R, FString& E) {
			if (!W.RemoveActor(A.Get("ActorId").AsString(), E))
				return false;
			R = true;
			return true;
		});
	Add("World.DuplicateActor", true, {{"ActorId", EJsonType::String}}, EJsonType::String,
		[](FWorld& W, const J& A, J& R, FString& E) {
			FString Id;
			if (!W.DuplicateActor(A.Get("ActorId").AsString(), Id, E))
				return false;
			R = Id;
			return true;
		});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
