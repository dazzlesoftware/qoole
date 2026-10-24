// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "GameFramework/CharacterMovement.h"
#include "GameFramework/GameInstance.h"
#include "GameFramework/SaveGame.h"
#include "HAL/File.h"
#include "Landscape/Landscape.h"
#include "Renderer/WorldSceneDrawList.h"
#include "World/GameWorld.h"
#include "World/WorldGeometry.h"
#include <chrono>
#include <filesystem>
#include <limits>

namespace
{
	constexpr int32 Quads = 8;
	constexpr double Cell = 1000.0;

	FString GameFolder()
	{
		static int Sequence = 0;
		const std::filesystem::path Directory = std::filesystem::temp_directory_path() /
		    FString::Format(
		        "GenesisGameWorld-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(), ++Sequence)
		        .ToWide();
		std::filesystem::create_directories(Directory);
		return FString::FromWide(Directory.wstring());
	}
	FString FileIn(const FString& Directory, const FString& Name)
	{
		return FString::FromWide((std::filesystem::path(Directory.ToWide()) / Name.ToWide()).wstring());
	}
	// A level holding a flat landscape at Height, moved to At, and optionally a player start.
	FAssetPackage Level(float Height, const Vector3d& At = Vector3d(), const Vector3d* PlayerStart = nullptr)
	{
		FAssetPackage Package;
		Package.Kind = EAssetPackageKind::Map;
		Package.Schema = "Genesis.World";
		Package.SchemaVersion = 3;
		FString Error;
		TArray<float> Heights((Quads + 1) * (Quads + 1), Height);
		const std::shared_ptr<const Landscape> Terrain =
		    Landscape::Create(Quads, Quads, static_cast<float>(Cell / Quads), Heights);
		REQUIRE(Terrain);
		FActorComponent Component = FActorComponent::New("Genesis.Landscape");
		FJsonValue Data;
		REQUIRE(FWorldGeometryCodec::EncodeLandscape(*Terrain, Package, Data, Error));
		Component.Properties["Data"] = Data;
		FActor Ground = FActor::New("Ground");
		Ground.Root.Transform.Location = At;
		Ground.Components.push_back(Component);
		FWorld Scene;
		REQUIRE(Scene.AddActor(Ground, Error));
		if (PlayerStart)
		{
			FActor Start = FActor::New("Player Start");
			Start.Root.Transform.Location = *PlayerStart;
			FActorComponent Entity = FActorComponent::New("Genesis.LegacyEntity");
			Entity.Properties["Data"]["Class"] = "info_player_start";
			Start.Components.push_back(Entity);
			REQUIRE(Scene.AddActor(Start, Error));
		}
		Package.Exports["Scene"] = Scene.ToJson();
		return Package;
	}

	// A row of regions at cells 0..Count-1, region I's ground at height I * 100.
	struct FRowWorld
	{
		FString Root;
		std::shared_ptr<FAssetRegistry> Registry = std::make_shared<FAssetRegistry>();
		TArray<FString> RegionIds;
		FAssetPackage CompositionPackage;
		FString CompositionFile;
		explicit FRowWorld(int32 Count)
		{
			Root = GameFolder();
			FString Error;
			FWorldComposition World;
			REQUIRE(World.SetCellSize(Cell, Error));
			for (int32 I = 0; I < Count; ++I)
			{
				FAssetPackage Map = Level(static_cast<float>(I * 100));
				REQUIRE(FAssetPackageStore::Save(FileIn(Root, FString::Format("Regions/R{}.gmap", I)), Map, Error));
				FWorldCompositionTile Tile = FWorldCompositionTile::New(
				    FString::Format("R{}", I), {Map.Id, FString::Format("/Game/Regions/R{}", I)}, I, 0);
				RegionIds.push_back(Tile.Id);
				REQUIRE(World.AddTile(Tile, Error));
			}
			World.ToPackage(CompositionPackage);
			CompositionFile = FileIn(Root, "World.gmap");
			REQUIRE(FAssetPackageStore::Save(CompositionFile, CompositionPackage, Error));
			REQUIRE(Registry->SetMounts({{"/Game", Root}}, Error));
			REQUIRE(Registry->Scan());
		}
		TUniquePtr<FGameWorld> Open()
		{
			FString Error;
			TUniquePtr<FGameWorld> Game = FGameWorld::Open(Registry, {{}, "/Game/World"}, Error);
			REQUIRE_MESSAGE(Game, *Error);
			FWorldCompositionStreamingSettings Settings;
			Settings.LoadDistance = 600;
			Settings.UnloadDistance = 900;
			Settings.MaxLoadsPerUpdate = 16;
			REQUIRE(Game->SetStreamingSettings(Settings, Error));
			return Game;
		}
	};
	bool Contains(const TArray<FString>& Ids, const FString& Id)
	{
		return std::find(Ids.begin(), Ids.end(), Id) != Ids.end();
	}
} // namespace

TEST_SUITE("GameWorld")
{
	TEST_CASE("A level opens as one region at the origin with its player start and ground")
	{
		const FString Root = GameFolder();
		FString Error;
		const Vector3d Start(120, -40, 300);
		FAssetPackage Map = Level(50, Vector3d(200, 0, 25), &Start);
		REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Maps/Main.gmap"), Map, Error));
		std::shared_ptr<FAssetRegistry> Registry = std::make_shared<FAssetRegistry>();
		REQUIRE(Registry->SetMounts({{"/Game", Root}}, Error));
		REQUIRE(Registry->Scan());
		TUniquePtr<FGameWorld> Game = FGameWorld::Open(Registry, {{}, "/Game/Maps/Main"}, Error);
		REQUIRE_MESSAGE(Game, *Error);
		CHECK_FALSE(Game->IsComposition());
		TArray<FGameRegion> Regions;
		Game->GetLoadedRegions(Regions);
		REQUIRE(Regions.size() == 1);
		CHECK(Regions[0].Id == Map.Id);
		CHECK(Regions[0].Name == "Main");

		FWorldLocation Found;
		REQUIRE(Game->FindPlayerStart(Found));
		CHECK(Found.RegionId == Map.Id);
		CHECK(Found.Local.GetX() == doctest::Approx(120));
		CHECK(Found.Local.GetZ() == doctest::Approx(300));
		Vector3d Render;
		REQUIRE(Game->ToRender(Found, Render));
		CHECK(Render.GetX() == doctest::Approx(120));
		CHECK(Render.GetY() == doctest::Approx(-40));

		// The landscape actor sits at (200, 0, 25), so its ground is at 75 and covers x -300..700.
		float Ground = 0;
		REQUIRE(Game->SampleGround({Map.Id, Vector3d(600, 0, 0)}, Ground));
		CHECK(Ground == doctest::Approx(75));
		CHECK_FALSE(Game->SampleGround({Map.Id, Vector3d(800, 0, 0)}, Ground));
		Game->Update(Found); // a level never streams or rebases
		CHECK(Game->GetOriginCellX() == 0);

		CHECK_FALSE(FGameWorld::Open(Registry, {{}, "/Game/Maps/Missing"}, Error));
		CHECK_FALSE(FGameWorld::Open(nullptr, {{}, "/Game/Maps/Main"}, Error));
	}

	TEST_CASE("A composed world streams regions, walks across boundaries and rebases its origin")
	{
		FRowWorld Row(6);
		TUniquePtr<FGameWorld> Game = Row.Open();
		CHECK(Game->IsComposition());
		FCharacterMovement Player;
		REQUIRE(Game->FindPlayerStart(Player.Location));
		CHECK(Player.Location.RegionId == Row.RegionIds[0]);
		FGameWorldChanges Changes;
		Game->Update(Player.Location, &Changes);
		CHECK(Contains(Changes.Loaded, Row.RegionIds[0]));
		CHECK(Contains(Changes.Loaded, Row.RegionIds[1]));
		CHECK_FALSE(Game->IsLoaded(Row.RegionIds[2]));

		// Walk east from region 0's centre into region 1, whose ground is 100 higher.
		REQUIRE(Game->FindPlayerStart(Player.Location));
		Player.Yaw = 0;
		FCharacterInput Walk;
		Walk.Forward = 1;
		for (int32 Frame = 0; Frame < 60 && Player.Location.RegionId == Row.RegionIds[0]; ++Frame)
		{
			Player.Update(*Game, Walk, 1.0f / 60);
			Game->Update(Player.Location);
		}
		REQUIRE(Player.Location.RegionId == Row.RegionIds[1]);
		CHECK(Player.Location.Local.GetX() < -400);
		for (int32 Frame = 0; Frame < 5; ++Frame)
			Player.Update(*Game, Walk, 1.0f / 60);
		CHECK(Player.Location.Local.GetZ() == doctest::Approx(100));
		CHECK(Player.bOnGround);

		// Travel to region 4: the origin rebases, render positions stay near the player.
		FWorldLocation Far{Row.RegionIds[4], Vector3d(100, 0, 400)};
		Game->Update(Far, &Changes);
		CHECK(Changes.bRebased);
		CHECK(Game->GetOriginCellX() == 4);
		CHECK(Contains(Changes.Unloaded, Row.RegionIds[0]));
		Vector3d Render;
		REQUIRE(Game->ToRender(Far, Render));
		CHECK(Render.GetX() == doctest::Approx(600));
		TArray<FGameRegion> Regions;
		Game->GetLoadedRegions(Regions);
		for (const FGameRegion& Region : Regions)
			CHECK(std::abs(Region.ToRender.GetMatrix(0, 3)) < 2 * Cell);
		double X = 0, Y = 0, Z = 0;
		REQUIRE(Game->ToAbsolute(Far, X, Y, Z));
		CHECK(X == doctest::Approx(4600));
		FWorldLocation Back;
		REQUIRE(Game->FromAbsolute(X, Y, Z, Back));
		CHECK(Back.RegionId == Row.RegionIds[4]);
		// Points past the last region belong to the nearest one.
		REQUIRE(Game->FromAbsolute(9000, 0, 0, Back));
		CHECK(Back.RegionId == Row.RegionIds[5]);
	}

	TEST_CASE("Region state outlives streaming and saved games survive regions moving or disappearing")
	{
		FRowWorld Row(4);
		TUniquePtr<FGameWorld> Game = Row.Open();
		FSaveGame Save;
		Save.World = Game->GetReference();
		Save.Player = {Row.RegionIds[2], Vector3d(-120, 40, 260)};
		REQUIRE(Game->ToAbsolute(Save.Player, Save.AbsoluteX, Save.AbsoluteY, Save.AbsoluteZ));
		Save.Yaw = 45;
		Save.Pitch = -10;
		(*Save.Regions.Edit(Row.RegionIds[0]))["DoorOpen"] = true;
		Save.Game["Score"] = 7;
		CHECK_FALSE(Save.Regions.Edit("not-a-region"));

		Game->Update({Row.RegionIds[0], Vector3d()});
		REQUIRE(Game->IsLoaded(Row.RegionIds[0]));
		Game->Update({Row.RegionIds[3], Vector3d()});
		CHECK_FALSE(Game->IsLoaded(Row.RegionIds[0]));
		Game->Update({Row.RegionIds[0], Vector3d()});
		REQUIRE(Game->IsLoaded(Row.RegionIds[0]));
		CHECK(Save.Regions.Find(Row.RegionIds[0])->Get("DoorOpen").AsBool());

		const FString File = FileIn(Row.Root, "Saved/SaveGames/Slot1.json");
		FString Error;
		REQUIRE_MESSAGE(Save.Save(File, Error), *Error);
		CHECK_FALSE(std::filesystem::exists((File + ".tmp").ToWide()));
		TOptional<FSaveGame> Loaded = FSaveGame::Load(File, Error);
		REQUIRE_MESSAGE(Loaded, *Error);
		CHECK(Loaded->ToJson() == Save.ToJson());
		FWorldLocation Player;
		REQUIRE(Loaded->ResolvePlayer(*Game, Player));
		CHECK(Player.RegionId == Row.RegionIds[2]);
		CHECK(Player.Local.GetX() == doctest::Approx(-120));

		// Move region 2 elsewhere on the grid: the player is still in it, at the same local spot.
		FWorldComposition Moved;
		REQUIRE(FWorldComposition::FromPackage(Row.CompositionPackage, Moved, Error));
		FWorldCompositionTile Tile = *Moved.FindTile(Row.RegionIds[2]);
		Tile.Y = 5;
		REQUIRE(Moved.UpdateTile(Tile, Error));
		// And remove region 1 entirely.
		REQUIRE(Moved.RemoveTile(Row.RegionIds[1], Error));
		FAssetPackage Package = Row.CompositionPackage;
		Moved.ToPackage(Package);
		REQUIRE(FAssetPackageStore::Save(Row.CompositionFile, Package, Error));
		REQUIRE(Row.Registry->Scan());
		TUniquePtr<FGameWorld> Rearranged = Row.Open();
		REQUIRE(Loaded->ResolvePlayer(*Rearranged, Player));
		CHECK(Player.RegionId == Row.RegionIds[2]);
		CHECK(Player.Local.GetY() == doctest::Approx(40));
		FSaveGame InRemoved = *Loaded;
		InRemoved.Player.RegionId = Row.RegionIds[1];
		REQUIRE(InRemoved.ResolvePlayer(*Rearranged, Player));
		CHECK(Player.RegionId != Row.RegionIds[1]);

		// Damaged or foreign files are refused.
		REQUIRE(FFile::WriteAllText(File, "{\"Schema\":\"Other\"}"));
		CHECK_FALSE(FSaveGame::Load(File, Error));
		REQUIRE(FFile::WriteAllText(File, "{ not json"));
		CHECK_FALSE(FSaveGame::Load(File, Error));
		CHECK_FALSE(FSaveGame::Load(FileIn(Row.Root, "Missing.json"), Error));
		FJsonValue Bad = Save.ToJson();
		Bad["Player"]["Local"][0] = std::numeric_limits<double>::infinity();
		CHECK_FALSE(FSaveGame::FromJson(Bad, Error));
	}

	TEST_CASE("A game instance plays a composed world or an unsaved level, with quick save and load")
	{
		FRowWorld Row(3);
		FString Error;
		const FString SaveFile = FileIn(Row.Root, "Saved/SaveGames/Quick.json");
		TUniquePtr<FGameInstance> Game = FGameInstance::Open(Row.Registry, {{}, "/Game/World"}, SaveFile, Error);
		REQUIRE_MESSAGE(Game, *Error);
		CHECK(Game->GetWorldName() == "/Game/World");
		CHECK(Game->Player.Location.RegionId == Row.RegionIds[0]);
		Game->Input.Forward = 1;
		Game->Input.bRun = true;
		for (int32 Frame = 0; Frame < 60; ++Frame)
			Game->Tick(1.0f / 30);
		CHECK(Game->GetTicks() == 60);
		CHECK(Game->GetRegionsEntered() >= 1);
		CHECK(Game->Player.Location.RegionId != Row.RegionIds[0]);
		Game->Input = {};
		const FWorldLocation Saved = Game->Player.Location;
		REQUIRE_MESSAGE(Game->QuickSave(Error), *Error);
		CHECK(Game->GetStatus() == GENESIS_TEXT("Saved"));
		Game->Player.Location = {Row.RegionIds[0], Vector3d()};
		REQUIRE_MESSAGE(Game->QuickLoad(Error), *Error);
		CHECK(Game->Player.Location.RegionId == Saved.RegionId);
		CHECK(Game->Player.Location.Local.GetX() == doctest::Approx(Saved.Local.GetX()));
		CHECK(Game->GetHudText(60).View().find("R") != std::string_view::npos);
		Game->ToggleFlying();
		CHECK(Game->Player.bFlying);

		// An unsaved level plays from memory, and another world's save is refused.
		FAssetPackage Level = ::Level(25);
		Level.Id = FAssetId::New();
		TUniquePtr<FGameInstance> Editing = FGameInstance::OpenLevel(Row.Registry, Level, "Editing", SaveFile, Error);
		REQUIRE_MESSAGE(Editing, *Error);
		CHECK_FALSE(Editing->GetWorld().IsComposition());
		CHECK(Editing->Player.Location.Local.GetZ() == doctest::Approx(25));
		CHECK_FALSE(Editing->QuickLoad(Error));
		FAssetPackage NoIdentity = ::Level(0);
		CHECK_FALSE(FGameInstance::OpenLevel(Row.Registry, NoIdentity, "Bad", SaveFile, Error));
	}

	TEST_CASE("Draw lists take several regions, each placed by its own transform")
	{
		FRowWorld Row(2);
		TUniquePtr<FGameWorld> Game = Row.Open();
		Game->Update({Row.RegionIds[0], Vector3d(400, 0, 0)});
		TArray<FGameRegion> Regions;
		Game->GetLoadedRegions(Regions);
		REQUIRE(Regions.size() == 2);
		FProjectedSceneView View;
		View.Width = View.Height = 256;
		View.CenterX = View.CenterY = 128;
		View.ProjectionScale = 128;
		Matrix44 ToView;
		ToView.SetIdentity();
		ToView.SetMatrix(1, 1, 0);
		ToView.SetMatrix(1, 2, -1);
		ToView.SetMatrix(2, 1, 1);
		ToView.SetMatrix(2, 2, 0);
		ToView.SetMatrix(0, 3, -1000);
		ToView.SetMatrix(1, 3, 3000);
		const FSceneViewVolume Volume{Vector3d(-1, 1, -1), Vector3d(1, 20000, 1), true};
		FWorldSceneDrawList Single, Both;
		FString Error;
		REQUIRE(Single.Append(*Regions[0].Scene, Regions[0].ToRender, ToView, Volume, View, 8, Error));
		for (const FGameRegion& Region : Regions)
			REQUIRE(Both.Append(*Region.Scene, Region.ToRender, ToView, Volume, View, 8, Error));
		CHECK(Both.TerrainTriangles > Single.TerrainTriangles);
		for (const FProjectedTriangleBatch& Batch : Both.Batches)
			CHECK(Batch.FirstVertex + Batch.NumVertices <= Both.Vertices.size());
		const size_t Vertices = Both.Vertices.size();
		FProjectedSceneView Broken = View;
		Broken.Width = 0;
		CHECK_FALSE(Both.Append(*Regions[1].Scene, Regions[1].ToRender, ToView, Volume, Broken, 8, Error));
		CHECK(Both.Vertices.size() == Vertices);
		Both.Reset();
		CHECK(Both.Vertices.empty());
	}
}
