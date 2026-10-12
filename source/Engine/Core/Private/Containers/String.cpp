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

#include "Containers/String.h"
#include "Misc/StringFormat.h"

#include <cstdarg>

namespace
{
	char ToLowerAscii(char Ch)
	{
		return (Ch >= 'A' && Ch <= 'Z') ? static_cast<char>(Ch - 'A' + 'a') : Ch;
	}

	bool EqualsRange(std::string_view A, std::string_view B, ESearchCase Case)
	{
		if (A.size() != B.size())
			return false;
		if (Case == ESearchCase::CaseSensitive)
			return A == B;
		for (size_t i = 0; i < A.size(); i++)
		{
			if (ToLowerAscii(A[i]) != ToLowerAscii(B[i]))
				return false;
		}
		return true;
	}

	bool IsSpace(char8_t Ch)
	{
		return Ch == ' ' || Ch == '\t' || Ch == '\r' || Ch == '\n';
	}
}

FString::FString(const char* Utf8)
{
	if (Utf8)
		Data.assign(reinterpret_cast<const char8_t*>(Utf8));
}

FString::FString(const char8_t* Utf8)
{
	if (Utf8)
		Data.assign(Utf8);
}

FString::FString(std::string_view Utf8)
	: Data(reinterpret_cast<const char8_t*>(Utf8.data()), Utf8.size())
{
}

bool FString::Equals(const FString& Other, ESearchCase Case) const
{
	return EqualsRange(View(), Other.View(), Case);
}

bool FString::StartsWith(const FString& Prefix, ESearchCase Case) const
{
	return Prefix.Data.size() <= Data.size()
		&& EqualsRange(View().substr(0, Prefix.Data.size()), Prefix.View(), Case);
}

bool FString::EndsWith(const FString& Suffix, ESearchCase Case) const
{
	return Suffix.Data.size() <= Data.size()
		&& EqualsRange(View().substr(Data.size() - Suffix.Data.size()), Suffix.View(), Case);
}

int32 FString::Find(const FString& Sub) const
{
	const size_t Pos = Data.find(Sub.Data);
	return Pos == std::u8string::npos ? -1 : static_cast<int32>(Pos);
}

FString FString::TrimStartAndEnd() const
{
	size_t Start = 0;
	size_t End = Data.size();
	while (Start < End && IsSpace(Data[Start]))
		Start++;
	while (End > Start && IsSpace(Data[End - 1]))
		End--;
	return FString(Data.substr(Start, End - Start));
}

FString FString::Printf(const char* Format, ...)
{
	va_list Args;
	va_start(Args, Format);
	FString Result(Genesis::FormatV(Format, Args));
	va_end(Args);
	return Result;
}
