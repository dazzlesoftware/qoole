// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "GenesisGameApp.h"
#include "GameViewportClient.h"
#include "Templates/SharedPointer.h"
#include "HAL/File.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "ProjectDescriptor.h"
#include "Renderer/LandscapeTextures.h"
#include "Services/ConfigStore.h"
#include "Services/Services.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include <algorithm>
#include <filesystem>

DEFINE_LOG_CATEGORY(LogGenesisGame, Log);

namespace
{
	constexpr const char* QuickSaveName = "QuickSave.json";
	const FVector2 WindowSize(1280.0f, 720.0f);

	TArray<FString> SplitCommandLine(const FString& CommandLine)
	{
		TArray<FString> Result;
		std::string Current;
		bool bQuoted = false, bAny = false;
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
					Result.push_back(FString(Current));
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
			Result.push_back(FString(Current));
		return Result;
	}
	TOptional<FString> GetOption(const TArray<FString>& Args, std::string_view Name)
	{
		for (const FString& Arg : Args)
			if (Arg.StartsWith(Name))
				return Arg.Mid(static_cast<int32>(Name.size()));
		return {};
	}
	bool HasFlag(const TArray<FString>& Args, std::string_view Flag)
	{
		for (const FString& Arg : Args)
			if (Arg.Equals(Flag, ESearchCase::IgnoreCase))
				return true;
		return false;
	}
} // namespace

FGenesisGameApp::FGenesisGameApp() = default;
FGenesisGameApp::~FGenesisGameApp() = default;

bool FGenesisGameApp::Fail(const FString& Message)
{
	StartupError = Message;
	GENESIS_LOG(LogGenesisGame, Error, "{}", Message);
	return false;
}

int32 FGenesisGameApp::Run(const FString& CommandLine)
{
	if (!Initialize(CommandLine))
	{
		Shutdown();
		return 1;
	}
	LastFrame = LastFpsTime = StartTime = FPlatformTime::Seconds();
	bool bRunning = true;
	int32 Result = 0;
	while (bRunning && !App->IsQuitRequested() && MainWindow && !MainWindow->IsClosed())
	{
		const double Now = FPlatformTime::Seconds();
		const float Elapsed = static_cast<float>(Now - LastFrame);
		LastFrame = Now;
		++FramesSinceFps;
		if (Now - LastFpsTime >= 0.5)
		{
			FramesPerSecond = static_cast<float>(FramesSinceFps / (Now - LastFpsTime));
			FramesSinceFps = 0;
			LastFpsTime = Now;
		}
		const float Delta = FixedDeltaSeconds > 0 ? FixedDeltaSeconds : std::min(Elapsed, MaxFrameSeconds);
		Game->Tick(Delta);
		bRunning = OnTick(Delta);
		App->Tick();
		if (!PlayTestReport.IsEmpty() && Now - StartTime >= PlayTestSeconds)
		{
			Result = WritePlayTestReport() ? 0 : 1;
			bRunning = false;
		}
	}
	Shutdown();
	return Result;
}

bool FGenesisGameApp::Initialize(const FString& CommandLine)
{
	const TArray<FString> Args = SplitCommandLine(CommandLine);
	Backend = ParseBackendFromCommandLine(CommandLine.View());
	PlayTestReport = GetOption(Args, "-playtest=").value_or(FString());
	OnInitialize(Args);
	FApp::SetName("Genesis Game");
	Config = MakeUnique<FMemoryConfigStore>();
	FServices::SetConfig(Config.get());

	Platform = CreatePlatformApplication().ToSharedPtr();
	FRHIDeviceDesc DeviceDesc;
	DeviceDesc.Backend = Backend;
	DeviceDesc.bDebugLayer = HasFlag(Args, "-debug");
	Device = FRHIDevice::Create(DeviceDesc);
	if (!Device)
		return Fail("Could not create a {} device.", ToString(Backend));
	FontCache = std::make_shared<FSlateFontCache>();
	Fonts = FStarshipCoreFonts::Load(*FontCache);
	if (!Fonts.IsValid())
		return Fail(GENESIS_TEXT("The fonts could not be loaded."));
	const TSharedRef<FSlateStyleSet> Style = FStarshipCoreStyle::Create(Fonts);
	FSlateStyleSet::Register(Style);
	FAppStyle::SetAppStyleSet(Style);
	Renderer = MakeUnique<FSlateRHIRenderingPolicy>(*Device, *FontCache);
	if (!Renderer->Initialize())
		return Fail(GENESIS_TEXT("UI renderer initialization failed."));
	WindowRenderer = MakeUnique<FSlateRHIRenderer>(*Device, *Renderer);
	App = FSlateApplication::Create(ToSharedRef(Platform), ToSharedRef(FontCache)).ToSharedPtr();
	App->SetRenderer(WindowRenderer.get());

	if (!OpenGame(Args))
		return false;

	Viewport = std::make_shared<FGameViewportClient>(*Device, *Renderer, *Game);
	Viewport->OnQuit = FSimpleDelegate::CreateLambda([this] { App->RequestQuit(); });
	WindowRenderer->AddViewportClient(ToSharedRef(Viewport));
	MainWindow =
	    SNew(SWindow)
	        .Title(FTextLocalization::Format("{} - Genesis Game ({})", Game->GetWorldName(), ToString(Backend)))
	        .ClientSize(WindowSize)
	        .OnWindowClosed([this](const TSharedRef<SWindow>&) { App->RequestQuit(); });
	App->AddWindow(ToSharedRef(MainWindow));
	MainWindow->SetContent(FGameViewportClient::MakeGameView(ToSharedRef(Viewport), [this]
	{
		return Game->GetHudText(FramesPerSecond) +
		    GENESIS_TEXT("\nClick to look around, Esc to release. WASD move, Shift run, Space jump, F fly, Q/E "
		                 "down/up, F5 save, F9 load, F10 quit");
	}, ViewportWidget));
	App->SetKeyboardFocus(ViewportWidget);
	GENESIS_LOG(LogGenesisGame, Display, "Playing {} on {}: {}", Game->GetWorldName(), ToString(Backend),
	    Device->GetAdapterName());
	return true;
}

