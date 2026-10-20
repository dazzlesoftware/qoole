// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FUIApplication: platform messages, input routing (docs/GenesisUI.md §7.2),
// replies, hover, capture, cursors and drag and drop.

#include "UIApplicationPrivate.h"

using namespace UIApplicationUtil;

namespace
{
	// Widgets that take part in routing: disabled ones are skipped (their
	// enabled ancestors still get the event).
	bool CanReceiveInput(const SWidget& Widget)
	{
		return Widget.IsEnabledInHierarchy();
	}
}

FPointerEvent FUIApplication::MakePointerEvent(FVector2 Position, EMouseButton Button, float WheelDelta) const
{
	return FPointerEvent(Position, LastCursorPosition, PressedButtons, Button, WheelDelta, Platform->GetModifierKeys(), 0);
}

TArray<TSharedRef<SWidget>> FUIApplication::GetHoveredPath() const
{
	TArray<TSharedRef<SWidget>> Path;
	for (const TWeakPtr<SWidget>& Weak : HoveredPath)
	{
		if (const TSharedPtr<SWidget> W = Weak.lock())
		{
			Path.push_back(ToSharedRef(W));
		}
	}
	return Path;
}

// ---------------------------------------------------------------------------
// Replies, capture

void FUIApplication::ProcessReply(const FReply& Reply, const TSharedPtr<SWidget>& Handler, const FPointerEvent* PointerEvent, int32 UserIndex)
{
	(void)Handler;
	if (const TSharedPtr<SWidget>& Captor = Reply.GetMouseCaptor())
	{
		SetMouseCapture(Captor);
		if (Reply.ShouldUseHighPrecisionMouse() && !bHighPrecisionMouse && HasMouseCapture(Captor.get()))
		{
			bHighPrecisionMouse = true;
			ICursor* Cursor = Platform->GetCursor();
			HighPrecisionRestoreScreen = Cursor->GetPosition();
			Cursor->Show(false);
			Platform->SetRawMouseInputEnabled(true);
			// Keep the hidden cursor inside the widget so it never leaves the window.
			if (const TSharedPtr<SWindow> Window = FindWindowForWidget(*Captor); Window && Window->GetNativeWindow())
			{
				const FRect Rect = Captor->GetCachedGeometry().GetLayoutBoundingRect();
				const FIntPoint Origin = Window->GetNativeWindow()->GetClientPosition();
				const FIntRect Screen(Origin.X + FMath::FloorToInt(Rect.Left), Origin.Y + FMath::FloorToInt(Rect.Top),
					Origin.X + FMath::CeilToInt(Rect.Right), Origin.Y + FMath::CeilToInt(Rect.Bottom));
				Cursor->Lock(&Screen);
			}
		}
	}
	else if (Reply.ShouldReleaseMouseCapture())
	{
		ReleaseMouseCapture();
	}

	if (const TSharedPtr<SWidget>& Recipient = Reply.GetFocusRecipient())
	{
		SetUserFocus(UserIndex, Recipient, Reply.GetFocusCause());
	}
	else if (Reply.ShouldClearFocus())
	{
		SetUserFocus(UserIndex, nullptr, EFocusCause::Cleared);
	}

	if (const TSharedPtr<SWidget>& DetectWidget = Reply.GetDetectDragWidget(); DetectWidget && PointerEvent)
	{
		DragDetect = FDragDetect{ DetectWidget, Reply.GetDetectDragButton(), PointerEvent->GetAbsolutePosition(), WindowUnderCursor };
	}
	if (const TSharedPtr<FDragDropOperation>& Operation = Reply.GetDragDropOperation(); Operation && PointerEvent)
	{
		BeginDragDrop(ToSharedRef(Operation), *PointerEvent);
	}
	if (Reply.ShouldEndDragDrop())
	{
		CancelDragDrop();
	}

	if (const TSharedPtr<SWidget>& LockWidget = Reply.GetMouseLockWidget())
	{
		MouseLockWidget = LockWidget;
		if (const TSharedPtr<SWindow> Window = FindWindowForWidget(*LockWidget); Window && Window->GetNativeWindow())
		{
			const FRect Rect = LockWidget->GetCachedGeometry().GetLayoutBoundingRect();
			const FIntPoint Origin = Window->GetNativeWindow()->GetClientPosition();
			const FIntRect Screen(Origin.X + FMath::FloorToInt(Rect.Left), Origin.Y + FMath::FloorToInt(Rect.Top),
				Origin.X + FMath::CeilToInt(Rect.Right), Origin.Y + FMath::CeilToInt(Rect.Bottom));
			Platform->GetCursor()->Lock(&Screen);
		}
	}
	else if (Reply.ShouldReleaseMouseLock() && !MouseLockWidget.expired())
	{
		MouseLockWidget.reset();
		Platform->GetCursor()->Lock(nullptr);
	}
}

bool FUIApplication::HasMouseCapture(const SWidget* Widget) const
{
	return Widget && MouseCaptor.lock().get() == Widget;
}

