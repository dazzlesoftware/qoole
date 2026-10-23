// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once

#include "Containers/String.h"

// Source strings identify messages independently of file and line changes.
// Install a catalog before constructing UI; identifiers and serialized values
// must never be passed through this service.
class FTextLocalization
{
public:
	static FString Get(std::string_view Source);
	static FString Get(const char* Source)
	{
		return Get(std::string_view(Source ? Source : ""));
	}
	static FString Get(const FString& Source)
	{
		return Get(Source.View());
	}
	// JSON object mapping English source strings to translated UTF-8 strings.
	// A failed load preserves the current catalog.
	static bool LoadCatalog(const FString& Path, FString& Error);
	static void Reset();

	template <typename... Args> static FString Format(std::format_string<Args...> Source, Args&&... Arguments)
	{
		const FString Pattern = Get(Source.get());
		// Catalogs are external data. A malformed translated format must not
		// prevent the original diagnostic from being reported.
		try
		{
			return FString(std::vformat(Pattern.View(), std::make_format_args(Arguments...)));
		}
		catch (const std::format_error&)
		{
			return FString(std::format(Source, std::forward<Args>(Arguments)...));
		}
	}
};

#define GENESIS_TEXT(Source) ::FTextLocalization::Get(Source)
