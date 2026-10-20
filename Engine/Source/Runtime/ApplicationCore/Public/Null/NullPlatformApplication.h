// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// A headless platform: windows exist only as data and input is scripted.
// Layout, input and renderer tests run on it (Engine/Documentation/Source/GenesisUI.md §4).

#pragma once

#include "GenericPlatform/PlatformApplication.h"

#include <deque>

class FNullPlatformApplication;

class FNullPlatformWindow final : public IPlatformWindow, public TSharedFromThis<FNullPlatformWindow>
{
public:
	FNullPlatformWindow(FNullPlatformApplication& InOwner, const FWindowDesc& InDesc);

	const FWindowDesc& GetDesc() const override { return Desc; }
	void Show() override { bVisible = true; }
	void Hide() override { bVisible = false; }
	void Minimize() override;
	void Maximize() override { bMinimized = false; bMaximized = true; }
	void Restore() override;
	void BringToFront(bool) override {}
	void Destroy() override { bDestroyed = true; bVisible = false; }
	void SetTitle(const FString& Title) override { Desc.Title = Title; }
	void SetOpacity(float Opacity) override { Desc.Opacity = Opacity; }
	void Enable(bool bEnable) override { bEnabled = bEnable; }
	void MoveWindowTo(int32 X, int32 Y) override;
	void ReshapeWindow(int32 X, int32 Y, int32 Width, int32 Height) override;
	FIntPoint GetClientSize() const override { return bMinimized ? FIntPoint(0, 0) : ClientSize; }
	FIntPoint GetClientPosition() const override { return ClientPosition; }
	float GetDpiScale() const override { return DpiScale; }
	bool IsVisible() const override { return bVisible; }
	bool IsMinimized() const override { return bMinimized; }
	bool IsMaximized() const override { return bMaximized; }
	bool IsForegroundWindow() const override { return bVisible && !bMinimized; }
	bool IsDestroyed() const override { return bDestroyed; }
	void* GetNativeHandle() const override { return nullptr; }

	// Simulates the window moving to a monitor with another DPI: rescales the
	// client area and notifies the message handler.
	void SimulateDpiChange(float NewScale);
	bool IsEnabled() const { return bEnabled; }

private:
	FNullPlatformApplication& Owner;
	FWindowDesc Desc;
	FIntPoint ClientSize;
	FIntPoint ClientPosition;
	float DpiScale = 1.0f;
	bool bVisible = false;
	bool bMinimized = false;
	bool bMaximized = false;
	bool bDestroyed = false;
	bool bEnabled = true;
};

// Records which context is active; compositions are scripted through
// FNullPlatformApplication::QueueComposition*.
class FNullTextInputMethodSystem final : public ITextInputMethodSystem
{
public:
	void ActivateContext(const TSharedRef<ITextInputMethodContext>& Context) override { Active = Context.ToSharedPtr(); }
	void DeactivateContext(const TSharedRef<ITextInputMethodContext>& Context) override
	{
		if (Active == Context.ToSharedPtr())
		{
			Active.reset();
		}
	}
	void UpdateContextLayout(const TSharedRef<ITextInputMethodContext>& Context) override { (void)Context; ++LayoutUpdates; }

	const TSharedPtr<ITextInputMethodContext>& GetActiveContext() const { return Active; }
	int32 GetLayoutUpdateCount() const { return LayoutUpdates; }

private:
	TSharedPtr<ITextInputMethodContext> Active;
	int32 LayoutUpdates = 0;
};

class FNullPlatformApplication final : public IPlatformApplication
{
public:
	explicit FNullPlatformApplication(float InDefaultDpiScale = 1.0f);

	void PumpMessages(float DeltaTime) override { (void)DeltaTime; }
	void ProcessDeferredEvents(float DeltaTime) override;
	void PollGameDeviceState(float DeltaTime) override { (void)DeltaTime; }
	void WaitForMessages(float TimeoutSeconds) override { (void)TimeoutSeconds; }

	TSharedRef<IPlatformWindow> MakeWindow(const FWindowDesc& Desc) override;

	ICursor* GetCursor() override { return &Cursor; }
	void SetCapture(const TSharedPtr<IPlatformWindow>& Window) override { CaptureWindow = Window; }
	void SetRawMouseInputEnabled(bool bEnable) override { bRawMouse = bEnable; }
	FModifierKeysState GetModifierKeys() const override { return Modifiers; }
	bool IsGamepadAttached(int32 UserIndex) const override { (void)UserIndex; return false; }
	void GetDisplayMetrics(FDisplayMetrics& OutMetrics) const override;
	void ClipboardCopy(const FString& Text) override { Clipboard = Text; }
	FString ClipboardPaste() override { return Clipboard; }
	bool IsHeadless() const override { return true; }
	ITextInputMethodSystem* GetTextInputMethodSystem() override { return &TextInputMethod; }

	// Scripted input, delivered on the next ProcessDeferredEvents.
	void QueueKeyDown(EKey Key, bool bIsRepeat = false);
	void QueueKeyUp(EKey Key);
	void QueueChar(char32_t Character);
	void QueueMouseMove(const TSharedRef<IPlatformWindow>& Window, FVector2 Position);
	void QueueMouseDown(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position);
	void QueueMouseUp(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position);
	void QueueMouseWheel(const TSharedRef<IPlatformWindow>& Window, float Delta, FVector2 Position);
	void QueueRawMouseMove(int32 DeltaX, int32 DeltaY);
	void SetModifierKeys(const FModifierKeysState& State) { Modifiers = State; }
	// IME composition sent to the active input context (dropped when none).
	void QueueCompositionBegin();
	void QueueCompositionUpdate(const FString& Text, int32 CaretIndex);
	// Ends the composition and types Committed (may be empty: cancelled).
	void QueueCompositionEnd(const FString& Committed);
	FNullTextInputMethodSystem& GetNullTextInputMethod() { return TextInputMethod; }

	int32 GetPendingEventCount() const { return static_cast<int32>(Events.size()); }
	const TArray<TWeakPtr<FNullPlatformWindow>>& GetWindows() const { return Windows; }
	TSharedPtr<IPlatformWindow> GetCaptureWindow() const { return CaptureWindow.lock(); }
	bool IsRawMouseEnabled() const { return bRawMouse; }
	float GetDefaultDpiScale() const { return DefaultDpiScale; }

private:
	class FNullCursor final : public ICursor
	{
	public:
		EMouseCursor GetType() const override { return Type; }
		void SetType(EMouseCursor InType) override { Type = InType; }
		FVector2 GetPosition() const override { return Position; }
		void SetPosition(int32 X, int32 Y) override { Position = FVector2(float(X), float(Y)); }
		void Show(bool bShow) override { bVisible = bShow; }
		void Lock(const FIntRect* Bounds) override { bLocked = Bounds != nullptr; }

		EMouseCursor Type = EMouseCursor::Default;
		FVector2 Position;
		bool bVisible = true;
		bool bLocked = false;
	};

	TArray<TWeakPtr<FNullPlatformWindow>> Windows;
	std::deque<TFunction<void(IPlatformMessageHandler&)>> Events;
	FNullCursor Cursor;
	FNullTextInputMethodSystem TextInputMethod;
	FModifierKeysState Modifiers;
	TWeakPtr<IPlatformWindow> CaptureWindow;
	FString Clipboard;
	float DefaultDpiScale = 1.0f;
	bool bRawMouse = false;
};
