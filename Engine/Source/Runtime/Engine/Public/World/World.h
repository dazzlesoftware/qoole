// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "AssetRegistry.h"
#include "Geometry/Math3D.h"

struct FTransform3D
{
	Vector3d Location;
	SphrVector Rotation; // radians, using Geometry's coordinate convention
	Vector3d Scale = Vector3d(1, 1, 1);
	bool IsValid() const;
	Matrix44 ToMatrix() const;
	FJsonValue ToJson() const;
	static TOptional<FTransform3D> FromJson(const FJsonValue& Json, FString& Error);
};

struct FWorldReference
{
	FString ActorId;
	FString ComponentId; // empty refers to the actor itself
	bool IsNull() const { return ActorId.IsEmpty() && ComponentId.IsEmpty(); }
};

struct FActorComponent
{
	FString Id;
	FString Type;
	FJsonValue Properties = FJsonValue::MakeObject();
	TArray<FWorldReference> References;
	TArray<FAssetReference> Assets;
	static FActorComponent New(const FString& Type);
	FJsonValue ToJson() const;
};

struct FSceneComponent : FActorComponent
{
	FTransform3D Transform;
	static FSceneComponent New();
};

struct FActor
{
	FString Id;
	FString Name;
	FString ParentId;
	FSceneComponent Root;
	TArray<FActorComponent> Components;
	static FActor New(const FString& Name);
	FJsonValue ToJson() const;
};

// Runtime scene ownership. Payload properties are schema data, not legacy
// Object/Geometry/Entity pointers. Mutations validate before committing.
class FWorld
{
public:
	const TArray<FActor>& GetActors() const { return Actors; }
	const FActor* FindActor(const FString& Id) const;
	const FActorComponent* FindComponent(const FString& ActorId, const FString& ComponentId) const;
	bool Resolve(const FWorldReference& Reference, const FActor*& Actor, const FActorComponent*& Component, FString& Error) const;
	bool AddActor(FActor Actor, FString& Error);
	bool AddComponent(const FString& ActorId, FActorComponent Component, FString& Error);
	bool SetTransform(const FString& ActorId, const FTransform3D& Transform, FString& Error);
	// Keeps the local transform. Keep-world reparenting is a later editor operation.
	bool SetParent(const FString& ActorId, const FString& ParentId, FString& Error);
	bool GetWorldTransform(const FString& ActorId, Matrix44& Matrix, FString& Error) const;
	// Removes a subtree, unless something outside it still references it.
	bool RemoveActor(const FString& ActorId, FString& Error);
	// New actor/component IDs; internal references are remapped, external ones retained.
	bool DuplicateActor(const FString& ActorId, FString& NewActorId, FString& Error);
	FJsonValue ToJson() const;
	static TOptional<FWorld> FromJson(const FJsonValue& Json, FString& Error);
	// Structural mutation attempts may invalidate addresses, even when rejected.
private:
	bool Validate(FString& Error);
	TArray<FActor> Actors;
	TMap<std::string, size_t> Index;
};
