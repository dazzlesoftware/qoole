// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UISandbox: milestone 2 of docs/GenesisUI.md. Two DPI-aware windows (one
// OS-decorated, one borderless with a UI-drawn title bar) drawing boxes,
// 9-slices, rounded boxes, lines, clipped regions, shaped text and a
// render-target viewport through the UI renderer, on D3D12 or Vulkan.
//
// Command line:
//   -d3d12 | -vulkan       backend (default D3D12)
//   -appscale=1.25         application scale on top of the monitor DPI
//   -nodebug / -debug      D3D12 debug layer / Vulkan validation (default: on in Debug builds)
//   -selftest=<dir>        scripted run: DPI changes, minimize/restore, move
//                          between monitors, resize; PNG screenshots and a log
//                          go to <dir>; exits when done
//
// Keys: Esc quits, F2 cycles the app scale, F3 simulates a DPI change of the
// main window (WM_DPICHANGED), the mouse wheel scrolls the clipped list.

#include "CoreMinimal.h"
#include "GenericPlatform/PlatformApplication.h"
#include "RHI.h"
#include "SandboxContent.h"
#include "Screenshot.h"
#include "UIRenderer.h"

#include <Windows.h>
#include <objbase.h>

#include <cstdio>

DEFINE_LOG_CATEGORY(LogSandbox, Log);

namespace
{
	// Writes the log to a file (selftest runs have no console).
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

	TOptional<FString> GetOption(const FString& CommandLine, std::string_view Name)
	{
		const std::string_view Text = CommandLine.View();
		const SIZE_T Pos = Text.find(Name);
		if (Pos == std::string_view::npos)
		{
			return {};
		}
		SIZE_T Start = Pos + Name.size();
		SIZE_T End;
		if (Start < Text.size() && Text[Start] == '"')
		{
			++Start;
			End = Text.find('"', Start);
		}
		else
		{
			End = Text.find(' ', Start);
		}
		return FString(Text.substr(Start, End == std::string_view::npos ? std::string_view::npos : End - Start));
	}

	bool HasFlag(const FString& CommandLine, std::string_view Flag)
	{
		const std::string_view Text = CommandLine.View();
		for (SIZE_T Pos = Text.find(Flag); Pos != std::string_view::npos; Pos = Text.find(Flag, Pos + 1))
		{
			const SIZE_T End = Pos + Flag.size();
			if ((Pos == 0 || Text[Pos - 1] == ' ') && (End == Text.size() || Text[End] == ' '))
			{
				return true;
			}
		}
		return false;
	}

	struct FSandboxWindow
	{
		TSharedPtr<IPlatformWindow> Window;
		TUniquePtr<FRHISwapChain> SwapChain;
		FDrawElementList Elements;
		bool bInspector = false;
		FVector2 MousePixels{ -1.0f, -1.0f };
		FUIRenderStats Stats;
	};
}

class FSandboxApp final : public IPlatformMessageHandler, public TSharedFromThis<FSandboxApp>
{
public:
	int32 Run(const FString& CommandLine);

	// IPlatformMessageHandler
	bool OnKeyDown(EKey Key, uint32 CharacterCode, bool bIsRepeat) override;
	bool OnMouseDown(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position) override;
	bool OnMouseMove(const TSharedRef<IPlatformWindow>& Window, FVector2 Position) override;
	bool OnMouseLeave(const TSharedRef<IPlatformWindow>& Window) override;
	bool OnMouseWheel(const TSharedRef<IPlatformWindow>& Window, float Delta, bool bHorizontal, FVector2 Position) override;
	bool OnGamepadButtonDown(int32 UserIndex, EGamepadButton Button, bool bIsRepeat) override;
	void OnGamepadConnectionChanged(int32 UserIndex, bool bConnected) override;
	void OnSizeChanged(const TSharedRef<IPlatformWindow>& Window, int32 Width, int32 Height, bool bWasMinimized) override;
	void OnMovedWindow(const TSharedRef<IPlatformWindow>& Window, int32 X, int32 Y) override;
	void OnDpiScaleChanged(const TSharedRef<IPlatformWindow>& Window, float NewScale) override;
	void OnWindowClose(const TSharedRef<IPlatformWindow>& Window) override;
	void OnOSPaint(const TSharedRef<IPlatformWindow>& Window) override;
	EWindowZone GetWindowZoneForPoint(const TSharedRef<IPlatformWindow>& Window, FVector2 Position) override;

private:
	bool Initialize(const FString& CommandLine);
	void CreateWindows();
	void RenderFrame();
	void TickSelfTest();
	void Capture(const char* Name);
	void SimulateDpiChange(float NewScale);
	void CloseInspector();
	FSandboxWindow* Find(const IPlatformWindow& Window);
	FSandboxFrameInfo MakeFrameInfo(const FSandboxWindow& Window) const;
	TArray<FString> InspectorLines(const FSandboxWindow& Window) const;

