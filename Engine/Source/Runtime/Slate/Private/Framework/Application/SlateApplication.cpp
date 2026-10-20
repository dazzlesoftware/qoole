// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FSlateApplication: setup, windows, the frame loop, focus and navigation,
// menus and tooltips. Platform input and routing are in
// SlateApplicationInput.cpp.

#include "SlateApplicationPrivate.h"

#include "Widgets/SToolTip.h"

DEFINE_LOG_CATEGORY(LogSlate, Log);

using namespace UIApplicationUtil;

namespace
{
	TSharedPtr<FSlateApplication>& Instance()
	{
		static TSharedPtr<FSlateApplication> App;
		return App;
	}

	constexpr int32 MaxUsers = 8;
}

// ---------------------------------------------------------------------------
// SMenuHost

void SMenuHost::Construct(const FArguments& Args)
{
	Style = &FAppStyle::Get().GetWidgetStyle<FMenuStyle>("Menu");
	ChildSlot.Padding(Style->Padding);
	if (Args._Content.Widget)
	{
		ChildSlot[ToSharedRef(Args._Content.Widget)];
	}
}

FNavigationReply SMenuHost::OnNavigation(const FGeometry&, const FNavigationEvent& Event)
{
	switch (Event.GetNavigationType())
	{
	case EUINavigation::Up:
	case EUINavigation::Down:
	case EUINavigation::Next:
	case EUINavigation::Previous:
		return FNavigationReply::Wrap();
	default:
		return FNavigationReply::Stop();
	}
}

FReply SMenuHost::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (Event.GetKey() != EKey::Left)
	{
		return FReply::Unhandled();
	}
	// Left closes a submenu and returns to the entry that opened it.
	FSlateApplication& App = FSlateApplication::Get();
	const TSharedPtr<IMenu> Menu = App.FindMenuContaining(*this);
	const TSharedPtr<SWidget> Parent = Menu ? Menu->GetParentWidget() : nullptr;
	if (Menu && Parent && App.FindMenuContaining(*Parent))
	{
		App.DismissMenuAndChildren(*Menu);
		App.SetKeyboardFocus(Parent, EFocusCause::Navigation);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMenuHost::OnMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
	// Clicks on the menu background do nothing (and do not reach the window).
	return FReply::Handled();
}

int32 SMenuHost::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	Style->Background.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
	return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, MakeChildStyle(InWidgetStyle));
}

// ---------------------------------------------------------------------------
// Setup

TSharedRef<FSlateApplication> FSlateApplication::Create(const TSharedRef<IPlatformApplication>& Platform, const TSharedRef<FSlateFontCache>& FontCache)
{
	checkf(!Instance(), "Only one FSlateApplication may exist");
	auto App = std::make_shared<FSlateApplication>(Platform, FontCache);
	Instance() = App;
	SetCurrent(App.get());
	Platform->SetMessageHandler(TSharedRef<IPlatformMessageHandler>(StaticCastSharedPtr<IPlatformMessageHandler>(App)));
	return TSharedRef<FSlateApplication>(App);
}

FSlateApplication& FSlateApplication::Get()
{
	check(Instance());
	return *Instance();
}

void FSlateApplication::Shutdown()
{
	TSharedPtr<FSlateApplication>& App = Instance();
	if (!App)
	{
		return;
	}
	App->DismissAllMenus();
	App->CloseToolTip();
	for (const TSharedRef<SWindow>& Window : TArray<TSharedRef<SWindow>>(App->Windows))
	{
		App->RequestDestroyWindow(Window);
	}
	App->DestroyPendingWindows();
	App->Platform->SetMessageHandler(MakeShared<IPlatformMessageHandler>());
	SetCurrent(nullptr);
	App.reset();
}

FSlateApplication::FSlateApplication(const TSharedRef<IPlatformApplication>& InPlatform, const TSharedRef<FSlateFontCache>& InFontCache)
	: Platform(InPlatform.ToSharedPtr())
	, FontCache(InFontCache.ToSharedPtr())
	, NavigationConfig(std::make_shared<FNavigationConfig>())
{
	Users.resize(MaxUsers);
	for (int32 I = 0; I < MaxUsers; ++I)
	{
		Users[I].UserIndex = I;
	}
	CurrentTime = LastTickTime = ReadClock();
}

FSlateApplication::~FSlateApplication() = default;

void FSlateApplication::SetRenderer(FSlateRenderer* InRenderer)
{
	Renderer = InRenderer;
	if (Renderer)
	{
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			if (Window->GetNativeWindow())
			{
				Renderer->CreateViewport(ToSharedRef(Window->GetNativeWindow()));
			}
		}
	}
}

void FSlateApplication::SetApplicationScale(float InScale)
{
	ApplicationScale = FMath::Clamp(InScale, 0.25f, 8.0f);
	for (const TSharedRef<SWindow>& Window : Windows)
	{
		if (Window->GetNativeWindow())
		{
			Window->SetScale(Window->GetNativeWindow()->GetDpiScale() * ApplicationScale);
		}
	}
}

void FSlateApplication::SetTimeSource(TFunction<double()> Source)
{
	TimeSource = std::move(Source);
	CurrentTime = LastTickTime = ReadClock();
}

double FSlateApplication::ReadClock() const
{
	return TimeSource ? TimeSource() : FPlatformTime::Seconds();
}

