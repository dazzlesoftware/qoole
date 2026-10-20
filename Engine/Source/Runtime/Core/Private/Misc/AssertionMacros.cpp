// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Misc/AssertionMacros.h"

#include "HAL/PlatformMisc.h"
#include "Logging/Log.h"

#include <cstdlib>

#if defined(_MSC_VER) && defined(_DEBUG)
	#include <crtdbg.h>
#endif

namespace
{
	std::string Describe(const char* Kind, const char* Expr, const char* File, int32 Line, const std::string& Message)
	{
		return Message.empty()
			? std::format("{}: {} [{}({})]", Kind, Expr, File, Line)
			: std::format("{}: {} [{}({})]: {}", Kind, Expr, File, Line, Message);
	}
}

void FAssertion::CheckFailed(const char* Expr, const char* File, int32 Line, const std::string& Message)
{
	FLog::Write(LogCore, ELogVerbosity::Error, Describe("Assertion failed", Expr, File, Line, Message));

	if (FPlatformMisc::IsDebuggerPresent())
	{
		FPlatformMisc::DebugBreak();
		return;
	}

#if defined(_MSC_VER) && defined(_DEBUG)
	// The report MFC's ASSERT showed: Abort ends the process, Retry breaks
	// into a debugger, Ignore continues.
	if (_CrtDbgReport(_CRT_ASSERT, File, Line, nullptr, "%s%s%s",
					  Expr, Message.empty() ? "" : "\n", Message.c_str()) == 1)
	{
		FPlatformMisc::DebugBreak();
	}
#endif
}

void FAssertion::Fatal(const char* Expr, const char* File, int32 Line, const std::string& Message)
{
	FLog::Write(LogCore, ELogVerbosity::Fatal, Describe("Fatal error", Expr, File, Line, Message));
	// FLog aborts on Fatal; this is for the compiler.
	std::abort();
}

bool FAssertion::EnsureFailed(const char* Expr, const char* File, int32 Line, bool& bFired, const std::string& Message)
{
	if (bFired)
	{
		return false;
	}
	bFired = true;

	FLog::Write(LogCore, ELogVerbosity::Error, Describe("Ensure failed", Expr, File, Line, Message));
	if (FPlatformMisc::IsDebuggerPresent())
	{
		FPlatformMisc::DebugBreak();
	}
	return false;
}
