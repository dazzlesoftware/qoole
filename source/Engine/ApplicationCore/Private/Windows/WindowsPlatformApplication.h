// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "GenericPlatform/PlatformApplication.h"

#include <Windows.h>
#include <imm.h>
#include <Xinput.h>

class FWindowsApplication;

class FWindowsWindow final : public IPlatformWindow, public TSharedFromThis<FWindowsWindow>
{
public:
	FWindowsWindow(FWindowsApplication& InOwner, const FWindowDesc& InDesc);
	~FWindowsWindow() override;

	// Creates the native window; called once right after construction.
	bool Initialize();

	const FWindowDesc& GetDesc() const override { return Desc; }
	void Show() override;
	void Hide() override;
	void Minimize() override;
	void Maximize() override;
	void Restore() override;
	void BringToFront(bool bForce) override;
	void Destroy() override;
	void SetTitle(const FString& Title) override;
	void SetOpacity(float Opacity) override;
	void Enable(bool bEnable) override;
	void MoveWindowTo(int32 X, int32 Y) override;
	void ReshapeWindow(int32 X, int32 Y, int32 Width, int32 Height) override;
	FIntPoint GetClientSize() const override;
	FIntPoint GetClientPosition() const override;
	float GetDpiScale() const override { return DpiScale; }
	bool IsVisible() const override;
	bool IsMinimized() const override;
	bool IsMaximized() const override;
	bool IsForegroundWindow() const override;
	bool IsDestroyed() const override { return Hwnd == nullptr; }
	void* GetNativeHandle() const override { return Hwnd; }

	HWND GetHwnd() const { return Hwnd; }
	FWindowsApplication& GetOwner() const { return Owner; }
	bool IsBorderless() const { return !Desc.bUseOSBorder && Desc.Type == EWindowType::Normal; }
	void SetDpiScale(float Scale) { DpiScale = Scale; }
	void AttachHwnd(HWND InHwnd) { Hwnd = InHwnd; }
	void OnNativeDestroyed() { Hwnd = nullptr; }

	bool bTrackingMouse = false;
	bool bInSizeMove = false;

private:
	FWindowsApplication& Owner;
	FWindowDesc Desc;
	HWND Hwnd = nullptr;
	float DpiScale = 1.0f;
};

class FWindowsCursor final : public ICursor
{
public:
	FWindowsCursor();

	EMouseCursor GetType() const override { return Type; }
	void SetType(EMouseCursor InType) override;
	FVector2 GetPosition() const override;
	void SetPosition(int32 X, int32 Y) override;
	void Show(bool bShow) override;
	void Lock(const FIntRect* ScreenBounds) override;

	// Applies the current shape (from WM_SETCURSOR).
	void Apply() const;

private:
	HCURSOR Cursors[static_cast<int32>(EMouseCursor::Count)] = {};
	EMouseCursor Type = EMouseCursor::Default;
	bool bVisible = true;
};

// IME through IMM32 (docs/GenesisUI.md §4). The IME is disabled on our
// windows unless a text context is active, so keys go straight to games and
// viewports; while one is active the composition is drawn inline by the
// widget and the candidate window follows its caret.
class FWindowsTextInputMethodSystem final : public ITextInputMethodSystem
{
public:
	void ActivateContext(const TSharedRef<ITextInputMethodContext>& Context) override;
	void DeactivateContext(const TSharedRef<ITextInputMethodContext>& Context) override;
	void UpdateContextLayout(const TSharedRef<ITextInputMethodContext>& Context) override;

	// The active context if it belongs to Hwnd.
	ITextInputMethodContext* GetContextFor(HWND Hwnd) const;
	void PositionCandidateWindow(HWND Hwnd) const;

private:
	TSharedPtr<ITextInputMethodContext> Active;
	HWND ActiveHwnd = nullptr;
};

class FWindowsApplication final : public IPlatformApplication
{
public:
	FWindowsApplication();
	~FWindowsApplication() override;

	void PumpMessages(float DeltaTime) override;
	void ProcessDeferredEvents(float DeltaTime) override;
	void PollGameDeviceState(float DeltaTime) override;
	void WaitForMessages(float TimeoutSeconds) override;
	TSharedRef<IPlatformWindow> MakeWindow(const FWindowDesc& Desc) override;
	ICursor* GetCursor() override { return &Cursor; }
	void SetCapture(const TSharedPtr<IPlatformWindow>& Window) override;
	void SetRawMouseInputEnabled(bool bEnable) override;
	FModifierKeysState GetModifierKeys() const override;
	bool IsGamepadAttached(int32 UserIndex) const override;
	void GetDisplayMetrics(FDisplayMetrics& OutMetrics) const override;
	void ClipboardCopy(const FString& Text) override;
	FString ClipboardPaste() override;
	ITextInputMethodSystem* GetTextInputMethodSystem() override { return &TextInputMethod; }

	static LRESULT CALLBACK WndProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam);
	static const wchar_t* GetWindowClassName() { return L"GenesisWindow"; }

private:
	struct FDeferredMessage
	{
		TWeakPtr<FWindowsWindow> Window;
		UINT Msg = 0;
		WPARAM WParam = 0;
		LPARAM LParam = 0;
		int32 X = 0; // raw mouse deltas or client position
		int32 Y = 0;
		FString Text; // IME composition text
	};

	struct FGamepadState
	{
		bool bConnected = false;
		double NextConnectionCheck = 0.0;
		bool Buttons[static_cast<int32>(EGamepadButton::Count)] = {};
		double RepeatTime[static_cast<int32>(EGamepadButton::Count)] = {};
		float Axes[static_cast<int32>(EGamepadAxis::Count)] = {};
	};

	LRESULT ProcessMessage(FWindowsWindow& Window, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam);
	void ProcessDeferredMessage(const FDeferredMessage& Message);
	void Defer(FWindowsWindow& Window, UINT Msg, WPARAM WParam, LPARAM LParam, int32 X = 0, int32 Y = 0);
	bool ProcessImeMessage(FWindowsWindow& Window, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM& LParam);
	LRESULT ProcessImeMessage(FWindowsWindow& Window, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam, bool& bHandled);
	LRESULT HitTest(FWindowsWindow& Window, LPARAM LParam);
	void RemoveWindow(FWindowsWindow& Window);

	TArray<TSharedRef<FWindowsWindow>> Windows;
	TArray<FDeferredMessage> DeferredMessages;
	FWindowsCursor Cursor;
	FWindowsTextInputMethodSystem TextInputMethod;
	FGamepadState Gamepads[XUSER_MAX_COUNT];
	wchar_t PendingHighSurrogate = 0;
	bool bRawMouse = false;
};
