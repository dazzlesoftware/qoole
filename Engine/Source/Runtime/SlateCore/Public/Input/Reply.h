// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// What input handlers return (Engine/Documentation/Source/GenesisUI.md §7.3): handled or not, plus
// requests the application applies after routing. Widgets never change
// focus, capture or drag state directly.
//
//   return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));

#pragma once

#include "Input/DragAndDrop.h"

class SWidget;

class FReply
{
public:
	static FReply Handled() { return FReply(true); }
	static FReply Unhandled() { return FReply(false); }

	bool IsEventHandled() const { return bHandled; }

	FReply& CaptureMouse(const TSharedRef<SWidget>& Widget) { MouseCaptor = Widget.ToSharedPtr(); bReleaseMouseCapture = false; return *this; }
	FReply& ReleaseMouseCapture() { MouseCaptor.reset(); bReleaseMouseCapture = true; return *this; }

	FReply& SetUserFocus(const TSharedRef<SWidget>& Widget, EFocusCause Cause = EFocusCause::SetDirectly)
	{
		FocusRecipient = Widget.ToSharedPtr();
		FocusCause = Cause;
		bClearFocus = false;
		return *this;
	}
	FReply& ClearUserFocus() { FocusRecipient.reset(); bClearFocus = true; return *this; }

	// Watch for the pointer moving past the drag threshold with Button held;
	// then Widget gets OnDragDetected.
	FReply& DetectDrag(const TSharedRef<SWidget>& Widget, EMouseButton Button) { DragDetectWidget = Widget.ToSharedPtr(); DragDetectButton = Button; return *this; }
	FReply& BeginDragDrop(const TSharedRef<FDragDropOperation>& Operation) { DragDropOperation = Operation.ToSharedPtr(); return *this; }
	FReply& EndDragDrop() { bEndDragDrop = true; return *this; }

	// Captures the mouse, hides and locks the cursor and reports raw device
	// motion (OnRawMouseMove) until capture is released: fly-through and
	// orbit controls in viewports. The cursor comes back where it was.
	FReply& UseHighPrecisionMouseMovement(const TSharedRef<SWidget>& Widget)
	{
		CaptureMouse(Widget);
		bHighPrecision = true;
		return *this;
	}
	bool ShouldUseHighPrecisionMouse() const { return bHighPrecision; }

	// Keeps the cursor inside the widget while captured.
	FReply& LockMouseToWidget(const TSharedRef<SWidget>& Widget) { MouseLockWidget = Widget.ToSharedPtr(); bReleaseMouseLock = false; return *this; }
	FReply& ReleaseMouseLock() { MouseLockWidget.reset(); bReleaseMouseLock = true; return *this; }

	const TSharedPtr<SWidget>& GetMouseCaptor() const { return MouseCaptor; }
	bool ShouldReleaseMouseCapture() const { return bReleaseMouseCapture; }
	const TSharedPtr<SWidget>& GetFocusRecipient() const { return FocusRecipient; }
	EFocusCause GetFocusCause() const { return FocusCause; }
	bool ShouldClearFocus() const { return bClearFocus; }
	const TSharedPtr<SWidget>& GetDetectDragWidget() const { return DragDetectWidget; }
	EMouseButton GetDetectDragButton() const { return DragDetectButton; }
	const TSharedPtr<FDragDropOperation>& GetDragDropOperation() const { return DragDropOperation; }
	bool ShouldEndDragDrop() const { return bEndDragDrop; }
	const TSharedPtr<SWidget>& GetMouseLockWidget() const { return MouseLockWidget; }
	bool ShouldReleaseMouseLock() const { return bReleaseMouseLock; }

private:
	explicit FReply(bool bInHandled) : bHandled(bInHandled) {}

	TSharedPtr<SWidget> MouseCaptor;
	TSharedPtr<SWidget> FocusRecipient;
	TSharedPtr<SWidget> DragDetectWidget;
	TSharedPtr<FDragDropOperation> DragDropOperation;
	TSharedPtr<SWidget> MouseLockWidget;
	EFocusCause FocusCause = EFocusCause::SetDirectly;
	EMouseButton DragDetectButton = EMouseButton::Invalid;
	bool bHandled = false;
	bool bReleaseMouseCapture = false;
	bool bClearFocus = false;
	bool bEndDragDrop = false;
	bool bReleaseMouseLock = false;
	bool bHighPrecision = false;
};

// Answer to SWidget::OnCursorQuery.
class FCursorReply
{
public:
	static FCursorReply Unhandled() { return FCursorReply(); }
	static FCursorReply Cursor(EMouseCursor InCursor)
	{
		FCursorReply R;
		R.MouseCursor = InCursor;
		return R;
	}

	bool IsEventHandled() const { return MouseCursor.has_value(); }
	EMouseCursor GetCursor() const { return MouseCursor.value_or(EMouseCursor::Default); }

private:
	TOptional<EMouseCursor> MouseCursor;
};

// Answer to SWidget::OnNavigation (§7.4): how navigation leaves this widget.
class FNavigationReply
{
public:
	enum class EBoundaryRule : uint8
	{
		Escape,   // not my concern; ask my parent
		Stop,     // navigation stops at my edge (focus stays)
		Wrap,     // search wraps around inside me
		Explicit, // go to a given widget
		Custom,   // ask a function
	};

	using FCustomHandler = TFunction<TSharedPtr<SWidget>(EUINavigation)>;

	static FNavigationReply Escape() { return FNavigationReply(EBoundaryRule::Escape); }
	static FNavigationReply Stop() { return FNavigationReply(EBoundaryRule::Stop); }
	static FNavigationReply Wrap() { return FNavigationReply(EBoundaryRule::Wrap); }
	static FNavigationReply Explicit(const TSharedPtr<SWidget>& Target)
	{
		FNavigationReply R(EBoundaryRule::Explicit);
		R.ExplicitTarget = Target;
		return R;
	}
	static FNavigationReply Custom(FCustomHandler Handler)
	{
		FNavigationReply R(EBoundaryRule::Custom);
		R.CustomHandler = std::move(Handler);
		return R;
	}

	EBoundaryRule GetBoundaryRule() const { return Rule; }
	const TSharedPtr<SWidget>& GetExplicitTarget() const { return ExplicitTarget; }
	const FCustomHandler& GetCustomHandler() const { return CustomHandler; }

private:
	explicit FNavigationReply(EBoundaryRule InRule) : Rule(InRule) {}

	EBoundaryRule Rule = EBoundaryRule::Escape;
	TSharedPtr<SWidget> ExplicitTarget;
	FCustomHandler CustomHandler;
};