	TSharedPtr<IPlatformApplication> Platform;
	TUniquePtr<FRHIDevice> Device;
	TUniquePtr<FFontCache> FontCache;
	TUniquePtr<FUIRenderer> Renderer;
	TUniquePtr<FSandboxContent> Content;
	TArray<TUniquePtr<FSandboxWindow>> Windows;
	TSharedPtr<FUIRenderTarget> ViewportTarget;
	FDrawElementList ViewportElements;
	TUniquePtr<FFileOutputDevice> FileLog;

	ERHIBackend Backend = ERHIBackend::D3D12;
	float AppScale = 1.0f;
	double StartTime = 0.0;
	double LastFpsTime = 0.0;
	int32 FramesSinceFps = 0;
	float FramesPerSecond = 0.0f;
	float ListScroll = 0.0f;
	bool bQuit = false;
	bool bInFrame = false;
	FString LastEvent = "none yet";

	FString SelfTestDir;
	int32 FrameNumber = 0;
};

bool FSandboxApp::Initialize(const FString& CommandLine)
{
	if (const TOptional<FString> Dir = GetOption(CommandLine, "-selftest="))
	{
		SelfTestDir = *Dir;
		if (!SelfTestDir.EndsWith("/") && !SelfTestDir.EndsWith("\\"))
		{
			SelfTestDir += "/";
		}
		CreateDirectoryW(SelfTestDir.ToWide().c_str(), nullptr);
	}
	Backend = ParseBackendFromCommandLine(CommandLine.View());
	if (!SelfTestDir.IsEmpty())
	{
		FileLog = MakeUnique<FFileOutputDevice>(SelfTestDir + FString::Format("log_{}.txt", ToString(Backend)));
		FLog::AddDevice(FileLog.get());
	}
	if (const TOptional<FString> Scale = GetOption(CommandLine, "-appscale="))
	{
		AppScale = FMath::Clamp(static_cast<float>(std::atof(**Scale)), 0.5f, 4.0f);
	}

	Platform = CreatePlatformApplication().ToSharedPtr();
	Platform->SetMessageHandler(TSharedRef<IPlatformMessageHandler>(StaticCastSharedPtr<IPlatformMessageHandler>(shared_from_this())));

	FRHIDeviceDesc DeviceDesc;
	DeviceDesc.Backend = Backend;
	if (HasFlag(CommandLine, "-debug")) DeviceDesc.bDebugLayer = true;
	if (HasFlag(CommandLine, "-nodebug")) DeviceDesc.bDebugLayer = false;
	Device = FRHIDevice::Create(DeviceDesc);
	if (!Device)
	{
		const FString Message = FString::Format("Could not create a {} device. See the log for details.", ToString(Backend));
		GE_LOG(LogSandbox, Error, "{}", Message);
		if (SelfTestDir.IsEmpty())
		{
			MessageBoxW(nullptr, Message.ToWide().c_str(), L"UISandbox", MB_ICONERROR);
		}
		return false;
	}

	FontCache = MakeUnique<FFontCache>();
	const FString Fonts = FPaths::FindContentDir() + "Fonts/";
	const TSharedPtr<FFontFace> Regular = FontCache->LoadFace(Fonts + "NotoSans-Regular.ttf");
	const TSharedPtr<FFontFace> Bold = FontCache->LoadFace(Fonts + "NotoSans-Bold.ttf");
	const TSharedPtr<FFontFace> Devanagari = FontCache->LoadFace(Fonts + "NotoSansDevanagari-Regular.ttf");
	if (!Regular || !Bold || !Devanagari)
	{
		GE_LOG(LogSandbox, Error, "Fonts not found in \"{}\"", Fonts);
		return false;
	}
	FSandboxFonts SandboxFonts;
	SandboxFonts.Regular = std::make_shared<FCompositeFont>(FName("Regular"), TArray<TSharedPtr<FFontFace>>{ Regular, Devanagari });
	SandboxFonts.Bold = std::make_shared<FCompositeFont>(FName("Bold"), TArray<TSharedPtr<FFontFace>>{ Bold, Devanagari });

	Renderer = MakeUnique<FUIRenderer>(*Device, *FontCache);
	if (!Renderer->Initialize())
	{
		GE_LOG(LogSandbox, Error, "UI renderer initialization failed");
		return false;
	}
	Content = MakeUnique<FSandboxContent>(*FontCache, SandboxFonts);
	CreateWindows();
	return true;
}

