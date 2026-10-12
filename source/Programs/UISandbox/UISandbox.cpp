// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UISandbox: Genesis UI on FUIApplication, drawn through the UI renderer on
// D3D12 or Vulkan. The main window is the editor framework demo (milestone 4
// of docs/GenesisUI.md: menus, toolbar and shortcuts from commands, dockable
// tabs, a property grid with undo, an RHI viewport) with the widget gallery
// of milestone 3 as one of its tabs. The borderless inspector window has a
// widget-built title bar and live information.
//
// Command line:
//   -d3d12 | -vulkan       backend (default D3D12)
//   -appscale=1.25         application scale on top of the monitor DPI
//   -nodebug / -debug      D3D12 debug layer / Vulkan validation (default: on in Debug builds)
//   -resetlayout           start from the default dock layout and key bindings
//   -selftest=<dir>        scripted run: every gallery page, menus, a tooltip,
//                          the property grid's color picker, a tab torn off
//                          into a window and docked back, DPI changes,
//                          minimize/restore, a move to another monitor,
//                          resize, app scale; PNG screenshots and a log go to
//                          <dir>; exits when done
//
// The layout and key bindings are kept in %LOCALAPPDATA%\Genesis\UISandbox.
// Keys: F2 cycles the app scale, F3 simulates a DPI change of the main
// window (WM_DPICHANGED); the editor's own shortcuts are listed in its Key
// bindings tab.

#include "Application/UIApplication.h"
#include "CoreMinimal.h"
#include "EditorDemo.h"
#include "Gallery.h"
#include "RHI.h"
#include "SandboxContent.h"
#include "Screenshot.h"
#include "Styling/GenesisStyle.h"
#include "UIRenderer.h"
#include "UIWindowRenderer.h"
#include "Widgets/Input/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include <cstdlib>

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
}

class FSandboxApp final : public IInputProcessor, public TSharedFromThis<FSandboxApp>
{
public:
	int32 Run(const FString& CommandLine);

	// IInputProcessor: global keys.
	bool HandleKeyDownEvent(FUIApplication& App, const FKeyEvent& Event) override;

private:
	bool Initialize(const FString& CommandLine);
	void CreateWindows();
	void CreateInspector();
	void ToggleInspector();
	TSharedRef<SWidget> MakeInspectorContent();
	void SetAppScale(float Scale);
	FSandboxFrameInfo MakeFrameInfo() const;
	void RenderViewportTarget(FRHICommandList& CommandList);
	void TickSelfTest();
	void Capture(const char* Name);
	// Moves the pointer to a text and optionally clicks it, through the
	// application's platform handler (real routing).
	bool PointAtText(const SWindow& Window, std::string_view Text, bool bClick);
	SWidgetGallery* GetGallery();
	void SimulateDpiChange(float NewScale);

	TSharedPtr<IPlatformApplication> Platform;
	TUniquePtr<FRHIDevice> Device;
	TSharedPtr<FFontCache> FontCache;
	TUniquePtr<FUIRenderer> Renderer;
	TUniquePtr<FUIWindowRenderer> WindowRenderer;
	TSharedPtr<FUIApplication> App;
	TUniquePtr<FSandboxContent> Content;
	TSharedPtr<SWindow> MainWindow;
	TSharedPtr<SWindow> Inspector;
	TSharedPtr<FEditorDemo> Editor;
	TSharedPtr<FUIRenderTarget> ViewportTarget;
	FDrawElementList ViewportElements;
	TUniquePtr<FFileOutputDevice> FileLog;

	ERHIBackend Backend = ERHIBackend::D3D12;
	float AppScale = 1.0f;
	double StartTime = 0.0;
	double LastFpsTime = 0.0;
	int32 FramesSinceFps = 0;
	float FramesPerSecond = 0.0f;

	FString SelfTestDir;
	FString SettingsDir;
	int32 SelfTestFrame = 0;
};

