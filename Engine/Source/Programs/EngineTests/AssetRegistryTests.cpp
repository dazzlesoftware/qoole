// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "AssetRegistry.h"
#include "HAL/File.h"
#include <chrono>
#include <filesystem>

namespace
{
	FString RegistryFolder()
	{
		static int Sequence = 0;
		const std::filesystem::path Directory = std::filesystem::temp_directory_path() /
		    FString::Format(
		        "GenesisRegistry-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(), ++Sequence)
		        .ToWide();
		std::filesystem::create_directories(Directory);
		return FString::FromWide(Directory.wstring());
	}
	FString FileIn(const FString& Directory, const char* Name)
	{
		return FString::FromWide((std::filesystem::path(Directory.ToWide()) / Name).wstring());
	}
	FAssetPackage RegistryPackage(const char* Schema = "Genesis.Prefab")
	{
		FAssetPackage Package;
		Package.Schema = Schema;
		Package.Exports["Label"] = "An asset";
		Package.Bulk = {1, 2, 3, 4};
		return Package;
	}
} // namespace

TEST_SUITE("AssetRegistry")
{
	TEST_CASE("Metadata discovery validates headers without opening bulk data")
	{
		const FString Root = RegistryFolder(), File = FileIn(Root, "Sample.asset");
		FAssetPackage Package = RegistryPackage();
		FString Error, Text;
		REQUIRE(FAssetPackageStore::Save(File, Package, Error));
		FAssetPackageMetadata Metadata;
		REQUIRE(FAssetPackageStore::ReadMetadata(File, Metadata, Error));
		CHECK(Metadata.Id == Package.Id);
		CHECK(Metadata.Schema == "Genesis.Prefab");
		CHECK(Metadata.BulkBytes == 4);
		CHECK(Metadata.ExportBytes > 0);
		REQUIRE(FFile::ReadAllText(File, Text));
		const TOptional<FJsonValue> Manifest = FJsonValue::Parse(Text.View());
		REQUIRE(Manifest);
		const FString Bulk = FileIn(Root, *Manifest->Get("Bulk").Get("File").AsString());
		REQUIRE(FFile::WriteAllText(Bulk, "damaged"));
		CHECK(FAssetPackageStore::ReadMetadata(File, Metadata, Error));
		FAssetPackage Loaded;
		Loaded.Schema = "unchanged";
		CHECK_FALSE(FAssetPackageStore::Load(File, Loaded, Error));
		CHECK(Loaded.Schema == "unchanged");
		FJsonValue Bad = *Manifest;
		Bad["Bulk"]["File"] = "../outside.gbulk";
		REQUIRE(FFile::WriteAllText(File, Bad.ToString()));
		CHECK_FALSE(FAssetPackageStore::ReadMetadata(File, Metadata, Error));
		CHECK(Metadata.Id == Package.Id);
	}

	TEST_CASE("Mounted packages resolve by identity and path and filter schema")
	{
		const FString Root = RegistryFolder();
		FString Error;
		FAssetPackage Prefab = RegistryPackage(), Material = RegistryPackage("Genesis.Material");
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Structures/House.asset"), Prefab, Error));
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Materials/Grass.asset"), Material, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", Root}}, Error));
		REQUIRE(Registry.Scan());
		REQUIRE(Registry.GetAssets().size() == 2);
		const FAssetData* House = Registry.FindByPath("/game/structures/house");
		REQUIRE(House);
		CHECK(House->Metadata.Id == Prefab.Id);
		CHECK(Registry.FindById(Prefab.Id) == House);
		CHECK(Registry.FindByFile(FileIn(Root, "Structures/House.asset")) == House);
		const FAssetReference Ref = House->GetReference();
		CHECK(Registry.Resolve(Ref, Error, "Genesis.Prefab") == House);
		CHECK_FALSE(Registry.Resolve(Ref, Error, "Genesis.Material"));
		CHECK(Error.Contains("expected"));
		FAssetPackage Back;
		REQUIRE(Registry.Load(Ref, Back, Error, "Genesis.Prefab"));
		CHECK(Back.Id == Prefab.Id);
		CHECK(Back.Bulk == Prefab.Bulk);
		CHECK(Registry.Resolve({{}, "/Game/Structures/House"}, Error));
		const TOptional<FAssetReference> RoundTrip = FAssetReference::FromJson(Ref.ToJson(), Error);
		REQUIRE(RoundTrip);
		CHECK(RoundTrip->Id == Ref.Id);
		CHECK(RoundTrip->Path == Ref.Path);
	}

	TEST_CASE("References survive package moves but never fall back from a missing identity")
	{
		const FString Root = RegistryFolder(), OldFile = FileIn(Root, "Old.asset"), NewFile = FileIn(Root, "New.asset");
		FString Error;
		FAssetPackage Original = RegistryPackage();
		REQUIRE(FAssetPackageStore::Save(OldFile, Original, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", Root}}, Error));
		REQUIRE(Registry.Scan());
		const FAssetReference Ref = Registry.FindById(Original.Id)->GetReference();
		// Package-aware move: rewrite its sidecars and retain UUID, then retire
		// the old manifest. The registry does not rename only a manifest.
		REQUIRE(FAssetPackageStore::Save(NewFile, Original, Error));
		REQUIRE(FFile::Delete(OldFile));
		REQUIRE(Registry.Scan());
		const FAssetData* Moved = Registry.Resolve(Ref, Error);
		REQUIRE(Moved);
		CHECK(Moved->PackagePath == "/Game/New");
		REQUIRE(FFile::Delete(NewFile));
		FAssetPackage Replacement = RegistryPackage();
		REQUIRE(FAssetPackageStore::Save(OldFile, Replacement, Error));
		REQUIRE(Registry.Scan());
		CHECK_FALSE(Registry.Resolve(Ref, Error));
		CHECK(Registry.Resolve({{}, Ref.Path}, Error)->Metadata.Id == Replacement.Id);
	}

	TEST_CASE("Duplicate UUIDs and logical names are diagnosed without an arbitrary winner")
	{
		const FString Root = RegistryFolder();
		FString Error;
		FAssetPackage Original = RegistryPackage();
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "First.asset"), Original, Error));
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Second.asset"), Original, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", Root}}, Error));
		CHECK_FALSE(Registry.Scan());
		CHECK(Registry.GetIssues().size() == 2);
		CHECK_FALSE(Registry.FindById(Original.Id));
		CHECK_FALSE(Registry.Resolve({Original.Id, "/Game/First"}, Error));
		CHECK_FALSE(Registry.Resolve({{}, "/Game/First"}, Error));
		REQUIRE(FFile::Delete(FileIn(Root, "Second.asset")));
		FAssetPackage World = RegistryPackage("Genesis.World");
		World.Kind = EAssetPackageKind::Map;
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "First.gmap"), World, Error));
		CHECK_FALSE(Registry.Scan());
		CHECK_FALSE(Registry.FindByPath("/Game/First"));
		CHECK(Registry.GetIssues().size() == 2);
		// Explicit UUID references can still distinguish different kinds.
		CHECK(Registry.Resolve({World.Id, {}}, Error, "Genesis.World"));
	}

	TEST_CASE("Invalid mount changes preserve snapshots and scans report malformed content")
	{
		const FString Root = RegistryFolder();
		FString Error;
		FAssetPackage Package = RegistryPackage();
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Valid.asset"), Package, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", Root}}, Error));
		REQUIRE(Registry.Scan());
		const uint64 Revision = Registry.GetRevision();
		CHECK_FALSE(Registry.SetMounts({{"/Bad/Root", Root}}, Error));
		CHECK_FALSE(Registry.SetMounts({{"/Game", Root}, {"/Engine", FileIn(Root, "Nested")}}, Error));
		CHECK_FALSE(Registry.SetMounts({{"/Game", Root}, {"/game", RegistryFolder()}}, Error));
		CHECK(Registry.GetRevision() == Revision);
		REQUIRE(Registry.FindById(Package.Id));
		REQUIRE(FFile::WriteAllText(FileIn(Root, "Broken.asset"), "not a package"));
		CHECK_FALSE(Registry.Scan());
		CHECK(Registry.GetIssues().size() == 1);
		CHECK(Registry.FindById(Package.Id));
		CHECK(Registry.GetRevision() > Revision);
		REQUIRE(Registry.SetMounts({{"/Game", Root}, {"/Missing", FileIn(RegistryFolder(), "DoesNotExist")}}, Error));
		CHECK_FALSE(Registry.Scan());
		CHECK(Registry.FindById(Package.Id));
	}

	TEST_CASE("Reference validation rejects traversal and stale discovery cannot load a replacement")
	{
		FString Error;
		for (const char* Path :
		    {"Game/Asset", "/Game/../Asset", "/Game//Asset", "/Game/C:/Asset", "/Game/a\\b", "/Game/Trailing/"})
		{
			CHECK_FALSE(FAssetRegistry::IsPackagePath(Path));
			CHECK_FALSE(FAssetReference::FromJson(FAssetReference{{}, Path}.ToJson(), Error));
		}
		CHECK(FAssetRegistry::IsPackagePath("/Game/Maps/My World"));
		CHECK(FAssetRegistry::IsPackagePath("/Game/Terrain/山"));
		CHECK(FAssetId::IsValid(FAssetId::New()));
		CHECK_FALSE(FAssetReference::FromJson(FAssetReference{"bad-id", "/Game/Asset"}.ToJson(), Error));
		const FString Root = RegistryFolder(), File = FileIn(Root, "Asset.asset");
		FAssetPackage Package = RegistryPackage();
		REQUIRE(FAssetPackageStore::Save(File, Package, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", Root}}, Error));
		REQUIRE(Registry.Scan());
		const FAssetReference Ref = Registry.FindById(Package.Id)->GetReference();
		FAssetPackage Replacement = RegistryPackage();
		REQUIRE(FAssetPackageStore::Save(File, Replacement, Error));
		FAssetPackage Output;
		Output.Schema = "unchanged";
		CHECK_FALSE(Registry.Load(Ref, Output, Error));
		CHECK(Output.Schema == "unchanged");
		CHECK(Error.Contains("refresh"));
	}
}
