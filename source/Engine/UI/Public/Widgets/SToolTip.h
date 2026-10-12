// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/StyleSet.h"
#include "Widgets/SCompoundWidget.h"

// The standard tooltip: wrapped text or any content on a popup background.
class SToolTip : public SCompoundWidget, public IToolTip
{
public:
	GE_BEGIN_ARGS(SToolTip)
		GE_ATTRIBUTE(FString, Text, "")
		GE_STYLE_ARGUMENT(FToolTipStyle, Style)
		GE_DEFAULT_SLOT(Content)
	GE_END_ARGS()

	void Construct(const FArguments& Args);

	// IToolTip
	TSharedRef<SWidget> AsWidget() override { return SharedThis(this); }
	bool IsEmpty() const override;

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FToolTipStyle* Style = nullptr;
	TAttribute<FString> Text;
	bool bHasCustomContent = false;
};
