// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The map editor module (Engine/MapEditor): the document and its undo
// stack, saving and loading, and the map views (picking, moving, drawing
// with a depth buffer). They use the built-in Genesis game and the data in
// Content/Editor; the views draw on D3D12 WARP (and on Vulkan when a driver
// is present).

#include "TestHelpers.h"
#include "UITestHelpers.h"

#include "MapCoreHeaders.h"
#include "MapDocument.h"
#include "MapPackage.h"
#include "AssetPackage.h"
#include "SContentBrowser.h"
#include "MapEditorEnvironment.h"
#include "MapFoliageMode.h"
#include "MapHeightmap.h"
#include "MapLandscapeTextures.h"
#include "HAL/File.h"
#include "MapLandscapeMode.h"
#include "MapObjectProperties.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "MapViewportClient.h"
#include "SlateRHIRenderingPolicy.h"
#include "Widgets/SViewport.h"

#include <filesystem>

namespace
{
	// The map core keeps global state (games, entity lists): start it once.
	void EnsureMapCore()
	{
		static bool bStarted = false;
		if (bStarted)
		{
			return;
		}
		FMapEditorEnvironment::FStartupOptions Options;
		Options.EditorContentDir = FPaths::Combine(FPaths::FindContentDir(), "Editor");
		REQUIRE(FMapEditorEnvironment::Startup(Options));
		REQUIRE(FMapEditorEnvironment::Activate(FMapEditorEnvironment::GetBuiltInGameSet()));
		bStarted = true;
	}

	FString PrefabPath(const char* Name)
	{
		for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
		{
			if (Prefab.Name == Name)
			{
				return Prefab.Path;
			}
		}
		FAIL("no prefab ", Name);
		return FString();
	}

	Vector3d PositionOf(Object* Obj)
	{
		Vector3d Position;
		Obj->GetPosition(Position);
		return Position;
	}

	bool Same(const Vector3d& A, const Vector3d& B)
	{
		return FMath::Abs(A.GetX() - B.GetX()) < 0.01f && FMath::Abs(A.GetY() - B.GetY()) < 0.01f && FMath::Abs(A.GetZ() - B.GetZ()) < 0.01f;
	}

	struct FTempFolder
	{
		std::filesystem::path Path;
		FTempFolder()
		{
			Path = std::filesystem::temp_directory_path() / std::filesystem::path(FString::Format("GenesisEditorTest{}", FPlatformTime::Seconds()).ToWide());
			std::filesystem::create_directories(Path);
		}
		~FTempFolder()
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
		}
		FString File(const char* Name) const { return FString::FromWide((Path / Name).wstring()); }
	};
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("The built-in game set needs no game folder and has prefabs and entities")
	{
		EnsureMapCore();
		const FMapGameSet& Set = FMapEditorEnvironment::GetBuiltInGameSet();
		CHECK(Set.IsBuiltIn());
		CHECK(FMapEditorEnvironment::GetActiveGameSet() == &Set);
		CHECK(FMapEditorEnvironment::FindGameSet("genesis", "GENESIS.ENT") == &Set);
		CHECK(FMapEditorEnvironment::GetPrefabs().size() >= 6);
		bool bLight = false;
		for (const FMapEditorEnvironment::FEntityClass& Class : FMapEditorEnvironment::GetPlaceableEntities())
		{
			CHECK(Class.ClassName != "worldspawn");
			bLight |= Class.ClassName == "light" && Class.Category == "Light";
		}
		CHECK(bLight);
	}

	TEST_CASE("Adding, moving and deleting objects are undo steps that restore the selection")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		CHECK(Doc->GetRoot().HasEntity());
		CHECK(!Doc->IsModified());
		int32 Changes = 0;
		Doc->OnChanged.AddLambda([&](EMapChange) { ++Changes; });

		Object* Cube = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(64.0f, 0.0f, 0.0f));
		REQUIRE(Cube);
		Object* Light = Doc->AddEntity("light", Vector3d(0.0f, 0.0f, 128.0f));
		CHECK(Doc->CountBrushes() == 1);
		CHECK(Doc->CountEntities() == 1);
		CHECK(Doc->GetSelection() == TArray<Object*>{ Light });
		CHECK(Doc->IsModified());
		CHECK(Doc->GetUndoDescription() == "Add light");
		CHECK(Changes > 0);

		// A drag: many moves, one transaction, one undo step.
		Doc->SetSelection({ Cube });
		Doc->BeginTransaction("Move Object");
		Doc->MoveObjects({ Cube }, Vector3d(16.0f, 0.0f, 0.0f));
		Doc->MoveObjects({ Cube }, Vector3d(16.0f, 32.0f, 0.0f));
		Doc->EndTransaction();
		CHECK(Same(PositionOf(Cube), Vector3d(96.0f, 32.0f, 0.0f)));
		Doc->ClearSelection();
		Doc->Undo();
		CHECK(Same(PositionOf(Cube), Vector3d(64.0f, 0.0f, 0.0f)));
		CHECK(Doc->GetSelection() == TArray<Object*>{ Cube }); // as before the move
		Doc->Redo();
		CHECK(Same(PositionOf(Cube), Vector3d(96.0f, 32.0f, 0.0f)));

		// Delete keeps the order of the others when undone.
		const TArray<Object*> Before = Doc->GetObjects();
		Doc->DeleteObjects({ Cube });
		CHECK(Doc->CountBrushes() == 0);
		CHECK(Doc->GetSelection().empty());
		Doc->Undo();
		CHECK(Doc->GetObjects() == Before);
		CHECK(Doc->GetSelection() == TArray<Object*>{ Cube });

		// Undo everything: the map is unmodified again; a new edit drops redo.
		while (Doc->CanUndo())
		{
			Doc->Undo();
		}
		CHECK(Doc->GetObjects().empty());
		CHECK(!Doc->IsModified());
		CHECK(Doc->CanRedo());
		Doc->AddEntity("info_null", Vector3d(0.0f, 0.0f, 0.0f));
		CHECK(!Doc->CanRedo());
		CHECK(Doc->IsModified());
	}

	TEST_CASE("Duplicating copies the selection and undoing it frees the copies")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Cube = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		const TArray<Object*> Copies = Doc->DuplicateObjects({ Cube }, Vector3d(16.0f, 16.0f, 0.0f));
		REQUIRE(Copies.size() == 1);
		CHECK(Copies[0] != Cube);
		CHECK(Same(PositionOf(Copies[0]), Vector3d(16.0f, 16.0f, 0.0f)));
		CHECK(Doc->GetSelection() == Copies);
		CHECK(Doc->CountBrushes() == 2);
		Doc->Undo();
		CHECK(Doc->CountBrushes() == 1);
		CHECK(Doc->GetSelection() == TArray<Object*>{ Cube });
		// The copy belongs to the redo step now; a new edit deletes it.
		Doc->MoveObjects({ Cube }, Vector3d(0.0f, 0.0f, 8.0f));
		CHECK(!Doc->CanRedo());
	}

	TEST_CASE("Maps are saved as .qle with their game set and as .map, and read back")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Doc->AddPrefab(PrefabPath("Cube"), Vector3d(-64.0f, 32.0f, 0.0f));
		Doc->AddPrefab(PrefabPath("Pyramid"), Vector3d(128.0f, 0.0f, 0.0f));
		Doc->AddEntity("light", Vector3d(0.0f, 0.0f, 96.0f));
		FString Error;
		const FString Qle = Temp.File("test.qle");
		REQUIRE(Doc->Save(Qle, Error));
		CHECK(!Doc->IsModified());
		CHECK(Doc->GetPath() == Qle);
		CHECK(Doc->GetDisplayName() == "test.qle");

		FString Text;
		REQUIRE(FFileHelper::LoadFileToString(Text, Qle));
		CHECK(Text.StartsWith("// GENESIS EDITOR"));
		CHECK(Text.View().find("// Game: Genesis") != std::string_view::npos);
		CHECK(Text.View().find("// EntList: genesis.ent") != std::string_view::npos);

		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		CHECK(Reopened->CountBrushes() == 2);
		CHECK(Reopened->CountEntities() == 1);
		CHECK(Reopened->GetGameSet().IsBuiltIn());
		CHECK(Reopened->IsModified()); // legacy import needs a native save

		// Export: the document keeps its path; the .map imports.
		const FString Map = Temp.File("test.map");
		REQUIRE(Doc->Save(Map, Error));
		CHECK(Doc->GetPath() == Qle);
		TUniquePtr<FMapDocument> Imported = FMapDocument::Open(Map, Error);
		REQUIRE(Imported);
		CHECK(Imported->CountBrushes() == 2);
		CHECK(Imported->CountEntities() == 1);
		CHECK(Imported->GetPath() == Temp.File("test.gmap")); // native destination beside the import
		CHECK(Imported->IsModified());

		CHECK(!FMapDocument::Open(Temp.File("missing.qle"), Error));
		CHECK(!Error.IsEmpty());
	}

	TEST_CASE("Texture names the game does not have survive loading and saving")
	{
		EnsureMapCore();
		FTempFolder Temp;
		const FString Source = Temp.File("textured.map");
		const std::string Map =
			"{\n\"classname\" \"worldspawn\"\n{\n"
			"( 32 32 32 ) ( 32 -32 32 ) ( -32 -32 32 ) metal/floor_1 0 0 0 1 1\n"
			"( 32 32 32 ) ( 32 32 -32 ) ( 32 -32 -32 ) metal/wall 0 0 0 1 1\n"
			"( 32 -32 32 ) ( 32 -32 -32 ) ( -32 -32 -32 ) metal/wall 0 0 0 1 1\n"
			"( -32 -32 32 ) ( -32 -32 -32 ) ( -32 32 -32 ) metal/wall 0 0 0 1 1\n"
			"( -32 32 32 ) ( -32 32 -32 ) ( 32 32 -32 ) metal/wall 0 0 0 1 1\n"
			"( -32 32 -32 ) ( -32 -32 -32 ) ( 32 -32 -32 ) metal/ceiling 0 0 0 1 1\n"
			"}\n}\n";
		REQUIRE(FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(Map.data()), Map.size()), Source));
		FString Error;
		TUniquePtr<FMapDocument> Doc = FMapDocument::Open(Source, Error);
		REQUIRE(Doc);
		REQUIRE(Doc->CountBrushes() == 1);

		const FString Qle = Temp.File("textured.qle");
		REQUIRE(Doc->Save(Qle, Error));
		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		const FString Out = Temp.File("out.map");
		REQUIRE(Reopened->Save(Out, Error));
		FString Text;
		REQUIRE(FFileHelper::LoadFileToString(Text, Out));
		CHECK(Text.View().find("metal/floor_1") != std::string_view::npos);
		CHECK(Text.View().find("metal/ceiling") != std::string_view::npos);
		CHECK(Text.View().find("metal/wall") != std::string_view::npos);
	}
}

// ---------------------------------------------------------------------------
// Views: a top and a 3D view of a document, in a headless window, drawing on
// a real device.

namespace
{
	struct FViewFixture
	{
		// Declared so that the application (which keeps the window, the
		// viewport and so the client) goes before what the client uses.
		TUniquePtr<FRHIDevice> Device;
		TUniquePtr<FSlateRHIRenderingPolicy> Renderer;
		TUniquePtr<FMapRenderResources> Resources;
		FMapViewSettings Settings;
		TUniquePtr<FMapDocument> Doc;
		FUITestApp T;
		TSharedPtr<FMapViewportClient> Client;
		TSharedPtr<SWindow> Window;

		bool Create(ERHIBackend Backend, EMapViewType Type, bool bSoftware = true)
		{
			EnsureMapCore();
			FRHIDeviceDesc Desc;
			Desc.Backend = Backend;
			Desc.bSoftwareAdapter = bSoftware;
			Device = FRHIDevice::Create(Desc);
			if (!Device)
			{
				return false;
			}
			Renderer = MakeUnique<FSlateRHIRenderingPolicy>(*Device, *FUITestStyle::GetFontCache());
			REQUIRE(Renderer->Initialize());
			Resources = MakeUnique<FMapRenderResources>(*Device);
			Doc = FMapDocument::CreateNew();
			Client = std::make_shared<FMapViewportClient>(*Device, *Renderer, *Resources, Settings, Type);
			Client->SetDocument(Doc.get());
			Window = T.AddWindow(SNew(SViewport).ViewportClient(StaticCastSharedPtr<IViewportClient>(Client)), FVector2(256.0f, 256.0f)).ToSharedPtr();
			T.Tick();
			return true;
		}

		~FViewFixture()
		{
			if (Device)
			{
				Device->WaitIdle();
			}
			if (Client)
			{
				Client->SetDocument(nullptr);
			}
		}

		// Draws the view and reads the pixels back (RGBA8, 256 x 256 at 100%).
		TArray<uint8> Render()
		{
			Client->RequestRedraw();
			FRHICommandList& CommandList = Device->BeginFrame();
			Renderer->BeginFrame();
			Client->RenderIfNeeded(CommandList);
			Device->EndFrame();
			TArray<uint8> Pixels;
			auto* Target = static_cast<FSlateRenderTargetRHI*>(Client->GetRenderTarget().get());
			REQUIRE(Target);
			REQUIRE(Device->ReadbackTexture(Target->GetTexture(), Pixels));
			return Pixels;
		}

		FVector2 At(const Vector3d& Point) const
		{
			FVector2 Local;
			REQUIRE(Client->MapToScreen(Point, Local));
			return Local;
		}
	};

	FColor PixelAt(const TArray<uint8>& Pixels, int32 X, int32 Y)
	{
		const SIZE_T I = (static_cast<SIZE_T>(Y) * 256 + static_cast<SIZE_T>(X)) * 4;
		return FColor(Pixels[I], Pixels[I + 1], Pixels[I + 2], Pixels[I + 3]);
	}

	void RunSolidDepth(ERHIBackend Backend)
	{
		FTestLogCapture Log;
		FViewFixture F;
		if (!F.Create(Backend, EMapViewType::Perspective))
		{
			MESSAGE(ToString(Backend), " device unavailable; skipped");
			return;
		}
		F.Client->SetViewMode(EMapViewMode::Lit);
		const Vector3d Near(0.0f, 0.0f, 0.0f);
		Object* Front = F.Doc->AddPrefab(PrefabPath("Cube"), Near);
		F.Doc->ClearSelection();
		F.Client->FrameSelection();
		const FVector2 Center = F.At(Near);
		const int32 X = static_cast<int32>(Center.X);
		const int32 Y = static_cast<int32>(Center.Y);
		const TArray<uint8> Alone = F.Render();
		const FColor Background = PixelAt(Alone, 1, 255);
		const FColor FrontColor = PixelAt(Alone, X, Y);
		CHECK(FrontColor != Background);

		// A second cube behind the first (further along the view direction),
		// drawn after it: the depth buffer keeps the front one.
		// The placement point is on the camera's line of sight, past the cube.
		Vector3d Away = F.Client->GetPlacementPoint();
		Away.SubVector(Near).Normalize();
		Vector3d Behind(Near);
		Behind.AddVector(Away.MultVector(160.0f));
		Object* Back = F.Doc->AddPrefab(PrefabPath("Cube"), Behind);
		F.Doc->ClearSelection();
		const TArray<uint8> Both = F.Render();
		CHECK(PixelAt(Both, X, Y) == FrontColor);
		CHECK(Front != Back);
		CHECK(Log.Problems.empty());
		for (const FString& Problem : Log.Problems)
		{
			MESSAGE(Problem.c_str());
		}
	}
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("The top view picks, cycles, box-selects and moves with grid snapping")
	{
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Top))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		Object* Cube = F.Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		Object* Light = F.Doc->AddEntity("light", Vector3d(160.0f, 0.0f, 0.0f));
		F.Doc->ClearSelection();
		F.Client->FrameSelection();
		const TSharedRef<SWindow> Window = ToSharedRef(F.Window);

		F.T.Click(Window, F.At(Vector3d(0.0f, 0.0f, 0.0f)));
		F.T.Tick();
		CHECK(F.Doc->GetSelection() == TArray<Object*>{ Cube });
		F.T.Click(Window, F.At(Vector3d(80.0f, 60.0f, 0.0f))); // empty space
		F.T.Tick();
		CHECK(F.Doc->GetSelection().empty());

		// Ctrl+click adds.
		F.T.Click(Window, F.At(Vector3d(0.0f, 0.0f, 0.0f)));
		F.T.SetModifiers(false, true);
		F.T.Click(Window, F.At(Vector3d(160.0f, 0.0f, 0.0f)));
		F.T.SetModifiers(false, false);
		F.T.Tick();
		CHECK(F.Doc->GetSelection().size() == 2);

		// Box selection around the light only.
		F.T.MouseDown(Window, F.At(Vector3d(120.0f, 40.0f, 0.0f)));
		F.T.MouseMove(Window, F.At(Vector3d(140.0f, 20.0f, 0.0f)));
		F.T.MouseMove(Window, F.At(Vector3d(200.0f, -40.0f, 0.0f)));
		F.T.MouseUp(Window, F.At(Vector3d(200.0f, -40.0f, 0.0f)));
		F.T.Tick();
		CHECK(F.Doc->GetSelection() == TArray<Object*>{ Light });

		// Drag the cube by about 70 units: it lands on the 16-unit grid.
		F.T.Click(Window, F.At(Vector3d(0.0f, 0.0f, 0.0f)));
		F.T.Tick();
		F.T.MouseDown(Window, F.At(Vector3d(0.0f, 0.0f, 0.0f)));
		F.T.MouseMove(Window, F.At(Vector3d(20.0f, -10.0f, 0.0f)));
		F.T.MouseMove(Window, F.At(Vector3d(70.0f, -37.0f, 0.0f)));
		F.T.MouseUp(Window, F.At(Vector3d(70.0f, -37.0f, 0.0f)));
		F.T.Tick();
		Vector3d Min, Max;
		REQUIRE(F.Doc->GetSelectionBounds(Min, Max));
		CHECK(FMath::Abs(std::fmod(Min.GetX(), 16.0f)) < 0.01f);
		CHECK(FMath::Abs(std::fmod(Min.GetY(), 16.0f)) < 0.01f);
		CHECK(PositionOf(Cube).GetX() > 50.0f);
		CHECK(PositionOf(Cube).GetY() < -20.0f);
		CHECK(PositionOf(Cube).GetZ() == 0.0f); // the depth axis does not move
		CHECK(F.Doc->GetUndoDescription() == "Move Object");
		F.Doc->Undo();
		CHECK(Same(PositionOf(Cube), Vector3d(0.0f, 0.0f, 0.0f)));

		// The wheel zooms about the cursor: the point under it stays.
		const FVector2 Cursor = F.At(Vector3d(160.0f, 0.0f, 0.0f));
		const float Zoom = F.Client->GetZoom();
		F.T.Wheel(Window, Cursor, 2.0f);
		F.T.Tick();
		CHECK(F.Client->GetZoom() > Zoom);
		const FVector2 After = F.At(Vector3d(160.0f, 0.0f, 0.0f));
		CHECK(FMath::Abs(After.X - Cursor.X) < 0.5f);
		CHECK(FMath::Abs(After.Y - Cursor.Y) < 0.5f);

		// It draws: grid, wireframes and the label.
		const TArray<uint8> Pixels = F.Render();
		int32 Lit = 0;
		for (int32 Y = 0; Y < 256; ++Y)
		{
			for (int32 X = 0; X < 256; ++X)
			{
				const FColor C = PixelAt(Pixels, X, Y);
				Lit += C.R > 150 && C.G > 150 ? 1 : 0;
			}
		}
		CHECK(Lit > 20);
	}

	TEST_CASE("The 3D view draws solid faces with a depth buffer on D3D12 (WARP)")
	{
		RunSolidDepth(ERHIBackend::D3D12);
	}

	TEST_CASE("The 3D view draws solid faces with a depth buffer on Vulkan")
	{
		RunSolidDepth(ERHIBackend::Vulkan);
	}
}

// ---------------------------------------------------------------------------
// Editing operations (milestone 6).

namespace
{
	struct FBounds
	{
		Vector3d Min, Max;
		Vector3d Size() const
		{
			Vector3d S(Max);
			return S.SubVector(Min);
		}
	};

	FBounds BoundsOf(FMapDocument& Doc, const TArray<Object*>& Objects)
	{
		Doc.SetSelection(Objects);
		FBounds B;
		REQUIRE(Doc.GetSelectionBounds(B.Min, B.Max));
		return B;
	}
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("Rotating, scaling and flipping transform about a pivot and undo exactly")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Plate = Doc->AddPrefab(PrefabPath("Plate"), Vector3d(64.0f, 0.0f, 0.0f));
		const FBounds Before = BoundsOf(*Doc, { Plate });
		CHECK(Same(Before.Size(), Vector3d(320.0f, 320.0f, 32.0f)));

		Doc->RotateObjects({ Plate }, 0, 90.0f, Vector3d(64.0f, 0.0f, 0.0f));
		const FBounds Rotated = BoundsOf(*Doc, { Plate });
		CHECK(Same(Rotated.Size(), Vector3d(320.0f, 32.0f, 320.0f)));
		CHECK(Doc->GetUndoDescription() == "Rotate");
		Doc->Undo();
		const FBounds Restored = BoundsOf(*Doc, { Plate });
		CHECK(Same(Restored.Min, Before.Min));
		CHECK(Same(Restored.Max, Before.Max));
		Doc->Redo();
		CHECK(Same(BoundsOf(*Doc, { Plate }).Size(), Vector3d(320.0f, 32.0f, 320.0f)));
		Doc->Undo();

		Doc->ScaleObjects({ Plate }, Vector3d(0.5f, 1.0f, 2.0f), Vector3d(-96.0f, 0.0f, 0.0f));
		const FBounds Scaled = BoundsOf(*Doc, { Plate });
		CHECK(Same(Scaled.Size(), Vector3d(160.0f, 320.0f, 64.0f)));
		CHECK(FMath::Abs(Scaled.Min.GetX() + 96.0f) < 0.01f); // min x was at the pivot
		Doc->Undo();

