// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// GenesisMapEditor: the map editor on Genesis UI (milestone 5 of
// docs/GenesisUI.md). It opens, edits and saves maps in four docked views,
// with menus, a toolbar and shortcuts from commands. The MFC editor
// (GenesisEditor.exe) keeps working until the new one reaches parity.
//
// Command line:
//   GenesisMapEditor.exe [map.qle | map.map]
//   -d3d12 | -vulkan       backend (default D3D12)
//   -appscale=1.25         application scale on top of the monitor DPI
//   -nodebug / -debug      D3D12 debug layer / Vulkan validation (default: on in Debug builds)
//   -resetlayout           start from the default window layout and key bindings
//   -selftest=<dir>        scripted run: builds a map from prefabs and
//                          entities, selects, moves with the mouse, undoes,
//                          box-selects, opens a menu, saves .qle and .map and
//                          reads them back, changes the DPI; PNG screenshots
//                          and a log go to <dir>; exits when done
//
// Settings (game folders, grid, the last folder, layout and key bindings)
// are kept in %LOCALAPPDATA%\Genesis\MapEditor.

#include "Application/UIApplication.h"
#include "CoreMinimal.h"
#include "GenericPlatform/PlatformDialogs.h"
#include "MapCoreHeaders.h"
#include "MapEditorShell.h"
#include "RHI.h"
#include "Screenshot.h"
#include "Misc/App.h"
#include "Services/ConfigStore.h"
#include "Services/Services.h"
#include "Styling/GenesisStyle.h"
#include "UIRenderer.h"
#include "UIWindowRenderer.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <Windows.h>
#include <objbase.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

DEFINE_LOG_CATEGORY(LogGenesisMapEditor, Log);

namespace
{
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

class FMapEditorApp final
{
public:
	int32 Run(const FString& CommandLine);

private:
	bool Initialize(const TArray<FString>& Args);
	void SetAppScale(float Scale);
	void Shutdown();

	// Self-test.
	void TickSelfTest();
	void Capture(const char* Name);
	FVector2 ViewPointToWindow(EMapViewType Type, const Vector3d& Point) const;
	void Drag(const FVector2& From, const FVector2& To, EMouseButton Button = EMouseButton::Left);
	bool PointAtText(std::string_view Text, bool bClick);
	void SimulateDpiChange(float NewScale);
	Object* FindByClass(const char* ClassName) const;

	TSharedPtr<IPlatformApplication> Platform;
	TUniquePtr<FRHIDevice> Device;
	TSharedPtr<FFontCache> FontCache;
	TUniquePtr<FUIRenderer> Renderer;
	TUniquePtr<FUIWindowRenderer> WindowRenderer;
	TSharedPtr<FUIApplication> App;
	TSharedPtr<SWindow> MainWindow;
	TSharedPtr<FMapEditorShell> Shell;
	TUniquePtr<FFileOutputDevice> FileLog;
	TUniquePtr<IConfigStore> Config;
	TUniquePtr<IMessageDialog> MessageDialog;
	TUniquePtr<IFileDialogs> FileDialogs;
	bool bEnvironmentStarted = false;

