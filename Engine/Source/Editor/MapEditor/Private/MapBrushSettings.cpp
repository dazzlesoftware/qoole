// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapBrushSettings.h"
#include <cmath>

void FLandscapeBrush::Grow(int32 Steps)
{
	Radius = FMath::Clamp(Radius * std::pow(1.25f, static_cast<float>(Steps)), MinRadius, MaxRadius);
}

void FLandscapeBrush::Strengthen(int32 Steps)
{
	Strength = FMath::Clamp(std::round((Strength + 0.05f * static_cast<float>(Steps)) * 100.0f) / 100.0f, 0.01f, 1.0f);
}

