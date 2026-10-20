// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/SlateStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/WidgetDelegates.h"

enum class EButtonClickMethod : uint8
{
	MouseUp,   // click on release over the button (default)
	MouseDown, // click as soon as it is pressed
};

// A clickable area with content (usually text). Activates with the mouse,
// or with Enter / Space / gamepad A while focused.
class SButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SButton)
		SLATE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)
		SLATE_OPTIONAL_ATTRIBUTE(FString, Text) // makes an STextBlock as the content
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ARGUMENT(EHorizontalAlignment, HAlign, HAlign_Center)
		SLATE_ARGUMENT(EVerticalAlignment, VAlign, VAlign_Center)
		SLATE_OPTIONAL_ATTRIBUTE(FMargin, ContentPadding) // overrides the style's padding
		SLATE_ARGUMENT(EButtonClickMethod, ClickMethod, EButtonClickMethod::MouseUp)
		SLATE_ARGUMENT(bool, IsFocusable, true)
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<FSlateColor>, ForegroundColor)
		SLATE_EVENT(FOnClicked, OnClicked)
		SLATE_EVENT(FSimpleDelegate, OnPressed)
		SLATE_EVENT(FSimpleDelegate, OnReleased)
		SLATE_EVENT(FSimpleDelegate, OnHovered)
		SLATE_EVENT(FSimpleDelegate, OnUnhovered)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	bool IsPressed() const { return bIsPressed; }
	void SetButtonStyle(const FButtonStyle* InStyle);
	void SetContent(const TSharedRef<SWidget>& Content) { ChildSlot[Content]; }
	void SetOnClicked(FOnClicked InOnClicked) { OnClicked = std::move(InOnClicked); }
	// Clicks the button from code.
	FReply SimulateClick();

	bool SupportsKeyboardFocus() const override { return bIsFocusable; }
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseLeave(const FPointerEvent& Event) override;
	void OnMouseCaptureLost() override;
	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnFocusReceived(const FGeometry& Geometry, const FFocusEvent& Event) override;
	void OnFocusLost(const FFocusEvent& Event) override;
	EWindowZone GetWindowZoneOverride() const override { return EWindowZone::ClientArea; }

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

	void UpdatePadding();
	void Press();
	void Release();

	const FButtonStyle* Style = nullptr;
	TOptional<FMargin> ContentPaddingOverride;
	FOnClicked OnClicked;
	FSimpleDelegate OnPressedDelegate;
	FSimpleDelegate OnReleasedDelegate;
	FSimpleDelegate OnHoveredDelegate;
	FSimpleDelegate OnUnhoveredDelegate;
	EButtonClickMethod ClickMethod = EButtonClickMethod::MouseUp;
	bool bIsFocusable = true;
	bool bIsPressed = false;
	bool bKeyPressed = false;
	bool bShowFocus = false; // focus came from the keyboard or code, not a click
};

// A box that toggles between checked and unchecked, with optional content
// (a label) to its right. If IsChecked is bound, the owner keeps the state
// and the box only reports clicks through OnCheckStateChanged.
class SCheckBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCheckBox)
		SLATE_STYLE_ARGUMENT(FCheckBoxStyle, Style)
		SLATE_ATTRIBUTE(ECheckBoxState, IsChecked, ECheckBoxState::Unchecked)
		SLATE_EVENT(FOnCheckStateChanged, OnCheckStateChanged)
		SLATE_OPTIONAL_ATTRIBUTE(FString, Text) // makes an STextBlock label
		SLATE_ARGUMENT(bool, IsFocusable, true)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	ECheckBoxState GetCheckedState() const { return CheckedState.Get(); }
	bool IsChecked() const { return CheckedState.Get() == ECheckBoxState::Checked; }
	void SetIsChecked(TAttribute<ECheckBoxState> State) { CheckedState.Assign(State); }
	void ToggleCheckedState();

	bool SupportsKeyboardFocus() const override { return bIsFocusable; }
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseCaptureLost() override { bIsPressed = false; }
	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnFocusReceived(const FGeometry& Geometry, const FFocusEvent& Event) override;
	void OnFocusLost(const FFocusEvent& Event) override;
	EWindowZone GetWindowZoneOverride() const override { return EWindowZone::ClientArea; }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FCheckBoxStyle* Style = nullptr;
	TWidgetAttribute<ECheckBoxState, EInvalidateWidgetReason::Paint> CheckedState{ *this, ECheckBoxState::Unchecked };
	FOnCheckStateChanged OnCheckStateChanged;
	bool bIsFocusable = true;
	bool bIsPressed = false;
	bool bShowFocus = false;
};