// ---------------------------------------------------------------------------
// Setup

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
	// Saved layout and key bindings (not for scripted runs, which need the defaults).
	if (SelfTestDir.IsEmpty())
	{
		if (const wchar_t* LocalAppData = _wgetenv(L"LOCALAPPDATA"))
		{
			const std::wstring Base = std::wstring(LocalAppData) + L"\\Genesis";
			CreateDirectoryW(Base.c_str(), nullptr);
			CreateDirectoryW((Base + L"\\UISandbox").c_str(), nullptr);
			SettingsDir = FString::FromWide(Base + L"\\UISandbox\\");
			if (HasFlag(CommandLine, "-resetlayout"))
			{
				DeleteFileW((Base + L"\\UISandbox\\Layout.json").c_str());
				DeleteFileW((Base + L"\\UISandbox\\Bindings.json").c_str());
			}
		}
	}
	if (const TOptional<FString> Scale = GetOption(CommandLine, "-appscale="))
	{
		AppScale = FMath::Clamp(static_cast<float>(std::atof(**Scale)), 0.5f, 4.0f);
	}

	Platform = CreatePlatformApplication().ToSharedPtr();

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

	FontCache = std::make_shared<FFontCache>();
	const FGenesisFonts Fonts = FGenesisFonts::Load(*FontCache);
	if (!Fonts.IsValid())
	{
		return false;
	}
	const TSharedRef<FStyleSet> Style = FGenesisStyle::Create(Fonts);
	FStyleSet::Register(Style);
	FAppStyle::SetAppStyleSet(Style);

	Renderer = MakeUnique<FUIRenderer>(*Device, *FontCache);
	if (!Renderer->Initialize())
	{
		GE_LOG(LogSandbox, Error, "UI renderer initialization failed");
		return false;
	}
	WindowRenderer = MakeUnique<FUIWindowRenderer>(*Device, *Renderer);
	WindowRenderer->OnBeginFrame.AddLambda([this](FRHICommandList& CommandList) { RenderViewportTarget(CommandList); });

	App = FUIApplication::Create(ToSharedRef(Platform), ToSharedRef(FontCache)).ToSharedPtr();
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

	FSandboxFonts SandboxFonts;
	SandboxFonts.Regular = Fonts.Regular;
	SandboxFonts.Bold = Fonts.Bold;
	Content = MakeUnique<FSandboxContent>(*FontCache, SandboxFonts);
	CreateWindows();
	return true;
}

void FSandboxApp::CreateWindows()
{
	FEditorDemo::FHost Host;
	Host.Device = Device.get();
	Host.Renderer = Renderer.get();
	Host.WindowRenderer = WindowRenderer.get();
	Host.Gallery.Content = Content.get();
	Host.Gallery.ViewportTarget = &ViewportTarget;
	Host.Gallery.GetFrameInfo = [this] { return MakeFrameInfo(); };
	Host.Gallery.SetAppScale = [this](float Scale) { SetAppScale(Scale); };
	Host.Gallery.ToggleInspector = [this] { ToggleInspector(); };
	Host.Gallery.Quit = [this] { App->RequestQuit(); };
	Host.ToggleInspector = [this] { ToggleInspector(); };
	Host.SetAppScale = [this](float Scale) { SetAppScale(Scale); };
	Host.Quit = [this] { App->RequestQuit(); };
	Host.SettingsDir = SettingsDir;
	Editor = std::make_shared<FEditorDemo>(std::move(Host));

	MainWindow = SNew(SWindow)
		.Title(FString::Format("Genesis UI Sandbox ({})", ToString(Backend)))
		.ClientSize(FVector2(1280.0f, 800.0f))
		.OnWindowClosed([this](const TSharedRef<SWindow>&) { App->RequestQuit(); });
	App->AddWindow(ToSharedRef(MainWindow));
	MainWindow->SetContent(Editor->MakeContent(ToSharedRef(MainWindow)));
	CreateInspector();
}

SWidgetGallery* FSandboxApp::GetGallery()
{
	return Editor ? Editor->Gallery.get() : nullptr;
}

