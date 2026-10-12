// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Containers/GenesisString.h"

#include <cctype>

namespace FUtf8
{
	char32_t Decode(const char*& Cursor, const char* End)
	{
		const auto Byte = [](const char* P) { return static_cast<uint8>(*P); };
		const uint8 C0 = Byte(Cursor);
		int32 Extra = 0;
		char32_t CodePoint = 0;
		if (C0 < 0x80) { ++Cursor; return C0; }
		else if ((C0 & 0xE0) == 0xC0) { Extra = 1; CodePoint = C0 & 0x1F; }
		else if ((C0 & 0xF0) == 0xE0) { Extra = 2; CodePoint = C0 & 0x0F; }
		else if ((C0 & 0xF8) == 0xF0) { Extra = 3; CodePoint = C0 & 0x07; }
		else { ++Cursor; return 0xFFFD; }

		if (End - Cursor <= Extra)
		{
			++Cursor;
			return 0xFFFD;
		}
		for (int32 I = 1; I <= Extra; ++I)
		{
			const uint8 C = Byte(Cursor + I);
			if ((C & 0xC0) != 0x80)
			{
				++Cursor;
				return 0xFFFD;
			}
			CodePoint = (CodePoint << 6) | (C & 0x3F);
		}
		Cursor += Extra + 1;
		return CodePoint;
	}

	void Encode(char32_t CodePoint, std::string& Out)
	{
		if (CodePoint < 0x80)
		{
			Out += static_cast<char>(CodePoint);
		}
		else if (CodePoint < 0x800)
		{
			Out += static_cast<char>(0xC0 | (CodePoint >> 6));
			Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
		}
		else if (CodePoint < 0x10000)
		{
			Out += static_cast<char>(0xE0 | (CodePoint >> 12));
			Out += static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
			Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
		}
		else
		{
			Out += static_cast<char>(0xF0 | (CodePoint >> 18));
			Out += static_cast<char>(0x80 | ((CodePoint >> 12) & 0x3F));
			Out += static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
			Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
		}
	}

	int32 CountCodePoints(std::string_view Text)
	{
		int32 Count = 0;
		const char* Cursor = Text.data();
		const char* End = Cursor + Text.size();
		while (Cursor < End)
		{
			Decode(Cursor, End);
			++Count;
		}
		return Count;
	}
}

FString FString::FromWide(std::wstring_view Text)
{
	std::string Out;
	Out.reserve(Text.size());
	for (SIZE_T I = 0; I < Text.size(); ++I)
	{
		char32_t C = Text[I];
		if (C >= 0xD800 && C <= 0xDBFF && I + 1 < Text.size() && Text[I + 1] >= 0xDC00 && Text[I + 1] <= 0xDFFF)
		{
			C = 0x10000 + ((C - 0xD800) << 10) + (Text[I + 1] - 0xDC00);
			++I;
		}
		else if (C >= 0xD800 && C <= 0xDFFF)
		{
			C = 0xFFFD;
		}
		FUtf8::Encode(C, Out);
	}
	return FString(Out);
}

std::wstring FString::ToWide() const
{
	std::wstring Out;
	Out.reserve(Data.size());
	const char* Cursor = **this;
	const char* End = Cursor + Data.size();
	while (Cursor < End)
	{
		const char32_t C = FUtf8::Decode(Cursor, End);
		if (C >= 0x10000)
		{
			Out += static_cast<wchar_t>(0xD800 + ((C - 0x10000) >> 10));
			Out += static_cast<wchar_t>(0xDC00 + ((C - 0x10000) & 0x3FF));
		}
		else
		{
			Out += static_cast<wchar_t>(C);
		}
	}
	return Out;
}

FString& FString::Append(std::string_view Text)
{
	Data.append(reinterpret_cast<const char8_t*>(Text.data()), Text.size());
	return *this;
}

FString& FString::operator+=(char Character)
{
	Data += static_cast<char8_t>(Character);
	return *this;
}

int32 FString::Find(std::string_view Sub, int32 Start) const
{
	if (Start < 0 || Start > Len())
	{
		return INDEX_NONE;
	}
	const SIZE_T Pos = View().find(Sub, static_cast<SIZE_T>(Start));
	return Pos == std::string_view::npos ? INDEX_NONE : static_cast<int32>(Pos);
}

FString FString::Mid(int32 Start, int32 Count) const
{
	const int32 Length = Len();
	const int32 ClampedStart = Start < 0 ? 0 : (Start > Length ? Length : Start);
	const int32 Available = Length - ClampedStart;
	const int32 ClampedCount = Count < 0 ? 0 : (Count > Available ? Available : Count);
	return FString(std::u8string(Data, static_cast<SIZE_T>(ClampedStart), static_cast<SIZE_T>(ClampedCount)));
}

FString FString::ToUpper() const
{
	std::u8string Out = Data;
	for (char8_t& C : Out)
	{
		if (C >= u8'a' && C <= u8'z') C = static_cast<char8_t>(C - u8'a' + u8'A');
	}
	return FString(std::move(Out));
}

FString FString::ToLower() const
{
	std::u8string Out = Data;
	for (char8_t& C : Out)
	{
		if (C >= u8'A' && C <= u8'Z') C = static_cast<char8_t>(C - u8'A' + u8'a');
	}
	return FString(std::move(Out));
}

FString FString::TrimStartAndEnd() const
{
	const auto IsSpace = [](char8_t C) { return C == u8' ' || C == u8'\t' || C == u8'\r' || C == u8'\n'; };
	SIZE_T First = 0;
	SIZE_T Last = Data.size();
	while (First < Last && IsSpace(Data[First])) ++First;
	while (Last > First && IsSpace(Data[Last - 1])) --Last;
	return FString(std::u8string(Data, First, Last - First));
}

TArray<FString> FString::ParseIntoArray(std::string_view Delimiter, bool bCullEmpty) const
{
	TArray<FString> Out;
	const std::string_view Text = View();
	if (Delimiter.empty())
	{
		Out.emplace_back(*this);
		return Out;
	}
	SIZE_T Start = 0;
	while (true)
	{
		const SIZE_T Pos = Text.find(Delimiter, Start);
		const std::string_view Piece = Text.substr(Start, Pos == std::string_view::npos ? std::string_view::npos : Pos - Start);
		if (!Piece.empty() || !bCullEmpty)
		{
			Out.emplace_back(Piece);
		}
		if (Pos == std::string_view::npos)
		{
			break;
		}
		Start = Pos + Delimiter.size();
	}
	return Out;
}
