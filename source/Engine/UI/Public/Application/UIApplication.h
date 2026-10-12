// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FUIApplication: the frame loop, the windows, and the only bridge from the
// OS into widgets (docs/GenesisUI.md §7).
//
// Frame (Tick):
//   PumpMessages -> ProcessDeferredEvents (input -> events)
//   -> tick timers and widgets that asked for it
//   -> evaluate bound attributes
//   -> measure / arrange dirty windows
//   -> paint dirty windows -> IUIWindowRenderer
//   -> sleep if nothing is dirty, animating or waiting on a timer
//
// Input routing (§7.2): pointer events go to the widget path under the
// pointer (preview root -> leaf, then leaf -> root) or straight to the
// widget holding capture; key events follow the focus path. Handlers return
// FReply requests (focus, capture, drag) that are applied after routing.
//
// Menus, tooltips and the drag decorator are popup layers inside the window
// they belong to (SWindow::AddPopup).

#pragma once

#include "Application/NavigationConfig.h"
#include "Application/UIApplicationBase.h"
#include "GenericPlatform/PlatformApplication.h"
#include "Rendering/WindowRenderer.h"
#include "Widgets/SWindow.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUI, Log);

class FUIApplication;

// Sees input before widgets do (global shortcuts, game input sharing a
// window with UI, a gamepad-driven cursor). Return true to consume.
class IInputProcessor
{
public:
	virtual ~IInputProcessor() = default;
	virtual bool HandleKeyDownEvent(FUIApplication& App, const FKeyEvent& Event) { (void)App; (void)Event; return false; }
	virtual bool HandleKeyUpEvent(FUIApplication& App, const FKeyEvent& Event) { (void)App; (void)Event; return false; }
	virtual bool HandleMouseButtonDownEvent(FUIApplication& App, const FPointerEvent& Event) { (void)App; (void)Event; return false; }
	virtual bool HandleMouseButtonUpEvent(FUIApplication& App, const FPointerEvent& Event) { (void)App; (void)Event; return false; }
	virtual bool HandleMouseMoveEvent(FUIApplication& App, const FPointerEvent& Event) { (void)App; (void)Event; return false; }
	virtual bool HandleMouseWheelEvent(FUIApplication& App, const FPointerEvent& Event) { (void)App; (void)Event; return false; }
	virtual bool HandleAnalogInputEvent(FUIApplication& App, int32 UserIndex, EGamepadAxis Axis, float Value) { (void)App; (void)UserIndex; (void)Axis; (void)Value; return false; }
};

// An open popup menu (§7.5).
class IMenu
{
public:
	virtual ~IMenu() = default;
	virtual void Dismiss() = 0;
	virtual bool IsDismissed() const = 0;
	virtual TSharedRef<SWidget> GetContent() const = 0;
	virtual TSharedPtr<SWidget> GetParentWidget() const = 0;
	virtual TSharedPtr<SWindow> GetOwnerWindow() const = 0;

	TMulticastDelegate<void(const TSharedRef<IMenu>&)> OnMenuDismissed;
};

// Per-user input state (§7.4). Keyboard and mouse belong to user 0; each
// gamepad drives its own user.
struct FUIUser
{
	int32 UserIndex = 0;
	TWeakPtr<SWidget> FocusedWidget;
	TArray<TWeakPtr<SWidget>> FocusPath; // root -> focused, as of the last focus change
	EFocusCause FocusCause = EFocusCause::SetDirectly;
	// Analog navigation repeat.
	EUINavigation AnalogDirection = EUINavigation::Invalid;
	double NextAnalogRepeat = 0.0;
	bool bAnalogRepeated = false;
};

class FUIApplication final : public FUIApplicationBase, public IPlatformMessageHandler, public TSharedFromThis<FUIApplication>
{
public:
	// Creates the application and registers it with the platform as its
	// message handler. Only one may exist at a time.
	static TSharedRef<FUIApplication> Create(const TSharedRef<IPlatformApplication>& Platform, const TSharedRef<FFontCache>& FontCache);
	static FUIApplication& Get();
	// Closes every window and releases the application.
	static void Shutdown();

	FUIApplication(const TSharedRef<IPlatformApplication>& InPlatform, const TSharedRef<FFontCache>& InFontCache);
	~FUIApplication() override;

	// --- Setup ---------------------------------------------------------------

	// Not owned; must outlive the windows. nullptr: nothing is drawn (tests).
	void SetRenderer(IUIWindowRenderer* InRenderer);
	IPlatformApplication& GetPlatformApplication() const { return *Platform; }

	// Extra scale on top of each monitor's DPI scale.
	void SetApplicationScale(float InScale);
	float GetApplicationScale() const { return ApplicationScale; }

