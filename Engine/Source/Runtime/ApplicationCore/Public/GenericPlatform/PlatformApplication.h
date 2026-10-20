// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "GenericPlatform/Cursor.h"
#include "GenericPlatform/PlatformMessageHandler.h"
#include "GenericPlatform/PlatformWindow.h"
#include "GenericPlatform/TextInputMethod.h"

DECLARE_LOG_CATEGORY_EXTERN(LogApplicationCore, Log);

struct FMonitorInfo
{
	FString Name;
	bool bIsPrimary = false;
	FIntRect DisplayRect; // screen pixels
	FIntRect WorkArea;    // without taskbars
	float DpiScale = 1.0f;
};

struct FDisplayMetrics
{
	TArray<FMonitorInfo> Monitors;
	FIntRect VirtualDesktopRect;

	const FMonitorInfo* GetPrimary() const;
};

// OS application services (docs/GenesisUI.md §4). One instance per process.
class IPlatformApplication
{
public:
	virtual ~IPlatformApplication() = default;

	virtual void SetMessageHandler(const TSharedRef<IPlatformMessageHandler>& Handler) { MessageHandler = Handler.ToSharedPtr(); }
	TSharedPtr<IPlatformMessageHandler> GetMessageHandler() const { return MessageHandler; }

	// Reads pending OS messages. Window events are dispatched right away,
	// input is queued for ProcessDeferredEvents.
	virtual void PumpMessages(float DeltaTime) = 0;
	// Dispatches queued input to the message handler.
	virtual void ProcessDeferredEvents(float DeltaTime) = 0;
	// Polls gamepads and dispatches their changes.
	virtual void PollGameDeviceState(float DeltaTime) = 0;
	// Blocks until an OS message arrives or the timeout elapses (idle frames).
	virtual void WaitForMessages(float TimeoutSeconds) = 0;

	virtual TSharedRef<IPlatformWindow> MakeWindow(const FWindowDesc& Desc) = 0;

	virtual ICursor* GetCursor() = 0;
	virtual void SetCapture(const TSharedPtr<IPlatformWindow>& Window) = 0;
	// Raw (unaccelerated) mouse deltas via OnRawMouseMove.
	virtual void SetRawMouseInputEnabled(bool bEnable) = 0;
	virtual FModifierKeysState GetModifierKeys() const = 0;
	virtual bool IsGamepadAttached(int32 UserIndex) const = 0;

	virtual void GetDisplayMetrics(FDisplayMetrics& OutMetrics) const = 0;

	// Input method editor support; nullptr when the platform has none.
	virtual ITextInputMethodSystem* GetTextInputMethodSystem() { return nullptr; }

	virtual void ClipboardCopy(const FString& Text) = 0;
	virtual FString ClipboardPaste() = 0;

	// Whether the process is a real windowed app (false for the null platform).
	virtual bool IsHeadless() const { return false; }

protected:
	TSharedPtr<IPlatformMessageHandler> MessageHandler = std::make_shared<IPlatformMessageHandler>();
};

// Creates the platform application for this OS (Win32 on Windows).
TSharedRef<IPlatformApplication> CreatePlatformApplication();
