// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "World/World.h"
#include "World/WorldIdentity.h"
#include <limits>

TEST_SUITE("World")
{
	TEST_CASE("Hierarchy composes transforms and rejects cycles without changing the scene")
	{
		FWorld World;
		FString Error;
		FActor Parent = FActor::New("Parent"), Child = FActor::New("Child");
		Parent.Root.Transform.Location = Vector3d(10, 20, 30);
		Parent.Root.Transform.Scale = Vector3d(-2, 3, 4);
		Child.ParentId = Parent.Id;
		Child.Root.Transform.Location = Vector3d(5, 6, 7);
		REQUIRE(World.AddActor(Parent, Error));
		REQUIRE(World.AddActor(Child, Error));
		Matrix44 Matrix;
		REQUIRE(World.GetWorldTransform(Child.Id, Matrix, Error));
		Vector3d Point;
		Matrix.Transform(Point, Vector3d());
		CHECK(Point.GetX() == doctest::Approx(0));
		CHECK(Point.GetY() == doctest::Approx(38));
		CHECK(Point.GetZ() == doctest::Approx(58));
		CHECK_FALSE(World.SetParent(Parent.Id, Child.Id, Error));
		CHECK(World.FindActor(Parent.Id)->ParentId.IsEmpty());
		CHECK_FALSE(World.SetParent(Child.Id, Child.Id, Error));
		CHECK_FALSE(World.SetParent(Child.Id, FAssetId::New(), Error));
		CHECK(World.FindActor(Child.Id)->ParentId == Parent.Id);
		REQUIRE(World.SetParent(Child.Id, {}, Error));
		CHECK(World.FindActor(Child.Id)->Root.Transform.Location.GetX() == 5);
		FTransform3D Invalid = Child.Root.Transform;
		Invalid.Scale.NewVector(0, 1, 1);
		CHECK_FALSE(World.SetTransform(Child.Id, Invalid, Error));
		Invalid.Scale.NewVector(1, 1, 1);
		Invalid.Location.NewVector(std::numeric_limits<float>::infinity(), 0, 0);
		CHECK_FALSE(World.SetTransform(Child.Id, Invalid, Error));
	}

	TEST_CASE("Actor component IDs and owned references survive scene round trip")
	{
		FWorld World;
		FString Error;
		FActor A = FActor::New("A"), B = FActor::New("B");
		FActorComponent C = FActorComponent::New("Game.Test");
		C.Properties["Health"] = 75;
		C.References = {{B.Id, B.Root.Id}, {}, {A.Id, {}}};
		C.Assets = {{FAssetId::New(), "/Game/Materials/Grass"}};
		A.Components.push_back(C);
		// Bulk scene loading permits forward references; sequential insertion rejects dangling ones.
		CHECK_FALSE(World.AddActor(A, Error));
		CHECK(World.GetActors().empty());
		FJsonValue Json = FJsonValue::MakeObject();
		Json["Version"] = 1;
		Json["Actors"] = FJsonValue::MakeArray();
		Json["Actors"].Push(A.ToJson()).Push(B.ToJson());
		TOptional<FWorld> Back = FWorld::FromJson(Json, Error);
		REQUIRE_MESSAGE(Back, *Error);
		const FActorComponent* Found = Back->FindComponent(A.Id, C.Id);
		REQUIRE(Found);
		CHECK(Found->Properties.Get("Health").AsNumber() == 75);
		CHECK(Found->Assets[0].Id == C.Assets[0].Id);
		const FActor* Owner = nullptr;
		const FActorComponent* Component = nullptr;
		REQUIRE(Back->Resolve(Found->References[0], Owner, Component, Error));
		CHECK(Owner->Id == B.Id);
		CHECK(Component->Id == B.Root.Id);
		CHECK_FALSE(Back->Resolve({A.Id, B.Root.Id}, Owner, Component, Error));
		CHECK_FALSE(Owner);
		CHECK_FALSE(Component);
		REQUIRE(Back->Resolve({}, Owner, Component, Error));
		CHECK_FALSE(Owner);
		CHECK_FALSE(Component);
		CHECK_FALSE(Back->RemoveActor(B.Id, Error));
		CHECK(Back->FindActor(B.Id));
		REQUIRE(Back->RemoveActor(A.Id, Error));
		REQUIRE(Back->RemoveActor(B.Id, Error));
		CHECK(Back->GetActors().empty());
	}

	TEST_CASE("Duplicating subtrees remaps internal references and retains external assets and actors")
	{
		FWorld World;
		FString Error;
		FActor Outside = FActor::New("Outside"), A = FActor::New("Parent"), B = FActor::New("Child");
		REQUIRE(World.AddActor(Outside, Error));
		REQUIRE(World.AddActor(A, Error));
		B.ParentId = A.Id;
		FActorComponent C = FActorComponent::New("Game.Link");
		C.References = {{A.Id, A.Root.Id}, {Outside.Id, Outside.Root.Id}, {B.Id, C.Id}};
		C.Assets = {{FAssetId::New(), "/Game/Models/Tree"}};
		B.Components.push_back(C);
		REQUIRE(World.AddActor(B, Error));
		FString CopyId;
		REQUIRE(World.DuplicateActor(A.Id, CopyId, Error));
		CHECK(CopyId != A.Id);
		const FActor* Copy = World.FindActor(CopyId);
		REQUIRE(Copy);
		CHECK(Copy->Root.Id != A.Root.Id);
		const FActor* Child = nullptr;
		for (const FActor& Actor : World.GetActors())
			if (Actor.ParentId == CopyId)
				Child = &Actor;
		REQUIRE(Child);
		REQUIRE(Child->Components.size() == 1);
		FActorComponent const& Link = Child->Components[0];
		CHECK(Child->Id != B.Id);
		CHECK(Link.Id != C.Id);
		CHECK(Link.References[0].ActorId == CopyId);
		CHECK(Link.References[0].ComponentId == Copy->Root.Id);
		CHECK(Link.References[1].ActorId == Outside.Id);
		CHECK(Link.References[1].ComponentId == Outside.Root.Id);
		CHECK(Link.References[2].ActorId == Child->Id);
		CHECK(Link.References[2].ComponentId == Link.Id);
		CHECK(Link.Assets[0].Id == C.Assets[0].Id);
		TOptional<FWorld> Snapshot = FWorld::FromJson(World.ToJson(), Error);
		REQUIRE(Snapshot);
		REQUIRE(World.RemoveActor(CopyId, Error));
		CHECK(World.GetActors().size() == 3);
		CHECK(Snapshot->FindActor(CopyId)); // snapshot identity is unchanged
	}

	TEST_CASE("Invalid scene identities schemas and references fail atomically")
	{
		FString Error;
		FWorld World;
		const FActor A = FActor::New("A");
		REQUIRE(World.AddActor(A, Error));
		CHECK_FALSE(World.AddActor(A, Error));
		REQUIRE(World.GetActors().size() == 1);
		FActor Collision = FActor::New("Collision");
		Collision.Root.Id = A.Id;
		CHECK_FALSE(World.AddActor(Collision, Error));
		FActorComponent C = FActorComponent::New("Test");
		C.Id = A.Root.Id;
		CHECK_FALSE(World.AddComponent(A.Id, C, Error));
		CHECK(World.FindActor(A.Id)->Components.empty());
		FJsonValue Json = World.ToJson();
		Json["Version"] = 2;
		CHECK_FALSE(FWorld::FromJson(Json, Error));
		Json = World.ToJson();
		Json["Actors"][0]["Parent"] = FAssetId::New();
		CHECK_FALSE(FWorld::FromJson(Json, Error));
		Json = World.ToJson();
		Json["Actors"][0]["Root"]["Id"] = A.Id;
		CHECK_FALSE(FWorld::FromJson(Json, Error));
		Json = World.ToJson();
		Json["Actors"][0]["Root"]["Transform"]["Scale"][0] = 0;
		CHECK_FALSE(FWorld::FromJson(Json, Error));
		CHECK(World.FindActor(A.Id));
		CHECK(World.GetActors().size() == 1);
		FActorIdentity Identity = FActorIdentity::New();
		REQUIRE(Identity.IsValid());
		TOptional<FActorIdentity> IdentityBack = FActorIdentity::FromJson(Identity.ToJson(), Error);
		REQUIRE(IdentityBack);
		CHECK(IdentityBack->Actor == Identity.Actor);
		Identity.Payload = Identity.Actor;
		CHECK_FALSE(Identity.IsValid());
	}
}