TSharedRef<SWidget> FSandboxApp::MakeInspectorContent()
{
	const auto Line = [](TAttribute<FString> Text, bool bHeading = false) -> TSharedRef<SWidget>
	{
		return SNew(STextBlock).Text(Text).AutoWrapText(true).TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(bHeading ? "BoldText" : "NormalText"));
	};
	const TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
	const auto Add = [&Lines](const TSharedRef<SWidget>& Widget, float Top = 2.0f) { Lines->AddSlot().AutoHeight().Padding(FMargin(0.0f, Top, 0.0f, 0.0f))[Widget]; };
	Add(Line("This window", true), 0.0f);
	Add(Line([this]
	{
		const FIntPoint Size = Inspector ? Inspector->GetNativeWindow()->GetClientSize() : FIntPoint();
		const FIntPoint Pos = Inspector ? Inspector->GetNativeWindow()->GetClientPosition() : FIntPoint();
		return FString::Format("DPI scale {:.2f} (app scale {:.2f}), {}x{} px at {}, {}",
			Inspector ? Inspector->GetNativeWindow()->GetDpiScale() : 1.0f, AppScale, Size.X, Size.Y, Pos.X, Pos.Y);
	}));
	Add(Line("Drag the title bar to move it; resize from any edge."));
	Add(Line("Monitors", true), 10.0f);
	Add(Line([this]
	{
		FDisplayMetrics Metrics;
		Platform->GetDisplayMetrics(Metrics);
		FString Text;
		for (const FMonitorInfo& Monitor : Metrics.Monitors)
		{
			Text += FString::Format("{}{}{}: {}x{} at {}, {}, {:.0f}%", Text.IsEmpty() ? "" : "\n", Monitor.Name.Mid(4), Monitor.bIsPrimary ? " (primary)" : "",
				Monitor.DisplayRect.Width(), Monitor.DisplayRect.Height(), Monitor.DisplayRect.Left, Monitor.DisplayRect.Top, Monitor.DpiScale * 100.0f);
		}
		return Text;
	}));
	Add(Line("Input", true), 10.0f);
	Add(Line([this]
	{
		const TSharedPtr<SWidget> Focused = App->GetKeyboardFocusedWidget();
		const TArray<TSharedRef<SWidget>> Hovered = App->GetHoveredPath();
		return FString::Format("Focus: {}\nUnder the pointer: {}\nCaptured by: {}\nMenus open: {}",
			Focused ? Focused->GetTypeName() : FString("none"), Hovered.empty() ? FString("none") : Hovered.back()->GetTypeName(),
			App->GetMouseCaptor() ? App->GetMouseCaptor()->GetTypeName() : FString("none"), App->GetMenuCount());
	}));
	Add(Line("Renderer", true), 10.0f);
	Add(Line([this]
	{
		const FUIRenderStats& Stats = WindowRenderer->GetStats(*MainWindow->GetNativeWindow());
		return FString::Format("{}: {}\nMain window: {} elements, {} batches\nGlyph atlas pages: {}\nFrames: {}", ToString(Backend), Device->GetAdapterName(),
			Stats.Elements, Stats.Batches, FontCache->GetPageCount(), App->GetFrameNumber());
	}));

	const FWindowStyle& WindowStyle = FAppStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
	return SNew(SVerticalBox)
		// The UI-drawn title bar: the empty part drags the window (the
		// window's TitleBarHeight), the close button claims its own area.
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&WindowStyle.TitleBar).Padding(FMargin(12.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SBox).HeightOverride(TOptional<float>(34.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text("Inspector — borderless window").TextStyle(&WindowStyle.TitleText)]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).ButtonStyle(&WindowStyle.CloseButton).IsFocusable(false).ContentPadding(FMargin(16.0f, 0.0f)).Text("×")
						.OnClicked([this] { ToggleInspector(); return FReply::Handled(); })
					]
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(FMargin(14.0f, 12.0f))[Lines];
}

