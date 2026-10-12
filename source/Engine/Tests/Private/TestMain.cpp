/*
Copyright (C) 2026 Dazzle Software, LLC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// Entry point of GenesisTests: headless tests of Core and MapCore. No MFC
// and no window is involved; the services keep their UI-free defaults.

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "CoreMinimal.h"

#include <atomic>
#include <crtdbg.h>
#include <cstdio>
#include <cstring>

namespace
{
	// Fails the run when any check() or ensure() fails, even inside code
	// that carries on afterwards.
	class FCheckFailureDevice : public FOutputDevice
	{
	public:
		std::atomic<int> Failures = 0;

		void Serialize(const FLogCategory&, ELogVerbosity Verbosity, std::string_view Message) override
		{
			if (Verbosity == ELogVerbosity::Error &&
				(Message.starts_with("Assertion failed") || Message.starts_with("Ensure failed")))
			{
				Failures++;
				std::fprintf(stderr, "%.*s\n", static_cast<int>(Message.size()), Message.data());
			}
		}
	};
}

int main(int argc, char** argv)
{
	// A failed check reports to stderr instead of opening a dialog.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

	// The map core logs its progress text; keep the test output to failures.
	FLog::SetDefaultDevicesEnabled(false);
	FCheckFailureDevice CheckFailures;
	FLog::AddDevice(&CheckFailures);

	doctest::Context Context(argc, argv);
	int Result = Context.run();

	FLog::RemoveDevice(&CheckFailures);
	if (CheckFailures.Failures > 0)
	{
		std::fprintf(stderr, "%d check failure(s) were logged.\n", CheckFailures.Failures.load());
		if (Result == 0)
			Result = 1;
	}
	return Result;
}
