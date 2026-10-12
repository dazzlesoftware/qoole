// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Headless tests for the Genesis Engine modules (doctest).
//
//   EngineTests.exe                  run everything
//   EngineTests.exe -tc="Batcher*"   run matching test cases
//   EngineTests.exe -ts=GPU          only the GPU tests (D3D12 WARP / Vulkan, skipped when unavailable)
//   EngineTests.exe --verbose-log    also print engine log output

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "CoreMinimal.h"

#include <cstring>

int main(int ArgC, char** ArgV)
{
	bool bVerboseLog = false;
	for (int I = 1; I < ArgC; ++I)
	{
		bVerboseLog |= std::strcmp(ArgV[I], "--verbose-log") == 0;
	}
	// Engine logging is noise in test output unless asked for.
	FLog::SetDefaultDevicesEnabled(bVerboseLog);

	doctest::Context Context;
	Context.applyCommandLine(ArgC, ArgV);
	return Context.run();
}
