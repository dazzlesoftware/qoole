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

// File access, replacing MFC's CFile, CFileStatus and CFileException.
// Paths are UTF-8. Failures are reported by return values, never exceptions.

#include "CoreTypes.h"
#include "Containers/String.h"

#include <cstdio>

enum class EFileOpenMode : uint8
{
	Read,   // Existing file, read only.
	Write,  // Created or truncated, write only.
	Append, // Created if missing, writes go to the end.
};

class FFile
{
public:
	FFile() = default;
	~FFile()
	{
		Close();
	}
	FFile(const FFile&) = delete;
	FFile& operator=(const FFile&) = delete;
	FFile(FFile&& Other) noexcept : Handle(Other.Handle)
	{
		Other.Handle = nullptr;
	}
	FFile& operator=(FFile&& Other) noexcept;

	bool Open(const FString& Path, EFileOpenMode Mode);
	void Close();
	bool IsOpen() const
	{
		return Handle != nullptr;
	}

	// Bytes actually read or written.
	int64 Read(void* Buffer, int64 Size);
	int64 Write(const void* Buffer, int64 Size);
	bool Write(const FString& Text)
	{
		return Write(*Text, Text.Len()) == Text.Len();
	}

	bool Seek(int64 Offset);
	int64 Tell() const;
	int64 Size() const;
	bool AtEnd() const;

	static bool Exists(const FString& Path); // a file, not a directory
	static bool DirectoryExists(const FString& Path);
	static bool IsReadOnly(const FString& Path);
	static bool SetReadOnly(const FString& Path, bool bReadOnly);
	// Deletes a file. With bEvenIfReadOnly the read-only attribute is cleared
	// first. Returns true if the file no longer exists.
	static bool Delete(const FString& Path, bool bEvenIfReadOnly = false);

	static bool ReadAllBytes(const FString& Path, TArray<uint8>& OutBytes);
	static bool ReadAllText(const FString& Path, FString& OutText);
	static bool WriteAllText(const FString& Path, const FString& Text);

private:
	std::FILE* Handle = nullptr;
};
