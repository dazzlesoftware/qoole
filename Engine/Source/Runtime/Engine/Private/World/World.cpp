// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/World.h"
#include <cmath>
#include <cfloat>
#include <stdexcept>

namespace
{
	using J = FJsonValue;
	void Require(bool Value, const char* Text) { if (!Value) throw std::runtime_error(Text); }
	bool ValidVector(const Vector3d& V, float Limit = 1.0e9f)
	{
		for (float N : { V.GetX(), V.GetY(), V.GetZ() }) if (!std::isfinite(N) || std::abs(N) > Limit) return false;
		return true;
	}
	bool ValidText(const FString& Value)
	{ return Value.Len() <= 65536 && Value.View().find('\0') == std::string_view::npos; }
	J Vector(const Vector3d& V) { auto Out = J::MakeArray(); Out.Push(V.GetX()).Push(V.GetY()).Push(V.GetZ()); return Out; }
	Vector3d ReadVector(const J& In)
	{
		Require(In.IsArray() && In.Num() == 3, "Expected a 3D vector.");
		float Values[3];
		for (int I = 0; I < 3; ++I)
		{
			Require(In[I].GetType() == EJsonType::Number && std::isfinite(In[I].AsNumber()) && std::abs(In[I].AsNumber()) <= 1.0e9, "Invalid vector coordinate.");
			Values[I] = In[I].AsFloat();
		}
		return Vector3d(Values[0], Values[1], Values[2]);
	}
	FString Text(const J& In)
	{
		Require(In.GetType() == EJsonType::String && In.AsString().Len() <= 65536 && In.AsString().View().find('\0') == std::string_view::npos, "Invalid scene string.");
		return In.AsString();
	}
	const TArray<J>& Array(const J& In, int Limit)
	{ Require(In.IsArray() && In.Num() <= Limit, "Invalid or excessive scene array."); return In.GetItems(); }
	J ComponentJson(const FActorComponent& C)
	{
		auto Out = J::MakeObject(); Out["Id"] = C.Id; Out["Type"] = C.Type; Out["Properties"] = C.Properties;
		Out["References"] = J::MakeArray(); Out["Assets"] = J::MakeArray();
		for (const auto& Ref : C.References)
		{
			auto R = J::MakeObject(); R["ActorId"] = Ref.ActorId; R["ComponentId"] = Ref.ComponentId; Out["References"].Push(R);
		}
		for (const auto& Ref : C.Assets) Out["Assets"].Push(Ref.ToJson());
		return Out;
	}
	FActorComponent ReadComponent(const J& In)
	{
		Require(In.IsObject(), "Expected a component object.");
		FActorComponent C; C.Id = Text(In.Get("Id")); C.Type = Text(In.Get("Type")); C.Properties = In.Get("Properties");
		Require(C.Properties.IsObject(), "Component properties must be an object.");
		for (const auto& R : Array(In.Get("References"), 100000))
			C.References.push_back({ Text(R.Get("ActorId")), Text(R.Get("ComponentId")) });
		for (const auto& R : Array(In.Get("Assets"), 100000))
		{
			FString Error; auto Ref = FAssetReference::FromJson(R, Error); Require(Ref.has_value(), "Invalid component asset reference."); C.Assets.push_back(*Ref);
		}
		return C;
	}
	TArray<const FActorComponent*> Components(const FActor& Actor)
	{
		TArray<const FActorComponent*> Result{ &Actor.Root };
		for (const auto& C : Actor.Components) Result.push_back(&C);
		return Result;
	}
}

