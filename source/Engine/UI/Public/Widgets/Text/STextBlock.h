// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/StyleSet.h"
#include "Text/TextLayout.h"
#include "Widgets/SLeafWidget.h"

// Read-only text: one or more lines, optionally wrapped.
//
// Color: ColorAndOpacity if given, otherwise the TextStyle's color if a
// style was given, otherwise the inherited foreground color (so text inside
// a button follows the button's state).
class STextBlock : public SLeafWidget
{
public:
	GE_BEGIN_ARGS(STextBlock)
		GE_ATTRIBUTE(FString, Text, "")
		GE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		GE_OPTIONAL_ATTRIBUTE(FFontInfo, Font)
		GE_OPTIONAL_ATTRIBUTE(FStyleColor, ColorAndOpacity)
		GE_ARGUMENT(ETextJustify, Justification, ETextJustify::Left)
		GE_ARGUMENT(bool, AutoWrapText, false) // wrap at the arranged width
		GE_ATTRIBUTE(float, WrapTextAt, 0.0f)    // wrap at a fixed width (> 0)
		GE_ARGUMENT(FMargin, Margin, FMargin())
		GE_ATTRIBUTE(float, MinDesiredWidth, 0.0f)
	GE_END_ARGS()

	void Construct(const FArguments& Args);

	const FString& GetText() const { return Text.Get(); }
	void SetText(TAttribute<FString> InText) { Text.Assign(InText); }
	void SetColorAndOpacity(TAttribute<FStyleColor> Color) { ColorAndOpacity.Assign(TAttribute<TOptional<FStyleColor>>::Create([Color] { return TOptional<FStyleColor>(Color.Get()); })); }
	void SetFont(const FFontInfo& InFont);
	void SetAutoWrapText(bool bWrap);
	void SetWrapTextAt(float Width) { WrapTextAt.Set(Width); }

	// The laid-out text (for tests and hit-testing).
	const FTextLayout& GetTextLayout() const { return Layout; }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnPreArrange(const FGeometry& AllottedGeometry) override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	FFontInfo GetEffectiveFont() const;
	float GetEffectiveWrapWidth() const;

	TWidgetAttribute<FString, EInvalidateWidgetReason::Layout> Text{ *this };
	TWidgetAttribute<TOptional<FStyleColor>, EInvalidateWidgetReason::Paint> ColorAndOpacity{ *this };
	TWidgetAttribute<float, EInvalidateWidgetReason::Layout> WrapTextAt{ *this, 0.0f };
	TWidgetAttribute<float, EInvalidateWidgetReason::Layout> MinDesiredWidth{ *this, 0.0f };
	const FTextBlockStyle* TextStyle = nullptr;
	bool bExplicitStyle = false;
	TOptional<FFontInfo> FontOverride;
	ETextJustify Justification = ETextJustify::Left;
	FMargin Margin;
	bool bAutoWrap = false;
	float ArrangedWidth = 0.0f;

	mutable FTextLayout Layout;
};
