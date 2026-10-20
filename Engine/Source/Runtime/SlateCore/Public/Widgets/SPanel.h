// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Widgets/DeclarativeSyntax.h"

// A widget with many typed slots. Children paint in slot order at the same
// layer unless the panel says otherwise.
class SPanel : public SWidget
{
protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override
	{
		(void)Geometry;
		return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId, InWidgetStyle);
	}
};
