// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FString: a UTF-8 string backed by std::u8string (Engine/Documentation/Source/GenesisUI.md §2).
//
// Plain char literals and std::string are accepted and assumed to be UTF-8
// (all modules compile with /utf-8). Lengths and indices are in bytes; use
// FUtf8 to walk code points. FString::Format uses std::format syntax.

#pragma once

#include "Containers/ContainerAliases.h"

#include <format>
#include <string>
#include <string_view>

namespace FUtf8
{
	// Decodes one code point starting at Cursor and advances it. Invalid
	// sequences decode to U+FFFD and advance by one byte.
	char32_t Decode(const char*& Cursor, const char* End);

	// Appends the UTF-8 encoding of CodePoint.
	void Encode(char32_t CodePoint, std::string& Out);

	// Number of code points.
	int32 CountCodePoints(std::string_view Text);
} // namespace FUtf8

enum class ESearchCase : uint8
{
	CaseSensitive,
	IgnoreCase,
};

class FString
{
public:
	FString() = default;
	FString(const char* Text) : Data(Text ? reinterpret_cast<const char8_t*>(Text) : u8"") {}
	FString(const char8_t* Text) : Data(Text ? Text : u8"") {}
	FString(const char* Text, int32 Length) : Data(reinterpret_cast<const char8_t*>(Text), static_cast<SIZE_T>(Length))
	{
	}
	FString(std::string_view Text) : Data(reinterpret_cast<const char8_t*>(Text.data()), Text.size()) {}
	FString(std::u8string_view Text) : Data(Text) {}
	FString(const std::string& Text) : FString(std::string_view(Text)) {}
	FString(std::u8string Text) : Data(std::move(Text)) {}

	template <typename... Args> static FString Format(std::format_string<Args...> Fmt, Args&&... InArgs)
	{
		return FString(std::format(Fmt, std::forward<Args>(InArgs)...));
	}

	// printf-style formatting, for code ported from printf/sprintf.
	static FString Printf(const char* Fmt, ...);

	// UTF-16 conversion for OS calls.
	static FString FromWide(std::wstring_view Text);
	std::wstring ToWide() const;

	// Conversion to and from the ANSI code page. The map core (Basic,
	// Objects, Render, Game) builds with the multibyte character set, so its
	// char strings are ANSI, not UTF-8; convert at that boundary.
	static FString FromAnsi(std::string_view Text);
	std::string ToAnsi() const;

	const char* operator*() const
	{
		return reinterpret_cast<const char*>(Data.c_str());
	}
	const char* c_str() const
	{
		return **this;
	}
	std::string_view View() const
	{
		return std::string_view(**this, Data.size());
	}
	std::string ToStdString() const
	{
		return std::string(View());
	}
	const std::u8string& Utf8() const
	{
		return Data;
	}

	int32 Len() const
	{
		return static_cast<int32>(Data.size());
	}
	bool IsEmpty() const
	{
		return Data.empty();
	}
	void Empty()
	{
		Data.clear();
	}
	void Reserve(int32 Capacity)
	{
		Data.reserve(static_cast<SIZE_T>(Capacity));
	}

	char operator[](int32 Index) const
	{
		return static_cast<char>(Data[static_cast<SIZE_T>(Index)]);
	}

	FString& Append(std::string_view Text);
	FString& operator+=(std::string_view Text)
	{
		return Append(Text);
	}
	FString& operator+=(const FString& Other)
	{
		return Append(Other.View());
	}
	FString& operator+=(const char* Text)
	{
		return Append(std::string_view(Text));
	}
	FString& operator+=(char Character);

	friend FString operator+(FString A, const FString& B)
	{
		A += B;
		return A;
	}
	friend FString operator+(FString A, const char* B)
	{
		A += std::string_view(B);
		return A;
	}
	friend FString operator+(const char* A, const FString& B)
	{
		FString R(A);
		R += B;
		return R;
	}

	friend bool operator==(const FString& A, const FString& B)
	{
		return A.Data == B.Data;
	}
	friend bool operator==(const FString& A, const char* B)
	{
		return A.View() == std::string_view(B);
	}
	friend std::strong_ordering operator<=>(const FString& A, const FString& B)
	{
		return A.Data <=> B.Data;
	}

	// Byte index of the first occurrence at or after Start, or INDEX_NONE.
	int32 Find(std::string_view Sub, int32 Start = 0) const;
	bool Contains(std::string_view Sub) const
	{
		return Find(Sub) != INDEX_NONE;
	}
	bool StartsWith(std::string_view Prefix) const
	{
		return View().starts_with(Prefix);
	}
	bool EndsWith(std::string_view Suffix) const
	{
		return View().ends_with(Suffix);
	}

	// IgnoreCase compares ASCII letters without case.
	bool Equals(std::string_view Other, ESearchCase Case = ESearchCase::CaseSensitive) const;
	bool StartsWith(std::string_view Prefix, ESearchCase Case) const;
	bool EndsWith(std::string_view Suffix, ESearchCase Case) const;

	FString Mid(int32 Start, int32 Count = INT32_MAX) const;
	FString Left(int32 Count) const
	{
		return Mid(0, Count);
	}
	FString Right(int32 Count) const
	{
		return Mid(Count >= Len() ? 0 : Len() - Count);
	}

	// ASCII-only case conversion.
	FString ToUpper() const;
	FString ToLower() const;
	FString TrimStartAndEnd() const;

	// Splits on Delimiter; empty pieces are dropped when bCullEmpty.
	TArray<FString> ParseIntoArray(std::string_view Delimiter, bool bCullEmpty = true) const;

	SIZE_T GetHash() const
	{
		return std::hash<std::u8string>{}(Data);
	}

private:
	std::u8string Data;
};

template <> struct std::hash<FString>
{
	SIZE_T operator()(const FString& S) const noexcept
	{
		return S.GetHash();
	}
};

template <> struct std::formatter<FString, char> : std::formatter<std::string_view, char>
{
	std::format_context::iterator format(const FString& S, std::format_context& Ctx) const
	{
		return std::formatter<std::string_view, char>::format(S.View(), Ctx);
	}
};
