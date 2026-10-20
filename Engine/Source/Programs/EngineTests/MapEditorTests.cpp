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
		TUniquePtr<FSlateRHIRenderingPolicy> Renderer;
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
			CHECK(Mesh->GetEdgeList().NumOfElm() > 0);
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
		CHECK(Field.GetMesh()->GetEdgeList().NumOfElm() == 4 * 3 + 5 * 2);

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