		Object* Cube = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		Doc->FlipObjects({ Cube }, 0, Vector3d(100.0f, 0.0f, 0.0f));
		CHECK(Same(PositionOf(Cube), Vector3d(200.0f, 0.0f, 0.0f)));
		CHECK(Cube->GetBrush().IntegrityCheck()); // faces still face out
		Doc->Undo();
		CHECK(Same(PositionOf(Cube), Vector3d(0.0f, 0.0f, 0.0f)));
	}

	TEST_CASE("Hollowing, subtracting and intersecting brushes are undoable")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Room = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Doc->HollowObjects({ Room }, 8.0f, true));
		CHECK(Doc->CountBrushes() == 6);
		CHECK(Room->GetNumChildren() == 6); // the same object, now a group of walls
		Doc->Undo();
		CHECK(Doc->CountBrushes() == 1);
		CHECK(Room->HasBrush());

		// A thin cutter through the cube's middle cuts it into pieces.
		Object* Cutter = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		Doc->ScaleObjects({ Cutter }, Vector3d(0.25f, 2.0f, 2.0f), Vector3d(0.0f, 0.0f, 0.0f));
		Doc->SetSelection({ Cutter });
		REQUIRE(Doc->SubtractSelection());
		CHECK(Doc->CountBrushes() > 2);
		CHECK(Doc->GetSelection() == TArray<Object*>{ Cutter });
		Doc->Undo();
		CHECK(Doc->CountBrushes() == 2);

		// The two cubes' common volume replaces them.
		Doc->SetSelection({ Room, Cutter });
		REQUIRE(Doc->IntersectSelection());
		CHECK(Doc->CountBrushes() == 1);
		const FBounds Common = BoundsOf(*Doc, Doc->GetObjects());
		CHECK(Same(Common.Size(), Vector3d(16.0f, 64.0f, 64.0f)));
		Doc->Undo();
		CHECK(Doc->CountBrushes() == 2);
		CHECK(Doc->GetObjects() == TArray<Object*>{ Room, Cutter });
	}

	TEST_CASE("A doorway subtracted from a hollowed room, and undone")
	{
		EnsureMapCore();
		FTestLogCapture Log;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Room = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 128.0f));
		Doc->ScaleObjects({ Room }, Vector3d(8.0f, 8.0f, 4.0f), Vector3d(0.0f, 0.0f, 128.0f));
		REQUIRE(Doc->HollowObjects({ Room }, 16.0f, true));
		CHECK(Doc->CountBrushes() == 6);
		Object* Door = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(248.0f, 0.0f, 64.0f));
		Doc->ScaleObjects({ Door }, Vector3d(1.0f, 1.5f, 1.5f), Vector3d(248.0f, 0.0f, 64.0f));
		Doc->SetSelection({ Door });
		REQUIRE(Doc->SubtractSelection());
		Doc->DeleteObjects({ Door });
		CHECK(Doc->CountBrushes() > 6); // the wall with the doorway is in pieces
		while (Doc->CanUndo())
		{
			Doc->Undo();
		}
		CHECK(Doc->CountBrushes() == 0);
		while (Doc->CanRedo())
		{
			Doc->Redo();
		}
		CHECK(Doc->CountBrushes() > 6);
		CHECK(Log.Problems.empty());
		for (const FString& Problem : Log.Problems)
		{
			MESSAGE(Problem.c_str());
		}
	}

	TEST_CASE("Clipping keeps either side or splits, and removes what is cut away")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Cube = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		const Plane AtX16(Vector3d(1.0f, 0.0f, 0.0f), Vector3d(16.0f, 0.0f, 0.0f));

		REQUIRE(Doc->ClipObjects({ Cube }, AtX16, EMapClip::KeepBack));
		FBounds B = BoundsOf(*Doc, { Cube });
		CHECK(FMath::Abs(B.Max.GetX() - 16.0f) < 0.01f);
		CHECK(FMath::Abs(B.Min.GetX() + 32.0f) < 0.01f);
		Doc->Undo();

		REQUIRE(Doc->ClipObjects({ Cube }, AtX16, EMapClip::KeepFront));
		B = BoundsOf(*Doc, { Cube });
		CHECK(FMath::Abs(B.Min.GetX() - 16.0f) < 0.01f);
		Doc->Undo();

		REQUIRE(Doc->ClipObjects({ Cube }, AtX16, EMapClip::Split));
		CHECK(Doc->CountBrushes() == 2);
		CHECK(Doc->GetSelection().size() == 2);
		Doc->Undo();
		CHECK(Doc->CountBrushes() == 1);

		// A plane past the brush clips all of it away: the object goes.
		const Plane Beyond(Vector3d(1.0f, 0.0f, 0.0f), Vector3d(-100.0f, 0.0f, 0.0f));
		REQUIRE(Doc->ClipObjects({ Cube }, Beyond, EMapClip::KeepBack));
		CHECK(Doc->GetObjects().empty());
		Doc->Undo();
		CHECK(Doc->GetObjects() == TArray<Object*>{ Cube });
	}

	TEST_CASE("Groups keep their members in place, scope into them, and ungroup")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* A = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(-64.0f, 0.0f, 0.0f));
		Object* B = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(64.0f, 32.0f, 0.0f));
		const FBounds Before = BoundsOf(*Doc, { A, B });

		Object* Group = Doc->GroupObjects({ A, B });
		REQUIRE(Group);
		CHECK(FMapDocument::IsGroup(*Group));
		CHECK(Doc->GetObjects() == TArray<Object*>{ Group });
		CHECK(Doc->GetSelection() == TArray<Object*>{ Group });
		const FBounds Grouped = BoundsOf(*Doc, { Group });
		CHECK(Same(Grouped.Min, Before.Min));
		CHECK(Same(Grouped.Max, Before.Max));

		// Moving the group moves its members; edits inside it are in its space.
		Doc->MoveObjects({ Group }, Vector3d(0.0f, 0.0f, 16.0f));
		REQUIRE(Doc->SetScope(*Group));
		CHECK(Doc->GetObjects() == TArray<Object*>{ A, B });
		CHECK(Doc->GetSelection().empty());
		Doc->SetSelection({ A });
		Doc->MoveObjects({ A }, Vector3d(8.0f, 0.0f, 0.0f));
		Doc->ScopeUp();
		CHECK(&Doc->GetScope() == &Doc->GetRoot());
		CHECK(Doc->GetSelection() == TArray<Object*>{ Group });

		REQUIRE(Doc->UngroupObjects({ Group }));
		CHECK(Doc->GetObjects() == TArray<Object*>{ A, B });
		const FBounds Ungrouped = BoundsOf(*Doc, { A, B });
		CHECK(Same(Ungrouped.Min, Vector3d(Before.Min.GetX() + 8.0f, Before.Min.GetY(), Before.Min.GetZ() + 16.0f)));

		// Undo back through the ungroup and the move made inside the group.
		Doc->Undo();
		CHECK(Doc->GetObjects() == TArray<Object*>{ Group });
		Doc->Undo();
		CHECK(&Doc->GetScope() == Group); // that step was made in the group
		Doc->Undo();
		Doc->Undo();
		CHECK(&Doc->GetScope() == &Doc->GetRoot());
		CHECK(Doc->GetObjects() == TArray<Object*>{ A, B });
		CHECK(Same(BoundsOf(*Doc, { A, B }).Min, Before.Min));
		while (Doc->CanRedo())
		{
			Doc->Redo();
		}
		CHECK(Doc->GetObjects() == TArray<Object*>{ A, B });
		CHECK(Same(BoundsOf(*Doc, { A, B }).Min, Ungrouped.Min));
	}

	TEST_CASE("Names, entity keys and textures are undoable properties")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Light = Doc->AddEntity("light", Vector3d(0.0f, 0.0f, 64.0f));
		Object* Cube = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));

		Doc->SetEntityKey({ Light }, "light", "450");
		CHECK(FString(Light->GetEntityPtr()->GetKey("light")) == "450");
		Doc->SetEntityKey({ Light }, "light", "");
		CHECK(Light->GetEntityPtr()->GetKey("light") == nullptr);
		Doc->Undo();
		CHECK(FString(Light->GetEntityPtr()->GetKey("light")) == "450");

		Doc->SetObjectName(*Cube, "Pillar");
		CHECK(FString(Cube->GetObjName()) == "Pillar");
		Doc->Undo();
		CHECK(FString(Cube->GetObjName()) != "Pillar");

		Doc->SetTexture({ Cube }, "stone/wall_02");
		CHECK(FString(Cube->GetBrush().GetFaceTexturePtr(0)->GetTName()) == "stone/wall_02");
		FString Error;
		const FString Out = Temp.File("tex.map");
		REQUIRE(Doc->Save(Out, Error));
		FString Text;
		REQUIRE(FFileHelper::LoadFileToString(Text, Out));
		CHECK(Text.View().find("stone/wall_02") != std::string_view::npos);
		CHECK(Text.View().find("450") != std::string_view::npos);
	}

	TEST_CASE("The top view's tools rotate, scale, clip and edit brushes, and follow the scope")
	{
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Top))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		const TSharedRef<SWindow> Window = ToSharedRef(F.Window);
		Object* A = F.Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		Object* B = F.Doc->AddPrefab(PrefabPath("Cube"), Vector3d(128.0f, 0.0f, 0.0f));
		F.Doc->SelectAll();
		F.Client->FrameSelection();
		F.T.Tick();
		const auto Drag = [&](const Vector3d& From, const Vector3d& To)
		{
			F.T.MouseDown(Window, F.At(From));
			F.T.MouseMove(Window, F.At(From) + (F.At(To) - F.At(From)) * 0.3f);
			F.T.MouseMove(Window, F.At(To));
			F.T.MouseUp(Window, F.At(To));
			F.T.Tick();
		};

		// Rotate both a quarter turn counterclockwise on screen about their center.
		F.Settings.Tool = EMapTool::Rotate;
		const FVector2 PivotOnScreen = F.At(Vector3d(64.0f, 0.0f, 0.0f));
		Drag(Vector3d(160.0f, 0.0f, 0.0f), Vector3d(64.0f, 96.0f, 0.0f));
		CHECK(F.Doc->GetUndoDescription() == "Rotate");
		const FVector2 BOnScreen = F.At(PositionOf(B));
		CHECK(FMath::Abs(BOnScreen.X - PivotOnScreen.X) < 1.0f);
		CHECK(BOnScreen.Y < PivotOnScreen.Y - 10.0f); // above the pivot now
		F.Doc->Undo();
		CHECK(Same(PositionOf(B), Vector3d(128.0f, 0.0f, 0.0f)));

		// Scale A to twice its width from its left side.
		F.Doc->SetSelection({ A });
		F.Settings.Tool = EMapTool::Scale;
		Drag(Vector3d(32.0f, 0.0f, 0.0f), Vector3d(96.0f, 0.0f, 0.0f));
		Vector3d Min, Max;
		REQUIRE(F.Doc->GetSelectionBounds(Min, Max));
		CHECK(FMath::Abs(Min.GetX() + 32.0f) < 0.01f);
		CHECK(FMath::Abs(Max.GetX() - 96.0f) < 0.01f);
		F.Doc->Undo();

		// Clip A along a line through its middle; Enter keeps one side.
		F.Doc->SetSelection({ A });
		F.Settings.Tool = EMapTool::Clip;
		Drag(Vector3d(0.0f, -80.0f, 0.0f), Vector3d(0.0f, 80.0f, 0.0f));
		F.T.Key(EKey::Enter);
		F.T.Tick();
		CHECK(F.Doc->GetUndoDescription() == "Clip");
		REQUIRE(F.Doc->GetSelectionBounds(Min, Max));
		CHECK(FMath::Abs((Max.GetX() - Min.GetX()) - 32.0f) < 0.01f);
		F.Doc->Undo();

		// Drag a vertical edge of A out to (64, 64) with the edge tool (the
		// brush stays convex; a single corner pulled out would not).
		F.Doc->SetSelection({ A });
		F.Settings.Tool = EMapTool::Edge;
		F.Render(); // picking dots uses what the view computed when it drew
		Drag(Vector3d(32.0f, 32.0f, 0.0f), Vector3d(64.0f, 64.0f, 0.0f));
		CHECK(F.Doc->GetUndoDescription() == "Move Edge");
		REQUIRE(F.Doc->GetSelectionBounds(Min, Max));
		CHECK(FMath::Abs(Max.GetX() - 64.0f) < 0.01f);
		CHECK(FMath::Abs(Max.GetY() - 64.0f) < 0.01f);
		F.Doc->Undo();
		F.Settings.Tool = EMapTool::Select;

		// Entering a group keeps everything where it was on screen.
		const FVector2 AOnScreen = F.At(PositionOf(A));
		Object* Group = F.Doc->GroupObjects({ A, B });
		REQUIRE(F.Doc->SetScope(*Group));
		F.T.Tick();
		const FVector2 AInGroup = F.At(PositionOf(A)); // A's position is now in the group's space
		CHECK(FMath::Abs(AInGroup.X - AOnScreen.X) < 0.5f);
		CHECK(FMath::Abs(AInGroup.Y - AOnScreen.Y) < 0.5f);
		F.T.Click(Window, AInGroup);
		F.T.Tick();
		CHECK(F.Doc->GetSelection() == TArray<Object*>{ A });
		F.Doc->ScopeUp();
		F.T.Tick();
		CHECK(F.Doc->GetSelection() == TArray<Object*>{ Group });
	}

	TEST_CASE("Map objects show their name, position, texture and typed entity keys as properties")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Light = Doc->AddEntity("light", Vector3d(16.0f, 32.0f, 64.0f));
		FMapObjectProperties Props(*Doc, *Light);
		TArray<FPropertyDesc> Descs;
		Props.GetProperties(Descs);
		const auto Find = [&](const char* Name) -> const FPropertyDesc*
		{
			for (const FPropertyDesc& Desc : Descs)
			{
				if (Desc.Name == FName(Name))
				{
					return &Desc;
				}
			}
			return nullptr;
		};
		REQUIRE(Find("Key:light"));
		CHECK(Find("Key:light")->Type == EPropertyType::Int);
		REQUIRE(Find("Key:style"));
		CHECK(Find("Key:style")->Type == EPropertyType::Enum);
		CHECK(Find("Key:style")->EnumOptions.size() == 6);
		REQUIRE(Find("Flag:spawnflags:1"));
		CHECK(Find("Flag:spawnflags:1")->DisplayName == "Start off");
		CHECK(Find("Class"));
		CHECK(Find("Class")->bReadOnly);
		CHECK(!Find("Texture"));

		// Defaults come from the definition; setting stores the key.
		CHECK(std::get<int32>(Props.GetValue("Key:light")) == 300);
		Props.SetValue("Key:light", int32(500));
		CHECK(FString(Light->GetEntityPtr()->GetKey("light")) == "500");
		Props.SetValue("Key:style", int32(3));
		CHECK(FString(Light->GetEntityPtr()->GetKey("style")) == "3");
		CHECK(std::get<int32>(Props.GetValue("Key:style")) == 3);
		Props.SetValue("Flag:spawnflags:1", true);
		CHECK(FString(Light->GetEntityPtr()->GetKey("spawnflags")) == "1");
		CHECK(std::get<bool>(Props.GetValue("Flag:spawnflags:1")));
		Props.SetValue("Position", FPropertyVector3{ 32.0f, 32.0f, 64.0f });
		CHECK(Same(PositionOf(Light), Vector3d(32.0f, 32.0f, 64.0f)));
		Doc->Undo();
		CHECK(Same(PositionOf(Light), Vector3d(16.0f, 32.0f, 64.0f)));
		Doc->Undo();
		CHECK(Light->GetEntityPtr()->GetKey("spawnflags") == nullptr);

		// Keys the class does not define appear as text.
		Doc->SetEntityKey({ Light }, "_shadow", "1");
		Descs.clear();
		Props.GetProperties(Descs);
		REQUIRE(Find("Key:_shadow"));
		CHECK(Find("Key:_shadow")->Category == "Other keys");

		// Brushes have a texture for all their faces.
		Object* Cube = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		FMapObjectProperties CubeProps(*Doc, *Cube);
		Descs.clear();
		CubeProps.GetProperties(Descs);
		REQUIRE(Find("Texture"));
		CubeProps.SetValue("Texture", FString("brick"));
		CHECK(std::get<FString>(CubeProps.GetValue("Texture")) == "brick");
		CHECK(CubeProps.GetDisplayName() == "Cube");
	}

	TEST_CASE("The property grid shows map objects and edits them")
	{
		EnsureMapCore();
		FUITestApp T;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Light = Doc->AddEntity("light", Vector3d(0.0f, 0.0f, 64.0f));
		Object* Cube = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		TSharedPtr<SPropertyGrid> Grid;
		T.AddWindow(SAssignNew(Grid, SPropertyGrid)
			.OnBeginTransaction([&](const FString& Description) { Doc->BeginTransaction(Description); })
			.OnEndTransaction([&] { Doc->EndTransaction(); }), FVector2(320.0f, 480.0f));
		const auto Show = [&](TArray<Object*> Objects)
		{
			TArray<TSharedRef<IPropertySource>> Sources;
			for (Object* Obj : Objects)
			{
				Sources.push_back(TSharedRef<IPropertySource>(std::make_shared<FMapObjectProperties>(*Doc, *Obj)));
			}
			Grid->SetObjects(Sources);
			T.Tick();
			T.Tick();
		};
		Show({ Light });
		Show({ Cube });
		Show({ Light, Cube });
		Doc->RotateObjects({ Cube }, 2, 45.0f, Vector3d(0.0f, 0.0f, 0.0f));
		T.Tick();
		Show({ Cube });
		Doc->RotateObjects({ Cube }, 2, 45.0f, Vector3d(0.0f, 0.0f, 0.0f));
		T.Tick();
		Grid->SetPropertyValue("Key:light", int32(600));
		Show({ Light });
		Grid->SetPropertyValue("Key:light", int32(600));
		CHECK(FString(Light->GetEntityPtr()->GetKey("light")) == "600");
	}

	TEST_CASE("Brush editing moves a face or a vertex, and an invalid shape is undone")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Cube = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		Geometry& Brush = Cube->GetBrush();

		// Pull the top face up.
		const GPolygon* Top = nullptr;
		const LinkList<PlaneNode>& Planes = Brush.GetPlaneList();
		for (int Index = 0; Index < Planes.NumOfElm(); ++Index)
		{
			if (Planes[Index].GetPlane().GetNorm().GetZ() > 0.9f)
			{
				Top = &Planes[Index].GetPolygon();
			}
		}
		REQUIRE(Top);
		Doc->BeginBrushEdit(*Cube, "Move Face");
		Doc->MoveBrushFace(*Top, Vector3d(0.0f, 0.0f, 32.0f));
		CHECK(Doc->EndBrushEdit());
		CHECK(Same(BoundsOf(*Doc, { Cube }).Size(), Vector3d(64.0f, 64.0f, 96.0f)));
		CHECK(Doc->GetUndoDescription() == "Move Face");
		Doc->Undo();
		CHECK(Same(BoundsOf(*Doc, { Cube }).Size(), Vector3d(64.0f, 64.0f, 64.0f)));

		// A corner pushed through the brush makes it concave: refused.
		Geometry& Again = Cube->GetBrush();
		int32 Corner = 0;
		for (int Index = 0; Index < Again.GetNumVertices(); ++Index)
		{
			const Vector3d& V = Again.GetVertex(Index);
			if (V.GetX() > 0 && V.GetY() > 0 && V.GetZ() > 0)
			{
				Corner = Index;
			}
		}
		Doc->BeginBrushEdit(*Cube, "Move Vertex");
		Doc->MoveBrushVertex(Corner, Vector3d(-96.0f, -96.0f, -96.0f));
		CHECK(!Doc->EndBrushEdit());
		CHECK(!Doc->IsInTransaction());
		CHECK(Same(BoundsOf(*Doc, { Cube }).Size(), Vector3d(64.0f, 64.0f, 64.0f)));
		CHECK(Doc->GetUndoDescription() != "Move Vertex");
	}
}

// ---------------------------------------------------------------------------
// Static mesh actors: Unreal's basic shapes placed with a transform.

