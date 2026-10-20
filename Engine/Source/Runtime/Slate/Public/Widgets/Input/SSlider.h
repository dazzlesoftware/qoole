// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/SlateStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/WidgetDelegates.h"

// A horizontal slider between MinValue and MaxValue. Drag the thumb or click
// the bar; Left/Right (or the D-pad) step by StepSize while focused.
// OnMouseCaptureBegin/End bracket a drag (one undo transaction).
class SSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSlider)
		SLATE_STYLE_ARGUMENT(FSliderStyle, Style)
		SLATE_ATTRIBUTE(float, Value, 0.0f)
		SLATE_ARGUMENT(float, MinValue, 0.0f)
		SLATE_ARGUMENT(float, MaxValue, 1.0f)
		SLATE_ARGUMENT(float, StepSize, 0.01f)
		SLATE_ARGUMENT(bool, IsFocusable, true)
		SLATE_OPTIONAL_ATTRIBUTE(FSlateColor, BarColor) // tints the filled part
		SLATE_EVENT(FOnFloatValueChanged, OnValueChanged)
		SLATE_EVENT(FSimpleDelegate, OnMouseCaptureBegin)
		SLATE_EVENT(FSimpleDelegate, OnMouseCaptureEnd)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	float GetValue() const { return ValueAttribute.Get(); }
	void SetValue(TAttribute<float> InValue) { ValueAttribute.Assign(InValue); }

	bool SupportsKeyboardFocus() const override { return bIsFocusable; }
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseCaptureLost() override;
	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FNavigationReply OnNavigation(const FGeometry& Geometry, const FNavigationEvent& Event) override;
	FReply OnFocusReceived(const FGeometry& Geometry, const FFocusEvent& Event) override;
	void OnFocusLost(const FFocusEvent& Event) override;
	EWindowZone GetWindowZoneOverride() const override { return EWindowZone::ClientArea; }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	float Fraction() const;
	void SetFromPosition(const FGeometry& Geometry, FVector2 AbsolutePosition);
	void CommitValue(float NewValue);

	const FSliderStyle* Style = nullptr;
	TWidgetAttribute<float, EInvalidateWidgetReason::Paint> ValueAttribute{ *this, 0.0f };
	TOptional<FSlateColor> BarColor;
	float MinValue = 0.0f;
	float MaxValue = 1.0f;
	float StepSize = 0.01f;
	bool bIsFocusable = true;
	bool bDragging = false;
	bool bShowFocus = false;
	FOnFloatValueChanged OnValueChanged;
	FSimpleDelegate OnMouseCaptureBegin;
	FSimpleDelegate OnMouseCaptureEnd;
};
