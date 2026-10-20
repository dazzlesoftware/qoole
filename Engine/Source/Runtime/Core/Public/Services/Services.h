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

// The services that let UI-free code reach the user and the settings. The
// application registers its implementations at startup; until it does, each
// getter returns a default that needs no UI (see the individual headers).
//
// Registered objects are not owned and must outlive their registration.
// Passing nullptr restores the default.

#include "Services/ConfigStore.h"
#include "Services/FileDialogs.h"
#include "Services/MessageDialog.h"
#include "Services/Progress.h"

class FServices
{
public:
	static IConfigStore& GetConfig();
	static void SetConfig(IConfigStore* Store);

	static IMessageDialog& GetMessageDialog();
	static void SetMessageDialog(IMessageDialog* Dialog);

	static IProgress& GetProgress();
	static void SetProgress(IProgress* Progress);

	static IFileDialogs& GetFileDialogs();
	static void SetFileDialogs(IFileDialogs* Dialogs);
};