void FSandboxApp::CreateWindows()
{
	auto Main = MakeUnique<FSandboxWindow>();
	FWindowDesc MainDesc;
	MainDesc.Title = FString::Format("Genesis UI Sandbox ({})", ToString(Backend));
	MainDesc.Size = { 1180.0f, 740.0f };
	MainDesc.bShowImmediately = false;
	Main->Window = Platform->MakeWindow(MainDesc).ToSharedPtr();

	// Place the inspector to the right of the main window when there is room.
	const FIntPoint MainPos = Main->Window->GetClientPosition();
	const FIntPoint MainSize = Main->Window->GetClientSize();
	auto Inspector = MakeUnique<FSandboxWindow>();
	Inspector->bInspector = true;
	FWindowDesc InspectorDesc;
	InspectorDesc.Title = "Genesis UI Sandbox - Inspector";
	InspectorDesc.Size = { 360.0f, 460.0f };
	InspectorDesc.bUseOSBorder = false;
	InspectorDesc.bHasMaximizeButton = false;
	InspectorDesc.bShowImmediately = false;
	InspectorDesc.bAppearsInTaskbar = false;
	InspectorDesc.Position = FIntPoint(MainPos.X + MainSize.X + FMath::RoundToInt(16 * Main->Window->GetDpiScale()), MainPos.Y);
	FDisplayMetrics Metrics;
	Platform->GetDisplayMetrics(Metrics);
	if (const FMonitorInfo* Primary = Metrics.GetPrimary())
	{
		const int32 InspectorWidth = FMath::RoundToInt(InspectorDesc.Size.X * Primary->DpiScale);
		if (InspectorDesc.Position->X + InspectorWidth > Primary->WorkArea.Right)
		{
			InspectorDesc.Position = FIntPoint(Primary->WorkArea.Right - InspectorWidth - 16, MainPos.Y + 40);
		}
	}
	InspectorDesc.Parent = Main->Window;
	Inspector->Window = Platform->MakeWindow(InspectorDesc).ToSharedPtr();

	for (TUniquePtr<FSandboxWindow>* W : { &Main, &Inspector })
	{
		(*W)->SwapChain = Device->CreateSwapChain(*(*W)->Window);
		Windows.push_back(std::move(*W));
	}
	// Show after the swapchains exist so the first paint has somewhere to go.
	Windows[0]->Window->Show();
	Windows[1]->Window->Show();
}

FSandboxWindow* FSandboxApp::Find(const IPlatformWindow& Window)
{
	for (TUniquePtr<FSandboxWindow>& W : Windows)
	{
		if (W->Window.get() == &Window)
		{
			return W.get();
		}
	}
	return nullptr;
}

