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

// File and folder pickers, replacing CDirDialog. Open/save file pickers will
// be added when code outside the UI needs them.

#include "Containers/String.h"

class IFileDialogs
{
public:
	virtual ~IFileDialogs() = default;

	// Returns false if the user cancelled. OutPath is a UTF-8 path.
	virtual bool BrowseForFolder(const FString& Title, FString& OutPath) = 0;
};

// Default without a UI: every dialog is cancelled.
class FNullFileDialogs : public IFileDialogs
{
public:
	bool BrowseForFolder(const FString& Title, FString& OutPath) override;
};