void FSandboxApp::CreateInspector()
{
	// To the right of the main window when there is room.
	const FIntPoint MainPos = MainWindow->GetNativeWindow()->GetClientPosition();
	const FIntPoint MainSize = MainWindow->GetNativeWindow()->GetClientSize();
	const float Dpi = MainWindow->GetNativeWindow()->GetDpiScale();
	FIntPoint Position(MainPos.X + MainSize.X + FMath::RoundToInt(16 * Dpi), MainPos.Y);
	FDisplayMetrics Metrics;
	Platform->GetDisplayMetrics(Metrics);
	if (const FMonitorInfo* Primary = Metrics.GetPrimary())
	{
		const int32 Width = FMath::RoundToInt(380.0f * Primary->DpiScale);
		if (Position.X + Width > Primary->WorkArea.Right)
		{
			Position = FIntPoint(Primary->WorkArea.Right - Width - 16, MainPos.Y + 40);
		}
	}
	Inspector = SNew(SWindow)
		.Title("Genesis UI Sandbox - Inspector")
		.ClientSize(FVector2(380.0f, 480.0f))
		.ScreenPosition(TOptional<FIntPoint>(Position))
		.UseOSBorder(false)
		.SupportsMaximize(false)
		.AppearsInTaskbar(false)
		.TitleBarHeight(34.0f)
		.OnWindowClosed([this](const TSharedRef<SWindow>&) { Inspector.reset(); })
		[
			MakeInspectorContent()
		];
	App->AddWindow(ToSharedRef(Inspector));
}

void FSandboxApp::ToggleInspector()
{
	if (Inspector)
	{
		Inspector->RequestClose();
	}
	else
	{
		CreateInspector();
	}
}

void FSandboxApp::SetAppScale(float Scale)
{
	AppScale = Scale;
	App->SetApplicationScale(Scale);
	GE_LOG(LogSandbox, Display, "App scale {:.2f}", AppScale);
}

FSandboxFrameInfo FSandboxApp::MakeFrameInfo() const
{
	FSandboxFrameInfo Frame;
	Frame.Time = FPlatformTime::Seconds() - StartTime;
	Frame.DpiScale = MainWindow ? MainWindow->GetNativeWindow()->GetDpiScale() : 1.0f;
	Frame.AppScale = AppScale;
	Frame.Scale = Frame.DpiScale * AppScale;
	Frame.PixelSize = MainWindow ? MainWindow->GetPixelSize() : FIntPoint();
	Frame.LogicalSize = Frame.PixelSize.ToVector2() / Frame.Scale;
	Frame.BackendName = ToString(Backend);
	Frame.AdapterName = Device->GetAdapterName();
	Frame.FramesPerSecond = FramesPerSecond;
	if (MainWindow && WindowRenderer)
	{
		Frame.Stats = WindowRenderer->GetStats(*MainWindow->GetNativeWindow());
	}
	return Frame;
}

