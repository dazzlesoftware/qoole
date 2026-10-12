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

// FString conversions to and from the Windows wide and ANSI encodings.

#include "Containers/String.h"
#include "Windows/WindowsPlatform.h"

namespace
{
	std::wstring MultiByteToWide(UINT CodePage, std::string_view Text)
	{
		if (Text.empty())
			return std::wstring();
		const int Length = MultiByteToWideChar(CodePage, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
		std::wstring Result(static_cast<size_t>(Length), L'\0');
		MultiByteToWideChar(CodePage, 0, Text.data(), static_cast<int>(Text.size()), Result.data(), Length);
		return Result;
	}

	std::string WideToMultiByte(UINT CodePage, std::wstring_view Text)
	{
		if (Text.empty())
			return std::string();
		const int Length = WideCharToMultiByte(CodePage, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0, nullptr, nullptr);
		std::string Result(static_cast<size_t>(Length), '\0');
		WideCharToMultiByte(CodePage, 0, Text.data(), static_cast<int>(Text.size()), Result.data(), Length, nullptr, nullptr);
		return Result;
	}
}

std::wstring FString::ToWide() const
{
	return MultiByteToWide(CP_UTF8, View());
}

FString FString::FromWide(std::wstring_view Wide)
{
	return FString(std::string_view(WideToMultiByte(CP_UTF8, Wide)));
}

std::string FString::ToAnsi() const
{
	return WideToMultiByte(CP_ACP, ToWide());
}

FString FString::FromAnsi(std::string_view Ansi)
{
	return FromWide(MultiByteToWide(CP_ACP, Ansi));
}
