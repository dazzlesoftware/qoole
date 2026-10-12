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
	class FCheckFailureSink : public ILogSink
	{
	public:
		std::atomic<int> Failures = 0;

		void Serialize(ELogVerbosity Verbosity, const char* Utf8Text) override
		{
			if (Verbosity == ELogVerbosity::Error &&
				(std::strncmp(Utf8Text, "Check failed", 12) == 0 || std::strncmp(Utf8Text, "Ensure failed", 13) == 0))
			{
				Failures++;
				std::fputs(Utf8Text, stderr);
			}
		}
	};
}

int main(int argc, char** argv)
{
	// A failed check reports to stderr instead of opening a dialog.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

	FCheckFailureSink CheckFailures;
	GLog->AddSink(&CheckFailures);

	doctest::Context Context(argc, argv);
	int Result = Context.run();

	GLog->RemoveSink(&CheckFailures);
	if (CheckFailures.Failures > 0)
	{
		std::fprintf(stderr, "%d check failure(s) were logged.\n", CheckFailures.Failures.load());
		if (Result == 0)
			Result = 1;
	}
	return Result;
}