bool FGenesisGameApp::OpenGame(const TArray<FString>& Args)
{
	FString Error;
	const TOptional<FString> ProjectPath = GetOption(Args, "-project=");
	if (!ProjectPath)
		return Fail(GENESIS_TEXT("Start the game with -project=<file.gproject>."));
	TOptional<FProjectContext> Project = FProjectContext::Open(*ProjectPath, Error);
	if (!Project)
		return Fail(Error);
	FProjectContext::SetActive(std::move(Project));
	const FProjectContext& Active = *FProjectContext::GetActive();
	const FString StartWorld = GetOption(Args, "-world=").value_or(Active.GetDescriptor().DefaultMap);
	if (StartWorld.IsEmpty())
		return Fail(GENESIS_TEXT("The project has no startup world; set one in the editor or pass -world=/Game/..."));
	Registry = std::make_shared<FAssetRegistry>();
	if (!Registry->SetMounts({{"/Game", Active.GetContentDir()}, {"/Engine", FPaths::FindContentDir()}}, Error))
		return Fail(Error);
	Registry->Scan();
	FLandscapeTextureLibrary::SetAssetRegistry(Registry);
	const FString SaveFile = FString::FromWide(
	    (std::filesystem::path(Active.GetSavedDir().ToWide()) / L"SaveGames" / QuickSaveName).wstring());
	Game = FGameInstance::Open(Registry, {{}, StartWorld}, SaveFile, Error);
	if (!Game)
		return Fail("Could not open {}: {}", StartWorld, Error);
	if (HasFlag(Args, "-load") && !Game->QuickLoad(Error))
		GENESIS_LOG(LogGenesisGame, Warning, "{}", Error);
	return true;
}

bool FGenesisGameApp::WritePlayTestReport()
{
	TArray<FGameRegion> Loaded;
	Game->GetWorld().GetLoadedRegions(Loaded);
	const FWorldCompositionTile* Region = Game->GetWorld().GetComposition().FindTile(Game->Player.Location.RegionId);
	FJsonValue Report = FJsonValue::MakeObject();
	Report["World"] = Game->GetWorldName();
	Report["WorldId"] = Game->GetWorld().GetReference().Id;
	Report["Composition"] = Game->GetWorld().IsComposition();
	Report["Region"] = Region ? Region->Name : FString();
	Report["RegionsLoaded"] = static_cast<int32>(Loaded.size());
	Report["Ticks"] = static_cast<double>(Game->GetTicks());
	Report["FramesDrawn"] = static_cast<double>(Viewport ? Viewport->GetFramesDrawn() : 0);
	Report["RegionsDrawn"] = Viewport ? Viewport->GetRegionsDrawn() : 0;
	Report["Error"] = Viewport ? Viewport->GetLastError() : FString();
	Report["Backend"] = ToString(Backend);
	const bool bWritten = FFile::WriteAllText(PlayTestReport, Report.ToString());
	GENESIS_LOG(
	    LogGenesisGame, Display, "Play test report {} {}", bWritten ? "written to" : "FAILED for", PlayTestReport);
	return bWritten;
}

void FGenesisGameApp::Shutdown()
{
	if (Device)
		Device->WaitIdle();
	if (App)
	{
		App->SetRenderer(nullptr);
		FSlateApplication::Shutdown();
	}
	App.reset();
	MainWindow.reset();
	ViewportWidget.reset();
	Viewport.reset();
	FLandscapeTextureLibrary::SetAssetRegistry({});
	Game.reset();
	if (Device)
		Device->WaitIdle();
	WindowRenderer.reset();
	Renderer.reset();
	Device.reset();
	FServices::SetConfig(nullptr);
	if (FileLog)
	{
		FLog::RemoveDevice(FileLog.get());
		FileLog.reset();
	}
}