TEST_SUITE("MapEditor")
{
	TEST_CASE("The basic shapes are static meshes centered on their pivot, and rays hit them")
	{
		EnsureMapCore();
		const std::vector<std::string>& Names = StaticMesh::GetBasicShapeNames();
		CHECK(Names == std::vector<std::string>{ "Cube", "Sphere", "Cylinder", "Cone", "Plane" });
		for (const std::string& Name : Names)
		{
			const std::shared_ptr<const StaticMesh> Mesh = StaticMesh::Find(Name.c_str());
			REQUIRE(Mesh);
			Vector3d Min, Max;
			Mesh->GetBounds(Min, Max);
			CAPTURE(Name);
			CHECK(FMath::Abs(Max.GetX() - Min.GetX() - StaticMesh::BasicShapeSize) < 0.01f);
			CHECK(FMath::Abs(Max.GetY() - Min.GetY() - StaticMesh::BasicShapeSize) < 0.01f);
			CHECK(FMath::Abs(Max.GetX() + Min.GetX()) < 0.01f); // centered
			CHECK(FMath::Abs(Max.GetZ() - Min.GetZ() - (Name == "Plane" ? 0.0f : StaticMesh::BasicShapeSize)) < 0.01f);
			CHECK(!Mesh->GetTriangles().empty());
			CHECK(Mesh->GetEdgeList().size() > 0);
			CHECK(Mesh->GetBoundRadius() >= StaticMesh::BasicShapeSize * 0.5f - 0.01f);
		}
		CHECK(StaticMesh::Find("sphere") == StaticMesh::Find("Sphere"));
		CHECK(!StaticMesh::Find("Teapot"));

		// A ray down onto the cube hits its top, 64 units below the start at z = 128.
		const std::shared_ptr<const StaticMesh> Cube = StaticMesh::Find("Cube");
		const Line Down(Vector3d(10.0f, -5.0f, 128.0f), Vector3d(0.0f, 0.0f, -1.0f));
		float Depth = 0.0f;
		REQUIRE(Cube->HitMesh(Down, 0.0f, 1000.0f, Depth));
		CHECK(FMath::Abs(Depth - 64.0f) < 0.01f);
		const Line Beside(Vector3d(100.0f, 0.0f, 128.0f), Vector3d(0.0f, 0.0f, -1.0f));
		CHECK(!Cube->HitMesh(Beside, 0.0f, 1000.0f, Depth));
	}

	TEST_CASE("Static mesh actors keep their transform when rotated, scaled and flipped, and undo exactly")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Cube = Doc->AddMesh("Cube", Vector3d(64.0f, 0.0f, 0.0f));
		REQUIRE(Cube);
		CHECK(!Doc->AddMesh("Teapot", Vector3d(0.0f, 0.0f, 0.0f)));
		CHECK(Cube->HasMesh());
		CHECK(Doc->CountMeshes() == 1);
		CHECK(Doc->CountBrushes() == 0);
		CHECK(Doc->GetUndoDescription() == "Add Cube");
		CHECK(Same(BoundsOf(*Doc, { Cube }).Size(), Vector3d(128.0f, 128.0f, 128.0f)));

		const Vector3d Center(64.0f, 0.0f, 0.0f);
		Doc->RotateObjects({ Cube }, 2, 30.0f, Center);
		CHECK(Cube->HasMesh()); // not baked into a brush
		CHECK(Same(PositionOf(Cube), Center));
		const Vector3d Turned = FMapDocument::GetMeshRotation(*Cube);
		CHECK(FMath::Abs(FMath::Abs(Turned.GetZ()) - 30.0f) < 0.05f);
		CHECK(FMath::Abs(Turned.GetX()) < 0.05f);
		CHECK(FMath::Abs(Turned.GetY()) < 0.05f);

		Doc->ScaleObjects({ Cube }, Vector3d(2.0f, 2.0f, 2.0f), Center);
		CHECK(Same(Cube->GetScale(), Vector3d(2.0f, 2.0f, 2.0f)));
		CHECK(FMath::Abs(BoundsOf(*Doc, { Cube }).Size().GetZ() - 256.0f) < 0.05f);

		Doc->FlipObjects({ Cube }, 2, Center);
		CHECK(Cube->HasMesh());
		CHECK(FMath::Abs(BoundsOf(*Doc, { Cube }).Size().GetZ() - 256.0f) < 0.05f);

		Doc->Undo(); // flip
		Doc->Undo(); // scale
		CHECK(Same(Cube->GetScale(), Vector3d(1.0f, 1.0f, 1.0f)));
		Doc->Undo(); // rotate
		CHECK(Same(FMapDocument::GetMeshRotation(*Cube), Vector3d(0.0f, 0.0f, 0.0f)));
		CHECK(Same(PositionOf(Cube), Center));
		Doc->Redo();
		CHECK(FMath::Abs(FMath::Abs(FMapDocument::GetMeshRotation(*Cube).GetZ()) - 30.0f) < 0.05f);
	}

	TEST_CASE("Static mesh actors show the Transform and the Static Mesh in the details, and edit them")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Mesh = Doc->AddMesh("Cube", Vector3d(0.0f, 0.0f, 64.0f));
		REQUIRE(Mesh);
		FMapObjectProperties Props(*Doc, *Mesh);
		CHECK(Props.GetDisplayName() == "Cube");
		TArray<FPropertyDesc> Descs;
		Props.GetProperties(Descs);
		TArray<FString> Names;
		for (const FPropertyDesc& Desc : Descs)
		{
			Names.push_back(Desc.DisplayName);
		}
		CHECK(Names == TArray<FString>{ "Name", "Location", "Rotation", "Scale", "Static Mesh" });

		// A rotation set in degrees reads back the same (roll, pitch, yaw).
		Props.SetValue("Rotation", FPropertyVector3{ 10.0f, 20.0f, 30.0f });
		const FPropertyVector3 Rotation = std::get<FPropertyVector3>(Props.GetValue("Rotation"));
		CHECK(FMath::Abs(Rotation.X - 10.0f) < 0.05f);
		CHECK(FMath::Abs(Rotation.Y - 20.0f) < 0.05f);
		CHECK(FMath::Abs(Rotation.Z - 30.0f) < 0.05f);
		CHECK(Doc->GetUndoDescription() == "Rotate");

		Props.SetValue("Scale", FPropertyVector3{ 1.0f, 2.0f, 3.0f });
		CHECK(Same(Mesh->GetScale(), Vector3d(1.0f, 2.0f, 3.0f)));
		Props.SetValue("Position", FPropertyVector3{ 16.0f, 32.0f, 48.0f });
		CHECK(Same(PositionOf(Mesh), Vector3d(16.0f, 32.0f, 48.0f)));

		Props.SetValue("StaticMesh", static_cast<int32>(1));
		CHECK(std::string(Mesh->GetMesh().GetName()) == "Sphere");
		CHECK(std::get<int32>(Props.GetValue("StaticMesh")) == 1);
		Doc->Undo();
		CHECK(std::string(Mesh->GetMesh().GetName()) == "Cube");
	}

	TEST_CASE("Static mesh actors are saved in .qle with their transform and left out of .map")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Doc->AddPrefab(PrefabPath("Cube"), Vector3d(-128.0f, 0.0f, 0.0f));
		Object* Sphere = Doc->AddMesh("Sphere", Vector3d(64.0f, 32.0f, 16.0f));
		REQUIRE(Sphere);
		Doc->SetMeshRotation(*Sphere, Vector3d(0.0f, 0.0f, 45.0f));
		Doc->SetMeshScale(*Sphere, Vector3d(1.5f, 1.5f, 0.5f));
		Doc->AddEntity("light", Vector3d(0.0f, 0.0f, 128.0f));
		FString Error;
		const FString Qle = Temp.File("meshes.qle");
		REQUIRE(Doc->Save(Qle, Error));
		FString Text;
		REQUIRE(FFileHelper::LoadFileToString(Text, Qle));
		CHECK(Text.View().find("StaticMesh: Sphere") != std::string_view::npos);

		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		CHECK(Reopened->CountMeshes() == 1);
		CHECK(Reopened->CountBrushes() == 1);
		CHECK(Reopened->CountEntities() == 1);
		Object* Loaded = nullptr;
		for (Object* Obj : Reopened->GetObjects())
		{
			Loaded = Obj->HasMesh() ? Obj : Loaded;
		}
		REQUIRE(Loaded);
		CHECK(std::string(Loaded->GetMesh().GetName()) == "Sphere");
		CHECK(Same(PositionOf(Loaded), Vector3d(64.0f, 32.0f, 16.0f)));
		CHECK(Same(Loaded->GetScale(), Vector3d(1.5f, 1.5f, 0.5f)));
		CHECK(FMath::Abs(FMapDocument::GetMeshRotation(*Loaded).GetZ() - 45.0f) < 0.05f);

		const FString Map = Temp.File("meshes.map");
		REQUIRE(Doc->Save(Map, Error));
		TUniquePtr<FMapDocument> Imported = FMapDocument::Open(Map, Error);
		REQUIRE(Imported);
		CHECK(Imported->CountMeshes() == 0);
		CHECK(Imported->CountBrushes() == 1);
		CHECK(Imported->CountEntities() == 1);
	}

	TEST_CASE("CSG, clipping and hollowing leave static mesh actors alone")
	{
		EnsureMapCore();
		FTestLogCapture Log;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Brush = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 0.0f));
		Object* Mesh = Doc->AddMesh("Cube", Vector3d(16.0f, 0.0f, 0.0f));
		REQUIRE(Mesh);

		// A mesh is no cutter, and is not cut.
		Doc->SetSelection({ Mesh });
		CHECK(!Doc->SubtractSelection());
		CHECK(Doc->CountBrushes() == 1);
		Doc->SetSelection({ Brush });
		Doc->SubtractSelection();
		CHECK(Mesh->HasMesh());
		CHECK(Doc->CountMeshes() == 1);

		const Plane AtX0(Vector3d(1.0f, 0.0f, 0.0f), Vector3d(0.0f, 0.0f, 0.0f));
		CHECK(!Doc->ClipObjects({ Mesh }, AtX0, EMapClip::KeepBack));
		CHECK(!Doc->HollowObjects({ Mesh }, 8.0f, true));
		CHECK(Doc->CountMeshes() == 1);

		// In a group with a brush, the clip cuts the brush and keeps the mesh.
		Object* Other = Doc->AddPrefab(PrefabPath("Cube"), Vector3d(200.0f, 0.0f, 0.0f));
		Object* Group = Doc->GroupObjects({ Other, Mesh });
		REQUIRE(Group);
		const Plane AtX200(Vector3d(1.0f, 0.0f, 0.0f), Vector3d(200.0f, 0.0f, 0.0f));
		CHECK(Doc->ClipObjects({ Group }, AtX200, EMapClip::KeepBack));
		CHECK(Doc->CountMeshes() == 1);
		CHECK(Mesh->HasMesh());
		CHECK(Log.Problems.empty());
		for (const FString& Problem : Log.Problems)
		{
			MESSAGE(Problem.c_str());
		}
	}

	TEST_CASE("The perspective view draws static mesh actors lit and as wireframes")
	{
		FTestLogCapture Log;
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Perspective))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		Object* Sphere = F.Doc->AddMesh("Sphere", Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Sphere);
		F.Client->FrameSelection();
		F.Doc->ClearSelection();
		const FVector2 Center = F.At(Vector3d(0.0f, 0.0f, 0.0f));
		const int32 X = static_cast<int32>(Center.X);
		const int32 Y = static_cast<int32>(Center.Y);

		F.Client->SetViewMode(EMapViewMode::Lit);
		const TArray<uint8> Lit = F.Render();
		const FColor Background = PixelAt(Lit, 1, 255);
		CHECK(PixelAt(Lit, X, Y) != Background);
		// Lit from one side: the sphere is not one flat color.
		CHECK(PixelAt(Lit, X - 20, Y) != PixelAt(Lit, X + 20, Y));

		F.Client->SetViewMode(EMapViewMode::Wireframe);
		const TArray<uint8> Wire = F.Render();
		int32 Lines = 0;
		for (int32 Dx = -40; Dx <= 40; ++Dx)
		{
			Lines += PixelAt(Wire, X + Dx, Y) != PixelAt(Wire, 1, 255) ? 1 : 0;
		}
		CHECK(Lines > 0);
		CHECK(Log.Problems.empty());
		for (const FString& Problem : Log.Problems)
		{
			MESSAGE(Problem.c_str());
		}
	}
}

// ---------------------------------------------------------------------------
// Landscapes: heightfields sculpted in the Landscape mode.


namespace
{
	float HeightAt(const Object& Land, int32 X, int32 Y)
	{
		return Land.GetLandscape().GetHeight(X, Y);
	}

	bool AllFlat(const Object& Land)
	{
		for (const float H : Land.GetLandscape().GetHeights())
		{
			if (H != 0.0f)
			{
				return false;
			}
		}
		return true;
	}
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("A landscape is a grid of heights centered on its pivot, with a mesh to draw and pick")
	{
		EnsureMapCore();
		const Landscape Field(4, 2, 32.0f);
		CHECK(Field.GetPointsX() == 5);
		CHECK(Field.GetPointsY() == 3);
		CHECK(Field.GetHeights().size() == 15);
		CHECK(Same(Field.GetPoint(0, 0), Vector3d(-64.0f, -32.0f, 0.0f)));
		CHECK(Same(Field.GetPoint(4, 2), Vector3d(64.0f, 32.0f, 0.0f)));
		CHECK(Field.GetMesh()->GetTriangles().size() == 16);
		CHECK(Field.GetMesh()->GetEdgeList().size() == 4 * 3 + 5 * 2);

		std::vector<float> Heights(15, 0.0f);
		Heights[7] = 10.0f; // (2, 1), the middle
		const std::shared_ptr<const Landscape> Bumped = Field.WithHeights(Heights);
		REQUIRE(Bumped);
		CHECK(Bumped->GetHeight(2, 1) == 10.0f);
		CHECK(FMath::Abs(Bumped->SampleHeight(16.0f, 0.0f) - 5.0f) < 0.01f); // halfway to the next point
		CHECK(Bumped->SampleHeight(1000.0f, 1000.0f) == 0.0f);                 // clamped at the edge
		CHECK(!Field.WithHeights(std::vector<float>(3, 0.0f)));
		CHECK(Field.GetHeight(2, 1) == 0.0f); // the original never changes

		// The mesh follows the heights: a ray down hits the bump's top.
		float Depth = 0.0f;
		REQUIRE(Bumped->GetMesh()->HitMesh(Line(Vector3d(0.0f, 0.0f, 100.0f), Vector3d(0.0f, 0.0f, -1.0f)), 0.0f, 1000.0f, Depth));
		CHECK(FMath::Abs(Depth - 90.0f) < 0.01f);
	}

	TEST_CASE("The Landscape mode sculpts, smooths, flattens and roughens, a stroke at a time")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		Mode.NewQuadsX = 32;
		Mode.NewQuadsY = 32;
		Mode.NewQuadSize = 32.0f;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		CHECK(Land->HasLandscape());
		CHECK(Land->HasMesh());
		CHECK(Doc->GetUndoDescription() == "Add Landscape");
		CHECK(AllFlat(*Land));

		// A ray from above finds it.
		Vector3d Hit;
		CHECK(Mode.TraceLandscape(Vector3d(40.0f, -20.0f, 500.0f), Vector3d(0.0f, 0.0f, -1.0f), Hit) == Land);
		CHECK(Same(Hit, Vector3d(40.0f, -20.0f, 0.0f)));
		CHECK(!Mode.TraceLandscape(Vector3d(5000.0f, 0.0f, 500.0f), Vector3d(0.0f, 0.0f, -1.0f), Hit));

		// Sculpt: a stroke at the middle raises it, not the corner; one undo step.
		Mode.Tool = ELandscapeTool::Sculpt;
		Mode.Brush.Radius = 128.0f;
		Mode.Brush.Strength = 1.0f;
		Mode.Brush.Falloff = 0.5f;
		const Vector3d Middle(0.0f, 0.0f, 0.0f);
		REQUIRE(Mode.BeginStroke(*Land, Middle, false));
		CHECK(Mode.IsStroking());
		for (int32 Tick = 0; Tick < 10; ++Tick)
		{
			Mode.ApplyAt(*Land, Middle, 1.0f / 60.0f, false);
		}
		Mode.EndStroke();
		CHECK(!Doc->IsInTransaction());
		const float Raised = HeightAt(*Land, 16, 16);
		CHECK(Raised > 40.0f); // 256 units a second at full strength, 11 steps of 1/60 s
		CHECK(HeightAt(*Land, 16 + 3, 16) < Raised); // falloff
		CHECK(HeightAt(*Land, 0, 0) == 0.0f);         // outside the brush
		CHECK(Doc->GetUndoDescription() == "Landscape Sculpt");
		Doc->Undo();
		CHECK(AllFlat(*Land));
		Doc->Redo();
		CHECK(HeightAt(*Land, 16, 16) == Raised);

		// Shift lowers.
		Mode.BeginStroke(*Land, Middle, true);
		Mode.EndStroke();
		CHECK(HeightAt(*Land, 16, 16) < Raised);

		// Smooth brings the peak down toward its neighbours.
		const float Peak = HeightAt(*Land, 16, 16);
		Mode.Tool = ELandscapeTool::Smooth;
		Mode.Brush.Radius = 64.0f;
		Mode.BeginStroke(*Land, Middle, false);
		Mode.ApplyAt(*Land, Middle, 0.5f, false);
		Mode.EndStroke();
		CHECK(HeightAt(*Land, 16, 16) < Peak);

		// Flatten pulls the ground to where the stroke began (the edge, 0).
		Mode.Tool = ELandscapeTool::Flatten;
		Mode.Brush.Radius = 256.0f;
		const Vector3d Edge(-400.0f, -400.0f, 0.0f);
		Mode.BeginStroke(*Land, Edge, false);
		for (int32 Tick = 0; Tick < 30; ++Tick)
		{
			Mode.ApplyAt(*Land, Middle, 0.1f, false);
		}
		Mode.EndStroke();
		CHECK(FMath::Abs(HeightAt(*Land, 16, 16)) < 1.0f);

		// Noise roughens a flat landscape.
		while (Doc->CanUndo() && Doc->GetUndoDescription() != "Add Landscape")
		{
			Doc->Undo();
		}
		CHECK(AllFlat(*Land));
		Mode.Tool = ELandscapeTool::Noise;
		Mode.BeginStroke(*Land, Middle, false);
		Mode.ApplyAt(*Land, Middle, 0.2f, false);
		Mode.EndStroke();
		CHECK(!AllFlat(*Land));
	}

	TEST_CASE("Landscapes are saved in .qle with their heights and left out of .map")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Land = Doc->AddLandscape(40, 24, 16.0f, Vector3d(32.0f, 0.0f, -16.0f));
		REQUIRE(Land);
		std::vector<float> Heights = Land->GetLandscape().GetHeights();
		for (SIZE_T Index = 0; Index < Heights.size(); ++Index)
		{
			Heights[Index] = static_cast<float>(Index % 17) * 2.5f;
		}
		Doc->SetLandscape(*Land, Land->GetLandscape().WithHeights(Heights));
		Doc->AddPrefab(PrefabPath("Cube"), Vector3d(0.0f, 0.0f, 64.0f));
		FString Error;
		const FString Qle = Temp.File("land.qle");
		REQUIRE(Doc->Save(Qle, Error));

		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		Object* Loaded = nullptr;
		for (Object* Obj : Reopened->GetObjects())
		{
			Loaded = Obj->HasLandscape() ? Obj : Loaded;
		}
		REQUIRE(Loaded);
		CHECK(Loaded->GetLandscape().GetQuadsX() == 40);
		CHECK(Loaded->GetLandscape().GetQuadsY() == 24);
		CHECK(Loaded->GetLandscape().GetQuadSize() == 16.0f);
		CHECK(Loaded->GetLandscape().GetHeights() == Heights);
		CHECK(Same(PositionOf(Loaded), Vector3d(32.0f, 0.0f, -16.0f)));
		CHECK(Reopened->CountBrushes() == 1);

		const FString Map = Temp.File("land.map");
		REQUIRE(Doc->Save(Map, Error));
		TUniquePtr<FMapDocument> Imported = FMapDocument::Open(Map, Error);
		REQUIRE(Imported);
		CHECK(Imported->CountMeshes() == 0);
		CHECK(Imported->CountBrushes() == 1);
	}

	TEST_CASE("In the Landscape mode a click on a landscape in the 3D view sculpts it")
	{
		FTestLogCapture Log;
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Perspective))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		FMapLandscapeMode Mode;
		Mode.SetDocument(F.Doc.get());
		Mode.NewQuadsX = 32;
		Mode.NewQuadsY = 32;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		F.Doc->ClearSelection();
		F.Client->ResetCamera();
		F.Settings.EditorMode = &Mode;
		F.Render();
		const FVector2 Center = F.At(Vector3d(0.0f, 0.0f, 0.0f));
		Vector3d RayOrigin, RayDirection, RayHit;
		REQUIRE(F.Client->GetCursorRay(Center, RayOrigin, RayDirection));
		CHECK(Mode.TraceLandscape(RayOrigin, RayDirection, RayHit) == Land);
		CHECK(FMath::Abs(RayHit.GetZ()) < 0.01f);
		// A click without the mode selects the landscape; with it, a quick
		// second press (a double click) is a stroke.
		F.Settings.EditorMode = nullptr;
		F.T.Click(ToSharedRef(F.Window), Center);
		CHECK(F.Doc->IsSelected(Land));
		F.Doc->ClearSelection();
		F.Settings.EditorMode = &Mode;
		F.T.MouseMove(ToSharedRef(F.Window), Center);
		F.T.MouseDown(ToSharedRef(F.Window), Center);
		F.T.Tick();
		CHECK(Mode.IsStroking());
		CHECK(F.Doc->IsInTransaction());
		F.Render(); // ticks the stroke
		F.T.MouseUp(ToSharedRef(F.Window), Center);
		F.T.Tick();
		CHECK(!Mode.IsStroking());
		CHECK(!F.Doc->IsInTransaction());
		CHECK(F.Doc->GetUndoDescription() == "Landscape Sculpt");
		CHECK(HeightAt(*Land, 16, 16) > 0.0f);
		CHECK(F.Doc->GetSelection().empty()); // the press sculpted instead of selecting
		const TArray<uint8> Lit = F.Render();
		CHECK(PixelAt(Lit, static_cast<int32>(Center.X), static_cast<int32>(Center.Y)) != PixelAt(Lit, 1, 1));
		F.Settings.EditorMode = nullptr;
		CHECK(Log.Problems.empty());
		for (const FString& Problem : Log.Problems)
		{
			MESSAGE(Problem.c_str());
		}
	}
}

// ---------------------------------------------------------------------------
// Landscape paint layers.

TEST_SUITE("MapEditor")
{
	TEST_CASE("Paint layers color a landscape's mesh, blended over the ground's own color")
	{
		EnsureMapCore();
		const Landscape Field(2, 2, 32.0f);
		CHECK(!Field.GetMesh()->HasVertexColors());
		LandscapeLayer Red = Field.MakeLayer("Red", 255, 0, 0);
		CHECK(Red.weights.size() == 9);
		Red.weights[4] = 1.0f;  // the middle, all red
		Red.weights[0] = 0.5f;  // a corner, half
		const std::shared_ptr<const Landscape> Painted = Field.WithLayers({ Red });
		REQUIRE(Painted);
		CHECK(Painted->GetNumLayers() == 1);
		CHECK(Painted->GetWeight(0, 1, 1) == 1.0f);
		const StaticMesh& Mesh = *Painted->GetMesh();
		REQUIRE(Mesh.HasVertexColors());
		CHECK((Mesh.GetVertexColor(4) & 0xFFFFFFu) == 0x0000FFu); // red
		unsigned char R, G, B;
		Landscape::GetBaseColor(R, G, B);
		CHECK((Mesh.GetVertexColor(8) & 0xFFu) == R); // unpainted: the ground's color
		CHECK(((Mesh.GetVertexColor(0) >> 8) & 0xFFu) == static_cast<unsigned int>(G * 0.5f + 0.5f));
		// Sculpting keeps the paint.
		const std::shared_ptr<const Landscape> Raised = Painted->WithHeights(std::vector<float>(9, 10.0f));
		REQUIRE(Raised);
		CHECK(Raised->GetWeight(0, 1, 1) == 1.0f);
		// Weights must match the grid.
		LandscapeLayer Short = Red;
		Short.weights.resize(3);
		CHECK(!Field.WithLayers({ Short }));
	}

	TEST_CASE("Painting a layer makes the others give way, erasing takes it off, and layers are undoable")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		Mode.NewQuadsX = 16;
		Mode.NewQuadsY = 16;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		Doc->ClearSelection();
		CHECK(Mode.GetTargetLandscape() == Land);

		REQUIRE(Mode.AddLayer(*Land));
		REQUIRE(Mode.AddLayer(*Land));
		CHECK(Doc->GetUndoDescription() == "Add Paint Layer");
		REQUIRE(Land->GetLandscape().GetNumLayers() == 2);
		CHECK(Land->GetLandscape().GetLayers()[0].name == "Grass");
		CHECK(Land->GetLandscape().GetLayers()[1].name == "Rock");
		CHECK(Mode.SelectedLayer == 1);

		// Grass at full strength over the middle.
		Mode.Tool = ELandscapeTool::Paint;
		Mode.SelectedLayer = 0;
		Mode.Brush.Radius = 96.0f;
		Mode.Brush.Strength = 1.0f;
		Mode.Brush.Falloff = 0.0f;
		const Vector3d Middle(0.0f, 0.0f, 0.0f);
		Mode.BeginStroke(*Land, Middle, false);
		Mode.ApplyAt(*Land, Middle, 0.5f, false);
		Mode.EndStroke();
		CHECK(Doc->GetUndoDescription() == "Landscape Paint");
		CHECK(Land->GetLandscape().GetWeight(0, 8, 8) == 1.0f);
		CHECK(Land->GetLandscape().GetWeight(0, 0, 0) == 0.0f);

		// Rock over it: grass gives way, the two never add up to more than 1.
		Mode.SelectedLayer = 1;
		Mode.Brush.Strength = 0.25f;
		Mode.BeginStroke(*Land, Middle, false);
		Mode.ApplyAt(*Land, Middle, 0.25f, false);
		Mode.EndStroke();
		const float Rock = Land->GetLandscape().GetWeight(1, 8, 8);
		const float Grass = Land->GetLandscape().GetWeight(0, 8, 8);
		CHECK(Rock > 0.2f);
		CHECK(Grass < 1.0f);
		CHECK(Rock + Grass <= 1.0001f);

		// Shift takes it off again.
		Mode.BeginStroke(*Land, Middle, true);
		Mode.ApplyAt(*Land, Middle, 1.0f, true);
		Mode.EndStroke();
		CHECK(Land->GetLandscape().GetWeight(1, 8, 8) == 0.0f);

		// Undo steps back through the strokes, one each.
		Doc->Undo();
		CHECK(Land->GetLandscape().GetWeight(1, 8, 8) == Rock);
		Doc->Undo();
		CHECK(Land->GetLandscape().GetWeight(0, 8, 8) == 1.0f);

		// Renaming, recoloring and removing.
		CHECK(Mode.RenameLayer(*Land, 1, "Cliff"));
		CHECK(Land->GetLandscape().GetLayers()[1].name == "Cliff");
		CHECK(Mode.SetLayerColor(*Land, 1, FColor(10, 20, 30)));
		CHECK(Land->GetLandscape().GetLayers()[1].b == 30);
		CHECK(Doc->GetUndoDescription() == "Change Paint Layer Color");
		CHECK(Mode.RemoveLayer(*Land, 0));
		CHECK(Land->GetLandscape().GetNumLayers() == 1);
		CHECK(Land->GetLandscape().GetLayers()[0].name == "Cliff");
		CHECK(Mode.SelectedLayer == 0);
		Doc->Undo();
		CHECK(Land->GetLandscape().GetNumLayers() == 2);
		CHECK(Land->GetLandscape().GetWeight(0, 8, 8) == 1.0f);

		// Painting without a layer does nothing.
		Mode.SelectedLayer = 5;
		CHECK(!Mode.ApplyAt(*Land, Middle, 0.5f, false));
	}

	TEST_CASE("Paint layers are saved in .qle with their names, colors and weights")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Land = Doc->AddLandscape(20, 10, 32.0f, Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		LandscapeLayer Sand = Land->GetLandscape().MakeLayer("Wet Sand", 200, 180, 120);
		for (SIZE_T Index = 0; Index < Sand.weights.size(); ++Index)
		{
			Sand.weights[Index] = static_cast<float>(Index % 5) * 0.25f;
		}
		LandscapeLayer Snow = Land->GetLandscape().MakeLayer("Snow", 240, 240, 250);
		Snow.weights[3] = 0.5f;
		Doc->SetLandscape(*Land, Land->GetLandscape().WithLayers({ Sand, Snow }), "Paint");
		FString Error;
		const FString Qle = Temp.File("layers.qle");
		REQUIRE(Doc->Save(Qle, Error));

		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		Object* Loaded = nullptr;
		for (Object* Obj : Reopened->GetObjects())
		{
			Loaded = Obj->HasLandscape() ? Obj : Loaded;
		}
		REQUIRE(Loaded);
		const std::vector<LandscapeLayer>& Layers = Loaded->GetLandscape().GetLayers();
		REQUIRE(Layers.size() == 2);
		CHECK(Layers[0].name == "Wet Sand");
		CHECK(Layers[0].r == 200);
		CHECK(Layers[0].g == 180);
		CHECK(Layers[0].b == 120);
		CHECK(Layers[0].weights == Sand.weights);
		CHECK(Layers[1].name == "Snow");
		CHECK(Layers[1].weights == Snow.weights);
		CHECK(Loaded->GetMesh().HasVertexColors());
	}

	TEST_CASE("A painted layer shows in its color in the Lit view")
	{
		FTestLogCapture Log;
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Perspective))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		FMapLandscapeMode Mode;
		Mode.SetDocument(F.Doc.get());
		Mode.NewQuadsX = 32;
		Mode.NewQuadsY = 32;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		REQUIRE(Mode.AddLayer(*Land, "Red", FColor(220, 30, 30)));
		F.Doc->ClearSelection();
		F.Client->ResetCamera();
		F.Client->SetViewMode(EMapViewMode::Lit);
		F.Render();
		const FVector2 Center = F.At(Vector3d(0.0f, 0.0f, 0.0f));
		const int32 X = static_cast<int32>(Center.X);
		const int32 Y = static_cast<int32>(Center.Y);
		const FColor Before = PixelAt(F.Render(), X, Y);

		Mode.Tool = ELandscapeTool::Paint;
		Mode.Brush.Radius = 200.0f;
		Mode.Brush.Strength = 1.0f;
		Mode.BeginStroke(*Land, Vector3d(0.0f, 0.0f, 0.0f), false);
		Mode.ApplyAt(*Land, Vector3d(0.0f, 0.0f, 0.0f), 1.0f, false);
		Mode.EndStroke();
		const FColor After = PixelAt(F.Render(), X, Y);
		CHECK(After != Before);
		CHECK(After.R > After.G + 40);
		CHECK(After.R > After.B + 40);
		CHECK(Log.Problems.empty());
		for (const FString& Problem : Log.Problems)
		{
			MESSAGE(Problem.c_str());
		}
	}
}