FSlateUser& FSlateApplication::GetUser(int32 UserIndex)
{
	return Users[FMath::Clamp(UserIndex, 0, MaxUsers - 1)];
}

const FSlateUser* FSlateApplication::FindUser(int32 UserIndex) const
{
	return UserIndex >= 0 && UserIndex < MaxUsers ? &Users[UserIndex] : nullptr;
}

TSharedRef<IToolTip> FSlateApplication::MakeToolTip(const TAttribute<FString>& Text)
{
	const TSharedRef<SToolTip> ToolTip = SNew(SToolTip).Text(Text);
	return TSharedRef<IToolTip>(std::static_pointer_cast<IToolTip>(ToolTip.ToSharedPtr()));
}

void FSlateApplication::RegisterInputPreProcessor(const TSharedRef<IInputProcessor>& Processor)
{
	InputProcessors.push_back(Processor.ToSharedPtr());
}

void FSlateApplication::UnregisterInputPreProcessor(const TSharedRef<IInputProcessor>& Processor)
{
	ArrayUtil::Remove(InputProcessors, Processor.ToSharedPtr());
}

// ---------------------------------------------------------------------------
// Windows

TSharedRef<SWindow> FSlateApplication::AddWindow(const TSharedRef<SWindow>& Window, bool bShowImmediately)
{
	const TSharedRef<IPlatformWindow> Native = Platform->MakeWindow(Window->MakeWindowDesc());
	Window->SetNativeWindow(Native);
	Window->SetScale(Native->GetDpiScale() * ApplicationScale);
	Windows.push_back(Window);
	if (Renderer)
	{
		Renderer->CreateViewport(Native);
	}
	// Lay out before the first paint the OS asks for.
	Window->UpdateLayout();
	if (bShowImmediately)
	{
		Native->Show();
		if (!Window->IsPopupWindow())
		{
			SetActiveWindow(Window.ToSharedPtr());
		}
	}
	return Window;
}

TSharedPtr<SWindow> FSlateApplication::GetActiveModalWindow() const
{
	for (auto It = Windows.rbegin(); It != Windows.rend(); ++It)
	{
		if ((*It)->IsModal() && !(*It)->IsClosed())
		{
			return It->ToSharedPtr();
		}
	}
	return nullptr;
}

void FSlateApplication::AddModalWindow(const TSharedRef<SWindow>& Window, const TSharedPtr<SWindow>& Parent)
{
	Window->SetIsModal(true);
	DismissAllMenus();
	CloseToolTip();
	ReleaseMouseCapture();
	const TSharedPtr<SWidget> PreviousFocus = GetKeyboardFocusedWidget();
	AddWindow(Window);
	for (const TSharedRef<SWindow>& Other : Windows)
	{
		if (Other != Window && Other->GetNativeWindow())
		{
			Other->GetNativeWindow()->Enable(false);
		}
	}
	++ModalDepth;
	while (!Window->IsClosed() && !bQuitRequested)
	{
		Tick();
		if (!Window->IsClosed() && !IsActive())
		{
			Platform->WaitForMessages(static_cast<float>(ComputeIdleTimeout()));
		}
	}
	--ModalDepth;

	// Re-enable what this modal disabled; an outer modal keeps its own.
	const TSharedPtr<SWindow> OuterModal = GetActiveModalWindow();
	for (const TSharedRef<SWindow>& Other : Windows)
	{
		if (Other->GetNativeWindow())
		{
			Other->GetNativeWindow()->Enable(!OuterModal || Other == ToSharedRef(OuterModal));
		}
	}
	if (Parent && Parent->GetNativeWindow())
	{
		Parent->GetNativeWindow()->BringToFront();
		SetActiveWindow(Parent);
	}
	if (PreviousFocus && PreviousFocus->IsVisibleInHierarchy())
	{
		SetKeyboardFocus(PreviousFocus, EFocusCause::WindowActivate);
	}
}

void FSlateApplication::RequestDestroyWindow(const TSharedRef<SWindow>& Window)
{
	if (!ArrayUtil::Contains(WindowsToDestroy, Window))
	{
		WindowsToDestroy.push_back(Window);
	}
	if (!bInTick)
	{
		DestroyPendingWindows();
	}
}

void FSlateApplication::DestroyPendingWindows()
{
	while (!WindowsToDestroy.empty())
	{
		const TSharedRef<SWindow> Window = WindowsToDestroy.front();
		WindowsToDestroy.erase(WindowsToDestroy.begin());
		if (!ArrayUtil::Contains(Windows, Window))
		{
			continue;
		}

		// Let go of everything that points into the window.
		for (const TSharedRef<FMenu>& Menu : TArray<TSharedRef<FMenu>>(MenuStack))
		{
			if (Menu->GetOwnerWindow() == Window.ToSharedPtr() && !Menu->IsDismissed())
			{
				DismissMenuAndChildren(*Menu);
			}
		}
		if (ToolTipWindow.lock() == Window.ToSharedPtr())
		{
			CloseToolTip();
		}
		if (DragDecoratorWindow.lock() == Window.ToSharedPtr())
		{
			CancelDragDrop();
		}
		if (const TSharedPtr<SWidget> Captor = MouseCaptor.lock(); Captor && Window->IsAncestorOf(Captor.get()))
		{
			ReleaseMouseCapture();
		}
		for (FSlateUser& User : Users)
		{
			if (const TSharedPtr<SWidget> Focused = User.FocusedWidget.lock(); Focused && Window->IsAncestorOf(Focused.get()))
			{
				SetUserFocus(User.UserIndex, nullptr, EFocusCause::OtherWidgetLostFocus);
			}
		}

		ArrayUtil::Remove(Windows, Window);
		if (const TSharedPtr<IPlatformWindow>& Native = Window->GetNativeWindow())
		{
			if (Renderer)
			{
				Renderer->DestroyViewport(*Native);
			}
			Native->Destroy();
		}
		if (ActiveWindow.lock() == Window.ToSharedPtr())
		{
			ActiveWindow.reset();
		}
		if (WindowUnderCursor.lock() == Window.ToSharedPtr())
		{
			WindowUnderCursor.reset();
			HoveredPath.clear();
		}
		Window->NotifyWindowClosed();
	}
}

