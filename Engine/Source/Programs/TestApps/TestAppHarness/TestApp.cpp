// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestApp.h"

#include "Screenshot.h"
#include "Services/ConfigStore.h"
#include "Services/Services.h"
#include "Widgets/Text/STextBlock.h"

#include <Windows.h>
#include <crtdbg.h>
#include <dwmapi.h>
#include <objbase.h>

#pragma comment(lib, "dwmapi.lib")

#include <cstdio>
#include <cstdlib>
#include <filesystem>

DEFINE_LOG_CATEGORY(LogTestApp, Log);

// Writes the log to a file (self-test runs have no console).
class FFileOutputDevice final : public FOutputDevice
{
public:
	explicit FFileOutputDevice(const FString& Path) { File = _wfopen(Path.ToWide().c_str(), L"w"); }
	~FFileOutputDevice() override { if (File) std::fclose(File); }
	void Serialize(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message) override
	{
		if (File)
		{
			std::fprintf(File, "[%8.3f] %s: %s: %.*s\n", FPlatformTime::Seconds() - Start, Category.Name, ToString(Verbosity),
				static_cast<int>(Message.size()), Message.data());
			std::fflush(File);
		}
	}

private:
	FILE* File = nullptr;
	double Start = FPlatformTime::Seconds();
};

namespace
{
	// Splits a command line into arguments (quotes group spaces).
	TArray<FString> SplitCommandLine(const FString& CommandLine)
	{
		TArray<FString> Result;
		std::string Current;
		bool bQuoted = false;
		bool bAny = false;
		for (const char C : CommandLine.View())
		{
			if (C == '"')
			{
				bQuoted = !bQuoted;
				bAny = true;
			}
			else if (C == ' ' && !bQuoted)
			{
				if (bAny)
				{
					Result.push_back(FString(Current));
				}
				Current.clear();
				bAny = false;
			}
			else
			{
				Current.push_back(C);
				bAny = true;
			}
		}
		if (bAny)
		{
			Result.push_back(FString(Current));
		}
		return Result;
	}

	TOptional<FString> GetOption(const TArray<FString>& Args, std::string_view Name)
	{
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(Name))
			{
				return Arg.Mid(static_cast<int32>(Name.size()));
			}
		}
		return {};
	}

	bool HasFlag(const TArray<FString>& Args, std::string_view Flag)
	{
		for (const FString& Arg : Args)
		{
			if (Arg.View() == Flag)
			{
				return true;
			}
		}
		return false;
	}
}

// ---------------------------------------------------------------------------
// Script

FTestScript& FTestScript::Do(TFunction<void()> Action, int32 Frames)
{
	Steps.push_back(FStep{ PendingFrames + Frames, std::move(Action) });
	PendingFrames = 0;
	return *this;
}

FTestScript& FTestScript::Wait(int32 Frames)
{
	PendingFrames += Frames;
	return *this;
}

FTestScript& FTestScript::WaitSeconds(double Seconds)
{
	return Do([this, Seconds] { App->ScriptHoldUntil = App->GetTime() + Seconds; }, 0);
}

FTestScript& FTestScript::Capture(const FString& Name, int32 Frames)
{
	return Do([this, Name] { App->Capture(Name); }, Frames);
}

FTestScript& FTestScript::Check(const FString& What, TFunction<bool()> Condition, int32 Frames)
{
	return Do([this, What, Condition = std::move(Condition)] { App->Expect(Condition(), What); }, Frames);
}

// ---------------------------------------------------------------------------
// Setup and shutdown

FTestApp::FTestApp(FTestAppInfo InInfo)
	: Info(std::move(InInfo))
{
	SelfTest.App = this;
}

FTestApp::~FTestApp() = default;

