// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "UITestHelpers.h"

namespace
{
	TSharedPtr<FSlateFontCache>& SharedFontCache()
	{
		static TSharedPtr<FSlateFontCache> Cache;
		return Cache;
	}
}

void FUITestStyle::EnsureInitialized()
{
	if (SharedFontCache())
	{
		return;
	}
	SharedFontCache() = std::make_shared<FSlateFontCache>();
	const FStarshipCoreFonts Fonts = FStarshipCoreFonts::Load(*SharedFontCache());
	REQUIRE(Fonts.IsValid());
	const TSharedRef<FSlateStyleSet> Style = FStarshipCoreStyle::Create(Fonts);
	FSlateStyleSet::Register(Style);
	FAppStyle::SetAppStyleSet(Style);
}

TSharedRef<FSlateFontCache> FUITestStyle::GetFontCache()
{
	EnsureInitialized();
	return ToSharedRef(SharedFontCache());
}

FUITestApp::FUITestApp(float DpiScale)
{
	FUITestStyle::EnsureInitialized();
	Platform = std::make_shared<FNullPlatformApplication>(DpiScale);
	App = FSlateApplication::Create(TSharedRef<IPlatformApplication>(StaticCastSharedPtr<IPlatformApplication>(Platform)), FUITestStyle::GetFontCache()).ToSharedPtr();
	App->SetTimeSource([this]() { return Time; });
}

FUITestApp::~FUITestApp()
{
	App.reset();
	FSlateApplication::Shutdown();
}

TSharedRef<SWindow> FUITestApp::AddWindow(const TSharedRef<SWidget>& Content, FVector2 Size)
{
	const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(Size)[Content];
	App->AddWindow(Window);
	Tick();
	return Window;
}

void FUITestApp::Tick(float Seconds)
{
	Time += Seconds;
	App->Tick();
}

void FUITestApp::Wait(float Seconds)
{
	const float Step = 1.0f / 60.0f;
	for (float T = 0.0f; T < Seconds; T += Step)
	{
		Tick(Step);
	}
}

void FUITestApp::MouseMove(const TSharedRef<SWindow>& Window, FVector2 Logical)
{
	Platform->QueueMouseMove(ToSharedRef(Window->GetNativeWindow()), Logical * Window->GetScale());
}

void FUITestApp::MouseDown(const TSharedRef<SWindow>& Window, FVector2 Logical, EMouseButton Button)
{
	Platform->QueueMouseDown(ToSharedRef(Window->GetNativeWindow()), Button, Logical * Window->GetScale());
}

void FUITestApp::MouseUp(const TSharedRef<SWindow>& Window, FVector2 Logical, EMouseButton Button)
{
	Platform->QueueMouseUp(ToSharedRef(Window->GetNativeWindow()), Button, Logical * Window->GetScale());
}

void FUITestApp::Click(const TSharedRef<SWindow>& Window, FVector2 Logical, EMouseButton Button)
{
	MouseMove(Window, Logical);
	MouseDown(Window, Logical, Button);
	MouseUp(Window, Logical, Button);
	Tick();
}

void FUITestApp::Wheel(const TSharedRef<SWindow>& Window, FVector2 Logical, float Delta)
{
	MouseMove(Window, Logical);
	Platform->QueueMouseWheel(ToSharedRef(Window->GetNativeWindow()), Delta, Logical * Window->GetScale());
	Tick();
}

void FUITestApp::ClickWidget(const SWidget& Widget, EMouseButton Button)
{
	const TSharedPtr<SWindow> Window = App->FindWindowForWidget(Widget);
	REQUIRE(Window);
	Click(ToSharedRef(Window), CenterOf(Widget), Button);
}

void FUITestApp::HoverWidget(const SWidget& Widget)
{
	const TSharedPtr<SWindow> Window = App->FindWindowForWidget(Widget);
	REQUIRE(Window);
	MouseMove(ToSharedRef(Window), CenterOf(Widget));
	Tick();
}

void FUITestApp::SetModifiers(bool bShift, bool bControl, bool bAlt)
{
	FModifierKeysState State;
	State.bLeftShift = bShift;
	State.bLeftControl = bControl;
	State.bLeftAlt = bAlt;
	Platform->SetModifierKeys(State);
}

void FUITestApp::Key(EKey InKey, bool bShift, bool bControl)
{
	SetModifiers(bShift, bControl);
	Platform->QueueKeyDown(InKey);
	Platform->QueueKeyUp(InKey);
	Tick();
	SetModifiers(false, false);
}

void FUITestApp::Type(std::string_view Text)
{
	const char* Cursor = Text.data();
	const char* const End = Cursor + Text.size();
	while (Cursor < End)
	{
		Platform->QueueChar(FUtf8::Decode(Cursor, End));
	}
	Tick();
}

FRect FUITestApp::RectOf(const SWidget& Widget)
{
	const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWindowForWidget(Widget);
	REQUIRE(Window);
	return Widget.GetCachedGeometry().GetLayoutBoundingRect().Scale(1.0f / Window->GetScale());
}

FVector2 FUITestApp::CenterOf(const SWidget& Widget)
{
	return RectOf(Widget).GetCenter();
}