// ---------------------------------------------------------------------------
// Heightmap and weightmap import and export.

namespace
{
	// A landscape with a slope along X and a bump toward +Y.
	std::shared_ptr<const Landscape> MakeTestTerrain(int32 QuadsX, int32 QuadsY)
	{
		std::vector<float> Heights;
		for (int32 Y = 0; Y <= QuadsY; ++Y)
		{
			for (int32 X = 0; X <= QuadsX; ++X)
			{
				Heights.push_back(static_cast<float>(X) * 4.0f - 30.0f + (Y == QuadsY ? 100.0f : 0.0f));
			}
		}
		return Landscape::Create(QuadsX, QuadsY, 32.0f, Heights);
	}

	void WriteBytes(const FString& Path, const TArray<uint8>& Bytes)
	{
		FFile File;
		REQUIRE(File.Open(Path, EFileOpenMode::Write));
		REQUIRE(File.Write(Bytes.data(), static_cast<int64>(Bytes.size())) == static_cast<int64>(Bytes.size()));
	}
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("Heightmaps round-trip through 16-bit PNG and RAW, the image's top at +Y")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Land = Doc->AddLandscape(MakeTestTerrain(12, 7), Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());

		FString Error;
		const FString Png = Temp.File("terrain.png");
		const FString Raw = Temp.File("terrain.r16");
		REQUIRE(Mode.ExportHeightmap(*Land, Png, Error));
		REQUIRE(Mode.ExportHeightmap(*Land, Raw, Error));
		FHeightmap Map;
		REQUIRE(FHeightmap::Load(Png, Map, Error));
		CHECK(Map.Width == 13);
		CHECK(Map.Height == 8);
		// Mid-grey is 0; the bump (+Y, the last row) is the image's top row.
		CHECK(Map.At(0, 0) > Map.At(0, 7));
		CHECK(Map.At(0, 7) == static_cast<uint16>(32768 - 30 * 128));
		// A non-square .r16 can't say its size, so it doesn't load.
		CHECK(!FHeightmap::Load(Raw, Map, Error));
		CHECK(Error.Contains("square"));

		// Into a fresh landscape: the same heights.
		Object* Imported = Mode.ImportLandscape(Png, Vector3d(0.0f, 0.0f, 0.0f), Error);
		REQUIRE(Imported);
		CHECK(Doc->GetUndoDescription() == "Import Landscape");
		const Landscape& Copy = Imported->GetLandscape();
		REQUIRE(Copy.GetQuadsX() == 12);
		REQUIRE(Copy.GetQuadsY() == 7);
		for (int32 Y = 0; Y <= 7; ++Y)
		{
			for (int32 X = 0; X <= 12; ++X)
			{
				CHECK(std::fabs((Copy.GetHeight(X, Y)) - (Land->GetLandscape().GetHeight(X, Y))) <= 0.01);
			}
		}

		// Square ones round-trip through RAW too, and onto a landscape of
		// another size (stretched).
		Object* Square = Doc->AddLandscape(MakeTestTerrain(8, 8), Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Mode.ExportHeightmap(*Square, Raw, Error));
		REQUIRE(FHeightmap::Load(Raw, Map, Error));
		CHECK(Map.Width == 9);
		Object* Flat = Doc->AddLandscape(16, 16, 16.0f, Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Mode.ImportHeightmap(*Flat, Raw, Error));
		CHECK(Doc->GetUndoDescription() == "Import Heightmap");
		CHECK(std::fabs((Flat->GetLandscape().GetHeight(16, 16)) - (Square->GetLandscape().GetHeight(8, 8))) <= 0.01);
		CHECK(std::fabs((Flat->GetLandscape().GetHeight(8, 0)) - (Square->GetLandscape().GetHeight(4, 0))) <= 0.01);
		Doc->Undo();
		CHECK(Flat->GetLandscape().GetHeight(16, 16) == 0.0f);

		// The Z range scales heights: twice the range, twice as tall.
		Mode.HeightmapZRange = 1024.0f;
		Object* Taller = Mode.ImportLandscape(Png, Vector3d(0.0f, 0.0f, 0.0f), Error);
		REQUIRE(Taller);
		CHECK(std::fabs((Taller->GetLandscape().GetHeight(0, 0)) - (-60.0f)) <= 0.02);
	}

	TEST_CASE("Heightmap import reads 8-bit images, resamples big ones and reports bad files")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		FString Error;

		// 1025 x 1025 16-bit RAW: more than MaxQuads, so half the points at
		// twice the quad size, over the same extent.
		const int32 Side = 1025;
		TArray<uint8> Bytes(static_cast<SIZE_T>(Side) * Side * 2);
		for (int32 Y = 0; Y < Side; ++Y)
		{
			for (int32 X = 0; X < Side; ++X)
			{
				const uint16 Value = static_cast<uint16>(FMath::Min(X * 64, 65535));
				const SIZE_T I = (static_cast<SIZE_T>(Y) * Side + X) * 2;
				Bytes[I] = static_cast<uint8>(Value & 0xFF);
				Bytes[I + 1] = static_cast<uint8>(Value >> 8);
			}
		}
		const FString Big = Temp.File("big.raw");
		WriteBytes(Big, Bytes);
		Mode.NewQuadSize = 10.0f;
		Object* Land = Mode.ImportLandscape(Big, Vector3d(0.0f, 0.0f, 0.0f), Error);
		REQUIRE(Land);
		CHECK(Land->GetLandscape().GetQuadsX() == Landscape::MaxQuads);
		CHECK(Land->GetLandscape().GetQuadsY() == Landscape::MaxQuads);
		CHECK(Land->GetLandscape().GetSizeX() == doctest::Approx(10240.0f));
		CHECK(std::fabs((Land->GetLandscape().GetHeight(512, 0)) - ((65535.0f - 32768.0f) / 128.0f)) <= 0.01);

		// An 8-bit RAW (bytes making a square): white is the top of the range.
		const FString Byte8 = Temp.File("eight.raw");
		WriteBytes(Byte8, TArray<uint8>(9, 255));
		FHeightmap Map;
		REQUIRE(FHeightmap::Load(Byte8, Map, Error));
		CHECK(Map.Width == 3);
		CHECK(Map.At(1, 1) == 65535);

		// Bad files say why.
		WriteBytes(Temp.File("odd.raw"), TArray<uint8>(7, 0));
		CHECK(!Mode.ImportLandscape(Temp.File("odd.raw"), Vector3d(0.0f, 0.0f, 0.0f), Error));
		CHECK(Error.Contains("square"));
		WriteBytes(Temp.File("junk.png"), TArray<uint8>(64, 7));
		CHECK(!Mode.ImportLandscape(Temp.File("junk.png"), Vector3d(0.0f, 0.0f, 0.0f), Error));
		CHECK(Error.Contains("image"));
		CHECK(!Mode.ImportLandscape(Temp.File("missing.png"), Vector3d(0.0f, 0.0f, 0.0f), Error));
		CHECK(Error.Contains("exist"));
	}

	TEST_CASE("Weightmaps export a layer as 8-bit and import it back, the other layers giving way")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		Object* Land = Doc->AddLandscape(10, 10, 32.0f, Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		REQUIRE(Mode.AddLayer(*Land));
		REQUIRE(Mode.AddLayer(*Land));
		// Grass a gradient over X, Rock everywhere it fits.
		std::vector<LandscapeLayer> Layers = Land->GetLandscape().GetLayers();
		for (int32 Y = 0; Y <= 10; ++Y)
		{
			for (int32 X = 0; X <= 10; ++X)
			{
				const SIZE_T I = static_cast<SIZE_T>(Y) * 11 + X;
				Layers[0].weights[I] = static_cast<float>(X) / 10.0f * (Y == 10 ? 1.0f : 0.5f);
				Layers[1].weights[I] = 1.0f - Layers[0].weights[I];
			}
		}
		Doc->SetLandscape(*Land, Land->GetLandscape().WithLayers(Layers), "Paint");

		FString Error;
		const FString Png = Temp.File("grass.png");
		const FString Raw = Temp.File("grass.raw");
		REQUIRE(Mode.ExportWeightmap(*Land, 0, Png, Error));
		REQUIRE(Mode.ExportWeightmap(*Land, 0, Raw, Error));
		CHECK(std::filesystem::file_size(std::filesystem::path(Raw.ToWide())) == 121); // 8 bits a point
		FHeightmap Map;
		REQUIRE(FHeightmap::Load(Png, Map, Error));
		CHECK(Map.Width == 11);
		CHECK(Map.At(10, 0) == 65535);  // the top row is +Y: full grass at the right
		CHECK(Map.At(10, 10) == 128 * 257);
		CHECK(!Mode.ExportWeightmap(*Land, 5, Png, Error));

		// Onto Rock: all of it where the map is white, so Grass gives way.
		REQUIRE(Mode.ImportWeightmap(*Land, 1, Raw, Error));
		CHECK(Doc->GetUndoDescription() == "Import Weightmap");
		const Landscape& After = Land->GetLandscape();
		for (int32 Y = 0; Y <= 10; ++Y)
		{
			for (int32 X = 0; X <= 10; ++X)
			{
				CHECK(After.GetWeight(0, X, Y) + After.GetWeight(1, X, Y) <= 1.0001f);
				CHECK(std::fabs((After.GetWeight(1, X, Y)) - (Layers[0].weights[static_cast<SIZE_T>(Y) * 11 + X])) <= 1.0 / 255.0 + 1e-4);
			}
		}
		CHECK(After.GetWeight(0, 10, 10) == 0.0f); // Rock is all of it there
		CHECK(After.GetWeight(0, 0, 0) == 0.0f);   // Rock is none of it, and Grass was none
		Doc->Undo();
		CHECK(Land->GetLandscape().GetWeight(1, 0, 0) == 1.0f);
		CHECK(!Mode.ImportWeightmap(*Land, 2, Png, Error));
	}

	TEST_CASE("An imported heightmap's top is at the top of the Top view")
	{
		FTestLogCapture Log;
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Top))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		// +Y, where the image's top row goes, is up on the screen.
		CHECK(F.At(Vector3d(0.0f, 64.0f, 0.0f)).Y < F.At(Vector3d(0.0f, -64.0f, 0.0f)).Y);
		CHECK(F.At(Vector3d(64.0f, 0.0f, 0.0f)).X > F.At(Vector3d(-64.0f, 0.0f, 0.0f)).X);
		CHECK(Log.Problems.empty());
	}
}

// ---------------------------------------------------------------------------
// Landscape layer textures.

