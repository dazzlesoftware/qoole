// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Geometry/Math3D.h"
#include "Serialization/Json.h"

// A place in a game world: a region's permanent ID and a position in that
// region's map space. It stays correct when regions move on the grid or the
// render origin is rebased, and its values stay small however large the world.
struct FWorldLocation
{
	FString RegionId;
	Vector3d Local;
	FJsonValue ToJson() const;
	static TOptional<FWorldLocation> FromJson(const FJsonValue& Json, FString& Error);
};