bool FUIApplication::SetMouseCapture(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget)
	{
		ReleaseMouseCapture();
		return true;
	}
	const TSharedPtr<SWidget> Old = MouseCaptor.lock();
	if (Old == Widget)
	{
		return true;
	}
	const TSharedPtr<SWindow> Window = FindWindowForWidget(*Widget);
	if (!Window)
	{
		return false;
	}
	MouseCaptor = Widget;
	CaptorWindow = Window;
	Platform->SetCapture(Window->GetNativeWindow());
	if (Old)
	{
		Old->OnMouseCaptureLost();
	}
	return true;
}

void FUIApplication::ReleaseMouseCapture()
{
	const TSharedPtr<SWidget> Old = MouseCaptor.lock();
	const bool bHadCapture = Old || !MouseCaptor.expired() || CaptorWindow.lock();
	MouseCaptor.reset();
	CaptorWindow.reset();
	if (bHadCapture)
	{
		Platform->SetCapture(nullptr);
	}
	if (!MouseLockWidget.expired())
	{
		MouseLockWidget.reset();
		Platform->GetCursor()->Lock(nullptr);
	}
	if (bHighPrecisionMouse)
	{
		bHighPrecisionMouse = false;
		ICursor* Cursor = Platform->GetCursor();
		Platform->SetRawMouseInputEnabled(false);
		Cursor->Lock(nullptr);
		Cursor->SetPosition(FMath::RoundToInt(HighPrecisionRestoreScreen.X), FMath::RoundToInt(HighPrecisionRestoreScreen.Y));
		Cursor->Show(true);
	}
	if (Old)
	{
		Old->OnMouseCaptureLost();
	}
}

bool FUIApplication::OnRawMouseMove(int32 DeltaX, int32 DeltaY)
{
	const TSharedPtr<SWidget> Captor = MouseCaptor.lock();
	if (!bHighPrecisionMouse || !Captor)
	{
		return false;
	}
	const FReply Reply = Captor->OnRawMouseMove(FVector2(static_cast<float>(DeltaX), static_cast<float>(DeltaY)));
	ProcessReply(Reply, Captor, nullptr, 0);
	return Reply.IsEventHandled();
}

// ---------------------------------------------------------------------------
// Hover and cursor

void FUIApplication::UpdateHover(const TSharedPtr<SWindow>& Window, const TArray<TSharedRef<SWidget>>& NewPath, const FPointerEvent& Event)
{
	(void)Window;
	const TArray<TWeakPtr<SWidget>> OldPath = HoveredPath;
	// Leave, innermost first.
	for (auto It = OldPath.rbegin(); It != OldPath.rend(); ++It)
	{
		const TSharedPtr<SWidget> W = It->lock();
		if (W && !PathContains(NewPath, W.get()))
		{
			W->SetHovered(false, false);
			W->OnMouseLeave(Event);
		}
	}
	HoveredPath.clear();
	for (SIZE_T I = 0; I < NewPath.size(); ++I)
	{
		const TSharedRef<SWidget>& W = NewPath[I];
		const bool bLeaf = I + 1 == NewPath.size();
		const bool bWasHovered = std::any_of(OldPath.begin(), OldPath.end(), [&W](const TWeakPtr<SWidget>& O) { return O.lock() == W.ToSharedPtr(); });
		W->SetHovered(true, bLeaf);
		if (!bWasHovered)
		{
			W->OnMouseEnter(W->GetCachedGeometry(), Event);
		}
		HoveredPath.push_back(W.ToSharedPtr());
	}
}

void FUIApplication::ApplyCursor()
{
	EMouseCursor Cursor = EMouseCursor::Default;
	bool bFound = false;
	if (DragDropOperation)
	{
		if (const TOptional<EMouseCursor> DragCursor = DragDropOperation->GetCursor())
		{
			Cursor = *DragCursor;
			bFound = true;
		}
	}
	if (!bFound)
	{
		const FPointerEvent Event = MakePointerEvent(CursorPosition, EMouseButton::Invalid, 0.0f);
		TArray<TSharedRef<SWidget>> Path;
		if (const TSharedPtr<SWidget> Captor = MouseCaptor.lock())
		{
			Path = GetPathToWidget(ToSharedRef(Captor));
		}
		else
		{
			Path = GetHoveredPath();
		}
		for (auto It = Path.rbegin(); It != Path.rend(); ++It)
		{
			const FCursorReply Reply = (*It)->OnCursorQuery((*It)->GetCachedGeometry(), Event);
			if (Reply.IsEventHandled())
			{
				Cursor = Reply.GetCursor();
				break;
			}
		}
	}
	if (ICursor* PlatformCursor = Platform->GetCursor(); PlatformCursor && PlatformCursor->GetType() != Cursor)
	{
		PlatformCursor->SetType(Cursor);
	}
}

bool FUIApplication::OnCursorSet()
{
	ApplyCursor();
	return false; // let the platform apply the shape we set
}

void FUIApplication::SynthesizeMouseMove()
{
	const TSharedPtr<SWindow> Window = WindowUnderCursor.lock();
	if (!Window || !Window->GetNativeWindow() || DragDropOperation)
	{
		return;
	}
	const FPointerEvent Event = MakePointerEvent(CursorPosition, EMouseButton::Invalid, 0.0f);
	TArray<TSharedRef<SWidget>> Path = Window->HitTestAt(CursorPosition);
	if (const TSharedPtr<SWidget> Captor = MouseCaptor.lock())
	{
		std::erase_if(Path, [&Captor](const TSharedRef<SWidget>& W) { return &W.Get() != Captor.get() && !W->IsAncestorOf(Captor.get()); });
	}
	UpdateHover(Window, Path, Event);
	ApplyCursor();
}

