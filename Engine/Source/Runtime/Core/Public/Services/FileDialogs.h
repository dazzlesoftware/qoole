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

// File and folder pickers, replacing CDirDialog and CFileDialog.

#include "Containers/ContainerAliases.h"
#include "Containers/UnrealString.h"

// One entry of a picker's file type list, e.g. { "Maps", "*.qle;*.map" }.
struct FFileTypeFilter
{
	FString Description;
	FString Patterns; // semicolon-separated wildcards
};

struct FFileDialogOptions
{
	FString Title;
	FString InitialPath;          // a folder, or a file (its folder and name are used)
	TArray<FFileTypeFilter> Filters;
	FString DefaultExtension;     // added to typed names without one (save), without the dot
	void* ParentWindow = nullptr; // native window handle; null means the active window
};

class IFileDialogs
{
public:
	virtual ~IFileDialogs() = default;

	// Each returns false if the user cancelled. Paths are UTF-8.
	virtual bool BrowseForFolder(const FString& Title, FString& OutPath) = 0;
	virtual bool OpenFile(const FFileDialogOptions& Options, FString& OutPath) { (void)Options; (void)OutPath; return false; }
	// Asks before replacing an existing file.
	virtual bool SaveFile(const FFileDialogOptions& Options, FString& OutPath) { (void)Options; (void)OutPath; return false; }
};

// Default without a UI: every dialog is cancelled.
class FNullFileDialogs : public IFileDialogs
{
public:
	bool BrowseForFolder(const FString& Title, FString& OutPath) override;
};