bool FTestApp::Initialize(const FString& CommandLine)
{
	const TArray<FString> Args = SplitCommandLine(CommandLine);
	if (const TOptional<FString> Dir = GetOption(Args, "-selftest="))
	{
		SelfTestDir = *Dir;
		if (!SelfTestDir.EndsWith("/") && !SelfTestDir.EndsWith("\\"))
		{
			SelfTestDir += "/";
		}
		std::error_code Error;
		std::filesystem::create_directories(std::filesystem::path(SelfTestDir.ToWide()), Error);
	}
	Backend = ParseBackendFromCommandLine(CommandLine.View());
	if (!SelfTestDir.IsEmpty())
	{
#if defined(_DEBUG)
		// Failed checks go to the log (and the debugger), not to a dialog
		// that would stop the scripted run.
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif
		FileLog = MakeUnique<FFileOutputDevice>(SelfTestDir + FString::Format("{}_{}_log.txt", Info.Name, ToString(Backend)));
		FLog::AddDevice(FileLog.get());
	}

	// Settings, for apps that keep any. Scripted runs start from the defaults.
	if (Info.bKeepsSettings && SelfTestDir.IsEmpty())
	{
		if (const wchar_t* LocalAppData = _wgetenv(L"LOCALAPPDATA"))
		{
			const std::filesystem::path Dir = std::filesystem::path(LocalAppData) / L"Genesis" / L"TestApps" / Info.Name.ToWide();
			std::error_code Error;
			if (HasFlag(Args, "-resetlayout"))
			{
				std::filesystem::remove_all(Dir, Error);
			}
			std::filesystem::create_directories(Dir, Error);
			SettingsDir = FString::FromWide(Dir.wstring()) + "\\";
		}
	}
	if (!SettingsDir.IsEmpty())
	{
		auto Ini = MakeUnique<FIniConfigStore>(SettingsDir + "Settings.ini");
		Ini->Load();
		Config = std::move(Ini);
	}
	else
	{
		Config = MakeUnique<FMemoryConfigStore>();
	}
	FServices::SetConfig(Config.get());

	if (const TOptional<FString> Scale = GetOption(Args, "-appscale="))
	{
		AppScale = FMath::Clamp(static_cast<float>(std::atof(**Scale)), 0.5f, 4.0f);
	}

	Platform = CreatePlatformApplication().ToSharedPtr();
	FRHIDeviceDesc DeviceDesc;
	DeviceDesc.Backend = Backend;
	if (HasFlag(Args, "-debug")) DeviceDesc.bDebugLayer = true;
	if (HasFlag(Args, "-nodebug")) DeviceDesc.bDebugLayer = false;
	Device = FRHIDevice::Create(DeviceDesc);
	if (!Device)
	{
		const FString Message = FString::Format("Could not create a {} device. See the log for details.", ToString(Backend));
		GE_LOG(LogTestApp, Error, "{}", Message);
		if (SelfTestDir.IsEmpty())
		{
			MessageBoxW(nullptr, Message.ToWide().c_str(), Info.Name.ToWide().c_str(), MB_ICONERROR);
		}
		return false;
	}

	FontCache = std::make_shared<FSlateFontCache>();
	Fonts = FStarshipCoreFonts::Load(*FontCache);
	if (!Fonts.IsValid())
	{
		GE_LOG(LogTestApp, Error, "The fonts could not be loaded");
		return false;
	}
	const TSharedRef<FSlateStyleSet> Style = FStarshipCoreStyle::Create(Fonts);
	FSlateStyleSet::Register(Style);
	FAppStyle::SetAppStyleSet(Style);

	Renderer = MakeUnique<FSlateRHIRenderingPolicy>(*Device, *FontCache);
	if (!Renderer->Initialize())
	{
		GE_LOG(LogTestApp, Error, "UI renderer initialization failed");
		return false;
	}
	WindowRenderer = MakeUnique<FSlateRHIRenderer>(*Device, *Renderer);
	WindowRenderer->OnBeginFrame.AddLambda([this](FRHICommandList& CommandList) { OnBeginFrame(CommandList); });

	App = FSlateApplication::Create(ToSharedRef(Platform), ToSharedRef(FontCache)).ToSharedPtr();
	App->SetRenderer(WindowRenderer.get());
	App->SetApplicationScale(AppScale);
	App->RegisterInputPreProcessor(TSharedRef<IInputProcessor>(StaticCastSharedPtr<IInputProcessor>(shared_from_this())));
	App->OnPreRender.AddLambda([this](float)
	{
		const double Now = FPlatformTime::Seconds();
		++FramesSinceFps;
		if (Now - LastFpsTime >= 0.5)
		{
			FramesPerSecond = static_cast<float>(FramesSinceFps / (Now - LastFpsTime));
			FramesSinceFps = 0;
			LastFpsTime = Now;
		}
	});
	StartTime = LastFpsTime = FPlatformTime::Seconds();

	if (!StartServices())
	{
		return false;
	}
	MainWindow = SNew(SWindow)
		.Title(FString::Format("{} ({})", Info.Title, ToString(Backend)))
		.ClientSize(Info.ClientSize)
		.OnWindowClosed([this](const TSharedRef<SWindow>&) { App->RequestQuit(); });
	App->AddWindow(ToSharedRef(MainWindow));
	MainWindow->SetContent(MakeContent());
	OnStarted();
	GE_LOG(LogTestApp, Display, "{} on {}: {}", Info.Name, ToString(Backend), Device->GetAdapterName());
	return true;
}

