// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Windows/WindowsPlatformApplication.h"

#include <ShellScalingApi.h>
#include <shellapi.h>
#include <windowsx.h>

// windowsx.h macros that collide with IPlatformWindow methods.
#undef IsMaximized
#undef IsMinimized

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "shcore.lib")

namespace
{
	constexpr UINT_PTR SizeMoveTimerId = 1;

	FWindowsWindow* GetWindowFromHwnd(HWND Hwnd)
	{
		return reinterpret_cast<FWindowsWindow*>(GetWindowLongPtrW(Hwnd, GWLP_USERDATA));
	}

	FVector2 ClientPosFromLParam(LPARAM LParam)
	{
		return FVector2(static_cast<float>(GET_X_LPARAM(LParam)), static_cast<float>(GET_Y_LPARAM(LParam)));
	}

	// Maps a virtual key (with the extended-key bit of lParam) to EKey.
	EKey TranslateKey(WPARAM VirtualKey, LPARAM LParam)
	{
		const bool bExtended = (LParam & (1 << 24)) != 0;
		switch (VirtualKey)
		{
		case VK_SHIFT:
		{
			const UINT ScanCode = (LParam >> 16) & 0xFF;
			return MapVirtualKeyW(ScanCode, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? EKey::RightShift : EKey::LeftShift;
		}
		case VK_CONTROL: return bExtended ? EKey::RightControl : EKey::LeftControl;
		case VK_MENU: return bExtended ? EKey::RightAlt : EKey::LeftAlt;
		case VK_RETURN: return EKey::Enter;
		default: break;
		}
		if (VirtualKey > 0 && VirtualKey < 0xFF)
		{
			return static_cast<EKey>(VirtualKey);
		}
		return EKey::Invalid;
	}

	EWindowZone DefaultBorderZone(const FWindowsWindow& Window, FVector2 Pos, FIntPoint Size)
	{
		if (!Window.GetDesc().bSizable || Window.IsMaximized())
		{
			return EWindowZone::ClientArea;
		}
		const float Border = Window.GetDesc().BorderlessResizeBorder * Window.GetDpiScale();
		const bool bLeft = Pos.X < Border;
		const bool bRight = Pos.X >= Size.X - Border;
		const bool bTop = Pos.Y < Border;
		const bool bBottom = Pos.Y >= Size.Y - Border;
		if (bTop && bLeft) return EWindowZone::TopLeftBorder;
		if (bTop && bRight) return EWindowZone::TopRightBorder;
		if (bBottom && bLeft) return EWindowZone::BottomLeftBorder;
		if (bBottom && bRight) return EWindowZone::BottomRightBorder;
		if (bTop) return EWindowZone::TopBorder;
		if (bBottom) return EWindowZone::BottomBorder;
		if (bLeft) return EWindowZone::LeftBorder;
		if (bRight) return EWindowZone::RightBorder;
		return EWindowZone::ClientArea;
	}

	LRESULT ZoneToHitTest(EWindowZone Zone)
	{
		switch (Zone)
		{
		case EWindowZone::NotInWindow: return HTNOWHERE;
		case EWindowZone::TitleBar: return HTCAPTION;
		case EWindowZone::TopLeftBorder: return HTTOPLEFT;
		case EWindowZone::TopBorder: return HTTOP;
		case EWindowZone::TopRightBorder: return HTTOPRIGHT;
		case EWindowZone::LeftBorder: return HTLEFT;
		case EWindowZone::RightBorder: return HTRIGHT;
		case EWindowZone::BottomLeftBorder: return HTBOTTOMLEFT;
		case EWindowZone::BottomBorder: return HTBOTTOM;
		case EWindowZone::BottomRightBorder: return HTBOTTOMRIGHT;
		case EWindowZone::MinimizeButton: return HTMINBUTTON;
		case EWindowZone::MaximizeButton: return HTMAXBUTTON;
		case EWindowZone::CloseButton: return HTCLOSE;
		case EWindowZone::SysMenu: return HTSYSMENU;
		default: return HTCLIENT;
		}
	}

	float ApplyStickDeadZone(SHORT Value, SHORT DeadZone)
	{
		const float V = static_cast<float>(Value);
		if (FMath::Abs(V) <= DeadZone)
		{
			return 0.0f;
		}
		const float Sign = V < 0.0f ? -1.0f : 1.0f;
		return Sign * FMath::Min(1.0f, (FMath::Abs(V) - DeadZone) / (32767.0f - DeadZone));
	}

	float ApplyTriggerThreshold(BYTE Value)
	{
		return Value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0.0f
			: static_cast<float>(Value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / (255.0f - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
	}
}

TSharedRef<IPlatformApplication> CreatePlatformApplication()
{
	return TSharedRef<IPlatformApplication>(std::make_shared<FWindowsApplication>());
}

// ---------------------------------------------------------------------------
// Cursor

FWindowsCursor::FWindowsCursor()
{
	const auto Load = [](LPCWSTR Id) { return LoadCursorW(nullptr, Id); };
	Cursors[static_cast<int32>(EMouseCursor::None)] = nullptr;
	Cursors[static_cast<int32>(EMouseCursor::Default)] = Load(IDC_ARROW);
	Cursors[static_cast<int32>(EMouseCursor::TextEditBeam)] = Load(IDC_IBEAM);
	Cursors[static_cast<int32>(EMouseCursor::ResizeLeftRight)] = Load(IDC_SIZEWE);
	Cursors[static_cast<int32>(EMouseCursor::ResizeUpDown)] = Load(IDC_SIZENS);
	Cursors[static_cast<int32>(EMouseCursor::ResizeSouthEast)] = Load(IDC_SIZENWSE);
	Cursors[static_cast<int32>(EMouseCursor::ResizeSouthWest)] = Load(IDC_SIZENESW);
	Cursors[static_cast<int32>(EMouseCursor::CardinalCross)] = Load(IDC_SIZEALL);
	Cursors[static_cast<int32>(EMouseCursor::Crosshairs)] = Load(IDC_CROSS);
	Cursors[static_cast<int32>(EMouseCursor::Hand)] = Load(IDC_HAND);
	Cursors[static_cast<int32>(EMouseCursor::GrabHand)] = Load(IDC_SIZEALL);
	Cursors[static_cast<int32>(EMouseCursor::SlashedCircle)] = Load(IDC_NO);
	Cursors[static_cast<int32>(EMouseCursor::Wait)] = Load(IDC_WAIT);
}

void FWindowsCursor::SetType(EMouseCursor InType)
{
	Type = InType;
	Apply();
}

void FWindowsCursor::Apply() const
{
	::SetCursor(bVisible ? Cursors[static_cast<int32>(Type)] : nullptr);
}

FVector2 FWindowsCursor::GetPosition() const
{
	POINT P{};
	GetCursorPos(&P);
	return FVector2(static_cast<float>(P.x), static_cast<float>(P.y));
}

void FWindowsCursor::SetPosition(int32 X, int32 Y)
{
	SetCursorPos(X, Y);
}

void FWindowsCursor::Show(bool bShow)
{
	bVisible = bShow;
	Apply();
}

void FWindowsCursor::Lock(const FIntRect* ScreenBounds)
{
	if (ScreenBounds)
	{
		const RECT Rect{ ScreenBounds->Left, ScreenBounds->Top, ScreenBounds->Right, ScreenBounds->Bottom };
		ClipCursor(&Rect);
	}
	else
	{
		ClipCursor(nullptr);
	}
}

// ---------------------------------------------------------------------------
// Application

FWindowsApplication::FWindowsApplication()
{
	// The manifest should already make the process per-monitor v2 aware;
	// this covers executables without one (tests, tools). It fails harmlessly
	// when the awareness is already set.
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
	{
		GE_LOG(LogApplicationCore, Warning, "Process is not per-monitor DPI aware (v2); text may be blurry on high-DPI monitors");
	}

	WNDCLASSEXW Class{ sizeof(WNDCLASSEXW) };
	Class.style = CS_DBLCLKS;
	Class.lpfnWndProc = &FWindowsApplication::WndProc;
	Class.hInstance = GetModuleHandleW(nullptr);
	Class.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
	if (!Class.hIcon)
	{
		Class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	}
	Class.hCursor = nullptr; // WM_SETCURSOR decides
	Class.hbrBackground = nullptr;
	Class.lpszClassName = GetWindowClassName();
	if (!RegisterClassExW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
	{
		GE_LOG(LogApplicationCore, Error, "RegisterClassEx failed ({})", GetLastError());
	}
}

FWindowsApplication::~FWindowsApplication()
{
	SetRawMouseInputEnabled(false);
	Windows.clear();
	UnregisterClassW(GetWindowClassName(), GetModuleHandleW(nullptr));
}

TSharedRef<IPlatformWindow> FWindowsApplication::MakeWindow(const FWindowDesc& Desc)
{
	auto Window = std::make_shared<FWindowsWindow>(*this, Desc);
	Windows.push_back(TSharedRef<FWindowsWindow>(Window));
	if (!Window->Initialize())
	{
		RemoveWindow(*Window);
	}
	return TSharedRef<IPlatformWindow>(Window);
}

void FWindowsApplication::RemoveWindow(FWindowsWindow& Window)
{
	std::erase_if(Windows, [&Window](const TSharedRef<FWindowsWindow>& W) { return &W.Get() == &Window; });
}

void FWindowsApplication::PumpMessages(float DeltaTime)
{
	(void)DeltaTime;
	MSG Msg;
	while (PeekMessageW(&Msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&Msg);
		DispatchMessageW(&Msg);
	}
}

void FWindowsApplication::WaitForMessages(float TimeoutSeconds)
{
	const DWORD Milliseconds = static_cast<DWORD>(FMath::Max(0.0f, TimeoutSeconds) * 1000.0f);
	MsgWaitForMultipleObjectsEx(0, nullptr, Milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

void FWindowsApplication::ProcessDeferredEvents(float DeltaTime)
{
	(void)DeltaTime;
	// Handlers may cause new messages (e.g. by creating windows); those are
	// processed next frame.
	TArray<FDeferredMessage> Messages;
	Messages.swap(DeferredMessages);
	for (const FDeferredMessage& Message : Messages)
	{
		ProcessDeferredMessage(Message);
	}
}

void FWindowsApplication::Defer(FWindowsWindow& Window, UINT Msg, WPARAM WParam, LPARAM LParam, int32 X, int32 Y)
{
	DeferredMessages.push_back({ Window.weak_from_this(), Msg, WParam, LParam, X, Y });
}

void FWindowsApplication::ProcessDeferredMessage(const FDeferredMessage& Message)
{
	IPlatformMessageHandler& Handler = *MessageHandler;
	const TSharedPtr<FWindowsWindow> WindowPtr = Message.Window.lock();

	switch (Message.Msg)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYUP:
	{
		const EKey Key = TranslateKey(Message.WParam, Message.LParam);
		const uint32 CharCode = MapVirtualKeyW(static_cast<UINT>(Message.WParam), MAPVK_VK_TO_CHAR);
		if (Message.Msg == WM_KEYDOWN || Message.Msg == WM_SYSKEYDOWN)
		{
			const bool bRepeat = (Message.LParam & (1 << 30)) != 0;
			Handler.OnKeyDown(Key, CharCode, bRepeat);
		}
		else
		{
			Handler.OnKeyUp(Key, CharCode, false);
		}
		return;
	}
	case WM_CHAR:
		Handler.OnKeyChar(static_cast<char32_t>(Message.WParam), (Message.LParam & (1 << 30)) != 0);
		return;
	case WM_INPUT:
		Handler.OnRawMouseMove(Message.X, Message.Y);
		return;
	default:
		break;
	}

	if (!WindowPtr || WindowPtr->IsDestroyed())
	{
		return;
	}
	const TSharedRef<IPlatformWindow> Window(StaticCastSharedPtr<IPlatformWindow>(WindowPtr));
	const FVector2 Pos(static_cast<float>(Message.X), static_cast<float>(Message.Y));

	switch (Message.Msg)
	{
	case WM_LBUTTONDOWN: Handler.OnMouseDown(Window, EMouseButton::Left, Pos); break;
	case WM_RBUTTONDOWN: Handler.OnMouseDown(Window, EMouseButton::Right, Pos); break;
	case WM_MBUTTONDOWN: Handler.OnMouseDown(Window, EMouseButton::Middle, Pos); break;
	case WM_XBUTTONDOWN: Handler.OnMouseDown(Window, GET_XBUTTON_WPARAM(Message.WParam) == XBUTTON1 ? EMouseButton::Thumb1 : EMouseButton::Thumb2, Pos); break;
	case WM_LBUTTONUP: Handler.OnMouseUp(Window, EMouseButton::Left, Pos); break;
	case WM_RBUTTONUP: Handler.OnMouseUp(Window, EMouseButton::Right, Pos); break;
	case WM_MBUTTONUP: Handler.OnMouseUp(Window, EMouseButton::Middle, Pos); break;
	case WM_XBUTTONUP: Handler.OnMouseUp(Window, GET_XBUTTON_WPARAM(Message.WParam) == XBUTTON1 ? EMouseButton::Thumb1 : EMouseButton::Thumb2, Pos); break;
	case WM_LBUTTONDBLCLK: Handler.OnMouseDoubleClick(Window, EMouseButton::Left, Pos); break;
	case WM_RBUTTONDBLCLK: Handler.OnMouseDoubleClick(Window, EMouseButton::Right, Pos); break;
	case WM_MBUTTONDBLCLK: Handler.OnMouseDoubleClick(Window, EMouseButton::Middle, Pos); break;
	case WM_XBUTTONDBLCLK: Handler.OnMouseDoubleClick(Window, GET_XBUTTON_WPARAM(Message.WParam) == XBUTTON1 ? EMouseButton::Thumb1 : EMouseButton::Thumb2, Pos); break;
	case WM_MOUSEMOVE: Handler.OnMouseMove(Window, Pos); break;
	case WM_MOUSELEAVE: Handler.OnMouseLeave(Window); break;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	{
		const float Delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(Message.WParam)) / WHEEL_DELTA;
		Handler.OnMouseWheel(Window, Delta, Message.Msg == WM_MOUSEHWHEEL, Pos);
		break;
	}
	default:
		break;
	}
}

void FWindowsApplication::PollGameDeviceState(float DeltaTime)
{
	(void)DeltaTime;
	constexpr double InitialRepeatDelay = 0.4;
	constexpr double RepeatInterval = 0.1;
	const double Now = FPlatformTime::Seconds();
	IPlatformMessageHandler& Handler = *MessageHandler;

	static constexpr WORD ButtonMasks[] = {
		XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
		XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
		XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
		XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
	};

	for (int32 User = 0; User < static_cast<int32>(XUSER_MAX_COUNT); ++User)
	{
		FGamepadState& Pad = Gamepads[User];
		// Polling an empty slot is slow (it enumerates devices), so
		// disconnected slots are only checked once a second.
		if (!Pad.bConnected && Now < Pad.NextConnectionCheck)
		{
			continue;
		}

		XINPUT_STATE State{};
		const bool bConnected = XInputGetState(static_cast<DWORD>(User), &State) == ERROR_SUCCESS;
		if (bConnected != Pad.bConnected)
		{
			Pad = FGamepadState();
			Pad.bConnected = bConnected;
			Handler.OnGamepadConnectionChanged(User, bConnected);
		}
		if (!bConnected)
		{
			Pad.NextConnectionCheck = Now + 1.0;
			continue;
		}

		const XINPUT_GAMEPAD& G = State.Gamepad;
		bool Pressed[static_cast<int32>(EGamepadButton::Count)] = {};
		for (int32 I = 0; I < static_cast<int32>(std::size(ButtonMasks)); ++I)
		{
			Pressed[I] = (G.wButtons & ButtonMasks[I]) != 0;
		}
		Pressed[static_cast<int32>(EGamepadButton::LeftTriggerThreshold)] = G.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
		Pressed[static_cast<int32>(EGamepadButton::RightTriggerThreshold)] = G.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

		for (int32 I = 0; I < static_cast<int32>(EGamepadButton::Count); ++I)
		{
			const EGamepadButton Button = static_cast<EGamepadButton>(I);
			if (Pressed[I] && !Pad.Buttons[I])
			{
				Handler.OnGamepadButtonDown(User, Button, false);
				Pad.RepeatTime[I] = Now + InitialRepeatDelay;
			}
			else if (!Pressed[I] && Pad.Buttons[I])
			{
				Handler.OnGamepadButtonUp(User, Button);
			}
			else if (Pressed[I] && Now >= Pad.RepeatTime[I])
			{
				Handler.OnGamepadButtonDown(User, Button, true);
				Pad.RepeatTime[I] = Now + RepeatInterval;
			}
			Pad.Buttons[I] = Pressed[I];
		}

		const float Axes[] = {
			ApplyStickDeadZone(G.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
			ApplyStickDeadZone(G.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
			ApplyStickDeadZone(G.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
			ApplyStickDeadZone(G.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
			ApplyTriggerThreshold(G.bLeftTrigger),
			ApplyTriggerThreshold(G.bRightTrigger),
		};
		for (int32 I = 0; I < static_cast<int32>(EGamepadAxis::Count); ++I)
		{
			// Report changes, and keep reporting non-zero values so held
			// sticks drive continuous motion.
			if (Axes[I] != Pad.Axes[I] || Axes[I] != 0.0f)
			{
				Handler.OnGamepadAnalog(User, static_cast<EGamepadAxis>(I), Axes[I]);
			}
			Pad.Axes[I] = Axes[I];
		}
	}
}

bool FWindowsApplication::IsGamepadAttached(int32 UserIndex) const
{
	return UserIndex >= 0 && UserIndex < static_cast<int32>(XUSER_MAX_COUNT) && Gamepads[UserIndex].bConnected;
}

void FWindowsApplication::SetCapture(const TSharedPtr<IPlatformWindow>& Window)
{
	if (Window && Window->GetNativeHandle())
	{
		::SetCapture(static_cast<HWND>(Window->GetNativeHandle()));
	}
	else
	{
		ReleaseCapture();
	}
}

void FWindowsApplication::SetRawMouseInputEnabled(bool bEnable)
{
	if (bEnable == bRawMouse)
	{
		return;
	}
	RAWINPUTDEVICE Device{};
	Device.usUsagePage = 0x01; // generic desktop
	Device.usUsage = 0x02;     // mouse
	Device.dwFlags = bEnable ? 0 : RIDEV_REMOVE;
	Device.hwndTarget = nullptr; // follows keyboard focus
	if (RegisterRawInputDevices(&Device, 1, sizeof(Device)))
	{
		bRawMouse = bEnable;
	}
	else
	{
		GE_LOG(LogApplicationCore, Warning, "RegisterRawInputDevices failed ({})", GetLastError());
	}
}

FModifierKeysState FWindowsApplication::GetModifierKeys() const
{
	const auto Down = [](int Key) { return (GetKeyState(Key) & 0x8000) != 0; };
	FModifierKeysState State;
	State.bLeftShift = Down(VK_LSHIFT);
	State.bRightShift = Down(VK_RSHIFT);
	State.bLeftControl = Down(VK_LCONTROL);
	State.bRightControl = Down(VK_RCONTROL);
	State.bLeftAlt = Down(VK_LMENU);
	State.bRightAlt = Down(VK_RMENU);
	State.bLeftCommand = Down(VK_LWIN);
	State.bRightCommand = Down(VK_RWIN);
	State.bCapsLocked = (GetKeyState(VK_CAPITAL) & 1) != 0;
	return State;
}

void FWindowsApplication::GetDisplayMetrics(FDisplayMetrics& OutMetrics) const
{
	OutMetrics.Monitors.clear();
	EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR Monitor, HDC, LPRECT, LPARAM Param) -> BOOL
	{
		auto& Metrics = *reinterpret_cast<FDisplayMetrics*>(Param);
		MONITORINFOEXW Info{};
		Info.cbSize = sizeof(Info);
		if (GetMonitorInfoW(Monitor, &Info))
		{
			FMonitorInfo M;
			M.Name = FString::FromWide(Info.szDevice);
			M.bIsPrimary = (Info.dwFlags & MONITORINFOF_PRIMARY) != 0;
			M.DisplayRect = FIntRect(Info.rcMonitor.left, Info.rcMonitor.top, Info.rcMonitor.right, Info.rcMonitor.bottom);
			M.WorkArea = FIntRect(Info.rcWork.left, Info.rcWork.top, Info.rcWork.right, Info.rcWork.bottom);
			UINT DpiX = 96, DpiY = 96;
			GetDpiForMonitor(Monitor, MDT_EFFECTIVE_DPI, &DpiX, &DpiY);
			M.DpiScale = static_cast<float>(DpiX) / 96.0f;
			Metrics.Monitors.push_back(M);
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&OutMetrics));

	const int32 X = GetSystemMetrics(SM_XVIRTUALSCREEN);
	const int32 Y = GetSystemMetrics(SM_YVIRTUALSCREEN);
	OutMetrics.VirtualDesktopRect = FIntRect(X, Y, X + GetSystemMetrics(SM_CXVIRTUALSCREEN), Y + GetSystemMetrics(SM_CYVIRTUALSCREEN));
}

void FWindowsApplication::ClipboardCopy(const FString& Text)
{
	if (!OpenClipboard(nullptr))
	{
		return;
	}
	EmptyClipboard();
	const std::wstring Wide = Text.ToWide();
	const SIZE_T Bytes = (Wide.size() + 1) * sizeof(wchar_t);
	if (HGLOBAL Memory = GlobalAlloc(GMEM_MOVEABLE, Bytes))
	{
		if (void* Dest = GlobalLock(Memory))
		{
			memcpy(Dest, Wide.c_str(), Bytes);
			GlobalUnlock(Memory);
			if (!SetClipboardData(CF_UNICODETEXT, Memory))
			{
				GlobalFree(Memory);
			}
		}
		else
		{
			GlobalFree(Memory);
		}
	}
	CloseClipboard();
}

FString FWindowsApplication::ClipboardPaste()
{
	FString Result;
	if (!OpenClipboard(nullptr))
	{
		return Result;
	}
	if (HANDLE Data = GetClipboardData(CF_UNICODETEXT))
	{
		if (const wchar_t* Text = static_cast<const wchar_t*>(GlobalLock(Data)))
		{
			Result = FString::FromWide(Text);
			GlobalUnlock(Data);
		}
	}
	CloseClipboard();
	return Result;
}

// ---------------------------------------------------------------------------
// Window procedure

LRESULT CALLBACK FWindowsApplication::WndProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
	if (Msg == WM_NCCREATE)
	{
		const auto* Create = reinterpret_cast<const CREATESTRUCTW*>(LParam);
		SetWindowLongPtrW(Hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(Create->lpCreateParams));
		static_cast<FWindowsWindow*>(Create->lpCreateParams)->AttachHwnd(Hwnd);
	}

	FWindowsWindow* Window = GetWindowFromHwnd(Hwnd);
	if (!Window)
	{
		return DefWindowProcW(Hwnd, Msg, WParam, LParam);
	}
	// Keep the window alive while its message is handled.
	const TSharedPtr<FWindowsWindow> Pin = Window->weak_from_this().lock();
	if (!Pin)
	{
		// Being destroyed from its destructor.
		return DefWindowProcW(Hwnd, Msg, WParam, LParam);
	}
	return Pin->GetOwner().ProcessMessage(*Pin, Hwnd, Msg, WParam, LParam);
}

LRESULT FWindowsApplication::HitTest(FWindowsWindow& Window, LPARAM LParam)
{
	POINT P{ GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam) };
	ScreenToClient(Window.GetHwnd(), &P);
	const FVector2 Pos(static_cast<float>(P.x), static_cast<float>(P.y));
	const FIntPoint Size = Window.GetClientSize();
	if (Pos.X < 0 || Pos.Y < 0 || Pos.X >= Size.X || Pos.Y >= Size.Y)
	{
		return HTNOWHERE;
	}

	// Resize borders win over whatever the UI draws there.
	const EWindowZone Border = DefaultBorderZone(Window, Pos, Size);
	if (Border != EWindowZone::ClientArea)
	{
		return ZoneToHitTest(Border);
	}
	const TSharedRef<IPlatformWindow> Ref(StaticCastSharedPtr<IPlatformWindow>(Window.shared_from_this()));
	const EWindowZone Zone = MessageHandler->GetWindowZoneForPoint(Ref, Pos);
	return ZoneToHitTest(Zone == EWindowZone::Unspecified ? EWindowZone::ClientArea : Zone);
}

LRESULT FWindowsApplication::ProcessMessage(FWindowsWindow& Window, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
	IPlatformMessageHandler& Handler = *MessageHandler;
	const auto Ref = [&Window]() { return TSharedRef<IPlatformWindow>(StaticCastSharedPtr<IPlatformWindow>(Window.shared_from_this())); };

	switch (Msg)
	{
	// --- Input: queued for ProcessDeferredEvents ---------------------------
	case WM_KEYDOWN:
	case WM_KEYUP:
		Defer(Window, Msg, WParam, LParam);
		return 0;
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		Defer(Window, Msg, WParam, LParam);
		// Let Windows handle Alt+F4 and Alt+Space; swallow the rest so a lone
		// Alt does not enter menu mode and stall the frame loop.
		if (WParam == VK_F4 || WParam == VK_SPACE)
		{
			break;
		}
		return 0;
	case WM_SYSCHAR:
		if (WParam == VK_SPACE)
		{
			break; // system menu
		}
		return 0;
	case WM_CHAR:
	{
		const wchar_t C = static_cast<wchar_t>(WParam);
		if (C >= 0xD800 && C <= 0xDBFF)
		{
			PendingHighSurrogate = C;
			return 0;
		}
		char32_t CodePoint = C;
		if (C >= 0xDC00 && C <= 0xDFFF)
		{
			if (!PendingHighSurrogate)
			{
				return 0;
			}
			CodePoint = 0x10000 + ((static_cast<char32_t>(PendingHighSurrogate) - 0xD800) << 10) + (C - 0xDC00);
		}
		PendingHighSurrogate = 0;
		Defer(Window, Msg, static_cast<WPARAM>(CodePoint), LParam);
		return 0;
	}
	case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
	case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:
	case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK: case WM_XBUTTONDBLCLK:
	case WM_MOUSEMOVE:
	{
		if (Msg == WM_MOUSEMOVE && !Window.bTrackingMouse)
		{
			TRACKMOUSEEVENT Track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, Hwnd, 0 };
			Window.bTrackingMouse = TrackMouseEvent(&Track) != FALSE;
		}
		Defer(Window, Msg, WParam, LParam, GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam));
		return (Msg == WM_XBUTTONDOWN || Msg == WM_XBUTTONUP || Msg == WM_XBUTTONDBLCLK) ? TRUE : 0;
	}
	case WM_MOUSELEAVE:
		Window.bTrackingMouse = false;
		Defer(Window, Msg, WParam, LParam);
		return 0;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	{
		POINT P{ GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam) }; // screen coordinates
		ScreenToClient(Hwnd, &P);
		Defer(Window, Msg, WParam, LParam, P.x, P.y);
		return 0;
	}
	case WM_INPUT:
	{
		RAWINPUT Raw{};
		UINT Size = sizeof(Raw);
		if (GetRawInputData(reinterpret_cast<HRAWINPUT>(LParam), RID_INPUT, &Raw, &Size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1)
			&& Raw.header.dwType == RIM_TYPEMOUSE && !(Raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
			&& (Raw.data.mouse.lLastX != 0 || Raw.data.mouse.lLastY != 0))
		{
			Defer(Window, Msg, 0, 0, Raw.data.mouse.lLastX, Raw.data.mouse.lLastY);
		}
		break; // DefWindowProc cleans up the raw input buffer
	}
	case WM_SETCURSOR:
		if (LOWORD(LParam) == HTCLIENT)
		{
			if (!Handler.OnCursorSet())
			{
				Cursor.Apply();
			}
			return TRUE;
		}
		break;
	case WM_DROPFILES:
	{
		HDROP Drop = reinterpret_cast<HDROP>(WParam);
		TArray<FString> Files;
		const UINT Count = DragQueryFileW(Drop, 0xFFFFFFFF, nullptr, 0);
		for (UINT I = 0; I < Count; ++I)
		{
			std::wstring Name(DragQueryFileW(Drop, I, nullptr, 0) + 1, L'\0');
			DragQueryFileW(Drop, I, Name.data(), static_cast<UINT>(Name.size()));
			Name.resize(Name.size() - 1);
			Files.push_back(FString::FromWide(Name));
		}
		POINT P{};
		DragQueryPoint(Drop, &P);
		DragFinish(Drop);
		Handler.OnFilesDropped(Ref(), Files, FVector2(static_cast<float>(P.x), static_cast<float>(P.y)));
		return 0;
	}

	// --- Window events: handled immediately ---------------------------------
	case WM_SIZE:
		if (WParam == SIZE_MINIMIZED)
		{
			Handler.OnSizeChanged(Ref(), 0, 0, true);
		}
		else
		{
			Handler.OnSizeChanged(Ref(), LOWORD(LParam), HIWORD(LParam), false);
		}
		return 0;
	case WM_MOVE:
		Handler.OnMovedWindow(Ref(), static_cast<int16>(LOWORD(LParam)), static_cast<int16>(HIWORD(LParam)));
		return 0;
	case WM_DPICHANGED:
	{
		const float NewScale = static_cast<float>(HIWORD(WParam)) / 96.0f;
		Window.SetDpiScale(NewScale);
		Handler.OnDpiScaleChanged(Ref(), NewScale);
		// Windows suggests a rectangle that keeps the window's logical size.
		const RECT* Suggested = reinterpret_cast<const RECT*>(LParam);
		SetWindowPos(Hwnd, nullptr, Suggested->left, Suggested->top, Suggested->right - Suggested->left,
			Suggested->bottom - Suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
		return 0;
	}
	case WM_ACTIVATE:
	{
		const WORD State = LOWORD(WParam);
		const EWindowActivation Activation = State == WA_INACTIVE ? EWindowActivation::Deactivate
			: (State == WA_CLICKACTIVE ? EWindowActivation::ActivateByMouse : EWindowActivation::Activate);
		Handler.OnWindowActivationChanged(Ref(), Activation);
		break;
	}
	case WM_ACTIVATEAPP:
		Handler.OnApplicationActivationChanged(WParam != FALSE);
		break;
	case WM_MOUSEACTIVATE:
		if (Window.GetDesc().Type != EWindowType::Normal)
		{
			return MA_NOACTIVATE;
		}
		break;
	case WM_CLOSE:
		Handler.OnWindowClose(Ref());
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT Paint;
		BeginPaint(Hwnd, &Paint);
		EndPaint(Hwnd, &Paint);
		Handler.OnOSPaint(Ref());
		return 0;
	}
	case WM_ENTERSIZEMOVE:
		// The OS runs a modal loop while the user drags or resizes; a timer
		// keeps frames coming so content stays live.
		Window.bInSizeMove = true;
		SetTimer(Hwnd, SizeMoveTimerId, USER_TIMER_MINIMUM, nullptr);
		Handler.OnEnterSizeMove(Ref());
		return 0;
	case WM_EXITSIZEMOVE:
		Window.bInSizeMove = false;
		KillTimer(Hwnd, SizeMoveTimerId);
		Handler.OnExitSizeMove(Ref());
		return 0;
	case WM_TIMER:
		if (WParam == SizeMoveTimerId)
		{
			Handler.OnOSPaint(Ref());
			return 0;
		}
		break;
	case WM_NCCALCSIZE:
		if (WParam && Window.IsBorderless())
		{
			// The whole window is client area. When maximized, Windows
			// positions the window so its (invisible) frame hangs off the
			// monitor; inset by the frame so content stays on screen.
			if (IsZoomed(Hwnd))
			{
				auto* Params = reinterpret_cast<NCCALCSIZE_PARAMS*>(LParam);
				const UINT Dpi = GetDpiForWindow(Hwnd);
				const int32 FrameX = GetSystemMetricsForDpi(SM_CXFRAME, Dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi);
				const int32 FrameY = GetSystemMetricsForDpi(SM_CYFRAME, Dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi);
				Params->rgrc[0].left += FrameX;
				Params->rgrc[0].right -= FrameX;
				Params->rgrc[0].top += FrameY;
				Params->rgrc[0].bottom -= FrameY;
			}
			return 0;
		}
		break;
	case WM_NCHITTEST:
		if (Window.IsBorderless())
		{
			return HitTest(Window, LParam);
		}
		if (Window.GetDesc().Type == EWindowType::ToolTip)
		{
			return HTTRANSPARENT;
		}
		break;
	case WM_NCACTIVATE:
		if (Window.IsBorderless())
		{
			// Keep DWM from repainting a (hidden) caption over our content.
			return DefWindowProcW(Hwnd, Msg, WParam, -1);
		}
		break;
	case WM_DESTROY:
		KillTimer(Hwnd, SizeMoveTimerId);
		Window.OnNativeDestroyed();
		// The Pin in WndProc keeps the object alive until this returns.
		RemoveWindow(Window);
		return 0;
	default:
		break;
	}
	return DefWindowProcW(Hwnd, Msg, WParam, LParam);
}
