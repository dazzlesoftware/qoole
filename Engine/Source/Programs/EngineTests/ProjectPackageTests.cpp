// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "ProjectDescriptor.h"
#include "AssetPackage.h"
#include "HAL/File.h"
#include <filesystem>
#include <chrono>

namespace
{
	// Test outputs remain in the OS temp directory; never delete user paths.
	FString Scratch(const char* Name)
	{
		static int Sequence = 0;
		const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto Dir = std::filesystem::temp_directory_path() /
			FString::Format("GenesisPackages-{}-{}", Stamp, ++Sequence).ToWide();
		std::filesystem::create_directories(Dir);
		return FString::FromWide((Dir / Name).wstring());
	}
	FAssetPackage Example()
	{
		FAssetPackage Package;
		Package.Kind = EAssetPackageKind::Map;
		Package.Schema = "Genesis.World";
		Package.Exports["Actors"] = FJsonValue::MakeArray();
		Package.Exports["Actors"].Push("Terrain α");
		Package.Bulk = { 0, 1, 128, 255 };
		Package.Dependencies = { "/Game/Materials/Grass" };
		return Package;
	}
}

TEST_SUITE("ProjectsPackages")
{
	TEST_CASE("Project descriptors validate versions and preserve extension fields")
	{
		FProjectDescriptor Project;
		Project.Name = "Landscape World";
		Project.DefaultMap = "/Game/Maps/Main";
		Project.Extensions["Plugins"] = FJsonValue::MakeArray();
		Project.Extensions["Plugins"].Push("FutureLandscapeTools");
		FString Error;
		auto Json = Project.ToJson();
		const auto Back = FProjectDescriptor::FromJson(Json, Error);
		REQUIRE(Back);
		CHECK(Back->ToJson() == Json);
		Json["FileVersion"] = 99;
		CHECK_FALSE(FProjectDescriptor::FromJson(Json, Error));
		Json["FileVersion"] = 1;
		Json["DefaultMap"] = "/Game/../Outside";
		CHECK_FALSE(FProjectDescriptor::FromJson(Json, Error));
		CHECK_FALSE(Error.IsEmpty());
	}

	TEST_CASE("Projects create a separate content tree and contain logical paths")
	{
		const FString Path = Scratch("World.gproject");
		FString Error, Resolved;
		const auto Project = FProjectContext::Create(Path, "My World", Error);
		REQUIRE_MESSAGE(Project, *Error);
		CHECK(FFile::DirectoryExists(Project->GetContentDir()));
		CHECK(FFile::DirectoryExists(Project->GetSavedDir()));
		CHECK(Project->ResolvePackage("/Game/Maps/Main", true, Resolved, Error));
		CHECK(Resolved.EndsWith("Main.gmap"));
		for (const char* Invalid : { "/Game/../Outside", "/Engine/Foo", "/Game/C:/Foo", "/Game/a\\b", "/Game//Foo", "/Game/Foo/" })
			CHECK_FALSE(Project->ResolvePackage(Invalid, true, Resolved, Error));
		CHECK_FALSE(FProjectContext::Create(Path, "Overwrite", Error));
		const auto Back = FProjectContext::Open(Path, Error);
		REQUIRE(Back);
		CHECK(Back->GetDescriptor().Name == "My World");
		FProjectContext::SetActive(*Back);
		REQUIRE(FProjectContext::GetActive());
		CHECK(FProjectContext::GetActive()->GetFilePath() == Back->GetFilePath());
		FProjectContext::SetActive({});
	}

	TEST_CASE("Native package generations retain identity and round trip exports and bulk")
	{
		const FString Path = Scratch("Terrain.gmap");
		FString Error;
		auto Package = Example();
		REQUIRE_MESSAGE(FAssetPackageStore::Save(Path, Package, Error), *Error);
		CHECK_FALSE(Package.Id.IsEmpty());
		const FString Id = Package.Id;
		FAssetPackage Loaded;
		REQUIRE_MESSAGE(FAssetPackageStore::Load(Path, Loaded, Error), *Error);
		CHECK(Loaded.Id == Id);
		CHECK(Loaded.Exports == Package.Exports);
		CHECK(Loaded.Bulk == Package.Bulk);
		CHECK(Loaded.Dependencies == Package.Dependencies);
		Package.Exports["Revision"] = 2;
		REQUIRE(FAssetPackageStore::Save(Path, Package, Error));
		CHECK(Package.Id == Id);
		REQUIRE(FAssetPackageStore::Load(Path, Loaded, Error));
		CHECK(Loaded.Exports.Get("Revision").AsInt() == 2);
		// Invalid saves leave the last committed generation readable.
		Package.SchemaVersion = 0;
		CHECK_FALSE(FAssetPackageStore::Save(Path, Package, Error));
		REQUIRE(FAssetPackageStore::Load(Path, Loaded, Error));
		CHECK(Loaded.Exports.Get("Revision").AsInt() == 2);
	}

	TEST_CASE("Asset extension and empty bulk are supported")
	{
		auto Package = Example();
		Package.Kind = EAssetPackageKind::Asset;
		Package.Schema = "Genesis.Material";
		Package.Bulk.clear();
		FString Error;
		CHECK_FALSE(FAssetPackageStore::Save(Scratch("Wrong.gmap"), Package, Error));
		const FString Path = Scratch("Grass.asset");
		REQUIRE(FAssetPackageStore::Save(Path, Package, Error));
		FAssetPackage Loaded;
		REQUIRE(FAssetPackageStore::Load(Path, Loaded, Error));
		CHECK(Loaded.Kind == EAssetPackageKind::Asset);
		CHECK(Loaded.Bulk.empty());
	}

	TEST_CASE("Damaged or malicious packages fail without changing the caller")
	{
		const FString Path = Scratch("Terrain.gmap");
		FString Error, Text;
		auto Package = Example();
		REQUIRE(FAssetPackageStore::Save(Path, Package, Error));
		REQUIRE(FFile::ReadAllText(Path, Text));
		const auto Manifest = FJsonValue::Parse(Text.View());
		REQUIRE(Manifest);
		auto Bad = *Manifest;
		Bad["Exports"]["File"] = "../outside.gexp";
		REQUIRE(FFile::WriteAllText(Path, Bad.ToString()));
		FAssetPackage Output;
		Output.Schema = "unchanged";
		CHECK_FALSE(FAssetPackageStore::Load(Path, Output, Error));
		CHECK(Output.Schema == "unchanged");
		Bad = *Manifest;
		Bad["FormatVersion"] = 2;
		REQUIRE(FFile::WriteAllText(Path, Bad.ToString()));
		CHECK_FALSE(FAssetPackageStore::Load(Path, Output, Error));
		REQUIRE(FFile::WriteAllText(Path, Manifest->ToString()));
		const auto Sidecar = std::filesystem::path(Path.ToWide()).parent_path() /
			Manifest->Get("Bulk").Get("File").AsString().ToWide();
		REQUIRE(FFile::WriteAllText(FString::FromWide(Sidecar.wstring()), "xxxx"));
		CHECK_FALSE(FAssetPackageStore::Load(Path, Output, Error));
		CHECK(Output.Schema == "unchanged");
		CHECK(Error.Contains("corrupt"));
	}
}
