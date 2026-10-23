// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Containers/ContainerAliases.h"
#include "Containers/String.h"

// An authored INI file, read-only and in file order, with line numbers for
// diagnostics (as Unreal's FConfigFile; FIniConfigStore keeps editor settings).
//
//   ; comment            # comment
//   [Section Name]
//   Key = value
//   Quoted = "value with ; and # kept"
//
// Keys match case-insensitively; section names match exactly. A key outside a
// section, a line that is neither, an empty or duplicate section, or a
// duplicate key in one section is an error.
struct FConfigEntry
{
	FString Key, Value;
	int32 Line = 0;
};

struct FConfigSection
{
	FString Name;
	int32 Line = 0;
	TArray<FConfigEntry> Entries;
	const FConfigEntry* Find(std::string_view Key) const;
};

class FConfigFile
{
public:
	static constexpr int32 MaxBytes = 1024 * 1024;
	static constexpr int32 MaxSections = 4096;
	static constexpr int32 MaxEntriesPerSection = 1024;
	// On failure Out is unchanged and Error names the line.
	static bool Parse(const FString& Text, const FString& SourceName, FConfigFile& Out, FString& Error);
	static bool Load(const FString& Path, FConfigFile& Out, FString& Error);
	const FString& GetSourceName() const
	{
		return SourceName;
	}
	const TArray<FConfigSection>& GetSections() const
	{
		return Sections;
	}
	const FConfigSection* FindSection(std::string_view Name) const;

private:
	FString SourceName;
	TArray<FConfigSection> Sections;
};
