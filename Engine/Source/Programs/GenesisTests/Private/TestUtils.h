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

#include "Containers/String.h"
#include "HAL/File.h"

#include <filesystem>
#include <process.h>
#include <string>

// A fresh folder under the system temp folder, removed again afterwards.
struct FTempDir
{
	FString Path;

	FTempDir()
	{
		static int Counter = 0;
		std::filesystem::path Dir = std::filesystem::temp_directory_path() /
		    ("GenesisTests-" + std::to_string(_getpid()) + "-" + std::to_string(Counter++));
		std::filesystem::remove_all(Dir);
		std::filesystem::create_directories(Dir);
		Path = FString(Dir.u8string());
	}

	~FTempDir()
	{
		std::error_code Error;
		std::filesystem::remove_all(std::filesystem::path(Path.Utf8()), Error);
	}

	FString File(const char* Name) const
	{
		return Path + "\\" + Name;
	}

	// For the map core, whose paths are in the ANSI code page.
	std::string AnsiFile(const char* Name) const
	{
		return File(Name).ToAnsi();
	}
};