TSharedPtr<SWindow> FSlateApplication::FindWindowForWidget(const SWidget& Widget) const
{
	SWidget* Root = const_cast<SWidget&>(Widget).GetRootWidget();
	for (const TSharedRef<SWindow>& Window : Windows)
	{
		if (&Window.Get() == Root)
		{
			return Window.ToSharedPtr();
		}
	}
	return nullptr;
}

TSharedPtr<SWindow> FSlateApplication::FindWindowForNative(const IPlatformWindow& Native) const
{
	for (const TSharedRef<SWindow>& Window : Windows)
	{
		if (Window->GetNativeWindow().get() == &Native)
		{
			return Window.ToSharedPtr();
		}
	}
	return nullptr;
}

void FSlateApplication::SetActiveWindow(const TSharedPtr<SWindow>& Window)
{
	ActiveWindow = Window;
}

// ---------------------------------------------------------------------------
// Frame loop

void FSlateApplication::Tick()
{
	if (bInTick)
	{
		return;
	}
	bInTick = true;

	const double Now = ReadClock();
	DeltaTime = static_cast<float>(FMath::Max(0.0, Now - LastTickTime));
	LastTickTime = Now;
	CurrentTime = Now;

	Platform->PumpMessages(DeltaTime);
	Platform->ProcessDeferredEvents(DeltaTime);
	Platform->PollGameDeviceState(DeltaTime);

	CheckFocusStillValid();
	TickTimersAndWidgets();
	SWidget::UpdateAllBoundAttributes();

	if (LayoutWindows() && !DragDropOperation)
	{
		// Things moved under the pointer: update hover and cursor.
		SynthesizeMouseMove();
		LayoutWindows();
	}
	UpdateToolTip();
	LayoutWindows(); // the tooltip may have just opened

	OnPreRender.Broadcast(DeltaTime);
	PaintWindows(true);
	DestroyPendingWindows();
	++FrameNumber;
	bInTick = false;
}

void FSlateApplication::RunMainLoop()
{
	while (!bQuitRequested && !Windows.empty())
	{
		Tick();
		if (!bQuitRequested && !IsActive())
		{
			Platform->WaitForMessages(static_cast<float>(ComputeIdleTimeout()));
		}
	}
}

bool FSlateApplication::LayoutWindows()
{
	bool bAny = false;
	for (const TSharedRef<SWindow>& Window : TArray<TSharedRef<SWindow>>(Windows))
	{
		const TSharedPtr<IPlatformWindow>& Native = Window->GetNativeWindow();
		if (!Native || Native->IsMinimized())
		{
			continue;
		}
		Window->SetScale(Native->GetDpiScale() * ApplicationScale);
		Window->SetPixelSize(Native->GetClientSize());
		bAny |= Window->UpdateLayout();
	}
	return bAny;
}

void FSlateApplication::PaintWindows(bool bOnlyDirty, SWindow* OnlyWindow)
{
	// Volatile widgets repaint every frame.
	for (SIZE_T I = 0; I < VolatileWidgets.size();)
	{
		const TSharedPtr<SWidget> Widget = VolatileWidgets[I].lock();
		if (!Widget || !Widget->IsVolatile())
		{
			VolatileWidgets.erase(VolatileWidgets.begin() + static_cast<std::ptrdiff_t>(I));
			continue;
		}
		if (Widget->IsVisibleInHierarchy())
		{
			Widget->Invalidate(EInvalidateWidgetReason::Paint);
		}
		++I;
	}

	TArray<FWindowDrawRequest> Requests;
	FPaintArgs Args;
	Args.CurrentTime = CurrentTime;
	Args.DeltaTime = DeltaTime;
	for (const TSharedRef<SWindow>& Window : Windows)
	{
		const TSharedPtr<IPlatformWindow>& Native = Window->GetNativeWindow();
		if (!Native || Native->IsMinimized() || !Native->IsVisible() || (OnlyWindow && OnlyWindow != &Window.Get()))
		{
			continue;
		}
		if (bOnlyDirty && !Window->NeedsRepaint())
		{
			continue;
		}
		const FIntPoint Size = Window->GetPixelSize();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			continue;
		}
		Window->UpdateLayout();
		Window->PaintWindow(Args);
		Requests.push_back({ Native.get(), &Window->GetDrawElements(), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f) });
	}
	if (Renderer && !Requests.empty())
	{
		Renderer->DrawWindows(Requests);
	}
}