namespace
{
	// A 24-bit BMP of the given BGR texels (rows from the top), which WIC reads.
	void WriteBmp(const FString& Path, int32 Width, int32 Height, const TArray<FColor>& Texels)
	{
		const int32 RowBytes = (Width * 3 + 3) & ~3;
		const uint32 DataSize = static_cast<uint32>(RowBytes * Height);
		TArray<uint8> Bytes(54 + DataSize, 0);
		const auto Put32 = [&Bytes](SIZE_T At, uint32 V) { for (int32 I = 0; I < 4; ++I) Bytes[At + I] = static_cast<uint8>(V >> (8 * I)); };
		Bytes[0] = 'B';
		Bytes[1] = 'M';
		Put32(2, 54 + DataSize);
		Put32(10, 54);
		Put32(14, 40);
		Put32(18, static_cast<uint32>(Width));
		Put32(22, static_cast<uint32>(-Height)); // top-down
		Bytes[26] = 1;
		Bytes[28] = 24;
		Put32(34, DataSize);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FColor& C = Texels[static_cast<SIZE_T>(Y) * Width + X];
				uint8* Out = &Bytes[54 + static_cast<SIZE_T>(Y) * RowBytes + static_cast<SIZE_T>(X) * 3];
				Out[0] = C.B;
				Out[1] = C.G;
				Out[2] = C.R;
			}
		}
		FFile File;
		REQUIRE(File.Open(Path, EFileOpenMode::Write));
		REQUIRE(File.Write(Bytes.data(), static_cast<int64>(Bytes.size())) == static_cast<int64>(Bytes.size()));
	}

	// One layer over all of a landscape.
	std::shared_ptr<const Landscape> Covered(const Landscape& Land, const char* Name, FColor Color, const std::string& Texture, float Tiling = 512.0f)
	{
		LandscapeLayer Layer = Land.MakeLayer(Name, Color.R, Color.G, Color.B);
		Layer.texture = Texture;
		Layer.tiling = Tiling;
		std::fill(Layer.weights.begin(), Layer.weights.end(), 1.0f);
		return Land.WithLayers({ Layer });
	}

	// The image's mean color, and how much it varies.
	void Stats(const FLandscapeBake& Bake, float Mean[3], float& Spread)
	{
		double Sum[3] = { 0, 0, 0 };
		double Squares = 0;
		const SIZE_T Count = static_cast<SIZE_T>(Bake.GetWidth()) * Bake.GetHeight();
		for (SIZE_T I = 0; I < Count; ++I)
		{
			for (int32 C = 0; C < 3; ++C)
			{
				Sum[C] += Bake.GetPixels()[I * 4 + C];
			}
			Squares += static_cast<double>(Bake.GetPixels()[I * 4 + 1]) * Bake.GetPixels()[I * 4 + 1];
		}
		for (int32 C = 0; C < 3; ++C)
		{
			Mean[C] = static_cast<float>(Sum[C] / static_cast<double>(Count));
		}
		Spread = static_cast<float>(std::sqrt(FMath::Max(Squares / static_cast<double>(Count) - static_cast<double>(Mean[1]) * Mean[1], 0.0)));
	}
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("The built-in landscape textures tint their layer's color without changing it on average")
	{
		FLandscapeTextureLibrary& Library = FLandscapeTextureLibrary::Get();
		REQUIRE(FLandscapeTextureLibrary::GetBuiltInNames().size() == 5);
		for (const FString& Name : FLandscapeTextureLibrary::GetBuiltInNames())
		{
			CAPTURE(Name);
			const FLandscapeTexture* Texture = Library.Find(std::string(FLandscapeTextureLibrary::BuiltIn(Name).View()));
			REQUIRE(Texture);
			CHECK(Texture->bTint);
			CHECK(Texture->Mips.front().Width == 256);
			CHECK(Texture->Mips.back().Width == 1);
			// The smallest mip is the mean: about 128, the layer's own color.
			CHECK(std::abs(static_cast<int32>(Texture->Mips.back().Pixels[0]) - 128) <= 16);
			// It wraps: U and U + 1 are the same texel.
			float A[3], B[3];
			Texture->Sample(0.3f, 0.7f, 0.0f, A);
			Texture->Sample(1.3f, -0.3f, 0.0f, B);
			CHECK(std::fabs(A[0] - B[0]) < 0.01f);
		}
		CHECK(!Library.Find("builtin:Lava"));
		CHECK(!Library.Find(""));
		CHECK(FLandscapeTextureLibrary::DisplayName("builtin:Rock") == "Rock");
		CHECK(FLandscapeTextureLibrary::DisplayName("C:/Art/Moss Big.png") == "Moss Big.png");
		CHECK(FLandscapeTextureLibrary::DisplayName("") == "None");
	}

	TEST_CASE("A landscape's layers bake into one image: textures, plain colors, the ground's own color")
	{
		EnsureMapCore();
		FTempFolder Temp;
		const Landscape Field(16, 8, 32.0f);
		CHECK(!FLandscapeTextureLibrary::IsTextured(Field));
		CHECK(FLandscapeBake::GetTexelsPerQuad(Field) == 16);
		CHECK(FLandscapeBake::GetTexelsPerQuad(Landscape(512, 4, 32.0f)) == 4);

		// Grass all over: the grass color on average, with detail in it.
		FLandscapeBake Bake;
		const std::shared_ptr<const Landscape> Grass = Covered(Field, "Grass", FColor(92, 146, 62), "builtin:Grass", 128.0f);
		REQUIRE(FLandscapeTextureLibrary::IsTextured(*Grass));
		const FIntRect All = Bake.Update(Grass);
		CHECK(All == FIntRect(0, 0, 256, 128));
		CHECK(Bake.GetWidth() == 256);
		CHECK(Bake.GetHeight() == 128);
		float Mean[3], Spread;
		Stats(Bake, Mean, Spread);
		CHECK(std::fabs(Mean[0] - 92.0f) < 14.0f);
		CHECK(std::fabs(Mean[1] - 146.0f) < 18.0f);
		CHECK(Spread > 6.0f);
		CHECK(Bake.Update(Grass).IsEmpty()); // nothing changed

		// A plain-colored layer is flat; where none is painted, the ground's color.
		LandscapeLayer Plain = Field.MakeLayer("Red", 200, 20, 20);
		for (const SIZE_T Corner : { SIZE_T(0), SIZE_T(1), SIZE_T(17), SIZE_T(18) })
		{
			Plain.weights[Corner] = 1.0f; // all of quad 0
		}
		FLandscapeBake PlainBake;
		PlainBake.Update(Field.WithLayers({ Plain }));
		CHECK(PlainBake.GetTexel(0, 0) == FColor(200, 20, 20, 255));
		unsigned char R, G, B;
		Landscape::GetBaseColor(R, G, B);
		CHECK(PlainBake.GetTexel(200, 100) == FColor(R, G, B, 255));

		// An image file, as it is: a 2 x 1 red and blue, repeating every 64
		// units (two quads), so each quad is one color.
		const FString Bmp = Temp.File("stripes.bmp");
		WriteBmp(Bmp, 2, 1, { FColor(255, 0, 0), FColor(0, 0, 255) });
		const std::shared_ptr<const Landscape> Striped = Covered(Field, "Stripes", FColor(255, 255, 255), std::string(Bmp.View()), 64.0f);
		FLandscapeBake ImageBake;
		ImageBake.Update(Striped);
		const FColor Left = ImageBake.GetTexel(8, 8);    // the middle of quad 0
		const FColor Right = ImageBake.GetTexel(24, 8);  // of quad 1
		CHECK(Left.R > 200);
		CHECK(Left.B < 60);
		CHECK(Right.B > 200);
		CHECK(Right.R < 60);
		CHECK(ImageBake.GetTexel(40, 8) == Left); // and again

		// A missing file draws as the plain color.
		FLandscapeBake Missing;
		Missing.Update(Covered(Field, "Gone", FColor(10, 200, 30), std::string(Temp.File("gone.png").View())));
		CHECK(Missing.GetTexel(5, 5) == FColor(10, 200, 30, 255));
	}

	TEST_CASE("A stroke bakes only the part of the image under it again")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		Mode.NewQuadsX = 64;
		Mode.NewQuadsY = 64;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		REQUIRE(Mode.AddLayer(*Land)); // Grass, textured
		CHECK(Land->GetLandscape().GetLayers()[0].texture == "builtin:Grass");
		REQUIRE(Mode.AddLayer(*Land)); // Rock
		FLandscapeBake Bake;
		CHECK(Bake.Update(Land->GetLandscapePtr()).Width() == 1024);

		// Sculpting changes no weights: nothing to bake.
		Mode.Tool = ELandscapeTool::Sculpt;
		Mode.BeginStroke(*Land, Vector3d(0.0f, 0.0f, 0.0f), false);
		Mode.ApplyAt(*Land, Vector3d(0.0f, 0.0f, 0.0f), 0.5f, false);
		Mode.EndStroke();
		CHECK(Bake.Update(Land->GetLandscapePtr()).IsEmpty());

		// Rock in a corner: only around it.
		Mode.Tool = ELandscapeTool::Paint;
		Mode.SelectedLayer = 1;
		Mode.Brush.Radius = 96.0f;
		Mode.Brush.Strength = 1.0f;
		const Vector3d Corner(-800.0f, -800.0f, 0.0f);
		Mode.BeginStroke(*Land, Corner, false);
		Mode.ApplyAt(*Land, Corner, 0.5f, false);
		Mode.EndStroke();
		const FIntRect Dirty = Bake.Update(Land->GetLandscapePtr());
		CHECK(!Dirty.IsEmpty());
		CHECK(Dirty.Right <= 512);
		CHECK(Dirty.Bottom <= 512);
		CHECK(Dirty.Width() * Dirty.Height() < 1024 * 1024 / 16);
		// The same as baking it all.
		FLandscapeBake Fresh;
		Fresh.Update(Land->GetLandscapePtr());
		CHECK(Fresh.GetPixels() == Bake.GetPixels());

		// A layer's look changing bakes all of it, and is undoable.
		CHECK(Mode.SetLayerTiling(*Land, 1, 128.0f));
		CHECK(Doc->GetUndoDescription() == "Change Paint Layer Tiling");
		CHECK(Bake.Update(Land->GetLandscapePtr()).Width() == 1024);
		CHECK(Mode.SetLayerTexture(*Land, 1, ""));
		CHECK(Doc->GetUndoDescription() == "Change Paint Layer Texture");
		CHECK(!Mode.SetLayerTexture(*Land, 1, ""));
		CHECK(Land->GetLandscape().GetLayers()[1].texture.empty());
		Doc->Undo();
		CHECK(Land->GetLandscape().GetLayers()[1].texture == "builtin:Rock");
	}

	TEST_CASE("Layer textures and tiling are saved in .qle; older files load untextured")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Land = Doc->AddLandscape(4, 4, 32.0f, Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		LandscapeLayer Moss = Land->GetLandscape().MakeLayer("Moss", 60, 120, 40);
		Moss.texture = "C:/Art Work/moss tile.png";
		Moss.tiling = 300.0f;
		LandscapeLayer Plain = Land->GetLandscape().MakeLayer("Plain", 1, 2, 3);
		Doc->SetLandscape(*Land, Land->GetLandscape().WithLayers({ Moss, Plain }), "Paint");
		FString Error;
		const FString Qle = Temp.File("textures.qle");
		REQUIRE(Doc->Save(Qle, Error));
		FString Text;
		REQUIRE(FFile::ReadAllText(Qle, Text));
		CHECK(Text.Contains("LandscapeTexture: 300.0 C:/Art Work/moss tile.png"));

		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		const Object* Loaded = nullptr;
		for (Object* Obj : Reopened->GetObjects())
		{
			Loaded = Obj->HasLandscape() ? Obj : Loaded;
		}
		REQUIRE(Loaded);
		const std::vector<LandscapeLayer>& Layers = Loaded->GetLandscape().GetLayers();
		REQUIRE(Layers.size() == 2);
		CHECK(Layers[0].texture == "C:/Art Work/moss tile.png");
		CHECK(Layers[0].tiling == 300.0f);
		CHECK(Layers[1].texture.empty());
		CHECK(Layers[1].tiling == 512.0f);
	}

	TEST_CASE("Textured landscapes draw with their baked image (D3D12 and Vulkan)")
	{
		for (const ERHIBackend Backend : { ERHIBackend::D3D12, ERHIBackend::Vulkan })
		{
			CAPTURE(ToString(Backend));
			FTestLogCapture Log;
			FViewFixture F;
			if (!F.Create(Backend, EMapViewType::Perspective))
			{
				MESSAGE(ToString(Backend), " device unavailable; skipped");
				continue;
			}
			FMapLandscapeMode Mode;
			Mode.SetDocument(F.Doc.get());
			Mode.NewQuadsX = 32;
			Mode.NewQuadsY = 32;
			Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
			REQUIRE(Land);
			F.Doc->ClearSelection();
			F.Client->ResetCamera();
			F.Client->SetViewMode(EMapViewMode::Lit);
			const FVector2 Center = F.At(Vector3d(0.0f, 0.0f, 0.0f));
			const int32 X = static_cast<int32>(Center.X);
			const int32 Y = static_cast<int32>(Center.Y);
			const FColor Flat = PixelAt(F.Render(), X, Y);
			CHECK(F.Resources->GetNumLandscapeTextures() == 0);

			// Red over the middle, as a plain color (layer textures replace
			// the per-corner colors, so this is the bake drawing).
			REQUIRE(Mode.AddLayer(*Land, "Red", FColor(220, 30, 30), "builtin:Dirt"));
			Mode.Tool = ELandscapeTool::Paint;
			Mode.Brush.Radius = 300.0f;
			Mode.Brush.Strength = 1.0f;
			Mode.BeginStroke(*Land, Vector3d(0.0f, 0.0f, 0.0f), false);
			Mode.ApplyAt(*Land, Vector3d(0.0f, 0.0f, 0.0f), 1.0f, false);
			Mode.EndStroke();
			const TArray<uint8> Pixels = F.Render();
			CHECK(F.Resources->GetNumLandscapeTextures() == 1);
			REQUIRE(F.Resources->FindLandscapeBake(*Land));
			const FColor Painted = PixelAt(Pixels, X, Y);
			CHECK(Painted != Flat);
			CHECK(Painted.R > Painted.G + 40);
			// The texture's detail: neighbouring pixels differ.
			int32 Differ = 0;
			for (int32 DX = -8; DX <= 8; ++DX)
			{
				Differ += PixelAt(Pixels, X + DX, Y) != Painted ? 1 : 0;
			}
			CHECK(Differ >= 4);

			// Painting again brings the image up to date: the middle goes back
			// to the ground's color.
			Mode.BeginStroke(*Land, Vector3d(0.0f, 0.0f, 0.0f), true);
			Mode.ApplyAt(*Land, Vector3d(0.0f, 0.0f, 0.0f), 1.0f, true);
			Mode.EndStroke();
			const FColor Erased = PixelAt(F.Render(), X, Y);
			CHECK(std::abs(static_cast<int32>(Erased.R) - static_cast<int32>(Flat.R)) <= 6);
			CHECK(std::abs(static_cast<int32>(Erased.G) - static_cast<int32>(Flat.G)) <= 6);
			CHECK(Log.Problems.empty());
			for (const FString& Problem : Log.Problems)
			{
				MESSAGE(Problem.c_str());
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Landscape tools: brush keys, Flatten and Paint settings, Ramp, Erosion,
// Auto Paint.

namespace
{
	struct FLandFixture
	{
		TUniquePtr<FMapDocument> Doc;
		FMapLandscapeMode Mode;
		Object* Land = nullptr;

		explicit FLandFixture(int32 Quads = 32)
		{
			EnsureMapCore();
			Doc = FMapDocument::CreateNew();
			Mode.SetDocument(Doc.get());
			Mode.NewQuadsX = Quads;
			Mode.NewQuadsY = Quads;
			Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
			REQUIRE(Land);
			Doc->ClearSelection();
		}

		void SetHeights(const TFunction<float(int32, int32)>& At)
		{
			const Landscape& Field = Land->GetLandscape();
			std::vector<float> Heights;
			for (int32 Y = 0; Y < Field.GetPointsY(); ++Y)
			{
				for (int32 X = 0; X < Field.GetPointsX(); ++X)
				{
					Heights.push_back(At(X, Y));
				}
			}
			Doc->SetLandscape(*Land, Field.WithHeights(Heights), "Set");
		}

		float H(int32 X, int32 Y) const { return Land->GetLandscape().GetHeight(X, Y); }
		// The grid point's place in scope space.
		Vector3d At(int32 X, int32 Y) const { return Land->GetLandscape().GetPoint(X, Y); }

		void Stroke(const Vector3d& Point, float Seconds, bool bInvert = false)
		{
			Mode.BeginStroke(*Land, Point, bInvert);
			for (float T = 0.0f; T < Seconds; T += 1.0f / 30.0f)
			{
				Mode.ApplyAt(*Land, Point, 1.0f / 30.0f, bInvert);
			}
			Mode.EndStroke();
		}
	};
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("[ and ] size the landscape brush and Ctrl with them its strength, within limits")
	{
		FLandscapeBrush Brush;
		Brush.Radius = 100.0f;
		Brush.Grow(1);
		CHECK(Brush.Radius == doctest::Approx(125.0f));
		Brush.Grow(-2);
		CHECK(Brush.Radius == doctest::Approx(80.0f));
		Brush.Grow(-100);
		CHECK(Brush.Radius == FLandscapeBrush::MinRadius);
		Brush.Grow(100);
		CHECK(Brush.Radius == FLandscapeBrush::MaxRadius);
		Brush.Strength = 0.4f;
		Brush.Strengthen(1);
		CHECK(Brush.Strength == doctest::Approx(0.45f));
		Brush.Strengthen(-100);
		CHECK(Brush.Strength == doctest::Approx(0.01f));
		Brush.Strengthen(100);
		CHECK(Brush.Strength == 1.0f);
	}

	TEST_CASE("Flatten pulls to a target height, and can only raise or only lower")
	{
		FLandFixture F;
		// A slope from -100 to 100 along X.
		F.SetHeights([](int32 X, int32) { return static_cast<float>(X - 16) * 6.25f; });
		F.Mode.Tool = ELandscapeTool::Flatten;
		F.Mode.Brush.Radius = 4096.0f;
		F.Mode.Brush.Strength = 1.0f;
		F.Mode.Brush.Falloff = 0.0f;

		F.Mode.FlattenMode = ELandscapeFlattenMode::Raise;
		F.Mode.bFlattenToTarget = true;
		F.Mode.FlattenTarget = 20.0f;
		F.Stroke(F.At(16, 16), 1.0f);
		CHECK(F.H(0, 5) == doctest::Approx(20.0f).epsilon(0.01));  // was -100: raised
		CHECK(F.H(32, 5) == doctest::Approx(100.0f));               // above: left alone
		CHECK(F.Doc->GetUndoDescription() == "Landscape Flatten");
		F.Doc->Undo();
		CHECK(F.H(0, 5) == doctest::Approx(-100.0f));

		F.Mode.FlattenMode = ELandscapeFlattenMode::Lower;
		F.Stroke(F.At(16, 16), 1.0f);
		CHECK(F.H(0, 5) == doctest::Approx(-100.0f));
		CHECK(F.H(32, 5) == doctest::Approx(20.0f).epsilon(0.01));
		F.Doc->Undo();

		// Without a target, to where the stroke began (the middle, 0).
		F.Mode.FlattenMode = ELandscapeFlattenMode::Both;
		F.Mode.bFlattenToTarget = false;
		F.Stroke(F.At(16, 16), 1.0f);
		CHECK(std::fabs(F.H(0, 5)) < 1.0f);
		CHECK(std::fabs(F.H(32, 5)) < 1.0f);
	}

	TEST_CASE("Paint moves a layer toward the target value, never past it")
	{
		FLandFixture F;
		REQUIRE(F.Mode.AddLayer(*F.Land));
		F.Mode.Tool = ELandscapeTool::Paint;
		F.Mode.SelectedLayer = 0;
		F.Mode.Brush.Radius = 4096.0f;
		F.Mode.Brush.Strength = 1.0f;
		F.Mode.Brush.Falloff = 0.0f;
		F.Mode.PaintTarget = 0.5f;
		F.Stroke(F.At(16, 16), 2.0f);
		CHECK(F.Land->GetLandscape().GetWeight(0, 16, 16) == doctest::Approx(0.5f));
		// From above, down to it.
		F.Mode.PaintTarget = 1.0f;
		F.Stroke(F.At(16, 16), 2.0f);
		CHECK(F.Land->GetLandscape().GetWeight(0, 16, 16) == 1.0f);
		F.Mode.PaintTarget = 0.25f;
		F.Stroke(F.At(16, 16), 2.0f);
		CHECK(F.Land->GetLandscape().GetWeight(0, 16, 16) == doctest::Approx(0.25f));
		// Shift still erases all of it.
		F.Stroke(F.At(16, 16), 2.0f, true);
		CHECK(F.Land->GetLandscape().GetWeight(0, 16, 16) == 0.0f);
	}

	TEST_CASE("Ramp lays a slope between two points, the brush's size wide")
	{
		FLandFixture F;
		// A step: 0 below Y = 16, 128 from it on.
		F.SetHeights([](int32, int32 Y) { return Y >= 24 ? 128.0f : 0.0f; });
		F.Mode.Tool = ELandscapeTool::Ramp;
		F.Mode.Brush.Radius = 96.0f;
		F.Mode.Brush.Falloff = 0.0f;
		CHECK(!F.Mode.ApplyAt(*F.Land, F.At(16, 16), 0.5f, false)); // a ramp is no stroke
		REQUIRE(F.Mode.AddRamp(*F.Land, F.At(16, 8), F.At(16, 28)));
		CHECK(F.Doc->GetUndoDescription() == "Landscape Ramp");
		// Up the middle, evenly from 0 to 128.
		CHECK(F.H(16, 8) == doctest::Approx(0.0f));
		CHECK(F.H(16, 18) == doctest::Approx(64.0f).epsilon(0.01));
		CHECK(F.H(16, 28) == doctest::Approx(128.0f));
		CHECK(F.H(18, 13) == doctest::Approx(32.0f).epsilon(0.01)); // within its width
		// Outside its width and past its ends, as it was.
		CHECK(F.H(24, 18) == 0.0f);
		CHECK(F.H(16, 4) == 0.0f);
		CHECK(F.H(16, 31) == 128.0f);
		F.Doc->Undo();
		CHECK(F.H(16, 18) == 0.0f);
		CHECK(!F.Mode.AddRamp(*F.Land, F.At(16, 8), F.At(16, 8)));
	}

	TEST_CASE("Erosion wears a cliff down toward the talus angle, keeping the ground's volume")
	{
		FLandFixture F;
		// A cliff 300 high across X = 16.
		F.SetHeights([](int32 X, int32) { return X >= 16 ? 300.0f : 0.0f; });
		float Before = 0.0f;
		for (const float H : F.Land->GetLandscape().GetHeights())
		{
			Before += H;
		}
		F.Mode.Tool = ELandscapeTool::Erosion;
		F.Mode.ErosionTalus = 30.0f;
		F.Mode.Brush.Radius = 4096.0f;
		F.Mode.Brush.Strength = 1.0f;
		F.Stroke(F.At(16, 16), 3.0f);
		CHECK(F.Doc->GetUndoDescription() == "Landscape Erosion");
		// Lower at the top of the cliff, higher at its foot.
		CHECK(F.H(16, 16) < 300.0f);
		CHECK(F.H(15, 16) > 0.0f);
		// No drop is much steeper than before.
		float Steepest = 0.0f;
		for (int32 X = 0; X < 32; ++X)
		{
			Steepest = FMath::Max(Steepest, std::fabs(F.H(X + 1, 16) - F.H(X, 16)));
		}
		CHECK(Steepest < 250.0f);
		float After = 0.0f;
		for (const float H : F.Land->GetLandscape().GetHeights())
		{
			After += H;
		}
		CHECK(After == doctest::Approx(Before).epsilon(0.001));
		// Flat ground doesn't move.
		FLandFixture Flat;
		Flat.Mode.Tool = ELandscapeTool::Erosion;
		CHECK(!Flat.Mode.ApplyAt(*Flat.Land, Flat.At(16, 16), 0.5f, false));
	}

	TEST_CASE("Auto Paint lays a layer by slope and height, the others giving way")
	{
		FLandFixture F;
		// Flat for X < 16, then a 45 degree slope up.
		F.SetHeights([](int32 X, int32) { return X > 16 ? static_cast<float>(X - 16) * 32.0f : 0.0f; });
		REQUIRE(F.Mode.AddLayer(*F.Land)); // Grass
		REQUIRE(F.Mode.AddLayer(*F.Land)); // Rock
		// Grass everywhere first.
		REQUIRE(F.Mode.ApplyAutoPaint(*F.Land, 0));
		CHECK(F.Land->GetLandscape().GetWeight(0, 5, 5) == 1.0f);
		CHECK(F.Land->GetLandscape().GetWeight(0, 28, 5) == 1.0f);

		// Rock on slopes over 30 degrees.
		F.Mode.AutoPaint.MinSlope = 30.0f;
		REQUIRE(F.Mode.ApplyAutoPaint(*F.Land, 1));
		CHECK(F.Doc->GetUndoDescription() == "Auto Paint Rock");
		CHECK(F.Land->GetLandscape().GetWeight(1, 28, 5) == 1.0f);
		CHECK(F.Land->GetLandscape().GetWeight(0, 28, 5) == 0.0f); // the grass gave way
		CHECK(F.Land->GetLandscape().GetWeight(1, 5, 5) == 0.0f);
		CHECK(F.Land->GetLandscape().GetWeight(0, 5, 5) == 1.0f);

		// And by height: rock only above 200.
		F.Doc->Undo();
		F.Mode.AutoPaint.MinSlope = 0.0f;
		F.Mode.AutoPaint.MinHeight = 200.0f;
		F.Mode.AutoPaint.HeightFade = 0.0f;
		REQUIRE(F.Mode.ApplyAutoPaint(*F.Land, 1));
		CHECK(F.Land->GetLandscape().GetWeight(1, 30, 5) == 1.0f); // 448 high
		CHECK(F.Land->GetLandscape().GetWeight(1, 20, 5) == 0.0f); // 128 high
		CHECK(!F.Mode.ApplyAutoPaint(*F.Land, 7));
	}
}

// ---------------------------------------------------------------------------
// Landscape level of detail: components, culling, the grid trace.

namespace
{
	// Rolling hills, so coarser levels really differ from the full grid.
	std::shared_ptr<const Landscape> Hills(int32 QuadsX, int32 QuadsY, float Amplitude = 40.0f)
	{
		std::vector<float> Heights;
		for (int32 Y = 0; Y <= QuadsY; ++Y)
		{
			for (int32 X = 0; X <= QuadsX; ++X)
			{
				Heights.push_back(Amplitude * std::sin(X * 0.7f) * std::cos(Y * 0.5f) + Amplitude * 0.5f * std::sin(X * 2.3f + Y * 1.7f));
			}
		}
		return Landscape::Create(QuadsX, QuadsY, 32.0f, Heights);
	}

	struct FLodView
	{
		FViewFixture F;
		Object* Land = nullptr;

		bool Create(ERHIBackend Backend, int32 Quads)
		{
			if (!F.Create(Backend, EMapViewType::Perspective))
			{
				return false;
			}
			Land = F.Doc->AddLandscape(Hills(Quads, Quads), Vector3d(0.0f, 0.0f, 0.0f));
			REQUIRE(Land);
			F.Doc->SetSelection({ Land });
			F.Client->FrameSelection();
			F.Doc->ClearSelection();
			F.Client->SetViewMode(EMapViewMode::Lit);
			return true;
		}

		// The triangles and components one frame draws.
		TArray<uint8> Draw(int32& Triangles, int32& Components)
		{
			View::landscapeTrianglesDrawn = 0;
			View::landscapeComponentsDrawn = 0;
			TArray<uint8> Pixels = F.Render();
			Triangles = View::landscapeTrianglesDrawn;
			Components = View::landscapeComponentsDrawn;
			return Pixels;
		}
	};
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("A landscape's grid trace hits where its full mesh does")
	{
		EnsureMapCore();
		const std::shared_ptr<const Landscape> Land = Hills(40, 24);
		REQUIRE(Land);
		CHECK(Land->GetComponentsX() == 2);
		CHECK(Land->GetComponentsY() == 1);
		float Low, High;
		Land->GetComponentHeights(1, 0, Low, High);
		CHECK(Low >= Land->GetMinHeight());
		CHECK(High <= Land->GetMaxHeight());
		CHECK(Land->GetMaxHeight() > 30.0f);

		const StaticMesh& Mesh = *Land->GetMesh();
		uint32 Seed = 7;
		const auto Random = [&Seed](float Lo, float Hi)
		{
			Seed = Seed * 1664525u + 1013904223u;
			return Lo + (Hi - Lo) * static_cast<float>(Seed >> 8) / 16777216.0f;
		};
		int32 Hits = 0;
		for (int32 Ray = 0; Ray < 400; ++Ray)
		{
			// From above, from below and from the side, at all angles.
			const Vector3d Origin(Random(-800.0f, 800.0f), Random(-500.0f, 500.0f), Ray % 3 == 0 ? -300.0f : Random(100.0f, 600.0f));
			const Vector3d Toward(Random(-640.0f, 640.0f), Random(-384.0f, 384.0f), Random(-60.0f, 60.0f));
			Vector3d Direction(Toward);
			Direction.SubVector(Origin);
			const Line Probe(Origin, Direction);
			float MeshDepth = 0.0f, GridDepth = 0.0f;
			const bool bMesh = Mesh.HitMesh(Probe, 0.0f, 1.0e7f, MeshDepth);
			const bool bGrid = Land->Raycast(Probe, 0.0f, 1.0e7f, GridDepth);
			CAPTURE(Ray);
			REQUIRE(bMesh == bGrid);
			if (bMesh)
			{
				++Hits;
				CHECK(GridDepth == doctest::Approx(MeshDepth).epsilon(1e-3));
			}
		}
		CHECK(Hits > 250);
		// Range limits count.
		const Line Down(Vector3d(0.0f, 0.0f, 500.0f), Vector3d(0.0f, 0.0f, -1.0f));
		float Depth = 0.0f;
		CHECK(Land->Raycast(Down, 0.0f, 1.0e7f, Depth));
		CHECK(!Land->Raycast(Down, 0.0f, 300.0f, Depth));
	}

	TEST_CASE("A big landscape is drawn coarser far away, never with cracks between levels")
	{
		FTestLogCapture Log;
		FLodView V;
		if (!V.Create(ERHIBackend::D3D12, 256))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		int32 Triangles = 0, Components = 0;
		const float Detail = View::landscapeDetailPixels;
		View::landscapeDetailPixels = 0.0f; // every quad, as before levels
		V.Draw(Triangles, Components);
		const int32 Full = Triangles;
		View::landscapeDetailPixels = Detail;
		const TArray<uint8> Pixels = V.Draw(Triangles, Components);
		CHECK(Components == 64);           // framed: all of it in view
		CHECK(Full > 60000);
		CHECK(Triangles < Full / 8);       // far ground at a fraction
		CHECK(Triangles > 500);
		MESSAGE("full ", Full, " triangles, with levels ", Triangles);

		// Inside the landscape's outline every pixel is ground: no crack
		// shows the background through.
		const FColor Background = PixelAt(V.F.Render(), 0, 0);
		const Vector3d Corners[4] = { { -3600.0f, -3600.0f, 0.0f }, { 3600.0f, -3600.0f, 0.0f }, { 3600.0f, 3600.0f, 0.0f }, { -3600.0f, 3600.0f, 0.0f } };
		FVector2 Screen[4];
		for (int32 I = 0; I < 4; ++I)
		{
			Screen[I] = V.F.At(Corners[I]);
		}
		int32 Checked = 0, Holes = 0;
		for (float U = 0.0f; U <= 1.0f; U += 0.02f)
		{
			for (float W = 0.0f; W <= 1.0f; W += 0.02f)
			{
				const FVector2 Bottom = Screen[0] + (Screen[1] - Screen[0]) * U;
				const FVector2 Top = Screen[3] + (Screen[2] - Screen[3]) * U;
				const FVector2 P = Bottom + (Top - Bottom) * W;
				if (P.X < 0.0f || P.Y < 0.0f || P.X >= 256.0f || P.Y >= 256.0f)
				{
					continue;
				}
				++Checked;
				Holes += PixelAt(Pixels, static_cast<int32>(P.X), static_cast<int32>(P.Y)) == Background ? 1 : 0;
			}
		}
		CHECK(Checked > 1000);
		CHECK(Holes == 0);
		CHECK(Log.Problems.empty());
	}

	TEST_CASE("Landscape components out of the view are not drawn")
	{
		FLodView V;
		if (!V.Create(ERHIBackend::D3D12, 256))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		// Close to a cube at one corner, looking out from over the landscape:
		// most of it is behind or beside the camera.
		Object* Cube = V.F.Doc->AddMesh("Cube", Vector3d(3900.0f, 3900.0f, 64.0f));
		REQUIRE(Cube);
		V.F.Doc->SetSelection({ Cube });
		V.F.Client->FrameSelection();
		V.F.Doc->ClearSelection();
		int32 Triangles = 0, Components = 0;
		V.Draw(Triangles, Components);
		CHECK(Components > 0);
		CHECK(Components < 32);
		// Up close, the nearest ground is at full detail.
		CHECK(Triangles > 200);
	}

	TEST_CASE("Editing a big landscape stays quick: no mesh is built for it")
	{
		EnsureMapCore();
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		Mode.NewQuadsX = 512;
		Mode.NewQuadsY = 512;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		REQUIRE(Mode.AddLayer(*Land));
		Mode.Tool = ELandscapeTool::Sculpt;
		Mode.Brush.Radius = 512.0f;
		const double Start = FPlatformTime::Seconds();
		Mode.BeginStroke(*Land, Vector3d(0.0f, 0.0f, 0.0f), false);
		for (int32 Step = 0; Step < 30; ++Step)
		{
			Mode.ApplyAt(*Land, Vector3d(0.0f, 0.0f, 0.0f), 1.0f / 30.0f, false);
			Vector3d Hit;
			REQUIRE(Mode.TraceLandscape(Vector3d(10.0f, 10.0f, 4000.0f), Vector3d(0.0f, 0.0f, -1.0f), Hit) == Land);
			CHECK(Hit.GetZ() > 0.0f);
		}
		Mode.EndStroke();
		const double Seconds = FPlatformTime::Seconds() - Start;
		MESSAGE("30 sculpt steps and traces on 512 x 512 quads: ", Seconds * 1000.0, " ms");
		CHECK(Seconds < 1.5); // was about 3 s with a mesh built each step
		// Bounds and the full mesh still agree.
		Vector3d Low, High;
		Land->GetBoundingVectors(Low, High);
		CHECK(High.GetZ() == doctest::Approx(Land->GetLandscape().GetMaxHeight()));
		CHECK(Land->GetMesh().GetNumVertices() == 513 * 513);
	}
}

// ---------------------------------------------------------------------------
// Landscape layers blended per pixel on the GPU.

namespace
{
	// How much the pixels around a point differ (the spread of their green).
	float Contrast(const TArray<uint8>& Pixels, int32 CX, int32 CY, int32 Half)
	{
		double Sum = 0.0, Squares = 0.0;
		int32 Count = 0;
		for (int32 Y = CY - Half; Y <= CY + Half; ++Y)
		{
			for (int32 X = CX - Half; X <= CX + Half; ++X)
			{
				const double G = PixelAt(Pixels, X, Y).G;
				Sum += G;
				Squares += G * G;
				++Count;
			}
		}
		const double Mean = Sum / Count;
		return static_cast<float>(std::sqrt(FMath::Max(Squares / Count - Mean * Mean, 0.0)));
	}
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("A landscape's layers are packed for the GPU: weights in two slices, textures a slice each")
	{
		EnsureMapCore();
		const Landscape Field(8, 4, 32.0f);
		std::vector<LandscapeLayer> Layers;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			LandscapeLayer Layer = Field.MakeLayer("L", static_cast<uint8>(40 * Index), 100, 200);
			Layer.texture = Index == 2 ? "builtin:Rock" : "";
			Layer.weights[static_cast<SIZE_T>(Index)] = 1.0f;
			Layers.push_back(Layer);
		}
		const std::shared_ptr<const Landscape> Land = Field.WithLayers(Layers);
		REQUIRE(FLandscapeLayerImages::CanBlend(*Land));
		FLandscapeLayerImages Images;
		CHECK(Images.UpdateWeights(Land) == FIntRect(0, 0, 9, 5));
		REQUIRE(Images.GetWeights().size() == 9 * 5 * 4 * 2);
		// Layer 1 is slice 0's green at point 1; layer 5 slice 1's green at point 5.
		CHECK(Images.GetWeights()[1 * 4 + 1] == 255);
		CHECK(Images.GetWeights()[9 * 5 * 4 + 5 * 4 + 1] == 255);
		CHECK(Images.GetWeights()[0 * 4 + 1] == 0);
		CHECK(Images.UpdateWeights(Land).IsEmpty());

		// A change at one point: just it.
		std::vector<LandscapeLayer> Changed = Land->GetLayers();
		Changed[3].weights[9 * 2 + 4] = 0.5f;
		const std::shared_ptr<const Landscape> Painted = Land->WithLayers(Changed);
		CHECK(Images.UpdateWeights(Painted) == FIntRect(4, 2, 5, 3));
		CHECK(Images.GetWeights()[(9 * 2 + 4) * 4 + 3] == 128);

		// Textures: one size (the built-in rock's 256), all mips, a slice a layer.
		REQUIRE(Images.UpdateTextures(*Land));
		CHECK(!Images.UpdateTextures(*Painted)); // the look is the same
		CHECK(Images.GetTextureSize() == 256);
		CHECK(Images.GetMipLevels() == 9);
		CHECK(Images.GetNumSlices() == 6);
		const SIZE_T SliceBytes = GetMipChainBytes(EPixelFormat::R8G8B8A8_UNORM, 256, 256, 9);
		REQUIRE(Images.GetTextures().size() == SliceBytes * 6);
		// A plain layer is its color all over; the rock is tinted, about its color.
		const uint8* Plain = Images.GetTextures().data() + SliceBytes * 4;
		CHECK(Plain[0] == 160);
		CHECK(Plain[1] == 100);
		CHECK(Plain[2] == 200);
		const uint8* RockSmallest = Images.GetTextures().data() + SliceBytes * 3 - 4;
		CHECK(std::abs(static_cast<int32>(RockSmallest[0]) - 80) < 16);
		CHECK(std::abs(static_cast<int32>(RockSmallest[2]) - 200) < 40);

		float Constants[12];
		FLandscapeLayerImages::GetConstants(*Land, Constants);
		CHECK(Constants[0] == 256.0f);
		CHECK(Constants[2] == 6.0f);
		CHECK(Constants[4] == doctest::Approx(1.0f / 512.0f));
		// More layers than the GPU blends: drawn baked.
		Layers.resize(9, Layers[2]);
		CHECK(!FLandscapeLayerImages::CanBlend(*Field.WithLayers(Layers)));
	}

	TEST_CASE("Landscape layers blended on the GPU are sharper up close than baked ones (D3D12 and Vulkan)")
	{
		for (const ERHIBackend Backend : { ERHIBackend::D3D12, ERHIBackend::Vulkan })
		{
			CAPTURE(ToString(Backend));
			FTestLogCapture Log;
			FTempFolder Temp;
			FViewFixture F;
			if (!F.Create(Backend, EMapViewType::Perspective))
			{
				MESSAGE(ToString(Backend), " device unavailable; skipped");
				continue;
			}
			// Big quads, and a checker of 8 x 8 squares repeating every 64 units:
			// squares of 8 units, half a baked texel (256 / 16 units).
			TArray<FColor> Checker;
			for (int32 Y = 0; Y < 64; ++Y)
			{
				for (int32 X = 0; X < 64; ++X)
				{
					Checker.push_back(((X / 8 + Y / 8) & 1) ? FColor(255, 255, 255) : FColor(0, 0, 0));
				}
			}
			const FString Bmp = Temp.File("checker.bmp");
			WriteBmp(Bmp, 64, 64, Checker);
			Object* Land = F.Doc->AddLandscape(Covered(Landscape(8, 8, 256.0f), "Checker", FColor(255, 255, 255), std::string(Bmp.View()), 64.0f),
				Vector3d(0.0f, 0.0f, 0.0f));
			REQUIRE(Land);
			// Close over its middle: framed on a cube there, which then goes.
			Object* Marker = F.Doc->AddMesh("Cube", Vector3d(0.0f, 0.0f, 64.0f));
			REQUIRE(Marker);
			F.Doc->SetSelection({ Marker });
			F.Client->FrameSelection();
			F.Doc->Undo();
			F.Client->SetViewMode(EMapViewMode::Unlit);

			F.Resources->bBlendLandscapeLayers = true;
			const TArray<uint8> Sharp = F.Render();
			F.Resources->bBlendLandscapeLayers = false;
			const TArray<uint8> Baked = F.Render();
			const float SharpContrast = Contrast(Sharp, 128, 160, 24);
			const float BakedContrast = Contrast(Baked, 128, 160, 24);
			MESSAGE(std::string(ToString(Backend)), ": contrast blended ", SharpContrast, ", baked ", BakedContrast);
			CHECK(SharpContrast > 50.0f);
			CHECK(SharpContrast > BakedContrast * 2.0f);
			CHECK(Log.Problems.empty());
			for (const FString& Problem : Log.Problems)
			{
				MESSAGE(Problem.c_str());
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Foliage.


namespace
{
	struct FFoliageFixture
	{
		TUniquePtr<FMapDocument> Doc;
		FMapFoliageMode Mode;
		Object* Land = nullptr;

		// A flat landscape 2048 units across, or one whose ground rises
		// along X at Slope degrees.
		explicit FFoliageFixture(float SlopeDegrees = 0.0f)
		{
			EnsureMapCore();
			Doc = FMapDocument::CreateNew();
			Mode.SetDocument(Doc.get());
			std::vector<float> Heights;
			const float Rise = std::tan(SlopeDegrees / 57.29578f) * 32.0f;
			for (int32 Y = 0; Y <= 64; ++Y)
			{
				for (int32 X = 0; X <= 64; ++X)
				{
					Heights.push_back(static_cast<float>(X - 32) * Rise);
				}
			}
			Land = Doc->AddLandscape(Landscape::Create(64, 64, 32.0f, Heights), Vector3d(0.0f, 0.0f, 0.0f));
			REQUIRE(Land);
			Mode.Brush.Radius = 256.0f;
			Mode.Brush.Strength = 1.0f;
			Mode.Brush.Falloff = 0.0f;
		}

		void Stroke(const Vector3d& At, float Seconds, bool bErase = false)
		{
			REQUIRE(Mode.BeginStroke(At, bErase));
			for (float T = 0.0f; T < Seconds; T += 1.0f / 30.0f)
			{
				Mode.ApplyAt(At, 1.0f / 30.0f, bErase);
			}
			Mode.EndStroke();
		}

		const Foliage& Leaves() { return Mode.GetFoliageObject()->GetFoliage(); }
	};
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("Foliage meshes are colored and stand on their pivot")
	{
		EnsureMapCore();
		REQUIRE(StaticMesh::GetFoliageMeshNames().size() == 5);
		for (const std::string& Name : StaticMesh::GetFoliageMeshNames())
		{
			CAPTURE(Name);
			const std::shared_ptr<const StaticMesh> Mesh = StaticMesh::Find(Name.c_str());
			REQUIRE(Mesh);
			CHECK(Mesh->HasVertexColors());
			CHECK(Mesh->GetTriangles().size() > 10);
			CHECK(Mesh->GetTriangles().size() < 600); // cheap: there may be thousands
			Vector3d Low, High;
			Mesh->GetBounds(Low, High);
			CHECK(High.GetZ() > 20.0f);
			CHECK(Low.GetZ() > -40.0f); // at most a little below the ground
		}
		// The basic shapes are still just those.
		CHECK(StaticMesh::GetBasicShapeNames().size() == 5);
		// A tree is green on top and brown at its foot.
		const std::shared_ptr<const StaticMesh> Tree = StaticMesh::Find("Tree");
		const unsigned int Foot = Tree->GetVertexColor(0);
		CHECK((Foot & 0xFF) > ((Foot >> 8) & 0xFF)); // more red than green: bark
	}

	TEST_CASE("Foliage instances are placed by their up, yaw and scale, and saved in .qle")
	{
		EnsureMapCore();
		FTempFolder Temp;
		FoliageType Bush = FMapFoliageMode::MakePreset("Bush");
		FoliageType Tree = FMapFoliageMode::MakePreset("Tree");
		Tree.name = "Big Oak";
		FoliageInstance A;
		A.type = 0;
		A.position.NewVector(100.0f, 200.0f, 30.0f);
		A.yaw = 1.5707963f; // a quarter turn: x goes to y
		A.scale = 2.0f;
		FoliageInstance B;
		B.type = 1;
		B.position.NewVector(-50.0f, 0.0f, 0.0f);
		B.up.NewVector(1.0f, 0.0f, 1.0f); // leaning 45 degrees toward +x
		const auto Leaves = std::make_shared<Foliage>(std::vector<FoliageType>{ Bush, Tree }, std::vector<FoliageInstance>{ A, B });
		CHECK(Leaves->GetNumInstances() == 2);
		CHECK(Leaves->CountInstances(1) == 1);
		REQUIRE(Leaves->GetMesh(0));

		const Matrix44 MA = Leaves->GetInstanceMatrix(A);
		Vector3d P;
		MA.Transform(P, Vector3d(10.0f, 0.0f, 0.0f));
		CHECK(Same(P, Vector3d(100.0f, 220.0f, 30.0f)));
		MA.Transform(P, Vector3d(0.0f, 0.0f, 10.0f));
		CHECK(Same(P, Vector3d(100.0f, 200.0f, 50.0f)));
		const Matrix44 MB = Leaves->GetInstanceMatrix(B);
		MB.Transform(P, Vector3d(0.0f, 0.0f, 100.0f));
		CHECK(Same(P, Vector3d(-50.0f + 70.7107f, 0.0f, 70.7107f)));

		Vector3d Low, High;
		REQUIRE(Leaves->GetBounds(Low, High));
		CHECK(High.GetZ() > 30.0f);
		// Removing the first type drops its instances; the second moves up.
		const std::shared_ptr<const Foliage> Fewer = Leaves->WithTypes({ Tree }, { 1 });
		CHECK(Fewer->GetNumTypes() == 1);
		REQUIRE(Fewer->GetNumInstances() == 1);
		CHECK(Fewer->GetInstances()[0].type == 0);

		// .qle: types (names with spaces) and instances.
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		REQUIRE(Doc->AddFoliage(Leaves));
		FString Error;
		const FString Qle = Temp.File("foliage.qle");
		REQUIRE(Doc->Save(Qle, Error));
		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		const Object* Loaded = nullptr;
		for (Object* Obj : Reopened->GetObjects())
		{
			Loaded = Obj->HasFoliage() ? Obj : Loaded;
		}
		REQUIRE(Loaded);
		const Foliage& Back = Loaded->GetFoliage();
		REQUIRE(Back.GetNumTypes() == 2);
		CHECK(Back.GetTypes()[1].name == "Big Oak");
		CHECK(Back.GetTypes()[1] == Tree);
		REQUIRE(Back.GetNumInstances() == 2);
		CHECK(Same(Back.GetInstances()[0].position, A.position));
		CHECK(Back.GetInstances()[0].yaw == doctest::Approx(A.yaw));
		CHECK(Back.GetInstances()[0].scale == 2.0f);
		CHECK(Same(Back.GetInstances()[1].up, B.up));
	}

	TEST_CASE("Painting foliage fills the brush to the type's density, on the ground, as one undo step")
	{
		FFoliageFixture F;
		REQUIRE(F.Mode.AddType("Grass"));
		CHECK(F.Doc->GetUndoDescription() == "Add Foliage Type");
		F.Stroke(Vector3d(0.0f, 0.0f, 0.0f), 2.0f);
		CHECK(F.Doc->GetUndoDescription() == "Paint Foliage");
		const float Expected = 2000.0f * 1.0e-6f * 3.14159265f * 256.0f * 256.0f; // about 412
		const int32 Count = F.Leaves().CountInstances(0);
		MESSAGE("grass: ", Count, " of about ", Expected);
		CHECK(Count > Expected * 0.85f);
		CHECK(Count < Expected * 1.1f);
		for (const FoliageInstance& Instance : F.Leaves().GetInstances())
		{
			const float R = std::sqrt(Instance.position.GetX() * Instance.position.GetX() + Instance.position.GetY() * Instance.position.GetY());
			CHECK(R <= 256.5f);
			CHECK(std::fabs(Instance.position.GetZ()) < 0.5f); // on the flat ground
			CHECK(Instance.scale >= 0.7f);
			CHECK(Instance.scale <= 1.4f);
		}
		// Painting again there adds nothing: it is full.
		F.Stroke(Vector3d(0.0f, 0.0f, 0.0f), 1.0f);
		CHECK(F.Leaves().CountInstances(0) < Count + Expected * 0.1f);
		// Undo takes a whole stroke away (the second added little or nothing).
		while (F.Doc->GetUndoDescription() == "Paint Foliage")
		{
			F.Doc->Undo();
		}
		REQUIRE(F.Mode.GetFoliageObject());
		CHECK(F.Leaves().CountInstances(0) == 0);
	}

	TEST_CASE("Foliage keeps its spacing, grows only on the slopes it may, and erases")
	{
		FFoliageFixture F;
		REQUIRE(F.Mode.AddType("Tree"));
		REQUIRE(F.Mode.AddType("Bush"));
		F.Mode.Brush.Radius = 900.0f;
		F.Stroke(Vector3d(0.0f, 0.0f, 0.0f), 3.0f);
		const std::vector<FoliageInstance>& All = F.Leaves().GetInstances();
		int32 Trees = 0;
		for (SIZE_T I = 0; I < All.size(); ++I)
		{
			if (All[I].type != 0)
			{
				continue;
			}
			++Trees;
			for (SIZE_T J = I + 1; J < All.size(); ++J)
			{
				if (All[J].type == 0)
				{
					const float Dx = All[I].position.GetX() - All[J].position.GetX(), Dy = All[I].position.GetY() - All[J].position.GetY();
					CHECK(Dx * Dx + Dy * Dy >= 220.0f * 220.0f);
				}
			}
		}
		CHECK(Trees > 5);
		const int32 Bushes = F.Leaves().CountInstances(1);
		CHECK(Bushes > 20);

		// Erasing only the trees (bushes unchecked).
		F.Mode.SetActive(1, false);
		F.Mode.Brush.Radius = 2000.0f;
		F.Stroke(Vector3d(0.0f, 0.0f, 0.0f), 2.0f, true);
		CHECK(F.Doc->GetUndoDescription() == "Erase Foliage");
		CHECK(F.Leaves().CountInstances(0) == 0);
		CHECK(F.Leaves().CountInstances(1) == Bushes);

		// Single: one where clicked.
		F.Mode.SetActive(0, true);
		F.Mode.Tool = EFoliageTool::Single;
		F.Stroke(Vector3d(600.0f, 600.0f, 0.0f), 0.5f);
		CHECK(F.Leaves().CountInstances(0) == 1);
		CHECK(F.Doc->GetUndoDescription() == "Place Foliage");

		// A 40 degree slope: no trees (30 at most), rocks (any slope) yes.
		FFoliageFixture Steep(40.0f);
		REQUIRE(Steep.Mode.AddType("Tree"));
		REQUIRE(Steep.Mode.AddType("Rock"));
		Steep.Mode.Brush.Radius = 600.0f;
		Steep.Stroke(Vector3d(0.0f, 0.0f, 0.0f), 2.0f);
		CHECK(Steep.Leaves().CountInstances(0) == 0);
		CHECK(Steep.Leaves().CountInstances(1) > 3);
		// Rocks lean with the ground, as far as they may (45 degrees).
		const FoliageInstance& Rock = Steep.Leaves().GetInstances().back();
		CHECK(std::acos(Rock.up.GetZ() / Rock.up.GetMag()) * 57.29578f == doctest::Approx(40.0f).epsilon(0.05));
	}

	TEST_CASE("Sculpting a landscape moves the foliage standing on it")
	{
		FFoliageFixture F;
		REQUIRE(F.Mode.AddType("Bush"));
		F.Mode.Brush.Radius = 700.0f;
		F.Stroke(Vector3d(0.0f, 0.0f, 0.0f), 2.0f);
		REQUIRE(F.Leaves().GetNumInstances() > 10);
		FMapLandscapeMode Sculpt;
		Sculpt.SetDocument(F.Doc.get());
		Sculpt.Tool = ELandscapeTool::Sculpt;
		Sculpt.Brush.Radius = 2000.0f;
		Sculpt.Brush.Strength = 1.0f;
		Sculpt.Brush.Falloff = 0.0f;
		Sculpt.BeginStroke(*F.Land, Vector3d(0.0f, 0.0f, 0.0f), false);
		Sculpt.ApplyAt(*F.Land, Vector3d(0.0f, 0.0f, 0.0f), 0.5f, false);
		Sculpt.EndStroke();
		const float Raised = F.Land->GetLandscape().GetHeight(32, 32);
		CHECK(Raised > 100.0f);
		for (const FoliageInstance& Instance : F.Leaves().GetInstances())
		{
			const float Ground = F.Land->GetLandscape().SampleHeight(Instance.position.GetX(), Instance.position.GetY());
			CHECK(std::fabs(Instance.position.GetZ() - Ground) < 0.5f);
		}
		// One undo step puts both back.
		CHECK(F.Doc->GetUndoDescription() == "Landscape Sculpt");
		F.Doc->Undo();
		CHECK(std::fabs(F.Leaves().GetInstances()[0].position.GetZ()) < 0.5f);
	}

	TEST_CASE("Foliage is drawn (D3D12 and Vulkan), culled past its distance, and not picked")
	{
		for (const ERHIBackend Backend : { ERHIBackend::D3D12, ERHIBackend::Vulkan })
		{
			CAPTURE(std::string(ToString(Backend)));
			FTestLogCapture Log;
			FViewFixture F;
			if (!F.Create(Backend, EMapViewType::Perspective))
			{
				MESSAGE(std::string(ToString(Backend)), " device unavailable; skipped");
				continue;
			}
			Object* Land = F.Doc->AddLandscape(16, 16, 32.0f, Vector3d(0.0f, 0.0f, 0.0f));
			REQUIRE(Land);
			F.Doc->SetSelection({ Land });
			F.Client->FrameSelection();
			F.Doc->ClearSelection();
			F.Client->SetViewMode(EMapViewMode::Lit);
			const FVector2 Center = F.At(Vector3d(0.0f, 0.0f, 0.0f));
			const int32 X = static_cast<int32>(Center.X), Y = static_cast<int32>(Center.Y);
			const FColor Bare = PixelAt(F.Render(), X, Y);

			FMapFoliageMode Mode;
			Mode.SetDocument(F.Doc.get());
			REQUIRE(Mode.AddType("Bush"));
			FoliageType Dense = Mode.GetFoliageObject()->GetFoliage().GetTypes()[0];
			Dense.density = 400.0f;
			Dense.radius = 20.0f;
			REQUIRE(Mode.SetType(0, Dense));
			Mode.Brush.Radius = 200.0f;
			Mode.Brush.Strength = 1.0f;
			Mode.Brush.Falloff = 0.0f;
			REQUIRE(Mode.BeginStroke(Vector3d(0.0f, 0.0f, 0.0f), false));
			for (int32 Step = 0; Step < 60; ++Step)
			{
				Mode.ApplyAt(Vector3d(0.0f, 0.0f, 0.0f), 1.0f / 30.0f, false);
			}
			Mode.EndStroke();
			REQUIRE(Mode.GetFoliageObject()->GetFoliage().GetNumInstances() > 20);

			View::foliageInstancesDrawn = 0;
			const TArray<uint8> Pixels = F.Render();
			const FColor Leafy = PixelAt(Pixels, X, Y);
			CHECK(View::foliageInstancesDrawn > 20);
			CHECK(Leafy != Bare);
			// Beside the origin's axis lines, the bushes' green.
			int32 Green = 0, Seen = 0;
			const FVector2 Beside = F.At(Vector3d(60.0f, 60.0f, 30.0f));
			for (int32 DY = -6; DY <= 6; ++DY)
			{
				for (int32 DX = -6; DX <= 6; ++DX)
				{
					const FColor C = PixelAt(Pixels, static_cast<int32>(Beside.X) + DX, static_cast<int32>(Beside.Y) + DY);
					Green += C.G > C.R + 20 && C.G > C.B + 20 ? 1 : 0;
					++Seen;
				}
			}
			CHECK(Green > Seen / 3);
			// Culled: drawn nowhere farther than 1 unit.
			Dense.cullDistance = 1.0f;
			REQUIRE(Mode.SetType(0, Dense));
			View::foliageInstancesDrawn = 0;
			const FColor Culled = PixelAt(F.Render(), X, Y);
			CHECK(View::foliageInstancesDrawn == 0);
			CHECK(Culled == Bare);
			// Rays go through it to the ground.
			Vector3d Hit;
			REQUIRE(Mode.TraceSurface(Vector3d(0.0f, 0.0f, 500.0f), Vector3d(0.0f, 0.0f, -1.0f), Hit));
			CHECK(std::fabs(Hit.GetZ()) < 0.5f);
			CHECK(Log.Problems.empty());
			for (const FString& Problem : Log.Problems)
			{
				MESSAGE(Problem.c_str());
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Foliage drawn instanced on the GPU.

TEST_SUITE("MapEditor")
{
	TEST_CASE("Instanced foliage looks as the triangles View makes would, and is quicker (D3D12 and Vulkan)")
	{
		for (const ERHIBackend Backend : { ERHIBackend::D3D12, ERHIBackend::Vulkan })
		{
			CAPTURE(std::string(ToString(Backend)));
			FTestLogCapture Log;
			FViewFixture F;
			if (!F.Create(Backend, EMapViewType::Perspective))
			{
				MESSAGE(std::string(ToString(Backend)), " device unavailable; skipped");
				continue;
			}
			Object* Land = F.Doc->AddLandscape(32, 32, 32.0f, Vector3d(0.0f, 0.0f, 0.0f));
			REQUIRE(Land);
			F.Doc->SetSelection({ Land });
			F.Client->FrameSelection();
			F.Doc->ClearSelection();
			F.Client->SetViewMode(EMapViewMode::Lit);

			// Trees, bushes, rocks and grass all over it.
			FMapFoliageMode Mode;
			Mode.SetDocument(F.Doc.get());
			for (const char* Name : { "Tree", "Bush", "Rock", "Grass" })
			{
				REQUIRE(Mode.AddType(Name));
			}
			Mode.Brush.Radius = 700.0f;
			Mode.Brush.Strength = 1.0f;
			Mode.Brush.Falloff = 0.0f;
			REQUIRE(Mode.BeginStroke(Vector3d(0.0f, 0.0f, 0.0f), false));
			for (int32 Step = 0; Step < 90; ++Step)
			{
				Mode.ApplyAt(Vector3d(0.0f, 0.0f, 0.0f), 1.0f / 30.0f, false);
			}
			Mode.EndStroke();
			const int32 Instances = Mode.GetFoliageObject()->GetFoliage().GetNumInstances();
			REQUIRE(Instances > 1500);

			F.Resources->bInstanceMeshes = false;
			const TArray<uint8> Triangles = F.Render();
			F.Resources->bInstanceMeshes = true;
			View::foliageInstancesDrawn = 0;
			const TArray<uint8> Instanced = F.Render();
			CHECK(View::foliageInstancesDrawn > 1000);

			// The same picture but for edges (rasterizing the same triangles
			// from other vertices): nearly every pixel within a few levels.
			int32 Close = 0, Total = 0;
			for (int32 Y = 0; Y < 256; ++Y)
			{
				for (int32 X = 0; X < 256; ++X)
				{
					const FColor A = PixelAt(Triangles, X, Y), B = PixelAt(Instanced, X, Y);
					const int32 Diff = std::abs(int32(A.R) - int32(B.R)) + std::abs(int32(A.G) - int32(B.G)) + std::abs(int32(A.B) - int32(B.B));
					Close += Diff <= 12 ? 1 : 0;
					++Total;
				}
			}
			MESSAGE(std::string(ToString(Backend)), ": ", Instances, " instances, ", Close * 100 / Total, "% of pixels alike");
			CHECK(Close > Total * 9 / 10);

			// And quicker to draw.
			// (Frames as the editor draws them: no reading the picture back.)
			const auto Time = [&F](bool bInstanced)
			{
				F.Resources->bInstanceMeshes = bInstanced;
				F.Render();
				const double Start = FPlatformTime::Seconds();
				for (int32 Frame = 0; Frame < 10; ++Frame)
				{
					F.Client->RequestRedraw();
					FRHICommandList& CommandList = F.Device->BeginFrame();
					F.Renderer->BeginFrame();
					F.Client->RenderIfNeeded(CommandList);
					F.Device->EndFrame();
				}
				F.Device->WaitIdle();
				return (FPlatformTime::Seconds() - Start) / 10.0;
			};
			const double Cpu = Time(false);
			const double Gpu = Time(true);
			MESSAGE(std::string(ToString(Backend)), ": a frame as triangles ", Cpu * 1000.0, " ms, instanced ", Gpu * 1000.0, " ms");
			// (Software rasterizers, as tests use, spend the time filling
			// pixels either way: instanced is no slower there.)
			CHECK(Gpu < Cpu * 1.2);
			CHECK(Log.Problems.empty());
			for (const FString& Problem : Log.Problems)
			{
				MESSAGE(Problem.c_str());
			}
		}
	}
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("Many foliage instances on the GPU (hardware adapter, when there is one)")
	{
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Perspective, false))
		{
			MESSAGE("no hardware D3D12 device; skipped");
			return;
		}
		Object* Land = F.Doc->AddLandscape(64, 64, 32.0f, Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		F.Doc->SetSelection({ Land });
		F.Client->FrameSelection();
		F.Doc->ClearSelection();
		F.Client->SetViewMode(EMapViewMode::Lit);
		FMapFoliageMode Mode;
		Mode.SetDocument(F.Doc.get());
		REQUIRE(Mode.AddType("Bush"));
		REQUIRE(Mode.AddType("Grass"));
		FoliageType Bush = Mode.GetFoliageObject()->GetFoliage().GetTypes()[0];
		Bush.density = 3000.0f;
		Bush.radius = 0.0f;
		REQUIRE(Mode.SetType(0, Bush));
		FoliageType Grass = Mode.GetFoliageObject()->GetFoliage().GetTypes()[1];
		Grass.density = 6000.0f;
		Grass.cullDistance = 0.0f;
		REQUIRE(Mode.SetType(1, Grass));
		Mode.Brush.Radius = 1000.0f;
		Mode.Brush.Strength = 1.0f;
		Mode.Brush.Falloff = 0.0f;
		REQUIRE(Mode.BeginStroke(Vector3d(0.0f, 0.0f, 0.0f), false));
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Mode.ApplyAt(Vector3d(0.0f, 0.0f, 0.0f), 1.0f / 30.0f, false);
		}
		Mode.EndStroke();
		const int32 Instances = Mode.GetFoliageObject()->GetFoliage().GetNumInstances();
		REQUIRE(Instances > 20000);
		const auto Time = [&F](bool bInstanced)
		{
			F.Resources->bInstanceMeshes = bInstanced;
			F.Render();
			const double Start = FPlatformTime::Seconds();
			for (int32 Frame = 0; Frame < 10; ++Frame)
			{
				F.Client->RequestRedraw();
				FRHICommandList& CommandList = F.Device->BeginFrame();
				F.Renderer->BeginFrame();
				F.Client->RenderIfNeeded(CommandList);
				F.Device->EndFrame();
			}
			F.Device->WaitIdle();
			return (FPlatformTime::Seconds() - Start) / 10.0;
		};
		const double Cpu = Time(false);
		View::foliageInstancesDrawn = 0;
		const double Gpu = Time(true);
		MESSAGE(Instances, " instances (", View::foliageInstancesDrawn / 11, " drawn a frame): as triangles ", Cpu * 1000.0, " ms a frame, instanced ",
			Gpu * 1000.0, " ms");
		CHECK(Gpu * 2.0 < Cpu);
	}
}

// ---------------------------------------------------------------------------
// Landscape splines.

namespace
{
	LandscapeSplinePoint SplinePoint(float X, float Y, float Z, float Width = 128.0f, float Falloff = 128.0f)
	{
		LandscapeSplinePoint Point;
		Point.position.NewVector(X, Y, Z);
		Point.width = Width;
		Point.falloff = Falloff;
		return Point;
	}
}

TEST_SUITE("MapEditor")
{
	TEST_CASE("A landscape spline's curve runs through its points, its width changing between them")
	{
		EnsureMapCore();
		LandscapeSpline Spline;
		Spline.points = { SplinePoint(0.0f, 0.0f, 0.0f, 100.0f), SplinePoint(500.0f, 200.0f, 50.0f, 300.0f), SplinePoint(1000.0f, 0.0f, 0.0f, 100.0f) };
		std::vector<LandscapeSpline::Sample> Samples;
		Spline.SampleCurve(10.0f, Samples);
		REQUIRE(Samples.size() > 100);
		CHECK(Same(Samples.front().position, Spline.points[0].position));
		CHECK(Same(Samples.back().position, Spline.points[2].position));
		// Through the middle point, smoothly (no step longer than about 10).
		bool bThrough = false;
		for (SIZE_T I = 0; I < Samples.size(); ++I)
		{
			bThrough |= Same(Samples[I].position, Spline.points[1].position);
			if (I > 0)
			{
				Vector3d Step(Samples[I].position);
				Step.SubVector(Samples[I - 1].position);
				CHECK(Step.GetMag() < 14.0f);
			}
		}
		CHECK(bThrough);
		CHECK(Samples[Samples.size() / 4].width > 100.0f);
		CHECK(Samples[Samples.size() / 4].width < 300.0f);
		// Closed: back to the start.
		Spline.closed = true;
		Spline.SampleCurve(10.0f, Samples);
		CHECK(Same(Samples.back().position, Spline.points[0].position));
		CHECK(Samples.size() > 200);
	}

	TEST_CASE("Applying splines shapes the ground to them and paints their layer")
	{
		EnsureMapCore();
		// Rolling ground about 0, 2048 units across.
		std::vector<float> Heights;
		for (int32 Y = 0; Y <= 64; ++Y)
		{
			for (int32 X = 0; X <= 64; ++X)
			{
				Heights.push_back(60.0f * std::sin(X * 0.3f) * std::cos(Y * 0.2f));
			}
		}
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Land = Doc->AddLandscape(Landscape::Create(64, 64, 32.0f, Heights), Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		REQUIRE(Mode.AddLayer(*Land)); // Grass
		REQUIRE(Mode.AddLayer(*Land)); // Rock
		// A road at height 20 across the middle, 128 wide, fading over 128.
		LandscapeSpline Road;
		Road.points = { SplinePoint(-800.0f, 0.0f, 20.0f), SplinePoint(0.0f, 0.0f, 20.0f), SplinePoint(800.0f, 0.0f, 20.0f) };
		Road.layer = "Rock";
		Doc->SetLandscape(*Land, Land->GetLandscape().WithSplines({ Road }), "Road");
		REQUIRE(Mode.ApplySplines(*Land));
		CHECK(Doc->GetUndoDescription() == "Apply Splines to Landscape");
		const Landscape& After = Land->GetLandscape();
		CHECK(After.GetSplines().size() == 1); // the splines stay
		// On the road (y = 0 is grid row 32): its height, and rock.
		for (int32 X = 10; X <= 54; X += 4)
		{
			CHECK(After.GetHeight(X, 32) == doctest::Approx(20.0f).epsilon(0.001));
			CHECK(After.GetWeight(1, X, 32) == 1.0f);
			CHECK(After.GetWeight(0, X, 32) == 0.0f);
		}
		// Within the falloff: partly; well away: as it was.
		CHECK(std::fabs(After.GetHeight(32, 32 + 5) - Heights[37 * 65 + 32]) > 0.0f);
		CHECK(After.GetHeight(32, 32 + 12) == Heights[44 * 65 + 32]);
		CHECK(After.GetWeight(1, 32, 44) == 0.0f);
		// Well past its ends (beyond the falloff round them): untouched.
		CHECK(After.GetHeight(0, 32) == Heights[32 * 65 + 0]);

		// Raise only: ground above the road stays.
		Doc->Undo();
		Road.lowerTerrain = false;
		Doc->SetLandscape(*Land, Land->GetLandscape().WithSplines({ Road }), "Road");
		REQUIRE(Mode.ApplySplines(*Land));
		for (int32 X = 10; X <= 54; ++X)
		{
			const float Was = Heights[32 * 65 + X];
			CHECK(Land->GetLandscape().GetHeight(X, 32) == doctest::Approx(Was > 20.0f ? Was : 20.0f).epsilon(0.001));
		}
		// Sculpting keeps the splines.
		CHECK(Land->GetLandscape().WithHeights(Heights)->GetSplines().size() == 1);
		CHECK(!Landscape(4, 4, 32.0f).WithSplinesApplied());
	}

	TEST_CASE("Spline points are added, extended, moved and deleted, each an undo step, and saved")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		Mode.NewQuadsX = 32;
		Mode.NewQuadsY = 32;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		CHECK(!Mode.HasSelectedSplinePoint());
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(-300.0f, 0.0f, 0.0f)));
		CHECK(Doc->GetUndoDescription() == "Add Spline Point");
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(0.0f, 100.0f, 0.0f)));
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(300.0f, 0.0f, 0.0f)));
		REQUIRE(Land->GetLandscape().GetSplines().size() == 1);
		CHECK(Land->GetLandscape().GetSplines()[0].points.size() == 3);
		CHECK(Mode.SelectedPoint == 2);
		// From the first point: before it.
		Mode.SelectedPoint = 0;
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(-500.0f, -50.0f, 0.0f)));
		CHECK(Mode.SelectedPoint == 0);
		CHECK(Land->GetLandscape().GetSplines()[0].points[0].position.GetX() == -500.0f);
		CHECK(Land->GetLandscape().GetSplines()[0].points.size() == 4);
		// Nothing selected: a new spline.
		Mode.SelectedSpline = Mode.SelectedPoint = -1;
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(0.0f, -400.0f, 0.0f)));
		CHECK(Land->GetLandscape().GetSplines().size() == 2);
		CHECK(Mode.SelectedSpline == 1);

		REQUIRE(Mode.MoveSplinePoint(*Land, 0, 1, Vector3d(-250.0f, 30.0f, 0.0f)));
		CHECK(Doc->GetUndoDescription() == "Move Spline Point");
		CHECK(Land->GetLandscape().GetSplines()[0].points[1].position.GetY() == 30.0f);
		Mode.SelectedSpline = 0;
		Mode.SelectedPoint = 1;
		REQUIRE(Mode.DeleteSelectedSplinePoint());
		CHECK(Land->GetLandscape().GetSplines()[0].points.size() == 3);
		Doc->Undo();
		CHECK(Land->GetLandscape().GetSplines()[0].points.size() == 4);

		// .qle
		LandscapeSpline River = Land->GetLandscape().GetSplines()[0];
		River.layer = "River Bed";
		River.raiseTerrain = false;
		River.closed = true;
		River.points[2].width = 333.0f;
		REQUIRE(Mode.SetSpline(*Land, 0, River));
		FString Error;
		const FString Qle = Temp.File("splines.qle");
		REQUIRE(Doc->Save(Qle, Error));
		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		const Object* Loaded = nullptr;
		for (Object* Obj : Reopened->GetObjects())
		{
			Loaded = Obj->HasLandscape() ? Obj : Loaded;
		}
		REQUIRE(Loaded);
		const std::vector<LandscapeSpline>& Splines = Loaded->GetLandscape().GetSplines();
		REQUIRE(Splines.size() == 2);
		CHECK(Splines[0].layer == "River Bed");
		CHECK(!Splines[0].raiseTerrain);
		CHECK(Splines[0].lowerTerrain);
		CHECK(Splines[0].closed);
		REQUIRE(Splines[0].points.size() == 4);
		CHECK(Splines[0].points[2].width == 333.0f);
		CHECK(Same(Splines[0].points[0].position, River.points[0].position));
		CHECK(Splines[1].points.size() == 1);
	}

	TEST_CASE("The Splines tool: Ctrl+click adds points, a drag moves one, with the mouse in a view")
	{
		FTestLogCapture Log;
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Top))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		FMapLandscapeMode Mode;
		Mode.SetDocument(F.Doc.get());
		Mode.NewQuadsX = 32;
		Mode.NewQuadsY = 32;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		F.Doc->ClearSelection();
		F.Client->ResetCamera();
		Mode.Tool = ELandscapeTool::Splines;
		F.Settings.EditorMode = &Mode;
		F.Render();
		const TSharedRef<SWindow> Window = ToSharedRef(F.Window);
		const auto CtrlClick = [&](const Vector3d& At)
		{
			F.T.SetModifiers(false, true);
			F.T.MouseMove(Window, F.At(At));
			F.T.MouseDown(Window, F.At(At));
			F.T.MouseUp(Window, F.At(At));
			F.T.Tick();
			F.T.SetModifiers(false, false);
		};
		CtrlClick(Vector3d(-200.0f, 0.0f, 0.0f));
		CtrlClick(Vector3d(0.0f, 120.0f, 0.0f));
		CtrlClick(Vector3d(200.0f, 0.0f, 0.0f));
		REQUIRE(Land->GetLandscape().GetSplines().size() == 1);
		CHECK(Land->GetLandscape().GetSplines()[0].points.size() == 3);
		CHECK(F.Doc->GetSelection().empty()); // no box selection, no picking

		// A drag of the middle point (one undo step).
		F.T.MouseMove(Window, F.At(Vector3d(0.0f, 120.0f, 0.0f)));
		F.T.MouseDown(Window, F.At(Vector3d(0.0f, 120.0f, 0.0f)));
		F.T.Tick();
		CHECK(Mode.SelectedPoint == 1);
		F.T.MouseMove(Window, F.At(Vector3d(0.0f, 60.0f, 0.0f)));
		F.T.MouseMove(Window, F.At(Vector3d(0.0f, -100.0f, 0.0f)));
		F.T.MouseUp(Window, F.At(Vector3d(0.0f, -100.0f, 0.0f)));
		F.T.Tick();
		const Vector3d& Moved = Land->GetLandscape().GetSplines()[0].points[1].position;
		CHECK(std::fabs(Moved.GetY() + 100.0f) < 8.0f);
		CHECK(F.Doc->GetUndoDescription() == "Move Spline Point");
		F.Doc->Undo();
		CHECK(std::fabs(Land->GetLandscape().GetSplines()[0].points[1].position.GetY() - 120.0f) < 8.0f);
		// The curve shows: a pixel on it is the spline's color.
		const TArray<uint8> Pixels = F.Render();
		const FVector2 OnCurve = F.At(Vector3d(-200.0f, 0.0f, 0.0f));
		const FColor C = PixelAt(Pixels, static_cast<int32>(OnCurve.X), static_cast<int32>(OnCurve.Y));
		CHECK(C.R > 180);
		F.Settings.EditorMode = nullptr;
		CHECK(Log.Problems.empty());
	}
}

