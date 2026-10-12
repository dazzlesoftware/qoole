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

#include "CoreTypes.h"

#include <compare>
#include <string>
#include <string_view>

enum class ESearchCase : uint8
{
	CaseSensitive,
	IgnoreCase,	// ASCII letters only
};

// UTF-8 string backed by std::u8string (docs/GenesisUI.md section 2).
//
// Legacy code in Basic/Objects/Render/Game builds with the multibyte
// character set, so its char strings are in the ANSI code page, not UTF-8.
// Convert at the boundary with FromAnsi() and ToAnsi().
class FString
{
public:
	FString() = default;
	FString(const char* Utf8);	// nullptr gives an empty string
	FString(const char8_t* Utf8);
	FString(std::string_view Utf8);
	FString(std::u8string Utf8) : Data(std::move(Utf8)) {}

	// The text as a null-terminated UTF-8 C string.
	const char* operator*() const { return reinterpret_cast<const char*>(Data.c_str()); }
	std::string_view View() const { return std::string_view(**this, Data.size()); }
	std::string ToStdString() const { return std::string(View()); }
	const std::u8string& GetData() const { return Data; }

	// Length in bytes (UTF-8 code units).
	int32 Len() const { return static_cast<int32>(Data.size()); }
	bool IsEmpty() const { return Data.empty(); }
	void Empty() { Data.clear(); }

	FString& operator+=(const FString& Other) { Data += Other.Data; return *this; }
	FString& operator+=(const char* Utf8) { return *this += FString(Utf8); }
	FString& operator+=(char Ch) { Data += static_cast<char8_t>(Ch); return *this; }

	friend FString operator+(FString Lhs, const FString& Rhs) { Lhs += Rhs; return Lhs; }

	bool operator==(const FString& Other) const = default;
	std::strong_ordering operator<=>(const FString& Other) const = default;

	bool Equals(const FString& Other, ESearchCase Case = ESearchCase::CaseSensitive) const;
	bool StartsWith(const FString& Prefix, ESearchCase Case = ESearchCase::CaseSensitive) const;
	bool EndsWith(const FString& Suffix, ESearchCase Case = ESearchCase::CaseSensitive) const;

	// Byte index of the first match, or -1.
	int32 Find(const FString& Sub) const;

	// Leading and trailing spaces, tabs and line breaks removed.
	FString TrimStartAndEnd() const;

	// printf-style formatting; arguments are UTF-8.
	static FString Printf(const char* Format, ...);

	// Conversions for platform APIs.
	std::wstring ToWide() const;
	static FString FromWide(std::wstring_view Wide);
	std::string ToAnsi() const;
	static FString FromAnsi(std::string_view Ansi);

private:
	std::u8string Data;
};

template<>
struct std::hash<FString>
{
	size_t operator()(const FString& Str) const noexcept { return std::hash<std::u8string>()(Str.GetData()); }
};
