// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "CoreMinimal.h"

class SWidget;

// A tooltip a widget shows after the pointer rests on it (§7.5). SToolTip
// (UI module) is the usual implementation.
class IToolTip
{
public:
	virtual ~IToolTip() = default;

	virtual TSharedRef<SWidget> AsWidget() = 0;
	// Empty tooltips are not shown.
	virtual bool IsEmpty() const = 0;
	virtual void OnOpening() {}
	virtual void OnClosed() {}
};
