// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// GenesisEditor: the map editor of Genesis Engine, on Genesis UI
// (Engine/Documentation/Source/GenesisUI.md, milestones 5 and 6). It replaced the MFC editor of
// the same name. Laid out like Unreal's level editor: the viewports, Place
// Actors, the Outliner and Details, content browsers and the Output Log,
// editor modes, the Editor Preferences window, menus, a toolbar and
// shortcuts from commands.
//
// Command line:
//   GenesisEditor.exe [map.qle | map.map]
//   -d3d12 | -vulkan       backend (default: Editor Preferences > Graphics, else D3D12)
//   -appscale=1.25         application scale on top of the monitor DPI
//   -nodebug / -debug      D3D12 debug layer / Vulkan validation (default: the
//                          preferences, else on in Debug builds)
//   -resetlayout           start from the default window layout and key bindings
//   -runscript=<path>     Lua/Python source or .NET DLL; -scriptfunction=Main
//   -scriptmodule=<name>  execute a named module from the active .gproject
//   -scriptargs=<json>    argument file; -scriptresult=<json> writes outcome
//   -exitafterscript      exit after script execution (nonzero on failure)
//   -selftest=<dir>        scripted run: builds maps from prefabs and
//                          entities, selects, moves, rotates and clips with
//                          the mouse, hollows and subtracts, edits
//                          properties and groups, opens menus and panels,
//                          saves .qle and .map and reads them back, changes
//                          the DPI; PNG screenshots and a log go to <dir>;
//                          exits when done
//
// Settings (preferences, grid, tools, the last map, layout, key bindings)
// and the prefabs you save are kept in %LOCALAPPDATA%\Genesis\Editor.

#include "Framework/Application/SlateApplication.h"
#include "CoreMinimal.h"
#include "ProjectDescriptor.h"
#include "AssetRegistry.h"
#include "MaterialEditor.h"
#include "EditorScriptExecution.h"
#include "HAL/File.h"
#include "MaterialEditingLibrary.h"
#include "MapAssetLibrary.h"
#include "GenericPlatform/PlatformDialogs.h"
#include "MapCoreHeaders.h"
#include "MapEditorShell.h"
#include "RHI.h"
#include "Screenshot.h"
#include "Misc/App.h"
#include "Services/ConfigStore.h"
#include "Services/Services.h"
#include "SOutputLog.h"
#include "Styling/StarshipCoreStyle.h"
#include "SlateRHIRenderingPolicy.h"
#include "SlateRHIRenderer.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <Windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <crtdbg.h>

#pragma comment(lib, "dwmapi.lib")

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>

DEFINE_LOG_CATEGORY(LogGenesisEditor, Log);

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
	// The main window, or another one (the preferences).
	void Capture(const char* Name, const TSharedPtr<SWindow>& Window = nullptr);
	FVector2 ViewPointToWindow(EMapViewType Type, const Vector3d& Point) const;
	void Drag(const FVector2& From, const FVector2& To, EMouseButton Button = EMouseButton::Left);
	bool PointAtText(std::string_view Text, bool bClick, TSharedPtr<SWindow> Target = nullptr);
	void SimulateDpiChange(float NewScale);
	Object* FindByClass(const char* ClassName) const;

	TSharedPtr<IPlatformApplication> Platform;
	TUniquePtr<FRHIDevice> Device;
	TSharedPtr<FSlateFontCache> FontCache;
	TUniquePtr<FSlateRHIRenderingPolicy> Renderer;
	TUniquePtr<FSlateRHIRenderer> WindowRenderer;
	TSharedPtr<FSlateApplication> App;
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
	float LandscapePaintBefore = 0.0f;
	float LandscapeBrushBefore = 0.0f;
	float LandscapeGridBefore = 0.0f;
	FString SettingsDir;
	int32 SelfTestFrame = 0;
	Object* TestCube = nullptr;
	Object* TestColumn = nullptr;
	Object* TestSphere = nullptr;
	Object* TestPyramid = nullptr;
};

// ---------------------------------------------------------------------------