// ---------------------------------------------------------------------------
// Spline meshes.

TEST_SUITE("MapEditor")
{
	TEST_CASE("Spline meshes run along +X, centered across, colored")
	{
		EnsureMapCore();
		REQUIRE(StaticMesh::GetSplineMeshNames().size() == 4);
		for (const std::string& Name : StaticMesh::GetSplineMeshNames())
		{
			CAPTURE(Name);
			const std::shared_ptr<const StaticMesh> Mesh = StaticMesh::Find(Name.c_str());
			REQUIRE(Mesh);
			CHECK(Mesh->HasVertexColors());
			Vector3d Low, High;
			Mesh->GetBounds(Low, High);
			CHECK(High.GetX() - Low.GetX() >= 190.0f);
			CHECK(std::fabs(Low.GetY() + High.GetY()) < 1.0f);
		}
	}

	TEST_CASE("A spline's mesh repeats along it and bends to it, fitted to its width")
	{
		EnsureMapCore();
		LandscapeSpline Spline;
		Spline.mesh = "Road";
		Spline.points = { SplinePoint(0.0f, 0.0f, 10.0f, 300.0f), SplinePoint(1000.0f, 0.0f, 10.0f, 300.0f) };
		const std::shared_ptr<const StaticMesh> Straight = Landscape::BuildSplineMesh(Spline);
		REQUIRE(Straight);
		const std::shared_ptr<const StaticMesh> Road = StaticMesh::Find("Road");
		CHECK(Straight->GetNumVertices() == 2 * Road->GetNumVertices()); // 1000 / 512: two pieces
		Vector3d Low, High;
		Straight->GetBounds(Low, High);
		CHECK(std::fabs(Low.GetX()) < 0.5f);
		CHECK(High.GetX() == doctest::Approx(1000.0f).epsilon(0.002));
		CHECK(High.GetY() == doctest::Approx(150.0f).epsilon(0.01)); // its 256 stretched to 300
		CHECK(High.GetZ() == doctest::Approx(13.5f).epsilon(0.01));  // its top, 3.5 above the curve
		// Its top faces up.
		float Up = 0.0f;
		int32 Count = 0;
		for (const StaticMesh::Triangle& Triangle : Straight->GetTriangles())
		{
			if (Straight->GetVertex(Triangle.v[0]).GetZ() > 12.0f)
			{
				Up += Triangle.norm[0].GetZ();
				++Count;
			}
		}
		REQUIRE(Count > 0);
		CHECK(Up / Count > 0.95f);

		// Round a bend, every vertex stays on the road's band about the curve.
		Spline.points = { SplinePoint(0.0f, 0.0f, 0.0f, 200.0f), SplinePoint(800.0f, 0.0f, 0.0f, 200.0f), SplinePoint(800.0f, 800.0f, 0.0f, 200.0f) };
		const std::shared_ptr<const StaticMesh> Bent = Landscape::BuildSplineMesh(Spline);
		REQUIRE(Bent);
		std::vector<LandscapeSpline::Sample> Samples;
		Spline.SampleCurve(4.0f, Samples);
		for (int32 V = 0; V < Bent->GetNumVertices(); V += 7)
		{
			const Vector3d& P = Bent->GetVertex(V);
			float Nearest = 1.0e9f;
			for (const LandscapeSpline::Sample& Sample : Samples)
			{
				const float Dx = P.GetX() - Sample.position.GetX(), Dy = P.GetY() - Sample.position.GetY();
				Nearest = FMath::Min(Nearest, std::sqrt(Dx * Dx + Dy * Dy));
			}
			CHECK(Nearest < 106.0f);
		}
		// No mesh, an unknown one, or one point: none.
		Spline.mesh = "";
		CHECK(!Landscape::BuildSplineMesh(Spline));
		Spline.mesh = "Teapot";
		CHECK(!Landscape::BuildSplineMesh(Spline));
	}

	TEST_CASE("A landscape keeps its splines' meshes through sculpting, takes them into its bounds, and saves them")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		Object* Land = Doc->AddLandscape(32, 32, 32.0f, Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		LandscapeSpline Fence;
		Fence.mesh = "Fence";
		Fence.meshFitWidth = false;
		Fence.meshScale = 1.5f;
		Fence.meshZOffset = 4.0f;
		Fence.points = { SplinePoint(-300.0f, 0.0f, 0.0f), SplinePoint(300.0f, 100.0f, 0.0f) };
		Doc->SetLandscape(*Land, Land->GetLandscape().WithSplines({ Fence }), "Fence");
		const std::shared_ptr<const StaticMesh> Built = Land->GetLandscape().GetSplineMeshes()[0];
		REQUIRE(Built);
		// Sculpting: the same mesh, not made again.
		const std::shared_ptr<const Landscape> Raised = Land->GetLandscape().WithHeights(std::vector<float>(33 * 33, 50.0f));
		CHECK(Raised->GetSplineMeshes()[0] == Built);
		// The fence's top (110 tall, 1.5 times, 4 up) is in the bounds.
		Vector3d Low, High;
		Land->GetLandscape().GetBounds(Low, High);
		CHECK(High.GetZ() == doctest::Approx(169.0f).epsilon(0.01));

		FString Error;
		const FString Qle = Temp.File("fence.qle");
		REQUIRE(Doc->Save(Qle, Error));
		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		const Object* Loaded = nullptr;
		for (Object* Obj : Reopened->GetObjects())
		{
			Loaded = Obj->HasLandscape() ? Obj : Loaded;
		}
		REQUIRE(Loaded);
		REQUIRE(Loaded->GetLandscape().GetSplines().size() == 1);
		const LandscapeSpline& Back = Loaded->GetLandscape().GetSplines()[0];
		CHECK(Back.mesh == "Fence");
		CHECK(!Back.meshFitWidth);
		CHECK(Back.meshScale == 1.5f);
		CHECK(Back.meshZOffset == 4.0f);
		REQUIRE(Loaded->GetLandscape().GetSplineMeshes()[0]);
		CHECK(Loaded->GetLandscape().GetSplineMeshes()[0]->GetNumVertices() == Built->GetNumVertices());
	}

	TEST_CASE("A road spline's mesh is drawn on the landscape (D3D12 and Vulkan)")
	{
		for (const ERHIBackend Backend : { ERHIBackend::D3D12, ERHIBackend::Vulkan })
		{
			CAPTURE(std::string(ToString(Backend)));
			FTestLogCapture Log;
			FViewFixture F;
			if (!F.Create(Backend, EMapViewType::Perspective))
			{
				MESSAGE(std::string(ToString(Backend)), " device unavailable; skipped");
				continue;
			}
			Object* Land = F.Doc->AddLandscape(32, 32, 32.0f, Vector3d(0.0f, 0.0f, 0.0f));
			REQUIRE(Land);
			F.Doc->SetSelection({ Land });
			F.Client->FrameSelection();
			F.Doc->ClearSelection();
			F.Client->SetViewMode(EMapViewMode::Lit);
			// Beside the origin's axis lines, and beside the spline's own line
			// down the road's middle.
			const FVector2 OnRoad = F.At(Vector3d(-200.0f, 360.0f, 0.0f));
			const int32 X = static_cast<int32>(OnRoad.X), Y = static_cast<int32>(OnRoad.Y);
			const FColor Ground = PixelAt(F.Render(), X, Y);
			LandscapeSpline Road;
			Road.mesh = "Road";
			Road.points = { SplinePoint(-480.0f, 300.0f, 0.0f, 200.0f), SplinePoint(480.0f, 300.0f, 0.0f, 200.0f) };
			F.Doc->SetLandscape(*Land, Land->GetLandscape().WithSplines({ Road }), "Road");
			const FColor Asphalt = PixelAt(F.Render(), X, Y);
			CHECK(Asphalt != Ground);
			// Grey and dark: the road's.
			CHECK(std::abs(int32(Asphalt.R) - int32(Asphalt.B)) < 20);
			CHECK(Asphalt.G < Ground.G);
			CHECK(Log.Problems.empty());
		}
	}
}

