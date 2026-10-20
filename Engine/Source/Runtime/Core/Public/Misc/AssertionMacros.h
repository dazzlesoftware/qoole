// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// check / checkf / verify / checkNoEntry / ensure / ensuref.
//
// - check(expr), checkf(expr, fmt, args...): compiled out (expression not
//   evaluated) when DO_CHECK is 0, the default for Release builds.
//   A failure is logged as an error and then, like the MFC ASSERT these
//   replace in the map core, breaks into an attached debugger or shows the C
//   runtime's Abort/Retry/Ignore report. Ignore continues.
// - verify(expr): like check, but the expression is always evaluated and
//   checked.
// - checkNoEntry(): marks code that must not be reached; fatal.
// - ensure(expr), ensuref(expr, fmt, args...): always evaluated; when false,
//   logs an error (once per call site), breaks into an attached debugger and
//   returns false so the caller can recover: if (!ensure(Ptr)) return;
//
// Messages use std::format syntax ("{}").

#pragma once

#include "CoreTypes.h"

#include <format>
#include <string>

#ifndef DO_CHECK
	#define DO_CHECK UE_BUILD_DEBUG
#endif

struct FAssertion
{
	// Reports a failed check or verify. Returns if the developer chooses to
	// continue.
	static void CheckFailed(const char* Expr, const char* File, int32 Line, const std::string& Message);

	// Reports a failure and terminates.
	[[noreturn]] static void Fatal(const char* Expr, const char* File, int32 Line, const std::string& Message);

	// Reports a failed ensure. bFired is the call site's "already reported" flag.
	// Always returns false.
	static bool EnsureFailed(const char* Expr, const char* File, int32 Line, bool& bFired, const std::string& Message);

	template <typename... Args>
	static std::string FormatText(std::format_string<Args...> Fmt, Args&&... InArgs)
	{
		return std::format(Fmt, std::forward<Args>(InArgs)...);
	}

	static std::string FormatText() { return {}; }
};

#if DO_CHECK
	#define check(expr) \
		do { if (!(expr)) [[unlikely]] { ::FAssertion::CheckFailed(#expr, __FILE__, __LINE__, {}); } } while (0)
	#define checkf(expr, fmt, ...) \
		do { if (!(expr)) [[unlikely]] { ::FAssertion::CheckFailed(#expr, __FILE__, __LINE__, ::FAssertion::FormatText(fmt __VA_OPT__(,) __VA_ARGS__)); } } while (0)
	#define checkNoEntry() ::FAssertion::Fatal("unreachable code", __FILE__, __LINE__, {})
#else
	#define check(expr) do { } while (0)
	#define checkf(expr, fmt, ...) do { } while (0)
	#define checkNoEntry() do { } while (0)
#endif

#define verify(expr) \
	do { if (!(expr)) [[unlikely]] { ::FAssertion::CheckFailed(#expr, __FILE__, __LINE__, {}); } } while (0)

#define ensure(expr) \
	((expr) ? true : [&]() { static bool bFired = false; return ::FAssertion::EnsureFailed(#expr, __FILE__, __LINE__, bFired, {}); }())

#define ensuref(expr, fmt, ...) \
	((expr) ? true : [&]() { static bool bFired = false; return ::FAssertion::EnsureFailed(#expr, __FILE__, __LINE__, bFired, ::FAssertion::FormatText(fmt __VA_OPT__(,) __VA_ARGS__)); }())
