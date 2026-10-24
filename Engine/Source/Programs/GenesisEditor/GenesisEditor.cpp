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
//
// Settings (preferences, grid, tools, the last map, layout, key bindings)
// and the prefabs you save are kept in %LOCALAPPDATA%\Genesis\Editor.

#include "Framework/Application/SlateApplication.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
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
#include "GenesisEditorApp.h"
#include "RHI.h"
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
#include <objbase.h>
#include <crtdbg.h>

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>

DEFINE_LOG_CATEGORY(LogGenesisEditor, Log);

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
} // namespace

// ---------------------------------------------------------------------------

bool FMapEditorApp::Initialize(const TArray<FString>& Args)
{
	FTextLocalization::Reset();
	if (const TOptional<FString> Catalog = GetOption(Args, "-localizationcatalog="))
	{
		FString Error;
		if (!FTextLocalization::LoadCatalog(*Catalog, Error))
		{
			GENESIS_LOG(LogGenesisEditor, Error, "{}", Error);
			return false;
		}
	}
	FApp::SetName("Genesis Editor");
	// Resolve a project before settings, windows or content browsers are built.
	TOptional<FString> ProjectPath = GetOption(Args, "-project=");
	const TOptional<FString> NewProject = GetOption(Args, "-newproject=");
	for (SIZE_T Index = 1; Index < Args.size(); ++Index)
		if (!Args[Index].StartsWith("-") && Args[Index].EndsWith(".gproject", ESearchCase::IgnoreCase))
			ProjectPath = Args[Index];
	if (NewProject && ProjectPath)
	{
		MessageBoxW(nullptr, GENESIS_TEXT("Use either -project or -newproject.").ToWide().c_str(),
		    GENESIS_TEXT("Genesis Project").ToWide().c_str(), MB_OK | MB_ICONERROR);
		return false;
	}
	if (ProjectPath || NewProject)
	{
		FString Error;
		const FString Path = NewProject ? *NewProject : *ProjectPath;
		TOptional<FProjectContext> Project = NewProject
		    ? FProjectContext::Create(
		          Path, FString::FromWide(std::filesystem::path(Path.ToWide()).stem().wstring()), Error)
		    : FProjectContext::Open(Path, Error);
		if (!Project)
		{
			MessageBoxW(nullptr, Error.ToWide().c_str(), GENESIS_TEXT("Genesis Project").ToWide().c_str(),
			    MB_OK | MB_ICONERROR);
			return false;
		}
		FProjectContext::SetActive(std::move(Project));
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
	OnInitialize(Args);

	// Settings. Scripted runs start from the defaults and keep nothing.
	if (bInteractive)
	{
		if (const FProjectContext* Project = FProjectContext::GetActive())
		{
			const std::filesystem::path Dir = std::filesystem::path(Project->GetSavedDir().ToWide()) / "Editor";
			std::error_code Error;
			std::filesystem::create_directories(Dir, Error);
			if (Error)
				return false;
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
		TUniquePtr<FIniConfigStore> Ini = MakeUnique<FIniConfigStore>(SettingsDir + "Settings.ini");
		Ini->Load();
		Config = std::move(Ini);
	}
	else
	{
		Config = MakeUnique<FMemoryConfigStore>();
	}
	FServices::SetConfig(Config.get());
	if (bInteractive)
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
	const FMapEditorShell::FStartupGraphics Graphics =
	    bInteractive ? FMapEditorShell::GetStartupGraphics() : FMapEditorShell::FStartupGraphics();
	if (!bBackendFromCommandLine && Graphics.Backend)
	{
		Backend = *Graphics.Backend;
	}
	DeviceDesc.Backend = Backend;
	if (Graphics.DebugLayer)
		DeviceDesc.bDebugLayer = *Graphics.DebugLayer;
	if (HasFlag(Args, "-debug"))
		DeviceDesc.bDebugLayer = true;
	if (HasFlag(Args, "-nodebug"))
		DeviceDesc.bDebugLayer = false;
	Device = FRHIDevice::Create(DeviceDesc);
	if (!Device)
	{
		const FString Message =
		    FTextLocalization::Format("Could not create a {} device. See the log for details.", ToString(Backend));
		GENESIS_LOG(LogGenesisEditor, Error, "{}", Message);
		if (bInteractive)
		{
			MessageBoxW(
			    nullptr, Message.ToWide().c_str(), GENESIS_TEXT("Genesis Editor").ToWide().c_str(), MB_ICONERROR);
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
		GENESIS_LOG(LogGenesisEditor, Error, "UI renderer initialization failed");
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
	                         ? FProjectContext::GetActive()->GetDescriptor().Name + " - Genesis Editor"
	                         : FString("Genesis Editor"))
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
		if (const FProjectContext* Project = FProjectContext::GetActive();
		    Project && !Project->GetDescriptor().DefaultMap.IsEmpty())
		{
			FString File, Error;
			if (Project->ResolvePackage(Project->GetDescriptor().DefaultMap, true, File, Error))
			{
				// A world composition opens its region editor; a level opens in the viewports.
				FAssetPackageMetadata Metadata;
				if (FAssetPackageStore::ReadMetadata(File, Metadata, Error) &&
				    Metadata.Schema == "Genesis.WorldComposition")
					Shell->OpenWorldComposition({Metadata.Id, Project->GetDescriptor().DefaultMap},
					    FString::FromWide(std::filesystem::path(File.ToWide()).stem().wstring()));
				else
				{
					Shell->OpenMap(File);
					bOpened = true;
				}
			}
		}
	}
	if (!bOpened && !GetOption(Args, "-runscript=") && !GetOption(Args, "-scriptmodule=") && bInteractive &&
	    FMapEditorShell::GetStartupMapPreference() == FMapEditorShell::EStartupMap::LastOpened)
	{
		const FString Last = FMapEditorShell::GetLastMap();
		std::error_code Error;
		if (!Last.IsEmpty() && std::filesystem::exists(std::filesystem::path(Last.ToWide()), Error))
		{
			Shell->OpenMap(Last);
		}
	}
	GENESIS_LOG(LogGenesisEditor, Display, "Drawing with {} on {}", ToString(Backend), Device->GetAdapterName());
	return true;
}

void FMapEditorApp::SetAppScale(float Scale)
{
	AppScale = Scale;
	App->SetApplicationScale(Scale);
	GENESIS_LOG(LogGenesisEditor, Display, "App scale {:.2f}", Scale);
}

int32 FMapEditorApp::Run(const FString& CommandLine)
{
	const TArray<FString> Args = SplitCommandLine(CommandLine);
	if (!Initialize(Args))
	{
		Shutdown();
		return 1;
	}
	TOptional<FString> Script = GetOption(Args, "-runscript=");
	FString DefaultScriptFunction = "Main";
	if (const TOptional<FString> Module = GetOption(Args, "-scriptmodule="))
	{
		const FProjectContext* Project = FProjectContext::GetActive();
		FString File, Error;
		if (Script || !Project || !Project->ResolveScriptModule(*Module, true, File, Error))
		{
			GENESIS_LOG(LogGenesisEditor, Error,
			    "Named script module requires an active project and cannot be combined with -runscript: {}", Error);
			Shutdown();
			return 1;
		}
		Script = File;
		for (const FProjectScriptModule& Entry : Project->GetDescriptor().ScriptModules)
			if (Entry.Name == *Module)
				DefaultScriptFunction = Entry.Extensions.Get("EditorFunction").AsString("Main");
	}
	if (Script)
	{
		FEditorScriptRequest Request;
		Request.ModulePath = *Script;
		Request.Language = GetOption(Args, "-scriptlanguage=")
		                       .value_or(Script->EndsWith(".lua", ESearchCase::IgnoreCase) ? FString("Lua")
		                               : Script->EndsWith(".py", ESearchCase::IgnoreCase)  ? FString("Python")
		                                                                                   : FString("DotNet"));
		Request.Function = GetOption(Args, "-scriptfunction=").value_or(DefaultScriptFunction);
		Request.Capabilities.push_back("Editor.Files");
		FString Error;
		FJsonValue Result;
		if (const TOptional<FString> Input = GetOption(Args, "-scriptargs="))
		{
			FString Text;
			if (!FFile::ReadAllText(*Input, Text))
			{
				Shutdown();
				return 1;
			}
			TOptional<FJsonValue> Parsed = FJsonValue::Parse(Text.View(), &Error);
			if (!Parsed)
			{
				GENESIS_LOG(LogGenesisEditor, Error, "Script arguments: {}", Error);
				Shutdown();
				return 1;
			}
			Request.Arguments = std::move(*Parsed);
		}
		TSharedPtr<FEditorScriptContext> Context = std::make_shared<FEditorScriptContext>();
		Context->GetDocument = [this] { return Shell->GetDocument(); };
		Context->Assets = FMapAssetLibrary::GetRegistry();
		Context->GetCommands = [this] { return Shell->GetCommandList(); };
		Request.Capabilities.push_back("Editor.Commands");
		const bool Success = ExecuteEditorScript(Request, Context, Result, Error);
		if (!Success)
			GENESIS_LOG(LogGenesisEditor, Error, "Editor script failed: {}", Error);
		if (const TOptional<FString> Output = GetOption(Args, "-scriptresult="))
		{
			FJsonValue Report = FJsonValue::MakeObject();
			Report["Success"] = Success;
			Report["Result"] = Result;
			Report["Error"] = Error;
			if (!FFile::WriteAllText(*Output, Report.ToString()))
			{
				Shutdown();
				return 1;
			}
		}
		if (!Success || HasFlag(Args, "-exitafterscript"))
		{
			Shutdown();
			return Success ? 0 : 1;
		}
	}
	OnReady();
	while (!App->IsQuitRequested() && MainWindow && !MainWindow->IsClosed())
	{
		App->Tick();
		if (!OnTick() && !App->IsActive())
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