int32 FTestApp::Run(const FString& CommandLine)
{
	if (!Initialize(CommandLine))
	{
		Shutdown();
		return 1;
	}
	if (!SelfTestDir.IsEmpty())
	{
		BuildSelfTest(SelfTest);
		SelfTest.Do([this] { App->RequestQuit(); }, 5);
		// Keep frames coming so the script advances while nothing changes.
		MainWindow->RegisterActiveTimer(0.0f, [](double, float) { return EActiveTimerReturn::Continue; });
		ScriptWait = 20 + (SelfTest.Steps.empty() ? 0 : SelfTest.Steps[0].Frames);
	}
	while (!App->IsQuitRequested() && MainWindow && !MainWindow->IsClosed())
	{
		App->Tick();
		if (!SelfTestDir.IsEmpty())
		{
			TickSelfTest();
		}
		else if (!App->IsActive())
		{
			Platform->WaitForMessages(0.25f);
		}
	}
	Shutdown();
	if (!SelfTestDir.IsEmpty())
	{
		return Failures > 0 ? 1 : 0;
	}
	return 0;
}

void FTestApp::Shutdown()
{
	if (!SelfTestDir.IsEmpty())
	{
		GE_LOG(LogTestApp, Display, "{}: {} checks, {} failed", Info.Name, Checks, Failures);
	}
	if (Device)
	{
		Device->WaitIdle();
	}
	if (App)
	{
		App->SetRenderer(nullptr);
		FSlateApplication::Shutdown();
	}
	App.reset();
	MainWindow.reset();
	OnShutdown();
	if (Device)
	{
		Device->WaitIdle();
	}
	WindowRenderer.reset();
	Renderer.reset();
	Device.reset();
	if (Config)
	{
		Config->Flush();
	}
	FServices::SetConfig(nullptr);
	if (FileLog)
	{
		FLog::RemoveDevice(FileLog.get());
		FileLog.reset();
	}
}

int32 RunTestApp(const TSharedRef<FTestApp>& App)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const int32 Result = App->Run(FString::FromWide(GetCommandLineW()));
	CoUninitialize();
	return Result;
}

// ---------------------------------------------------------------------------
// Scale, DPI, keys

double FTestApp::GetTime() const
{
	return FPlatformTime::Seconds() - StartTime;
}

void FTestApp::SetAppScale(float Scale)
{
	AppScale = Scale;
	App->SetApplicationScale(Scale);
	GE_LOG(LogTestApp, Display, "App scale {:.2f}", AppScale);
}

void FTestApp::SimulateDpiChange(float NewScale)
{
	// The new DPI plus a suggested window rectangle keeping the logical size.
	const HWND Hwnd = static_cast<HWND>(MainWindow->GetNativeWindow()->GetNativeHandle());
	RECT Rect;
	GetWindowRect(Hwnd, &Rect);
	const float Ratio = NewScale / MainWindow->GetNativeWindow()->GetDpiScale();
	RECT Suggested{ Rect.left, Rect.top, Rect.left + FMath::RoundToInt((Rect.right - Rect.left) * Ratio), Rect.top + FMath::RoundToInt((Rect.bottom - Rect.top) * Ratio) };
	const WORD Dpi = static_cast<WORD>(FMath::RoundToInt(NewScale * 96.0f));
	GE_LOG(LogTestApp, Display, "Simulating WM_DPICHANGED to {} dpi", Dpi);
	SendMessageW(Hwnd, WM_DPICHANGED, MAKEWPARAM(Dpi, Dpi), reinterpret_cast<LPARAM>(&Suggested));
}

float FTestApp::GetNativeDpiScale() const
{
	return static_cast<float>(GetDpiForWindow(static_cast<HWND>(MainWindow->GetNativeWindow()->GetNativeHandle()))) / 96.0f;
}

