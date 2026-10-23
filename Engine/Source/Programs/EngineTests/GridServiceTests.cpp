// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "Misc/ConfigFile.h"
#include "World/GridService.h"
#include "World/RegionConfig.h"
#include "HAL/File.h"
#include <chrono>
#include <filesystem>

namespace
{
	FConfigFile Parse(const char* Text)
	{
		FConfigFile File;
		FString Error;
		REQUIRE_MESSAGE(FConfigFile::Parse(Text, "Test.ini", File, Error), *Error);
		return File;
	}
	TArray<FRegionConfig> Regions(const char* Text)
	{
		TArray<FRegionConfig> Out;
		FString Error;
		REQUIRE_MESSAGE(FRegionConfigReader::Read(Parse(Text), Out, Error), *Error);
		return Out;
	}
	bool RegionsFail(const char* Text, FString& Error)
	{
		TArray<FRegionConfig> Out;
		const bool bRead = FRegionConfigReader::Read(Parse(Text), Out, Error);
		CHECK(Out.empty());
		return !bRead;
	}
	FWorldCompositionTile Region(const char* Name, int32 X, int32 Y)
	{
		return FWorldCompositionTile::New(Name, {FAssetId::New(), FString::Format("/Game/Regions/{}", Name)}, X, Y);
	}
	FGridServiceSettings Settings(const char* Text)
	{
		FGridServiceSettings Out;
		FString Error;
		REQUIRE_MESSAGE(FGridServiceSettings::FromConfig(*Parse(Text).FindSection("GridService"), Out, Error), *Error);
		return Out;
	}
} // namespace