FSandboxFrameInfo FSandboxApp::MakeFrameInfo(const FSandboxWindow& Window) const
{
	FSandboxFrameInfo Frame;
	Frame.Time = FPlatformTime::Seconds() - StartTime;
	Frame.DpiScale = Window.Window->GetDpiScale();
	Frame.AppScale = AppScale;
	Frame.Scale = Frame.DpiScale * AppScale;
	Frame.PixelSize = Window.SwapChain ? Window.SwapChain->GetSize() : Window.Window->GetClientSize();
	Frame.LogicalSize = Frame.PixelSize.ToVector2() / Frame.Scale;
	Frame.MouseLogical = Window.MousePixels.X < 0.0f ? FVector2(-1.0f, -1.0f) : Window.MousePixels / Frame.Scale;
	Frame.BackendName = ToString(Backend);
	Frame.AdapterName = Device->GetAdapterName();
	Frame.FramesPerSecond = FramesPerSecond;
	Frame.Stats = Window.Stats;
	Frame.ListScroll = ListScroll;
	return Frame;
}

TArray<FString> FSandboxApp::InspectorLines(const FSandboxWindow& Window) const
{
	TArray<FString> Lines;
	Lines.push_back("#This window");
	const FIntPoint Pos = Window.Window->GetClientPosition();
	const FIntPoint Size = Window.Window->GetClientSize();
	Lines.push_back(FString::Format("DPI scale {:.2f} (app scale {:.2f})", Window.Window->GetDpiScale(), AppScale));
	Lines.push_back(FString::Format("Client {}x{} px at {}, {}", Size.X, Size.Y, Pos.X, Pos.Y));
	Lines.push_back("Drag the title bar to another monitor;");
	Lines.push_back("resize from any edge.");
	Lines.push_back("#Monitors");
	FDisplayMetrics Metrics;
	Platform->GetDisplayMetrics(Metrics);
	for (const FMonitorInfo& Monitor : Metrics.Monitors)
	{
		Lines.push_back(FString::Format("{}{}: {}x{} at {}, {}, {:.0f}%", Monitor.Name.Mid(4), Monitor.bIsPrimary ? " (primary)" : "",
			Monitor.DisplayRect.Width(), Monitor.DisplayRect.Height(), Monitor.DisplayRect.Left, Monitor.DisplayRect.Top, Monitor.DpiScale * 100.0f));
	}
	Lines.push_back("#Renderer");
	Lines.push_back(FString::Format("{}: {}", ToString(Backend), Device->GetAdapterName()));
	Lines.push_back(FString::Format("Glyph atlas pages: {}", FontCache->GetPageCount()));
	int32 Pads = 0;
	for (int32 User = 0; User < 4; ++User)
	{
		Pads += Platform->IsGamepadAttached(User) ? 1 : 0;
	}
	Lines.push_back("#Input");
	Lines.push_back(FString::Format("Gamepads: {}", Pads));
	Lines.push_back(FString::Format("Last event: {}", LastEvent));
	return Lines;
}

void FSandboxApp::RenderFrame()
{
	if (bInFrame || !Device)
	{
		return;
	}
	bInFrame = true;

	const double Now = FPlatformTime::Seconds();
	++FramesSinceFps;
	if (Now - LastFpsTime >= 0.5)
	{
		FramesPerSecond = static_cast<float>(FramesSinceFps / (Now - LastFpsTime));
		FramesSinceFps = 0;
		LastFpsTime = Now;
	}

	FRHICommandList& CommandList = Device->BeginFrame();
	Renderer->BeginFrame();

	// The render-target viewport, sized for the main window's scale.
	FSandboxWindow& Main = *Windows[0];
	const FSandboxFrameInfo MainFrame = MakeFrameInfo(Main);
	const FIntPoint TargetSize(FMath::RoundToInt(240.0f * MainFrame.Scale), FMath::RoundToInt(150.0f * MainFrame.Scale));
	if (!ViewportTarget || ViewportTarget->GetSize() != TargetSize)
	{
		if (ViewportTarget)
		{
			Device->DeferRelease(ViewportTarget);
		}
		ViewportTarget = Renderer->CreateRenderTarget(TargetSize.X, TargetSize.Y, "Sandbox viewport").ToSharedPtr();
	}
	ViewportElements.Reset();
	Content->PaintViewportScene(ViewportElements, MainFrame, TargetSize);
	Renderer->Render(CommandList, ViewportTarget->GetTexture(), ViewportElements, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));

	for (TUniquePtr<FSandboxWindow>& W : Windows)
	{
		if (!W->SwapChain || !W->SwapChain->IsDrawable() || W->Window->IsMinimized())
		{
			continue;
		}
		const FSandboxFrameInfo Frame = MakeFrameInfo(*W);
		W->Elements.Reset();
		W->Elements.SetViewportSize(Frame.PixelSize);
		if (W->bInspector)
		{
			Content->PaintInspector(W->Elements, Frame, InspectorLines(*W));
		}
		else
		{
			Content->PaintMain(W->Elements, Frame, ViewportTarget);
		}
		Renderer->Render(CommandList, *W->SwapChain, W->Elements, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		W->Stats = Renderer->GetLastStats(); // shown next frame
	}
	Device->EndFrame();
	++FrameNumber;
	bInFrame = false;
}