bool FTransform3D::IsValid() const
{
	return ValidVector(Location) && ValidVector(Vector3d(Rotation.GetYaw(), Rotation.GetPitch(), Rotation.GetRoll())) && ValidVector(Scale, 1.0e6f) &&
		std::abs(Scale.GetX()) >= 1.0e-6f && std::abs(Scale.GetY()) >= 1.0e-6f && std::abs(Scale.GetZ()) >= 1.0e-6f;
}
Matrix44 FTransform3D::ToMatrix() const
{
	TransSpace Space; Space.SetPosition(Location); Space.SetOrientation(Rotation); Space.SetScale(Scale);
	Matrix44 Matrix; Space.CalTransSpaceMatrix(Matrix.SetIdentity()); return Matrix;
}
J FTransform3D::ToJson() const
{
	auto Out = J::MakeObject(); Out["Location"] = Vector(Location); Out["Rotation"] = Vector(Vector3d(Rotation.GetYaw(), Rotation.GetPitch(), Rotation.GetRoll())); Out["Scale"] = Vector(Scale); return Out;
}
TOptional<FTransform3D> FTransform3D::FromJson(const J& Json, FString& Error)
{
	try
	{
		Require(Json.IsObject(), "Expected a transform object."); FTransform3D T;
		T.Location = ReadVector(Json.Get("Location")); const auto R = ReadVector(Json.Get("Rotation")); T.Rotation.NewVector(R.GetX(), R.GetY(), R.GetZ()); T.Scale = ReadVector(Json.Get("Scale"));
		Require(T.IsValid(), "Invalid or singular transform."); Error.Empty(); return T;
	}
	catch (const std::exception& E) { Error = E.what(); return {}; }
}
FActorComponent FActorComponent::New(const FString& Type) { FActorComponent C; C.Id = FAssetId::New(); C.Type = Type; return C; }
FSceneComponent FSceneComponent::New() { FSceneComponent C; static_cast<FActorComponent&>(C) = FActorComponent::New("Genesis.Transform"); return C; }
FActor FActor::New(const FString& Name) { FActor A; A.Id = FAssetId::New(); A.Name = Name; A.Root = FSceneComponent::New(); return A; }
J FActorComponent::ToJson() const { return ComponentJson(*this); }
J FActor::ToJson() const
{
	auto Actor = J::MakeObject(); Actor["Id"] = Id; Actor["Name"] = Name; Actor["Parent"] = ParentId;
	Actor["Root"] = Root.ToJson(); Actor["Root"]["Transform"] = Root.Transform.ToJson(); Actor["Components"] = J::MakeArray();
	for (const auto& C : Components) Actor["Components"].Push(C.ToJson()); return Actor;
}

