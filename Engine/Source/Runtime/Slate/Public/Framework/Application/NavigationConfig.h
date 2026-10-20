// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Maps keys, gamepad buttons and sticks to navigation (Engine/Documentation/Source/GenesisUI.md
// §7.4). Replaceable per application (FSlateApplication::SetNavigationConfig),
// e.g. a game that wants WASD navigation.

#pragma once

#include "Input/Events.h"

class FNavigationConfig
{
public:
	virtual ~FNavigationConfig() = default;

	virtual EUINavigation GetNavigationDirectionFromKey(const FKeyEvent& Event) const;
	virtual EUINavigationAction GetNavigationActionFromKey(const FKeyEvent& Event) const;
	// A stick crossing the threshold navigates; Invalid otherwise.
	virtual EUINavigation GetNavigationDirectionFromAnalog(EGamepadAxis Axis, float Value) const;

	bool bTabNavigation = true;
	bool bKeyNavigation = true;    // arrow keys
	bool bAnalogNavigation = true; // left stick
	float AnalogThreshold = 0.5f;
	float AnalogRepeatDelay = 0.4f; // first repeat
	float AnalogRepeatRate = 0.12f; // later repeats
};
