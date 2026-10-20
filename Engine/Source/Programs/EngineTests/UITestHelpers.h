// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// A headless UI application for widget tests: the null platform, the
// Genesis style, a scripted clock and helpers that send input in logical
// units.

#pragma once

#include "TestHelpers.h"

#include "Application/UIApplication.h"
#include "Null/NullPlatformApplication.h"
#include "Styling/GenesisStyle.h"
#include "Widgets/Layout/SBox.h"

class FUITestApp
{
public:
	explicit FUITestApp(float DpiScale = 1.0f);
	~FUITestApp();
	FUITestApp(const FUITestApp&) = delete;
	FUITestApp& operator=(const FUITestApp&) = delete;

	// Opens a window with Content filling it (logical size).
	TSharedRef<SWindow> AddWindow(const TSharedRef<SWidget>& Content, FVector2 Size = FVector2(400.0f, 300.0f));

	// Advances the clock and runs one frame.
	void Tick(float Seconds = 1.0f / 60.0f);
	// Runs frames until Seconds have passed.
	void Wait(float Seconds);

	// Input at logical positions in Window (delivered on the next Tick).
	void MouseMove(const TSharedRef<SWindow>& Window, FVector2 Logical);
	void MouseDown(const TSharedRef<SWindow>& Window, FVector2 Logical, EMouseButton Button = EMouseButton::Left);
	void MouseUp(const TSharedRef<SWindow>& Window, FVector2 Logical, EMouseButton Button = EMouseButton::Left);
	void Click(const TSharedRef<SWindow>& Window, FVector2 Logical, EMouseButton Button = EMouseButton::Left);
	void Wheel(const TSharedRef<SWindow>& Window, FVector2 Logical, float Delta);
	// Clicks the center of a widget.
	void ClickWidget(const SWidget& Widget, EMouseButton Button = EMouseButton::Left);
	void HoverWidget(const SWidget& Widget);
	void Key(EKey Key, bool bShift = false, bool bControl = false);
	void Type(std::string_view Text);
	void SetModifiers(bool bShift, bool bControl, bool bAlt = false);

	// Center of a widget in its window, logical units.
	static FVector2 CenterOf(const SWidget& Widget);
	// Local rectangle of a widget relative to its window, logical units.
	static FRect RectOf(const SWidget& Widget);

	FNullPlatformApplication& GetPlatform() { return *Platform; }
	FUIApplication& GetApp() { return *App; }
	FNullPlatformWindow& Native(const TSharedRef<SWindow>& Window) { return static_cast<FNullPlatformWindow&>(*Window->GetNativeWindow()); }

	double Time = 1000.0;

private:
	TSharedPtr<FNullPlatformApplication> Platform;
	TSharedPtr<FUIApplication> App;
};

// The fonts and style used by every UI test (loaded once).
struct FUITestStyle
{
	static void EnsureInitialized();
	static TSharedRef<FFontCache> GetFontCache();
};