void FSlateApplication::TickTimersAndWidgets()
{
	for (const FWidgetTimer& Timer : TArray<FWidgetTimer>(ActiveTimers))
	{
		const TSharedPtr<SWidget> Widget = Timer.Widget.lock();
		if (!Widget || Timer.Handle->bPendingRemoval)
		{
			continue;
		}
		if (CurrentTime + 1e-6 >= Timer.Handle->NextExecution)
		{
			Timer.Handle->NextExecution = CurrentTime + Timer.Handle->Period;
			if (Timer.Handle->Delegate(CurrentTime, DeltaTime) == EActiveTimerReturn::Stop)
			{
				Timer.Handle->bPendingRemoval = true;
			}
		}
	}
	std::erase_if(ActiveTimers, [](const FWidgetTimer& T) { return T.Widget.expired() || T.Handle->bPendingRemoval; });

	for (SIZE_T I = 0; I < TickingWidgets.size();)
	{
		const TSharedPtr<SWidget> Widget = TickingWidgets[I].lock();
		if (!Widget || !Widget->GetCanTick())
		{
			TickingWidgets.erase(TickingWidgets.begin() + static_cast<std::ptrdiff_t>(I));
			continue;
		}
		if (Widget->IsVisibleInHierarchy() && FindWindowForWidget(*Widget))
		{
			Widget->Tick(Widget->GetCachedGeometry(), CurrentTime, DeltaTime);
		}
		++I;
	}

	// Held sticks repeat their navigation.
	for (FSlateUser& User : Users)
	{
		if (User.AnalogDirection != EUINavigation::Invalid && CurrentTime >= User.NextAnalogRepeat)
		{
			AttemptNavigation(User.AnalogDirection, User.UserIndex, ENavigationGenesis::Controller, Platform->GetModifierKeys());
			User.NextAnalogRepeat = CurrentTime + NavigationConfig->AnalogRepeatRate;
			User.bAnalogRepeated = true;
		}
	}
}

bool FSlateApplication::IsActive() const
{
	for (const TSharedRef<SWindow>& Window : Windows)
	{
		const TSharedPtr<IPlatformWindow>& Native = Window->GetNativeWindow();
		if (Native && !Native->IsMinimized() && (Window->NeedsLayout() || Window->NeedsRepaint()))
		{
			return true;
		}
	}
	if (!WindowsToDestroy.empty())
	{
		return true;
	}
	for (const TWeakPtr<SWidget>& Weak : TickingWidgets)
	{
		if (!Weak.expired())
		{
			return true;
		}
	}
	for (const TWeakPtr<SWidget>& Weak : VolatileWidgets)
	{
		const TSharedPtr<SWidget> Widget = Weak.lock();
		if (Widget && Widget->IsVolatile() && Widget->IsVisibleInHierarchy())
		{
			return true;
		}
	}
	for (const FWidgetTimer& Timer : ActiveTimers)
	{
		if (!Timer.Widget.expired() && Timer.Handle->Period <= 0.0f)
		{
			return true;
		}
	}
	return false;
}

double FSlateApplication::ComputeIdleTimeout() const
{
	double Next = CurrentTime + 0.5;
	for (const FWidgetTimer& Timer : ActiveTimers)
	{
		if (!Timer.Widget.expired())
		{
			Next = FMath::Min(Next, Timer.Handle->NextExecution);
		}
	}
	if (ToolTipOwner.lock() && !ActiveToolTip && !bToolTipSuppressed)
	{
		Next = FMath::Min(Next, ToolTipCandidateTime + ToolTipDelay);
	}
	for (const FSlateUser& User : Users)
	{
		if (User.AnalogDirection != EUINavigation::Invalid)
		{
			Next = FMath::Min(Next, User.NextAnalogRepeat);
		}
	}
	return FMath::Max(0.0, Next - CurrentTime);
}

void FSlateApplication::RegisterActiveTimer(const TSharedRef<SWidget>& Widget, const TSharedRef<FActiveTimerHandle>& Timer)
{
	ActiveTimers.push_back({ Widget.ToSharedPtr(), Timer.ToSharedPtr() });
}

void FSlateApplication::UnRegisterActiveTimer(const TSharedRef<SWidget>& Widget, const TSharedRef<FActiveTimerHandle>& Timer)
{
	(void)Widget;
	Timer->bPendingRemoval = true;
}

void FSlateApplication::RegisterTickingWidget(const TSharedRef<SWidget>& Widget)
{
	TickingWidgets.push_back(Widget.ToSharedPtr());
}

void FSlateApplication::RegisterVolatileWidget(const TSharedRef<SWidget>& Widget)
{
	VolatileWidgets.push_back(Widget.ToSharedPtr());
}

// ---------------------------------------------------------------------------
// Focus

TArray<TSharedRef<SWidget>> FSlateApplication::GetPathToWidget(const TSharedRef<SWidget>& Widget) const
{
	TArray<TSharedRef<SWidget>> Path;
	for (SWidget* W = &Widget.Get(); W; W = W->GetParentWidget())
	{
		Path.push_back(TSharedRef<SWidget>(W->shared_from_this()));
	}
	std::reverse(Path.begin(), Path.end());
	return Path;
}

TArray<TSharedRef<SWidget>> FSlateApplication::GetFocusPath(int32 UserIndex) const
{
	const FSlateUser* User = FindUser(UserIndex);
	const TSharedPtr<SWidget> Focused = User ? User->FocusedWidget.lock() : nullptr;
	return Focused ? GetPathToWidget(ToSharedRef(Focused)) : TArray<TSharedRef<SWidget>>();
}