void FSandboxApp::RenderViewportTarget(FRHICommandList& CommandList)
{
	// The render-target viewport of the draw-elements page, sized for the main window's scale.
	FSandboxFrameInfo Frame = MakeFrameInfo();
	const FIntPoint TargetSize(FMath::RoundToInt(240.0f * Frame.Scale), FMath::RoundToInt(150.0f * Frame.Scale));
	if (!ViewportTarget || ViewportTarget->GetSize() != TargetSize)
	{
		if (ViewportTarget)
		{
			Device->DeferRelease(ViewportTarget);
		}
		ViewportTarget = Renderer->CreateRenderTarget(TargetSize.X, TargetSize.Y, "Sandbox viewport").ToSharedPtr();
	}
	ViewportElements.Reset();
	Frame.Origin = FVector2::Zero();
	Content->PaintViewportScene(ViewportElements, Frame, TargetSize);
	Renderer->Render(CommandList, ViewportTarget->GetTexture(), ViewportElements, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
}

int32 FSandboxApp::Run(const FString& CommandLine)
{
	if (!Initialize(CommandLine))
	{
		return 1;
	}
	StartTime = LastFpsTime = FPlatformTime::Seconds();
	if (!SelfTestDir.IsEmpty())
	{
		// Keep frames coming so the script advances while nothing changes.
		MainWindow->RegisterActiveTimer(0.0f, [](double, float) { return EActiveTimerReturn::Continue; });
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

	if (Editor)
	{
		Editor->SaveSettings();
	}
	Device->WaitIdle();
	App->SetRenderer(nullptr);
	FUIApplication::Shutdown();
	App.reset();
	MainWindow.reset();
	Inspector.reset();
	Editor.reset();
	WindowRenderer.reset();
	ViewportTarget.reset();
	Renderer.reset();
	if (FileLog)
	{
		FLog::RemoveDevice(FileLog.get());
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Global keys

bool FSandboxApp::HandleKeyDownEvent(FUIApplication&, const FKeyEvent& Event)
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
// Self-test: every gallery page plus a menu and a tooltip, then the DPI,
// minimize, monitor, resize and app-scale script of milestone 2.

void FSandboxApp::Capture(const char* Name)
{
	int32 Floating = 0;
	for (const TSharedRef<SWindow>& Window : TArray<TSharedRef<SWindow>>(App->GetWindows()))
	{
		const char* Kind = Window == ToSharedRef(MainWindow) ? "main" : (Inspector && Window == ToSharedRef(Inspector) ? "inspector" : nullptr);
		const FString Label = Kind ? FString(Kind) : FString::Format("floating{}", ++Floating);
		// Bring the window on top and let DWM compose a couple of frames.
		const HWND Hwnd = static_cast<HWND>(Window->GetNativeWindow()->GetNativeHandle());
		SetWindowPos(Hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		for (int32 I = 0; I < 3; ++I)
		{
			Window->Invalidate(EInvalidateWidgetReason::Paint);
			App->Tick();
		}
		const FString Path = SelfTestDir + FString::Format("{}_{}_{}.png", ToString(Backend), Label, Name);
		const bool bOk = CaptureWindowClientToPng(Hwnd, Path);
		SetWindowPos(Hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		GE_LOG(LogSandbox, Display, "Screenshot {} -> {} ({}x{} px, DPI scale {:.2f}, app scale {:.2f})", bOk ? "saved" : "FAILED", Path,
			Window->GetPixelSize().X, Window->GetPixelSize().Y, Window->GetNativeWindow()->GetDpiScale(), AppScale);
	}
}

bool FSandboxApp::PointAtText(const SWindow& Window, std::string_view Text, bool bClick)
{
	STextBlock* Found = nullptr;
	const TFunction<void(const SWidget&)> Find = [&](const SWidget& Widget)
	{
		if (STextBlock* Block = dynamic_cast<STextBlock*>(const_cast<SWidget*>(&Widget)); Block && Block->GetText().View() == Text && !Found)
		{
			Found = Block;
		}
		for (const FArrangedWidget& Child : Widget.GetArrangedChildren())
		{
			Find(Child.Widget.Get());
		}
	};
	Find(Window);
	if (!Found)
	{
		GE_LOG(LogSandbox, Warning, "Self-test: no \"{}\" to point at", Text);
		return false;
	}
	const TSharedRef<IPlatformWindow> Native = ToSharedRef(Window.GetNativeWindow());
	const FVector2 Center = Found->GetCachedGeometry().GetLayoutBoundingRect().GetCenter();
	App->OnMouseMove(Native, Center);
	if (bClick)
	{
		App->OnMouseDown(Native, EMouseButton::Left, Center);
		App->OnMouseUp(Native, EMouseButton::Left, Center);
	}
	return true;
}

void FSandboxApp::SimulateDpiChange(float NewScale)
{
	// What Windows sends when a window moves to a monitor with another DPI:
	// the new DPI plus a suggested window rectangle keeping the logical size.
	const HWND Hwnd = static_cast<HWND>(MainWindow->GetNativeWindow()->GetNativeHandle());
	RECT Rect;
	GetWindowRect(Hwnd, &Rect);
	const float Ratio = NewScale / MainWindow->GetNativeWindow()->GetDpiScale();
	RECT Suggested{ Rect.left, Rect.top, Rect.left + FMath::RoundToInt((Rect.right - Rect.left) * Ratio), Rect.top + FMath::RoundToInt((Rect.bottom - Rect.top) * Ratio) };
	const WORD Dpi = static_cast<WORD>(FMath::RoundToInt(NewScale * 96.0f));
	GE_LOG(LogSandbox, Display, "Simulating WM_DPICHANGED to {} dpi", Dpi);
	SendMessageW(Hwnd, WM_DPICHANGED, MAKEWPARAM(Dpi, Dpi), reinterpret_cast<LPARAM>(&Suggested));
}

void FSandboxApp::TickSelfTest()
{
	const int32 Frame = SelfTestFrame++;
	const TSharedRef<IPlatformWindow> Native = ToSharedRef(MainWindow->GetNativeWindow());
	const float NativeScale = static_cast<float>(GetDpiForWindow(static_cast<HWND>(Native->GetNativeHandle()))) / 96.0f;
	FTabManager& Tabs = *Editor->TabManager;

	// The editor first, then every gallery page (30 frames each, captured on the 25th).
	switch (Frame)
	{
	case 20: Capture("editor"); return;
	case 25: Tabs.TryInvokeTab("Gallery"); return;
	default: break;
	}
	const int32 PageFrames = 30;
	SWidgetGallery* Gallery = GetGallery();
	const int32 Pages = Gallery ? Gallery->GetPageCount() : 0;
	if (Frame >= 30 && Frame < 30 + Pages * PageFrames)
	{
		const int32 Page = (Frame - 30) / PageFrames;
		const int32 Step = (Frame - 30) % PageFrames;
		if (Step == 0)
		{
			Gallery->SetPage(Page);
		}
		if (Step == 25)
		{
			Capture(FString::Format("page{}", Page + 1).c_str());
		}
		return;
	}
	if (Frame < 30 + Pages * PageFrames)
	{
		return;
	}

	const int32 T = Frame - (30 + Pages * PageFrames);
	switch (T)
	{
	// The gallery's tooltip.
	case 0: Gallery->SetPage(4); break;
	case 5:
		if (Gallery->ToolTipDemo)
		{
			App->OnMouseMove(Native, Gallery->ToolTipDemo->GetCachedGeometry().GetLayoutBoundingRect().GetCenter());
		}
		break;
	case 55: Capture("tooltip"); break;
	case 60:
		App->OnMouseMove(Native, FVector2(2.0f, 2.0f));
		Tabs.TryInvokeTab("Viewport");
		break;

	// Select the lights, open the Edit menu (Undo names the last change) and View > Application scale.
	case 65:
	{
		TArray<int32> Lights;
		for (const FDemoEntity& E : Editor->Scene.Entities)
		{
			if (E.Kind == 1) Lights.push_back(E.Id);
		}
		Editor->Scene.SetSelection(Lights);
		Editor->ViewportClient->FrameSelection();
		break;
	}
	case 70:
		// A change to undo: the lights' brightness through the property grid.
		if (Editor->Details)
		{
			Editor->Details->SetPropertyValue("Brightness", 4.0f);
		}
		break;
	case 75: PointAtText(*MainWindow, "Edit", true); break;
	case 90: Capture("menu_edit"); break;
	case 95: PointAtText(*MainWindow, "View", false); break;
	case 100: PointAtText(*MainWindow, "Application scale", false); break;
	case 115: Capture("menu_view"); break;
	case 120: App->DismissAllMenus(); break;

	// The color picker of the property grid.
	case 125:
	{
		if (Editor->Details)
		{
			const TFunction<SColorBlock*(const SWidget&)> FindSwatch = [&](const SWidget& Widget) -> SColorBlock*
			{
				if (SColorBlock* Block = dynamic_cast<SColorBlock*>(const_cast<SWidget*>(&Widget)))
				{
					return Block;
				}
				for (const FArrangedWidget& Child : Widget.GetArrangedChildren())
				{
					if (SColorBlock* Found = FindSwatch(Child.Widget.Get()))
					{
						return Found;
					}
				}
				return nullptr;
			};
			if (SColorBlock* Swatch = FindSwatch(*Editor->Details))
			{
				const FVector2 Center = Swatch->GetCachedGeometry().GetLayoutBoundingRect().GetCenter();
				App->OnMouseMove(Native, Center);
				App->OnMouseDown(Native, EMouseButton::Left, Center);
				App->OnMouseUp(Native, EMouseButton::Left, Center);
			}
		}
		break;
	}
	case 140: Capture("colorpicker"); break;
	case 145: App->DismissAllMenus(); break;

	// Tear the Details tab off into its own window, then dock it back.
	case 150:
		if (const TSharedPtr<SDockTab> Details = Tabs.FindExistingLiveTab("Details"))
		{
			const FIntPoint Pos = Native->GetClientPosition();
			Tabs.MoveTabToNewWindow(ToSharedRef(Details), FIntPoint(Pos.X + 200, Pos.Y + 160), FVector2(380.0f, 520.0f));
		}
		break;
	case 175: Capture("torn_off"); break;
	case 180:
		if (const TSharedPtr<SDockTab> Details = Tabs.FindExistingLiveTab("Details"))
		{
			if (const TSharedPtr<SDockTab> Viewport = Tabs.FindExistingLiveTab("Viewport"))
			{
				Tabs.MoveTabToNewStack(ToSharedRef(Details), ToSharedRef(Viewport->GetParentStack()), EUINavigation::Right);
			}
		}
		break;
	case 200: Capture("docked_back"); break;
	case 205: Editor->ResetLayout(); break;

	// Milestone 2's script: DPI, minimize, monitors, resize, app scale.
	case 220: Capture("native"); break;
	case 230: SimulateDpiChange(1.0f); break;
	case 260: Capture("dpi100"); break;
	case 270: SimulateDpiChange(2.0f); break;
	case 300: Capture("dpi200"); break;
	case 310: SimulateDpiChange(NativeScale); break;
	case 330: Native->Minimize(); break;
	case 360: Native->Restore(); break;
	case 390: Capture("restored"); break;
	case 400:
	{
		FDisplayMetrics Metrics;
		Platform->GetDisplayMetrics(Metrics);
		const FIntPoint Pos = Native->GetClientPosition();
		for (const FMonitorInfo& Monitor : Metrics.Monitors)
		{
			const bool bContains = Pos.X >= Monitor.DisplayRect.Left && Pos.X < Monitor.DisplayRect.Right && Pos.Y >= Monitor.DisplayRect.Top && Pos.Y < Monitor.DisplayRect.Bottom;
			if (!bContains)
			{
				GE_LOG(LogSandbox, Display, "Moving the main window to monitor {} ({:.0f}%)", Monitor.Name, Monitor.DpiScale * 100.0f);
				Native->MoveWindowTo(Monitor.WorkArea.Left + 80, Monitor.WorkArea.Top + 80);
				if (Inspector)
				{
					Inspector->GetNativeWindow()->MoveWindowTo(Monitor.WorkArea.Left + 80 + Native->GetClientSize().X + 24, Monitor.WorkArea.Top + 80);
				}
				break;
			}
		}
		break;
	}
	case 440: Capture("monitor2"); break;
	case 450:
	{
		const FIntPoint Pos = Native->GetClientPosition();
		Native->ReshapeWindow(Pos.X, Pos.Y, 1500, 1000);
		break;
	}
	case 480: Capture("resized"); break;
	case 490: SetAppScale(1.25f); break;
	case 520: Capture("appscale125"); break;
	case 530: App->RequestQuit(); break;
	default: break;
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const FString CommandLine = FString::FromWide(GetCommandLineW());
	int32 Result;
	{
		auto App = std::make_shared<FSandboxApp>();
		Result = App->Run(CommandLine);
	}
	CoUninitialize();
	return Result;
}