// ---------------------------------------------------------------------------
// Spline tangents and junctions.

TEST_SUITE("MapEditor")
{
	TEST_CASE("A spline's tangents: automatic ones are Catmull-Rom's, ones set by hand steer the curve")
	{
		EnsureMapCore();
		LandscapeSpline Spline;
		Spline.points = { SplinePoint(0.0f, 0.0f, 0.0f), SplinePoint(400.0f, 0.0f, 0.0f), SplinePoint(800.0f, 400.0f, 0.0f) };
		CHECK(Same(Spline.GetTangent(1), Vector3d(400.0f, 200.0f, 0.0f)));  // half of next - previous
		CHECK(Same(Spline.GetTangent(0), Vector3d(200.0f, 0.0f, 0.0f)));    // the ends repeat
		// Halfway between the first two points, Catmull-Rom's own value.
		std::vector<LandscapeSpline::Sample> Samples;
		Spline.SampleCurve(1000.0f, Samples); // a step a segment: t = 0, then the next point
		REQUIRE(Samples.size() == 3);
		Spline.SampleCurve(400.0f, Samples); // two steps on the first segment
		// t = 0.5 on the first segment: 0.5 p1 + 0.125 m1 + 0.5 p2 - 0.125 m2.
		CHECK(Same(Samples[1].position, Vector3d(200.0f + 25.0f - 50.0f, -25.0f, 0.0f)));

		// By hand at the middle point: straight up the y axis, long.
		Spline.points[1].autoTangent = false;
		Spline.points[1].tangent.NewVector(0.0f, 900.0f, 0.0f);
		Spline.SampleCurve(4.0f, Samples);
		// Still through the point; leaving it along +y.
		SIZE_T At = 0;
		for (SIZE_T I = 0; I < Samples.size(); ++I)
		{
			At = Same(Samples[I].position, Spline.points[1].position) ? I : At;
		}
		REQUIRE(At > 0);
		Vector3d Leaving(Samples[At + 1].position);
		Leaving.SubVector(Samples[At].position);
		CHECK(std::fabs(Leaving.GetX()) < 0.5f);
		CHECK(Leaving.GetY() > 2.0f);
		// It bulges: the first segment dips below y = 0 further than before.
		float Lowest = 0.0f;
		for (SIZE_T I = 0; I < At; ++I)
		{
			Lowest = FMath::Min(Lowest, Samples[I].position.GetY());
		}
		CHECK(Lowest < -60.0f);
	}

	TEST_CASE("Spline junctions: connecting points, moving them together, breaking and saving them")
	{
		EnsureMapCore();
		FTempFolder Temp;
		TUniquePtr<FMapDocument> Doc = FMapDocument::CreateNew();
		FMapLandscapeMode Mode;
		Mode.SetDocument(Doc.get());
		Mode.NewQuadsX = 64;
		Mode.NewQuadsY = 64;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		// A main road west to east, and a side road from the north.
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(-800.0f, 0.0f, 0.0f)));
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(0.0f, 0.0f, 0.0f)));
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(800.0f, 0.0f, 0.0f)));
		Mode.SelectedSpline = Mode.SelectedPoint = -1;
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(0.0f, 800.0f, 0.0f)));
		REQUIRE(Mode.AddSplinePoint(*Land, Vector3d(0.0f, 300.0f, 0.0f)));
		// From the side road's end to the main road's middle: a junction.
		REQUIRE(Mode.ConnectSplinePoint(*Land, 0, 1));
		CHECK(Doc->GetUndoDescription() == "Connect Spline Points");
		const std::vector<LandscapeSpline>* Splines = &Land->GetLandscape().GetSplines();
		REQUIRE((*Splines)[1].points.size() == 3);
		CHECK(Same((*Splines)[1].points[2].position, (*Splines)[0].points[1].position));
		const int Joint = (*Splines)[0].points[1].joint;
		CHECK(Joint >= 0);
		CHECK((*Splines)[1].points[2].joint == Joint);
		CHECK(Mode.SelectedSpline == 1);
		CHECK(Mode.SelectedPoint == 2);

		// Moving either moves both.
		REQUIRE(Mode.MoveSplinePoint(*Land, 0, 1, Vector3d(50.0f, -40.0f, 0.0f)));
		Splines = &Land->GetLandscape().GetSplines();
		CHECK(Same((*Splines)[1].points[2].position, Vector3d(50.0f, -40.0f, 0.0f)));
		CHECK(Same((*Splines)[0].points[1].position, Vector3d(50.0f, -40.0f, 0.0f)));

		// From the main road's west end, nothing selected: a branch starts there.
		Mode.SelectedSpline = Mode.SelectedPoint = -1;
		REQUIRE(Mode.ConnectSplinePoint(*Land, 0, 0));
		Splines = &Land->GetLandscape().GetSplines();
		REQUIRE(Splines->size() == 3);
		CHECK((*Splines)[2].points.size() == 1);
		CHECK((*Splines)[2].points[0].joint == (*Splines)[0].points[0].joint);
		CHECK((*Splines)[0].points[0].joint != Joint);
		// From a middle point to another point: a spline between them.
		Mode.SplineLand = Land;
		Mode.SelectedSpline = 1;
		Mode.SelectedPoint = 1;
		REQUIRE(Mode.ConnectSplinePoint(*Land, 0, 2));
		Splines = &Land->GetLandscape().GetSplines();
		REQUIRE(Splines->size() == 4);
		CHECK((*Splines)[3].points.size() == 2);
		CHECK((*Splines)[3].points[0].joint == (*Splines)[1].points[1].joint);
		CHECK((*Splines)[3].points[1].joint == (*Splines)[0].points[2].joint);
		CHECK((*Splines)[3].points[0].joint != (*Splines)[3].points[1].joint); // two places, two junctions

		// Breaking: it moves alone.
		Mode.SelectedSpline = 1;
		Mode.SelectedPoint = 2;
		REQUIRE(Mode.BreakSelectedJunction());
		REQUIRE(Mode.MoveSplinePoint(*Land, 1, 2, Vector3d(100.0f, 100.0f, 0.0f)));
		Splines = &Land->GetLandscape().GetSplines();
		CHECK(Same((*Splines)[0].points[1].position, Vector3d(50.0f, -40.0f, 0.0f)));
		Doc->Undo();
		Doc->Undo();
		Splines = &Land->GetLandscape().GetSplines();
		CHECK((*Splines)[1].points[2].joint == Joint);

		// Tangents by hand and junctions in .qle; and back to automatic.
		REQUIRE(Mode.SetSplineTangent(*Land, 0, 1, Vector3d(300.0f, 120.0f, 10.0f)));
		CHECK(Doc->GetUndoDescription() == "Change Spline Tangent");
		FString Error;
		const FString Qle = Temp.File("junctions.qle");
		REQUIRE(Doc->Save(Qle, Error));
		TUniquePtr<FMapDocument> Reopened = FMapDocument::Open(Qle, Error);
		REQUIRE(Reopened);
		const Object* Loaded = nullptr;
		for (Object* Obj : Reopened->GetObjects())
		{
			Loaded = Obj->HasLandscape() ? Obj : Loaded;
		}
		REQUIRE(Loaded);
		const std::vector<LandscapeSpline>& Back = Loaded->GetLandscape().GetSplines();
		REQUIRE(Back.size() == Splines->size());
		CHECK(!Back[0].points[1].autoTangent);
		CHECK(Same(Back[0].points[1].tangent, Vector3d(300.0f, 120.0f, 10.0f)));
		CHECK(Back[0].points[1].joint == Joint);
		CHECK(Back[1].points[2].joint == Joint);
		CHECK(Back[1].points[0].autoTangent);
		CHECK(Back[1].points[0].joint == -1);
		REQUIRE(Mode.SetSplineAutoTangent(*Land, 0, 1));
		CHECK(Land->GetLandscape().GetSplines()[0].points[1].autoTangent);

		// Deleting one of a junction of two leaves the other in none.
		Mode.SelectedSpline = 2;
		Mode.SelectedPoint = 0;
		REQUIRE(Mode.DeleteSelectedSplinePoint());
		CHECK(Land->GetLandscape().GetSplines()[0].points[0].joint == -1);
	}

	TEST_CASE("Ctrl+click on a point makes a junction, and the selected point's tangent handle drags, with the mouse")
	{
		FTestLogCapture Log;
		FViewFixture F;
		if (!F.Create(ERHIBackend::D3D12, EMapViewType::Top))
		{
			MESSAGE("D3D12 device unavailable; skipped");
			return;
		}
		FMapLandscapeMode Mode;
		Mode.SetDocument(F.Doc.get());
		Mode.NewQuadsX = 32;
		Mode.NewQuadsY = 32;
		Object* Land = Mode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f));
		REQUIRE(Land);
		F.Doc->ClearSelection();
		F.Client->ResetCamera();
		Mode.Tool = ELandscapeTool::Splines;
		F.Settings.EditorMode = &Mode;
		F.Render();
		const TSharedRef<SWindow> Window = ToSharedRef(F.Window);
		const auto Click = [&](const Vector3d& At, bool bCtrl)
		{
			F.T.SetModifiers(false, bCtrl);
			F.T.MouseMove(Window, F.At(At));
			F.T.MouseDown(Window, F.At(At));
			F.T.MouseUp(Window, F.At(At));
			F.T.Tick();
			F.T.SetModifiers(false, false);
		};
		// One road across, then a second from the north ending on its middle point.
		Click(Vector3d(-200.0f, 0.0f, 0.0f), true);
		Click(Vector3d(0.0f, 0.0f, 0.0f), true);
		Click(Vector3d(200.0f, 0.0f, 0.0f), true);
		Click(Vector3d(-150.0f, 150.0f, 0.0f), false); // nothing there: nothing selected
		CHECK(!Mode.HasSelectedSplinePoint());
		Click(Vector3d(0.0f, 200.0f, 0.0f), true);
		Click(Vector3d(0.0f, 0.0f, 0.0f), true); // on the first road's point
		const std::vector<LandscapeSpline>& Splines = Land->GetLandscape().GetSplines();
		REQUIRE(Splines.size() == 2);
		REQUIRE(Splines[1].points.size() == 2);
		CHECK(Splines[1].points[1].joint >= 0);
		CHECK(Splines[0].points[1].joint == Splines[1].points[1].joint);

		// The first road's end point picked; its handle dragged sideways.
		Click(Vector3d(200.0f, 0.0f, 0.0f), false);
		REQUIRE(Mode.HasSelectedSplinePoint());
		Vector3d Before, After;
		REQUIRE(Mode.GetTangentHandles(Before, After));
		F.Render();
		F.T.MouseMove(Window, F.At(After));
		F.T.MouseDown(Window, F.At(After));
		F.T.Tick();
		const Vector3d Target(After.GetX(), After.GetY() + 120.0f, 0.0f);
		F.T.MouseMove(Window, F.At(Target));
		F.T.MouseUp(Window, F.At(Target));
		F.T.Tick();
		const LandscapeSplinePoint& End = Land->GetLandscape().GetSplines()[0].points[2];
		CHECK(!End.autoTangent);
		CHECK(End.tangent.GetY() > 250.0f); // three times the handle's move
		CHECK(F.Doc->GetUndoDescription() == "Change Spline Tangent");
		F.Settings.EditorMode = nullptr;
		CHECK(Log.Problems.empty());
	}
}