	void SetNavigationConfig(const TSharedRef<FNavigationConfig>& Config) { NavigationConfig = Config.ToSharedPtr(); }
	const FNavigationConfig& GetNavigationConfig() const { return *NavigationConfig; }

	// Replaces the clock (tests drive time by hand).
	void SetTimeSource(TFunction<double()> Source);

	// --- Windows ---------------------------------------------------------------

	// Creates the native window for an SWindow and shows it.
	TSharedRef<SWindow> AddWindow(const TSharedRef<SWindow>& Window, bool bShowImmediately = true);
	// Shows a window modally: other windows are disabled and this call runs
	// frames until the window closes.
	void AddModalWindow(const TSharedRef<SWindow>& Window, const TSharedPtr<SWindow>& Parent);
	// Destroys the window at the end of the frame.
	void RequestDestroyWindow(const TSharedRef<SWindow>& Window);
	const TArray<TSharedRef<SWindow>>& GetWindows() const { return Windows; }
	TSharedPtr<SWindow> FindWindowForWidget(const SWidget& Widget) const;
	TSharedPtr<SWindow> FindWindowForNative(const IPlatformWindow& Native) const;
	TSharedPtr<SWindow> GetActiveTopLevelWindow() const { return ActiveWindow.lock(); }
	TSharedPtr<SWindow> GetActiveModalWindow() const;

	// --- Frame loop --------------------------------------------------------------

	// Runs one frame.
	void Tick();
	// Runs frames until RequestQuit or the last window closes.
	void RunMainLoop();
	void RequestQuit() { bQuitRequested = true; }
	bool IsQuitRequested() const { return bQuitRequested; }
	uint64 GetFrameNumber() const { return FrameNumber; }
	// Lays out every window now (without painting); handy in tests.
	bool LayoutWindows(); // returns whether any window was laid out
	// Whether the next frame has work to do (otherwise the loop sleeps).
	bool IsActive() const;
	// Called after layout and before painting, each frame.
	TMulticastDelegate<void(float DeltaTime)> OnPreRender;

	// --- Focus ---------------------------------------------------------------------

	bool SetUserFocus(int32 UserIndex, const TSharedPtr<SWidget>& Widget, EFocusCause Cause = EFocusCause::SetDirectly);
	bool SetKeyboardFocus(const TSharedPtr<SWidget>& Widget, EFocusCause Cause = EFocusCause::SetDirectly) { return SetUserFocus(0, Widget, Cause); }
	void ClearKeyboardFocus(EFocusCause Cause = EFocusCause::Cleared) { SetUserFocus(0, nullptr, Cause); }
	TSharedPtr<SWidget> GetUserFocusedWidget(int32 UserIndex) const;
	TSharedPtr<SWidget> GetKeyboardFocusedWidget() const { return GetUserFocusedWidget(0); }
	EFocusCause GetFocusCause(int32 UserIndex = 0) const;
	// Moves focus as a key would. Returns true if focus moved.
	bool NavigateFocus(EUINavigation Direction, int32 UserIndex = 0, ENavigationGenesis Genesis = ENavigationGenesis::User);
	// Every focusable, visible, enabled widget under Root in tab order.
	static void GatherFocusableWidgets(const TSharedRef<SWidget>& Root, TArray<TSharedRef<SWidget>>& Out);

	// --- Pointer -----------------------------------------------------------------------

	TSharedPtr<SWidget> GetMouseCaptor() const { return MouseCaptor.lock(); }
	bool SetMouseCapture(const TSharedPtr<SWidget>& Widget);
	void ReleaseMouseCapture();
	// Last pointer position in window pixels and the window it is in.
	FVector2 GetCursorPosition() const { return CursorPosition; }
	TSharedPtr<SWindow> GetWindowUnderCursor() const { return WindowUnderCursor.lock(); }
	// Widgets under the pointer, root to leaf, as of the last pointer event.
	TArray<TSharedRef<SWidget>> GetHoveredPath() const;
	// Re-hit-tests the pointer (layout moved things under it).
	void SynthesizeMouseMove();
	// Pixels the pointer must travel before a drag starts (logical units).
	float DragTriggerDistance = 5.0f;

	// --- Drag and drop --------------------------------------------------------------------

	TSharedPtr<FDragDropOperation> GetDragDropContent() const { return DragDropOperation; }
	void CancelDragDrop();

	// --- Menus -------------------------------------------------------------------------------