TSharedPtr<SWidget> FSlateApplication::GetUserFocusedWidget(int32 UserIndex) const
{
	const FSlateUser* User = FindUser(UserIndex);
	return User ? User->FocusedWidget.lock() : nullptr;
}

EFocusCause FSlateApplication::GetFocusCause(int32 UserIndex) const
{
	const FSlateUser* User = FindUser(UserIndex);
	return User ? User->FocusCause : EFocusCause::Cleared;
}

bool FSlateApplication::HasFocus(const SWidget* Widget, int32 UserIndex) const
{
	const FSlateUser* User = FindUser(UserIndex);
	return User && Widget && User->FocusedWidget.lock().get() == Widget;
}

bool FSlateApplication::HasFocusedDescendants(const SWidget* Widget, int32 UserIndex) const
{
	const FSlateUser* User = FindUser(UserIndex);
	const TSharedPtr<SWidget> Focused = User ? User->FocusedWidget.lock() : nullptr;
	return Widget && Focused && Widget->IsAncestorOf(Focused.get());
}

bool FSlateApplication::SetUserFocus(int32 UserIndex, const TSharedPtr<SWidget>& Widget, EFocusCause Cause)
{
	if (Widget && (!Widget->SupportsKeyboardFocus() || !Widget->IsEnabledInHierarchy()))
	{
		return false;
	}
	FSlateUser& User = GetUser(UserIndex);
	const TSharedPtr<SWidget> Old = User.FocusedWidget.lock();
	if (Old == Widget)
	{
		return true;
	}

	TArray<TWeakPtr<SWidget>> OldPath = User.FocusPath;
	const TArray<TSharedRef<SWidget>> NewPath = Widget ? GetPathToWidget(ToSharedRef(Widget)) : TArray<TSharedRef<SWidget>>();
	User.FocusedWidget = Widget;
	User.FocusCause = Widget ? Cause : EFocusCause::Cleared;
	User.FocusPath.clear();
	for (const TSharedRef<SWidget>& W : NewPath)
	{
		User.FocusPath.push_back(W.ToSharedPtr());
	}

	const FFocusEvent Event(Cause, UserIndex);
	for (const TWeakPtr<SWidget>& Weak : OldPath)
	{
		if (const TSharedPtr<SWidget> W = Weak.lock())
		{
			W->OnFocusChanging(Old.get(), Widget.get(), Event);
		}
	}
	for (const TSharedRef<SWidget>& W : NewPath)
	{
		const bool bAlreadyTold = std::any_of(OldPath.begin(), OldPath.end(), [&W](const TWeakPtr<SWidget>& O) { return O.lock() == W.ToSharedPtr(); });
		if (!bAlreadyTold)
		{
			W->OnFocusChanging(Old.get(), Widget.get(), Event);
		}
	}

	if (Old)
	{
		Old->OnFocusLost(Event);
		Old->Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (Widget && User.FocusedWidget.lock() == Widget)
	{
		const FReply Reply = Widget->OnFocusReceived(Widget->GetCachedGeometry(), Event);
		Widget->Invalidate(EInvalidateWidgetReason::Paint);
		// A focus request from OnFocusReceived would recurse; apply the rest.
		if (!Reply.GetFocusRecipient() || Reply.GetFocusRecipient() == Widget)
		{
			ProcessReply(Reply, Widget, nullptr, UserIndex);
		}
		if (Cause == EFocusCause::Navigation)
		{
			ScrollFocusedIntoView(ToSharedRef(Widget));
		}
	}
	return true;
}

void FSlateApplication::CheckFocusStillValid()
{
	for (FSlateUser& User : Users)
	{
		if (User.FocusPath.empty())
		{
			continue;
		}
		const TSharedPtr<SWidget> Focused = User.FocusedWidget.lock();
		if (!Focused)
		{
			// Destroyed: nobody to tell.
			User.FocusPath.clear();
			User.FocusedWidget.reset();
			continue;
		}
		const bool bInWindow = FindWindowForWidget(*Focused) != nullptr;
		if (!bInWindow || !Focused->IsVisibleInHierarchy() || !Focused->IsEnabledInHierarchy())
		{
			SetUserFocus(User.UserIndex, nullptr, EFocusCause::OtherWidgetLostFocus);
		}
	}
	if (const TSharedPtr<SWidget> Captor = MouseCaptor.lock())
	{
		if (!FindWindowForWidget(*Captor) || !Captor->IsVisibleInHierarchy())
		{
			ReleaseMouseCapture();
		}
	}
	else if (!MouseCaptor.expired() || CaptorWindow.lock())
	{
		ReleaseMouseCapture();
	}
}

// ---------------------------------------------------------------------------
// Navigation

void FSlateApplication::GatherFocusableWidgets(const TSharedRef<SWidget>& Root, TArray<TSharedRef<SWidget>>& Out)
{
	const EVisibility Visibility = Root->GetVisibility();
	if (!IsPainted(Visibility) || !Root->IsEnabled())
	{
		return;
	}
	if (Root->SupportsKeyboardFocus() && Root->IsEnabledInHierarchy())
	{
		Out.push_back(Root);
	}
	for (const FArrangedWidget& Child : Root->GetArrangedChildren())
	{
		GatherFocusableWidgets(Child.Widget, Out);
	}
}

bool FSlateApplication::NavigateFocus(EUINavigation Direction, int32 UserIndex, ENavigationGenesis Genesis)
{
	return AttemptNavigation(Direction, UserIndex, Genesis, Platform->GetModifierKeys());
}

bool FSlateApplication::AttemptNavigation(EUINavigation Direction, int32 UserIndex, ENavigationGenesis Genesis, const FModifierKeysState& Modifiers)
{
	FSlateUser& User = GetUser(UserIndex);
	const TSharedPtr<SWidget> Focused = User.FocusedWidget.lock();
	TSharedPtr<SWindow> Window = Focused ? FindWindowForWidget(*Focused) : GetActiveModalWindow();
	if (!Window)
	{
		Window = ActiveWindow.lock();
	}
	if (!Window && !Windows.empty())
	{
		Window = Windows.front().ToSharedPtr();
	}
	if (!Window)
	{
		return false;
	}

	const FNavigationEvent Event(Direction, Genesis, Modifiers, UserIndex);
	TSharedPtr<SWidget> Boundary = Window;
	bool bWrap = Direction == EUINavigation::Next || Direction == EUINavigation::Previous;
	if (Focused)
	{
		for (SWidget* W = Focused.get(); W; W = W->GetParentWidget())
		{
			const FNavigationReply Reply = W->OnNavigation(W->GetCachedGeometry(), Event);
			TSharedPtr<SWidget> Target;
			switch (Reply.GetBoundaryRule())
			{
			case FNavigationReply::EBoundaryRule::Escape:
				continue;
			case FNavigationReply::EBoundaryRule::Stop:
				return false;
			case FNavigationReply::EBoundaryRule::Explicit:
				Target = Reply.GetExplicitTarget();
				return Target && SetUserFocus(UserIndex, Target, EFocusCause::Navigation);
			case FNavigationReply::EBoundaryRule::Custom:
				Target = Reply.GetCustomHandler() ? Reply.GetCustomHandler()(Direction) : nullptr;
				return Target && SetUserFocus(UserIndex, Target, EFocusCause::Navigation);
			case FNavigationReply::EBoundaryRule::Wrap:
				Boundary = W->shared_from_this();
				bWrap = true;
				break;
			}
			break;
		}
	}

	const TSharedPtr<SWidget> Target = FindNavigationTarget(ToSharedRef(Boundary), Focused, Direction, bWrap);
	return Target && Target != Focused && SetUserFocus(UserIndex, Target, EFocusCause::Navigation);
}

TSharedPtr<SWidget> FSlateApplication::FindNavigationTarget(const TSharedRef<SWidget>& Boundary, const TSharedPtr<SWidget>& From, EUINavigation Direction, bool bWrap) const
{
	TArray<TSharedRef<SWidget>> Candidates;
	GatherFocusableWidgets(Boundary, Candidates);
	// The boundary itself (a focusable menu, say) is where we come from, not a target.
	std::erase_if(Candidates, [&](const TSharedRef<SWidget>& C) { return &C.Get() == &Boundary.Get() && From.get() != &Boundary.Get(); });
	if (Candidates.empty())
	{
		return nullptr;
	}
	const bool bForward = Direction == EUINavigation::Next || Direction == EUINavigation::Down || Direction == EUINavigation::Right;

	// No starting point, or starting from a container of the candidates
	// (e.g. a menu that has focus itself): enter at the first or last.
	const auto IsInside = [&From](const TSharedRef<SWidget>& C) { return From && From->IsAncestorOf(&C.Get()); };
	if (!From || std::all_of(Candidates.begin(), Candidates.end(), [&](const TSharedRef<SWidget>& C) { return IsInside(C) || &C.Get() == From.get(); }))
	{
		std::erase_if(Candidates, [&](const TSharedRef<SWidget>& C) { return &C.Get() == From.get(); });
		if (Candidates.empty())
		{
			return nullptr;
		}
		return bForward ? Candidates.front().ToSharedPtr() : Candidates.back().ToSharedPtr();
	}

	if (Direction == EUINavigation::Next || Direction == EUINavigation::Previous)
	{
		const auto It = std::find_if(Candidates.begin(), Candidates.end(), [&](const TSharedRef<SWidget>& C) { return &C.Get() == From.get(); });
		int32 Index;
		if (It == Candidates.end())
		{
			// From is not focusable here (it may be outside the boundary): start at an end.
			return bForward ? Candidates.front().ToSharedPtr() : Candidates.back().ToSharedPtr();
		}
		Index = static_cast<int32>(It - Candidates.begin()) + (bForward ? 1 : -1);
		const int32 Count = static_cast<int32>(Candidates.size());
		if (Index < 0 || Index >= Count)
		{
			if (!bWrap)
			{
				return nullptr;
			}
			Index = (Index + Count) % Count;
		}
		return Candidates[Index].ToSharedPtr();
	}

	// Directional: the nearest widget past the edge we leave through,
	// preferring ones that line up with us.
	const FRect FromRect = From->GetCachedGeometry().GetRenderBoundingRect();
	const auto Gap = [](float A0, float A1, float B0, float B1) { return B1 < A0 ? A0 - B1 : (B0 > A1 ? B0 - A1 : 0.0f); };
	const auto Score = [&](const FRect& R, bool bWrapping) -> TOptional<float>
	{
		float Primary = 0.0f, Secondary = 0.0f;
		const float TolY = FromRect.Height() * 0.25f;
		const float TolX = FromRect.Width() * 0.25f;
		switch (Direction)
		{
		case EUINavigation::Down:
			if (!bWrapping && R.Top < FromRect.Bottom - TolY) return {};
			Primary = bWrapping ? R.Top : R.Top - FromRect.Bottom;
			Secondary = Gap(FromRect.Left, FromRect.Right, R.Left, R.Right);
			break;
		case EUINavigation::Up:
			if (!bWrapping && R.Bottom > FromRect.Top + TolY) return {};
			Primary = bWrapping ? -R.Bottom : FromRect.Top - R.Bottom;
			Secondary = Gap(FromRect.Left, FromRect.Right, R.Left, R.Right);
			break;
		case EUINavigation::Right:
			if (!bWrapping && R.Left < FromRect.Right - TolX) return {};
			Primary = bWrapping ? R.Left : R.Left - FromRect.Right;
			Secondary = Gap(FromRect.Top, FromRect.Bottom, R.Top, R.Bottom);
			break;
		case EUINavigation::Left:
			if (!bWrapping && R.Right > FromRect.Left + TolX) return {};
			Primary = bWrapping ? -R.Right : FromRect.Left - R.Right;
			Secondary = Gap(FromRect.Top, FromRect.Bottom, R.Top, R.Bottom);
			break;
		default:
			return {};
		}
		// Wrapping picks the widget nearest the opposite edge (Primary may be negative).
		return (bWrapping ? Primary : FMath::Max(Primary, 0.0f)) + Secondary * 2.0f;
	};

	for (const bool bWrapping : { false, true })
	{
		if (bWrapping && !bWrap)
		{
			break;
		}
		TSharedPtr<SWidget> Best;
		float BestScore = 0.0f;
		for (const TSharedRef<SWidget>& Candidate : Candidates)
		{
			if (&Candidate.Get() == From.get() || Candidate->IsAncestorOf(From.get()) || From->IsAncestorOf(&Candidate.Get()))
			{
				continue;
			}
			const TOptional<float> S = Score(Candidate->GetCachedGeometry().GetRenderBoundingRect(), bWrapping);
			if (S && (!Best || *S < BestScore))
			{
				Best = Candidate.ToSharedPtr();
				BestScore = *S;
			}
		}
		if (Best)
		{
			return Best;
		}
	}
	return nullptr;
}

void FSlateApplication::ScrollFocusedIntoView(const TSharedRef<SWidget>& Widget)
{
	for (SWidget* Ancestor = Widget->GetParentWidget(); Ancestor; Ancestor = Ancestor->GetParentWidget())
	{
		Ancestor->ScrollDescendantIntoView(Widget.Get());
	}
}

// ---------------------------------------------------------------------------
// Menus

TSharedRef<IMenu> FSlateApplication::PushMenu(const TSharedRef<SWidget>& ParentWidget, const TSharedRef<SWidget>& Content, FVector2 Position, bool bFocusImmediately)
{
	const TSharedPtr<SWindow> Window = FindWindowForWidget(*ParentWidget);
	checkf(Window, "PushMenu: the parent widget is not in a window");

	// A menu opened from inside a menu is its submenu; anything else starts a
	// new menu tree.
	if (const TSharedPtr<IMenu> ParentMenu = FindMenuContaining(*ParentWidget))
	{
		DismissMenusAfter(*ParentMenu);
	}
	else
	{
		DismissAllMenus();
	}
	CloseToolTip();

	const TSharedRef<SWidget> Host = SNew(SMenuHost)[Content];
	auto Menu = MakeShared<FMenu>(*this, Host, ParentWidget, ToSharedRef(Window));
	Menu->PreviousFocus = GetKeyboardFocusedWidget();
	MenuStack.push_back(Menu);
	Window->AddPopup(Host, Position, EPopupLayer::Menu);
	if (bFocusImmediately)
	{
		SetKeyboardFocus(Host, EFocusCause::SetDirectly);
	}
	return TSharedRef<IMenu>(StaticCastSharedPtr<IMenu>(Menu.ToSharedPtr()));
}

void FSlateApplication::DismissAllMenus()
{
	if (!MenuStack.empty())
	{
		DismissMenuAndChildren(*MenuStack.front());
	}
}

void FSlateApplication::DismissMenusAfter(const IMenu& Menu)
{
	for (SIZE_T I = 0; I < MenuStack.size(); ++I)
	{
		if (&MenuStack[I].Get() == &Menu)
		{
			if (I + 1 < MenuStack.size())
			{
				DismissMenuAndChildren(*MenuStack[I + 1]);
			}
			return;
		}
	}
}

void FSlateApplication::DismissMenuAndChildren(const IMenu& Menu)
{
	SIZE_T Index = MenuStack.size();
	for (SIZE_T I = 0; I < MenuStack.size(); ++I)
	{
		if (&MenuStack[I].Get() == &Menu)
		{
			Index = I;
			break;
		}
	}
	if (Index == MenuStack.size())
	{
		return;
	}

	TArray<TSharedRef<FMenu>> Dismissed(MenuStack.begin() + static_cast<std::ptrdiff_t>(Index), MenuStack.end());
	MenuStack.erase(MenuStack.begin() + static_cast<std::ptrdiff_t>(Index), MenuStack.end());

	// Focus inside a closing menu goes back to where it was before it opened.
	const TSharedPtr<SWidget> Focused = GetKeyboardFocusedWidget();
	bool bFocusInside = false;
	for (const TSharedRef<FMenu>& M : Dismissed)
	{
		bFocusInside |= Focused && (&M->Host.Get() == Focused.get() || M->Host->IsAncestorOf(Focused.get()));
	}

	for (auto It = Dismissed.rbegin(); It != Dismissed.rend(); ++It)
	{
		const TSharedRef<FMenu>& M = *It;
		M->bDismissed = true;
		if (const TSharedPtr<SWindow> Window = M->GetOwnerWindow())
		{
			Window->RemovePopup(M->Host.Get());
		}
	}
	if (bFocusInside)
	{
		const TSharedPtr<SWidget> Previous = Dismissed.front()->PreviousFocus.lock();
		const bool bPreviousUsable = Previous && Previous->IsVisibleInHierarchy() && FindWindowForWidget(*Previous)
			&& !std::any_of(Dismissed.begin(), Dismissed.end(), [&](const TSharedRef<FMenu>& M) { return M->Host->IsAncestorOf(Previous.get()); });
		SetKeyboardFocus(bPreviousUsable ? Previous : nullptr, EFocusCause::SetDirectly);
	}
	for (auto It = Dismissed.rbegin(); It != Dismissed.rend(); ++It)
	{
		const TSharedRef<FMenu>& M = *It;
		M->OnMenuDismissed.Broadcast(TSharedRef<IMenu>(StaticCastSharedPtr<IMenu>(M.ToSharedPtr())));
	}
}

TSharedPtr<IMenu> FSlateApplication::FindMenuContaining(const SWidget& Widget) const
{
	for (auto It = MenuStack.rbegin(); It != MenuStack.rend(); ++It)
	{
		const TSharedRef<FMenu>& Menu = *It;
		if (&Menu->Host.Get() == &Widget || Menu->Host->IsAncestorOf(&Widget))
		{
			return StaticCastSharedPtr<IMenu>(Menu.ToSharedPtr());
		}
	}
	return nullptr;
}

void FSlateApplication::DismissMenusOnClick(const TArray<TSharedRef<SWidget>>& Path)
{
	if (MenuStack.empty())
	{
		return;
	}
	// Keep the menus the click is in, and those whose anchor was clicked (the
	// anchor toggles them itself); close everything opened after them.
	int32 Keep = INDEX_NONE;
	for (int32 I = 0; I < static_cast<int32>(MenuStack.size()); ++I)
	{
		const TSharedRef<FMenu>& Menu = MenuStack[I];
		const TSharedPtr<SWidget> Parent = Menu->GetParentWidget();
		if (PathContains(Path, &Menu->Host.Get()) || (Parent && PathContains(Path, Parent.get())))
		{
			Keep = I;
		}
	}
	if (Keep == INDEX_NONE)
	{
		DismissAllMenus();
	}
	else if (Keep + 1 < static_cast<int32>(MenuStack.size()))
	{
		DismissMenuAndChildren(*MenuStack[Keep + 1]);
	}
}

// ---------------------------------------------------------------------------
// Tooltips

void FSlateApplication::UpdateToolTip()
{
	const TSharedPtr<SWindow> Window = WindowUnderCursor.lock();
	if (DragDropOperation || PressedButtons != 0 || !bAppActive || !Window || MouseCaptor.lock())
	{
		CloseToolTip();
		return;
	}

	TSharedPtr<SWidget> Owner;
	for (auto It = HoveredPath.rbegin(); It != HoveredPath.rend(); ++It)
	{
		if (const TSharedPtr<SWidget> W = It->lock())
		{
			const TSharedPtr<IToolTip> ToolTip = W->GetToolTip();
			if (ToolTip && !ToolTip->IsEmpty())
			{
				Owner = W;
				break;
			}
		}
	}
	if (Owner != ToolTipOwner.lock())
	{
		CloseToolTip();
		ToolTipOwner = Owner;
		ToolTipCandidateTime = CurrentTime;
		bToolTipSuppressed = false;
	}
	if (!Owner || bToolTipSuppressed || ActiveToolTip)
	{
		return;
	}
	if (CurrentTime - ToolTipCandidateTime + 1e-6 >= ToolTipDelay)
	{
		ActiveToolTip = Owner->GetToolTip();
		const TSharedRef<SWidget> Widget = ActiveToolTip->AsWidget();
		Widget->SetVisibility(EVisibility::HitTestInvisible);
		ActiveToolTip->OnOpening();
		Window->AddPopup(Widget, CursorPosition / Window->GetScale() + ToolTipOffset, EPopupLayer::ToolTip);
		ToolTipWindow = Window;
	}
}

void FSlateApplication::CloseToolTip()
{
	if (!ActiveToolTip)
	{
		return;
	}
	const TSharedPtr<IToolTip> ToolTip = ActiveToolTip;
	ActiveToolTip.reset();
	if (const TSharedPtr<SWindow> Window = ToolTipWindow.lock())
	{
		Window->RemovePopup(ToolTip->AsWidget().Get());
	}
	ToolTipWindow.reset();
	ToolTip->OnClosed();
	// Do not reopen until the pointer moves to another widget.
	bToolTipSuppressed = true;
}
