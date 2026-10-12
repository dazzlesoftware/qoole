// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Input/SSlider.h"

void SSlider::Construct(const FArguments& Args)
{
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");
	ValueAttribute.Assign(Args._Value);
	MinValue = Args._MinValue;
	MaxValue = FMath::Max(Args._MaxValue, Args._MinValue);
	StepSize = Args._StepSize;
	bIsFocusable = Args._IsFocusable;
	if (Args._BarColor.IsSet())
	{
		BarColor = Args._BarColor.Get();
	}
	OnValueChanged = Args._OnValueChanged;
	OnMouseCaptureBegin = Args._OnMouseCaptureBegin;
	OnMouseCaptureEnd = Args._OnMouseCaptureEnd;
}

float SSlider::Fraction() const
{
	const float Range = MaxValue - MinValue;
	return Range > 0.0f ? FMath::Clamp((ValueAttribute.Get() - MinValue) / Range, 0.0f, 1.0f) : 0.0f;
}

void SSlider::CommitValue(float NewValue)
{
	NewValue = FMath::Clamp(NewValue, MinValue, MaxValue);
	if (NewValue == ValueAttribute.Get())
	{
		return;
	}
	if (!ValueAttribute.IsBound())
	{
		ValueAttribute.Set(NewValue);
	}
	OnValueChanged.ExecuteIfBound(NewValue);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSlider::SetFromPosition(const FGeometry& Geometry, FVector2 AbsolutePosition)
{
	const float Half = Style->ThumbSize * 0.5f;
	const float Usable = FMath::Max(1.0f, Geometry.Size.X - Style->ThumbSize);
	const float T = FMath::Clamp((Geometry.AbsoluteToLocal(AbsolutePosition).X - Half) / Usable, 0.0f, 1.0f);
	CommitValue(MinValue + T * (MaxValue - MinValue));
}

FVector2 SSlider::ComputeDesiredSize(float) const
{
	return FVector2(Style->ThumbSize * 4.0f, Style->ThumbSize + 4.0f);
}

FReply SSlider::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left)
	{
		return FReply::Unhandled();
	}
	bDragging = true;
	OnMouseCaptureBegin.ExecuteIfBound();
	SetFromPosition(Geometry, Event.GetAbsolutePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSlider::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	SetFromPosition(Geometry, Event.GetAbsolutePosition());
	return FReply::Handled();
}

FReply SSlider::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (!bDragging || Event.GetEffectingButton() != EMouseButton::Left)
	{
		return FReply::Unhandled();
	}
	return FReply::Handled().ReleaseMouseCapture(); // OnMouseCaptureLost ends the drag
}

void SSlider::OnMouseCaptureLost()
{
	if (bDragging)
	{
		bDragging = false;
		OnMouseCaptureEnd.ExecuteIfBound();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SSlider::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	const bool bLeft = Event.GetKey() == EKey::Left || Event.GetGamepadButton() == EGamepadButton::DPadLeft;
	const bool bRight = Event.GetKey() == EKey::Right || Event.GetGamepadButton() == EGamepadButton::DPadRight;
	if (!bLeft && !bRight)
	{
		return FReply::Unhandled();
	}
	OnMouseCaptureBegin.ExecuteIfBound();
	CommitValue(ValueAttribute.Get() + (bRight ? StepSize : -StepSize));
	OnMouseCaptureEnd.ExecuteIfBound();
	return FReply::Handled();
}

FNavigationReply SSlider::OnNavigation(const FGeometry&, const FNavigationEvent& Event)
{
	// Left and right adjust the value instead of moving focus.
	const EUINavigation Type = Event.GetNavigationType();
	return Type == EUINavigation::Left || Type == EUINavigation::Right ? FNavigationReply::Stop() : FNavigationReply::Escape();
}

FReply SSlider::OnFocusReceived(const FGeometry&, const FFocusEvent& Event)
{
	bShowFocus = Event.GetCause() != EFocusCause::Mouse;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SSlider::OnFocusLost(const FFocusEvent&)
{
	bShowFocus = false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SSlider::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect&, FDrawElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	FColor Tint = InWidgetStyle.ColorAndOpacityTint;
	if (!Args.bEnabled)
	{
		Tint.A = uint8(Tint.A / 2);
	}
	const float Half = Style->ThumbSize * 0.5f;
	const float CenterY = Geometry.Size.Y * 0.5f;
	const float BarTop = CenterY - Style->BarThickness * 0.5f;
	const FRect Bar(Half, BarTop, Geometry.Size.X - Half, BarTop + Style->BarThickness);
	Style->Bar.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(Bar), Tint);
	const float ThumbX = Half + Fraction() * FMath::Max(0.0f, Geometry.Size.X - Style->ThumbSize);
	const FColor FillTint = BarColor ? FWidgetStyle::Multiply(Tint, BarColor->Resolve()) : Tint;
	Style->BarFill.Paint(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(FRect(Bar.Left, Bar.Top, ThumbX, Bar.Bottom)), FillTint);
	const FRect Thumb(ThumbX - Half, CenterY - Half, ThumbX + Half, CenterY + Half);
	(IsHovered() || bDragging ? Style->ThumbHovered : Style->Thumb).Paint(OutDrawElements, LayerId + 2, Geometry.ToPaintGeometry(Thumb), Tint);
	if (bShowFocus && HasKeyboardFocus())
	{
		Style->Focused.Paint(OutDrawElements, LayerId + 3, Geometry.ToPaintGeometry(Thumb.Expand(FMargin(2.0f))), Tint);
	}
	return LayerId + 3;
}