// ---------------------------------------------------------------------------
// Pointer input

bool FUIApplication::RoutePointerDown(SWindow& Window, const FPointerEvent& Event, bool bDoubleClick)
{
	const TSharedPtr<SWidget> Captor = MouseCaptor.lock();
	FReply Reply = FReply::Unhandled();
	TSharedPtr<SWidget> Handler;
	TArray<TSharedRef<SWidget>> Path;

	if (Captor)
	{
		Path = GetPathToWidget(ToSharedRef(Captor));
		Reply = bDoubleClick ? Captor->OnMouseButtonDoubleClick(Captor->GetCachedGeometry(), Event) : Captor->OnMouseButtonDown(Captor->GetCachedGeometry(), Event);
		if (bDoubleClick && !Reply.IsEventHandled())
		{
			Reply = Captor->OnMouseButtonDown(Captor->GetCachedGeometry(), Event);
		}
		Handler = Captor;
	}
	else
	{
		Path = Window.HitTestAt(Event.GetAbsolutePosition());
		DismissMenusOnClick(Path);

		// Preview: root to leaf.
		for (const TSharedRef<SWidget>& W : Path)
		{
			if (!CanReceiveInput(*W))
			{
				continue;
			}
			Reply = W->OnPreviewMouseButtonDown(W->GetCachedGeometry(), Event);
			if (Reply.IsEventHandled())
			{
				Handler = W.ToSharedPtr();
				break;
			}
		}
		// Bubble: leaf to root.
		if (!Handler)
		{
			for (auto It = Path.rbegin(); It != Path.rend(); ++It)
			{
				const TSharedRef<SWidget>& W = *It;
				if (!CanReceiveInput(*W))
				{
					continue;
				}
				Reply = bDoubleClick ? W->OnMouseButtonDoubleClick(W->GetCachedGeometry(), Event) : W->OnMouseButtonDown(W->GetCachedGeometry(), Event);
				if (bDoubleClick && !Reply.IsEventHandled())
				{
					Reply = W->OnMouseButtonDown(W->GetCachedGeometry(), Event);
				}
				if (Reply.IsEventHandled())
				{
					Handler = W.ToSharedPtr();
					break;
				}
			}
		}
	}

	ProcessReply(Reply, Handler, &Event, 0);

	// Clicking moves focus to the nearest focusable widget at or above the one
	// that handled the click, unless the reply decided otherwise. A click
	// nobody handled on nothing focusable clears focus.
	if (!Captor && !Reply.GetFocusRecipient() && !Reply.ShouldClearFocus())
	{
		auto Start = Path.rbegin();
		if (Handler)
		{
			Start = std::find_if(Path.rbegin(), Path.rend(), [&Handler](const TSharedRef<SWidget>& W) { return W.ToSharedPtr() == Handler; });
		}
		TSharedPtr<SWidget> Focusable;
		for (auto It = Start; It != Path.rend(); ++It)
		{
			if ((*It)->SupportsKeyboardFocus() && CanReceiveInput(**It))
			{
				Focusable = It->ToSharedPtr();
				break;
			}
		}
		if (Focusable)
		{
			if (Focusable != GetKeyboardFocusedWidget())
			{
				SetKeyboardFocus(Focusable, EFocusCause::Mouse);
			}
		}
		else if (!Reply.IsEventHandled())
		{
			ClearKeyboardFocus(EFocusCause::Mouse);
		}
	}
	ApplyCursor();
	return Reply.IsEventHandled();
}

bool FUIApplication::OnMouseDown(const TSharedRef<IPlatformWindow>& Native, EMouseButton Button, FVector2 Position)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	if (!Window)
	{
		return false;
	}
	LastCursorPosition = Window == WindowUnderCursor.lock() ? CursorPosition : Position;
	CursorPosition = Position;
	WindowUnderCursor = Window;
	PressedButtons |= FPointerEvent::ButtonBit(Button);
	CloseToolTip();

	const TSharedPtr<SWindow> Modal = GetActiveModalWindow();
	if (Modal && Modal != Window)
	{
		return true; // blocked by a modal window
	}
	const FPointerEvent Event = MakePointerEvent(Position, Button, 0.0f);
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleMouseButtonDownEvent(*this, Event))
		{
			return true;
		}
	}
	SetActiveWindow(Window);
	return RoutePointerDown(*Window, Event, false);
}

bool FUIApplication::OnMouseDoubleClick(const TSharedRef<IPlatformWindow>& Native, EMouseButton Button, FVector2 Position)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	if (!Window)
	{
		return false;
	}
	CursorPosition = Position;
	WindowUnderCursor = Window;
	PressedButtons |= FPointerEvent::ButtonBit(Button);
	CloseToolTip();
	const TSharedPtr<SWindow> Modal = GetActiveModalWindow();
	if (Modal && Modal != Window)
	{
		return true;
	}
	const FPointerEvent Event = MakePointerEvent(Position, Button, 0.0f);
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleMouseButtonDownEvent(*this, Event))
		{
			return true;
		}
	}
	return RoutePointerDown(*Window, Event, true);
}

