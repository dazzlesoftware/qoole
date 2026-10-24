// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The standalone game host shared by GenesisGame and its self-test program:
// the RHI device, the UI renderer and the window showing the game view. The
// game itself (world, player, saves) is an FGameInstance and its view an
// FGameViewportClient, which Play-in-Editor uses too.
#pragma once

#include "Framework/Application/SlateApplication.h"
#include "CoreMinimal.h"
#include "RHI.h"
#include "SlateRHIRenderer.h"
#include "SlateRHIRenderingPolicy.h"
#include "Styling/StarshipCoreStyle.h"
#include "GameFramework/GameInstance.h"
#include "Internationalization/TextLocalization.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGenesisGame, Log);

class FGameViewportClient;
class IConfigStore;
class SViewport;

class FGenesisGameApp : public TSharedFromThis<FGenesisGameApp>
{
public:
	static constexpr float MaxFrameSeconds = 0.1f;
	// How long -playtest plays before writing its report.
	static constexpr double PlayTestSeconds = 3.0;
	FGenesisGameApp();
	virtual ~FGenesisGameApp();
	// The command line, without the program name:
	//   -project=<file.gproject>  required
	//   -world=/Game/...           a level or world composition instead of the project's DefaultMap
	//   -d3d12 | -vulkan           backend (default D3D12)
	//   -load                      continue from the quick save
	//   -playtest=<report.json>    play briefly, write what was played and drawn, and quit
	int32 Run(const FString& CommandLine);
	// Why the game could not start, for the entry point to show.
	const FString& GetStartupError() const
	{
		return StartupError;
	}

protected:
	// Self-test hooks.
	virtual void OnInitialize(const TArray<FString>& Args)
	{
		(void)Args;
	}
	// After each simulation step; return false to quit.
	virtual bool OnTick(float DeltaSeconds)
	{
		(void)DeltaSeconds;
		return true;
	}

	ERHIBackend Backend = ERHIBackend::D3D12;
	float FixedDeltaSeconds = 0; // when set, each frame advances the game by exactly this much
	TUniquePtr<FGameInstance> Game;
	TSharedPtr<FSlateApplication> App;
	TSharedPtr<SWindow> MainWindow;
	TSharedPtr<FGameViewportClient> Viewport;
	TUniquePtr<FOutputDevice> FileLog;

private:
	bool Initialize(const FString& CommandLine);
	template <typename... Args> bool Fail(std::format_string<Args...> Format, Args&&... Arguments)
	{
		return Fail(FTextLocalization::Format(Format, std::forward<Args>(Arguments)...));
	}
	bool Fail(const FString& Message);
	bool OpenGame(const TArray<FString>& Args);
	bool WritePlayTestReport();
	void Shutdown();

	TSharedPtr<IPlatformApplication> Platform;
	TUniquePtr<FRHIDevice> Device;
	TSharedPtr<FSlateFontCache> FontCache;
	FStarshipCoreFonts Fonts;
	TUniquePtr<FSlateRHIRenderingPolicy> Renderer;
	TUniquePtr<FSlateRHIRenderer> WindowRenderer;
	TUniquePtr<IConfigStore> Config;
	std::shared_ptr<FAssetRegistry> Registry;
	TSharedPtr<SViewport> ViewportWidget;
	FString StartupError, PlayTestReport;
	double LastFrame = 0, LastFpsTime = 0, StartTime = 0;
	int32 FramesSinceFps = 0;
	float FramesPerSecond = 0;
};
