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

// Application-wide facts the core libraries need without knowing which
// application they run in (replacing AfxGetAppName and posting WM_QUIT to
// AfxGetMainWnd).

#include "Containers/Containers.h"
#include "Containers/String.h"

class FApp
{
public:
	// Used for dialog captions and file headers. Defaults to "Genesis Editor".
	static const FString& GetName();
	static void SetName(const FString& Name);

	// Folder holding the running executable, without a trailing separator.
	static FString GetExecutableDir();

	// Asks the application to quit, e.g. after a fatal error. The handler is
	// set by the application; without one only IsExitRequested() changes.
	static void RequestExit();
	static bool IsExitRequested();
	static void SetRequestExitHandler(TFunction<void()> Handler);
};