bool FUIApplication::OnMouseUp(const TSharedRef<IPlatformWindow>& Native, EMouseButton Button, FVector2 Position)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	if (!Window)
	{
		return false;
	}
	LastCursorPosition = CursorPosition;
	CursorPosition = Position;
	PressedButtons &= static_cast<uint8>(~FPointerEvent::ButtonBit(Button));
	const FPointerEvent Event = MakePointerEvent(Position, Button, 0.0f);
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleMouseButtonUpEvent(*this, Event))
		{
			return true;
		}
	}
	if (DragDetect && DragDetect->Button == Button)
	{
		DragDetect.reset();
	}
	if (DragDropOperation)
	{
		FVector2 Local;
		const TSharedPtr<SWindow> Target = MapDragToWindow(*Window, Position, Local);
		const TArray<TSharedRef<SWidget>> DropPath = Target ? Target->HitTestAt(Local) : TArray<TSharedRef<SWidget>>();
		DropOrCancel(&DropPath, MakePointerEvent(Target ? Local : Position, Button, 0.0f), false);
		SynthesizeMouseMove();
		return true;
	}

	FReply Reply = FReply::Unhandled();
	TSharedPtr<SWidget> Handler;
	if (const TSharedPtr<SWidget> Captor = MouseCaptor.lock())
	{
		Reply = Captor->OnMouseButtonUp(Captor->GetCachedGeometry(), Event);
		Handler = Captor;
	}
	else
	{
		const TSharedPtr<SWindow> Modal = GetActiveModalWindow();
		if (Modal && Modal != Window)
		{
			return true;
		}
		const TArray<TSharedRef<SWidget>> Path = Window->HitTestAt(Position);
		for (auto It = Path.rbegin(); It != Path.rend(); ++It)
		{
			if (!CanReceiveInput(**It))
			{
				continue;
			}
			Reply = (*It)->OnMouseButtonUp((*It)->GetCachedGeometry(), Event);
			if (Reply.IsEventHandled())
			{
				Handler = It->ToSharedPtr();
				break;
			}
		}
	}
	ProcessReply(Reply, Handler, &Event, 0);
	ApplyCursor();
	return Reply.IsEventHandled();
}

bool FUIApplication::OnMouseMove(const TSharedRef<IPlatformWindow>& Native, FVector2 Position)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	if (!Window)
	{
		return false;
	}
	LastCursorPosition = Window == WindowUnderCursor.lock() ? CursorPosition : Position;
	CursorPosition = Position;
	WindowUnderCursor = Window;
	const FPointerEvent Event = MakePointerEvent(Position, EMouseButton::Invalid, 0.0f);
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleMouseMoveEvent(*this, Event))
		{
			return true;
		}
	}

	// A drag starts once the pointer has moved far enough with the button held.
	if (DragDetect && !DragDropOperation)
	{
		const float Threshold = DragTriggerDistance * Window->GetScale();
		if ((Position - DragDetect->StartPosition).SizeSquared() >= Threshold * Threshold)
		{
			const TSharedPtr<SWidget> Source = DragDetect->Widget.lock();
			const FVector2 Start = DragDetect->StartPosition;
			DragDetect.reset();
			if (Source)
			{
				const FPointerEvent DragEvent(Start, Start, PressedButtons, EMouseButton::Invalid, 0.0f, Platform->GetModifierKeys(), 0);
				const FReply Reply = Source->OnDragDetected(Source->GetCachedGeometry(), DragEvent);
				ProcessReply(Reply, Source, &Event, 0);
			}
		}
	}

	if (DragDropOperation)
	{
		// The drag holds the OS capture; find the window under the pointer.
		FVector2 Local;
		const TSharedPtr<SWindow> Target = MapDragToWindow(*Window, Position, Local);
		const FPointerEvent TargetEvent = MakePointerEvent(Target ? Local : Position, EMouseButton::Invalid, 0.0f);
		const TArray<TSharedRef<SWidget>> DragPath = Target ? Target->HitTestAt(Local) : TArray<TSharedRef<SWidget>>();
		UpdateHover(Target, DragPath, TargetEvent);
		UpdateDragDropTargets(DragPath, TargetEvent);
		UpdateDragDecorator();
		DragDropOperation->OnDragged(TargetEvent);
		ApplyCursor();
		return true;
	}

	TArray<TSharedRef<SWidget>> Path = Window->HitTestAt(Position);

	const TSharedPtr<SWindow> Modal = GetActiveModalWindow();
	FReply Reply = FReply::Unhandled();
	TSharedPtr<SWidget> Handler;
	if (const TSharedPtr<SWidget> Captor = MouseCaptor.lock())
	{
		// While captured, only the captor and its ancestors count as hovered.
		std::erase_if(Path, [&Captor](const TSharedRef<SWidget>& W) { return &W.Get() != Captor.get() && !W->IsAncestorOf(Captor.get()); });
		UpdateHover(Window, Path, Event);
		Reply = Captor->OnMouseMove(Captor->GetCachedGeometry(), Event);
		Handler = Captor;
	}
	else if (!Modal || Modal == Window)
	{
		UpdateHover(Window, Path, Event);
		for (auto It = Path.rbegin(); It != Path.rend(); ++It)
		{
			if (!CanReceiveInput(**It))
			{
				continue;
			}
			Reply = (*It)->OnMouseMove((*It)->GetCachedGeometry(), Event);
			if (Reply.IsEventHandled())
			{
				Handler = It->ToSharedPtr();
				break;
			}
		}
	}
	ProcessReply(Reply, Handler, &Event, 0);
	ApplyCursor();
	return true;
}

