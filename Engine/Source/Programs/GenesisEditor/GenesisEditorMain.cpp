// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "GenesisEditorApp.h"
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	// An exception nothing caught: say what it was before the process ends.
	std::set_terminate([]
	{
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
		GENESIS_LOG(LogGenesisEditor, Error, "Unhandled exception: {}", What);
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