TEST_SUITE("MapPackages")
{
	TEST_CASE("Content Browser imports native packages with their sidecars and hides payload files")
	{
		EnsureMapCore(); FTempFolder Temp; FString Error;
		auto Doc = FMapDocument::CreateNew();
		REQUIRE(Doc->AddLandscape(4, 4, 16, Vector3d()));
		const auto Source = Temp.File("Source.gmap");
		REQUIRE(Doc->Save(Source, Error));
		const auto TargetDir = Temp.File("Imported");
		std::filesystem::create_directory(std::filesystem::path(TargetDir.ToWide()));
		FUITestApp App;
		const auto Browser = SNew(SContentBrowser)
			.Roots(TArray<FContentBrowserRoot>{ { "Content", TargetDir } }).InitialPath(TargetDir);
		App.AddWindow(Browser);
		REQUIRE(Browser->ImportFile(Source));
		App.Tick();
		const auto Imported = FString::FromWide((std::filesystem::path(TargetDir.ToWide()) / "Source.gmap").wstring());
		auto Back = FMapDocument::Open(Imported, Error);
		REQUIRE_MESSAGE(Back, *Error);
		CHECK(Back->CountMeshes() == 1);
		FAssetPackage OriginalPackage, CopiedPackage;
		REQUIRE(FAssetPackageStore::Load(Source, OriginalPackage, Error));
		REQUIRE(FAssetPackageStore::Load(Imported, CopiedPackage, Error));
		CHECK(OriginalPackage.Id != CopiedPackage.Id);
		CHECK(Browser->GetItems().size() == 1);
		CHECK(Browser->GetAssetType(Imported) == EContentAssetType::Map);
	}

 TEST_CASE("Native worlds preserve brushes actors landscape layers splines and foliage")
 {
  EnsureMapCore();
  FTempFolder Temp;
  auto Doc = FMapDocument::CreateNew();
  REQUIRE(Doc->AddPrefab(PrefabPath("Cube"), Vector3d(40, 80, 120)));
  REQUIRE(Doc->AddEntity("light", Vector3d(100, 200, 300)));
  auto* Mesh = Doc->AddMesh("Sphere", Vector3d(3, 4, 5));
  REQUIRE(Mesh);
  Mesh->SetScale(Vector3d(2, 3, 4));
  Mesh->SetOrientation(SphrVector(0.2f, 0.3f, 0.4f));
  auto* Terrain = Doc->AddLandscape(8, 12, 32, Vector3d(10, 20, 30));
  REQUIRE(Terrain);
  auto Heights = Terrain->GetLandscape().GetHeights();
  for (size_t I = 0; I < Heights.size(); ++I) Heights[I] = static_cast<float>(I) * 0.123456f;
  auto Layer = Terrain->GetLandscape().MakeLayer("Grass", 10, 20, 30);
  Layer.texture = "builtin:Grass";
  Layer.tiling = 192;
  for (size_t I = 0; I < Layer.weights.size(); ++I) Layer.weights[I] = static_cast<float>(I % 5) / 5;
  LandscapeSpline Road;
  Road.mesh = "Road";
  LandscapeSplinePoint Point;
  Point.position = Vector3d(100, 20, 30); Point.autoTangent = false;
  Point.tangent = Vector3d(30, 40, 50); Point.joint = 7;
  Road.points.push_back(Point);
  Point.position = Vector3d(300, 100, 40); Point.autoTangent = true; Point.joint = -1;
  Road.points.push_back(Point);
  Doc->SetLandscape(*Terrain, Landscape::Create(8, 12, 32, Heights, {Layer}, {Road}));
  FoliageType Type; Type.name = "Tree"; Type.mesh = "Tree"; Type.alignToNormal = true; Type.density = 15;
  FoliageInstance Instance; Instance.position = Vector3d(90, 80, 70); Instance.yaw = 0.6f; Instance.scale = 1.1f;
  REQUIRE(Doc->AddFoliage(std::make_shared<Foliage>(std::vector<FoliageType>{Type}, std::vector<FoliageInstance>{Instance})));
  FString Error;
  const auto File = Temp.File("World.gmap");
  REQUIRE_MESSAGE(Doc->Save(File, Error), *Error);
  CHECK_FALSE(Doc->IsModified());
  auto Back = FMapDocument::Open(File, Error);
  REQUIRE_MESSAGE(Back, *Error);
  CHECK_FALSE(Back->IsModified());
  CHECK(Back->CountBrushes() == Doc->CountBrushes());
  CHECK(Back->CountEntities() == Doc->CountEntities());
  CHECK(Back->CountMeshes() == Doc->CountMeshes());
  bool FoundLand = false, FoundFoliage = false, FoundMesh = false;
  for (Object* Obj : Back->GetObjects())
  {
   if (Obj->HasLandscape())
   {
    FoundLand = true;
    const auto& L = Obj->GetLandscape();
    CHECK(L.GetHeights() == Heights);
    REQUIRE(L.GetLayers().size() == 1);
    CHECK(L.GetLayers()[0].weights == Layer.weights);
    CHECK(L.GetLayers()[0].texture == Layer.texture);
    CHECK(L.GetLayers()[0].tiling == Layer.tiling);
    REQUIRE(L.GetSplines().size() == 1);
    CHECK(L.GetSplines()[0].mesh == "Road");
    CHECK(L.GetSplines()[0].points[0].joint == 7);
    CHECK_FALSE(L.GetSplines()[0].points[0].autoTangent);
    CHECK(Same(L.GetSplines()[0].points[0].tangent, Vector3d(30,40,50)));
    CHECK(Same(Obj->GetPosition(), Terrain->GetPosition()));
   }
   else if (Obj->HasMesh())
   {
    FoundMesh = true;
    CHECK(Same(Obj->GetScale(), Mesh->GetScale()));
    CHECK(Obj->GetOrientation().GetRoll() == doctest::Approx(0.4f));
   }
   if (Obj->HasFoliage())
   {
    FoundFoliage = true;
    REQUIRE(Obj->GetFoliage().GetInstances().size() == 1);
    CHECK(Obj->GetFoliage().GetTypes()[0] == Type);
    CHECK(Same(Obj->GetFoliage().GetInstances()[0].position, Instance.position));
   }
  }
  CHECK(FoundLand); CHECK(FoundMesh); CHECK(FoundFoliage);
  FAssetPackage Package;
  REQUIRE(FAssetPackageStore::Load(File, Package, Error));
  const auto Id = Package.Id;
  REQUIRE(Back->Save(File, Error));
  REQUIRE(FAssetPackageStore::Load(File, Package, Error));
  CHECK(Package.Id == Id);
  REQUIRE(Back->Save(Temp.File("Copy.gmap"), Error));
  REQUIRE(FAssetPackageStore::Load(Temp.File("Copy.gmap"), Package, Error));
  CHECK(Package.Id != Id);
 }

 TEST_CASE("Native prefab assets can be placed and legacy maps choose a native save path")
 {
  EnsureMapCore(); FTempFolder Temp; FString Error;
  auto Doc = FMapDocument::CreateNew();
  auto* Mesh = Doc->AddMesh("Cube", Vector3d(10,20,30));
  REQUIRE(Mesh);
  const auto Asset = Temp.File("Building.asset");
  REQUIRE_MESSAGE(Doc->SavePrefab({Mesh}, Asset, Error), *Error);
  auto* Placed = Doc->AddPrefab(Asset, Vector3d(100,200,300), &Error);
  REQUIRE_MESSAGE(Placed, *Error);
  REQUIRE(Placed->GetNumChildren() == 1);
  CHECK(Placed->GetChild(0).HasMesh());
  CHECK(Same(Placed->GetPosition(), Vector3d(100,200,300)));
  REQUIRE(Doc->Save(Temp.File("Old.qle"), Error));
  auto Legacy = FMapDocument::Open(Temp.File("Old.qle"), Error);
  REQUIRE(Legacy);
  CHECK(Legacy->GetPath() == Temp.File("Old.gmap"));
  CHECK(Legacy->IsModified());
  REQUIRE(Legacy->Save(Legacy->GetPath(), Error));
  CHECK(FFile::Exists(Temp.File("Old.qle")));
  auto SecondImport = FMapDocument::Open(Temp.File("Old.qle"), Error);
  REQUIRE(SecondImport);
  CHECK(SecondImport->GetPath().IsEmpty()); // Save As protects the existing native map
 }

 TEST_CASE("World schema rejects invalid bulk references and leaf hierarchies")
 {
  EnsureMapCore(); FTempFolder Temp; FString Error;
  auto Doc = FMapDocument::CreateNew();
  REQUIRE(Doc->AddLandscape(4,4,16,Vector3d()));
  const auto File = Temp.File("World.gmap");
  REQUIRE(Doc->Save(File, Error));
  FAssetPackage Package;
  REQUIRE(FAssetPackageStore::Load(File, Package, Error));
  Package.Exports["Root"]["Children"][0]["Landscape"]["Heights"]["Offset"] = 1000000;
  REQUIRE(FAssetPackageStore::Save(File, Package, Error));
  CHECK_FALSE(FMapDocument::Open(File, Error));
  CHECK(Error.Contains("bulk range"));
 }
}
