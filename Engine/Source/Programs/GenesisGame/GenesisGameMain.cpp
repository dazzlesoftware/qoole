// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// GenesisGame: plays a project's startup world, a level or a world
// composition, as Unreal's standalone game.
//
// Command line:
//   GenesisGame.exe -project=<file.gproject> [-world=/Game/...] [-d3d12 | -vulkan] [-load]
#include "GenesisGameApp.h"
#include <Windows.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	int32 Result = 0;
	{
		const TSharedPtr<FGenesisGameApp> Game = std::make_shared<FGenesisGameApp>();
		Result = Game->Run(FString::FromWide(GetCommandLineW()));
		if (Result != 0 && !Game->GetStartupError().IsEmpty())
			MessageBoxW(nullptr, Game->GetStartupError().ToWide().c_str(), L"Genesis Game", MB_OK | MB_ICONERROR);
	}
	CoUninitialize();
	return Result;
}
