// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapTestApp.h"

#include "Widgets/SViewport.h"

bool FMapTestApp::StartServices()
{
	FMapEditorEnvironment::FStartupOptions Options;
	Options.EditorContentDir = FPaths::Combine(FPaths::FindContentDir(), "Editor");
	bEnvironmentStarted = FMapEditorEnvironment::Startup(Options);
	FString Error;
	if (!Expect(bEnvironmentStarted && FMapEditorEnvironment::Activate(FMapEditorEnvironment::GetBuiltInGameSet(), &Error),
		FString::Format("the map core starts with the Genesis game{}", Error.IsEmpty() ? FString() : FString(": ") + Error)))
	{
		return false;
	}
	ViewSettings.OnCursorText = [this](const FString& Text) { CursorText = Text; };
	ViewSettings.OnMessage = [this](const FString& Text) { ToolMessage = Text; SetLastEvent(Text); };
	RenderResources = MakeUnique<FMapRenderResources>(GetDevice());
	SetDocument(FMapDocument::CreateNew());
	return true;
}

void FMapTestApp::OnShutdown()
{
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		View->SetDocument(nullptr);
	}
	Views.clear();
	Document.reset();
	RenderResources.reset();
	if (bEnvironmentStarted)
	{
		FMapEditorEnvironment::Shutdown();
	}
}

void FMapTestApp::SetDocument(TUniquePtr<FMapDocument> NewDocument)
{
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		View->SetDocument(nullptr);
	}
	Document = std::move(NewDocument);
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		View->SetDocument(Document.get());
	}
}

TSharedRef<FMapViewportClient> FMapTestApp::CreateView(EMapViewType Type)
{
	auto View = std::make_shared<FMapViewportClient>(GetDevice(), GetRenderer(), *RenderResources, ViewSettings, Type);
	GetWindowRenderer().AddViewportClient(ToSharedRef(StaticCastSharedPtr<FRenderTargetViewportClient>(View)));
	View->SetDocument(Document.get());
	Views.push_back(View);
	return TSharedRef<FMapViewportClient>(View);
}

TSharedRef<SViewport> FMapTestApp::MakeViewport(const TSharedRef<FMapViewportClient>& View) const
{
	return SNew(SViewport).ViewportClient(StaticCastSharedPtr<IViewportClient>(View.ToSharedPtr()));
}

FString FMapTestApp::FindPrefab(const char* Name)
{
	for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
	{
		if (Prefab.Name == Name)
		{
			return Prefab.Path;
		}
	}
	Expect(false, FString::Format("the prefab {} exists", Name));
	return FString();
}

Object* FMapTestApp::FindByClass(const char* ClassName) const
{
	for (Object* Obj : Document->GetObjects())
	{
		if (Obj->HasEntity() && !stricmp(Obj->GetEntityPtr()->GetClassname(), ClassName))
		{
			return Obj;
		}
	}
	return nullptr;
}

FVector2 FMapTestApp::MapToWindow(const FMapViewportClient& View, const Vector3d& Point) const
{
	const TSharedPtr<SViewport> Widget = View.GetViewport();
	FVector2 Local;
	if (!Widget || !View.MapToScreen(Point, Local))
	{
		return FVector2(-1.0f, -1.0f);
	}
	return Widget->GetCachedGeometry().LocalToAbsolute(Local);
}

void FMapTestApp::GetSelectionBounds(Vector3d& OutMin, Vector3d& OutMax) const
{
	if (!Document->GetSelectionBounds(OutMin, OutMax))
	{
		OutMin = OutMax = Vector3d(0.0f, 0.0f, 0.0f);
	}
}

void FMapTestApp::Report(const char* What) const
{
	Vector3d Min, Max;
	const bool bAny = Document->GetSelectionBounds(Min, Max);
	UE_LOG(LogTestApp, Display, "{}: {} brushes, {} entities, {} selected{}", What, Document->CountBrushes(), Document->CountEntities(), Document->GetSelection().size(),
		bAny ? FString::Format(" at ({:.0f} {:.0f} {:.0f})-({:.0f} {:.0f} {:.0f})", Min.GetX(), Min.GetY(), Min.GetZ(), Max.GetX(), Max.GetY(), Max.GetZ()) : FString());
}

void FMapTestApp::BuildScene()
{
	FMapDocument& Doc = *Document;
	Doc.AddPrefab(FindPrefab("Plate"), Vector3d(0.0f, 0.0f, -8.0f));
	Cube = Doc.AddPrefab(FindPrefab("Cube"), Vector3d(-160.0f, -96.0f, 32.0f));
	Column = Doc.AddPrefab(FindPrefab("Column"), Vector3d(160.0f, 96.0f, 64.0f));
	Sphere = Doc.AddPrefab(FindPrefab("Sphere"), Vector3d(-128.0f, 160.0f, 64.0f));
	Pyramid = Doc.AddPrefab(FindPrefab("Pyramid"), Vector3d(176.0f, -144.0f, 32.0f));
	Doc.AddEntity("info_player_start", Vector3d(-32.0f, -192.0f, 32.0f));
	Doc.AddEntity("light", Vector3d(32.0f, 32.0f, 192.0f));
	Doc.ClearSelection();
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		View->FrameSelection();
	}
	Report("Scene built");
}
