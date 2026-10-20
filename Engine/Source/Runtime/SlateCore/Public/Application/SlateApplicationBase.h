// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// What the widget core needs from the application (focus, capture, time,
// timers, fonts) without depending on the UI module. FSlateApplication
// (UI/Framework/Application/SlateApplication.h) is the implementation; there is at most
// one at a time.

#pragma once

#include "CoreMinimal.h"
#include "Types/WidgetTypes.h"
#include "Widgets/Attribute.h"

class SWidget;
class FSlateFontCache;
class IToolTip;
class FActiveTimerHandle;

using FWidgetActiveTimerDelegate = TFunction<EActiveTimerReturn(double CurrentTime, float DeltaTime)>;

// A registered active timer. Destroying the last reference does not stop
// it; call SWidget::UnRegisterActiveTimer or return Stop.
class FActiveTimerHandle
{
public:
	FActiveTimerHandle(float InPeriod, FWidgetActiveTimerDelegate InDelegate, double InNextExecution)
		: Period(InPeriod), Delegate(std::move(InDelegate)), NextExecution(InNextExecution)
	{
	}

	float Period;                     // seconds; 0 = every frame
	FWidgetActiveTimerDelegate Delegate;
	double NextExecution;
	bool bPendingRemoval = false;
};

class FSlateApplicationBase
{
public:
	virtual ~FSlateApplicationBase() = default;

	static bool IsInitialized() { return CurrentApplication != nullptr; }
	static FSlateApplicationBase& Get()
	{
		check(CurrentApplication);
		return *CurrentApplication;
	}

	// User 0 unless noted.
	virtual bool HasFocus(const SWidget* Widget, int32 UserIndex = 0) const = 0;
	virtual bool HasFocusedDescendants(const SWidget* Widget, int32 UserIndex = 0) const = 0;
	virtual bool HasMouseCapture(const SWidget* Widget) const = 0;
	virtual bool IsDragDropping() const = 0;

	virtual double GetTime() const = 0;
	virtual float GetDeltaTime() const = 0;

	virtual FSlateFontCache& GetFontCache() = 0;

	// Called by SWidget; the widget is held weakly.
	virtual void RegisterActiveTimer(const TSharedRef<SWidget>& Widget, const TSharedRef<FActiveTimerHandle>& Timer) = 0;
	virtual void UnRegisterActiveTimer(const TSharedRef<SWidget>& Widget, const TSharedRef<FActiveTimerHandle>& Timer) = 0;
	virtual void RegisterTickingWidget(const TSharedRef<SWidget>& Widget) = 0;
	virtual void RegisterVolatileWidget(const TSharedRef<SWidget>& Widget) = 0;

	// The widget is being destroyed (or hidden): drop focus/capture/hover on it.
	virtual void OnWidgetDestroyed(const SWidget* Widget) = 0;

	// Makes a tooltip widget showing text (implemented by the UI module).
	virtual TSharedRef<IToolTip> MakeToolTip(const TAttribute<FString>& Text) = 0;

protected:
	static void SetCurrent(FSlateApplicationBase* App) { CurrentApplication = App; }

private:
	static inline FSlateApplicationBase* CurrentApplication = nullptr;
};