FString FTestApp::GetStatusLine() const
{
	const float Dpi = MainWindow ? MainWindow->GetNativeWindow()->GetDpiScale() : 1.0f;
	return FString::Format("{} · {} · DPI {:.0f}% · app scale {:.0f}% · {:.0f} fps", ToString(Backend), Device->GetAdapterName(), Dpi * 100.0f,
		AppScale * 100.0f, FramesPerSecond);
}

bool FTestApp::HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event)
{
	switch (Event.GetKey())
	{
	case EKey::F2:
	{
		const float Scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
		int32 Next = 0;
		for (int32 I = 0; I < 4; ++I)
		{
			if (FMath::IsNearlyEqual(AppScale, Scales[I], 0.01f))
			{
				Next = (I + 1) % 4;
			}
		}
		SetAppScale(Scales[Next]);
		return true;
	}
	case EKey::F3:
	{
		const float Current = MainWindow->GetNativeWindow()->GetDpiScale();
		SimulateDpiChange(Current >= 2.0f ? 1.0f : Current + 0.5f);
		return true;
	}
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Input

namespace
{
	TSharedRef<IPlatformWindow> NativeOf(const TSharedPtr<SWindow>& Main, const SWindow* Window)
	{
		return ToSharedRef(Window ? Window->GetNativeWindow() : Main->GetNativeWindow());
	}
}

void FTestApp::MoveMouse(FVector2 Position, const SWindow* Window)
{
	App->OnMouseMove(NativeOf(MainWindow, Window), Position);
}

void FTestApp::MouseDown(FVector2 Position, EMouseButton Button, const SWindow* Window)
{
	const TSharedRef<IPlatformWindow> Native = NativeOf(MainWindow, Window);
	App->OnMouseMove(Native, Position);
	App->OnMouseDown(Native, Button, Position);
}

void FTestApp::MouseUp(FVector2 Position, EMouseButton Button, const SWindow* Window)
{
	const TSharedRef<IPlatformWindow> Native = NativeOf(MainWindow, Window);
	App->OnMouseMove(Native, Position);
	App->OnMouseUp(Native, Button, Position);
}

void FTestApp::Click(FVector2 Position, EMouseButton Button, const SWindow* Window)
{
	const TSharedRef<IPlatformWindow> Native = NativeOf(MainWindow, Window);
	App->OnMouseMove(Native, Position);
	App->OnMouseDown(Native, Button, Position);
	App->OnMouseUp(Native, Button, Position);
}

void FTestApp::Drag(FVector2 From, FVector2 To, EMouseButton Button, const SWindow* Window)
{
	const TSharedRef<IPlatformWindow> Native = NativeOf(MainWindow, Window);
	App->OnMouseMove(Native, From);
	App->OnMouseDown(Native, Button, From);
	for (int32 Step = 1; Step <= 8; ++Step)
	{
		const float T = static_cast<float>(Step) / 8.0f;
		App->OnMouseMove(Native, From + (To - From) * T);
	}
	App->OnMouseUp(Native, Button, To);
}

void FTestApp::Wheel(FVector2 Position, float Delta, const SWindow* Window)
{
	const TSharedRef<IPlatformWindow> Native = NativeOf(MainWindow, Window);
	App->OnMouseMove(Native, Position);
	App->OnMouseWheel(Native, Delta, false, Position);
}

void FTestApp::PressKey(EKey Key, uint32 Character)
{
	App->OnKeyDown(Key, Character, false);
	App->OnKeyUp(Key, Character, false);
}

void FTestApp::SetModifierKeys(bool bShift, bool bControl, bool bAlt)
{
	BYTE State[256];
	GetKeyboardState(State);
	const auto Set = [&State](int Key, bool bDown) { State[Key] = bDown ? static_cast<BYTE>(0x80) : static_cast<BYTE>(0); };
	Set(VK_SHIFT, bShift);
	Set(VK_LSHIFT, bShift);
	Set(VK_CONTROL, bControl);
	Set(VK_LCONTROL, bControl);
	Set(VK_MENU, bAlt);
	Set(VK_LMENU, bAlt);
	SetKeyboardState(State);
}

void FTestApp::TypeText(std::string_view Text)
{
	for (const char C : Text)
	{
		App->OnKeyChar(static_cast<char32_t>(static_cast<unsigned char>(C)), false);
	}
}

FVector2 FTestApp::GetCenter(const SWidget& Widget)
{
	return Widget.GetCachedGeometry().GetLayoutBoundingRect().GetCenter();
}

bool FTestApp::PointAtText(std::string_view Text, bool bClick, const SWindow* Window)
{
	const auto Match = [Text](STextBlock& Block) { return Block.GetText().View() == Text; };
	STextBlock* Found = nullptr;
	const SWindow* FoundIn = nullptr;
	for (const TSharedRef<SWindow>& Candidate : TArray<TSharedRef<SWindow>>(App->GetWindows()))
	{
		if (Window && &Candidate.Get() != Window)
		{
			continue;
		}
		if ((Found = FindWidget<STextBlock>(Candidate.Get(), Match)) != nullptr)
		{
			FoundIn = &Candidate.Get();
			break;
		}
	}
	if (!Expect(Found != nullptr, FString::Format("\"{}\" is on screen", Text)))
	{
		return false;
	}
	if (bClick)
	{
		Click(GetCenter(*Found), EMouseButton::Left, FoundIn);
	}
	else
	{
		MoveMouse(GetCenter(*Found), FoundIn);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Self-test

FString FTestApp::GetWindowLabel(const SWindow& Window) const
{
	if (MainWindow && &Window == MainWindow.get())
	{
		return "main";
	}
	int32 Index = 0;
	for (const TSharedRef<SWindow>& Other : App->GetWindows())
	{
		if (&Other.Get() == MainWindow.get())
		{
			continue;
		}
		++Index;
		if (&Other.Get() == &Window)
		{
			break;
		}
	}
	return FString::Format("window{}", Index);
}

void FTestApp::Capture(const FString& Name)
{
	for (const TSharedRef<SWindow>& Window : TArray<TSharedRef<SWindow>>(App->GetWindows()))
	{
		if (Window->GetNativeWindow()->IsMinimized())
		{
			continue;
		}
		// Bring the window on top and let the compositor catch up with the
		// frames just presented (a fast backend can be a few ahead of it).
		const HWND Hwnd = static_cast<HWND>(Window->GetNativeWindow()->GetNativeHandle());
		SetWindowPos(Hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		for (int32 I = 0; I < 3; ++I)
		{
			Window->Invalidate(EInvalidateWidgetReason::Paint);
			App->Tick();
		}
		for (int32 I = 0; I < 3; ++I)
		{
			DwmFlush();
		}
		const FString Path = SelfTestDir + FString::Format("{}_{}_{}_{}.png", Info.Name, ToString(Backend), GetWindowLabel(*Window), Name);
		const bool bOk = CaptureWindowClientToPng(Hwnd, Path);
		SetWindowPos(Hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		Expect(bOk, FString::Format("screenshot {}", Path));
		GE_LOG(LogTestApp, Display, "Screenshot {} ({}x{} px, DPI scale {:.2f}, app scale {:.2f})", Path,
			Window->GetPixelSize().X, Window->GetPixelSize().Y, Window->GetNativeWindow()->GetDpiScale(), AppScale);
	}
}

void FTestApp::SetLastEvent(const FString& Event)
{
	LastEvent = Event;
	GE_LOG(LogTestApp, Log, "{}", Event);
}

bool FTestApp::Expect(bool bCondition, const FString& What)
{
	++Checks;
	if (bCondition)
	{
		GE_LOG(LogTestApp, Display, "Check passed: {}", What);
	}
	else
	{
		++Failures;
		GE_LOG(LogTestApp, Error, "Check FAILED: {}", What);
	}
	return bCondition;
}

void FTestApp::TickSelfTest()
{
	if (ScriptStep >= static_cast<int32>(SelfTest.Steps.size()))
	{
		return;
	}
	if (ScriptWait > 0)
	{
		--ScriptWait;
		return;
	}
	if (GetTime() < ScriptHoldUntil)
	{
		return;
	}
	// The action may add windows or quit; take the step first.
	const TFunction<void()> Action = SelfTest.Steps[ScriptStep].Action;
	++ScriptStep;
	if (ScriptStep < static_cast<int32>(SelfTest.Steps.size()))
	{
		ScriptWait = SelfTest.Steps[ScriptStep].Frames;
	}
	Action();
}
