// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// check / checkf / verify / ensure / ensuref.
//
// - check(expr), checkf(expr, fmt, args...): fatal when false. Compiled out
//   (expression not evaluated) when GENESIS_DO_CHECK is 0, the default for
//   Release builds.
// - verify(expr): like check, but the expression is always evaluated.
// - ensure(expr), ensuref(expr, fmt, args...): always evaluated; when false,
//   logs an error (once per call site), breaks into an attached debugger and
//   returns false so the caller can recover: if (!ensure(Ptr)) return;
//
// Messages use std::format syntax ("{}").

#pragma once

#include "CoreTypes.h"

#include <format>
#include <string>

#ifndef GENESIS_DO_CHECK
	#define GENESIS_DO_CHECK GENESIS_DEBUG
#endif

struct FAssertion
{
	// Reports a failed check and terminates.
	[[noreturn]] static void CheckFailed(const char* Expr, const char* File, int32 Line, const std::string& Message);

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

#if GENESIS_DO_CHECK
	#define check(expr) \
		do { if (!(expr)) [[unlikely]] { ::FAssertion::CheckFailed(#expr, __FILE__, __LINE__, {}); } } while (0)
	#define checkf(expr, fmt, ...) \
		do { if (!(expr)) [[unlikely]] { ::FAssertion::CheckFailed(#expr, __FILE__, __LINE__, ::FAssertion::FormatText(fmt __VA_OPT__(,) __VA_ARGS__)); } } while (0)
	#define checkNoEntry() ::FAssertion::CheckFailed("unreachable code", __FILE__, __LINE__, {})
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
