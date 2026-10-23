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

// FString conversions to and from the Windows ANSI code page.

#include "Containers/String.h"
#include "Windows/WindowsPlatform.h"

FString FString::FromAnsi(std::string_view Text)
{
	if (Text.empty())
		return FString();
	const int Length = MultiByteToWideChar(CP_ACP, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
	std::wstring Wide(static_cast<size_t>(Length), L'\0');
	MultiByteToWideChar(CP_ACP, 0, Text.data(), static_cast<int>(Text.size()), Wide.data(), Length);
	return FromWide(Wide);
}

std::string FString::ToAnsi() const
{
	const std::wstring Wide = ToWide();
	if (Wide.empty())
		return std::string();
	const int Length =
	    WideCharToMultiByte(CP_ACP, 0, Wide.data(), static_cast<int>(Wide.size()), nullptr, 0, nullptr, nullptr);
	std::string Result(static_cast<size_t>(Length), '\0');
	WideCharToMultiByte(CP_ACP, 0, Wide.data(), static_cast<int>(Wide.size()), Result.data(), Length, nullptr, nullptr);
	return Result;
}
