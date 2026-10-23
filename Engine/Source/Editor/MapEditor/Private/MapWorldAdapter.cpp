// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapWorldAdapter.h"
#include "Misc/ValidationContext.h"
#include "Internationalization/TextLocalization.h"
#include "World/WorldIdentity.h"
#include "World/WorldGeometry.h"

namespace
{
	using J = FJsonValue;
	struct FPayload
	{
		const char* Key;
		const char* Type;
	};
	constexpr FPayload Payloads[] = {{"Entity", "Genesis.LegacyEntity"}, {"Brush", "Genesis.LegacyBrush"},
	    {"Mesh", "Genesis.StaticMesh"}, {"Landscape", "Genesis.Landscape"}, {"Foliage", "Genesis.Foliage"}};

	TArray<FAssetReference> AssetReferences(FValidationContext& Validation, const char* Key, const J& Payload)
	{
		FString Type;
		for (const FPayload& P : Payloads)
			if (std::string_view(Key) == P.Key)
				Type = P.Type;
		TArray<FAssetReference> Refs;
		FString Error;
		if (!FWorldGeometryCodec::GetAssetReferences(Type, Payload, Refs, Error))
		{
			Validation.Fail(Error);
			return {};
		}
		return Refs;
	}

	void Capture(FValidationContext& Validation, const J& Node, const FString& Parent, J& Actors, int Depth)
	{
		if (!Validation.Check(
		        Node.IsObject() && Depth < 128 && Actors.Num() < 100000, GENESIS_TEXT("Invalid legacy hierarchy.")))
		{
			return;
		}

		FString Error;
		TOptional<FActorIdentity> Identity = FActorIdentity::FromJson(Node.Get("Identity"), Error);
		if (!Validation.Check(Identity.has_value(), GENESIS_TEXT("Missing actor/component identity.")))
		{
			return;
		}

		FActor Actor;
		Actor.Id = Identity->Actor;
		Actor.ParentId = Parent;
		Actor.Name = Node.Get("Name").AsString();
		Actor.Root.Id = Identity->Transform;
		Actor.Root.Type = "Genesis.Transform";
		FJsonValue Transform = J::MakeObject();
		Transform["Location"] = Node.Get("Position");
		Transform["Rotation"] = Node.Get("Rotation");
		Transform["Scale"] = Node.Get("Scale");
		const TOptional<FTransform3D> Parsed = FTransform3D::FromJson(Transform, Error);
		if (!Validation.Check(Parsed.has_value(), GENESIS_TEXT("Invalid legacy transform.")))
		{
			return;
		}

		Actor.Root.Transform = *Parsed;
		for (const FPayload& Payload : Payloads)
			if (Node.Contains(Payload.Key))
			{
				FActorComponent C;
				C.Type = Payload.Type;
				C.Id = C.Type == "Genesis.LegacyEntity" ? Identity->Entity : Identity->Payload;
				C.Properties["Data"] = Node.Get(Payload.Key);
				C.Assets = AssetReferences(Validation, Payload.Key, Node.Get(Payload.Key));
				if (!Validation.IsValid())
				{
					return;
				}

				Actor.Components.push_back(std::move(C));
			}
		Actors.Push(Actor.ToJson());
		if (!Validation.Check(Node.Get("Children").IsArray(), GENESIS_TEXT("Missing legacy children.")))
		{
			return;
		}

		for (const FJsonValue& Child : Node.Get("Children").GetItems())
		{
			Capture(Validation, Child, Actor.Id, Actors, Depth + 1);
			if (!Validation.IsValid())
			{
				return;
			}
		}
		if (!Validation.IsValid())
		{
			return;
		}
	}
	J Restore(FValidationContext& Validation, const FActor& Actor,
	    const TMap<std::string, TArray<const FActor*>>& Children, int Depth)
	{
		if (!Validation.Check(Depth < 128, GENESIS_TEXT("Legacy hierarchy depth limit exceeded.")))
		{
			return {};
		}

		const std::string NameAnsi = Actor.Name.ToAnsi();
		if (!Validation.Check(NameAnsi.size() <= 31 && FString::FromAnsi(NameAnsi) == Actor.Name &&
		            Actor.Name.View().find_first_of("\r\n") == std::string_view::npos &&
		            (Actor.Name.IsEmpty() || (Actor.Name[0] != ':' && Actor.Name[0] != ' ' && Actor.Name[0] != '\t')),
		        GENESIS_TEXT("Actor name cannot be represented by the legacy editor; no data was discarded.")))
		{
			return {};
		}

		if (!Validation.Check(
		        Actor.Root.Properties.Num() == 0 && Actor.Root.References.empty() && Actor.Root.Assets.empty(),
		        GENESIS_TEXT("The legacy editor cannot yet preserve root component properties or references.")))
		{
			return {};
		}

		FJsonValue Out = J::MakeObject();
		Out["Name"] = Actor.Name;
		const FJsonValue Transform = Actor.Root.Transform.ToJson();
		Out["Position"] = Transform.Get("Location");
		Out["Rotation"] = Transform.Get("Rotation");
		Out["Scale"] = Transform.Get("Scale");
		FActorIdentity Identity = FActorIdentity::New();
		Identity.Actor = Actor.Id;
		Identity.Transform = Actor.Root.Id;
		int GeometryCount = 0;
		for (const FActorComponent& C : Actor.Components)
		{
			if (!Validation.Check(C.References.empty() && C.Properties.Num() == 1 && C.Properties.Contains("Data"),
			        GENESIS_TEXT("The legacy editor cannot yet preserve this component's properties or references.")))
			{
				return {};
			}

			const FPayload* Match = nullptr;
			for (const FPayload& P : Payloads)
				if (C.Type == P.Type)
					Match = &P;
			if (!Validation.Check(
			        Match != nullptr, GENESIS_TEXT("The legacy editor does not support this runtime component type.")))
			{
				return {};
			}

			const TArray<FAssetReference> Expected = AssetReferences(Validation, Match->Key, C.Properties.Get("Data"));
			if (!Validation.IsValid())
			{
				return {};
			}

			if (!Validation.Check(
			        Expected.size() == C.Assets.size(), GENESIS_TEXT("Unexpected component asset references.")))
			{
				return {};
			}

			for (size_t I = 0; I < Expected.size(); ++I)
			{
				if (!Validation.Check(Expected[I].ToJson() == C.Assets[I].ToJson(),
				        GENESIS_TEXT("Component asset references disagree with the payload.")))
				{
					return {};
				}
			}
			if (!Validation.Check(!Out.Contains(Match->Key),
			        GENESIS_TEXT("Multiple components of this type require the runtime editor migration.")))
			{
				return {};
			}

			Out[Match->Key] = C.Properties.Get("Data");
			if (C.Type == "Genesis.LegacyEntity")
				Identity.Entity = C.Id;
			else
			{
				if (!Validation.Check(++GeometryCount == 1,
				        GENESIS_TEXT("The legacy editor supports only one geometry payload per actor.")))
				{
					return {};
				}

				Identity.Payload = C.Id;
			}
		}
		Out["Identity"] = Identity.ToJson();
		Out["Children"] = J::MakeArray();
		const TMap<std::string, TArray<const FActor*>>::const_iterator Found = Children.find(Actor.Id.ToStdString());
		if (Found != Children.end())
			for (const FActor* Child : Found->second)
			{
				Out["Children"].Push(Restore(Validation, *Child, Children, Depth + 1));
				if (!Validation.IsValid())
				{
					return {};
				}
			}
		if (!Validation.IsValid())
		{
			return {};
		}
		return Out;
	}
} // namespace

