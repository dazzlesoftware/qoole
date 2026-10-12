// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Misc/AssertionMacros.h"

#include "HAL/PlatformMisc.h"
#include "Logging/Log.h"

#include <cstdlib>

void FAssertion::CheckFailed(const char* Expr, const char* File, int32 Line, const std::string& Message)
{
	const std::string Text = Message.empty()
		? std::format("Assertion failed: {} [{}({})]", Expr, File, Line)
		: std::format("Assertion failed: {} [{}({})]: {}", Expr, File, Line, Message);
	FLog::Write(LogCore, ELogVerbosity::Fatal, Text);
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

	const std::string Text = Message.empty()
		? std::format("Ensure failed: {} [{}({})]", Expr, File, Line)
		: std::format("Ensure failed: {} [{}({})]: {}", Expr, File, Line, Message);
	FLog::Write(LogCore, ELogVerbosity::Error, Text);
	if (FPlatformMisc::IsDebuggerPresent())
	{
		FPlatformMisc::DebugBreak();
	}
	return false;
}
