// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "World/WorldComposition.h"
#include "World/WorldCompositionStreaming.h"
#include "World/WorldGeometry.h"
#include "Landscape/Landscape.h"
#include <chrono>
#include <filesystem>
#include <limits>

namespace
{
	FAssetReference MapReference(const char* Name)
	{
		return {FAssetId::New(), FString::Format("/Game/Regions/{}", Name)};
	}
	FWorldCompositionTile Tile(const char* Name, int32 X, int32 Y, int32 SizeX = 1, int32 SizeY = 1)
	{
		FWorldCompositionTile Result = FWorldCompositionTile::New(Name, MapReference(Name), X, Y);
		Result.SizeX = SizeX;
		Result.SizeY = SizeY;
		return Result;
	}
	TArray<FString> Names(const TArray<const FWorldCompositionTile*>& Tiles)
	{
		TArray<FString> Out;
		for (const FWorldCompositionTile* Found : Tiles)
			Out.push_back(Found->Name);
		return Out;
	}
	Vector3d Translation(const Matrix44& Matrix)
	{
		Vector3d Point;
		Matrix.Transform(Point, Vector3d());
		return Point;
	}

	// Counts loads and fails the tiles it is told to.
	class FTestTileSource final : public IWorldTileSource
	{
	public:
		bool LoadTile(const FWorldCompositionTile& Tile, FWorldSceneSnapshot& Snapshot, FString& Error) override
		{
			(void)Snapshot;
			++Loads[Tile.Name.ToStdString()];
			if (Failing.contains(Tile.Name.ToStdString()))
			{
				Error = "unavailable";
				return false;
			}
			return true;
		}
		TMap<std::string, int32> Loads;
		TSet<std::string> Failing;
	};

