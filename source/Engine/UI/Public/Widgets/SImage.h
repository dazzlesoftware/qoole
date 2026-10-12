// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Display widgets: images, separators, progress bars, throbbers.

#pragma once

#include "Styling/StyleSet.h"
#include "Widgets/SLeafWidget.h"

class SImage : public SLeafWidget
{
public:
	GE_BEGIN_ARGS(SImage)
		GE_STYLE_ARGUMENT(FStyleBrush, Image)
		GE_ATTRIBUTE(FStyleColor, ColorAndOpacity, FStyleColor(FColor::White()))
		GE_OPTIONAL_ATTRIBUTE(FVector2, DesiredSizeOverride)
	GE_END_ARGS()

	void Construct(const FArguments& Args);
	void SetImage(const FStyleBrush* InImage);
	void SetColorAndOpacity(TAttribute<FStyleColor> Color) { ColorAndOpacity.Assign(Color); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FStyleBrush* Image = nullptr;
	TWidgetAttribute<FStyleColor, EInvalidateWidgetReason::Paint> ColorAndOpacity{ *this, FStyleColor(FColor::White()) };
	TOptional<FVector2> DesiredSizeOverride;
};

// A thin line between groups of widgets.
class SSeparator : public SLeafWidget
{
public:
	GE_BEGIN_ARGS(SSeparator)
		GE_ARGUMENT(EOrientation, Orientation, EOrientation::Horizontal)
		GE_STYLE_ARGUMENT(FSeparatorStyle, Style)
		GE_ARGUMENT(float, Padding, 4.0f) // space on both sides of the line
	GE_END_ARGS()

	void Construct(const FArguments& Args);

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
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
	GE_BEGIN_ARGS(SProgressBar)
		GE_STYLE_ARGUMENT(FProgressBarStyle, Style)
		GE_OPTIONAL_ATTRIBUTE(TOptional<float>, Percent) // 0..1; unset = busy
		GE_OPTIONAL_ATTRIBUTE(FStyleColor, FillColor)
	GE_END_ARGS()

	void Construct(const FArguments& Args);
	void SetPercent(TAttribute<TOptional<float>> InPercent);
	TOptional<float> GetPercent() const { return Percent.Get(); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FProgressBarStyle* Style = nullptr;
	TWidgetAttribute<TOptional<float>, EInvalidateWidgetReason::Paint> Percent{ *this };
	TOptional<FStyleColor> FillColor;
};

// Pulsing dots for "working on it".
class SThrobber : public SLeafWidget
{
public:
	GE_BEGIN_ARGS(SThrobber)
		GE_ARGUMENT(int32, NumPieces, 3)
		GE_ARGUMENT(float, PieceSize, 8.0f)
		GE_ARGUMENT(FStyleColor, Color, FStyleColor(EStyleColor::Primary))
	GE_END_ARGS()

	void Construct(const FArguments& Args);

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	int32 NumPieces = 3;
	float PieceSize = 8.0f;
	FStyleColor Color;
};