bool FUIApplication::OnMouseLeave(const TSharedRef<IPlatformWindow>& Native)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	// A drag follows the pointer across windows itself.
	if (!Window || Window != WindowUnderCursor.lock() || MouseCaptor.lock() || DragDropOperation)
	{
		return false;
	}
	const FPointerEvent Event = MakePointerEvent(CursorPosition, EMouseButton::Invalid, 0.0f);
	UpdateHover(Window, {}, Event);
	WindowUnderCursor.reset();
	CloseToolTip();
	ToolTipOwner.reset();
	return true;
}

bool FUIApplication::OnMouseWheel(const TSharedRef<IPlatformWindow>& Native, float Delta, bool bHorizontal, FVector2 Position)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	if (!Window)
	{
		return false;
	}
	CursorPosition = Position;
	CloseToolTip();
	FPointerEvent Event = MakePointerEvent(Position, EMouseButton::Invalid, Delta);
	Event.SetHorizontalWheel(bHorizontal);
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleMouseWheelEvent(*this, Event))
		{
			return true;
		}
	}
	const TSharedPtr<SWindow> Modal = GetActiveModalWindow();
	if (Modal && Modal != Window)
	{
		return true;
	}

	FReply Reply = FReply::Unhandled();
	TSharedPtr<SWidget> Handler;
	const TArray<TSharedRef<SWidget>> Path = MouseCaptor.lock() ? GetPathToWidget(ToSharedRef(MouseCaptor.lock())) : Window->HitTestAt(Position);
	for (auto It = Path.rbegin(); It != Path.rend(); ++It)
	{
		if (!CanReceiveInput(**It))
		{
			continue;
		}
		Reply = (*It)->OnMouseWheel((*It)->GetCachedGeometry(), Event);
		if (Reply.IsEventHandled())
		{
			Handler = It->ToSharedPtr();
			break;
		}
	}
	ProcessReply(Reply, Handler, &Event, 0);
	return Reply.IsEventHandled();
}

// ---------------------------------------------------------------------------
// Drag and drop

TSharedPtr<SWindow> FUIApplication::MapDragToWindow(const SWindow& EventWindow, FVector2 Position, FVector2& OutLocal)
{
	const TSharedPtr<IPlatformWindow>& Native = EventWindow.GetNativeWindow();
	const FVector2 Screen = (Native ? Native->GetClientPosition().ToVector2() : FVector2::Zero()) + Position;
	if (DragDropOperation)
	{
		DragDropOperation->ScreenPosition = Screen;
	}
	// The window the pointer was in last wins ties; then the newest window
	// (floating windows are created on top).
	TArray<TSharedRef<SWindow>> Candidates;
	if (const TSharedPtr<SWindow> Last = WindowUnderCursor.lock())
	{
		Candidates.push_back(ToSharedRef(Last));
	}
	for (auto It = Windows.rbegin(); It != Windows.rend(); ++It)
	{
		Candidates.push_back(*It);
	}
	for (const TSharedRef<SWindow>& Candidate : Candidates)
	{
		const TSharedPtr<IPlatformWindow>& CandidateNative = Candidate->GetNativeWindow();
		if (!CandidateNative || !CandidateNative->IsVisible() || CandidateNative->IsMinimized())
		{
			continue;
		}
		const FVector2 Origin = CandidateNative->GetClientPosition().ToVector2();
		const FVector2 Size = CandidateNative->GetClientSize().ToVector2();
		const FVector2 L = Screen - Origin;
		if (L.X >= 0.0f && L.Y >= 0.0f && L.X < Size.X && L.Y < Size.Y)
		{
			OutLocal = L;
			WindowUnderCursor = Candidate.ToSharedPtr();
			CursorPosition = L;
			return Candidate.ToSharedPtr();
		}
	}
	WindowUnderCursor.reset();
	OutLocal = Position;
	return nullptr;
}

void FUIApplication::BeginDragDrop(const TSharedRef<FDragDropOperation>& Operation, const FPointerEvent& Event)
{
	if (DragDropOperation)
	{
		CancelDragDrop();
	}
	DragDropOperation = Operation.ToSharedPtr();
	DragDetect.reset();
	ReleaseMouseCapture();
	// Keep the OS capture so the drag sees the pointer outside the window
	// (other windows, or nowhere: tabs tear off into new windows).
	if (const TSharedPtr<SWindow> Window = WindowUnderCursor.lock(); Window && Window->GetNativeWindow())
	{
		Operation->ScreenPosition = Window->GetNativeWindow()->GetClientPosition().ToVector2() + CursorPosition;
		Platform->SetCapture(Window->GetNativeWindow());
		bDragHoldsCapture = true;
	}
	CloseToolTip();
	DragDecorator = Operation->GetDefaultDecorator();
	if (DragDecorator)
	{
		DragDecorator->SetVisibility(EVisibility::HitTestInvisible);
	}
	UpdateDragDecorator();
	if (const TSharedPtr<SWindow> Window = WindowUnderCursor.lock())
	{
		UpdateDragDropTargets(Window->HitTestAt(Event.GetAbsolutePosition()), Event);
	}
	ApplyCursor();
}

