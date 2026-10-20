// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The shared harness of the test applications (source/Programs/TestApps).
// Each test application shows one feature of Genesis UI or of the map
// editor in a window of its own; FTestApp does everything else: the RHI
// device, the UI renderer, fonts and style, the application, the main
// window, the command line and the scripted self-test.
//
// Command line (every test application):
//   -d3d12 | -vulkan       backend (default D3D12)
//   -appscale=1.25         application scale on top of the monitor DPI
//   -nodebug / -debug      D3D12 debug layer / Vulkan validation (default: on in Debug builds)
//   -resetlayout           forget saved settings (apps that keep any)
//   -selftest=<dir>        runs the app's script: PNG screenshots, a log and
//                          the result of every check go to <dir>; exits
//                          when done, with 1 if a check failed
//
// Keys: F2 cycles the app scale, F3 simulates a DPI change of the main
// window (WM_DPICHANGED).
//
// A test application derives from FTestApp, builds its window content in
// MakeContent, writes its script in BuildSelfTest, and ends its .cpp with
// GENESIS_TEST_APP(FMyTestApp).

#pragma once

#include "Application/UIApplication.h"
#include "CoreMinimal.h"
#include "RHI.h"
#include "Styling/GenesisStyle.h"
#include "UIRenderer.h"
#include "UIWindowRenderer.h"
#include "Widgets/SWindow.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTestApp, Log);

class FTestApp;
class FFileOutputDevice;
class IConfigStore;

// A self-test: steps run one after the other, each after waiting a number
// of frames from the step before it.
class FTestScript
{
public:
	// Runs Action after Frames frames.
	FTestScript& Do(TFunction<void()> Action, int32 Frames = 5);
	// Waits before the next step.
	FTestScript& Wait(int32 Frames);
	// Waits at least this long before the next step (for timers such as the
	// tooltip delay: frames can come much faster than the display's rate).
	FTestScript& WaitSeconds(double Seconds);
	// Screenshots every window after Frames frames (for animations,
	// tooltips and layout to settle).
	FTestScript& Capture(const FString& Name, int32 Frames = 15);
	// Fails the run when Condition is false.
	FTestScript& Check(const FString& What, TFunction<bool()> Condition, int32 Frames = 1);

private:
	friend class FTestApp;

	struct FStep
	{
		int32 Frames = 0;
		TFunction<void()> Action;
	};
	FTestApp* App = nullptr;
	TArray<FStep> Steps;
	int32 PendingFrames = 0;
};

struct FTestAppInfo
{
	FString Name;           // the program's name, e.g. "UIButtonsTest"; names settings and screenshots
	FString Title;          // the window title
	FVector2 ClientSize = FVector2(1100.0f, 720.0f);
	bool bKeepsSettings = false; // gets a settings folder in %LOCALAPPDATA%\Genesis\TestApps\<Name>
};

class FTestApp : public IInputProcessor, public TSharedFromThis<FTestApp>
{
public:
	explicit FTestApp(FTestAppInfo InInfo);
	~FTestApp() override;

	int32 Run(const FString& CommandLine);

	// IInputProcessor: F2 and F3.
	bool HandleKeyDownEvent(FUIApplication& App, const FKeyEvent& Event) override;

	// ----- For scripts and content -----
	FRHIDevice& GetDevice() const { return *Device; }
	FUIRenderer& GetRenderer() const { return *Renderer; }
	FUIWindowRenderer& GetWindowRenderer() const { return *WindowRenderer; }
	FFontCache& GetFontCache() const { return *FontCache; }
	const FGenesisFonts& GetFonts() const { return Fonts; }
	FUIApplication& GetApp() const { return *App; }
	IPlatformApplication& GetPlatform() const { return *Platform; }
	const TSharedPtr<SWindow>& GetMainWindow() const { return MainWindow; }
	ERHIBackend GetBackend() const { return Backend; }
	const FString& GetSettingsDir() const { return SettingsDir; } // empty: nothing is saved
	bool IsSelfTest() const { return !SelfTestDir.IsEmpty(); }
	const FString& GetSelfTestDir() const { return SelfTestDir; }
	double GetTime() const;

	float GetAppScale() const { return AppScale; }
	void SetAppScale(float Scale);
	// What Windows sends when the main window moves to a monitor with
	// another DPI.
	void SimulateDpiChange(float NewScale);
	// The DPI scale of the monitor the main window is on.
	float GetNativeDpiScale() const;
	float GetFramesPerSecond() const { return FramesPerSecond; }
	// "D3D12 · adapter · DPI 150% · app scale 100% · 60 fps".
	FString GetStatusLine() const;

