// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Callbacks from the platform layer to the application (docs/GenesisUI.md §4).
// Positions are client-area pixels of the given window. Every input callback
// returns whether it was handled.
//
// Input callbacks arrive from IPlatformApplication::ProcessDeferredEvents.
// Window callbacks (size, move, DPI, paint, activation, close) arrive
// immediately, possibly from inside the OS's modal move/size loop.

#pragma once

#include "GenericPlatform/PlatformWindow.h"

class IPlatformMessageHandler
{
public:
	virtual ~IPlatformMessageHandler() = default;

	// Keyboard (sent to the application, which knows the focused window).
	virtual bool OnKeyChar(char32_t Character, bool bIsRepeat) { (void)Character; (void)bIsRepeat; return false; }
	virtual bool OnKeyDown(EKey Key, uint32 CharacterCode, bool bIsRepeat) { (void)Key; (void)CharacterCode; (void)bIsRepeat; return false; }
	virtual bool OnKeyUp(EKey Key, uint32 CharacterCode, bool bIsRepeat) { (void)Key; (void)CharacterCode; (void)bIsRepeat; return false; }

	// Mouse.
	virtual bool OnMouseDown(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position) { (void)Window; (void)Button; (void)Position; return false; }
	virtual bool OnMouseUp(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position) { (void)Window; (void)Button; (void)Position; return false; }
	virtual bool OnMouseDoubleClick(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position) { (void)Window; (void)Button; (void)Position; return false; }
	// Delta in notches (positive = away from the user / right).
	virtual bool OnMouseWheel(const TSharedRef<IPlatformWindow>& Window, float Delta, bool bHorizontal, FVector2 Position) { (void)Window; (void)Delta; (void)bHorizontal; (void)Position; return false; }
	virtual bool OnMouseMove(const TSharedRef<IPlatformWindow>& Window, FVector2 Position) { (void)Window; (void)Position; return false; }
	virtual bool OnMouseLeave(const TSharedRef<IPlatformWindow>& Window) { (void)Window; return false; }
	// Unaccelerated device deltas, only while raw mouse input is enabled.
	virtual bool OnRawMouseMove(int32 DeltaX, int32 DeltaY) { (void)DeltaX; (void)DeltaY; return false; }
	// Return true after setting the cursor yourself; otherwise the platform
	// applies ICursor's current shape.
	virtual bool OnCursorSet() { return false; }

	// Gamepads (UserIndex 0..3).
	virtual bool OnGamepadButtonDown(int32 UserIndex, EGamepadButton Button, bool bIsRepeat) { (void)UserIndex; (void)Button; (void)bIsRepeat; return false; }
	virtual bool OnGamepadButtonUp(int32 UserIndex, EGamepadButton Button) { (void)UserIndex; (void)Button; return false; }
	// Sticks in [-1, 1] (Y up), triggers in [0, 1]; dead zones already applied.
	virtual bool OnGamepadAnalog(int32 UserIndex, EGamepadAxis Axis, float Value) { (void)UserIndex; (void)Axis; (void)Value; return false; }
	virtual void OnGamepadConnectionChanged(int32 UserIndex, bool bConnected) { (void)UserIndex; (void)bConnected; }

	// Windows.
	virtual void OnSizeChanged(const TSharedRef<IPlatformWindow>& Window, int32 Width, int32 Height, bool bWasMinimized) { (void)Window; (void)Width; (void)Height; (void)bWasMinimized; }
	virtual void OnMovedWindow(const TSharedRef<IPlatformWindow>& Window, int32 X, int32 Y) { (void)Window; (void)X; (void)Y; }
	virtual void OnDpiScaleChanged(const TSharedRef<IPlatformWindow>& Window, float NewScale) { (void)Window; (void)NewScale; }
	virtual void OnWindowActivationChanged(const TSharedRef<IPlatformWindow>& Window, EWindowActivation Activation) { (void)Window; (void)Activation; }
	virtual void OnApplicationActivationChanged(bool bIsActive) { (void)bIsActive; }
	// The user asked to close the window. The window stays open until the
	// application calls IPlatformWindow::Destroy.
	virtual void OnWindowClose(const TSharedRef<IPlatformWindow>& Window) { (void)Window; }
	// The OS wants the window redrawn now (exposure, or periodically while
	// the user drags/resizes a window and the main loop is blocked).
	virtual void OnOSPaint(const TSharedRef<IPlatformWindow>& Window) { (void)Window; }
	virtual void OnEnterSizeMove(const TSharedRef<IPlatformWindow>& Window) { (void)Window; }
	virtual void OnExitSizeMove(const TSharedRef<IPlatformWindow>& Window) { (void)Window; }
	virtual bool OnFilesDropped(const TSharedRef<IPlatformWindow>& Window, const TArray<FString>& Files, FVector2 Position) { (void)Window; (void)Files; (void)Position; return false; }

	// Hit-testing for borderless windows: which zone is under Position.
	// Unspecified lets the platform use its default (resize borders, rest client).
	virtual EWindowZone GetWindowZoneForPoint(const TSharedRef<IPlatformWindow>& Window, FVector2 Position) { (void)Window; (void)Position; return EWindowZone::Unspecified; }
};
