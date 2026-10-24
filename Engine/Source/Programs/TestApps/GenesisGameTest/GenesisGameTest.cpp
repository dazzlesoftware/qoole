// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// GenesisGameTest: the game with a scripted player, for a world composition
// such as the demo (WorldCompositionDemo). It flies east across regions,
// checks streaming, region changes and origin rebasing, saves, moves away,
// loads the save back, lands and walks, capturing the view on the way.
//
//   GenesisGameTest.exe -project=<WorldDemo.gproject> -world=/Game/Worlds/WorldDemo -selftest=<dir> [-vulkan]
//
// Exits with 1 when a check failed.
#include "GenesisGameApp.h"
#include "GameViewportClient.h"
#include "Screenshot.h"
#include "Widgets/SWindow.h"
#include <Windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <cstdio>
#include <filesystem>
#pragma comment(lib, "dwmapi.lib")

namespace
{
	constexpr float StepSeconds = 1.0f / 30.0f;
	constexpr int32 FlightFrames = 150; // 5 s of running flight, about 50,000 units

	class FFileOutputDevice final : public FOutputDevice
	{
	public:
		explicit FFileOutputDevice(const FString& Path)
		{
			File = _wfopen(Path.ToWide().c_str(), L"w");
		}
		~FFileOutputDevice() override
		{
			if (File)
				std::fclose(File);
		}
		void Serialize(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message) override
		{
			if (File)
			{
				std::fprintf(File, "[%8.3f] %s: %s: %.*s\n", FPlatformTime::Seconds() - Start, Category.Name,
				    ToString(Verbosity), static_cast<int>(Message.size()), Message.data());
				std::fflush(File);
			}
		}

	private:
		FILE* File = nullptr;
		double Start = FPlatformTime::Seconds();
	};
} // namespace

class FGenesisGameTestApp final : public FGenesisGameApp
{
public:
	int32 GetFailures() const
	{
		return Failures + (Checks == 0 ? 1 : 0);
	}

protected:
	void OnInitialize(const TArray<FString>& Args) override
	{
		for (const FString& Arg : Args)
			if (Arg.StartsWith("-selftest="))
				SelfTestDir = Arg.Mid(10);
		if (SelfTestDir.IsEmpty())
			SelfTestDir = "Engine/Saved/GenesisGameTest/";
		if (!SelfTestDir.EndsWith("/") && !SelfTestDir.EndsWith("\\"))
			SelfTestDir += "/";
		std::error_code Error;
		std::filesystem::create_directories(std::filesystem::path(SelfTestDir.ToWide()), Error);
		FixedDeltaSeconds = StepSeconds;
		FileLog = MakeUnique<FFileOutputDevice>(
		    SelfTestDir + FString::Format("GenesisGameTest_{}_log.txt", ToString(Backend)));
		FLog::AddDevice(FileLog.get());
	}

	bool OnTick(float) override
	{
		const int32 Frame = FrameCount++;
		FString Error;
		if (Frame == 10)
		{
			Check(Game->GetWorld().IsComposition() && Game->GetWorld().IsLoaded(Game->Player.Location.RegionId),
			    "game started in a loaded region");
			Start = Game->Player.Location;
			Capture("game_start");
		}
		else if (Frame == 11)
		{
			// Fly east, running, climbing a little.
			Game->Player.bFlying = true;
			Game->Player.Yaw = 0;
			Game->Player.Pitch = 0;
			Game->Input = {};
			Game->Input.Forward = 1;
			Game->Input.Up = 0.15f;
			Game->Input.bRun = true;
		}
		else if (Frame == 11 + FlightFrames)
		{
			Game->Input = {};
			const FWorldCompositionTile* Tile =
			    Game->GetWorld().GetComposition().FindTile(Game->Player.Location.RegionId);
			Check(Game->GetRegionsEntered() >= 3,
			    FString::Format("flew across {} region boundaries", Game->GetRegionsEntered()));
			Check(Game->GetRebases() >= 1,
			    FString::Format(
			        "rebased the origin {} times, now cell {}", Game->GetRebases(), Game->GetWorld().GetOriginCellX()));
			Check(Tile && Tile->Name == "Mesa", FString::Format("arrived over {}", Tile ? Tile->Name : FString("?")));
			Vector3d Render;
			Check(Game->GetWorld().ToRender(Game->Player.Location, Render) &&
			        std::abs(Render.GetX()) < 3 * Game->GetWorld().GetComposition().GetCellSize(),
			    "render position stayed near the origin");
			// Look back west over the world just crossed.
			Game->Player.Yaw = 180;
			Game->Player.Pitch = -20;
		}
		else if (Frame == 14 + FlightFrames)
		{
			Capture("game_flight");
			Check(Game->QuickSave(Error), "quick save " + Error);
			Saved = Game->Player.Location;
			Game->Player.Location = Start;
			Game->GetWorld().Update(Game->Player.Location);
		}
		else if (Frame == 18 + FlightFrames)
		{
			Check(Game->Player.Location.RegionId == Start.RegionId, "moved back to the start");
			Check(Game->QuickLoad(Error), "quick load " + Error);
			Check(Game->Player.Location.RegionId == Saved.RegionId &&
			        std::abs(Game->Player.Location.Local.GetX() - Saved.Local.GetX()) < 1.0f,
			    "the save restored the region and position");
			Game->Player.bFlying = false;
			Game->Player.Pitch = -10;
		}
		else if (Frame == 18 + FlightFrames + 120)
		{
			Check(Game->Player.bOnGround, "landed on the region's terrain");
			Game->Input.Forward = 1;
		}
		else if (Frame == 18 + FlightFrames + 150)
		{
			Game->Input = {};
			Check(Game->Player.bOnGround, "walked on the ground");
			Capture("game_ground");
			Check(Viewport && Viewport->GetLastError().IsEmpty(),
			    "drew without errors " + (Viewport ? Viewport->GetLastError() : FString()));
		}
		else if (Frame == 20 + FlightFrames + 150)
		{
			GENESIS_LOG(LogGenesisGame, Display, "GenesisGameTest: {} checks, {} failed", Checks, Failures);
			return false;
		}
		return true;
	}

private:
	void Check(bool bCondition, const FString& What)
	{
		++Checks;
		if (!bCondition)
			++Failures;
		GENESIS_LOG(LogGenesisGame, Display, "Self-test {}{}", bCondition ? "" : "FAILED: ", What);
	}
	void Capture(const char* Name)
	{
		const HWND Hwnd = static_cast<HWND>(MainWindow->GetNativeWindow()->GetNativeHandle());
		SetWindowPos(Hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		for (int32 I = 0; I < 3; ++I)
			App->Tick();
		for (int32 I = 0; I < 3; ++I)
			DwmFlush();
		const FString Path = SelfTestDir + FString::Format("{}_{}.png", ToString(Backend), Name);
		Check(CaptureWindowClientToPng(Hwnd, Path), FString::Format("screenshot {}", Path));
		SetWindowPos(Hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	FString SelfTestDir;
	FWorldLocation Start, Saved;
	int32 FrameCount = 0, Checks = 0, Failures = 0;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	int32 Result = 0;
	{
		const TSharedPtr<FGenesisGameTestApp> Game = std::make_shared<FGenesisGameTestApp>();
		Result = Game->Run(FString::FromWide(GetCommandLineW()));
		if (Result == 0 && Game->GetFailures() > 0)
			Result = 1;
	}
	CoUninitialize();
	return Result;
}
