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

#include "Services/ConfigStore.h"
#include "HAL/File.h"

#include <algorithm>
#include <charconv>

// ========== FMemoryConfigStore ==========

bool FMemoryConfigStore::GetString(const FString& Section, const FString& Key, FString& OutValue) const
{
	const TMap<FString, FSection>::const_iterator SectionIt = Sections.find(Section);
	if (SectionIt == Sections.end())
		return false;
	const FSection::const_iterator KeyIt = SectionIt->second.find(Key);
	if (KeyIt == SectionIt->second.end())
		return false;
	OutValue = KeyIt->second;
	return true;
}

bool FMemoryConfigStore::GetInt(const FString& Section, const FString& Key, int32& OutValue) const
{
	FString Text;
	if (!GetString(Section, Key, Text))
		return false;

	const std::string_view View = Text.View();
	int32 Value = 0;
	const std::from_chars_result Result = std::from_chars(View.data(), View.data() + View.size(), Value);
	if (Result.ec != std::errc() || Result.ptr != View.data() + View.size())
		return false;
	OutValue = Value;
	return true;
}

void FMemoryConfigStore::SetString(const FString& Section, const FString& Key, const FString& Value)
{
	Sections[Section][Key] = Value;
}

void FMemoryConfigStore::SetInt(const FString& Section, const FString& Key, int32 Value)
{
	SetString(Section, Key, FString::Printf("%d", Value));
}

// ========== FIniConfigStore ==========

bool FIniConfigStore::Load()
{
	Clear();
	FString Text;
	if (!FFile::ReadAllText(Path, Text))
		return false;
	FromText(Text);
	return true;
}

bool FIniConfigStore::Flush()
{
	return FFile::WriteAllText(Path, ToText());
}

FString FIniConfigStore::ToText() const
{
	TArray<const FString*> SectionNames;
	for (const TPair<const FString, FSection>& Section : Sections)
		SectionNames.push_back(&Section.first);
	std::sort(SectionNames.begin(), SectionNames.end(), [](const FString* A, const FString* B) { return *A < *B; });

	FString Text;
	for (const FString* SectionName : SectionNames)
	{
		const FSection& Keys = Sections.at(*SectionName);
		TArray<const std::pair<const FString, FString>*> Entries;
		for (const std::pair<const FString, FString>& Entry : Keys)
			Entries.push_back(&Entry);
		std::sort(Entries.begin(), Entries.end(),
		    [](const TPair<const FString, FString>* A, const TPair<const FString, FString>* B)
		{ return A->first < B->first; });

		if (!Text.IsEmpty())
			Text += "\n";
		Text += "[" + *SectionName + "]\n";
		for (const std::pair<const FString, FString>* Entry : Entries)
			Text += Entry->first + "=" + Entry->second + "\n";
	}
	return Text;
}

void FIniConfigStore::FromText(const FString& Text)
{
	const std::string_view All = Text.View();
	FString Section;
	size_t Start = 0;
	while (Start < All.size())
	{
		size_t End = All.find('\n', Start);
		if (End == std::string_view::npos)
			End = All.size();
		const FString Line = FString(All.substr(Start, End - Start)).TrimStartAndEnd();
		Start = End + 1;

		const std::string_view View = Line.View();
		if (View.empty() || View[0] == ';' || View[0] == '#')
			continue;
		if (View.front() == '[' && View.back() == ']')
		{
			Section = FString(View.substr(1, View.size() - 2)).TrimStartAndEnd();
			continue;
		}
		const size_t Equals = View.find('=');
		if (Equals == std::string_view::npos)
			continue;
		SetString(Section, FString(View.substr(0, Equals)).TrimStartAndEnd(),
		    FString(View.substr(Equals + 1)).TrimStartAndEnd());
	}
}
