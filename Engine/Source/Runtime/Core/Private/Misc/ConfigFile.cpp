// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Misc/ConfigFile.h"
#include "Misc/ValidationContext.h"
#include "HAL/File.h"

const FConfigEntry* FConfigSection::Find(std::string_view Key) const
{
	for (const FConfigEntry& Entry : Entries)
		if (Entry.Key.Equals(Key, ESearchCase::IgnoreCase))
			return &Entry;
	return nullptr;
}

const FConfigSection* FConfigFile::FindSection(std::string_view Name) const
{
	for (const FConfigSection& Section : Sections)
		if (Section.Name.Equals(Name))
			return &Section;
	return nullptr;
}

bool FConfigFile::Parse(const FString& Text, const FString& InSourceName, FConfigFile& Out, FString& Error)
{
	FValidationContext Validation(Error);
	if (!Validation.Check(
	        Text.Len() <= MaxBytes, FTextLocalization::Format("{} is larger than {} bytes.", InSourceName, MaxBytes)))
	{
		return false;
	}
	std::string_view All = Text.View();
	if (All.starts_with("\xEF\xBB\xBF"))
		All.remove_prefix(3);
	FConfigFile Result;
	Result.SourceName = InSourceName;
	size_t Start = 0;
	int32 Line = 0;
	while (Start < All.size())
	{
		size_t End = All.find('\n', Start);
		if (End == std::string_view::npos)
			End = All.size();
		const FString Trimmed = FString(All.substr(Start, End - Start)).TrimStartAndEnd();
		Start = End + 1;
		++Line;
		const std::string_view View = Trimmed.View();
		if (View.empty() || View[0] == ';' || View[0] == '#')
			continue;
		const TFunction<bool(const FString&)> Fail = [&](const FString& Message)
		{ return Validation.Fail(FTextLocalization::Format("{}({}): {}", InSourceName, Line, Message)); };
		if (View.front() == '[')
		{
			const FString Name =
			    View.back() == ']' ? FString(View.substr(1, View.size() - 2)).TrimStartAndEnd() : FString();
			if (Name.IsEmpty())
				return Fail(GENESIS_TEXT("Expected a section name in [brackets]."));
			if (Result.FindSection(Name.View()))
				return Fail(FTextLocalization::Format("Section [{}] appears twice.", Name));
			if (Result.Sections.size() >= static_cast<size_t>(MaxSections))
				return Fail(GENESIS_TEXT("Too many sections."));
			Result.Sections.push_back({Name, Line, {}});
			continue;
		}
		const size_t Equals = View.find('=');
		if (Equals == std::string_view::npos)
			return Fail(GENESIS_TEXT("Expected Key = Value, a [Section] or a ; comment."));
		if (Result.Sections.empty())
			return Fail(GENESIS_TEXT("A key appears before any [Section]."));
		FConfigEntry Entry;
		Entry.Key = FString(View.substr(0, Equals)).TrimStartAndEnd();
		Entry.Value = FString(View.substr(Equals + 1)).TrimStartAndEnd();
		Entry.Line = Line;
		const std::string_view Value = Entry.Value.View();
		if (Value.size() >= 2 && Value.front() == '"' && Value.back() == '"')
			Entry.Value = FString(Value.substr(1, Value.size() - 2));
		FConfigSection& Section = Result.Sections.back();
		if (Entry.Key.IsEmpty())
			return Fail(GENESIS_TEXT("A key name is empty."));
		if (Section.Find(Entry.Key.View()))
			return Fail(FTextLocalization::Format("{} is set twice in [{}].", Entry.Key, Section.Name));
		if (Section.Entries.size() >= static_cast<size_t>(MaxEntriesPerSection))
			return Fail(FTextLocalization::Format("Too many keys in [{}].", Section.Name));
		Section.Entries.push_back(std::move(Entry));
	}
	Out = std::move(Result);
	Error.Empty();
	return true;
}

bool FConfigFile::Load(const FString& Path, FConfigFile& Out, FString& Error)
{
	FString Text;
	if (!FFile::ReadAllText(Path, Text))
	{
		Error = FTextLocalization::Format("Could not read {}.", Path);
		return false;
	}
	return Parse(Text, Path, Out, Error);
}
