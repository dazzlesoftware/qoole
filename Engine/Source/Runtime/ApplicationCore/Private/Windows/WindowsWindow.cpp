// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Windows/WindowsPlatformApplication.h"

#include <ShellScalingApi.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

FWindowsWindow::FWindowsWindow(FWindowsApplication& InOwner, const FWindowDesc& InDesc) : Owner(InOwner), Desc(InDesc)
{
}

FWindowsWindow::~FWindowsWindow()
{
	if (Hwnd)
	{
		HWND Old = Hwnd;
		Hwnd = nullptr;
		SetWindowLongPtrW(Old, GWLP_USERDATA, 0);
		DestroyWindow(Old);
	}
}

bool FWindowsWindow::Initialize()
{
	DWORD Style = 0;
	DWORD ExStyle = 0;
	switch (Desc.Type)
	{
	case EWindowType::Normal:
		if (Desc.bUseOSBorder)
		{
			Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
			if (Desc.bSizable)
				Style |= WS_THICKFRAME;
			if (Desc.bHasMinimizeButton)
				Style |= WS_MINIMIZEBOX;
			if (Desc.bHasMaximizeButton)
				Style |= WS_MAXIMIZEBOX;
		}
		else
		{
			// Keep the caption/frame styles so the OS still provides snapping,
			// shadows, minimize/maximize animations and Alt+Space; the frame
			// itself is removed in WM_NCCALCSIZE.
			Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
			if (Desc.bSizable)
				Style |= WS_THICKFRAME;
			if (Desc.bHasMaximizeButton)
				Style |= WS_MAXIMIZEBOX;
		}
		ExStyle = Desc.bAppearsInTaskbar ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW;
		break;
	case EWindowType::Menu:
		Style = WS_POPUP;
		ExStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
		break;
	case EWindowType::ToolTip:
		Style = WS_POPUP;
		ExStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
		break;
	}
	if (Desc.bTopmost)
		ExStyle |= WS_EX_TOPMOST;
	if (Desc.bAcceptsFileDrops)
		ExStyle |= WS_EX_ACCEPTFILES;
	if (Desc.Opacity < 1.0f)
		ExStyle |= WS_EX_LAYERED;

	// Pick the monitor first, so the logical size can be scaled by its DPI.
	HMONITOR Monitor = Desc.Position
	    ? MonitorFromPoint(POINT{Desc.Position->X, Desc.Position->Y}, MONITOR_DEFAULTTONEAREST)
	    : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
	UINT DpiX = 96, DpiY = 96;
	GetDpiForMonitor(Monitor, MDT_EFFECTIVE_DPI, &DpiX, &DpiY);
	DpiScale = static_cast<float>(DpiX) / 96.0f;

	const int32 ClientWidth = FMath::Max(1, FMath::RoundToInt(Desc.Size.X * DpiScale));
	const int32 ClientHeight = FMath::Max(1, FMath::RoundToInt(Desc.Size.Y * DpiScale));

	MONITORINFO MonitorInfo{sizeof(MONITORINFO)};
	GetMonitorInfoW(Monitor, &MonitorInfo);
	const RECT& Work = MonitorInfo.rcWork;
	const int32 ClientX = Desc.Position ? Desc.Position->X : Work.left + ((Work.right - Work.left) - ClientWidth) / 2;
	const int32 ClientY = Desc.Position ? Desc.Position->Y : Work.top + ((Work.bottom - Work.top) - ClientHeight) / 2;

	RECT Rect{ClientX, ClientY, ClientX + ClientWidth, ClientY + ClientHeight};
	if (Desc.Type == EWindowType::Normal && Desc.bUseOSBorder)
	{
		AdjustWindowRectExForDpi(&Rect, Style, FALSE, ExStyle, DpiX);
	}

	const HWND ParentHwnd = Desc.Parent ? static_cast<HWND>(Desc.Parent->GetNativeHandle()) : nullptr;
	const std::wstring Title = Desc.Title.ToWide();
	Hwnd = CreateWindowExW(ExStyle, FWindowsApplication::GetWindowClassName(), Title.c_str(), Style, Rect.left,
	    Rect.top, Rect.right - Rect.left, Rect.bottom - Rect.top, ParentHwnd, nullptr, GetModuleHandleW(nullptr), this);
	if (!Hwnd)
	{
		GENESIS_LOG(LogApplicationCore, Error, "CreateWindowEx failed ({})", GetLastError());
		return false;
	}

	// The IME stays off until a text field activates an input context.
	ImmAssociateContextEx(Hwnd, nullptr, 0);

	// The window may have landed on a monitor with another DPI.
	DpiScale = static_cast<float>(GetDpiForWindow(Hwnd)) / 96.0f;

	if (IsBorderless())
	{
		// A one-pixel frame extension keeps the DWM shadow and, on
		// Windows 11, rounded corners.
		const MARGINS Margins{0, 0, 1, 0};
		DwmExtendFrameIntoClientArea(Hwnd, &Margins);
		SetWindowPos(
		    Hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (Desc.Opacity < 1.0f)
	{
		SetOpacity(Desc.Opacity);
	}
	if (!Desc.bHasCloseButton && Desc.bUseOSBorder)
	{
		EnableMenuItem(GetSystemMenu(Hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
	}
	if (Desc.bShowImmediately)
	{
		Show();
	}
	return true;
}

void FWindowsWindow::Show()
{
	if (Hwnd)
	{
		const bool bActivate = Desc.bActivateWhenShown && Desc.Type == EWindowType::Normal;
		ShowWindow(Hwnd, bActivate ? SW_SHOW : SW_SHOWNOACTIVATE);
	}
}

void FWindowsWindow::Hide()
{
	if (Hwnd)
		ShowWindow(Hwnd, SW_HIDE);
}

void FWindowsWindow::Minimize()
{
	if (Hwnd)
		ShowWindow(Hwnd, SW_MINIMIZE);
}

void FWindowsWindow::Maximize()
{
	if (Hwnd)
		ShowWindow(Hwnd, SW_MAXIMIZE);
}

void FWindowsWindow::Restore()
{
	if (Hwnd)
		ShowWindow(Hwnd, SW_RESTORE);
}

void FWindowsWindow::BringToFront(bool bForce)
{
	if (!Hwnd)
		return;
	if (IsIconic(Hwnd))
	{
		ShowWindow(Hwnd, SW_RESTORE);
	}
	if (bForce)
	{
		SetForegroundWindow(Hwnd);
	}
	SetWindowPos(Hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | (bForce ? 0 : SWP_NOACTIVATE));
}

void FWindowsWindow::Destroy()
{
	if (Hwnd)
	{
		DestroyWindow(Hwnd); // WM_DESTROY clears Hwnd and unregisters us
	}
}

void FWindowsWindow::SetTitle(const FString& Title)
{
	Desc.Title = Title;
	if (Hwnd)
		SetWindowTextW(Hwnd, Title.ToWide().c_str());
}

void FWindowsWindow::SetOpacity(float Opacity)
{
	Desc.Opacity = Opacity;
	if (!Hwnd)
		return;
	const LONG_PTR ExStyle = GetWindowLongPtrW(Hwnd, GWL_EXSTYLE);
	if (Opacity < 1.0f)
	{
		SetWindowLongPtrW(Hwnd, GWL_EXSTYLE, ExStyle | WS_EX_LAYERED);
		SetLayeredWindowAttributes(Hwnd, 0, static_cast<BYTE>(FMath::Clamp(Opacity, 0.0f, 1.0f) * 255.0f), LWA_ALPHA);
	}
	else if (ExStyle & WS_EX_LAYERED)
	{
		SetWindowLongPtrW(Hwnd, GWL_EXSTYLE, ExStyle & ~WS_EX_LAYERED);
	}
}

void FWindowsWindow::Enable(bool bEnable)
{
	if (Hwnd)
		EnableWindow(Hwnd, bEnable ? TRUE : FALSE);
}

void FWindowsWindow::MoveWindowTo(int32 X, int32 Y)
{
	if (!Hwnd)
		return;
	// X, Y are the client origin; convert to the outer window origin.
	RECT Window, Client;
	GetWindowRect(Hwnd, &Window);
	GetClientRect(Hwnd, &Client);
	POINT Origin{0, 0};
	ClientToScreen(Hwnd, &Origin);
	SetWindowPos(Hwnd, nullptr, X - (Origin.x - Window.left), Y - (Origin.y - Window.top), 0, 0,
	    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FWindowsWindow::ReshapeWindow(int32 X, int32 Y, int32 Width, int32 Height)
{
	if (!Hwnd)
		return;
	RECT Rect{X, Y, X + Width, Y + Height};
	if (Desc.Type == EWindowType::Normal && Desc.bUseOSBorder)
	{
		AdjustWindowRectExForDpi(&Rect, static_cast<DWORD>(GetWindowLongPtrW(Hwnd, GWL_STYLE)), FALSE,
		    static_cast<DWORD>(GetWindowLongPtrW(Hwnd, GWL_EXSTYLE)), GetDpiForWindow(Hwnd));
	}
	SetWindowPos(Hwnd, nullptr, Rect.left, Rect.top, Rect.right - Rect.left, Rect.bottom - Rect.top,
	    SWP_NOZORDER | SWP_NOACTIVATE);
}

FIntPoint FWindowsWindow::GetClientSize() const
{
	RECT Rect{};
	if (Hwnd)
		GetClientRect(Hwnd, &Rect);
	return FIntPoint(Rect.right - Rect.left, Rect.bottom - Rect.top);
}

FIntPoint FWindowsWindow::GetClientPosition() const
{
	POINT Origin{0, 0};
	if (Hwnd)
		ClientToScreen(Hwnd, &Origin);
	return FIntPoint(Origin.x, Origin.y);
}

bool FWindowsWindow::IsVisible() const
{
	return Hwnd && IsWindowVisible(Hwnd);
}

bool FWindowsWindow::IsMinimized() const
{
	return Hwnd && IsIconic(Hwnd);
}

bool FWindowsWindow::IsMaximized() const
{
	return Hwnd && IsZoomed(Hwnd);
}

bool FWindowsWindow::IsForegroundWindow() const
{
	return Hwnd && GetForegroundWindow() == Hwnd;
}
