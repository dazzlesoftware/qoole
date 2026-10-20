// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/SlateStyle.h"
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
	SLATE_BEGIN_ARGS(STextBlock)
		SLATE_ATTRIBUTE(FString, Text, "")
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_OPTIONAL_ATTRIBUTE(FSlateFontInfo, Font)
		SLATE_OPTIONAL_ATTRIBUTE(FSlateColor, ColorAndOpacity)
		SLATE_ARGUMENT(ETextJustify, Justification, ETextJustify::Left)
		SLATE_ARGUMENT(bool, AutoWrapText, false) // wrap at the arranged width
		SLATE_ATTRIBUTE(float, WrapTextAt, 0.0f)    // wrap at a fixed width (> 0)
		SLATE_ARGUMENT(FMargin, Margin, FMargin())
		SLATE_ATTRIBUTE(float, MinDesiredWidth, 0.0f)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	const FString& GetText() const { return Text.Get(); }
	void SetText(TAttribute<FString> InText) { Text.Assign(InText); }
	void SetColorAndOpacity(TAttribute<FSlateColor> Color) { ColorAndOpacity.Assign(TAttribute<TOptional<FSlateColor>>::Create([Color] { return TOptional<FSlateColor>(Color.Get()); })); }
	void SetFont(const FSlateFontInfo& InFont);
	void SetAutoWrapText(bool bWrap);
	void SetWrapTextAt(float Width) { WrapTextAt.Set(Width); }

	// The laid-out text (for tests and hit-testing).
	const FTextLayout& GetTextLayout() const { return Layout; }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnPreArrange(const FGeometry& AllottedGeometry) override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	FSlateFontInfo GetEffectiveFont() const;
	float GetEffectiveWrapWidth() const;

	TWidgetAttribute<FString, EInvalidateWidgetReason::Layout> Text{ *this };
	TWidgetAttribute<TOptional<FSlateColor>, EInvalidateWidgetReason::Paint> ColorAndOpacity{ *this };
	TWidgetAttribute<float, EInvalidateWidgetReason::Layout> WrapTextAt{ *this, 0.0f };
	TWidgetAttribute<float, EInvalidateWidgetReason::Layout> MinDesiredWidth{ *this, 0.0f };
	const FTextBlockStyle* TextStyle = nullptr;
	bool bExplicitStyle = false;
	TOptional<FSlateFontInfo> FontOverride;
	ETextJustify Justification = ETextJustify::Left;
	FMargin Margin;
	bool bAutoWrap = false;
	float ArrangedWidth = 0.0f;

	mutable FTextLayout Layout;
};