int32 FSandboxApp::Run(const FString& CommandLine)
{
	if (!Initialize(CommandLine))
	{
		return 1;
	}
	StartTime = LastFpsTime = FPlatformTime::Seconds();
	double LastTime = StartTime;
	while (!bQuit)
	{
		const double Now = FPlatformTime::Seconds();
		const float DeltaTime = static_cast<float>(Now - LastTime);
		LastTime = Now;

		Platform->PumpMessages(DeltaTime);
		Platform->ProcessDeferredEvents(DeltaTime);
		Platform->PollGameDeviceState(DeltaTime);
		if (!SelfTestDir.IsEmpty())
		{
			TickSelfTest();
		}

		bool bAnyVisible = false;
		for (const TUniquePtr<FSandboxWindow>& W : Windows)
		{
			bAnyVisible |= W->SwapChain && W->SwapChain->IsDrawable() && !W->Window->IsMinimized();
		}
		if (bAnyVisible)
		{
			RenderFrame(); // vsync paces the loop
		}
		else
		{
			// Minimized: nothing to animate, sleep until the OS has news.
			Platform->WaitForMessages(0.25f);
			++FrameNumber; // keeps the self-test script moving while minimized
		}
	}

	Device->WaitIdle();
	Windows.clear();
	ViewportTarget.reset();
	Renderer.reset();
	if (FileLog)
	{
		FLog::RemoveDevice(FileLog.get());
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Self-test: a scripted run that exercises DPI changes, minimize/restore,
// moving between monitors and resizing, and saves screenshots.

void FSandboxApp::Capture(const char* Name)
{
	for (const TUniquePtr<FSandboxWindow>& W : Windows)
	{
		// Bring the window on top and let DWM compose a couple of frames.
		const HWND Hwnd = static_cast<HWND>(W->Window->GetNativeHandle());
		SetWindowPos(Hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		RenderFrame();
		RenderFrame();
		const FString Path = SelfTestDir + FString::Format("{}_{}_{}.png", ToString(Backend), W->bInspector ? "inspector" : "main", Name);
		const bool bOk = CaptureWindowClientToPng(Hwnd, Path);
		SetWindowPos(Hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		GE_LOG(LogSandbox, Display, "Screenshot {} -> {} ({}x{} px, DPI scale {:.2f}, app scale {:.2f})", bOk ? "saved" : "FAILED", Path,
			W->Window->GetClientSize().X, W->Window->GetClientSize().Y, W->Window->GetDpiScale(), AppScale);
	}
}

void FSandboxApp::SimulateDpiChange(float NewScale)
{
	// What Windows sends when a window moves to a monitor with another DPI:
	// the new DPI plus a suggested window rectangle keeping the logical size.
	FSandboxWindow& Main = *Windows[0];
	const HWND Hwnd = static_cast<HWND>(Main.Window->GetNativeHandle());
	RECT Rect;
	GetWindowRect(Hwnd, &Rect);
	const float Ratio = NewScale / Main.Window->GetDpiScale();
	RECT Suggested{ Rect.left, Rect.top, Rect.left + FMath::RoundToInt((Rect.right - Rect.left) * Ratio), Rect.top + FMath::RoundToInt((Rect.bottom - Rect.top) * Ratio) };
	const WORD Dpi = static_cast<WORD>(FMath::RoundToInt(NewScale * 96.0f));
	GE_LOG(LogSandbox, Display, "Simulating WM_DPICHANGED to {} dpi", Dpi);
	SendMessageW(Hwnd, WM_DPICHANGED, MAKEWPARAM(Dpi, Dpi), reinterpret_cast<LPARAM>(&Suggested));
}

void FSandboxApp::TickSelfTest()
{
	FSandboxWindow& Main = *Windows[0];
	const float NativeScale = static_cast<float>(GetDpiForWindow(static_cast<HWND>(Main.Window->GetNativeHandle()))) / 96.0f;
	switch (FrameNumber)
	{
	case 60: Capture("native"); break;
	case 70: SimulateDpiChange(1.0f); break;
	case 100: Capture("dpi100"); break;
	case 110: SimulateDpiChange(2.0f); break;
	case 140: Capture("dpi200"); break;
	case 150: SimulateDpiChange(NativeScale); break;
	case 170: Main.Window->Minimize(); break;
	case 200: Main.Window->Restore(); break;
	case 230: Capture("restored"); break;
	case 240:
	{
		// Move to another monitor when there is one.
		FDisplayMetrics Metrics;
		Platform->GetDisplayMetrics(Metrics);
		const FIntPoint Pos = Main.Window->GetClientPosition();
		for (const FMonitorInfo& Monitor : Metrics.Monitors)
		{
			const bool bContains = Pos.X >= Monitor.DisplayRect.Left && Pos.X < Monitor.DisplayRect.Right && Pos.Y >= Monitor.DisplayRect.Top && Pos.Y < Monitor.DisplayRect.Bottom;
			if (!bContains)
			{
				GE_LOG(LogSandbox, Display, "Moving the main window to monitor {} ({:.0f}%)", Monitor.Name, Monitor.DpiScale * 100.0f);
				Main.Window->MoveWindowTo(Monitor.WorkArea.Left + 80, Monitor.WorkArea.Top + 80);
				Windows[1]->Window->MoveWindowTo(Monitor.WorkArea.Left + 80 + Main.Window->GetClientSize().X + 24, Monitor.WorkArea.Top + 80);
				break;
			}
		}
		break;
	}
	case 280: Capture("monitor2"); break;
	case 290:
	{
		const FIntPoint Pos = Main.Window->GetClientPosition();
		Main.Window->ReshapeWindow(Pos.X, Pos.Y, 1500, 1000);
		break;
	}
	case 320: Capture("resized"); break;
	case 330: AppScale = 1.25f; break;
	case 360: Capture("appscale125"); break;
	case 370: bQuit = true; break;
	default: break;
	}
}

// ---------------------------------------------------------------------------
// Input and window events

bool FSandboxApp::OnKeyDown(EKey Key, uint32, bool bIsRepeat)
{
	LastEvent = FString::Format("key {}{}", ToString(Key), bIsRepeat ? " (repeat)" : "");
	switch (Key)
	{
	case EKey::Escape:
		bQuit = true;
		return true;
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
		AppScale = Scales[Next];
		GE_LOG(LogSandbox, Display, "App scale {:.2f}", AppScale);
		return true;
	}
	case EKey::F3:
	{
		const float Current = Windows[0]->Window->GetDpiScale();
		SimulateDpiChange(Current >= 2.0f ? 1.0f : Current + 0.5f);
		return true;
	}
	default:
		return false;
	}
}

bool FSandboxApp::OnMouseDown(const TSharedRef<IPlatformWindow>& Window, EMouseButton Button, FVector2 Position)
{
	LastEvent = FString::Format("mouse down {} at {:.0f}, {:.0f}", static_cast<int32>(Button), Position.X, Position.Y);
	FSandboxWindow* W = Find(Window.Get());
	if (W && W->bInspector && Button == EMouseButton::Left)
	{
		const float Scale = Window->GetDpiScale() * AppScale;
		const FVector2 Logical = Window->GetClientSize().ToVector2() / Scale;
		if (FSandboxContent::GetInspectorCloseButton(Logical).Contains(Position / Scale))
		{
			CloseInspector();
		}
	}
	return true;
}

bool FSandboxApp::OnMouseMove(const TSharedRef<IPlatformWindow>& Window, FVector2 Position)
{
	if (FSandboxWindow* W = Find(Window.Get()))
	{
		W->MousePixels = Position;
	}
	return true;
}

bool FSandboxApp::OnMouseLeave(const TSharedRef<IPlatformWindow>& Window)
{
	if (FSandboxWindow* W = Find(Window.Get()))
	{
		W->MousePixels = FVector2(-1.0f, -1.0f);
	}
	return true;
}

bool FSandboxApp::OnMouseWheel(const TSharedRef<IPlatformWindow>&, float Delta, bool bHorizontal, FVector2)
{
	if (!bHorizontal)
	{
		ListScroll -= Delta * 26.0f * 3.0f;
		LastEvent = FString::Format("wheel {:+.1f}", Delta);
	}
	return true;
}

bool FSandboxApp::OnGamepadButtonDown(int32 UserIndex, EGamepadButton Button, bool bIsRepeat)
{
	LastEvent = FString::Format("pad {} {}{}", UserIndex, ToString(Button), bIsRepeat ? " (repeat)" : "");
	return true;
}

void FSandboxApp::OnGamepadConnectionChanged(int32 UserIndex, bool bConnected)
{
	GE_LOG(LogSandbox, Display, "Gamepad {} {}", UserIndex, bConnected ? "connected" : "disconnected");
}

void FSandboxApp::OnSizeChanged(const TSharedRef<IPlatformWindow>& Window, int32 Width, int32 Height, bool bWasMinimized)
{
	FSandboxWindow* W = Find(Window.Get());
	if (!W || !W->SwapChain)
	{
		return;
	}
	GE_LOG(LogSandbox, Log, "{} resized to {}x{}{}", W->bInspector ? "Inspector" : "Main window", Width, Height, bWasMinimized ? " (minimized)" : "");
	W->SwapChain->Resize(static_cast<uint32>(Width), static_cast<uint32>(Height));
	// Redraw right away so the content follows interactive resizing.
	if (!bWasMinimized)
	{
		RenderFrame();
	}
}

void FSandboxApp::OnMovedWindow(const TSharedRef<IPlatformWindow>& Window, int32 X, int32 Y)
{
	(void)Window;
	LastEvent = FString::Format("window moved to {}, {}", X, Y);
}

void FSandboxApp::OnDpiScaleChanged(const TSharedRef<IPlatformWindow>& Window, float NewScale)
{
	FSandboxWindow* W = Find(Window.Get());
	GE_LOG(LogSandbox, Display, "{} DPI scale changed to {:.2f}", W && W->bInspector ? "Inspector" : "Main window", NewScale);
	LastEvent = FString::Format("DPI scale {:.2f}", NewScale);
}

void FSandboxApp::CloseInspector()
{
	for (SIZE_T I = 0; I < Windows.size(); ++I)
	{
		if (Windows[I]->bInspector)
		{
			Device->WaitIdle();
			Windows[I]->SwapChain.reset();
			Windows[I]->Window->Destroy();
			Windows.erase(Windows.begin() + static_cast<std::ptrdiff_t>(I));
			return;
		}
	}
}

void FSandboxApp::OnWindowClose(const TSharedRef<IPlatformWindow>& Window)
{
	FSandboxWindow* W = Find(Window.Get());
	if (W && W->bInspector)
	{
		CloseInspector();
	}
	else
	{
		bQuit = true;
	}
}

void FSandboxApp::OnOSPaint(const TSharedRef<IPlatformWindow>&)
{
	RenderFrame();
}

EWindowZone FSandboxApp::GetWindowZoneForPoint(const TSharedRef<IPlatformWindow>& Window, FVector2 Position)
{
	const float Scale = Window->GetDpiScale() * AppScale;
	const FVector2 Logical = Position / Scale;
	const FVector2 Size = Window->GetClientSize().ToVector2() / Scale;
	if (FSandboxContent::GetInspectorCloseButton(Size).Contains(Logical))
	{
		return EWindowZone::ClientArea; // handled by OnMouseDown so it can show hover
	}
	return Logical.Y < FSandboxContent::TitleBarHeight ? EWindowZone::TitleBar : EWindowZone::ClientArea;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const FString CommandLine = FString::FromWide(GetCommandLineW());
	auto App = std::make_shared<FSandboxApp>();
	const int32 Result = App->Run(CommandLine);
	App.reset();
	CoUninitialize();
	return Result;
}
