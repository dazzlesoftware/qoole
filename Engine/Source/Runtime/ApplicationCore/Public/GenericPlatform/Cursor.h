// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "GenericPlatform/InputTypes.h"

// The OS mouse cursor. Positions are in screen pixels.
class ICursor
{
public:
	virtual ~ICursor() = default;

	virtual EMouseCursor GetType() const = 0;
	virtual void SetType(EMouseCursor Type) = 0;

	virtual FVector2 GetPosition() const = 0;
	virtual void SetPosition(int32 X, int32 Y) = 0;

	virtual void Show(bool bShow) = 0;

	// Confines the cursor to a screen rectangle; nullptr releases it.
	virtual void Lock(const FIntRect* ScreenBounds) = 0;
};
