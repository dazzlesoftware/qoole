// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapWorldAdapter.h"
#include "World/WorldIdentity.h"
#include "World/WorldGeometry.h"
#include <stdexcept>

namespace
{
using J = FJsonValue;
struct FPayload
{
	const char* Key;
	const char* Type;
};
constexpr FPayload Payloads[] = {{"Entity", "Genesis.LegacyEntity"},
								 {"Brush", "Genesis.LegacyBrush"},
								 {"Mesh", "Genesis.StaticMesh"},
								 {"Landscape", "Genesis.Landscape"},
								 {"Foliage", "Genesis.Foliage"}};
void Require(bool Condition, const char* Text)
{
	if (!Condition)
		throw std::runtime_error(Text);
}
TArray<FAssetReference> AssetReferences(const char* Key, const J& Payload)
{
	FString Type;
	for (const auto& P : Payloads)
		if (std::string_view(Key) == P.Key)
			Type = P.Type;
	TArray<FAssetReference> Refs;
	FString Error;
	if (!FWorldGeometryCodec::GetAssetReferences(Type, Payload, Refs, Error))
		throw std::runtime_error(Error.ToStdString());
	return Refs;
}

void Capture(const J& Node, const FString& Parent, J& Actors, int Depth)
{
	Require(Node.IsObject() && Depth < 128 && Actors.Num() < 100000, "Invalid legacy hierarchy.");
	FString Error;
	auto Identity = FActorIdentity::FromJson(Node.Get("Identity"), Error);
	Require(Identity.has_value(), "Missing actor/component identity.");
	FActor Actor;
	Actor.Id = Identity->Actor;
	Actor.ParentId = Parent;
	Actor.Name = Node.Get("Name").AsString();
	Actor.Root.Id = Identity->Transform;
	Actor.Root.Type = "Genesis.Transform";
	auto Transform = J::MakeObject();
	Transform["Location"] = Node.Get("Position");
	Transform["Rotation"] = Node.Get("Rotation");
	Transform["Scale"] = Node.Get("Scale");
	const auto Parsed = FTransform3D::FromJson(Transform, Error);
	Require(Parsed.has_value(), "Invalid legacy transform.");
	Actor.Root.Transform = *Parsed;
	for (const auto& Payload : Payloads)
		if (Node.Contains(Payload.Key))
		{
			FActorComponent C;
			C.Type = Payload.Type;
			C.Id = C.Type == "Genesis.LegacyEntity" ? Identity->Entity : Identity->Payload;
			C.Properties["Data"] = Node.Get(Payload.Key);
			C.Assets = AssetReferences(Payload.Key, Node.Get(Payload.Key));
			Actor.Components.push_back(std::move(C));
		}
	Actors.Push(Actor.ToJson());
	Require(Node.Get("Children").IsArray(), "Missing legacy children.");
	for (const auto& Child : Node.Get("Children").GetItems())
		Capture(Child, Actor.Id, Actors, Depth + 1);
}
J Restore(const FActor& Actor, const TMap<std::string, TArray<const FActor*>>& Children, int Depth)
{
	Require(Depth < 128, "Legacy hierarchy depth limit exceeded.");
	const auto NameAnsi = Actor.Name.ToAnsi();
	Require(NameAnsi.size() <= 31 && FString::FromAnsi(NameAnsi) == Actor.Name &&
				Actor.Name.View().find_first_of("\r\n") == std::string_view::npos &&
				(Actor.Name.IsEmpty() || (Actor.Name[0] != ':' && Actor.Name[0] != ' ' && Actor.Name[0] != '\t')),
			"Actor name cannot be represented by the legacy editor; no data was discarded.");
	Require(Actor.Root.Properties.Num() == 0 && Actor.Root.References.empty() && Actor.Root.Assets.empty(),
			"The legacy editor cannot yet preserve root component properties or references.");
	auto Out = J::MakeObject();
	Out["Name"] = Actor.Name;
	const auto Transform = Actor.Root.Transform.ToJson();
	Out["Position"] = Transform.Get("Location");
	Out["Rotation"] = Transform.Get("Rotation");
	Out["Scale"] = Transform.Get("Scale");
	auto Identity = FActorIdentity::New();
	Identity.Actor = Actor.Id;
	Identity.Transform = Actor.Root.Id;
	int GeometryCount = 0;
	for (const auto& C : Actor.Components)
	{
		Require(C.References.empty() && C.Properties.Num() == 1 && C.Properties.Contains("Data"),
				"The legacy editor cannot yet preserve this component's properties or references.");
		const FPayload* Match = nullptr;
		for (const auto& P : Payloads)
			if (C.Type == P.Type)
				Match = &P;
		Require(Match != nullptr, "The legacy editor does not support this runtime component type.");
		const auto Expected = AssetReferences(Match->Key, C.Properties.Get("Data"));
		Require(Expected.size() == C.Assets.size(), "Unexpected component asset references.");
		for (size_t I = 0; I < Expected.size(); ++I)
			Require(Expected[I].ToJson() == C.Assets[I].ToJson(),
					"Component asset references disagree with the payload.");
		Require(!Out.Contains(Match->Key), "Multiple components of this type require the runtime editor migration.");
		Out[Match->Key] = C.Properties.Get("Data");
		if (C.Type == "Genesis.LegacyEntity")
			Identity.Entity = C.Id;
		else
		{
			Require(++GeometryCount == 1, "The legacy editor supports only one geometry payload per actor.");
			Identity.Payload = C.Id;
		}
	}
	Out["Identity"] = Identity.ToJson();
	Out["Children"] = J::MakeArray();
	const auto Found = Children.find(Actor.Id.ToStdString());
	if (Found != Children.end())
		for (const auto* Child : Found->second)
			Out["Children"].Push(Restore(*Child, Children, Depth + 1));
	return Out;
}
} // namespace

bool FMapWorldAdapter::ToRuntime(const J& LegacyRoot, FWorld& World, FString& Error)
{
	try
	{
		auto Scene = J::MakeObject();
		Scene["Version"] = 1;
		Scene["Actors"] = J::MakeArray();
		Capture(LegacyRoot, {}, Scene["Actors"], 0);
		auto Parsed = FWorld::FromJson(Scene, Error);
		if (!Parsed)
			return false;
		World = std::move(*Parsed);
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FMapWorldAdapter::ToLegacy(const FWorld& World, J& LegacyRoot, FString& Error)
{
	try
	{
		const FActor* Root = nullptr;
		TMap<std::string, TArray<const FActor*>> Children;
		for (const auto& A : World.GetActors())
			if (A.ParentId.IsEmpty())
			{
				Require(!Root, "The legacy editor requires one root actor.");
				Root = &A;
			}
			else
				Children[A.ParentId.ToStdString()].push_back(&A);
		Require(Root != nullptr, "The legacy editor requires a root actor.");
		auto Result = Restore(*Root, Children, 0);
		LegacyRoot = std::move(Result);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