TEST_SUITE("GridService")
{
	TEST_CASE("Config files keep order and line numbers, and reject malformed lines without output")
	{
		const FConfigFile File = Parse("\xEF\xBB\xBF; header\r\n"
		                               "[First Region]\r\n"
		                               "# comment\r\n"
		                               "Key = value ; not a comment\r\n"
		                               "Quoted = \"kept ; and # inside\"\r\n"
		                               "\r\n"
		                               "[Second]\n"
		                               "empty =\n");
		REQUIRE(File.GetSections().size() == 2);
		const FConfigSection& First = File.GetSections()[0];
		CHECK(First.Name == "First Region");
		CHECK(First.Line == 2);
		REQUIRE(First.Find("KEY"));
		CHECK(First.Find("key")->Value == "value ; not a comment");
		CHECK(First.Find("key")->Line == 4);
		CHECK(First.Find("Quoted")->Value == "kept ; and # inside");
		CHECK(File.FindSection("Second")->Find("Empty")->Value.IsEmpty());
		CHECK_FALSE(File.FindSection("second"));

		FConfigFile Out = File;
		FString Error;
		for (const char* Bad : {"Key = before any section\n", "[A]\nno equals sign\n", "[A]\n[A]\n", "[]\n",
		         "[A]\nKey = 1\nkey = 2\n", "[Unclosed\n", "[A]\n = value\n"})
		{
			CHECK_FALSE(FConfigFile::Parse(Bad, "Bad.ini", Out, Error));
			CHECK(Error.StartsWith("Bad.ini("));
			CHECK(Out.GetSections().size() == 2);
		}
		CHECK_FALSE(FConfigFile::Parse("[A]\nx = 1\nno equals\n", "Bad.ini", Out, Error));
		CHECK(Error.StartsWith("Bad.ini(3)"));
	}

	TEST_CASE("Region files read OpenSim-style sections and report unused keys")
	{
		const TArray<FRegionConfig> Read = Regions("[Hills]\n"
		                                           "RegionUUID = 7C1E0C8A-1111-4222-8333-444455556666\n"
		                                           "Location = 1000, -3\n"
		                                           "SizeX = 2\n"
		                                           "Map = /Game/Regions/Hills\n"
		                                           "InternalAddress = 0.0.0.0\n"
		                                           "ExternalHostName = 127.0.0.1\n"
		                                           "InternalPort = 9000\n"
		                                           "MaxAgents = 100\n"
		                                           "[Coast]\n"
		                                           "RegionUUID = 11111111-2222-3333-4444-555555555555\n"
		                                           "Location = 1002,1000\n"
		                                           "Map = /Game/Regions/Coast\n");
		REQUIRE(Read.size() == 2);
		const FWorldCompositionTile& Hills = Read[0].Tile;
		CHECK(Hills.Name == "Hills");
		CHECK(Hills.Id == "7c1e0c8a-1111-4222-8333-444455556666");
		CHECK(Hills.X == 1000);
		CHECK(Hills.Y == -3);
		CHECK(Hills.SizeX == 2);
		CHECK(Hills.SizeY == 1);
		CHECK(Hills.Map.Path == "/Game/Regions/Hills");
		CHECK(Hills.Map.Id.IsEmpty());
		CHECK(Hills.Host == "127.0.0.1:9000");
		CHECK(Read[0].Line == 1);
		REQUIRE(Read[0].Warnings.size() == 2);
		CHECK(Read[0].Warnings[0].View().find("InternalAddress") != std::string_view::npos);
		CHECK(Read[1].Tile.Host.IsEmpty());
		CHECK(Read[1].Line == 10);

		FString Error;
		const char* Valid = "RegionUUID = 11111111-2222-3333-4444-555555555555\nLocation = 1,1\nMap = /Game/R\n";
		CHECK(RegionsFail("[R]\nLocation = 1,1\nMap = /Game/R\n", Error));
		CHECK(Error.View().find("RegionUUID is required") != std::string_view::npos);
		CHECK(Error.StartsWith("Test.ini(1): [R]"));
		CHECK(RegionsFail("[R]\nRegionUUID = not-a-uuid\nLocation = 1,1\nMap = /Game/R\n", Error));
		CHECK(Error.StartsWith("Test.ini(2)"));
		for (const char* Bad : {"Location = 1\n", "Location = 1,2,3\n", "Location = a,b\n", "Location = 1.5,2\n",
		         "Location = 99999999999,1\n"})
		{
			const FString Text =
			    FString::Format("[R]\nRegionUUID = 11111111-2222-3333-4444-555555555555\n{}Map = /Game/R\n", Bad);
			CHECK(RegionsFail(*Text, Error));
		}
		for (const char* Bad : {"SizeX = 0\n", "SizeY = 65\n", "Map2 = x\nMap = Regions/R\n", "ExternalHostName = h\n",
		         "InternalPort = 9000\n", "ExternalHostName = h\nInternalPort = 70000\n",
		         "ExternalHostName = h:1\nInternalPort = 9\n"})
		{
			FString Text = FString::Format("[R]\n{}{}", Valid, Bad);
			if (std::string_view(Bad).find("Map = Regions") != std::string_view::npos)
				Text =
				    FString::Format("[R]\nRegionUUID = 11111111-2222-3333-4444-555555555555\nLocation = 1,1\n{}", Bad);
			CHECK(RegionsFail(*Text, Error));
		}
		// One bad section rejects the whole file.
		CHECK(RegionsFail("[Good]\nRegionUUID = 11111111-2222-3333-4444-555555555555\nLocation = 1,1\nMap = /Game/G\n"
		                  "[Bad]\nLocation = 2,2\n",
		    Error));
	}

	TEST_CASE("A region folder loads its good files and reports the others")
	{
		const std::filesystem::path Folder = std::filesystem::temp_directory_path() /
		    FString::Format("GenesisRegions-{}", std::chrono::steady_clock::now().time_since_epoch().count()).ToWide();
		std::filesystem::create_directories(Folder);
		const TFunction<void(const char*, const char*)> Write = [&](const char* Name, const char* Text)
		{ REQUIRE(FFile::WriteAllText(FString::FromWide((Folder / Name).wstring()), Text)); };
		Write("B.ini", "[Coast]\nRegionUUID = 22222222-2222-3333-4444-555555555555\nLocation = 1,0\nMap = /Game/C\n");
		Write("A.INI", "[Hills]\nRegionUUID = 11111111-2222-3333-4444-555555555555\nLocation = 0,0\nMap = /Game/H\n");
		Write("C.ini", "[Broken]\nLocation = 5,5\n");
		Write("Notes.txt", "[Ignored]\nLocation = 9,9\n");
		TArray<FRegionConfig> Read;
		TArray<FRegionConfigIssue> Issues;
		CHECK_FALSE(FRegionConfigReader::ReadDirectory(FString::FromWide(Folder.wstring()), Read, Issues));
		REQUIRE(Read.size() == 2);
		CHECK(Read[0].Tile.Name == "Hills");
		CHECK(Read[1].Tile.Name == "Coast");
		REQUIRE(Issues.size() == 1);
		CHECK(Issues[0].SourceFile.EndsWith("C.ini"));
		Issues.clear();
		CHECK_FALSE(
		    FRegionConfigReader::ReadDirectory(FString::FromWide((Folder / L"Missing").wstring()), Read, Issues));
		CHECK(Issues.size() == 1);
	}

	TEST_CASE("Grid service settings read OpenSim's region flags")
	{
		const FGridServiceSettings Read =
		    Settings("[GridService]\n"
		             "LocalServiceModule = \"OpenSim.Services.GridService.dll:GridService\"\n"
		             "AutoRegistration = false\n"
		             "DeleteOnUnregister = FALSE\n"
		             "Region_Hills = \"DefaultRegion, FallbackRegion\"\n"
		             "Region_11111111-2222-3333-4444-555555555555 = NoMove\n");
		CHECK_FALSE(Read.bAutoRegistration);
		CHECK_FALSE(Read.bDeleteOnUnregister);
		FWorldCompositionTile Hills = Region("Hills", 0, 0);
		CHECK(Read.GetFlags(Hills) == (ERegionFlags::DefaultRegion | ERegionFlags::FallbackRegion));
		Hills.Id = "11111111-2222-3333-4444-555555555555";
		CHECK(HasFlag(Read.GetFlags(Hills), ERegionFlags::NoMove));
		CHECK(HasFlag(Read.GetFlags(Hills), ERegionFlags::DefaultRegion));

		FGridServiceSettings Out;
		FString Error;
		for (const char* Bad : {"[GridService]\nRegion_A = Reservation\n", "[GridService]\nRegion_A = Authenticate\n",
		         "[GridService]\nRegion_A = Sometimes\n", "[GridService]\nRegion_ = DefaultRegion\n",
		         "[GridService]\nAutoRegistration = yes\n"})
			CHECK_FALSE(FGridServiceSettings::FromConfig(*Parse(Bad).FindSection("GridService"), Out, Error));
		CHECK(Error.View().find("AutoRegistration") != std::string_view::npos);
	}

	TEST_CASE("Registration brings regions online and applies the grid's rules atomically")
	{
		FWorldComposition World;
		FString Error;
		const FWorldCompositionTile Home = Region("Home", 0, 0), Locked = Region("Locked", 5, 5);
		REQUIRE(World.AddTile(Home, Error));
		REQUIRE(World.AddTile(Locked, Error));
		FGridService Grid(World,
		    Settings("[GridService]\n"
		             "Region_Home = DefaultRegion, NoMove\n"
		             "Region_Locked = LockedOut\n"
		             "Region_Kept = Persistent, FallbackRegion\n"));
		CHECK_FALSE(Grid.IsOnline(Home.Id));
		REQUIRE_MESSAGE(Grid.RegisterRegion(Home, Error), *Error);
		CHECK(Grid.IsOnline(Home.Id));
		CHECK_FALSE(Grid.IsModified());
		FWorldCompositionTile Moved = Home;
		Moved.X = 3;
		CHECK_FALSE(Grid.RegisterRegion(Moved, Error));
		CHECK(Grid.GetWorld().FindTile(Home.Id)->X == 0);
		CHECK_FALSE(Grid.RegisterRegion(Locked, Error));
		CHECK(Error.View().find("locked out") != std::string_view::npos);
		CHECK_FALSE(Grid.IsOnline(Locked.Id));

		// A region server may change a region's host and name; it keeps its identity.
		FWorldCompositionTile Hosted = Home;
		Hosted.Host = "127.0.0.1:9000";
		REQUIRE(Grid.RegisterRegion(Hosted, Error));
		CHECK(Grid.GetWorld().FindTile(Home.Id)->Host == "127.0.0.1:9000");
		CHECK(Grid.IsModified());

		// New regions register themselves unless they overlap or reuse a name.
		const FWorldCompositionTile Kept = Region("Kept", 1, 0), Visitor = Region("Visitor", 2, 0);
		REQUIRE(Grid.RegisterRegion(Kept, Error));
		REQUIRE(Grid.RegisterRegion(Visitor, Error));
		CHECK_FALSE(Grid.RegisterRegion(Region("Overlap", 2, 0), Error));
		CHECK_FALSE(Grid.RegisterRegion(Region("Home", 9, 9), Error));
		FWorldCompositionTile PathOnly = Region("PathOnly", 9, 9);
		PathOnly.Map.Id.Empty();
		CHECK_FALSE(Grid.RegisterRegion(PathOnly, Error));
		CHECK(Grid.GetWorld().GetTiles().size() == 4);

		TArray<const FWorldCompositionTile*> Found;
		Grid.GetRegionsWithFlag(ERegionFlags::DefaultRegion, Found);
		REQUIRE(Found.size() == 1);
		CHECK(Found[0]->Id == Home.Id);
		Grid.GetRegionsWithFlag(ERegionFlags::FallbackRegion, Found);
		REQUIRE(Found.size() == 1);
		CHECK(Found[0]->Id == Kept.Id);

		// Unregistering: auto-registered regions leave unless persistent; registry regions stay.
		REQUIRE(Grid.DeregisterRegion(Visitor.Id, Error));
		CHECK_FALSE(Grid.GetWorld().FindTile(Visitor.Id));
		REQUIRE(Grid.DeregisterRegion(Kept.Id, Error));
		CHECK(Grid.GetWorld().FindTile(Kept.Id));
		CHECK_FALSE(Grid.IsOnline(Kept.Id));
		REQUIRE(Grid.DeregisterRegion(Home.Id, Error));
		CHECK(Grid.GetWorld().FindTile(Home.Id));
		CHECK_FALSE(Grid.DeregisterRegion(Home.Id, Error));
		Grid.GetRegionsWithFlag(ERegionFlags::DefaultRegion, Found);
		CHECK(Found.empty());

		FGridService Closed(World, Settings("[GridService]\nAutoRegistration = false\n"));
		CHECK_FALSE(Closed.RegisterRegion(Region("Stranger", 7, 7), Error));
		CHECK(Error.View().find("automatic registration is off") != std::string_view::npos);
		REQUIRE(Closed.RegisterRegion(Home, Error));
		FGridService Keeping(World, Settings("[GridService]\nDeleteOnUnregister = false\n"));
		REQUIRE(Keeping.RegisterRegion(Visitor, Error));
		REQUIRE(Keeping.DeregisterRegion(Visitor.Id, Error));
		CHECK(Keeping.GetWorld().FindTile(Visitor.Id));
	}
}