	FString CompositionFolder()
	{
		static int Sequence = 0;
		const std::filesystem::path Directory = std::filesystem::temp_directory_path() /
		    FString::Format(
		        "GenesisComposition-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(), ++Sequence)
		        .ToWide();
		std::filesystem::create_directories(Directory);
		return FString::FromWide(Directory.wstring());
	}
	FString FileIn(const FString& Directory, const char* Name)
	{
		return FString::FromWide((std::filesystem::path(Directory.ToWide()) / Name).wstring());
	}
	// A region map holding one flat landscape actor.
	FAssetPackage RegionMap(int32 Quads)
	{
		FAssetPackage Package;
		Package.Kind = EAssetPackageKind::Map;
		Package.Schema = "Genesis.World";
		Package.SchemaVersion = 3;
		FWorld Scene;
		FString Error;
		FActor Ground = FActor::New("Ground");
		Landscape Terrain(Quads, Quads, 32);
		FActorComponent Component = FActorComponent::New("Genesis.Landscape");
		FJsonValue Data;
		REQUIRE(FWorldGeometryCodec::EncodeLandscape(Terrain, Package, Data, Error));
		Component.Properties["Data"] = Data;
		Ground.Components.push_back(Component);
		REQUIRE(Scene.AddActor(Ground, Error));
		Package.Exports["Scene"] = Scene.ToJson();
		return Package;
	}
} // namespace

TEST_SUITE("WorldComposition")
{
	TEST_CASE("Tiles register on the grid and overlapping or duplicate registrations leave it unchanged")
	{
		FWorldComposition World;
		FString Error;
		const FWorldCompositionTile Home = Tile("Home", 0, 0, 2, 2);
		REQUIRE(World.AddTile(Home, Error));
		REQUIRE(World.AddTile(Tile("East", 2, 0), Error));
		REQUIRE(World.AddTile(Tile("Corner", -1, -1), Error));
		REQUIRE(World.AddTile(Tile("FarWest", -200, 5), Error));

		CHECK_FALSE(World.AddTile(Tile("Inside", 1, 1), Error));
		CHECK(Error.View().find("Home") != std::string_view::npos);
		CHECK_FALSE(World.AddTile(Tile("Wide", -1, 1, 4, 1), Error));
		FWorldCompositionTile SameMap = Tile("SameMap", 10, 10);
		SameMap.Map = Home.Map;
		CHECK_FALSE(World.AddTile(SameMap, Error));
		SameMap.Map.Id.Empty();
		CHECK_FALSE(World.AddTile(SameMap, Error));
		FWorldCompositionTile SameId = Tile("SameId", 10, 10);
		SameId.Id = Home.Id;
		CHECK_FALSE(World.AddTile(SameId, Error));
		CHECK_FALSE(World.AddTile(Tile("Home", 10, 10), Error));
		CHECK_FALSE(World.AddTile(Tile("", 10, 10), Error));
		CHECK_FALSE(World.AddTile(Tile("TooBig", 10, 10, WorldCompositionLimits::MaxTileCells + 1, 1), Error));
		CHECK_FALSE(World.AddTile(Tile("Empty", 10, 10, 0, 1), Error));
		CHECK_FALSE(World.AddTile(Tile("Outside", 1 << 30, 0), Error));
		FWorldCompositionTile NoMap = Tile("NoMap", 10, 10);
		NoMap.Map = {};
		CHECK_FALSE(World.AddTile(NoMap, Error));
		CHECK(World.GetTiles().size() == 4);

		CHECK(World.FindTileAt(100, 100)->Name == "Home");
		CHECK(World.FindTileAt(World.GetCellSize() * 2 + 1, 1)->Name == "East");
		CHECK(World.FindTileAt(-1, -1)->Name == "Corner");
		CHECK(World.FindTileAt(-1, World.GetCellSize() + 1) == nullptr);
		CHECK(World.FindTileAt(-199.5 * World.GetCellSize(), 5.5 * World.GetCellSize())->Name == "FarWest");
		CHECK(World.FindTileByName("East")->Id == World.GetTiles()[1].Id);

		TArray<const FWorldCompositionTile*> Neighbours;
		World.GetNeighbours(Home.Id, Neighbours);
		// Corner only touches Home at a point, so it is not a neighbour.
		REQUIRE(Neighbours.size() == 1);
		CHECK(Neighbours[0]->Name == "East");

		const Vector3d Origin = World.GetTileOrigin(*World.FindTile(Home.Id));
		CHECK(Origin.GetX() == doctest::Approx(World.GetCellSize()));
		CHECK(Origin.GetY() == doctest::Approx(World.GetCellSize()));
	}

	TEST_CASE("Moving, renaming and removing tiles validate before committing")
	{
		FWorldComposition World;
		FString Error;
		const FWorldCompositionTile A = Tile("A", 0, 0), B = Tile("B", 1, 0);
		REQUIRE(World.AddTile(A, Error));
		REQUIRE(World.AddTile(B, Error));
		FWorldCompositionTile Moved = A;
		Moved.X = 1;
		CHECK_FALSE(World.UpdateTile(Moved, Error));
		CHECK(World.FindTile(A.Id)->X == 0);
		Moved.Name = "B";
		Moved.X = 5;
		CHECK_FALSE(World.UpdateTile(Moved, Error));
		Moved.Name = "Renamed";
		REQUIRE(World.UpdateTile(Moved, Error));
		CHECK(World.FindTileByName("A") == nullptr);
		CHECK(World.FindTileAt(5.5 * World.GetCellSize(), 1)->Id == A.Id);
		CHECK(World.FindTileAt(0.5 * World.GetCellSize(), 1) == nullptr);
		// Its own cells and map do not conflict with itself.
		Moved.SizeX = 2;
		REQUIRE(World.UpdateTile(Moved, Error));
		CHECK_FALSE(World.UpdateTile(Tile("Unknown", 20, 20), Error));

		REQUIRE(World.RemoveTile(B.Id, Error));
		CHECK_FALSE(World.RemoveTile(B.Id, Error));
		CHECK(World.FindTile(B.Id) == nullptr);
		CHECK(World.FindTile(A.Id)->Name == "Renamed");
		REQUIRE(World.AddTile(Tile("B again", 1, 0), Error));

		CHECK_FALSE(World.SetCellSize(0, Error));
		CHECK_FALSE(World.SetCellSize(std::numeric_limits<double>::infinity(), Error));
		REQUIRE(World.SetCellSize(512, Error));
		CHECK(World.FindTileAt(5.5 * 512, 1)->Id == A.Id);
	}

	TEST_CASE("Compositions round trip through packages and reject malformed registrations")
	{
		FWorldComposition World;
		FString Error;
		REQUIRE(World.SetCellSize(4096, Error));
		FWorldCompositionTile Hosted = Tile("Hosted", -3, 7, 2, 3);
		Hosted.Host = "region-01.example:7000";
		REQUIRE(World.AddTile(Tile("Home", 0, 0), Error));
		REQUIRE(World.AddTile(Hosted, Error));
		FAssetPackage Package;
		World.ToPackage(Package);
		CHECK(Package.Schema == "Genesis.WorldComposition");
		CHECK(Package.Kind == EAssetPackageKind::Map);
		REQUIRE(Package.Dependencies.size() == 2);
		CHECK(Package.Dependencies[1] == Hosted.Map.Path);

		FWorldComposition Back;
		REQUIRE_MESSAGE(FWorldComposition::FromPackage(Package, Back, Error), *Error);
		CHECK(Back.GetCellSize() == 4096);
		REQUIRE(Back.GetTiles().size() == 2);
		const FWorldCompositionTile* Found = Back.FindTile(Hosted.Id);
		REQUIRE(Found);
		CHECK(Found->Host == Hosted.Host);
		CHECK(Found->ToJson() == Hosted.ToJson());

		FAssetPackage Overlapping = Package;
		FJsonValue Twin = Hosted.ToJson();
		Twin["Id"] = FAssetId::New();
		Twin["Name"] = "Twin";
		Twin["Map"] = MapReference("Twin").ToJson();
		Overlapping.Exports["Composition"]["Tiles"].Push(Twin);
		CHECK_FALSE(FWorldComposition::FromPackage(Overlapping, Back, Error));
		CHECK(Error.View().find("Twin") != std::string_view::npos);
		CHECK(Back.GetTiles().size() == 2);

		for (const char* Field : {"X", "SizeX"})
		{
			FAssetPackage Bad = Package;
			Bad.Exports["Composition"]["Tiles"][0][Field] = 1.5;
			CHECK_FALSE(FWorldComposition::FromPackage(Bad, Back, Error));
		}
		FAssetPackage Bad = Package;
		Bad.Exports["Composition"]["Version"] = 99;
		CHECK_FALSE(FWorldComposition::FromPackage(Bad, Back, Error));
		Bad = Package;
		Bad.Schema = "Genesis.World";
		CHECK_FALSE(FWorldComposition::FromPackage(Bad, Back, Error));
		CHECK(Back.FindTile(Hosted.Id));
	}

	TEST_CASE("Distance queries return nearby tiles nearest first through the index and the full scan")
	{
		FWorldComposition World;
		FString Error;
		REQUIRE(World.SetCellSize(100, Error));
		for (int32 Y = -20; Y < 20; Y += 2)
			for (int32 X = -300; X < 300; X += 3)
				REQUIRE(World.AddTile(Tile(*FString::Format("T{}_{}", X, Y), X, Y, 2, 1), Error));
		TArray<const FWorldCompositionTile*> Near;
		World.GetTilesWithinDistance(50, 50, 0, Near);
		CHECK(Names(Near) == TArray<FString>{"T0_0"});
		World.GetTilesWithinDistance(50, 50, 160, Near);
		REQUIRE(Near.size() == 4);
		CHECK(Near[0]->Name == "T0_0");
		// Matches a brute-force scan, at a range the index covers and one that falls back.
		for (double Distance : {750.0, 1.0e7})
		{
			World.GetTilesWithinDistance(-1234, 321, Distance, Near);
			size_t Expected = 0;
			for (const FWorldCompositionTile& Each : World.GetTiles())
				Expected += World.GetTileBounds(Each).GetDistance(-1234, 321) <= Distance ? 1 : 0;
			CHECK(Near.size() == Expected);
			for (size_t I = 1; I < Near.size(); ++I)
				CHECK(World.GetTileBounds(*Near[I - 1]).GetDistance(-1234, 321) <=
				    World.GetTileBounds(*Near[I]).GetDistance(-1234, 321));
		}
		World.GetTilesWithinDistance(std::numeric_limits<double>::quiet_NaN(), 0, 100, Near);
		CHECK(Near.empty());
		World.GetTilesWithinDistance(1.0e300, 0, 100, Near);
		CHECK(Near.empty());
	}

	TEST_CASE("Streaming loads nearby tiles within budget and unloads them past the unload distance")
	{
		FWorldComposition World;
		FString Error;
		REQUIRE(World.SetCellSize(1000, Error));
		const FWorldCompositionTile Home = Tile("Home", 0, 0), East = Tile("East", 1, 0), Far = Tile("Far", 9, 0);
		REQUIRE(World.AddTile(Home, Error));
		REQUIRE(World.AddTile(East, Error));
		REQUIRE(World.AddTile(Far, Error));
		FTestTileSource Source;
		FWorldCompositionStreaming Streaming(Source);
		FWorldCompositionStreamingSettings Settings;
		Settings.LoadDistance = 1000;
		Settings.UnloadDistance = 500;
		CHECK_FALSE(Streaming.SetSettings(Settings, Error));
		Settings.UnloadDistance = 2000;
		Settings.MaxLoadsPerUpdate = 1;
		REQUIRE(Streaming.SetSettings(Settings, Error));

		FWorldCompositionStreamingChanges Changes;
		Streaming.Update(World, Vector3d(500, 500, 0), &Changes);
		CHECK(Changes.Loaded == TArray<FString>{Home.Id});
		Streaming.Update(World, Vector3d(500, 500, 0), &Changes);
		CHECK(Changes.Loaded == TArray<FString>{East.Id});
		Streaming.Update(World, Vector3d(500, 500, 0), &Changes);
		CHECK(Changes.Loaded.empty());
		CHECK_FALSE(Streaming.IsLoaded(Far.Id));
		const FStreamedWorldTile* Streamed = Streaming.FindTile(East.Id);
		REQUIRE(Streamed);
		REQUIRE(Streamed->Snapshot);
		CHECK(Translation(Streamed->ToWorld).GetX() == doctest::Approx(1500));
		CHECK(Translation(Streamed->ToWorld).GetY() == doctest::Approx(500));

		// Between the load and unload distances nothing changes.
		Streaming.Update(World, Vector3d(-1000, 500, 0), &Changes);
		CHECK(Changes.Unloaded.empty());
		CHECK(Streaming.IsLoaded(East.Id));
		Streaming.Update(World, Vector3d(-1500, 500, 0), &Changes);
		CHECK(Changes.Unloaded == TArray<FString>{East.Id});
		CHECK(Streaming.IsLoaded(Home.Id));

		// Moving a loaded tile moves it without reloading; removing it unloads it.
		FWorldCompositionTile Moved = Home;
		Moved.Y = -1;
		REQUIRE(World.UpdateTile(Moved, Error));
		Streaming.Update(World, Vector3d(-500, 0, 0), &Changes);
		CHECK(Translation(Streaming.FindTile(Home.Id)->ToWorld).GetY() == doctest::Approx(-500));
		CHECK(Source.Loads["Home"] == 1);
		REQUIRE(World.RemoveTile(Home.Id, Error));
		Streaming.Update(World, Vector3d(-500, 0, 0), &Changes);
		CHECK(Changes.Unloaded == TArray<FString>{Home.Id});
		CHECK(Streaming.GetTiles().empty());

		Streaming.Update(World, Vector3d(std::numeric_limits<float>::quiet_NaN(), 0, 0), &Changes);
		CHECK(Changes.Loaded.empty());
	}

	TEST_CASE("Failed tiles are not reloaded every update until retried or re-registered")
	{
		FWorldComposition World;
		FString Error;
		REQUIRE(World.SetCellSize(1000, Error));
		FWorldCompositionTile Broken = Tile("Broken", 0, 0);
		REQUIRE(World.AddTile(Broken, Error));
		FTestTileSource Source;
		Source.Failing.insert("Broken");
		FWorldCompositionStreaming Streaming(Source);
		FWorldCompositionStreamingChanges Changes;
		Streaming.Update(World, Vector3d(), &Changes);
		CHECK(Changes.Failed == TArray<FString>{Broken.Id});
		const FStreamedWorldTile* Streamed = Streaming.FindTile(Broken.Id);
		REQUIRE(Streamed);
		CHECK(Streamed->State == EWorldTileStreamingState::Failed);
		CHECK(Streamed->Error == "unavailable");
		CHECK_FALSE(Streamed->Snapshot);
		Streaming.Update(World, Vector3d(), &Changes);
		CHECK(Source.Loads["Broken"] == 1);
		CHECK(Changes.Unloaded.empty());

		Streaming.Retry(Broken.Id);
		Streaming.Update(World, Vector3d(), &Changes);
		CHECK(Source.Loads["Broken"] == 2);

		Source.Failing.clear();
		Broken.Map = MapReference("Fixed");
		REQUIRE(World.UpdateTile(Broken, Error));
		Streaming.Update(World, Vector3d(), &Changes);
		CHECK(Changes.Loaded == TArray<FString>{Broken.Id});
		CHECK(Streaming.IsLoaded(Broken.Id));
	}

	TEST_CASE("The local tile source streams independently saved region maps with their landscapes")
	{
		const FString Root = CompositionFolder();
		FString Error;
		FAssetPackage West = RegionMap(8), East = RegionMap(16);
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Regions/West.gmap"), West, Error));
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Regions/East.gmap"), East, Error));
		FAssetRegistry Registry;
		REQUIRE(Registry.SetMounts({{"/Game", Root}}, Error));
		REQUIRE(Registry.Scan());

		FWorldComposition World;
		REQUIRE(World.SetCellSize(512, Error));
		const FWorldCompositionTile WestTile =
		    FWorldCompositionTile::New("West", {West.Id, "/Game/Regions/West"}, -1, 0);
		const FWorldCompositionTile EastTile = FWorldCompositionTile::New("East", {East.Id, {}}, 0, 0);
		FWorldCompositionTile Hosted = FWorldCompositionTile::New("Hosted", MapReference("Hosted"), 1, 0);
		Hosted.Host = "region-02.example:7000";
		FWorldCompositionTile Missing = FWorldCompositionTile::New("Missing", MapReference("Missing"), -2, 0);
		for (const FWorldCompositionTile& Each : {WestTile, EastTile, Hosted, Missing})
			REQUIRE(World.AddTile(Each, Error));

		FLocalWorldTileSource Source(Registry);
		FWorldCompositionStreaming Streaming(Source);
		FWorldCompositionStreamingSettings Settings;
		Settings.LoadDistance = 600;
		Settings.UnloadDistance = 800;
		REQUIRE(Streaming.SetSettings(Settings, Error));
		FWorldCompositionStreamingChanges Changes;
		Streaming.Update(World, Vector3d(0, 256, 0), &Changes);
		CHECK(Changes.Loaded.size() == 2);
		CHECK(Changes.Failed.size() == 2);

		const FStreamedWorldTile* EastStreamed = Streaming.FindTile(EastTile.Id);
		REQUIRE(EastStreamed);
		REQUIRE(EastStreamed->Snapshot);
		REQUIRE(EastStreamed->Snapshot->GetItems().size() == 1);
		REQUIRE(EastStreamed->Snapshot->GetItems()[0].Terrain);
		CHECK(EastStreamed->Snapshot->GetItems()[0].Terrain->GetQuadsX() == 16);
		CHECK(Translation(EastStreamed->ToWorld).GetX() == doctest::Approx(256));
		const FStreamedWorldTile* WestStreamed = Streaming.FindTile(WestTile.Id);
		REQUIRE(WestStreamed);
		REQUIRE(WestStreamed->Snapshot);
		CHECK(WestStreamed->Snapshot->GetItems()[0].Terrain->GetQuadsX() == 8);
		CHECK(Translation(WestStreamed->ToWorld).GetX() == doctest::Approx(-256));
		CHECK(Streaming.FindTile(Hosted.Id)->State == EWorldTileStreamingState::Failed);
		CHECK(Streaming.FindTile(Missing.Id)->State == EWorldTileStreamingState::Failed);
		CHECK_FALSE(Streaming.FindTile(Missing.Id)->Error.IsEmpty());
	}
}
