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

#include "doctest.h"
#include "TestUtils.h"

#include "LCommon.h"
#include "Services/Services.h"

#include <cstring>

namespace
{
	// Makes Store the config service for one test.
	struct FScopedConfig
	{
		explicit FScopedConfig(IConfigStore& Store) { FServices::SetConfig(&Store); }
		~FScopedConfig() { FServices::SetConfig(nullptr); }
	};

	// The variables a typical LConfig section holds, one of each kind.
	struct FSettings
	{
		int Grid = 16;
		unsigned int Flags = 3;
		bool bShow = true;
		unsigned short Small = 7;
		float Gamma = 0.7f;
		char Driver[8] = "MFC";

		void Register(LConfig& Config)
		{
			Config.RegisterVar("Grid", &Grid, LVAR_INT, 1, 8192);
			Config.RegisterVar("Flags", &Flags, LVAR_INT);
			Config.RegisterVar("Show", &bShow, LVAR_INT);
			Config.RegisterVar("Small", &Small, LVAR_INT);
			Config.RegisterVar("Gamma", &Gamma, LVAR_FLOAT);
			Config.RegisterVar("Driver", &Driver, LVAR_STR);
		}
	};
}

TEST_CASE("FMemoryConfigStore keeps strings and integers")
{
	FMemoryConfigStore Store;
	FString Value = "unchanged";
	int32 Number = -1;
	CHECK_FALSE(Store.GetString("Draw", "Driver", Value));
	CHECK(Value == FString("unchanged"));
	CHECK_FALSE(Store.GetInt("Draw", "Grid", Number));
	CHECK(Number == -1);

	Store.SetString("Draw", "Driver", "DX12");
	Store.SetInt("Draw", "Grid", 32);
	CHECK(Store.GetString("Draw", "Driver", Value));
	CHECK(Value == FString("DX12"));
	CHECK(Store.GetInt("Draw", "Grid", Number));
	CHECK(Number == 32);

	// Sections are separate.
	CHECK_FALSE(Store.GetString("Other", "Driver", Value));

	// Text that is not a number is not an integer.
	Store.SetString("Draw", "Grid", "lots");
	CHECK_FALSE(Store.GetInt("Draw", "Grid", Number));
}

TEST_CASE("FIniConfigStore round-trips through a file")
{
	FTempDir Temp;
	const FString Path = Temp.File("Settings.ini");

	{
		FIniConfigStore Store(Path);
		CHECK_FALSE(Store.Load());	// no file yet
		Store.SetString("Draw", "Driver", u8"Vulk\u00e4n");
		Store.SetInt("Draw", "GridStep1", 16);
		Store.SetString("_LConfig", "Version", "5");
		Store.SetString("Paths", "Quake", "C:\\Games\\Quake");
		REQUIRE(Store.Flush());
	}

	FString Text;
	REQUIRE(FFile::ReadAllText(Path, Text));
	CHECK(Text == FString(u8"[Draw]\nDriver=Vulk\u00e4n\nGridStep1=16\n\n[Paths]\nQuake=C:\\Games\\Quake\n\n[_LConfig]\nVersion=5\n"));

	FIniConfigStore Loaded(Path);
	REQUIRE(Loaded.Load());
	FString Value;
	int32 Number = 0;
	CHECK(Loaded.GetString("Draw", "Driver", Value));
	CHECK(Value == FString(u8"Vulk\u00e4n"));
	CHECK(Loaded.GetInt("Draw", "GridStep1", Number));
	CHECK(Number == 16);
	CHECK(Loaded.GetString("Paths", "Quake", Value));
	CHECK(Value == FString("C:\\Games\\Quake"));

	// Comments, blank lines and spaces around names are tolerated.
	Loaded.FromText("; comment\n\n[ Section ]\n Key = Value \n");
	CHECK(Loaded.GetString("Section", "Key", Value));
	CHECK(Value == FString("Value"));
}

TEST_CASE("LConfig saves and loads its variables through the config service")
{
	FMemoryConfigStore Store;
	FScopedConfig Scope(Store);

	LConfig::Init(5, 5);
	{
		FSettings Settings;
		Settings.Grid = 64;
		Settings.Flags = 5;
		Settings.bShow = false;
		Settings.Small = 65535;
		Settings.Gamma = 1.25f;
		std::strcpy(Settings.Driver, "DX12");

		LConfig Config("Draw");
		Settings.Register(Config);
		Config.SaveVars();
	}
	LConfig::Exit();

	int32 Version = 0;
	CHECK(Store.GetInt("_LConfig", "Version", Version));
	CHECK(Version == 5);

	// A new session loads what the last one saved.
	LConfig::Init(5, 5);
	FSettings Loaded;
	LConfig Config("Draw");
	Loaded.Register(Config);
	CHECK(Loaded.Grid == 64);
	CHECK(Loaded.Flags == 5u);
	CHECK(Loaded.bShow == false);
	CHECK(Loaded.Small == 65535);
	CHECK(Loaded.Gamma == doctest::Approx(1.25f));
	CHECK(std::strcmp(Loaded.Driver, "DX12") == 0);
}

TEST_CASE("LConfig ignores bad and outdated settings")
{
	FMemoryConfigStore Store;
	FScopedConfig Scope(Store);

	Store.SetString("Draw", "Grid", "0");			// below minVal
	Store.SetString("Draw", "Show", "256");			// an old 4-byte bool: only the low byte counts
	Store.SetString("Draw", "Small", "131080");		// an old 4-byte WORD: only the low 16 bits count
	Store.SetString("Draw", "Driver", "Glide3x");	// longer than the 8-byte buffer
	Store.SetString("Draw", "Gamma", "");			// empty means unset
	Store.SetInt("_LConfig", "Version", 5);

	LConfig::Init(5, 5);
	{
		FSettings Settings;
		LConfig Config("Draw");
		Settings.Register(Config);
		CHECK(Settings.Grid == 16);
		CHECK(Settings.bShow == false);
		CHECK(Settings.Small == 8);
		CHECK(std::strcmp(Settings.Driver, "Glide3x") == 0);
		CHECK(Settings.Gamma == doctest::Approx(0.7f));
	}

	Store.SetString("Draw", "Driver", "TooLongName");
	{
		FSettings Settings;
		LConfig Config("Draw");
		Settings.Register(Config);
		CHECK(std::strcmp(Settings.Driver, "TooLong") == 0);	// truncated to fit
	}

	// Settings saved by a version older than the required one are skipped.
	LConfig::Init(6, 6);
	{
		Store.SetString("Draw", "Grid", "128");
		FSettings Settings;
		LConfig Config("Draw");
		Settings.Register(Config);
		CHECK(Settings.Grid == 16);
	}
}
