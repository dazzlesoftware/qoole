// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "CoreMinimal.h"

// Shared viewport brush settings; the legacy type name is retained during migration.
struct FLandscapeBrush
{
	float Radius = 256.0f;
	float Strength = 0.4f;
	float Falloff = 0.5f;

	static constexpr float MinRadius = 8.0f;
	static constexpr float MaxRadius = 8192.0f;

	// Unreal's [ and ] (a step of a quarter), and Ctrl with them for strength.
	void Grow(int32 Steps);
	void Strengthen(int32 Steps);
};

