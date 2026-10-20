// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Display widgets: images, separators, progress bars, throbbers.

#pragma once

#include "Styling/SlateStyle.h"
#include "Widgets/SLeafWidget.h"

class SImage : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImage)
		SLATE_STYLE_ARGUMENT(FSlateBrush, Image)
		SLATE_ATTRIBUTE(FSlateColor, ColorAndOpacity, FSlateColor(FColor::White()))
		SLATE_OPTIONAL_ATTRIBUTE(FVector2, DesiredSizeOverride)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	void SetImage(const FSlateBrush* InImage);
	void SetColorAndOpacity(TAttribute<FSlateColor> Color) { ColorAndOpacity.Assign(Color); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FSlateBrush* Image = nullptr;
	TWidgetAttribute<FSlateColor, EInvalidateWidgetReason::Paint> ColorAndOpacity{ *this, FSlateColor(FColor::White()) };
	TOptional<FVector2> DesiredSizeOverride;
};

// A thin line between groups of widgets.
class SSeparator : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSeparator)
		SLATE_ARGUMENT(EOrientation, Orientation, EOrientation::Horizontal)
		SLATE_STYLE_ARGUMENT(FSeparatorStyle, Style)
		SLATE_ARGUMENT(float, Padding, 4.0f) // space on both sides of the line
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	EOrientation Orientation = EOrientation::Horizontal;
	const FSeparatorStyle* Style = nullptr;
	float Padding = 4.0f;
};

// Shows a fraction; without one it animates back and forth (busy).
class SProgressBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SProgressBar)
		SLATE_STYLE_ARGUMENT(FProgressBarStyle, Style)
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<float>, Percent) // 0..1; unset = busy
		SLATE_OPTIONAL_ATTRIBUTE(FSlateColor, FillColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	void SetPercent(TAttribute<TOptional<float>> InPercent);
	TOptional<float> GetPercent() const { return Percent.Get(); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FProgressBarStyle* Style = nullptr;
	TWidgetAttribute<TOptional<float>, EInvalidateWidgetReason::Paint> Percent{ *this };
	TOptional<FSlateColor> FillColor;
};

// Pulsing dots for "working on it".
class SThrobber : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SThrobber)
		SLATE_ARGUMENT(int32, NumPieces, 3)
		SLATE_ARGUMENT(float, PieceSize, 8.0f)
		SLATE_ARGUMENT(FSlateColor, Color, FSlateColor(EStyleColor::Primary))
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	int32 NumPieces = 3;
	float PieceSize = 8.0f;
	FSlateColor Color;
};