bool FMapWorldAdapter::ToRuntime(const J& LegacyRoot, FWorld& World, FString& Error)
{
	FValidationContext Validation(Error);

	FJsonValue Scene = J::MakeObject();
	Scene["Version"] = 1;
	Scene["Actors"] = J::MakeArray();
	Capture(Validation, LegacyRoot, {}, Scene["Actors"], 0);
	if (!Validation.IsValid())
	{
		return false;
	}

	TOptional<FWorld> Parsed = FWorld::FromJson(Scene, Error);
	if (!Parsed)
		return false;
	World = std::move(*Parsed);
	return true;
}
bool FMapWorldAdapter::ToLegacy(const FWorld& World, J& LegacyRoot, FString& Error)
{
	FValidationContext Validation(Error);

	const FActor* Root = nullptr;
	TMap<std::string, TArray<const FActor*>> Children;
	for (const FActor& A : World.GetActors())
		if (A.ParentId.IsEmpty())
		{
			if (!Validation.Check(!Root, GENESIS_TEXT("The legacy editor requires one root actor.")))
			{
				return false;
			}

			Root = &A;
		}
		else
			Children[A.ParentId.ToStdString()].push_back(&A);
	if (!Validation.Check(Root != nullptr, GENESIS_TEXT("The legacy editor requires a root actor.")))
	{
		return false;
	}

	J Result = Restore(Validation, *Root, Children, 0);
	if (!Validation.IsValid())
	{
		return false;
	}

	LegacyRoot = std::move(Result);
	Error.Empty();
	return true;
}
