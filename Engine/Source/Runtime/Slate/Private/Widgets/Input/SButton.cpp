// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Input/SButton.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	bool IsAcceptKey(const FKeyEvent& Event)
	{
		return FSlateApplication::Get().GetNavigationConfig().GetNavigationActionFromKey(Event) == EUINavigationAction::Accept;
	}
}

// ---------------------------------------------------------------------------
// SButton

void SButton::Construct(const FArguments& Args)
{
	Style = Args._ButtonStyle ? Args._ButtonStyle : &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
	if (Args._ContentPadding.IsSet())
	{
		ContentPaddingOverride = Args._ContentPadding.Get();
	}
	ClickMethod = Args._ClickMethod;
	bIsFocusable = Args._IsFocusable;
	OnClicked = Args._OnClicked;
	OnPressedDelegate = Args._OnPressed;
	OnReleasedDelegate = Args._OnReleased;
	OnHoveredDelegate = Args._OnHovered;
	OnUnhoveredDelegate = Args._OnUnhovered;
	ForegroundColor.Assign(Args._ForegroundColor);

	ChildSlot.HAlign(Args._HAlign).VAlign(Args._VAlign);
	if (Args._Content.Widget)
	{
		ChildSlot[ToSharedRef(Args._Content.Widget)];
	}
	else if (Args._Text.IsSet())
	{
		ChildSlot[SNew(STextBlock).Text(Args._Text).TextStyle(Args._TextStyle)];
	}
	UpdatePadding();
}

void SButton::SetButtonStyle(const FButtonStyle* InStyle)
{
	Style = InStyle ? InStyle : &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
	UpdatePadding();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SButton::UpdatePadding()
{
	if (ContentPaddingOverride)
	{
		ChildSlot.Padding(*ContentPaddingOverride);
	}
	else
	{
		ChildSlot.Padding(bIsPressed ? Style->PressedPadding : Style->NormalPadding);
	}
}

void SButton::Press()
{
	if (!bIsPressed)
	{
		bIsPressed = true;
		UpdatePadding();
		Invalidate(EInvalidateWidgetReason::Paint);
		OnPressedDelegate.ExecuteIfBound();
	}
}

void SButton::Release()
{
	if (bIsPressed)
	{
		bIsPressed = false;
		UpdatePadding();
		Invalidate(EInvalidateWidgetReason::Paint);
		OnReleasedDelegate.ExecuteIfBound();
	}
}

FReply SButton::SimulateClick()
{
	if (!IsEnabledInHierarchy())
	{
		return FReply::Unhandled();
	}
	return OnClicked.IsBound() ? OnClicked.Execute() : FReply::Handled();
}

FReply SButton::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left)
	{
		return FReply::Unhandled();
	}
	Press();
	if (ClickMethod == EButtonClickMethod::MouseDown)
	{
		FReply Reply = SimulateClick();
		Release();
		return Reply.IsEventHandled() ? Reply : FReply::Handled();
	}
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SButton::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return OnMouseButtonDown(Geometry, Event);
}

FReply SButton::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left || !bIsPressed)
	{
		return FReply::Unhandled();
	}
	Release();
	FReply Reply = FReply::Handled();
	if (Geometry.IsUnderLocation(Event.GetAbsolutePosition()))
	{
		const FReply ClickReply = SimulateClick();
		if (ClickReply.IsEventHandled())
		{
			Reply = ClickReply;
		}
	}
	if (HasMouseCapture())
	{
		Reply.ReleaseMouseCapture();
	}
	return Reply;
}

void SButton::OnMouseEnter(const FGeometry&, const FPointerEvent&)
{
	OnHoveredDelegate.ExecuteIfBound();
}

void SButton::OnMouseLeave(const FPointerEvent&)
{
	OnUnhoveredDelegate.ExecuteIfBound();
}

void SButton::OnMouseCaptureLost()
{
	Release();
}

FReply SButton::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (!IsAcceptKey(Event))
	{
		return FReply::Unhandled();
	}
	if (!Event.IsRepeat())
	{
		bKeyPressed = true;
		Press();
	}
	return FReply::Handled();
}

FReply SButton::OnKeyUp(const FGeometry&, const FKeyEvent& Event)
{
	if (!bKeyPressed || !IsAcceptKey(Event))
	{
		return FReply::Unhandled();
	}
	bKeyPressed = false;
	Release();
	const FReply Reply = SimulateClick();
	return Reply.IsEventHandled() ? Reply : FReply::Handled();
}

