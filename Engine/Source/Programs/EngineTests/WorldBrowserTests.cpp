// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "UITestHelpers.h"
#include "SWorldCompositionGrid.h"
#include "WorldCompositionEditing.h"
#include "World/World.h"
#include "World/WorldGeometry.h"
#include "Landscape/Landscape.h"
#include <chrono>
#include <filesystem>

namespace
{
	FString BrowserFolder()
	{
		static int Sequence = 0;
		const std::filesystem::path Directory = std::filesystem::temp_directory_path() /
		    FString::Format(
		        "GenesisWorldBrowser-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(), ++Sequence)
		        .ToWide();
		std::filesystem::create_directories(Directory);
		return FString::FromWide(Directory.wstring());
	}
	FString FileIn(const FString& Directory, const char* Name)
	{
		return FString::FromWide((std::filesystem::path(Directory.ToWide()) / Name).wstring());
	}
	// What a host writes for a new region: a world map with one flat landscape.
	bool WriteRegionMap(const FString& File, double SizeX, double SizeY, FString& Error)
	{
		FAssetPackage Package;
		Package.Kind = EAssetPackageKind::Map;
		Package.Schema = "Genesis.World";
		Package.SchemaVersion = 3;
		const int32 Quads = 8;
		Landscape Terrain(Quads, Quads, static_cast<float>(std::max(SizeX, SizeY) / Quads));
		FActorComponent Component = FActorComponent::New("Genesis.Landscape");
		FJsonValue Data;
		if (!FWorldGeometryCodec::EncodeLandscape(Terrain, Package, Data, Error))
			return false;
		Component.Properties["Data"] = Data;
		FActor Ground = FActor::New("Ground");
		Ground.Components.push_back(Component);
		FWorld Scene;
		if (!Scene.AddActor(Ground, Error))
			return false;
		Package.Exports["Scene"] = Scene.ToJson();
		return FAssetPackageStore::Save(File, Package, Error);
	}

	// A mounted project folder with an empty composition and a few region maps.
	struct FBrowserProject
	{
		FString Root;
		std::shared_ptr<FAssetRegistry> Registry = std::make_shared<FAssetRegistry>();
		FAssetReference Composition, Hills, Coast, OtherHills, Prefab;
		FBrowserProject()
		{
			Root = BrowserFolder();
			FString Error;
			FAssetPackage World;
			FWorldComposition().ToPackage(World);
			REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Worlds/Main.gmap"), World, Error));
			REQUIRE(WriteRegionMap(FileIn(Root, "Regions/Hills.gmap"), 1000, 1000, Error));
			REQUIRE(WriteRegionMap(FileIn(Root, "Regions/Coast.gmap"), 1000, 1000, Error));
			REQUIRE(WriteRegionMap(FileIn(Root, "More/Hills.gmap"), 1000, 1000, Error));
			FAssetPackage Asset;
			Asset.Schema = "Genesis.Prefab";
			REQUIRE(FAssetPackageStore::Save(FileIn(Root, "Prefabs/House.asset"), Asset, Error));
			REQUIRE(Registry->SetMounts({{"/Game", Root}}, Error));
			REQUIRE(Registry->Scan());
			Composition = {{}, "/Game/Worlds/Main"};
			Hills = {{}, "/Game/Regions/Hills"};
			Coast = {{}, "/Game/Regions/Coast"};
			OtherHills = {{}, "/Game/More/Hills"};
			Prefab = {{}, "/Game/Prefabs/House"};
		}
		TUniquePtr<FWorldCompositionEditSession> Open()
		{
			FString Error;
			TUniquePtr<FWorldCompositionEditSession> Session =
			    FWorldCompositionEditSession::Open(Registry, Composition, Error);
			REQUIRE_MESSAGE(Session, *Error);
			return Session;
		}
	};
} // namespace

TEST_SUITE("WorldBrowser")
{
	TEST_CASE("Registering maps names regions after their files and rejects invalid placements")
	{
		FBrowserProject Project;
		TUniquePtr<FWorldCompositionEditSession> Session = Project.Open();
		FString Error, HillsId, CoastId, OtherId, Ignored;
		REQUIRE_MESSAGE(Session->RegisterMap(Project.Hills, 0, 0, HillsId, Error), *Error);
		REQUIRE(Session->RegisterMap(Project.Coast, 1, 0, CoastId, Error));
		REQUIRE(Session->RegisterMap(Project.OtherHills, 0, 1, OtherId, Error));
		CHECK(Session->GetComposition().FindTile(HillsId)->Name == "Hills");
		CHECK(Session->GetComposition().FindTile(OtherId)->Name == "Hills 2");
		// The registered reference carries the map's identity, not only its path.
		CHECK_FALSE(Session->GetComposition().FindTile(HillsId)->Map.Id.IsEmpty());
		CHECK(Session->IsDirty());

		CHECK_FALSE(Session->RegisterMap(Project.Hills, 5, 5, Ignored, Error));
		CHECK_FALSE(Session->RegisterMap(Project.Prefab, 5, 5, Ignored, Error));
		CHECK_FALSE(Session->RegisterMap({{}, "/Game/Regions/Missing"}, 5, 5, Ignored, Error));
		CHECK(Session->GetComposition().GetTiles().size() == 3);

		CHECK_FALSE(Session->MoveTile(HillsId, 1, 0, Error));
		CHECK(Error.View().find("Coast") != std::string_view::npos);
		CHECK_FALSE(Session->ResizeTile(HillsId, 2, 1, Error));
		CHECK_FALSE(Session->RenameTile(HillsId, "Coast", Error));
		CHECK_FALSE(Session->RenameTile(HillsId, "", Error));
		const FWorldCompositionTile* Hills = Session->GetComposition().FindTile(HillsId);
		CHECK(Hills->X == 0);
		CHECK(Hills->SizeX == 1);
		CHECK(Hills->Name == "Hills");
		REQUIRE(Session->MoveTile(HillsId, -3, 2, Error));
		REQUIRE(Session->ResizeTile(HillsId, 2, 2, Error));
		REQUIRE(Session->SetTileHost(HillsId, "region-01.example:7000", Error));
		REQUIRE(Session->RenameTile(HillsId, "Highlands", Error));
		const FAssetData* Map = Session->ResolveMap(HillsId, Error);
		REQUIRE(Map);
		CHECK(Map->PackagePath == "/Game/Regions/Hills");
	}

	TEST_CASE("Undo and redo step through whole registry changes, and saving keeps the package identity")
	{
		FBrowserProject Project;
		TUniquePtr<FWorldCompositionEditSession> Session = Project.Open();
		const FString PackageId = Project.Registry->FindByPath("/Game/Worlds/Main")->Metadata.Id;
		FString Error, HillsId, CoastId;
		CHECK_FALSE(Session->CanUndo());
		REQUIRE(Session->RegisterMap(Project.Hills, 0, 0, HillsId, Error));
		REQUIRE(Session->MoveTile(HillsId, 4, 4, Error));
		REQUIRE(Session->RegisterMap(Project.Coast, 0, 0, CoastId, Error));
		// A no-op edit is not an undo step.
		REQUIRE(Session->MoveTile(HillsId, 4, 4, Error));
		REQUIRE(Session->Undo());
		CHECK_FALSE(Session->GetComposition().FindTile(CoastId));
		REQUIRE(Session->Undo());
		CHECK(Session->GetComposition().FindTile(HillsId)->X == 0);
		REQUIRE(Session->Redo());
		CHECK(Session->GetComposition().FindTile(HillsId)->X == 4);
		REQUIRE(Session->RegisterMap(Project.Coast, 1, 0, CoastId, Error));
		CHECK_FALSE(Session->CanRedo());

		REQUIRE(Session->SetCellSize(2048, Error));
		REQUIRE_MESSAGE(Session->Save(Error), *Error);
		CHECK_FALSE(Session->IsDirty());
		CHECK(Project.Registry->FindByPath("/Game/Worlds/Main")->Metadata.Id == PackageId);

		TUniquePtr<FWorldCompositionEditSession> Reopened = Project.Open();
		CHECK(Reopened->GetComposition().GetCellSize() == 2048);
		REQUIRE(Reopened->GetComposition().GetTiles().size() == 2);
		CHECK(Reopened->GetComposition().FindTile(HillsId)->X == 4);
		CHECK(Reopened->GetComposition().FindTile(CoastId)->ToJson() ==
		    Session->GetComposition().FindTile(CoastId)->ToJson());

		FString Ignored;
		CHECK_FALSE(FWorldCompositionEditSession::Open(Project.Registry, Project.Hills, Ignored));
		CHECK_FALSE(FWorldCompositionEditSession::Open(nullptr, Project.Composition, Ignored));
	}

	TEST_CASE("New regions are written beside the composition through the host and registered")
	{
		FBrowserProject Project;
		TUniquePtr<FWorldCompositionEditSession> Session = Project.Open();
		FString Error, TileId, Ignored;
		CHECK_FALSE(Session->CreateRegion({}, 0, 0, Ignored, Error));
		int32 Calls = 0;
		double Size = 0;
		const FCreateRegionMap Create =
		    FCreateRegionMap::CreateLambda([&](const FString& File, double SizeX, double SizeY, FString& Out)
		{
			++Calls;
			Size = SizeX;
			CHECK(SizeX == SizeY);
			return WriteRegionMap(File, SizeX, SizeY, Out);
		});
		REQUIRE_MESSAGE(Session->CreateRegion(Create, 2, 3, TileId, Error), *Error);
		const FWorldCompositionTile* Tile = Session->GetComposition().FindTile(TileId);
		REQUIRE(Tile);
		CHECK(Tile->Name == "Region 1");
		CHECK(Tile->X == 2);
		CHECK(Tile->Y == 3);
		CHECK(Size == Session->GetComposition().GetCellSize());
		CHECK(std::filesystem::exists(FileIn(Project.Root, "Worlds/MainRegions/Region 1.gmap").ToWide()));
		CHECK(Session->ResolveMap(TileId, Error)->PackagePath == "/Game/Worlds/MainRegions/Region 1");

		// An occupied cell is rejected before any file is written.
		CHECK_FALSE(Session->CreateRegion(Create, 2, 3, Ignored, Error));
		CHECK(Calls == 1);
		REQUIRE(Session->CreateRegion(Create, 3, 3, TileId, Error));
		CHECK(Session->GetComposition().FindTile(TileId)->Name == "Region 2");

		const FCreateRegionMap Failing = FCreateRegionMap::CreateLambda([](const FString&, double, double, FString& Out)
		{
			Out = "disk full";
			return false;
		});
		CHECK_FALSE(Session->CreateRegion(Failing, 9, 9, Ignored, Error));
		CHECK(Error == "disk full");
		CHECK(Session->GetComposition().GetTiles().size() == 2);

		int32 X = 0, Y = 0;
		REQUIRE(Session->FindFreeCell(2, 3, 1, 1, X, Y));
		CHECK(std::max(std::abs(X - 2), std::abs(Y - 3)) == 1);
		CHECK_FALSE(Session->GetComposition().FindTileAt(
		    (X + 0.5) * Session->GetComposition().GetCellSize(), (Y + 0.5) * Session->GetComposition().GetCellSize()));
		REQUIRE(Session->FindFreeCell(7, 7, 1, 1, X, Y));
		CHECK(X == 7);
		CHECK(Y == 7);
	}

	TEST_CASE("Region thumbnails show each map's terrain and rebuild after the registry rescans")
	{
		FBrowserProject Project;
		TUniquePtr<FWorldCompositionEditSession> Session = Project.Open();
		FString Error, HillsId, CoastId;
		REQUIRE(Session->RegisterMap(Project.Hills, 0, 0, HillsId, Error));
		REQUIRE(Session->RegisterMap(Project.Coast, 1, 0, CoastId, Error));
		FWorldTileThumbnails Thumbnails;
		const FWorldCompositionTile& Hills = *Session->GetComposition().FindTile(HillsId);
		CHECK_FALSE(Thumbnails.Find(Hills));
		int32 Built = 0;
		while (Thumbnails.BuildNext(Session->GetComposition(), Session->GetRegistry()))
			++Built;
		CHECK(Built == 2);
		const TSharedPtr<FSlateTextureData> Image = Thumbnails.Find(Hills);
		REQUIRE(Image);
		// The test maps' landscapes have 9 x 9 heights: one pixel each.
		CHECK(Image->GetWidth() == 9);
		CHECK(Image->GetHeight() == 9);
		CHECK(Image->GetPixels()[3] == 255);
		CHECK(Thumbnails.Find(*Session->GetComposition().FindTile(CoastId)));
		CHECK_FALSE(Thumbnails.BuildNext(Session->GetComposition(), Session->GetRegistry()));
		Project.Registry->Scan();
		CHECK(Thumbnails.BuildNext(Session->GetComposition(), Session->GetRegistry()));

		// Light comes from the north: ground rising to the north faces away and is darker.
		const int32 Quads = 16;
		TArray<float> Rising((Quads + 1) * (Quads + 1));
		for (int32 Y = 0; Y <= Quads; ++Y)
			for (int32 X = 0; X <= Quads; ++X)
				Rising[Y * (Quads + 1) + X] = Y * 40.0f;
		const std::shared_ptr<const Landscape> Slope = Landscape::Create(Quads, Quads, 32, Rising);
		REQUIRE(Slope);
		const TSharedPtr<FSlateTextureData> Flat = FWorldTileThumbnails::Render(Landscape(Quads, Quads, 32));
		const TSharedPtr<FSlateTextureData> Facing = FWorldTileThumbnails::Render(*Slope);
		const size_t Middle = (static_cast<size_t>(8) * Flat->GetWidth() + 8) * 4;
		CHECK(Facing->GetPixels()[Middle] < Flat->GetPixels()[Middle]);
	}

	TEST_CASE("The grid moves regions by dragging, previews invalid drops and removes the selection")
	{
		FBrowserProject Project;
		std::shared_ptr<FWorldCompositionEditSession> Session(Project.Open().release());
		FString Error, HillsId, CoastId;
		REQUIRE(Session->RegisterMap(Project.Hills, 0, 0, HillsId, Error));
		REQUIRE(Session->RegisterMap(Project.Coast, 2, 0, CoastId, Error));
		FUITestApp App;
		int32 Changes = 0;
		FString Reported;
		const TSharedRef<SWorldCompositionGrid> Grid =
		    SNew(SWorldCompositionGrid).Session(Session).OnChanged([&] { ++Changes; }).OnError([&](const FString& E) {
			Reported = E;
		});
		const TSharedRef<SWindow> Window = App.AddWindow(Grid, FVector2(600, 400));
		App.Tick();
		// Framed: every region is inside the view.
		const FVector2 Low = Grid->CellToLocal(0, 0), High = Grid->CellToLocal(3, 1);
		CHECK(Low.X >= 0);
		CHECK(High.X <= 600);
		CHECK(High.Y >= 0);
		CHECK(Low.Y <= 400);
		// Framing ran (the grid ticks): the regions' centre is the middle of the view.
		const FVector2 Middle = Grid->CellToLocal(1.5, 0.5);
		CHECK(Middle.X == doctest::Approx(300).epsilon(0.01));
		CHECK(Middle.Y == doctest::Approx(200).epsilon(0.01));
		const TFunction<FVector2(double, double)> CellCenter = [&](double X, double Y)
		{ return Grid->CellToLocal(X + 0.5, Y + 0.5); };
		int32 CellX = 0, CellY = 0;
		Grid->LocalToCell(CellCenter(2, 0), CellX, CellY);
		CHECK(CellX == 2);
		CHECK(CellY == 0);

		// Drag Hills up one cell.
		App.MouseMove(Window, CellCenter(0, 0));
		App.MouseDown(Window, CellCenter(0, 0));
		App.Tick();
		CHECK(Grid->GetSelectedTile() == HillsId);
		App.MouseMove(Window, CellCenter(0, 1));
		App.Tick();
		App.MouseUp(Window, CellCenter(0, 1));
		App.Tick();
		CHECK(Session->GetComposition().FindTile(HillsId)->Y == 1);
		CHECK(Changes == 1);

		// Dropping onto Coast is refused and leaves Hills where it was.
		App.MouseMove(Window, CellCenter(0, 1));
		App.MouseDown(Window, CellCenter(0, 1));
		App.Tick();
		App.MouseMove(Window, CellCenter(2, 0));
		App.Tick();
		App.MouseUp(Window, CellCenter(2, 0));
		App.Tick();
		CHECK(Session->GetComposition().FindTile(HillsId)->X == 0);
		CHECK(Session->GetComposition().FindTile(HillsId)->Y == 1);
		CHECK_FALSE(Reported.IsEmpty());
		CHECK(Changes == 1);

		App.MouseMove(Window, CellCenter(2, 0));
		App.MouseDown(Window, CellCenter(2, 0));
		App.MouseUp(Window, CellCenter(2, 0));
		App.Tick();
		CHECK(Grid->GetSelectedTile() == CoastId);
		App.Key(EKey::Delete);
		CHECK_FALSE(Session->GetComposition().FindTile(CoastId));
		CHECK(Grid->GetSelectedTile().IsEmpty());
		CHECK(Changes == 2);
		REQUIRE(Session->Undo());
		CHECK(Session->GetComposition().FindTile(CoastId));

		// Clicking empty ground clears the selection.
		App.MouseMove(Window, CellCenter(1, -1));
		App.MouseDown(Window, CellCenter(1, -1));
		App.MouseUp(Window, CellCenter(1, -1));
		App.Tick();
		CHECK(Grid->GetSelectedTile().IsEmpty());
		Grid->SetSelectedTile(HillsId);
		CHECK(Grid->GetSelectedTile() == HillsId);
		Grid->SetSelectedTile(FAssetId::New());
		CHECK(Grid->GetSelectedTile().IsEmpty());
	}
}
