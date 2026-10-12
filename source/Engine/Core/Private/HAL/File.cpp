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

#include "HAL/File.h"

#include <filesystem>
#include <share.h>
#include <system_error>

namespace
{
	std::filesystem::path ToPath(const FString& Path)
	{
		return std::filesystem::path(Path.GetData());
	}

	std::FILE* OpenFile(const FString& Path, EFileOpenMode Mode)
	{
		const std::filesystem::path NativePath = ToPath(Path);
#ifdef _WIN32
		const wchar_t* ModeText = Mode == EFileOpenMode::Read ? L"rb" : Mode == EFileOpenMode::Write ? L"wb" : L"ab";
		return _wfsopen(NativePath.c_str(), ModeText, _SH_DENYNO);
#else
		const char* ModeText = Mode == EFileOpenMode::Read ? "rb" : Mode == EFileOpenMode::Write ? "wb" : "ab";
		return std::fopen(NativePath.c_str(), ModeText);
#endif
	}
}

FFile& FFile::operator=(FFile&& Other) noexcept
{
	if (this != &Other)
	{
		Close();
		Handle = Other.Handle;
		Other.Handle = nullptr;
	}
	return *this;
}

bool FFile::Open(const FString& Path, EFileOpenMode Mode)
{
	Close();
	Handle = OpenFile(Path, Mode);
	return Handle != nullptr;
}

void FFile::Close()
{
	if (Handle)
	{
		std::fclose(Handle);
		Handle = nullptr;
	}
}

int64 FFile::Read(void* Buffer, int64 Size)
{
	if (!Handle || Size <= 0)
		return 0;
	return static_cast<int64>(std::fread(Buffer, 1, static_cast<size_t>(Size), Handle));
}

int64 FFile::Write(const void* Buffer, int64 Size)
{
	if (!Handle || Size <= 0)
		return 0;
	return static_cast<int64>(std::fwrite(Buffer, 1, static_cast<size_t>(Size), Handle));
}

bool FFile::Seek(int64 Offset)
{
	return Handle && _fseeki64(Handle, Offset, SEEK_SET) == 0;
}

int64 FFile::Tell() const
{
	return Handle ? _ftelli64(Handle) : -1;
}

int64 FFile::Size() const
{
	if (!Handle)
		return -1;
	const int64 Position = _ftelli64(Handle);
	_fseeki64(Handle, 0, SEEK_END);
	const int64 Result = _ftelli64(Handle);
	_fseeki64(Handle, Position, SEEK_SET);
	return Result;
}

bool FFile::AtEnd() const
{
	return !Handle || Tell() >= Size();
}

bool FFile::Exists(const FString& Path)
{
	std::error_code Error;
	return std::filesystem::is_regular_file(ToPath(Path), Error);
}

bool FFile::DirectoryExists(const FString& Path)
{
	std::error_code Error;
	return std::filesystem::is_directory(ToPath(Path), Error);
}

bool FFile::IsReadOnly(const FString& Path)
{
	std::error_code Error;
	const std::filesystem::file_status Status = std::filesystem::status(ToPath(Path), Error);
	if (Error || !std::filesystem::exists(Status))
		return false;
	return (Status.permissions() & std::filesystem::perms::owner_write) == std::filesystem::perms::none;
}

bool FFile::SetReadOnly(const FString& Path, bool bReadOnly)
{
	// On Windows, removing write permission sets FILE_ATTRIBUTE_READONLY.
	std::error_code Error;
	std::filesystem::permissions(ToPath(Path), std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write,
								 bReadOnly ? std::filesystem::perm_options::remove : std::filesystem::perm_options::add, Error);
	return !Error;
}

bool FFile::Delete(const FString& Path, bool bEvenIfReadOnly)
{
	if (!Exists(Path))
		return true;
	if (bEvenIfReadOnly && IsReadOnly(Path))
		SetReadOnly(Path, false);
	std::error_code Error;
	std::filesystem::remove(ToPath(Path), Error);
	return !Exists(Path);
}

bool FFile::ReadAllBytes(const FString& Path, TArray<uint8>& OutBytes)
{
	FFile File;
	if (!File.Open(Path, EFileOpenMode::Read))
		return false;
	const int64 Size = File.Size();
	if (Size < 0)
		return false;
	OutBytes.resize(static_cast<size_t>(Size));
	return File.Read(OutBytes.data(), Size) == Size;
}

bool FFile::ReadAllText(const FString& Path, FString& OutText)
{
	TArray<uint8> Bytes;
	if (!ReadAllBytes(Path, Bytes))
		return false;

	// Skip a UTF-8 byte order mark.
	size_t Start = 0;
	if (Bytes.size() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
		Start = 3;
	OutText = FString(std::string_view(reinterpret_cast<const char*>(Bytes.data()) + Start, Bytes.size() - Start));
	return true;
}

bool FFile::WriteAllText(const FString& Path, const FString& Text)
{
	FFile File;
	return File.Open(Path, EFileOpenMode::Write) && File.Write(Text);
}
