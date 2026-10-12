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

#pragma once

// Assertions, replacing MFC's ASSERT/VERIFY.
//
// check(Expr)          Debug only, like ASSERT. Expr is not evaluated in
//                      release builds.
// checkf(Expr, Fmt...) check with a printf-style message.
// verify(Expr)         Expr is always evaluated; only checked in debug.
// checkNoEntry()       Marks code that must not be reached.
// ensure(Expr)         Always evaluated and checked, returns Expr as a bool;
//                      a failure is logged but execution continues.
//
// A failed check logs an error and then, in debug builds, shows the C
// runtime's Abort/Retry/Ignore report (the dialog MFC's ASSERT used), or
// breaks straight into an attached debugger.

#include "CoreTypes.h"

namespace Genesis::Private
{
	// Return true when the caller should break into the debugger.
	bool OnCheckFailed(const char* Expr, const char* File, int Line);
	bool OnCheckfFailed(const char* Expr, const char* File, int Line, const char* Format, ...);
	bool OnEnsureFailed(const char* Expr, const char* File, int Line);
}

#if GENESIS_DO_CHECK

#define check(Expr) \
	do { if (!(Expr) && ::Genesis::Private::OnCheckFailed(#Expr, __FILE__, __LINE__)) GENESIS_DEBUG_BREAK(); } while (0)

#define checkf(Expr, Format, ...) \
	do { if (!(Expr) && ::Genesis::Private::OnCheckfFailed(#Expr, __FILE__, __LINE__, Format, ##__VA_ARGS__)) GENESIS_DEBUG_BREAK(); } while (0)

#define verify(Expr) check(Expr)

#else

#define check(Expr) ((void)0)
#define checkf(Expr, Format, ...) ((void)0)
#define verify(Expr) ((void)(Expr))

#endif

#define checkNoEntry() check(!"Enclosing block should never be called")

#define ensure(Expr) \
	((Expr) ? true : (::Genesis::Private::OnEnsureFailed(#Expr, __FILE__, __LINE__) ? (GENESIS_DEBUG_BREAK(), false) : false))