	// Opens Content as a popup in ParentWidget's window at Position (window
	// logical units). Menus stack: opening a menu from inside another keeps
	// the outer one; clicking outside a menu dismisses it and every menu
	// opened from it.
	TSharedRef<IMenu> PushMenu(const TSharedRef<SWidget>& ParentWidget, const TSharedRef<SWidget>& Content, FVector2 Position, bool bFocusImmediately = true);
	void DismissAllMenus();
	// Dismisses Menu and every menu opened after it.
	void DismissMenuAndChildren(const IMenu& Menu);
	// Dismisses only the menus opened after Menu (its submenus).
	void DismissMenusAfter(const IMenu& Menu);
	bool AnyMenusVisible() const { return !MenuStack.empty(); }
	int32 GetMenuCount() const { return static_cast<int32>(MenuStack.size()); }
	// The innermost menu containing Widget, if any.
	TSharedPtr<IMenu> FindMenuContaining(const SWidget& Widget) const;

	// --- Tooltips -------------------------------------------------------------------------------

	float ToolTipDelay = 0.5f;
	FVector2 ToolTipOffset = FVector2(14.0f, 18.0f); // logical units from the cursor
	TSharedPtr<IToolTip> GetActiveToolTip() const { return ActiveToolTip; }
	void CloseToolTip();

	// --- Input processors ----------------------------------------------------------------------

	void RegisterInputPreProcessor(const TSharedRef<IInputProcessor>& Processor);
	void UnregisterInputPreProcessor(const TSharedRef<IInputProcessor>& Processor);

	// --- FUIApplicationBase -----------------------------------------------------------------------

	bool HasFocus(const SWidget* Widget, int32 UserIndex = 0) const override;
	bool HasFocusedDescendants(const SWidget* Widget, int32 UserIndex = 0) const override;
	bool HasMouseCapture(const SWidget* Widget) const override;
	bool IsDragDropping() const override { return DragDropOperation != nullptr; }
	double GetTime() const override { return CurrentTime; }
	float GetDeltaTime() const override { return DeltaTime; }
	FFontCache& GetFontCache() override { return *FontCache; }
	void RegisterActiveTimer(const TSharedRef<SWidget>& Widget, const TSharedRef<FActiveTimerHandle>& Timer) override;
	void UnRegisterActiveTimer(const TSharedRef<SWidget>& Widget, const TSharedRef<FActiveTimerHandle>& Timer) override;
	void RegisterTickingWidget(const TSharedRef<SWidget>& Widget) override;
	void RegisterVolatileWidget(const TSharedRef<SWidget>& Widget) override;
	void OnWidgetDestroyed(const SWidget* Widget) override { (void)Widget; }
	TSharedRef<IToolTip> MakeToolTip(const TAttribute<FString>& Text) override;

	// --- IPlatformMessageHandler ---------------------------------------------------------------------

	bool OnKeyChar(char32_t Character, bool bIsRepeat) override;
	bool OnKeyDown(EKey Key, uint32 CharacterCode, bool bIsRepeat) override;
	bool OnKeyUp(EKey Key, uint32 CharacterCode, bool bIsRepeat) override;
	bool OnMouseDown(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position) override;
	bool OnMouseUp(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position) override;
	bool OnMouseDoubleClick(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position) override;
	bool OnMouseWheel(const TSharedRef<IPlatformWindow>& Window, float Delta, bool bHorizontal, FVector2 Position) override;
	bool OnMouseMove(const TSharedRef<IPlatformWindow>& Window, FVector2 Position) override;
	bool OnMouseLeave(const TSharedRef<IPlatformWindow>& Window) override;
	bool OnCursorSet() override;
	bool OnGamepadButtonDown(int32 UserIndex, EGamepadButton Button, bool bIsRepeat) override;
	bool OnGamepadButtonUp(int32 UserIndex, EGamepadButton Button) override;
	bool OnGamepadAnalog(int32 UserIndex, EGamepadAxis Axis, float Value) override;
	void OnSizeChanged(const TSharedRef<IPlatformWindow>& Window, int32 Width, int32 Height, bool bWasMinimized) override;
	void OnDpiScaleChanged(const TSharedRef<IPlatformWindow>& Window, float NewScale) override;
	void OnWindowActivationChanged(const TSharedRef<IPlatformWindow>& Window, EWindowActivation Activation) override;
	void OnApplicationActivationChanged(bool bIsActive) override;
	void OnWindowClose(const TSharedRef<IPlatformWindow>& Window) override;
	void OnOSPaint(const TSharedRef<IPlatformWindow>& Window) override;
	EWindowZone GetWindowZoneForPoint(const TSharedRef<IPlatformWindow>& Window, FVector2 Position) override;

private:
	class FMenu;
	struct FDragDetect
	{
		TWeakPtr<SWidget> Widget;
		EMouseButton Button = EMouseButton::Invalid;
		FVector2 StartPosition;
		TWeakPtr<SWindow> Window;
	};
	struct FWidgetTimer
	{
		TWeakPtr<SWidget> Widget;
		TSharedPtr<FActiveTimerHandle> Handle;
	};

