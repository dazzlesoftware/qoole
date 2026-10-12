// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Containers/Name.h"

#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace
{
	struct FNameTable
	{
		std::shared_mutex Mutex;
		// A deque keeps element addresses stable, so views stay valid.
		std::deque<std::string> Entries{ std::string() };
		std::unordered_map<std::string_view, uint32> Lookup{ { std::string_view(), 0u } };
	};

	FNameTable& GetTable()
	{
		static FNameTable Table;
		return Table;
	}
}

FName::FName(std::string_view Text)
{
	if (Text.empty())
	{
		return;
	}

	FNameTable& Table = GetTable();
	{
		std::shared_lock Lock(Table.Mutex);
		if (auto It = Table.Lookup.find(Text); It != Table.Lookup.end())
		{
			Index = It->second;
			return;
		}
	}

	std::unique_lock Lock(Table.Mutex);
	if (auto It = Table.Lookup.find(Text); It != Table.Lookup.end())
	{
		Index = It->second;
		return;
	}
	Index = static_cast<uint32>(Table.Entries.size());
	const std::string& Stored = Table.Entries.emplace_back(Text);
	Table.Lookup.emplace(std::string_view(Stored), Index);
}

std::string_view FName::View() const
{
	FNameTable& Table = GetTable();
	std::shared_lock Lock(Table.Mutex);
	return Table.Entries[Index];
}