void FUIApplication::UpdateDragDecorator()
{
	if (!DragDecorator)
	{
		return;
	}
	const TSharedPtr<SWindow> Window = WindowUnderCursor.lock();
	const TSharedPtr<SWindow> Current = DragDecoratorWindow.lock();
	if (Current && Current != Window)
	{
		Current->RemovePopup(*DragDecorator);
		DragDecoratorWindow.reset();
	}
	if (!Window)
	{
		return;
	}
	const FVector2 Position = CursorPosition / Window->GetScale() + DragDropOperation->GetDecoratorOffset();
	if (Window->HasPopup(*DragDecorator))
	{
		Window->MovePopup(*DragDecorator, Position);
	}
	else
	{
		Window->AddPopup(ToSharedRef(DragDecorator), Position, EPopupLayer::DragDecorator);
		DragDecoratorWindow = Window;
	}
}

void FUIApplication::UpdateDragDropTargets(const TArray<TSharedRef<SWidget>>& Path, const FPointerEvent& Event)
{
	if (!DragDropOperation)
	{
		return;
	}
	const FDragDropEvent DragEvent(Event, DragDropOperation);
	for (auto It = DragDropTargets.rbegin(); It != DragDropTargets.rend(); ++It)
	{
		const TSharedPtr<SWidget> W = It->lock();
		if (W && !PathContains(Path, W.get()))
		{
			W->OnDragLeave(DragEvent);
		}
	}
	const TArray<TWeakPtr<SWidget>> Old = DragDropTargets;
	DragDropTargets.clear();
	for (const TSharedRef<SWidget>& W : Path)
	{
		const bool bWasTarget = std::any_of(Old.begin(), Old.end(), [&W](const TWeakPtr<SWidget>& O) { return O.lock() == W.ToSharedPtr(); });
		if (!bWasTarget && CanReceiveInput(*W))
		{
			W->OnDragEnter(W->GetCachedGeometry(), DragEvent);
		}
		DragDropTargets.push_back(W.ToSharedPtr());
	}
	for (auto It = Path.rbegin(); It != Path.rend(); ++It)
	{
		if (CanReceiveInput(**It) && (*It)->OnDragOver((*It)->GetCachedGeometry(), DragEvent).IsEventHandled())
		{
			break;
		}
	}
}

void FUIApplication::DropOrCancel(const TArray<TSharedRef<SWidget>>* Path, const FPointerEvent& Event, bool bCancel)
{
	if (!DragDropOperation)
	{
		return;
	}
	const TSharedPtr<FDragDropOperation> Operation = DragDropOperation;
	const FDragDropEvent DragEvent(Event, Operation);
	bool bHandled = false;
	FReply Reply = FReply::Unhandled();
	TSharedPtr<SWidget> Handler;
	if (!bCancel && Path)
	{
		for (auto It = Path->rbegin(); It != Path->rend(); ++It)
		{
			if (!CanReceiveInput(**It))
			{
				continue;
			}
			Reply = (*It)->OnDrop((*It)->GetCachedGeometry(), DragEvent);
			if (Reply.IsEventHandled())
			{
				bHandled = true;
				Handler = It->ToSharedPtr();
				break;
			}
		}
	}
	for (auto It = DragDropTargets.rbegin(); It != DragDropTargets.rend(); ++It)
	{
		if (const TSharedPtr<SWidget> W = It->lock())
		{
			W->OnDragLeave(DragEvent);
		}
	}
	DragDropTargets.clear();
	if (bDragHoldsCapture)
	{
		bDragHoldsCapture = false;
		Platform->SetCapture(nullptr);
	}
	Operation->bCancelled = bCancel;
	if (DragDecorator)
	{
		if (const TSharedPtr<SWindow> Window = DragDecoratorWindow.lock())
		{
			Window->RemovePopup(*DragDecorator);
		}
		DragDecorator.reset();
		DragDecoratorWindow.reset();
	}
	DragDropOperation.reset();
	Operation->OnDrop(bHandled, Event);
	if (bHandled)
	{
		ProcessReply(Reply, Handler, nullptr, 0);
	}
	ApplyCursor();
}

void FUIApplication::CancelDragDrop()
{
	DropOrCancel(nullptr, MakePointerEvent(CursorPosition, EMouseButton::Invalid, 0.0f), true);
}

// ---------------------------------------------------------------------------
// Keyboard and gamepad

