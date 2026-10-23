// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Null/NullPlatformApplication.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"

FNullPlatformWindow::FNullPlatformWindow(FNullPlatformApplication& InOwner, const FWindowDesc& InDesc)
    : Owner(InOwner), Desc(InDesc), DpiScale(InOwner.GetDefaultDpiScale())
{
	ClientSize = FIntPoint(FMath::RoundToInt(Desc.Size.X * DpiScale), FMath::RoundToInt(Desc.Size.Y * DpiScale));
	if (Desc.Position)
	{
		ClientPosition = *Desc.Position;
	}
	bVisible = Desc.bShowImmediately;
}

void FNullPlatformWindow::Minimize()
{
	if (!bMinimized)
	{
		bMinimized = true;
		Owner.GetMessageHandler()->OnSizeChanged(TSharedRef<IPlatformWindow>(shared_from_this()), 0, 0, true);
	}
}

void FNullPlatformWindow::Restore()
{
	const bool bWasMinimized = bMinimized;
	bMinimized = false;
	bMaximized = false;
	if (bWasMinimized)
	{
		Owner.GetMessageHandler()->OnSizeChanged(
		    TSharedRef<IPlatformWindow>(shared_from_this()), ClientSize.X, ClientSize.Y, false);
	}
}

void FNullPlatformWindow::MoveWindowTo(int32 X, int32 Y)
{
	ClientPosition = FIntPoint(X, Y);
	Owner.GetMessageHandler()->OnMovedWindow(TSharedRef<IPlatformWindow>(shared_from_this()), X, Y);
}

void FNullPlatformWindow::ReshapeWindow(int32 X, int32 Y, int32 Width, int32 Height)
{
	ClientPosition = FIntPoint(X, Y);
	ClientSize = FIntPoint(Width, Height);
	Owner.GetMessageHandler()->OnSizeChanged(TSharedRef<IPlatformWindow>(shared_from_this()), Width, Height, false);
}

void FNullPlatformWindow::SimulateDpiChange(float NewScale)
{
	check(NewScale > 0.0f);
	const float Ratio = NewScale / DpiScale;
	DpiScale = NewScale;
	ClientSize = FIntPoint(FMath::RoundToInt(ClientSize.X * Ratio), FMath::RoundToInt(ClientSize.Y * Ratio));
	const TSharedRef<IPlatformWindow> Self(shared_from_this());
	Owner.GetMessageHandler()->OnDpiScaleChanged(Self, NewScale);
	Owner.GetMessageHandler()->OnSizeChanged(Self, ClientSize.X, ClientSize.Y, false);
}

FNullPlatformApplication::FNullPlatformApplication(float InDefaultDpiScale) : DefaultDpiScale(InDefaultDpiScale) {}

TSharedRef<IPlatformWindow> FNullPlatformApplication::MakeWindow(const FWindowDesc& Desc)
{
	TSharedPtr<FNullPlatformWindow> Window = std::make_shared<FNullPlatformWindow>(*this, Desc);
	Windows.push_back(Window);
	return TSharedRef<IPlatformWindow>(Window);
}

void FNullPlatformApplication::ProcessDeferredEvents(float DeltaTime)
{
	(void)DeltaTime;
	// Events queued by handlers run on the next call, like the real platform.
	std::deque<TFunction<void(IPlatformMessageHandler&)>> Pending;
	Pending.swap(Events);
	const TSharedPtr<IPlatformMessageHandler> Handler = GetMessageHandler();
	for (std::function<void(IPlatformMessageHandler&)>& Event : Pending)
	{
		Event(*Handler);
	}
}

void FNullPlatformApplication::GetDisplayMetrics(FDisplayMetrics& OutMetrics) const
{
	FMonitorInfo Monitor;
	Monitor.Name = "Null";
	Monitor.bIsPrimary = true;
	Monitor.DisplayRect = FIntRect(0, 0, 1920, 1080);
	Monitor.WorkArea = FIntRect(0, 0, 1920, 1040);
	Monitor.DpiScale = DefaultDpiScale;
	OutMetrics.Monitors = {Monitor};
	OutMetrics.VirtualDesktopRect = Monitor.DisplayRect;
}

void FNullPlatformApplication::QueueKeyDown(EKey Key, bool bIsRepeat)
{
	Events.push_back([Key, bIsRepeat](IPlatformMessageHandler& H) { H.OnKeyDown(Key, 0, bIsRepeat); });
}

void FNullPlatformApplication::QueueKeyUp(EKey Key)
{
	Events.push_back([Key](IPlatformMessageHandler& H) { H.OnKeyUp(Key, 0, false); });
}

void FNullPlatformApplication::QueueChar(char32_t Character)
{
	Events.push_back([Character](IPlatformMessageHandler& H) { H.OnKeyChar(Character, false); });
}

void FNullPlatformApplication::QueueMouseMove(const TSharedRef<IPlatformWindow>& Window, FVector2 Position)
{
	Events.push_back([Window, Position](IPlatformMessageHandler& H) { H.OnMouseMove(Window, Position); });
}

void FNullPlatformApplication::QueueMouseDown(
    const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position)
{
	Events.push_back(
	    [Window, Button, Position](IPlatformMessageHandler& H) { H.OnMouseDown(Window, Button, Position); });
}

void FNullPlatformApplication::QueueMouseUp(
    const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position)
{
	Events.push_back([Window, Button, Position](IPlatformMessageHandler& H) { H.OnMouseUp(Window, Button, Position); });
}

void FNullPlatformApplication::QueueMouseWheel(
    const TSharedRef<IPlatformWindow>& Window, float Delta, FVector2 Position)
{
	Events.push_back(
	    [Window, Delta, Position](IPlatformMessageHandler& H) { H.OnMouseWheel(Window, Delta, false, Position); });
}

void FNullPlatformApplication::QueueCompositionBegin()
{
	Events.push_back([this](IPlatformMessageHandler&)
	{
		if (const TSharedPtr<ITextInputMethodContext>& Context = TextInputMethod.GetActiveContext())
		{
			Context->BeginComposition();
		}
	});
}

void FNullPlatformApplication::QueueCompositionUpdate(const FString& Text, int32 CaretIndex)
{
	Events.push_back([this, Text, CaretIndex](IPlatformMessageHandler&)
	{
		if (const TSharedPtr<ITextInputMethodContext>& Context = TextInputMethod.GetActiveContext())
		{
			Context->UpdateComposition(Text, CaretIndex);
		}
	});
}

void FNullPlatformApplication::QueueCompositionEnd(const FString& Committed)
{
	Events.push_back([this](IPlatformMessageHandler&)
	{
		if (const TSharedPtr<ITextInputMethodContext>& Context = TextInputMethod.GetActiveContext())
		{
			Context->EndComposition();
		}
	});
	const char* Read = *Committed;
	const char* const End = Read + Committed.Len();
	while (Read < End)
	{
		QueueChar(FUtf8::Decode(Read, End));
	}
}

void FNullPlatformApplication::QueueRawMouseMove(int32 DeltaX, int32 DeltaY)
{
	Events.push_back([this, DeltaX, DeltaY](IPlatformMessageHandler& H)
	{
		if (bRawMouse)
		{
			H.OnRawMouseMove(DeltaX, DeltaY);
		}
	});
}
