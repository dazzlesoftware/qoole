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
#include "MapEditorEnvironment.h"
#include "MapObjectProperties.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "MapViewportClient.h"
#include "UIRenderer.h"
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
		CHECK(!Reopened->IsModified());

		// Export: the document keeps its path; the .map imports.
		const FString Map = Temp.File("test.map");
		REQUIRE(Doc->Save(Map, Error));
		CHECK(Doc->GetPath() == Qle);
		TUniquePtr<FMapDocument> Imported = FMapDocument::Open(Map, Error);
		REQUIRE(Imported);
		CHECK(Imported->CountBrushes() == 2);
		CHECK(Imported->CountEntities() == 1);
		CHECK(Imported->GetPath() == Temp.File("test.qle")); // saves as .qle next to it
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
		TUniquePtr<FUIRenderer> Renderer;
		TUniquePtr<FMapRenderResources> Resources;
		FMapViewSettings Settings;
		TUniquePtr<FMapDocument> Doc;
		FUITestApp T;
		TSharedPtr<FMapViewportClient> Client;
		TSharedPtr<SWindow> Window;

		bool Create(ERHIBackend Backend, EMapViewType Type)
		{
			EnsureMapCore();
			FRHIDeviceDesc Desc;
			Desc.Backend = Backend;
			Desc.bSoftwareAdapter = true;
			Device = FRHIDevice::Create(Desc);
			if (!Device)
			{
				return false;
			}
			Renderer = MakeUnique<FUIRenderer>(*Device, *FUITestStyle::GetFontCache());
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
			auto* Target = static_cast<FUIRenderTarget*>(Client->GetRenderTarget().get());
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
		F.Client->SetRenderMode(EMapRenderMode::Solid);
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
