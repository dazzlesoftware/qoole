// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Widgets/Input/SSlider.h"
#include "Widgets/SCompoundWidget.h"

using FOnColorChanged = TDelegate<void(FColor)>;

// A color swatch; the right half shows the alpha over a checkerboard.
class SColorBlock : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SColorBlock)
		SLATE_ATTRIBUTE(FColor, Color, FColor::White())
		SLATE_ARGUMENT(FVector2, Size, (FVector2(28.0f, 16.0f)))
		SLATE_ARGUMENT(float, CornerRadius, 3.0f)
		SLATE_ATTRIBUTE(bool, IsUndetermined, false) // several different colors
		SLATE_EVENT(FPointerEventHandler, OnMouseButtonDown)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
	{
		return OnClick.IsBound() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
	}

protected:
	FVector2 ComputeDesiredSize(float) const override { return Size; }
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	TWidgetAttribute<FColor, EInvalidateWidgetReason::Paint> Color{ *this, FColor::White() };
	TWidgetAttribute<bool, EInvalidateWidgetReason::Paint> IsUndetermined{ *this, false };
	FVector2 Size;
	float CornerRadius = 3.0f;
	FPointerEventHandler OnClick;
};

// RGBA sliders with numeric entry and a hex field. Colors are 8-bit sRGB.
// OnInteractionBegin/End bracket drags (one undo transaction each); a
// change outside them (a typed value) is a transaction of its own.
class SColorPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SColorPicker)
		SLATE_ATTRIBUTE(FColor, TargetColor, FColor::White())
		SLATE_ARGUMENT(bool, UseAlpha, true)
		SLATE_EVENT(FOnColorChanged, OnColorChanged)
		SLATE_EVENT(FSimpleDelegate, OnInteractionBegin)
		SLATE_EVENT(FSimpleDelegate, OnInteractionEnd)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	FColor GetColor() const { return Color; }
	void SetColor(FColor NewColor, bool bNotify);

	static FString ToHex(FColor Color, bool bWithAlpha);
	static TOptional<FColor> FromHex(const FString& Hex);

private:
	TSharedRef<SWidget> MakeChannel(const char* Label, int32 Channel, FSlateColor BarColor);

	FColor Color;
	bool bUseAlpha = true;
	FOnColorChanged OnColorChanged;
	FSimpleDelegate OnInteractionBegin;
	FSimpleDelegate OnInteractionEnd;
};
