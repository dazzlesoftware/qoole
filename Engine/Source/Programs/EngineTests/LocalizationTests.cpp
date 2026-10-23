// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "Internationalization/TextLocalization.h"
#include "HAL/File.h"
#include <filesystem>

TEST_CASE("Localization catalogs replace display text atomically" * doctest::test_suite("Core"))
{
	FTextLocalization::Reset();
	CHECK(GENESIS_TEXT("Save") == "Save");
	const std::filesystem::path Directory = std::filesystem::temp_directory_path() / "GenesisLocalizationTests";
	std::filesystem::create_directories(Directory);
	const FString Path = FString::FromWide((Directory / "catalog.json").wstring());
	FString Error;
	REQUIRE(FFile::WriteAllText(Path, R"({"Save":"Enregistrer","Open":"Ouvrir"})"));
	REQUIRE(FTextLocalization::LoadCatalog(Path, Error));
	CHECK(Error.IsEmpty());
	CHECK(GENESIS_TEXT("Save") == "Enregistrer");
	CHECK(GENESIS_TEXT("Unknown") == "Unknown");
	REQUIRE(FFile::WriteAllText(Path, R"({"Save":42})"));
	CHECK_FALSE(FTextLocalization::LoadCatalog(Path, Error));
	CHECK_FALSE(Error.IsEmpty());
	CHECK(GENESIS_TEXT("Save") == "Enregistrer");
	REQUIRE(FFile::WriteAllText(Path, R"({"Value {}":"Valeur {missing}"})"));
	REQUIRE(FTextLocalization::LoadCatalog(Path, Error));
	CHECK(FTextLocalization::Format("Value {}", 3) == "Value 3");
	CHECK_FALSE(FTextLocalization::LoadCatalog(Path + ".missing", Error));
	CHECK(FTextLocalization::Format("Value {}", 3) == "Value 3");
	FTextLocalization::Reset();
	CHECK(GENESIS_TEXT("Save") == "Save");
	std::filesystem::remove(Directory / "catalog.json");
	std::filesystem::remove(Directory);
}