bool FUIApplication::RouteKeyDown(const FKeyEvent& Event)
{
	const int32 UserIndex = Event.GetUserIndex();
	TArray<TSharedRef<SWidget>> Path = GetFocusPath(UserIndex);
	if (Path.empty())
	{
		TSharedPtr<SWindow> Window = GetActiveModalWindow();
		if (!Window)
		{
			Window = ActiveWindow.lock();
		}
		if (Window)
		{
			// Nothing focused: the window and its content (where an editor
			// puts its shortcut scope) see the key.
			Path.push_back(ToSharedRef(StaticCastSharedPtr<SWidget>(Window)));
			Path.push_back(Window->GetContent());
		}
	}

	for (const TSharedRef<SWidget>& W : Path)
	{
		if (!CanReceiveInput(*W))
		{
			continue;
		}
		const FReply Reply = W->OnPreviewKeyDown(W->GetCachedGeometry(), Event);
		if (Reply.IsEventHandled())
		{
			ProcessReply(Reply, W.ToSharedPtr(), nullptr, UserIndex);
			return true;
		}
	}
	for (auto It = Path.rbegin(); It != Path.rend(); ++It)
	{
		if (!CanReceiveInput(**It))
		{
			continue;
		}
		const FReply Reply = (*It)->OnKeyDown((*It)->GetCachedGeometry(), Event);
		if (Reply.IsEventHandled())
		{
			ProcessReply(Reply, It->ToSharedPtr(), nullptr, UserIndex);
			return true;
		}
	}

	// Nobody wanted it: Back closes the innermost menu...
	if (NavigationConfig->GetNavigationActionFromKey(Event) == EUINavigationAction::Back && !MenuStack.empty())
	{
		const TSharedRef<FMenu> Menu = MenuStack.back();
		const TSharedPtr<SWidget> Parent = Menu->GetParentWidget();
		DismissMenuAndChildren(*Menu);
		if (Parent && Parent->SupportsKeyboardFocus() && FindMenuContaining(*Parent))
		{
			SetUserFocus(UserIndex, Parent, EFocusCause::Navigation);
		}
		return true;
	}

	// ...and keys that navigate move focus.
	const EUINavigation Direction = NavigationConfig->GetNavigationDirectionFromKey(Event);
	if (Direction != EUINavigation::Invalid)
	{
		const bool bMoved = AttemptNavigation(Direction, UserIndex, Event.IsGamepad() ? ENavigationGenesis::Controller : ENavigationGenesis::Keyboard, Event.GetModifierKeys());
		// Tab never reaches the OS or anything else.
		return bMoved || Direction == EUINavigation::Next || Direction == EUINavigation::Previous;
	}
	return false;
}

bool FUIApplication::RouteKeyUp(const FKeyEvent& Event)
{
	const TArray<TSharedRef<SWidget>> Path = GetFocusPath(Event.GetUserIndex());
	for (auto It = Path.rbegin(); It != Path.rend(); ++It)
	{
		if (!CanReceiveInput(**It))
		{
			continue;
		}
		const FReply Reply = (*It)->OnKeyUp((*It)->GetCachedGeometry(), Event);
		if (Reply.IsEventHandled())
		{
			ProcessReply(Reply, It->ToSharedPtr(), nullptr, Event.GetUserIndex());
			return true;
		}
	}
	return false;
}

bool FUIApplication::OnKeyDown(EKey Key, uint32 CharacterCode, bool bIsRepeat)
{
	const FKeyEvent Event(Key, Platform->GetModifierKeys(), 0, bIsRepeat, CharacterCode);
	CloseToolTip();
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleKeyDownEvent(*this, Event))
		{
			return true;
		}
	}
	if (DragDropOperation && Key == EKey::Escape)
	{
		CancelDragDrop();
		return true;
	}
	return RouteKeyDown(Event);
}

bool FUIApplication::OnKeyUp(EKey Key, uint32 CharacterCode, bool bIsRepeat)
{
	const FKeyEvent Event(Key, Platform->GetModifierKeys(), 0, bIsRepeat, CharacterCode);
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleKeyUpEvent(*this, Event))
		{
			return true;
		}
	}
	return RouteKeyUp(Event);
}

bool FUIApplication::OnKeyChar(char32_t Character, bool bIsRepeat)
{
	const FCharacterEvent Event(Character, Platform->GetModifierKeys(), 0, bIsRepeat);
	const TArray<TSharedRef<SWidget>> Path = GetFocusPath(0);
	for (auto It = Path.rbegin(); It != Path.rend(); ++It)
	{
		if (!CanReceiveInput(**It))
		{
			continue;
		}
		const FReply Reply = (*It)->OnKeyChar((*It)->GetCachedGeometry(), Event);
		if (Reply.IsEventHandled())
		{
			ProcessReply(Reply, It->ToSharedPtr(), nullptr, 0);
			return true;
		}
	}
	return false;
}

bool FUIApplication::OnGamepadButtonDown(int32 UserIndex, EGamepadButton Button, bool bIsRepeat)
{
	const FKeyEvent Event = FKeyEvent::FromGamepad(Button, Platform->GetModifierKeys(), UserIndex, bIsRepeat);
	CloseToolTip();
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleKeyDownEvent(*this, Event))
		{
			return true;
		}
	}
	return RouteKeyDown(Event);
}

bool FUIApplication::OnGamepadButtonUp(int32 UserIndex, EGamepadButton Button)
{
	const FKeyEvent Event = FKeyEvent::FromGamepad(Button, Platform->GetModifierKeys(), UserIndex, false);
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleKeyUpEvent(*this, Event))
		{
			return true;
		}
	}
	return RouteKeyUp(Event);
}