	// Input, through the application's platform handler (real routing).
	// Positions are absolute: window pixels, as in a widget's geometry.
	void MoveMouse(FVector2 Position, const SWindow* Window = nullptr);
	void MouseDown(FVector2 Position, EMouseButton Button = EMouseButton::Left, const SWindow* Window = nullptr);
	void MouseUp(FVector2 Position, EMouseButton Button = EMouseButton::Left, const SWindow* Window = nullptr);
	void Click(FVector2 Position, EMouseButton Button = EMouseButton::Left, const SWindow* Window = nullptr);
	void Drag(FVector2 From, FVector2 To, EMouseButton Button = EMouseButton::Left, const SWindow* Window = nullptr);
	void Wheel(FVector2 Position, float Delta, const SWindow* Window = nullptr);
	void PressKey(EKey Key, uint32 Character = 0);
	// Holds modifier keys down as far as this thread's keyboard state goes,
	// which is where the platform reads them from (until set again).
	void SetModifierKeys(bool bShift, bool bControl, bool bAlt = false);
	void TypeText(std::string_view Text);
	static FVector2 GetCenter(const SWidget& Widget);
	// The first text block showing Text (in Window, or any window): points
	// at it and clicks it when asked. False (and a failed check) if none.
	bool PointAtText(std::string_view Text, bool bClick, const SWindow* Window = nullptr);

	// The first widget of type T under Root (depth first) that Match accepts.
	template <typename T>
	static T* FindWidget(const SWidget& Root, const TFunction<bool(T&)>& Match = nullptr)
	{
		if (T* Typed = dynamic_cast<T*>(const_cast<SWidget*>(&Root)); Typed && (!Match || Match(*Typed)))
		{
			return Typed;
		}
		for (const FArrangedWidget& Child : Root.GetArrangedChildren())
		{
			if (T* Found = FindWidget<T>(Child.Widget.Get(), Match))
			{
				return Found;
			}
		}
		return nullptr;
	}

	// What the user did last (shown in the status bar of TestFrame, and logged).
	void SetLastEvent(const FString& Event);
	const FString& GetLastEvent() const { return LastEvent; }

	// Screenshots every window: <dir>/<app>_<backend>_<window>_<name>.png.
	void Capture(const FString& Name);
	// Logs the check; a failure makes the self-test exit with 1.
	bool Expect(bool bCondition, const FString& What);

protected:
	// After the device, the UI and the main window's style exist, before the
	// content: start what the content needs (false stops the app).
	virtual bool StartServices() { return true; }
	// The main window's content.
	virtual TSharedRef<SWidget> MakeContent() = 0;
	// After the main window opens (other windows, initial state).
	virtual void OnStarted() {}
	// The self-test script (-selftest=<dir>).
	virtual void BuildSelfTest(FTestScript& Script) { (void)Script; }
	// At the start of each rendered frame (render targets).
	virtual void OnBeginFrame(FRHICommandList& CommandList) { (void)CommandList; }
	// When the app ends, after the UI is gone and before the renderer and
	// device are: release GPU objects and stop services here.
	virtual void OnShutdown() {}

	// The name of a window in screenshot file names.
	virtual FString GetWindowLabel(const SWindow& Window) const;

private:
	friend class FTestScript;

	bool Initialize(const FString& CommandLine);
	void Shutdown();
	void TickSelfTest();

	FTestAppInfo Info;
	TSharedPtr<IPlatformApplication> Platform;
	TUniquePtr<FRHIDevice> Device;
	TSharedPtr<FFontCache> FontCache;
	FGenesisFonts Fonts;
	TUniquePtr<FUIRenderer> Renderer;
	TUniquePtr<FUIWindowRenderer> WindowRenderer;
	TSharedPtr<FUIApplication> App;
	TSharedPtr<SWindow> MainWindow;
	TUniquePtr<FFileOutputDevice> FileLog;
	TUniquePtr<IConfigStore> Config;

	ERHIBackend Backend = ERHIBackend::D3D12;
	float AppScale = 1.0f;
	double StartTime = 0.0;
	double LastFpsTime = 0.0;
	int32 FramesSinceFps = 0;
	float FramesPerSecond = 0.0f;

	FString SelfTestDir;
	FString SettingsDir;
	FString LastEvent = "Ready";
	FTestScript SelfTest;
	int32 ScriptStep = 0;
	int32 ScriptWait = 0;
	double ScriptHoldUntil = 0.0;
	int32 Checks = 0;
	int32 Failures = 0;
};

// The program's entry point for a test application class.
int32 RunTestApp(const TSharedRef<FTestApp>& App);

#define GENESIS_TEST_APP(AppClass) \
	int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) \
	{ \
		return RunTestApp(TSharedRef<FTestApp>(std::make_shared<AppClass>())); \
	}

// Windows types for the macro, without <Windows.h> in every test app.
#ifndef WINAPI
struct HINSTANCE__;
using HINSTANCE = HINSTANCE__*;
using PWSTR = wchar_t*;
#define WINAPI __stdcall
#endif
