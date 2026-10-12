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

#include "Misc/AssertionMacros.h"
#include "Logging/Log.h"
#include "Misc/StringFormat.h"

namespace Genesis::Private
{
	// Platform part: shows the failure to the developer. Returns true to break.
	bool ReportCheckFailure(const char* Expr, const char* File, int Line, const char* Message);
	bool IsDebuggerAttached();

	bool OnCheckFailed(const char* Expr, const char* File, int Line)
	{
		GLog->Logf(ELogVerbosity::Error, "Check failed: %s, %s(%d)\n", Expr, File, Line);
		return ReportCheckFailure(Expr, File, Line, nullptr);
	}

	bool OnCheckfFailed(const char* Expr, const char* File, int Line, const char* Format, ...)
	{
		va_list Args;
		va_start(Args, Format);
		const std::string Message = FormatV(Format, Args);
		va_end(Args);

		GLog->Logf(ELogVerbosity::Error, "Check failed: %s, %s(%d): %s\n", Expr, File, Line, Message.c_str());
		return ReportCheckFailure(Expr, File, Line, Message.c_str());
	}

	bool OnEnsureFailed(const char* Expr, const char* File, int Line)
	{
		GLog->Logf(ELogVerbosity::Error, "Ensure failed: %s, %s(%d)\n", Expr, File, Line);
		return IsDebuggerAttached();
	}
}