bool FMapEditorApp::Initialize(const TArray<FString>& Args)
{
	FApp::SetName("Genesis Editor");
	// Resolve a project before settings, windows or content browsers are built.
	TOptional<FString> ProjectPath = GetOption(Args, "-project=");
	const auto NewProject = GetOption(Args, "-newproject=");
	for (SIZE_T Index = 1; Index < Args.size(); ++Index)
		if (!Args[Index].StartsWith("-") && Args[Index].EndsWith(".gproject", ESearchCase::IgnoreCase))
			ProjectPath = Args[Index];
	if (NewProject && ProjectPath)
	{
		MessageBoxW(nullptr, L"Use either -project or -newproject.", L"Genesis Project", MB_OK | MB_ICONERROR);
		return false;
	}
	if (ProjectPath || NewProject)
	{
		FString Error;
		const FString Path = NewProject ? *NewProject : *ProjectPath;
		auto Project = NewProject
			? FProjectContext::Create(Path, FString::FromWide(std::filesystem::path(Path.ToWide()).stem().wstring()), Error)
			: FProjectContext::Open(Path, Error);
		if (!Project)
		{
			MessageBoxW(nullptr, Error.ToWide().c_str(), L"Genesis Project", MB_OK | MB_ICONERROR);
			return false;
		}
		FProjectContext::SetActive(std::move(Project));
	}
	if (const TOptional<FString> Dir = GetOption(Args, "-selftest="))
	{
		SelfTestDir = *Dir;
		if (!SelfTestDir.EndsWith("/") && !SelfTestDir.EndsWith("\\"))
		{
			SelfTestDir += "/";
		}
		CreateDirectoryW(SelfTestDir.ToWide().c_str(), nullptr);
	}
	// The Output Log shows everything from here on.
	FOutputLogHistory::Get().Start();
	bool bBackendFromCommandLine = false;
	for (const FString& Arg : Args)
	{
		if (Arg.Equals("-vulkan", ESearchCase::IgnoreCase) || Arg.Equals("-d3d12", ESearchCase::IgnoreCase))
		{
			Backend = Arg.Equals("-vulkan", ESearchCase::IgnoreCase) ? ERHIBackend::Vulkan : ERHIBackend::D3D12;
			bBackendFromCommandLine = true;
		}
	}
	if (!SelfTestDir.IsEmpty())
	{
#if defined(_DEBUG)
		// Failed checks go to the log (and the debugger), not to a dialog
		// that would stop the scripted run.
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif
		FileLog = MakeUnique<FFileOutputDevice>(SelfTestDir + FString::Format("log_{}.txt", ToString(Backend)));
		FLog::AddDevice(FileLog.get());
	}

	// Settings. Scripted runs start from the defaults and keep nothing.
	if (SelfTestDir.IsEmpty())
	{
		if (const FProjectContext* Project = FProjectContext::GetActive())
		{
			const auto Dir = std::filesystem::path(Project->GetSavedDir().ToWide()) / "Editor";
			std::error_code Error;
			std::filesystem::create_directories(Dir, Error);
			if (Error) return false;
			SettingsDir = FString::FromWide(Dir.wstring()) + "\\";
			if (HasFlag(Args, "-resetlayout"))
			{
				std::filesystem::remove(Dir / "Layout.json", Error);
				std::filesystem::remove(Dir / "Bindings.json", Error);
			}
		}
		else if (const wchar_t* LocalAppData = _wgetenv(L"LOCALAPPDATA"))
		{
			const std::filesystem::path Dir = std::filesystem::path(LocalAppData) / L"Genesis" / L"Editor";
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
	// Editor Preferences > Graphics, unless the command line says otherwise
	// (scripted runs keep the defaults).
	const FMapEditorShell::FStartupGraphics Graphics = SelfTestDir.IsEmpty() ? FMapEditorShell::GetStartupGraphics() : FMapEditorShell::FStartupGraphics();
	if (!bBackendFromCommandLine && Graphics.Backend)
	{
		Backend = *Graphics.Backend;
	}
	DeviceDesc.Backend = Backend;
	if (Graphics.DebugLayer) DeviceDesc.bDebugLayer = *Graphics.DebugLayer;
	if (HasFlag(Args, "-debug")) DeviceDesc.bDebugLayer = true;
	if (HasFlag(Args, "-nodebug")) DeviceDesc.bDebugLayer = false;
	Device = FRHIDevice::Create(DeviceDesc);
	if (!Device)
	{
		const FString Message = FString::Format("Could not create a {} device. See the log for details.", ToString(Backend));
		UE_LOG(LogGenesisEditor, Error, "{}", Message);
		if (SelfTestDir.IsEmpty())
		{
			MessageBoxW(nullptr, Message.ToWide().c_str(), L"Genesis Editor", MB_ICONERROR);
		}
		return false;
	}

	FontCache = std::make_shared<FSlateFontCache>();
	const FStarshipCoreFonts Fonts = FStarshipCoreFonts::Load(*FontCache);
	if (!Fonts.IsValid())
	{
		return false;
	}
	const TSharedRef<FSlateStyleSet> Style = FStarshipCoreStyle::Create(Fonts);
	FSlateStyleSet::Register(Style);
	FAppStyle::SetAppStyleSet(Style);

	Renderer = MakeUnique<FSlateRHIRenderingPolicy>(*Device, *FontCache);
	if (!Renderer->Initialize())
	{
		UE_LOG(LogGenesisEditor, Error, "UI renderer initialization failed");
		return false;
	}
	WindowRenderer = MakeUnique<FSlateRHIRenderer>(*Device, *Renderer);
	App = FSlateApplication::Create(ToSharedRef(Platform), ToSharedRef(FontCache)).ToSharedPtr();
	App->SetRenderer(WindowRenderer.get());
	App->SetApplicationScale(AppScale);

	// The map core, with the built-in game's data in Content/Editor.
	FMapEditorEnvironment::FStartupOptions Options;
	Options.EditorContentDir = FPaths::Combine(FPaths::FindContentDir(), "Editor");
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
		.Title(FProjectContext::GetActive()
			? FProjectContext::GetActive()->GetDescriptor().Name + " - Genesis Editor" : FString("Genesis Editor"))
		.ClientSize(FVector2(1440.0f, 900.0f))
		.OnWindowClosed([this](const TSharedRef<SWindow>&) { App->RequestQuit(); });
	App->AddWindow(ToSharedRef(MainWindow));
	MainWindow->SetContent(Shell->MakeContent(ToSharedRef(MainWindow)));
	Shell->NewMap(FMapEditorEnvironment::GetBuiltInGameSet());

	// A map named on the command line (the first argument after the program),
	// else the last one if the preferences say so.
	bool bOpened = false;
	for (SIZE_T Index = 1; Index < Args.size(); ++Index)
	{
		if (!Args[Index].StartsWith("-") && !Args[Index].EndsWith(".gproject", ESearchCase::IgnoreCase))
		{
			Shell->OpenMap(Args[Index]);
			bOpened = true;
			break;
		}
	}
	if (!bOpened && !GetOption(Args, "-runscript=") && !GetOption(Args, "-scriptmodule="))
	{
		if (const auto* Project = FProjectContext::GetActive(); Project && !Project->GetDescriptor().DefaultMap.IsEmpty())
		{
			FString File, Error;
			if (Project->ResolvePackage(Project->GetDescriptor().DefaultMap, true, File, Error))
			{
				Shell->OpenMap(File);
				bOpened = true;
			}
		}
	}
	if (!bOpened && !GetOption(Args, "-runscript=") && !GetOption(Args, "-scriptmodule=") && SelfTestDir.IsEmpty() && FMapEditorShell::GetStartupMapPreference() == FMapEditorShell::EStartupMap::LastOpened)
	{
		const FString Last = FMapEditorShell::GetLastMap();
		std::error_code Error;
		if (!Last.IsEmpty() && std::filesystem::exists(std::filesystem::path(Last.ToWide()), Error))
		{
			Shell->OpenMap(Last);
		}
	}
	UE_LOG(LogGenesisEditor, Display, "Drawing with {} on {}", ToString(Backend), Device->GetAdapterName());
	return true;
}

void FMapEditorApp::SetAppScale(float Scale)
{
	AppScale = Scale;
	App->SetApplicationScale(Scale);
	UE_LOG(LogGenesisEditor, Display, "App scale {:.2f}", Scale);
}

int32 FMapEditorApp::Run(const FString& CommandLine)
{
	const TArray<FString> Args = SplitCommandLine(CommandLine);
	if (!Initialize(Args))
	{
		Shutdown();
		return 1;
	}
    auto Script=GetOption(Args,"-runscript=");
    FString DefaultScriptFunction="Main";
    if(const auto Module=GetOption(Args,"-scriptmodule=")) {
        const auto* Project=FProjectContext::GetActive();FString File,Error;
        if(Script||!Project||!Project->ResolveScriptModule(*Module,true,File,Error)){
            UE_LOG(LogGenesisEditor,Error,"Named script module requires an active project and cannot be combined with -runscript: {}",Error);Shutdown();return 1;
        }
        Script=File;
        for(const auto& Entry:Project->GetDescriptor().ScriptModules)if(Entry.Name==*Module)DefaultScriptFunction=Entry.Extensions.Get("EditorFunction").AsString("Main");
    }
    if (Script)
    {
		FEditorScriptRequest Request;
		Request.ModulePath = *Script;
		Request.Language = GetOption(Args, "-scriptlanguage=").value_or(Script->EndsWith(".lua", ESearchCase::IgnoreCase) ? FString("Lua") : Script->EndsWith(".py", ESearchCase::IgnoreCase) ? FString("Python") : FString("DotNet"));
		Request.Function = GetOption(Args, "-scriptfunction=").value_or(DefaultScriptFunction);
		Request.Capabilities.push_back("Editor.Files");
		FString Error; FJsonValue Result;
		if (const auto Input=GetOption(Args,"-scriptargs="))
		{
			FString Text;
			if (!FFile::ReadAllText(*Input,Text)) {Shutdown();return 1;}
			auto Parsed=FJsonValue::Parse(Text.View(),&Error);
			if (!Parsed) {UE_LOG(LogGenesisEditor,Error,"Script arguments: {}",Error);Shutdown();return 1;}
			Request.Arguments=std::move(*Parsed);
		}
		auto Context=std::make_shared<FEditorScriptContext>();
		Context->GetDocument=[this]{return Shell->GetDocument();};
		Context->Assets=FMapAssetLibrary::GetRegistry();
		Context->GetCommands=[this]{return Shell->GetCommandList();};
		Request.Capabilities.push_back("Editor.Commands");
		const bool Success=ExecuteEditorScript(Request,Context,Result,Error);
		if (!Success) UE_LOG(LogGenesisEditor,Error,"Editor script failed: {}",Error);
		if (const auto Output=GetOption(Args,"-scriptresult="))
		{
			auto Report=FJsonValue::MakeObject();Report["Success"]=Success;Report["Result"]=Result;Report["Error"]=Error;
			if (!FFile::WriteAllText(*Output,Report.ToString())) {Shutdown();return 1;}
		}
		if (!Success || HasFlag(Args,"-exitafterscript")) {Shutdown();return Success?0:1;}
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
		FSlateApplication::Shutdown();
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
	FOutputLogHistory::Get().Stop();
}

// ---------------------------------------------------------------------------
// Self-test

void FMapEditorApp::Capture(const char* Name, const TSharedPtr<SWindow>& Window)
{
	const TSharedPtr<SWindow> Target = Window ? Window : MainWindow;
	if ((std::string_view(Name)=="material_asset"||std::string_view(Name)=="material_instance"))
	{
		for(int I=0;I<3;++I){Target->Invalidate(EInvalidateWidgetReason::Paint);App->Tick();}
		const auto Size=Target->GetPixelSize();
		auto Image=Renderer->CreateRenderTarget(Size.X,Size.Y,"Material editor QA");
		auto& Cmd=Device->BeginFrame();Renderer->BeginFrame();
		Renderer->Render(Cmd,Image->GetTexture(),Target->GetDrawElements(),FLinearColor(.03f,.03f,.03f,1));
		Device->EndFrame();TArray<uint8> Pixels;
		const auto Path=SelfTestDir+FString::Format("{}_{}.png",ToString(Backend),Name);
		const bool Good=Device->ReadbackTexture(Image->GetTexture(),Pixels)&&SaveRgbaToPng(Path,Size.X,Size.Y,Pixels);
		UE_LOG(LogGenesisEditor,Display,"Material editor offscreen screenshot {} -> {}",Good?"saved":"FAILED",Path);
		return;
	}
	const HWND Hwnd = static_cast<HWND>(Target->GetNativeWindow()->GetNativeHandle());
	SetWindowPos(Hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	for (int32 I = 0; I < 3; ++I)
	{
		Target->Invalidate(EInvalidateWidgetReason::Paint);
		App->Tick();
	}
	// The capture copies the screen, and the compositor can be a few frames
	// behind a fast backend (Release Vulkan was caught showing an earlier
	// step): wait until it has composed the frames just presented.
	for (int32 I = 0; I < 3; ++I)
	{
		DwmFlush();
	}
	const FString Path = SelfTestDir + FString::Format("{}_{}.png", ToString(Backend), Name);
	const bool bOk = CaptureWindowClientToPng(Hwnd, Path);
	SetWindowPos(Hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	UE_LOG(LogGenesisEditor, Display, "Screenshot {} -> {} ({}x{} px, DPI scale {:.2f})", bOk ? "saved" : "FAILED", Path,
		Target->GetPixelSize().X, Target->GetPixelSize().Y, Target->GetNativeWindow()->GetDpiScale());
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

bool FMapEditorApp::PointAtText(std::string_view Text, bool bClick, TSharedPtr<SWindow> Target)
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
	const auto Window = Target ? Target : MainWindow;
	Find(*Window);
	if (!Found)
	{
		UE_LOG(LogGenesisEditor, Warning, "Self-test: no \"{}\" to point at", Text);
		return false;
	}
	const TSharedRef<IPlatformWindow> Native = ToSharedRef(Window->GetNativeWindow());
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
	UE_LOG(LogGenesisEditor, Display, "Simulating WM_DPICHANGED to {} dpi", Dpi);
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
	static double MaterialWindowOpenedAt=0;
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
		UE_LOG(LogGenesisEditor, Error, "Self-test: no prefab {}", Name);
		return FString();
	};
	// After OpenMap the shell holds another document: look it up each time.
	const auto Report = [this](const char* What)
	{
		FMapDocument* Doc = Shell->GetDocument();
		Vector3d Min, Max;
		const bool bAny = Doc->GetSelectionBounds(Min, Max);
		UE_LOG(LogGenesisEditor, Display, "Self-test {}: {} brushes, {} entities, {} selected{}", What, Doc->CountBrushes(), Doc->CountEntities(),
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
		break;

	// Static mesh actors: the five shapes in a row behind the scene, one
	// turned and stretched.
	case 121:
	{
		float X = -320.0f;
		Object* Last = nullptr;
		for (const FMapPlaceable& Item : Shell->GetPlaceables())
		{
			if (Item.Kind == EMapPlaceableKind::Mesh)
			{
				Last = Shell->Place(Item, Vector3d(X, 352.0f, 64.0f));
				X += 160.0f;
			}
		}
		if (Last)
		{
			Doc->SetMeshRotation(*Last, Vector3d(0.0f, 0.0f, 30.0f));
		}
		Doc->ClearSelection();
		Commands.ExecuteAction(*Shell->Cmd.FrameSelection);
		Report(Doc->CountMeshes() == 5 ? "five static mesh actors" : "FAILED: static mesh actors");
		break;
	}
	case 123: Capture("meshes"); break;

	// Milestone 6: a room hollowed from a big brush, with a doorway
	// subtracted from a wall, furnished, then edited with the tools.
	case 125:
	{
		Shell->NewMap(FMapEditorEnvironment::GetBuiltInGameSet());
		FMapDocument* Map = Shell->GetDocument();
		Object* Room = Map->AddPrefab(Prefab("Cube"), Vector3d(0.0f, 0.0f, 128.0f));
		Map->ScaleObjects({ Room }, Vector3d(8.0f, 8.0f, 4.0f), Vector3d(0.0f, 0.0f, 128.0f));
		Map->HollowObjects({ Room }, 16.0f, true);
		Object* Door = Map->AddPrefab(Prefab("Cube"), Vector3d(248.0f, 0.0f, 64.0f));
		Map->ScaleObjects({ Door }, Vector3d(1.0f, 1.5f, 1.5f), Vector3d(248.0f, 0.0f, 64.0f));
		Map->SetSelection({ Door });
		Map->SubtractSelection();
		Map->DeleteObjects({ Door });
		TestColumn = Map->AddPrefab(Prefab("Column"), Vector3d(-128.0f, 96.0f, 80.0f));
		TestSphere = Map->AddPrefab(Prefab("Sphere"), Vector3d(112.0f, -112.0f, 80.0f));
		TestPyramid = Map->AddPrefab(Prefab("Pyramid"), Vector3d(96.0f, 128.0f, 48.0f));
		Map->AddEntity("info_player_start", Vector3d(-160.0f, -160.0f, 48.0f));
		Map->AddEntity("light", Vector3d(0.0f, 0.0f, 200.0f));
		Map->ClearSelection();
		Commands.ExecuteAction(*Shell->Cmd.FrameSelection);
		Report("room built");
		break;
	}
	case 140: Capture("room"); break;

	// The properties of a light: typed keys from the entity set.
	case 145:
		Doc->SetSelection({ FindByClass("light") });
		Doc->SetEntityKey(Doc->GetSelection(), "style", "3");
		Doc->SetEntityKey(Doc->GetSelection(), "light", "450");
		break;
	case 160: Capture("properties_light"); break;
	case 165: PointAtText("Object", true); break;
	case 175: Capture("menu_object"); break;
	case 180: App->DismissAllMenus(); break;

	// Rotate the pyramid 45 degrees by dragging in the top view.
	case 185:
	{
		Doc->SetSelection({ TestPyramid });
		Shell->SetTool(EMapTool::Rotate);
		const FVector2 From = ViewPointToWindow(EMapViewType::Top, Vector3d(176.0f, 128.0f, 48.0f));
		const FVector2 To = ViewPointToWindow(EMapViewType::Top, Vector3d(152.0f, 184.0f, 48.0f));
		Drag(From, To);
		Report("rotated the pyramid");
		break;
	}
	case 195: Capture("rotated"); break;

	// Clip the sphere along a vertical line in the front view.
	case 200:
	{
		Doc->SetSelection({ TestSphere });
		Shell->SetTool(EMapTool::Clip);
		const FVector2 From = ViewPointToWindow(EMapViewType::Front, Vector3d(112.0f, 0.0f, 192.0f));
		const FVector2 To = ViewPointToWindow(EMapViewType::Front, Vector3d(112.0f, 0.0f, -32.0f));
		Drag(From, To);
		break;
	}
	case 205: Capture("clip_line"); break;
	case 207:
		App->OnKeyDown(EKey::Enter, 13, false);
		App->OnKeyUp(EKey::Enter, 13, false);
		Report("clipped the sphere");
		break;
	case 215: Capture("clipped"); break;

	// The vertex dots of the column.
	case 220:
		Doc->SetSelection({ TestColumn });
		Shell->SetTool(EMapTool::Vertex);
		break;
	case 230: Capture("vertices"); break;

	// Group the column and the pyramid and edit inside the group.
	case 235:
	{
		Shell->SetTool(EMapTool::Select);
		Object* Group = Doc->GroupObjects({ TestColumn, TestPyramid });
		if (Group)
		{
			Doc->SetObjectName(*Group, "Furniture");
			Doc->SetScope(*Group);
			Doc->SetSelection({ TestColumn });
		}
		Report("inside the group");
		break;
	}
	case 245: Capture("scope"); break;
	// The editor shell: Brush Editing mode, a second content browser, the
	// Output Log and the preferences window.
	case 250:
		Doc->ScopeUp();
		Shell->SetMode(EMapEditorMode::BrushEditing);
		Shell->GetTabManager().TryInvokeTab("ContentBrowserTab2");
		break;
	case 265:
		Capture("brush_editing");
		Report(Shell->GetContentBrowser(1) && Shell->GetViewSettings().Tool == EMapTool::Vertex ? "brush editing mode, content browser 2 open" : "FAILED: brush editing mode or content browser 2");
		break;
	case 268:
		Shell->SetMode(EMapEditorMode::Selection);
		Shell->GetTabManager().TryInvokeTab("OutputLog");
		Shell->OpenPreferences();
		break;
	case 272:
		Report(Shell->GetPreferencesWindow() && Shell->GetViewSettings().Tool == EMapTool::Select ? "preferences open, selection mode" : "FAILED: preferences or selection mode");
		Capture("output_log");
		if (const TSharedPtr<SWindow> Preferences = Shell->GetPreferencesWindow())
		{
			Capture("preferences", Preferences);
			App->RequestDestroyWindow(ToSharedRef(Preferences));
		}
		Shell->GetTabManager().TryInvokeTab("ContentBrowserTab1");
		break;

	// Save, export and read both back.
	case 275:
		Shell->SaveMap(QlePath);
		Shell->ExportMap(MapPath);
		break;
	case 280:
		Shell->OpenMap(QlePath);
		Report("reopened .qle");
		break;
	case 290: Capture("reopened"); break;
	case 295:
		Shell->OpenMap(MapPath);
		Report("imported .map");
		break;
	case 305: Capture("imported"); break;

	// Landscape mode: a new landscape, sculpted into a hill and a valley,
	// with the brush ring under the mouse in the perspective view.
	case 306:
	{
		Shell->NewMap(FMapEditorEnvironment::GetBuiltInGameSet());
		Shell->SetMode(EMapEditorMode::Landscape);
		FMapLandscapeMode& Land = Shell->GetLandscapeMode();
		Land.NewQuadsX = 48;
		Land.NewQuadsY = 48;
		Land.NewQuadSize = 32.0f;
		Object* Terrain = Land.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		if (Terrain)
		{
			Land.Brush.Radius = 384.0f;
			Land.Brush.Strength = 1.0f;
			Land.Tool = ELandscapeTool::Sculpt;
			const auto Stroke = [&Land, Terrain](const Vector3d& At, float Seconds, bool bLower)
			{
				Land.BeginStroke(*Terrain, At, bLower);
				for (float T = 0.0f; T < Seconds; T += 1.0f / 30.0f)
				{
					Land.ApplyAt(*Terrain, At, 1.0f / 30.0f, bLower);
				}
				Land.EndStroke();
			};
			Land.Brush.Falloff = 0.8f;
			Stroke(Vector3d(-160.0f, 128.0f, 0.0f), 0.8f, false);
			Stroke(Vector3d(256.0f, -224.0f, 0.0f), 0.5f, false);
			Stroke(Vector3d(160.0f, 256.0f, 0.0f), 0.4f, true);
			Land.Tool = ELandscapeTool::Smooth;
			Stroke(Vector3d(0.0f, 0.0f, 0.0f), 0.3f, false);
			Land.Tool = ELandscapeTool::Noise;
			Land.Brush.Radius = 768.0f;
			Land.Brush.Strength = 0.3f;
			Stroke(Vector3d(0.0f, 0.0f, 0.0f), 0.2f, false);

			// Paint layers: grass all over, rock on the hill, dirt in the valley.
			Land.AddLayer(*Terrain);
			Land.AddLayer(*Terrain);
			Land.AddLayer(*Terrain);
			Land.Tool = ELandscapeTool::Paint;
			Land.Brush.Falloff = 0.6f;
			Land.Brush.Strength = 1.0f;
			Land.SelectedLayer = 0;
			Land.Brush.Radius = 1200.0f;
			Stroke(Vector3d(0.0f, 0.0f, 0.0f), 0.5f, false);
			Land.SelectedLayer = 1;
			Land.Brush.Radius = 320.0f;
			Stroke(Vector3d(-160.0f, 128.0f, 0.0f), 0.3f, false);
			Land.SelectedLayer = 2;
			Land.Brush.Radius = 256.0f;
			Stroke(Vector3d(160.0f, 256.0f, 0.0f), 0.3f, false);
			Land.SelectedLayer = 1;
			Land.Brush.Radius = 256.0f;
		}
		Doc = Shell->GetDocument();
		Doc->ClearSelection();
		Commands.ExecuteAction(*Shell->Cmd.FrameSelection);
		Report(Terrain && Terrain->GetLandscape().GetNumLayers() == 3 && Doc->GetUndoDescription() == "Landscape Paint" ? "landscape sculpted and painted"
			: "FAILED: landscape");
		break;
	}
	case 308:
	{
		const TSharedRef<IPlatformWindow> Native = ToSharedRef(MainWindow->GetNativeWindow());
		App->OnMouseMove(Native, ViewPointToWindow(EMapViewType::Perspective, Vector3d(-160.0f, 128.0f, 120.0f)));
		break;
	}
	case 309:
	{
		Capture("landscape");
		// Its heightmap and Rock weightmap out to files and into a second
		// landscape: the same ground and rock (then undone).
		FMapLandscapeMode& Land = Shell->GetLandscapeMode();
		Object* Terrain = Land.GetTargetLandscape();
		const FString HeightPath = SelfTestDir + "landscape_height.png";
		const FString RockPath = SelfTestDir + "landscape_rock.png";
		FString Error;
		bool bOk = Terrain && Land.ExportHeightmap(*Terrain, HeightPath, Error) && Land.ExportWeightmap(*Terrain, 1, RockPath, Error);
		Object* Copy = bOk ? Land.ImportLandscape(HeightPath, Vector3d(2000.0f, 0.0f, 0.0f), Error) : nullptr;
		bOk = Copy && Land.AddLayer(*Copy, "Rock", FColor(122, 118, 112)) && Land.ImportWeightmap(*Copy, 0, RockPath, Error);
		float HeightError = 0.0f;
		float WeightError = 0.0f;
		if (bOk)
		{
			const Landscape& A = Terrain->GetLandscape();
			const Landscape& B = Copy->GetLandscape();
			bOk = A.GetPointsX() == B.GetPointsX() && A.GetPointsY() == B.GetPointsY();
			for (int32 Y = 0; bOk && Y < A.GetPointsY(); ++Y)
			{
				for (int32 X = 0; X < A.GetPointsX(); ++X)
				{
					HeightError = FMath::Max(HeightError, std::fabs(A.GetHeight(X, Y) - B.GetHeight(X, Y)));
					WeightError = FMath::Max(WeightError, std::fabs(A.GetWeight(1, X, Y) - B.GetWeight(0, X, Y)));
				}
			}
		}
		const FString Outcome = bOk && HeightError < 0.01f && WeightError <= 0.5f / 255.0f + 1e-4f
			? FString::Format("landscape heightmap and weightmap round-trip (height error {:.4f}, weight error {:.4f})", HeightError, WeightError)
			: FString::Format("FAILED: landscape heightmap and weightmap ({})", Error);
		Report(*Outcome);
		for (int32 Step = 0; Step < 3 && Copy; ++Step)
		{
			Shell->GetDocument()->Undo();
		}
		break;
	}

	// Landscape mode as a user drives it, with real input: S (a tool key,
	// and a fly key) keeps the mode; the mode follows the Place Actors and
	// Landscape tabs; a held left button paints, and never box-selects.
	case 310:
		App->OnKeyDown(EKey::S, 's', false);
		App->OnKeyUp(EKey::S, 's', false);
		// ] grows the brush in Landscape mode, not the grid.
		LandscapeBrushBefore = Shell->GetLandscapeMode().Brush.Radius;
		LandscapeGridBefore = Shell->GetViewSettings().GridSize;
		App->OnKeyDown(EKey::RightBracket, ']', false);
		App->OnKeyUp(EKey::RightBracket, ']', false);
		break;
	case 312:
		Report(Shell->GetMode() == EMapEditorMode::Landscape && Shell->GetViewSettings().Tool == EMapTool::Select ? "landscape mode kept through the S key"
			: "FAILED: the S key left landscape mode");
		Report(std::fabs(Shell->GetLandscapeMode().Brush.Radius - LandscapeBrushBefore * 1.25f) < 0.01f && Shell->GetViewSettings().GridSize == LandscapeGridBefore
			? "] grew the landscape brush" : "FAILED: ] in landscape mode");
		Shell->GetLandscapeMode().Brush.Radius = LandscapeBrushBefore;
		PointAtText("Place Actors", true);
		break;
	case 314:
		Report(Shell->GetMode() == EMapEditorMode::Selection ? "the Place Actors tab is selection mode" : "FAILED: the Place Actors tab");
		PointAtText("Landscape", true);
		break;
	case 316:
	{
		Report(Shell->GetMode() == EMapEditorMode::Landscape ? "the Landscape tab is landscape mode" : "FAILED: the Landscape tab");
		const Object* Terrain = Shell->GetLandscapeMode().GetTargetLandscape();
		Shell->GetLandscapeMode().Tool = ELandscapeTool::Paint;
		Shell->GetLandscapeMode().SelectedLayer = 1; // Rock
		LandscapePaintBefore = 0.0f;
		for (const float Weight : Terrain ? Terrain->GetLandscape().GetLayers()[1].weights : std::vector<float>())
		{
			LandscapePaintBefore += Weight;
		}
		const Vector3d On(-400.0f, 300.0f, Terrain ? Terrain->GetLandscape().SampleHeight(-400.0f, 300.0f) : 0.0f);
		const TSharedRef<IPlatformWindow> Native = ToSharedRef(MainWindow->GetNativeWindow());
		const FVector2 At = ViewPointToWindow(EMapViewType::Perspective, On);
		App->OnMouseMove(Native, At);
		App->OnMouseDown(Native, EMouseButton::Left, At);
		break;
	}
	case 318:
	case 320:
	case 322:
	{
		// Moving while held: the stroke follows.
		const Object* Terrain = Shell->GetLandscapeMode().GetTargetLandscape();
		const float X = -400.0f + static_cast<float>(Frame - 316) * 24.0f;
		const Vector3d On(X, 300.0f, Terrain ? Terrain->GetLandscape().SampleHeight(X, 300.0f) : 0.0f);
		App->OnMouseMove(ToSharedRef(MainWindow->GetNativeWindow()), ViewPointToWindow(EMapViewType::Perspective, On));
		break;
	}
	case 326:
	{
		const TSharedRef<IPlatformWindow> Native = ToSharedRef(MainWindow->GetNativeWindow());
		App->OnMouseUp(Native, EMouseButton::Left, App->GetCursorPosition());
		const Object* Terrain = Shell->GetLandscapeMode().GetTargetLandscape();
		float After = 0.0f;
		for (const float Weight : Terrain ? Terrain->GetLandscape().GetLayers()[1].weights : std::vector<float>())
		{
			After += Weight;
		}
		Doc = Shell->GetDocument();
		const bool bOk = After > LandscapePaintBefore + 1.0f && Doc->GetUndoDescription() == "Landscape Paint" && Doc->GetSelection().empty();
		const FString Outcome = FString::Format("{}landscape painted with the mouse (rock {:.1f} -> {:.1f}, {} selected)", bOk ? "" : "FAILED: ",
			LandscapePaintBefore, After, Doc->GetSelection().size());
		Report(*Outcome);
		break;
	}
	case 327:
	{
		// A ramp dragged out with the mouse, laid on release.
		FMapLandscapeMode& Land = Shell->GetLandscapeMode();
		const Object* Terrain = Land.GetTargetLandscape();
		Land.Tool = ELandscapeTool::Ramp;
		Land.Brush.Radius = 96.0f;
		const auto OnGround = [Terrain](float X, float Y) { return Vector3d(X, Y, Terrain ? Terrain->GetLandscape().SampleHeight(X, Y) : 0.0f); };
		Drag(ViewPointToWindow(EMapViewType::Perspective, OnGround(-500.0f, -400.0f)), ViewPointToWindow(EMapViewType::Perspective, OnGround(-160.0f, 128.0f)));
		Report(Shell->GetDocument()->GetUndoDescription() == "Landscape Ramp" && Shell->GetDocument()->GetSelection().empty()
			? "landscape ramp dragged out with the mouse" : "FAILED: landscape ramp");
		Land.Tool = ELandscapeTool::Flatten;
		Land.bFlattenToTarget = true;
		break;
	}
	case 329:
		Capture("landscape_tools");
		Shell->SetMode(EMapEditorMode::Selection);
		break;

	// DPI changes, as in milestone 2.
	case 330: SimulateDpiChange(1.0f); break;
	case 355: Capture("dpi100"); break;
	case 360: SimulateDpiChange(2.0f); break;
	case 385: Capture("dpi200"); break;
	case 390: SimulateDpiChange(static_cast<float>(GetDpiForWindow(static_cast<HWND>(MainWindow->GetNativeWindow()->GetNativeHandle()))) / 96.0f); break;
	// A big landscape (256 x 256 quads, 8192 units) framed whole: far
	// components are drawn coarser.
	case 392:
	{
		Shell->NewMap(FMapEditorEnvironment::GetBuiltInGameSet());
		Shell->SetMode(EMapEditorMode::Landscape);
		constexpr int32 Quads = 256;
		std::vector<float> Heights;
		for (int32 Y = 0; Y <= Quads; ++Y)
		{
			for (int32 X = 0; X <= Quads; ++X)
			{
				const float U = static_cast<float>(X) / Quads, V = static_cast<float>(Y) / Quads;
				Heights.push_back(420.0f * std::sin(U * 6.0f) * std::cos(V * 5.0f) + 160.0f * std::sin(U * 23.0f + V * 17.0f) + 40.0f * std::sin(U * 71.0f) * std::cos(V * 63.0f));
			}
		}
		Doc = Shell->GetDocument();
		Object* Big = Doc->AddLandscape(Landscape::Create(Quads, Quads, 32.0f, Heights), Vector3d(0.0f, 0.0f, 0.0f));
		FMapLandscapeMode& Land = Shell->GetLandscapeMode();
		if (Big)
		{
			Land.AddLayer(*Big);
			Land.AddLayer(*Big);
			Land.ApplyAutoPaint(*Big, 0);
			Land.AutoPaint.MinSlope = 28.0f;
			Land.ApplyAutoPaint(*Big, 1);
			Land.AutoPaint.MinSlope = 0.0f;
			// A dirt road over the hills: a spline, applied.
			Land.AddLayer(*Big); // Dirt
			Land.SplineLand = nullptr;
			Land.SelectedSpline = Land.SelectedPoint = -1;
			for (const Vector3d& At : { Vector3d(-3600.0f, -2400.0f, 0.0f), Vector3d(-1500.0f, -1200.0f, 0.0f), Vector3d(200.0f, -1500.0f, 0.0f),
				Vector3d(1800.0f, -300.0f, 0.0f), Vector3d(3600.0f, 600.0f, 0.0f) })
			{
				Land.AddSplinePoint(*Big, Vector3d(At.GetX(), At.GetY(), Big->GetLandscape().SampleHeight(At.GetX(), At.GetY()) + 10.0f));
			}
			if (!Big->GetLandscape().GetSplines().empty())
			{
				LandscapeSpline Road = Big->GetLandscape().GetSplines()[0];
				Road.layer = "Dirt";
				Road.mesh = "Road";
				for (LandscapeSplinePoint& Point : Road.points)
				{
					Point.width = 160.0f;
					Point.falloff = 220.0f;
				}
				Land.SetSpline(*Big, 0, Road);
				Land.ApplySplines(*Big);
				// A fence over the hills beside it (shaping nothing): a point
				// every 150 units on the ground.
				LandscapeSpline Fence;
				Fence.mesh = "Fence";
				Fence.meshFitWidth = false;
				Fence.raiseTerrain = Fence.lowerTerrain = false;
				for (float X = 600.0f; X <= 1800.0f; X += 150.0f)
				{
					const float Y = -620.0f + (X - 600.0f) * 0.35f;
					LandscapeSplinePoint Post;
					Post.position.NewVector(X, Y, Big->GetLandscape().SampleHeight(X, Y));
					Fence.points.push_back(Post);
				}
				std::vector<LandscapeSpline> Splines = Big->GetLandscape().GetSplines();
				Splines.push_back(Fence);
				Doc->SetLandscape(*Big, Big->GetLandscape().WithSplines(std::move(Splines)), "Add Fence");
			}
			// Foliage: trees, pines, bushes and rocks over much of it, grass
			// where the close-up looks (as the Foliage mode paints them).
			FMapFoliageMode& Leaves = Shell->GetFoliageMode();
			for (const char* Name : { "Tree", "Pine", "Bush", "Rock", "Grass" })
			{
				Leaves.AddType(Name);
			}
			const auto Paint = [&Leaves](const Vector3d& At, float Radius, std::initializer_list<int32> Types)
			{
				for (int32 Type = 0; Type < 5; ++Type)
				{
					Leaves.SetActive(Type, std::find(Types.begin(), Types.end(), Type) != Types.end());
				}
				Leaves.Brush.Radius = Radius;
				Leaves.Brush.Strength = 1.0f;
				Leaves.BeginStroke(At, false);
				for (int32 Step = 0; Step < 30; ++Step)
				{
					Leaves.ApplyAt(At, 1.0f / 15.0f, false);
				}
				Leaves.EndStroke();
			};
			Paint(Vector3d(-1500.0f, 1500.0f, 0.0f), 2200.0f, { 0, 2, 3 });
			Paint(Vector3d(1800.0f, -1600.0f, 0.0f), 2000.0f, { 1, 2, 3 });
			Paint(Vector3d(1200.0f, -900.0f, 0.0f), 700.0f, { 4 });
			// The road kept clear (erased along its curve).
			for (int32 Type = 0; Type < 5; ++Type)
			{
				Leaves.SetActive(Type, true);
			}
			if (!Big->GetLandscape().GetSplines().empty())
			{
				std::vector<LandscapeSpline::Sample> Samples;
				Big->GetLandscape().GetSplines()[0].SampleCurve(150.0f, Samples);
				Leaves.Brush.Radius = 260.0f;
				Leaves.BeginStroke(Samples[0].position, true);
				for (const LandscapeSpline::Sample& Sample : Samples)
				{
					for (int32 Step = 0; Step < 8; ++Step)
					{
						Leaves.ApplyAt(Sample.position, 1.0f / 15.0f, true);
					}
				}
				Leaves.EndStroke();
			}
			Leaves.Brush.Radius = 512.0f;
			Doc->SetSelection({ Big });
			Commands.ExecuteAction(*Shell->Cmd.FrameSelection);
			Doc->ClearSelection();
		}
		View::landscapeTrianglesDrawn = 0;
		View::landscapeComponentsDrawn = 0;
		break;
	}
	case 396:
	{
		Capture("landscape_big");
		const FString Outcome = FString::Format("big landscape drawn by levels ({} components, {} triangles over the frames since)",
			View::landscapeComponentsDrawn, View::landscapeTrianglesDrawn);
		Report(*Outcome);
		// Close to the ground: the layers' textures blended per pixel.
		if (Object* Big = Shell->GetLandscapeMode().GetTargetLandscape())
		{
			const Vector3d At(1200.0f, -900.0f, Big->GetLandscape().SampleHeight(1200.0f, -900.0f) + 40.0f);
			if (Object* Marker = Doc->AddMesh("Cube", At))
			{
				Doc->SetSelection({ Marker });
				Commands.ExecuteAction(*Shell->Cmd.FrameSelection);
				Doc->Undo();
			}
		}
		break;
	}
	case 397:
		Shell->SetMode(EMapEditorMode::Foliage);
		break;
	case 399:
	{
		Capture("landscape_close");
		const Object* Leaves = Shell->GetFoliageMode().GetFoliageObject();
		const FString Outcome = Leaves && Leaves->GetFoliage().GetNumInstances() > 100
			? FString::Format("foliage painted: {} instances of {} types", Leaves->GetFoliage().GetNumInstances(), Leaves->GetFoliage().GetNumTypes())
			: FString("FAILED: foliage");
		Report(*Outcome);
		// The Splines tool: a side road branching off the road's fourth
		// point (a junction), which is then picked, its tangent set by hand
		// so its handles show.
		Shell->SetMode(EMapEditorMode::Landscape);
		FMapLandscapeMode& Land = Shell->GetLandscapeMode();
		Land.Tool = ELandscapeTool::Splines;
		if (Object* Big = Land.SplineLand)
		{
			Land.SelectedSpline = Land.SelectedPoint = -1;
			Land.ConnectSplinePoint(*Big, 0, 3);
			for (const Vector3d& At : { Vector3d(2300.0f, -900.0f, 0.0f), Vector3d(2900.0f, -1100.0f, 0.0f) })
			{
				Land.AddSplinePoint(*Big, Vector3d(At.GetX(), At.GetY(), Big->GetLandscape().SampleHeight(At.GetX(), At.GetY()) + 10.0f));
			}
			LandscapeSpline Side = Big->GetLandscape().GetSplines().back();
			Side.layer = "Dirt";
			Side.mesh = "Road";
			for (LandscapeSplinePoint& Point : Side.points)
			{
				Point.width = 140.0f;
				Point.falloff = 200.0f;
			}
			Land.SetSpline(*Big, static_cast<int32>(Big->GetLandscape().GetSplines().size()) - 1, Side);
			Land.ApplySplines(*Big);
			const Vector3d JointAt = Big->GetLandscape().GetSplines()[0].points[3].position;
			Land.SetSplineTangent(*Big, 0, 3, Vector3d(1500.0f, 500.0f, 0.0f));
			Land.SelectedSpline = 0;
			Land.SelectedPoint = 3;
			// Looking at it: framed on a cube there, which then goes.
			const float X = JointAt.GetX(), Y = JointAt.GetY();
			if (Object* Marker = Shell->GetDocument()->AddMesh("Cube", Vector3d(X, Y, Big->GetLandscape().SampleHeight(X, Y) + 60.0f)))
			{
				Shell->GetDocument()->SetSelection({ Marker });
				Commands.ExecuteAction(*Shell->Cmd.FrameSelection);
				Shell->GetDocument()->Undo();
			}
		}
		break;
	}
	case 402:
	{
		const Object* Road = Shell->GetLandscapeMode().SplineLand;
		Capture("landscape_splines");
		Report(Road && !Road->GetLandscape().GetSplines().empty() ? "landscape spline road applied" : "FAILED: landscape spline");
		const bool bJunction = Road && Road->GetLandscape().GetSplines().size() == 3
			&& Road->GetLandscape().GetSplines()[0].points[3].joint >= 0
			&& Road->GetLandscape().GetSplines()[2].points[0].joint == Road->GetLandscape().GetSplines()[0].points[3].joint
			&& !Road->GetLandscape().GetSplines()[0].points[3].autoTangent;
		Report(bJunction ? "landscape spline junction and tangent" : "FAILED: landscape spline junction");
		Shell->SetMode(EMapEditorMode::Selection);
		break;
	}
	case 403:
	{
		const auto RootId = Shell->GetDocument()->GetRoot().GetIdentity().Actor;
		TArray<FString> ActorIds;
		for (const auto* Obj : Shell->GetDocument()->GetObjects()) ActorIds.push_back(Obj->GetIdentity().Actor);
		FString Native = SelfTestDir + "landscape_native.gmap";
		if (const auto* Project = FProjectContext::GetActive())
		{
			FString Error;
			Project->ResolvePackage("/Game/Maps/RegistryWorld", true, Native, Error);
		}
		const bool Saved = Shell->SaveMap(Native);
		const bool Opened = Saved && Shell->OpenMap(Native);
		Report(Opened ? "native world package saved and reopened" : "FAILED: native world package");
		TArray<FString> ReloadedIds;
		for (const auto* Obj : Shell->GetDocument()->GetObjects()) ReloadedIds.push_back(Obj->GetIdentity().Actor);
		Report(Opened && RootId == Shell->GetDocument()->GetRoot().GetIdentity().Actor && ActorIds == ReloadedIds
			? "runtime scene actor identities preserved" : "FAILED: runtime scene actor identities");
		break;
	}
	case 404:
	{
		const auto* Project = FProjectContext::GetActive();
		if (!Project) break;
		FString File, Error;
		Project->ResolvePackage("/Game/Prefabs/RegistryPrefab", false, File, Error);
		Object* Marker = Doc->AddMesh("Cube", Vector3d());
		const bool Saved = Marker && Doc->SavePrefab({ Marker }, File, Error);
		if (Marker) Doc->Undo();
		Shell->RefreshAssetRegistry();
		const auto* Registry = Shell->GetAssetRegistry();
		const auto* Asset = Registry->FindByPath("/Game/Prefabs/RegistryPrefab");
		FAssetPackage Loaded;
		const bool Resolved = Asset && Registry->Load(Asset->GetReference(), Loaded, Error, "Genesis.Prefab");
		Report(Saved && Resolved ? "asset registry discovered and resolved prefab" : "FAILED: asset registry prefab");
		Shell->GetTabManager().TryInvokeTab("ContentBrowserTab1");
		if (const auto Browser = Shell->GetContentBrowser(0))
		{
			Browser->SetPath(Project->GetContentDir());
			Browser->SetSearchText("Registry");
		}
		break;
	}
	case 405:
	{
		const auto* Project=FProjectContext::GetActive(); if(!Project) break;
		FString TextureFile,MaterialFile,MeshFile,Error;
		Project->ResolvePackage("/Game/RegistryTexture",false,TextureFile,Error);
		Project->ResolvePackage("/Game/RegistryMaterial",false,MaterialFile,Error);
		Project->ResolvePackage("/Game/RegistryMesh",false,MeshFile,Error);
		FTextureAsset Texture; Texture.Width=Texture.Height=2; Texture.Pixels={80,140,40,255,120,160,60,255,60,100,30,255,180,170,90,255}; FAssetPackage T,M,S;
		bool Good=FRenderAssetCodec::EncodeTexture(Texture,T,Error) && FAssetPackageStore::Save(TextureFile,T,Error);
		FMaterialAsset Material; Material.BaseColor[0]=0.5f; Material.BaseColor[1]=0.8f; Material.BaseColorTexture={T.Id,"/Game/RegistryTexture"};
		Good=Good && FRenderAssetCodec::EncodeMaterial(Material,M,Error) && FAssetPackageStore::Save(MaterialFile,M,Error);
		Good=Good && FRenderAssetCodec::EncodeMesh({StaticMesh::Find("Cube"),{M.Id,"/Game/RegistryMaterial"}},S,Error) && FAssetPackageStore::Save(MeshFile,S,Error);
		Shell->RefreshAssetRegistry();
		Object* Mesh=Good?Doc->AddMeshAsset({S.Id,"/Game/RegistryMesh"},Vector3d(0,0,300),Error):nullptr;
		Good=Good && Mesh && Mesh->GetMeshAsset()->Texture.has_value();
		if(auto* Terrain=Shell->GetLandscapeMode().GetTargetLandscape()) Good=Good && Shell->GetLandscapeMode().SetLayerTexture(*Terrain,0,FMapAssetLibrary::TextureToken({T.Id,"/Game/RegistryTexture"}));
		const FString NativePath=Doc->GetPath();
		if(Good) Good=Shell->SaveMap(NativePath) && Shell->OpenMap(NativePath);
		if(!Good) UE_LOG(LogGenesisEditor,Error,"{}",Error);
		Report(Good?"native mesh material texture dependencies saved and reopened":"FAILED: native render assets");
		break;
	}
	case 406:
	{
		const auto* Project=FProjectContext::GetActive(); if(!Project) break;
		FString Error, File; FTextureAsset Pattern, Normals;
		Pattern.Width=Pattern.Height=Normals.Width=Normals.Height=64;
		Pattern.Pixels.resize(64*64*4); Normals.Pixels.resize(64*64*4); Normals.bSRGB=false;
		for(int Y=0; Y<64; ++Y) for(int X=0; X<64; ++X) {
			const size_t I=(Y*64+X)*4; const bool Checker=((X/8+Y/8)&1)!=0;
			Pattern.Pixels[I]=Checker?210:35; Pattern.Pixels[I+1]=Checker?180:65; Pattern.Pixels[I+2]=Checker?75:155; Pattern.Pixels[I+3]=Checker?255:0;
			const float NX=0.55f*std::sin(X*6.2831853f/16), NY=0.35f*std::cos(Y*6.2831853f/16), NZ=std::sqrt(1-NX*NX-NY*NY);
			Normals.Pixels[I]=static_cast<uint8>((NX*0.5f+0.5f)*255); Normals.Pixels[I+1]=static_cast<uint8>((NY*0.5f+0.5f)*255); Normals.Pixels[I+2]=static_cast<uint8>((NZ*0.5f+0.5f)*255); Normals.Pixels[I+3]=255;
		}
		FAssetPackage Color,Normal; Project->ResolvePackage("/Game/MaterialPattern",false,File,Error);
		bool Good=FRenderAssetCodec::EncodeTexture(Pattern,Color,Error) && FAssetPackageStore::Save(File,Color,Error);
		Project->ResolvePackage("/Game/MaterialNormal",false,File,Error);
		Good=Good && FRenderAssetCodec::EncodeTexture(Normals,Normal,Error) && FAssetPackageStore::Save(File,Normal,Error);
		TArray<FAssetReference> PreviewMaterials;
		for (int I = 0; I < 5 && Good; ++I)
		{
			FMaterialAsset Material;
			Material.Roughness = I == 0 ? 0.10f : I == 1 ? 0.85f : 0.4f;
			Material.Metallic = I >= 2 ? 0.0f : 1.0f;
			if (I < 2)
			{
				Material.BaseColor[0] = 0.95f;
				Material.BaseColor[1] = 0.67f;
				Material.BaseColor[2] = 0.27f;
			}
			else
			{
				Material.BaseColorTexture = {Color.Id, "/Game/MaterialPattern"};
				Material.NormalTexture = {Normal.Id, "/Game/MaterialNormal"};
			}
			if (I == 3)
			{
				Material.BlendMode = EMaterialBlendMode::Masked;
				Material.NormalTexture = {};
			}
			if (I == 4)
			{
				Material.BlendMode = EMaterialBlendMode::Translucent;
				Material.BaseColor[3] = 0.4f;
				Material.NormalTexture = {};
			}
			const auto MaterialPath = FString::Format("/Game/PreviewMaterial{}", I),
					   MeshPath = FString::Format("/Game/PreviewSphere{}", I);
			FAssetPackage M, S;
			Project->ResolvePackage(MaterialPath, false, File, Error);
			Good = FRenderAssetCodec::EncodeMaterial(Material, M, Error) && FAssetPackageStore::Save(File, M, Error);
			PreviewMaterials.push_back({M.Id, MaterialPath});
			Project->ResolvePackage(MeshPath, false, File, Error);
			Good = Good &&
				   FRenderAssetCodec::EncodeMesh({StaticMesh::Find("Sphere"), {M.Id, MaterialPath}}, S, Error) &&
				   FAssetPackageStore::Save(File, S, Error);
			Shell->RefreshAssetRegistry();
			if (auto* Obj = Good ? Doc->AddMeshAsset({S.Id, MeshPath}, Vector3d(30000 + (I - 2) * 180.0f, 0, 80), Error)
								 : nullptr)
				Obj->SetObjName(I == 0	 ? "Smooth Metal"
								: I == 1 ? "Rough Metal"
								: I == 2 ? "Textured Normal"
								: I == 3 ? "Masked Material"
										 : "Translucent Material");
			else
				Good = false;
		}
		if (Good && PreviewMaterials.size() == 5)
		{
			const auto Sphere = StaticMesh::Find("Sphere");
			auto Multi = std::make_shared<StaticMesh>("Slotted Sphere");
			for (int I = 0; I < Sphere->GetNumVertices(); ++I)
				Multi->AddVertex(Sphere->GetVertex(I));
			for (const auto& T : Sphere->GetTriangles())
			{
				Multi->AddTriangle(T.v[0], T.v[1], T.v[2], T.norm[0], T.norm[1], T.norm[2]);
				const int I = static_cast<int>(Multi->GetTriangles().size()) - 1;
				Multi->SetTriangleUVs(I, T.uv);
				const float X = (Sphere->GetVertex(T.v[0]).GetX() + Sphere->GetVertex(T.v[1]).GetX() +
								 Sphere->GetVertex(T.v[2]).GetX()) /
								3;
				Multi->SetTriangleMaterial(I, X < -20 ? 0 : X > 20 ? 1 : 2);
			}
			Multi->Finish();
			FStaticMeshAsset Slotted{
				Multi, PreviewMaterials[0], {PreviewMaterials[2], PreviewMaterials[3]}, {"Metal", "Paint", "Cutout"}};
			FAssetPackage P;
			Project->ResolvePackage("/Game/PreviewSlots", false, File, Error);
			Good = FRenderAssetCodec::EncodeMesh(Slotted, P, Error) && FAssetPackageStore::Save(File, P, Error);
			Shell->RefreshAssetRegistry();
			if (auto* Obj =
					Good ? Doc->AddMeshAsset({P.Id, "/Game/PreviewSlots"}, Vector3d(30540, 0, 80), Error) : nullptr)
				Obj->SetObjName("Material Slots");
			else
				Good = false;
		}
		const FString Native = Doc->GetPath();
		Good = Good && Shell->SaveMap(Native) && Shell->OpenMap(Native);
		Report(Good ? "PBR normal maps and material slots saved and reopened" : "FAILED: material slot assets");
		break;
	}
	case 407:
		if (FProjectContext::GetActive())
			Capture("content_registry");
		break;
	case 408:
		if (const auto Browser = Shell->GetContentBrowser(0))
		{
			for (const auto& Item : Browser->GetItems())
				if (Item.Type == EContentAssetType::Material)
				{
					Browser->Activate(Item);
					MaterialWindowOpenedAt = FPlatformTime::Seconds();
					break;
				}
		}
		break;
	case 412:
		// Let the OS finish its native window fade-in before capturing pixels.
		if (MaterialWindowOpenedAt > 0 && FPlatformTime::Seconds() - MaterialWindowOpenedAt < 0.5)
		{
			--SelfTestFrame;
			break;
		}
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().View().substr(0,9)=="Material:") Capture("material_asset",Window.ToSharedPtr());
		break;
	case 414:
	{
		TArray<TSharedRef<SWindow>> Close;
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().View().substr(0,9)=="Material:") Close.push_back(Window);
		for(const auto& Window:Close) App->RequestDestroyWindow(Window);
		break;
	}
	case 416:
	{
		TArray<Object*> Samples;
		for(auto* Obj:Doc->GetObjects()) if(!strcmp(Obj->GetObjName(),"Smooth Metal") || !strcmp(Obj->GetObjName(),"Rough Metal") || !strcmp(Obj->GetObjName(),"Textured Normal") || !strcmp(Obj->GetObjName(),"Masked Material") || !strcmp(Obj->GetObjName(),"Translucent Material") || !strcmp(Obj->GetObjName(),"Material Slots")) Samples.push_back(Obj);
		if(Samples.size()==6) { Doc->SetSelection(Samples); Commands.ExecuteAction(*Shell->Cmd.FrameSelection); Doc->ClearSelection(); }
		Report(Samples.size()==6?"material preview scene framed":"FAILED: material preview scene");
		break;
	}
	case 418: if(FProjectContext::GetActive() && Shell->GetViewportLayout()) Shell->GetViewportLayout()->ToggleMaximize(1); break;
	case 420: if(FProjectContext::GetActive()) Capture("mesh_materials"); break;
	case 422:
		for(auto* Obj:Doc->GetObjects()) if(!strcmp(Obj->GetObjName(),"Material Slots")) { Doc->SetSelection({Obj}); Commands.ExecuteAction(*Shell->Cmd.FrameSelection); break; }
		break;
	case 424: if(FProjectContext::GetActive()) Capture("mesh_slots"); break;
	case 426:
		if(const auto Registry=FMapAssetLibrary::GetRegistry()) if(const auto* Asset=Registry->FindByPath("/Game/PreviewSlots")) if(const auto Browser=Shell->GetContentBrowser(0)) {
			FContentBrowserItem Item; Item.Name="PreviewSlots"; Item.Type=EContentAssetType::StaticMesh; Item.PackageId=Asset->Metadata.Id; Item.PackagePath=Asset->PackagePath; Item.Path=Asset->FilePath; Item.Schema=Asset->Metadata.Schema;
			Browser->Activate(Item); MaterialWindowOpenedAt=FPlatformTime::Seconds();
		}
		break;
	case 428:
		if(MaterialWindowOpenedAt>0 && FPlatformTime::Seconds()-MaterialWindowOpenedAt<0.5) { --SelfTestFrame; break; }
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Static Mesh:")) Capture("mesh_asset_editor",Window.ToSharedPtr());
		break;
	case 429:
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Static Mesh:")) PointAtText("Save Mesh Asset",true,Window.ToSharedPtr());
		break;
	case 430:
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Static Mesh:")) Capture("mesh_asset_saved",Window.ToSharedPtr());
		break;
	case 432: {
		TArray<TSharedRef<SWindow>> Close; for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Static Mesh:")) Close.push_back(Window);
		for(const auto& Window:Close) App->RequestDestroyWindow(Window);
		break;
	}
	case 434:
		if(const auto Registry=FMapAssetLibrary::GetRegistry()) if(const auto* Asset=Registry->FindByPath("/Game/MaterialPattern")) if(const auto Browser=Shell->GetContentBrowser(0)) {
			FContentBrowserItem Item; Item.Name="MaterialPattern"; Item.Type=EContentAssetType::Texture; Item.PackageId=Asset->Metadata.Id; Item.PackagePath=Asset->PackagePath; Item.Path=Asset->FilePath; Item.Schema=Asset->Metadata.Schema;
			Browser->Activate(Item); MaterialWindowOpenedAt=FPlatformTime::Seconds();
		}
		break;
	case 436:
		if(MaterialWindowOpenedAt>0 && FPlatformTime::Seconds()-MaterialWindowOpenedAt<0.5) { --SelfTestFrame; break; }
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Texture:")) Capture("texture_asset_editor",Window.ToSharedPtr());
		break;
	case 437:
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Texture:")) PointAtText("sRGB Color Texture",true,Window.ToSharedPtr());
		break;
	case 438:
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Texture:")) PointAtText("Save Texture Asset",true,Window.ToSharedPtr());
		break;
	case 440:
		for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Texture:")) Capture("texture_asset_saved",Window.ToSharedPtr());
		break;
	case 442: {
		TArray<TSharedRef<SWindow>> Close; for(const auto& Window:App->GetWindows()) if(Window->GetTitle().StartsWith("Texture:")) Close.push_back(Window);
		for(const auto& Window:Close) App->RequestDestroyWindow(Window); break;
	}

    case 444:
    {
        const auto Project=FProjectContext::GetActive();const auto Registry=FMapAssetLibrary::GetRegistry();
        if(!Project||!Registry)break;FString Error,File;FMaterialAsset Material;Material.Graph=FMaterialGraph{};auto& G=*Material.Graph;
        const auto Texture=G.Add(EMaterialExpression::TextureSampleParameter2D);G.Find(Texture)->ParameterName="Albedo";
        if(const auto* Asset=Registry->FindByPath("/Game/MaterialPattern"))G.Find(Texture)->Texture=Asset->GetReference();
        const auto Tint=G.Add(EMaterialExpression::VectorParameter);G.Find(Tint)->ParameterName="Tint";for(float& V:G.Find(Tint)->Value)V=1;
        const auto Product=G.Add(EMaterialExpression::Multiply);G.Find(Product)->Inputs[0]=Texture;G.Find(Product)->Inputs[1]=Tint;
        const auto RGB=G.Add(EMaterialExpression::ComponentMask);G.Find(RGB)->Inputs[0]=Product;G.Outputs[0]=RGB;
        const auto Roughness=G.Add(EMaterialExpression::ScalarParameter);G.Find(Roughness)->ParameterName="Roughness";G.Find(Roughness)->Value[0]=.5f;G.Outputs[1]=Roughness;
        FAssetPackage Base,InstancePackage;FMaterialInstanceAsset Instance;
        bool Good=FMaterialEditingLibrary::CompileMaterial(Material,Error)&&Project->ResolvePackage("/Game/InstanceQABase",false,File,Error)&&FRenderAssetCodec::EncodeMaterial(Material,Base,Error)&&FAssetPackageStore::Save(File,Base,Error);
        Instance.Parent={Base.Id,"/Game/InstanceQABase"};Instance.Parameters={{"Tint",4,{.1f,.7f,1.f,1.f}},{"Roughness",1,{.2f,0,0,0}}};
        Good=Good&&Project->ResolvePackage("/Game/InstanceQA",false,File,Error)&&FRenderAssetCodec::EncodeMaterialInstance(Instance,InstancePackage,Error)&&FAssetPackageStore::Save(File,InstancePackage,Error);
        if(Good){
            Shell->RefreshAssetRegistry();FMaterialEditorArgs Args;Args.Registry=Registry;Args.Material={InstancePackage.Id,"/Game/InstanceQA"};Args.Name="Instance QA";Args.Device=Device.get();Args.Renderer=Renderer.get();Args.WindowRenderer=WindowRenderer.get();
            Good=FMaterialEditor::Open(Args,Error)!=nullptr;MaterialWindowOpenedAt=FPlatformTime::Seconds();
        }
        const FString Result=Good?FString("material instance editor opened with inherited texture and scalar/vector overrides"):"FAILED: material instance editor "+Error;Report(*Result);break;
    }
    case 446:
        if(MaterialWindowOpenedAt>0&&FPlatformTime::Seconds()-MaterialWindowOpenedAt<.5){--SelfTestFrame;break;}
        for(const auto& Window:App->GetWindows())if(Window->GetTitle().StartsWith("Material Instance:"))Capture("material_instance",Window.ToSharedPtr());break;
    case 448:
		App->RequestQuit();
		break;
	default:
		break;
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	// An exception nothing caught: say what it was before the process ends.
	std::set_terminate([] {
		FString What = "unknown";
		try
		{
			if (const std::exception_ptr Current = std::current_exception())
			{
				std::rethrow_exception(Current);
			}
		}
		catch (const std::exception& Exception)
		{
			What = Exception.what();
		}
		catch (...)
		{
		}
		UE_LOG(LogGenesisEditor, Error, "Unhandled exception: {}", What);
		std::abort();
	});
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