	ERHIBackend Backend = ERHIBackend::D3D12;
	float AppScale = 1.0f;
	FString SelfTestDir;
	FString SettingsDir;
	int32 SelfTestFrame = 0;
	Object* TestCube = nullptr;
};

// ---------------------------------------------------------------------------

bool FMapEditorApp::Initialize(const TArray<FString>& Args)
{
	FApp::SetName("Genesis Map Editor");
	if (const TOptional<FString> Dir = GetOption(Args, "-selftest="))
	{
		SelfTestDir = *Dir;
		if (!SelfTestDir.EndsWith("/") && !SelfTestDir.EndsWith("\\"))
		{
			SelfTestDir += "/";
		}
		CreateDirectoryW(SelfTestDir.ToWide().c_str(), nullptr);
	}
	for (const FString& Arg : Args)
	{
		if (Arg.Equals("-vulkan", ESearchCase::IgnoreCase))
		{
			Backend = ERHIBackend::Vulkan;
		}
	}
	if (!SelfTestDir.IsEmpty())
	{
		FileLog = MakeUnique<FFileOutputDevice>(SelfTestDir + FString::Format("log_{}.txt", ToString(Backend)));
		FLog::AddDevice(FileLog.get());
	}

	// Settings. Scripted runs start from the defaults and keep nothing.
	if (SelfTestDir.IsEmpty())
	{
		if (const wchar_t* LocalAppData = _wgetenv(L"LOCALAPPDATA"))
		{
			const std::filesystem::path Dir = std::filesystem::path(LocalAppData) / L"Genesis" / L"MapEditor";
			std::error_code Error;
			std::filesystem::create_directories(Dir, Error);
			SettingsDir = FString::FromWide(Dir.wstring()) + "\\";
			if (HasFlag(Args, "-resetlayout"))
			{
				std::filesystem::remove(Dir / L"Layout.json", Error);
				std::filesystem::remove(Dir / L"Bindings.json", Error);
			}
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
	if (SelfTestDir.IsEmpty())
	{
		MessageDialog = FPlatformDialogs::CreateMessageDialog();
		FileDialogs = FPlatformDialogs::CreateFileDialogs();
		FServices::SetMessageDialog(MessageDialog.get());
		FServices::SetFileDialogs(FileDialogs.get());
	}

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
		GE_LOG(LogGenesisMapEditor, Error, "{}", Message);
		if (SelfTestDir.IsEmpty())
		{
			MessageBoxW(nullptr, Message.ToWide().c_str(), L"Genesis Map Editor", MB_ICONERROR);
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
		GE_LOG(LogGenesisMapEditor, Error, "UI renderer initialization failed");
		return false;
	}
	WindowRenderer = MakeUnique<FUIWindowRenderer>(*Device, *Renderer);
	App = FUIApplication::Create(ToSharedRef(Platform), ToSharedRef(FontCache)).ToSharedPtr();
	App->SetRenderer(WindowRenderer.get());
	App->SetApplicationScale(AppScale);

	// The map core: Content/Editor for the built-in game, and the old pak
	// (next to the executable) for the legacy games when it is there.
	FMapEditorEnvironment::FStartupOptions Options;
	Options.EditorContentDir = FPaths::Combine(FPaths::FindContentDir(), "Editor");
	Options.PakPath = FPaths::Combine(FPaths::ExecutableDir(), "GenesisEditor.pak");
	bEnvironmentStarted = FMapEditorEnvironment::Startup(Options);
	FMapEditorEnvironment::Activate(FMapEditorEnvironment::GetBuiltInGameSet());

	FMapEditorShell::FHost Host;
	Host.Device = Device.get();
	Host.Renderer = Renderer.get();
	Host.WindowRenderer = WindowRenderer.get();
	Host.SettingsDir = SettingsDir;
	Host.Quit = [this] { App->RequestQuit(); };
	Host.SetAppScale = [this](float Scale) { SetAppScale(Scale); };
	Shell = std::make_shared<FMapEditorShell>(std::move(Host));

	MainWindow = SNew(SWindow)
		.Title("Genesis Map Editor")
		.ClientSize(FVector2(1440.0f, 900.0f))
		.OnWindowClosed([this](const TSharedRef<SWindow>&) { App->RequestQuit(); });
	App->AddWindow(ToSharedRef(MainWindow));
	MainWindow->SetContent(Shell->MakeContent(ToSharedRef(MainWindow)));
	Shell->NewMap(FMapEditorEnvironment::GetBuiltInGameSet());

	// A map named on the command line (the first argument after the program).
	for (SIZE_T Index = 1; Index < Args.size(); ++Index)
	{
		if (!Args[Index].StartsWith("-"))
		{
			Shell->OpenMap(Args[Index]);
			break;
		}
	}
	return true;
}

void FMapEditorApp::SetAppScale(float Scale)
{
	AppScale = Scale;
	App->SetApplicationScale(Scale);
	GE_LOG(LogGenesisMapEditor, Display, "App scale {:.2f}", Scale);
}

int32 FMapEditorApp::Run(const FString& CommandLine)
{
	const TArray<FString> Args = SplitCommandLine(CommandLine);
	if (!Initialize(Args))
	{
		Shutdown();
		return 1;
	}
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
	Shutdown();
	return 0;
}

void FMapEditorApp::Shutdown()
{
	if (Shell)
	{
		Shell->SaveSettings();
	}
	if (Device)
	{
		Device->WaitIdle();
	}
	if (App)
	{
		App->SetRenderer(nullptr);
		FUIApplication::Shutdown();
	}
	App.reset();
	MainWindow.reset();
	// The shell owns the map; the map core must still be running.
	Shell.reset();
	if (bEnvironmentStarted)
	{
		FMapEditorEnvironment::Shutdown();
	}
	WindowRenderer.reset();
	Renderer.reset();
	Device.reset();
	if (Config)
	{
		Config->Flush();
	}
	FServices::SetMessageDialog(nullptr);
	FServices::SetFileDialogs(nullptr);
	FServices::SetConfig(nullptr);
	if (FileLog)
	{
		FLog::RemoveDevice(FileLog.get());
	}
}

// ---------------------------------------------------------------------------
// Self-test

void FMapEditorApp::Capture(const char* Name)
{
	const HWND Hwnd = static_cast<HWND>(MainWindow->GetNativeWindow()->GetNativeHandle());
	SetWindowPos(Hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	for (int32 I = 0; I < 3; ++I)
	{
		MainWindow->Invalidate(EInvalidateWidgetReason::Paint);
		App->Tick();
	}
	const FString Path = SelfTestDir + FString::Format("{}_{}.png", ToString(Backend), Name);
	const bool bOk = CaptureWindowClientToPng(Hwnd, Path);
	SetWindowPos(Hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	GE_LOG(LogGenesisMapEditor, Display, "Screenshot {} -> {} ({}x{} px, DPI scale {:.2f})", bOk ? "saved" : "FAILED", Path,
		MainWindow->GetPixelSize().X, MainWindow->GetPixelSize().Y, MainWindow->GetNativeWindow()->GetDpiScale());
}

FVector2 FMapEditorApp::ViewPointToWindow(EMapViewType Type, const Vector3d& Point) const
{
	const TSharedPtr<FMapViewportClient> View = Shell->GetView(Type);
	const TSharedPtr<SViewport> Widget = View ? View->GetViewport() : nullptr;
	FVector2 Local;
	if (!Widget || !View->MapToScreen(Point, Local))
	{
		return FVector2(-1.0f, -1.0f);
	}
	return Widget->GetCachedGeometry().LocalToAbsolute(Local);
}

void FMapEditorApp::Drag(const FVector2& From, const FVector2& To, EMouseButton Button)
{
	const TSharedRef<IPlatformWindow> Native = ToSharedRef(MainWindow->GetNativeWindow());
	App->OnMouseMove(Native, From);
	App->OnMouseDown(Native, Button, From);
	for (int32 Step = 1; Step <= 8; ++Step)
	{
		const float T = static_cast<float>(Step) / 8.0f;
		App->OnMouseMove(Native, From + (To - From) * T);
	}
	App->OnMouseUp(Native, Button, To);
}

bool FMapEditorApp::PointAtText(std::string_view Text, bool bClick)
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
	Find(*MainWindow);
	if (!Found)
	{
		GE_LOG(LogGenesisMapEditor, Warning, "Self-test: no \"{}\" to point at", Text);
		return false;
	}
	const TSharedRef<IPlatformWindow> Native = ToSharedRef(MainWindow->GetNativeWindow());
	const FVector2 Center = Found->GetCachedGeometry().GetLayoutBoundingRect().GetCenter();
	App->OnMouseMove(Native, Center);
	if (bClick)
	{
		App->OnMouseDown(Native, EMouseButton::Left, Center);
		App->OnMouseUp(Native, EMouseButton::Left, Center);
	}
	return true;
}

void FMapEditorApp::SimulateDpiChange(float NewScale)
{
	const HWND Hwnd = static_cast<HWND>(MainWindow->GetNativeWindow()->GetNativeHandle());
	RECT Rect;
	GetWindowRect(Hwnd, &Rect);
	const float Ratio = NewScale / MainWindow->GetNativeWindow()->GetDpiScale();
	RECT Suggested{ Rect.left, Rect.top, Rect.left + FMath::RoundToInt((Rect.right - Rect.left) * Ratio), Rect.top + FMath::RoundToInt((Rect.bottom - Rect.top) * Ratio) };
	const WORD Dpi = static_cast<WORD>(FMath::RoundToInt(NewScale * 96.0f));
	GE_LOG(LogGenesisMapEditor, Display, "Simulating WM_DPICHANGED to {} dpi", Dpi);
	SendMessageW(Hwnd, WM_DPICHANGED, MAKEWPARAM(Dpi, Dpi), reinterpret_cast<LPARAM>(&Suggested));
}

Object* FMapEditorApp::FindByClass(const char* ClassName) const
{
	for (Object* Obj : Shell->GetDocument()->GetObjects())
	{
		if (Obj->HasEntity() && !stricmp(Obj->GetEntityPtr()->GetClassname(), ClassName))
		{
			return Obj;
		}
	}
	return nullptr;
}

void FMapEditorApp::TickSelfTest()
{
	const int32 Frame = SelfTestFrame++;
	FMapDocument* Doc = Shell->GetDocument();
	const auto Prefab = [](const char* Name) -> FString
	{
		for (const FMapEditorEnvironment::FPrefab& P : FMapEditorEnvironment::GetPrefabs())
		{
			if (P.Name == Name)
			{
				return P.Path;
			}
		}
		GE_LOG(LogGenesisMapEditor, Error, "Self-test: no prefab {}", Name);
		return FString();
	};
	// After OpenMap the shell holds another document: look it up each time.
	const auto Report = [this](const char* What)
	{
		FMapDocument* Doc = Shell->GetDocument();
		Vector3d Min, Max;
		const bool bAny = Doc->GetSelectionBounds(Min, Max);
		GE_LOG(LogGenesisMapEditor, Display, "Self-test {}: {} brushes, {} entities, {} selected{}", What, Doc->CountBrushes(), Doc->CountEntities(),
			Doc->GetSelection().size(),
			bAny ? FString::Format(" at ({:.0f} {:.0f} {:.0f})-({:.0f} {:.0f} {:.0f})", Min.GetX(), Min.GetY(), Min.GetZ(), Max.GetX(), Max.GetY(), Max.GetZ()) : FString());
	};
	const FString QlePath = SelfTestDir + "selftest.qle";
	const FString MapPath = SelfTestDir + "selftest.map";
	FUICommandList& Commands = *Shell->GetCommandList();

	switch (Frame)
	{
	case 20: Capture("empty"); break;

	// A small scene: prefabs on the grid and two entities.
	case 25:
		Doc->AddPrefab(Prefab("Plate"), Vector3d(0.0f, 0.0f, -8.0f));
		TestCube = Doc->AddPrefab(Prefab("Cube"), Vector3d(-160.0f, -96.0f, 32.0f));
		Doc->AddPrefab(Prefab("Column"), Vector3d(160.0f, 96.0f, 64.0f));
		Doc->AddPrefab(Prefab("Sphere"), Vector3d(-128.0f, 160.0f, 64.0f));
		Doc->AddPrefab(Prefab("Pyramid"), Vector3d(176.0f, -144.0f, 32.0f));
		Doc->AddEntity("info_player_start", Vector3d(-32.0f, -192.0f, 32.0f));
		Doc->AddEntity("light", Vector3d(32.0f, 32.0f, 192.0f));
		Doc->ClearSelection();
		Commands.ExecuteAction(*Shell->Cmd.FrameSelection);
		Report("scene built");
		break;
	case 40: Capture("scene"); break;

	// Click the cube in the top view, then drag it 128 units right and 64 up.
	case 45:
	{
		const TSharedRef<IPlatformWindow> Native = ToSharedRef(MainWindow->GetNativeWindow());
		const FVector2 At = ViewPointToWindow(EMapViewType::Top, Vector3d(-160.0f, -96.0f, 32.0f));
		App->OnMouseMove(Native, At);
		App->OnMouseDown(Native, EMouseButton::Left, At);
		App->OnMouseUp(Native, EMouseButton::Left, At);
		Report("clicked the cube");
		break;
	}
	case 50:
	{
		const FVector2 From = ViewPointToWindow(EMapViewType::Top, Vector3d(-160.0f, -96.0f, 32.0f));
		const FVector2 To = ViewPointToWindow(EMapViewType::Top, Vector3d(-32.0f, -32.0f, 32.0f));
		Drag(From, To);
		Report("dragged the cube");
		break;
	}
	case 60: Capture("moved"); break;
	case 65: PointAtText("Edit", true); break;
	case 75: Capture("menu_edit"); break;
	case 80:
		App->DismissAllMenus();
		Commands.ExecuteAction(*Shell->Cmd.Undo);
		Report("after undo");
		Commands.ExecuteAction(*Shell->Cmd.Redo);
		Report("after redo");
		break;

	// Box-select in the front view, across the middle of the scene.
	case 85:
	{
		const FVector2 From = ViewPointToWindow(EMapViewType::Front, Vector3d(-200.0f, 0.0f, 120.0f));
		const FVector2 To = ViewPointToWindow(EMapViewType::Front, Vector3d(200.0f, 0.0f, -40.0f));
		Drag(From, To);
		Report("box selection");
		break;
	}
	case 95: Capture("box_select"); break;
	case 100:
		Commands.ExecuteAction(*Shell->Cmd.Duplicate);
		Report("duplicated");
		Commands.ExecuteAction(*Shell->Cmd.Delete);
		Report("deleted the copies");
		break;
	case 105:
		Commands.ExecuteAction(*Shell->Cmd.SolidView);
		break;
	case 115: Capture("wireframe_3d"); break;
	case 120:
		Commands.ExecuteAction(*Shell->Cmd.SolidView);
		Doc->SetSelection({ TestCube, FindByClass("light") });
		break;

	// Save, export and read both back.
	case 125:
		Shell->SaveMap(QlePath);
		Shell->ExportMap(MapPath);
		break;
	case 130:
		Shell->OpenMap(QlePath);
		Report("reopened .qle");
		break;
	case 140: Capture("reopened"); break;
	case 145:
		Shell->OpenMap(MapPath);
		Report("imported .map");
		break;
	case 155: Capture("imported"); break;

	// DPI changes, as in milestone 2.
	case 160: SimulateDpiChange(1.0f); break;
	case 185: Capture("dpi100"); break;
	case 190: SimulateDpiChange(2.0f); break;
	case 215: Capture("dpi200"); break;
	case 220: SimulateDpiChange(static_cast<float>(GetDpiForWindow(static_cast<HWND>(MainWindow->GetNativeWindow()->GetNativeHandle()))) / 96.0f); break;
	case 230: App->RequestQuit(); break;
	default: break;
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const FString CommandLine = FString::FromWide(GetCommandLineW());
	int32 Result;
	{
		FMapEditorApp EditorApp;
		Result = EditorApp.Run(CommandLine);
	}
	CoUninitialize();
	return Result;
}
