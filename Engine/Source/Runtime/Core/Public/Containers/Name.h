// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FName: an interned string id. Construction looks the string up in a global
// table (thread-safe); copies and comparisons are a single integer. Names are
// case-sensitive. The empty name is NAME_None (index 0).

#pragma once

#include "Containers/UnrealString.h"

class FName
{
public:
	FName() = default;
	FName(std::string_view Text);
	FName(const char* Text) : FName(std::string_view(Text ? Text : "")) {}
	FName(const FString& Text) : FName(Text.View()) {}

	bool IsNone() const { return Index == 0; }
	uint32 GetIndex() const { return Index; }

	// The interned text; valid for the lifetime of the program.
	std::string_view View() const;
	FString ToString() const { return FString(View()); }

	friend bool operator==(FName A, FName B) { return A.Index == B.Index; }
	// Orders by index (creation order), not alphabetically.
	friend auto operator<=>(FName A, FName B) { return A.Index <=> B.Index; }

private:
	uint32 Index = 0;
};

inline const FName NAME_None;

template <>
struct std::hash<FName>
{
	SIZE_T operator()(FName N) const noexcept { return std::hash<uint32>{}(N.GetIndex()); }
};

template <>
struct std::formatter<FName, char> : std::formatter<std::string_view, char>
{
	auto format(FName N, std::format_context& Ctx) const
	{
		return std::formatter<std::string_view, char>::format(N.View(), Ctx);
	}
};
