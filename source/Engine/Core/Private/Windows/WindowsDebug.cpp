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

// Debugger output and check failure reports on Windows.

#include "Containers/String.h"
#include "Windows/WindowsPlatform.h"

#include <crtdbg.h>

namespace Genesis::Private
{
	void WriteToDebugger(const char* Utf8Text)
	{
		OutputDebugStringW(FString(Utf8Text).ToWide().c_str());
	}

	bool IsDebuggerAttached()
	{
		return IsDebuggerPresent() != FALSE;
	}

	bool ReportCheckFailure(const char* Expr, const char* File, int Line, const char* Message)
	{
		if (IsDebuggerAttached())
			return true;

#ifdef _DEBUG
		// The report MFC's ASSERT showed: Abort ends the process, Retry
		// breaks into a debugger, Ignore continues.
		return _CrtDbgReport(_CRT_ASSERT, File, Line, nullptr, "%s%s%s",
							 Expr, Message ? "\n" : "", Message ? Message : "") == 1;
#else
		(void)Expr;
		(void)File;
		(void)Line;
		(void)Message;
		return false;
#endif
	}
}