const FActor* FWorld::FindActor(const FString& Id) const
{
	const auto It = Index.find(Id.ToStdString()); return It == Index.end() ? nullptr : &Actors[It->second];
}
const FActorComponent* FWorld::FindComponent(const FString& ActorId, const FString& ComponentId) const
{
	const auto* A = FindActor(ActorId); if (!A) return nullptr;
	if (A->Root.Id == ComponentId) return &A->Root;
	for (const auto& C : A->Components) if (C.Id == ComponentId) return &C;
	return nullptr;
}
bool FWorld::Resolve(const FWorldReference& Ref, const FActor*& Actor, const FActorComponent*& Component, FString& Error) const
{
	Actor = nullptr; Component = nullptr; Error.Empty(); if (Ref.IsNull()) return true;
	if (!FAssetId::IsValid(Ref.ActorId) || (!Ref.ComponentId.IsEmpty() && !FAssetId::IsValid(Ref.ComponentId))) { Error = "Invalid world reference identity."; return false; }
	Actor = FindActor(Ref.ActorId);
	if (!Actor) { Error = "World reference points to a missing actor."; return false; }
	if (!Ref.ComponentId.IsEmpty())
	{
		Component = FindComponent(Ref.ActorId, Ref.ComponentId);
		if (!Component) { Actor = nullptr; Error = "World reference points to a missing or differently owned component."; return false; }
	}
	return true;
}
bool FWorld::Validate(FString& Error)
{
	Error.Empty(); Index.clear(); TSet<std::string> Ids;
	if (Actors.size() > 100000) { Error = "Scene exceeds the actor limit."; return false; }
	for (size_t I = 0; I < Actors.size(); ++I)
	{
		const auto& A = Actors[I];
		if (!ValidText(A.Name)) { Error = "Invalid actor name."; return false; }
		if (!FAssetId::IsValid(A.Id) || !Ids.insert(A.Id.ToStdString()).second) { Error = "Invalid or duplicate actor/component UUID."; return false; }
		Index[A.Id.ToStdString()] = I;
		if (!A.Root.Transform.IsValid() || A.Root.Type != "Genesis.Transform" || A.Components.size() > 1024) { Error = "Invalid actor root transform or component count."; return false; }
		for (const auto* C : Components(A))
		{
			if (!FAssetId::IsValid(C->Id) || !Ids.insert(C->Id.ToStdString()).second || C->Type.IsEmpty() || !ValidText(C->Type) || !C->Properties.IsObject() || C->References.size() > 100000 || C->Assets.size() > 100000)
			{ Error = "Invalid or duplicate actor/component UUID or component type."; return false; }
			for (const auto& Ref : C->Assets) if (!FAssetReference::FromJson(Ref.ToJson(), Error)) return false;
		}
	}
	for (const auto& A : Actors)
	{
		TSet<std::string> Chain; const FActor* Current = &A;
		while (Current)
		{
			if (!Chain.insert(Current->Id.ToStdString()).second || Chain.size() > 128) { Error = "Actor hierarchy contains a cycle or exceeds 128 levels."; return false; }
			if (Current->ParentId.IsEmpty()) break;
			Current = FindActor(Current->ParentId);
			if (!Current) { Error = "Actor parent is missing."; return false; }
		}
		for (const auto* C : Components(A)) for (const auto& Ref : C->References)
		{
			const FActor* Owner; const FActorComponent* Target;
			if (!Resolve(Ref, Owner, Target, Error)) return false;
		}
	}
	return true;
}
bool FWorld::AddActor(FActor Actor, FString& Error)
{
	Actors.push_back(std::move(Actor));
	if (Validate(Error)) return true;
	Actors.pop_back(); FString Ignored; Validate(Ignored); return false;
}
bool FWorld::AddComponent(const FString& ActorId, FActorComponent Component, FString& Error)
{
	const auto It = Index.find(ActorId.ToStdString()); if (It == Index.end()) { Error = "Actor is missing."; return false; }
	auto& List = Actors[It->second].Components; List.push_back(std::move(Component));
	if (Validate(Error)) return true;
	List.pop_back(); FString Ignored; Validate(Ignored); return false;
}
bool FWorld::SetTransform(const FString& ActorId, const FTransform3D& Transform, FString& Error)
{
	const auto It = Index.find(ActorId.ToStdString()); if (It == Index.end() || !Transform.IsValid()) { Error = "Actor is missing or transform is invalid."; return false; }
	Actors[It->second].Root.Transform = Transform; Error.Empty(); return true;
}
bool FWorld::SetParent(const FString& ActorId, const FString& ParentId, FString& Error)
{
	const auto It = Index.find(ActorId.ToStdString()); if (It == Index.end()) { Error = "Actor is missing."; return false; }
	auto& A = Actors[It->second]; const auto Previous = A.ParentId; A.ParentId = ParentId;
	if (Validate(Error)) return true;
	A.ParentId = Previous; FString Ignored; Validate(Ignored); return false;
}
bool FWorld::GetWorldTransform(const FString& ActorId, Matrix44& Matrix, FString& Error) const
{
	const auto* A = FindActor(ActorId); if (!A) { Error = "Actor is missing."; return false; }
	Matrix44 Result = A->Root.Transform.ToMatrix();
	while (!A->ParentId.IsEmpty())
	{
		A = FindActor(A->ParentId); if (!A) { Error = "Actor parent is missing."; return false; }
		Matrix44 Combined; Combined.Multiply(A->Root.Transform.ToMatrix(), Result); Result = Combined;
	}
	for (const auto& Basis : { Vector3d(), Vector3d(1,0,0), Vector3d(0,1,0), Vector3d(0,0,1) })
	{ Vector3d Point; Result.Transform(Point, Basis); if (!ValidVector(Point, FLT_MAX)) { Error = "World transform overflow."; return false; } }
	Matrix = Result; Error.Empty(); return true;
}
bool FWorld::RemoveActor(const FString& ActorId, FString& Error)
{
	if (!FindActor(ActorId)) { Error = "Actor is missing."; return false; }
	TSet<std::string> Remove{ ActorId.ToStdString() };
	bool Changed = true;
	while (Changed) { Changed = false; for (const auto& A : Actors) if (Remove.contains(A.ParentId.ToStdString())) Changed |= Remove.insert(A.Id.ToStdString()).second; }
	for (const auto& A : Actors) if (!Remove.contains(A.Id.ToStdString()))
		for (const auto* C : Components(A)) for (const auto& R : C->References)
			if (Remove.contains(R.ActorId.ToStdString())) { Error = "An actor outside the subtree still references it."; return false; }
	std::erase_if(Actors, [&](const FActor& A) { return Remove.contains(A.Id.ToStdString()); }); return Validate(Error);
}
bool FWorld::DuplicateActor(const FString& ActorId, FString& NewActorId, FString& Error)
{
	if (!FindActor(ActorId)) { Error = "Actor is missing."; return false; }
	TSet<std::string> Copy{ ActorId.ToStdString() }; bool Changed = true;
	while (Changed) { Changed = false; for (const auto& A : Actors) if (Copy.contains(A.ParentId.ToStdString())) Changed |= Copy.insert(A.Id.ToStdString()).second; }
	TMap<std::string, FString> Remap;
	for (const auto& A : Actors) if (Copy.contains(A.Id.ToStdString()))
	{ Remap[A.Id.ToStdString()] = FAssetId::New(); for (const auto* C : Components(A)) Remap[C->Id.ToStdString()] = FAssetId::New(); }
	const auto Map = [&](const FString& Id) { const auto It = Remap.find(Id.ToStdString()); return It == Remap.end() ? Id : It->second; };
	TArray<FActor> Copies;
	for (const auto& A : Actors) if (Copy.contains(A.Id.ToStdString()))
	{
		FActor B = A; B.Id = Map(A.Id); B.ParentId = Map(A.ParentId);
		const auto Fix = [&](FActorComponent& C) { C.Id = Map(C.Id); for (auto& Ref : C.References) { Ref.ActorId = Map(Ref.ActorId); Ref.ComponentId = Map(Ref.ComponentId); } };
		Fix(B.Root); for (auto& C : B.Components) Fix(C); Copies.push_back(std::move(B));
	}
	const auto Before = Actors.size(); Actors.insert(Actors.end(), Copies.begin(), Copies.end());
	if (!Validate(Error)) { Actors.resize(Before); FString Ignored; Validate(Ignored); return false; }
	NewActorId = Map(ActorId); return true;
}
J FWorld::ToJson() const
{
	auto Out = J::MakeObject(); Out["Version"] = 1; Out["Actors"] = J::MakeArray();
	for (const auto& A : Actors) Out["Actors"].Push(A.ToJson());
	return Out;
}
TOptional<FWorld> FWorld::FromJson(const J& Json, FString& Error)
{
	try
	{
		Require(Json.IsObject() && Json.Get("Version").GetType() == EJsonType::Number && Json.Get("Version").AsNumber() == 1, "Unsupported runtime scene version.");
		FWorld World;
		for (const auto& In : Array(Json.Get("Actors"), 100000))
		{
			FActor A; A.Id = Text(In.Get("Id")); A.Name = Text(In.Get("Name")); A.ParentId = Text(In.Get("Parent"));
			static_cast<FActorComponent&>(A.Root) = ReadComponent(In.Get("Root"));
			auto Transform = FTransform3D::FromJson(In.Get("Root").Get("Transform"), Error); Require(Transform.has_value(), "Invalid root component transform."); A.Root.Transform = *Transform;
			for (const auto& C : Array(In.Get("Components"), 1024)) A.Components.push_back(ReadComponent(C)); World.Actors.push_back(std::move(A));
		}
		if (!World.Validate(Error)) return {}; return World;
	}
	catch (const std::exception& E) { Error = E.what(); return {}; }
}
