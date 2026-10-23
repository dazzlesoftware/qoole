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

// Persistent settings, replacing CWinApp::Get/WriteProfile*.
//
// Settings are grouped in sections of key/value pairs. Strings and integers
// are separate calls because a store may keep them differently (the MFC
// profile writes integers to the registry as DWORDs and strings as REG_SZ).

#include "Containers/String.h"

class IConfigStore
{
public:
	virtual ~IConfigStore() = default;

	// Return false, leaving OutValue unchanged, if the value is not stored.
	virtual bool GetString(const FString& Section, const FString& Key, FString& OutValue) const = 0;
	virtual bool GetInt(const FString& Section, const FString& Key, int32& OutValue) const = 0;

	virtual void SetString(const FString& Section, const FString& Key, const FString& Value) = 0;
	virtual void SetInt(const FString& Section, const FString& Key, int32 Value) = 0;

	// Writes pending changes to permanent storage.
	virtual bool Flush()
	{
		return true;
	}
};

// Settings kept in memory only. The default store when the application has
// not registered one, and the base of FIniConfigStore.
class FMemoryConfigStore : public IConfigStore
{
public:
	bool GetString(const FString& Section, const FString& Key, FString& OutValue) const override;
	bool GetInt(const FString& Section, const FString& Key, int32& OutValue) const override;
	void SetString(const FString& Section, const FString& Key, const FString& Value) override;
	void SetInt(const FString& Section, const FString& Key, int32 Value) override;

	void Clear()
	{
		Sections.clear();
	}

protected:
	using FSection = TMap<FString, FString>;
	TMap<FString, FSection> Sections;
};

// Settings in a UTF-8 INI file:
//
//   [Section]
//   Key=Value
//
// The file is read by Load() and written by Flush(). Sections and keys are
// written in sorted order.
class FIniConfigStore : public FMemoryConfigStore
{
public:
	explicit FIniConfigStore(FString InPath) : Path(std::move(InPath)) {}

	const FString& GetPath() const
	{
		return Path;
	}

	// Replaces the current contents with the file's. A missing file loads as
	// empty and returns false.
	bool Load();
	bool Flush() override;

	// The INI text, without touching the file.
	FString ToText() const;
	void FromText(const FString& Text);

private:
	FString Path;
};