FReply SButton::OnFocusReceived(const FGeometry&, const FFocusEvent& Event)
{
	bShowFocus = Event.GetCause() != EFocusCause::Mouse;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SButton::OnFocusLost(const FFocusEvent&)
{
	bShowFocus = false;
	if (bKeyPressed)
	{
		bKeyPressed = false;
		Release();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SButton::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const bool bHovered = IsHovered() && Args.bEnabled;
	const FSlateBrush& Brush = !Args.bEnabled ? Style->Disabled : (bIsPressed ? Style->Pressed : (bHovered ? Style->Hovered : Style->Normal));
	Brush.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);

	FWidgetStyle ChildStyle = MakeChildStyle(InWidgetStyle);
	if (!GetForegroundColor())
	{
		const FSlateColor& Foreground = !Args.bEnabled ? Style->DisabledForeground
			: (bIsPressed ? Style->PressedForeground : (bHovered ? Style->HoveredForeground : Style->NormalForeground));
		ChildStyle.SetForegroundColor(Foreground.Resolve());
	}
	int32 MaxLayer = PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, ChildStyle);
	if (bShowFocus && HasKeyboardFocus())
	{
		Style->Focused.Paint(OutDrawElements, ++MaxLayer, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
	}
	return MaxLayer;
}

// ---------------------------------------------------------------------------
// SCheckBox

void SCheckBox::Construct(const FArguments& Args)
{
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("CheckBox");
	CheckedState.Assign(Args._IsChecked);
	OnCheckStateChanged = Args._OnCheckStateChanged;
	bIsFocusable = Args._IsFocusable;
	ChildSlot.VAlign(VAlign_Center).Padding(FMargin(Style->BoxSize + Style->Spacing, 0.0f, 0.0f, 0.0f));
	if (Args._Content.Widget)
	{
		ChildSlot[ToSharedRef(Args._Content.Widget)];
	}
	else if (Args._Text.IsSet())
	{
		ChildSlot[SNew(STextBlock).Text(Args._Text)];
	}
	else
	{
		ChildSlot.Padding(FMargin(Style->BoxSize, 0.0f, 0.0f, 0.0f));
	}
}

void SCheckBox::ToggleCheckedState()
{
	const ECheckBoxState NewState = CheckedState.Get() == ECheckBoxState::Checked ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
	if (!CheckedState.IsBound())
	{
		CheckedState.Set(NewState);
	}
	OnCheckStateChanged.ExecuteIfBound(NewState);
}

FVector2 SCheckBox::ComputeDesiredSize(float LayoutScale) const
{
	const FVector2 Size = SCompoundWidget::ComputeDesiredSize(LayoutScale);
	return FVector2(Size.X, FMath::Max(Size.Y, Style->BoxSize));
}

FReply SCheckBox::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left)
	{
		return FReply::Unhandled();
	}
	bIsPressed = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SCheckBox::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left || !bIsPressed)
	{
		return FReply::Unhandled();
	}
	bIsPressed = false;
	if (Geometry.IsUnderLocation(Event.GetAbsolutePosition()))
	{
		ToggleCheckedState();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SCheckBox::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (!IsAcceptKey(Event) || Event.IsRepeat())
	{
		return FReply::Unhandled();
	}
	ToggleCheckedState();
	return FReply::Handled();
}

FReply SCheckBox::OnFocusReceived(const FGeometry&, const FFocusEvent& Event)
{
	bShowFocus = Event.GetCause() != EFocusCause::Mouse;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SCheckBox::OnFocusLost(const FFocusEvent&)
{
	bShowFocus = false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SCheckBox::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const float Box = Style->BoxSize;
	const FRect BoxRect(0.0f, (Geometry.Size.Y - Box) * 0.5f, Box, (Geometry.Size.Y + Box) * 0.5f);
	const ECheckBoxState State = CheckedState.Get();
	const bool bHovered = IsHovered() && Args.bEnabled;
	const FSlateBrush& Brush = !Args.bEnabled ? Style->BoxDisabled
		: (State != ECheckBoxState::Unchecked ? (bHovered ? Style->BoxCheckedHovered : Style->BoxChecked) : (bHovered ? Style->BoxHovered : Style->Box));
	Brush.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(BoxRect), InWidgetStyle.ColorAndOpacityTint);

	const FColor Mark = InWidgetStyle.Tinted(Style->CheckColor.Resolve());
	const FPaintGeometry MarkGeometry = Geometry.ToPaintGeometry(BoxRect);
	if (State == ECheckBoxState::Checked)
	{
		OutDrawElements.AddLines(LayerId + 1, MarkGeometry, { FVector2(Box * 0.24f, Box * 0.52f), FVector2(Box * 0.43f, Box * 0.71f), FVector2(Box * 0.77f, Box * 0.31f) }, Mark, 2.0f);
	}
	else if (State == ECheckBoxState::Undetermined)
	{
		OutDrawElements.AddLines(LayerId + 1, MarkGeometry, { FVector2(Box * 0.25f, Box * 0.5f), FVector2(Box * 0.75f, Box * 0.5f) }, Mark, 2.0f);
	}
	if (bShowFocus && HasKeyboardFocus())
	{
		Style->Focused.Paint(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(FRect(BoxRect.Left - 2.0f, BoxRect.Top - 2.0f, BoxRect.Right + 2.0f, BoxRect.Bottom + 2.0f)),
			InWidgetStyle.ColorAndOpacityTint);
	}

	FWidgetStyle ChildStyle = MakeChildStyle(InWidgetStyle);
	ChildStyle.SetForegroundColor((Args.bEnabled ? Style->Foreground : Style->DisabledForeground).Resolve());
	return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, ChildStyle);
}
