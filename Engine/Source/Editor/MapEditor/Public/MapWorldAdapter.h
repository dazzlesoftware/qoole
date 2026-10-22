// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "World/World.h"

// The temporary bridge between the legacy document serialization and runtime
// scenes. Bulk references remain offsets into the enclosing package.
class FMapWorldAdapter
{
public:
	static bool ToRuntime(const FJsonValue& LegacyRoot, FWorld& World, FString& Error);
	static bool ToLegacy(const FWorld& World, FJsonValue& LegacyRoot, FString& Error);
};