bool FUIApplication::OnGamepadAnalog(int32 UserIndex, EGamepadAxis Axis, float Value)
{
	for (const TSharedPtr<IInputProcessor>& Processor : TArray<TSharedPtr<IInputProcessor>>(InputProcessors))
	{
		if (Processor->HandleAnalogInputEvent(*this, UserIndex, Axis, Value))
		{
			return true;
		}
	}
	FUIUser& User = GetUser(UserIndex);
	const EUINavigation Direction = NavigationConfig->GetNavigationDirectionFromAnalog(Axis, Value);
	const bool bHorizontalAxis = Axis == EGamepadAxis::LeftX;
	const bool bCurrentIsHorizontal = User.AnalogDirection == EUINavigation::Left || User.AnalogDirection == EUINavigation::Right;
	if (Direction == EUINavigation::Invalid)
	{
		// The stick came back on the axis we were navigating along.
		if (User.AnalogDirection != EUINavigation::Invalid && bHorizontalAxis == bCurrentIsHorizontal && (Axis == EGamepadAxis::LeftX || Axis == EGamepadAxis::LeftY))
		{
			User.AnalogDirection = EUINavigation::Invalid;
		}
		return false;
	}
	if (Direction != User.AnalogDirection)
	{
		User.AnalogDirection = Direction;
		User.NextAnalogRepeat = CurrentTime + NavigationConfig->AnalogRepeatDelay;
		User.bAnalogRepeated = false;
		AttemptNavigation(Direction, UserIndex, ENavigationGenesis::Controller, Platform->GetModifierKeys());
	}
	return true;
}

// ---------------------------------------------------------------------------
// Window events

void FUIApplication::OnSizeChanged(const TSharedRef<IPlatformWindow>& Native, int32 Width, int32 Height, bool bWasMinimized)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	if (!Window || bWasMinimized)
	{
		return;
	}
	Window->SetPixelSize(FIntPoint(Width, Height));
	if (Renderer)
	{
		Renderer->ResizeViewport(Native.Get(), FIntPoint(Width, Height));
	}
	// Redraw right away so the content follows interactive resizing (the
	// main loop is blocked inside the OS's size loop).
	if (!bInTick)
	{
		Window->SetScale(Native->GetDpiScale() * ApplicationScale);
		Window->UpdateLayout();
		PaintWindows(false, Window.get());
	}
}

void FUIApplication::OnDpiScaleChanged(const TSharedRef<IPlatformWindow>& Native, float NewScale)
{
	if (const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get()))
	{
		Window->SetScale(NewScale * ApplicationScale);
	}
}

void FUIApplication::OnWindowActivationChanged(const TSharedRef<IPlatformWindow>& Native, EWindowActivation Activation)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	if (!Window || Window->IsPopupWindow())
	{
		return;
	}
	if (Activation == EWindowActivation::Deactivate)
	{
		// Remember focus for when the window comes back, and let go of it so
		// text fields commit and IME contexts close.
		const TSharedPtr<SWidget> Focused = GetKeyboardFocusedWidget();
		if (Focused && Window->IsAncestorOf(Focused.get()))
		{
			Window->WidgetToFocusOnActivate = Focused;
			ClearKeyboardFocus(EFocusCause::WindowActivate);
		}
		if (const TSharedPtr<SWidget> Captor = MouseCaptor.lock(); Captor && Window->IsAncestorOf(Captor.get()))
		{
			ReleaseMouseCapture();
		}
		CloseToolTip();
		return;
	}
	SetActiveWindow(Window);
	const TSharedPtr<SWidget> Restore = Window->WidgetToFocusOnActivate.lock();
	Window->WidgetToFocusOnActivate.reset();
	if (Restore && Restore->IsVisibleInHierarchy() && !GetKeyboardFocusedWidget())
	{
		SetKeyboardFocus(Restore, EFocusCause::WindowActivate);
	}
}

void FUIApplication::OnApplicationActivationChanged(bool bIsActive)
{
	bAppActive = bIsActive;
	if (!bIsActive)
	{
		DismissAllMenus();
		CloseToolTip();
		CancelDragDrop();
		ReleaseMouseCapture();
		DragDetect.reset();
		PressedButtons = 0;
	}
}

void FUIApplication::OnWindowClose(const TSharedRef<IPlatformWindow>& Native)
{
	if (const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get()))
	{
		const TSharedPtr<SWindow> Modal = GetActiveModalWindow();
		if (!Modal || Modal == Window)
		{
			Window->RequestClose();
		}
	}
}

void FUIApplication::OnOSPaint(const TSharedRef<IPlatformWindow>& Native)
{
	if (bInTick)
	{
		return;
	}
	if (const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get()))
	{
		Window->SetScale(Native->GetDpiScale() * ApplicationScale);
		Window->SetPixelSize(Native->GetClientSize());
		Window->UpdateLayout();
		PaintWindows(false, Window.get());
	}
}

EWindowZone FUIApplication::GetWindowZoneForPoint(const TSharedRef<IPlatformWindow>& Native, FVector2 Position)
{
	const TSharedPtr<SWindow> Window = FindWindowForNative(Native.Get());
	return Window ? Window->GetWindowZoneForPoint(Position) : EWindowZone::Unspecified;
}
