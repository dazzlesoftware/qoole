// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Serialization/Json.h"

// Identity slots used by the transitional editor adapter. Only existing
// components are exported to the runtime scene; snapshots copy these IDs.
struct FActorIdentity
{
	FString Actor, Transform, Payload, Entity;
	static FActorIdentity New();
	bool IsValid() const;
	FJsonValue ToJson() const;
	static TOptional<FActorIdentity> FromJson(const FJsonValue& Json, FString& Error);
};