	FUIUser& GetUser(int32 UserIndex);
	const FUIUser* FindUser(int32 UserIndex) const;
	double ReadClock() const;

	// Frame steps.
	void TickTimersAndWidgets();
	void UpdateToolTip();
	void PaintWindows(bool bOnlyDirty, SWindow* OnlyWindow = nullptr);
	double ComputeIdleTimeout() const;
	void DestroyPendingWindows();
	void CheckFocusStillValid();

	// Routing.
	FPointerEvent MakePointerEvent(FVector2 Position, EMouseButton Button, float WheelDelta) const;
	TArray<TSharedRef<SWidget>> GetPathToWidget(const TSharedRef<SWidget>& Widget) const;
	TArray<TSharedRef<SWidget>> GetFocusPath(int32 UserIndex) const;
	bool RoutePointerDown(SWindow& Window, const FPointerEvent& Event, bool bDoubleClick);
	void ProcessReply(const FReply& Reply, const TSharedPtr<SWidget>& Handler, const FPointerEvent* PointerEvent, int32 UserIndex);
	void UpdateHover(const TSharedPtr<SWindow>& Window, const TArray<TSharedRef<SWidget>>& NewPath, const FPointerEvent& Event);
	void UpdateDragDropTargets(const TArray<TSharedRef<SWidget>>& Path, const FPointerEvent& Event);
	void DropOrCancel(const TArray<TSharedRef<SWidget>>* Path, const FPointerEvent& Event, bool bCancel);
	void BeginDragDrop(const TSharedRef<FDragDropOperation>& Operation, const FPointerEvent& Event);
	void UpdateDragDecorator();
	void ApplyCursor();
	bool RouteKeyDown(const FKeyEvent& Event);
	bool RouteKeyUp(const FKeyEvent& Event);
	bool AttemptNavigation(EUINavigation Direction, int32 UserIndex, ENavigationGenesis Genesis, const FModifierKeysState& Modifiers);
	TSharedPtr<SWidget> FindNavigationTarget(const TSharedRef<SWidget>& Boundary, const TSharedPtr<SWidget>& From, EUINavigation Direction, bool bWrap) const;
	void ScrollFocusedIntoView(const TSharedRef<SWidget>& Widget);
	void DismissMenusOnClick(const TArray<TSharedRef<SWidget>>& Path);
	void SetActiveWindow(const TSharedPtr<SWindow>& Window);

	TSharedPtr<IPlatformApplication> Platform;
	TSharedPtr<FFontCache> FontCache;
	IUIWindowRenderer* Renderer = nullptr;
	TSharedPtr<FNavigationConfig> NavigationConfig;
	TFunction<double()> TimeSource;

	TArray<TSharedRef<SWindow>> Windows;
	TArray<TSharedRef<SWindow>> WindowsToDestroy;
	TWeakPtr<SWindow> ActiveWindow;

	TArray<FUIUser> Users;
	TWeakPtr<SWidget> MouseCaptor;
	TWeakPtr<SWindow> CaptorWindow;
	TArray<TWeakPtr<SWidget>> HoveredPath;
	TWeakPtr<SWindow> WindowUnderCursor;
	FVector2 CursorPosition;
	FVector2 LastCursorPosition;
	uint8 PressedButtons = 0;
	TOptional<FDragDetect> DragDetect;
	TSharedPtr<FDragDropOperation> DragDropOperation;
	TSharedPtr<SWidget> DragDecorator;
	TWeakPtr<SWindow> DragDecoratorWindow;
	TArray<TWeakPtr<SWidget>> DragDropTargets; // widgets that got OnDragEnter
	TWeakPtr<SWidget> MouseLockWidget;

	TArray<TSharedRef<FMenu>> MenuStack;

	TSharedPtr<IToolTip> ActiveToolTip;
	TWeakPtr<SWindow> ToolTipWindow;
	TWeakPtr<SWidget> ToolTipOwner;      // widget the visible (or pending) tooltip belongs to
	double ToolTipCandidateTime = 0.0;   // when the pointer settled on ToolTipOwner
	bool bToolTipSuppressed = false;     // until the pointer leaves the owner (after a click)

	TArray<FWidgetTimer> ActiveTimers;
	TArray<TWeakPtr<SWidget>> TickingWidgets;
	TArray<TWeakPtr<SWidget>> VolatileWidgets;
	TArray<TSharedPtr<IInputProcessor>> InputProcessors;

	float ApplicationScale = 1.0f;
	double CurrentTime = 0.0;
	double LastTickTime = 0.0;
	float DeltaTime = 0.0f;
	uint64 FrameNumber = 0;
	bool bQuitRequested = false;
	bool bInTick = false;
	bool bAppActive = true;
	int32 ModalDepth = 0;
};
