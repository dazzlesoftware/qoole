// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/SlateStyle.h"
#include "Widgets/SCompoundWidget.h"

// The standard tooltip: wrapped text or any content on a popup background.
class SToolTip : public SCompoundWidget, public IToolTip
{
public:
	SLATE_BEGIN_ARGS(SToolTip)
		SLATE_ATTRIBUTE(FString, Text, "")
		SLATE_STYLE_ARGUMENT(FToolTipStyle, Style)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	// IToolTip
	TSharedRef<SWidget> AsWidget() override { return SharedThis(this); }
	bool IsEmpty() const override;

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FToolTipStyle* Style = nullptr;
	TAttribute<FString> Text;
	bool bHasCustomContent = false;
};
