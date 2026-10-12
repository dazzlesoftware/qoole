// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "GenericPlatform/InputTypes.h"

class IPlatformWindow;

enum class EWindowType : uint8
{
	Normal,  // top-level window with a taskbar entry
	Menu,    // popup owned by another window, no activation on show
	ToolTip, // popup, never activates, ignores input
};

// Everything needed to create a native window. The size is in logical units
// (1/96 inch) and is scaled by the DPI of the monitor the window opens on.
// The position is in screen pixels, because logical units are not
// well-defined across monitors with different DPIs.
struct FWindowDesc
{
	FString Title = "Genesis";
	EWindowType Type = EWindowType::Normal;

	FVector2 Size = { 1280.0f, 720.0f }; // client area, logical units
	TOptional<FIntPoint> Position;       // client area top-left, screen pixels; centered on the primary monitor's work area when unset

	bool bUseOSBorder = true;   // false: borderless, the UI draws its own title bar
	bool bSizable = true;
	bool bHasMinimizeButton = true;
	bool bHasMaximizeButton = true;
	bool bHasCloseButton = true;
	bool bTopmost = false;
	bool bAppearsInTaskbar = true;
	bool bAcceptsFileDrops = false;
	bool bShowImmediately = true;
	bool bActivateWhenShown = true;
	float Opacity = 1.0f;

	// Border width (logical units) used for resize hit-testing of borderless windows.
	float BorderlessResizeBorder = 6.0f;

	TSharedPtr<IPlatformWindow> Parent; // owner window (popups, dialogs)
};

// One native window.
class IPlatformWindow
{
public:
	virtual ~IPlatformWindow() = default;

	virtual const FWindowDesc& GetDesc() const = 0;

	virtual void Show() = 0;
	virtual void Hide() = 0;
	virtual void Minimize() = 0;
	virtual void Maximize() = 0;
	virtual void Restore() = 0;
	virtual void BringToFront(bool bForce = false) = 0;
	virtual void Destroy() = 0;

	virtual void SetTitle(const FString& Title) = 0;
	virtual void SetOpacity(float Opacity) = 0;
	// Disabled windows get no input (used while a modal window is up).
	virtual void Enable(bool bEnable) = 0;

	// Client area position (screen pixels) and size (pixels).
	virtual void MoveWindowTo(int32 X, int32 Y) = 0;
	virtual void ReshapeWindow(int32 X, int32 Y, int32 Width, int32 Height) = 0;
	virtual FIntPoint GetClientSize() const = 0;
	virtual FIntPoint GetClientPosition() const = 0;

	// Physical pixels per logical unit (1.0 at 96 DPI).
	virtual float GetDpiScale() const = 0;

	virtual bool IsVisible() const = 0;
	virtual bool IsMinimized() const = 0;
	virtual bool IsMaximized() const = 0;
	virtual bool IsForegroundWindow() const = 0;
	virtual bool IsDestroyed() const = 0;

	// HWND on Windows; nullptr for windows without a native handle.
	virtual void* GetNativeHandle() const = 0;
};
