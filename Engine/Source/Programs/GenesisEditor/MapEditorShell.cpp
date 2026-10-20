// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapEditorShell.h"

#include "Application/UIApplication.h"
#include "Framework/MultiBox/MenuRegistry.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "MapCoreHeaders.h"
#include "Misc/App.h"
#include "Serialization/Json.h"
#include "Services/FileDialogs.h"
#include "Services/MessageDialog.h"
#include "Services/Services.h"
#include "UIWindowRenderer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>

DECLARE_LOG_CATEGORY_EXTERN(LogGenesisEditor, Log);

namespace
{
	const FTextBlockStyle& SmallText() { return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"); }

	const TArray<FFileTypeFilter>& MapFilters()
	{
		static const TArray<FFileTypeFilter> Filters = {
			{ "Maps (*.qle, *.map)", "*.qle;*.map" },
			{ "Genesis maps (*.qle)", "*.qle" },
			{ "Quake maps (*.map)", "*.map" },
		};
		return Filters;
	}
}

FMapEditorShell::FMapEditorShell(FHost InHost)
	: Host(std::move(InHost))
{
	Commands = std::make_shared<FUICommandList>();
	TabManager = FTabManager::Create().ToSharedPtr();
	RenderResources = MakeUnique<FMapRenderResources>(*Host.Device);
	ViewSettings.OnCursorText = [this](const FString& Text) { CursorText = Text; };
	ViewSettings.OnMessage = [this](const FString& Text) { StatusText = Text; };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Views[Index] = std::make_shared<FMapViewportClient>(*Host.Device, *Host.Renderer, *RenderResources, ViewSettings, static_cast<EMapViewType>(Index));
		Host.WindowRenderer->AddViewportClient(ToSharedRef(StaticCastSharedPtr<FRenderTargetViewportClient>(Views[Index])));
	}
	Views[3]->SetRenderMode(EMapRenderMode::Solid);
	LoadEditorSettings();
	if (!Host.SettingsDir.IsEmpty())
	{
		const std::filesystem::path Folder = std::filesystem::path(Host.SettingsDir.ToWide()) / L"Prefabs";
		std::error_code Error;
		std::filesystem::create_directories(Folder, Error);
		FMapEditorEnvironment::SetUserPrefabFolder(FString::FromWide(Folder.wstring()));
	}
	RegisterCommands();
	RegisterMenus();
	RegisterTabs();
}

FMapEditorShell::~FMapEditorShell()
{
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		View->SetDocument(nullptr);
	}
	if (Document)
	{
		Document->OnChanged.Remove(DocumentHandle);
	}
	Properties.reset();
	Document.reset();
	Clipboard.clear();
}

void FMapEditorShell::Log(const FString& Line)
{
	LogLines.push_back(Line);
	StatusText = Line;
	GE_LOG(LogGenesisEditor, Display, "{}", Line);
}

// ----- Settings -----

void FMapEditorShell::LoadEditorSettings()
{
	IConfigStore& Config = FServices::GetConfig();
	FString Text;
	if (Config.GetString("MapEditor", "GridSize", Text))
	{
		ViewSettings.GridSize = FMath::Clamp(static_cast<float>(std::atof(Text.ToAnsi().c_str())), FMapViewSettings::MinGridSize, FMapViewSettings::MaxGridSize);
	}
	if (Config.GetString("MapEditor", "RotateSnap", Text))
	{
		ViewSettings.RotateSnapDegrees = FMath::Clamp(static_cast<float>(std::atof(Text.ToAnsi().c_str())), 0.0f, 90.0f);
	}
	int32 Value = 0;
	if (Config.GetInt("MapEditor", "SnapToGrid", Value)) ViewSettings.bSnapToGrid = Value != 0;
	if (Config.GetInt("MapEditor", "ShowGrid", Value)) ViewSettings.bShowGrid = Value != 0;
	if (Config.GetInt("MapEditor", "Solid3D", Value)) Views[3]->SetRenderMode(Value ? EMapRenderMode::Solid : EMapRenderMode::Wireframe);
}

void FMapEditorShell::SaveEditorSettings() const
{
	IConfigStore& Config = FServices::GetConfig();
	Config.SetString("MapEditor", "GridSize", FString::Format("{}", ViewSettings.GridSize));
	Config.SetString("MapEditor", "RotateSnap", FString::Format("{}", ViewSettings.RotateSnapDegrees));
	Config.SetInt("MapEditor", "SnapToGrid", ViewSettings.bSnapToGrid ? 1 : 0);
	Config.SetInt("MapEditor", "ShowGrid", ViewSettings.bShowGrid ? 1 : 0);
	Config.SetInt("MapEditor", "Solid3D", Views[3]->GetRenderMode() == EMapRenderMode::Solid ? 1 : 0);
}

// ----- Documents -----

void FMapEditorShell::SetDocument(TUniquePtr<FMapDocument> NewDocument)
{
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		View->SetDocument(nullptr);
	}
	if (Document)
	{
		Document->OnChanged.Remove(DocumentHandle);
	}
	if (Properties)
	{
		Properties->SetObjects({});
	}
	Document = std::move(NewDocument);
	if (Document)
	{
		DocumentHandle = Document->OnChanged.AddLambda([this](EMapChange Change) { OnDocumentChanged(Change); });
		for (const TSharedPtr<FMapViewportClient>& View : Views)
		{
			View->SetDocument(Document.get());
		}
		for (const TSharedPtr<FMapViewportClient>& View : Views)
		{
			View->FrameSelection();
		}
	}
	RefreshOutliner();
	RefreshProperties();
	UpdateTitle();
}

void FMapEditorShell::OnDocumentChanged(EMapChange Change)
{
	if (HasAnyChange(Change, EMapChange::Structure | EMapChange::Selection | EMapChange::Scope | EMapChange::Geometry))
	{
		RefreshOutliner();
		RefreshProperties();
	}
	if (HasAnyChange(Change, EMapChange::File))
	{
		UpdateTitle();
	}
}

void FMapEditorShell::UpdateTitle()
{
	const TSharedPtr<SWindow> Window = MainWindow.lock();
	if (!Window)
	{
		return;
	}
	if (!Document)
	{
		Window->SetTitle("Genesis Editor");
		return;
	}
	Window->SetTitle(FString::Format("{}{} - Genesis Editor", Document->GetDisplayName(), Document->IsModified() ? " *" : ""));
}

bool FMapEditorShell::ConfirmDiscard()
{
	if (!Document || !Document->IsModified())
	{
		return true;
	}
	const EMessageResult Answer = FServices::GetMessageDialog().Show(EMessageType::Question, EMessageButtons::YesNoCancel,
		FString::Format("Save the changes to {}?", Document->GetDisplayName()));
	if (Answer == EMessageResult::Cancel)
	{
		return false;
	}
	if (Answer == EMessageResult::Yes)
	{
		FString Path = Document->GetPath();
		if (Path.IsEmpty())
		{
			FFileDialogOptions Options;
			Options.Title = "Save map";
			Options.Filters = { { "Genesis maps (*.qle)", "*.qle" } };
			Options.DefaultExtension = "qle";
			Options.InitialPath = DefaultFolder();
			if (!FServices::GetFileDialogs().SaveFile(Options, Path))
			{
				return false;
			}
		}
		return SaveMap(Path);
	}
	return true;
}

void FMapEditorShell::NewMap(const FMapGameSet& GameSet)
{
	FString Error;
	if (FMapEditorEnvironment::GetActiveGameSet() != &GameSet && !FMapEditorEnvironment::Activate(GameSet, &Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return;
	}
	SetDocument(FMapDocument::CreateNew());
	Log("New map");
}

bool FMapEditorShell::OpenMap(const FString& Path)
{
	FString Error;
	TUniquePtr<FMapDocument> Opened = FMapDocument::Open(Path, Error);
	if (!Opened)
	{
		Log(FString::Format("Could not open {}", Path));
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return false;
	}
	SetDocument(std::move(Opened));
	Log(FString::Format("Opened {}: {} brushes, {} entities", Path, Document->CountBrushes(), Document->CountEntities()));
	FServices::GetConfig().SetString("MapEditor", "LastFolder", FString::FromWide(std::filesystem::path(Path.ToWide()).parent_path().wstring()));
	return true;
}

bool FMapEditorShell::SaveMap(const FString& Path)
{
	FString Error;
	if (!Document || !Document->Save(Path, Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return false;
	}
	Log(FString::Format("Saved {}", Path));
	FServices::GetConfig().SetString("MapEditor", "LastFolder", FString::FromWide(std::filesystem::path(Path.ToWide()).parent_path().wstring()));
	return true;
}

bool FMapEditorShell::ExportMap(const FString& Path)
{
	FString Error;
	if (!Document || !Document->Save(Path, Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return false;
	}
	Log(FString::Format("Exported {}", Path));
	return true;
}

FString FMapEditorShell::DefaultFolder() const
{
	if (Document && !Document->GetPath().IsEmpty())
	{
		return Document->GetPath();
	}
	FString Folder;
	FServices::GetConfig().GetString("MapEditor", "LastFolder", Folder);
	return Folder;
}

// ----- Views -----

TSharedPtr<FMapViewportClient> FMapEditorShell::GetActiveView() const
{
	if (const TSharedPtr<SDockTab> Active = TabManager->GetActiveTab())
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (ViewTabs[Index].lock() == Active)
			{
				return Views[Index];
			}
		}
	}
	return Views[LastViewIndex];
}

Vector3d FMapEditorShell::SnappedPlacement() const
{
	Vector3d Point = GetActiveView()->GetPlacementPoint();
	if (ViewSettings.bSnapToGrid)
	{
		const float Grid = ViewSettings.GridSize;
		Point.NewVector(std::round(Point.GetX() / Grid) * Grid, std::round(Point.GetY() / Grid) * Grid, std::round(Point.GetZ() / Grid) * Grid);
	}
	return Point;
}

Vector3d FMapEditorShell::SelectionCenter() const
{
	Vector3d Min, Max;
	if (!Document || !Document->GetSelectionBounds(Min, Max))
	{
		return Vector3d(0.0f, 0.0f, 0.0f);
	}
	return Min.AddVector(Max).MultVector(0.5f);
}

Object* FMapEditorShell::AddPrefab(const FString& Path)
{
	if (!Document)
	{
		return nullptr;
	}
	FString Error;
	Object* Added = Document->AddPrefab(Path, SnappedPlacement(), &Error);
	if (!Added)
	{
		Log(Error);
	}
	return Added;
}

Object* FMapEditorShell::AddEntity(const FString& ClassName)
{
	return Document ? Document->AddEntity(ClassName, SnappedPlacement()) : nullptr;
}

void FMapEditorShell::SetGridSize(float Size)
{
	ViewSettings.GridSize = FMath::Clamp(Size, FMapViewSettings::MinGridSize, FMapViewSettings::MaxGridSize);
	ViewSettings.Changed();
	Log(FString::Format("Grid size {:.0f}", ViewSettings.GridSize));
}

void FMapEditorShell::SetTool(EMapTool Tool)
{
	ViewSettings.Tool = Tool;
	ViewSettings.Changed();
	StatusText = FString::Format("Tool: {}", ToString(Tool));
}

void FMapEditorShell::SaveSelectionAsPrefab()
{
	if (!Document || Document->GetSelection().empty())
	{
		return;
	}
	FFileDialogOptions Options;
	Options.Title = "Save the selection as a prefab";
	Options.Filters = { { "Prefabs (*.qle)", "*.qle" } };
	Options.DefaultExtension = "qle";
	Options.InitialPath = FMapEditorEnvironment::GetUserPrefabFolder();
	FString Path;
	if (!FServices::GetFileDialogs().SaveFile(Options, Path))
	{
		return;
	}
	FString Error;
	if (!Document->SavePrefab(Document->GetSelection(), Path, Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return;
	}
	FMapEditorEnvironment::RefreshPrefabs();
	RefreshPrefabList();
	Log(FString::Format("Saved prefab {}", Path));
}

// ----- Commands -----

void FMapEditorShell::RegisterCommands()
{
	auto Context = MakeShared<FBindingContext>(FName("MapEditor"), "Map editor");
	using T = EUserInterfaceActionType;
	using M = EModifierKey;
	const auto Add = [&](const char* Name, const char* Label, const char* Tip, T Type = T::Button, FInputChord Chord = FInputChord())
	{
		return Context->AddCommand(Name, Label, Tip, Type, Chord).ToSharedPtr();
	};
	Cmd.NewMap = Add("NewMap", "New", "Start a new map", T::Button, FInputChord(EKey::N, M::Control));
	Cmd.OpenMap = Add("OpenMap", "Open...", "Open a .qle map or import a .map file", T::Button, FInputChord(EKey::O, M::Control));
	Cmd.SaveMap = Add("SaveMap", "Save", "Save the map", T::Button, FInputChord(EKey::S, M::Control));
	Cmd.SaveMapAs = Add("SaveMapAs", "Save As...", "Save the map under another name", T::Button, FInputChord(EKey::S, M::Control | M::Shift));
	Cmd.ExportMap = Add("ExportMap", "Export .map...", "Write the map as a Quake .map file", T::Button, FInputChord(EKey::E, M::Control));
	Cmd.Exit = Add("Exit", "Exit", "Close the editor");

	Cmd.Undo = Add("Undo", "Undo", "Undo the last change", T::Button, FInputChord(EKey::Z, M::Control));
	Cmd.Redo = Add("Redo", "Redo", "Redo the change undone last", T::Button, FInputChord(EKey::Y, M::Control));
	Cmd.Cut = Add("Cut", "Cut", "Move the selection to the clipboard", T::Button, FInputChord(EKey::X, M::Control));
	Cmd.Copy = Add("Copy", "Copy", "Copy the selection to the clipboard", T::Button, FInputChord(EKey::C, M::Control));
	Cmd.Paste = Add("Paste", "Paste", "Add the clipboard's objects, one grid step away from where they were", T::Button, FInputChord(EKey::V, M::Control));
	Cmd.Duplicate = Add("Duplicate", "Duplicate", "Copy the selection, one grid step away", T::Button, FInputChord(EKey::D, M::Control));
	Cmd.Delete = Add("Delete", "Delete", "Delete the selection", T::Button, FInputChord(EKey::Delete));
	Cmd.SelectAll = Add("SelectAll", "Select All", "Select every object of the group being edited", T::Button, FInputChord(EKey::A, M::Control));
	Cmd.SelectNone = Add("SelectNone", "Select None", "Clear the selection", T::Button, FInputChord(EKey::Escape));

	Cmd.FrameSelection = Add("FrameSelection", "Frame Selection", "Fit every view to the selection (or the whole map)", T::Button, FInputChord(EKey::L, M::Control));
	Cmd.ResetViews = Add("ResetViews", "Reset Views", "Put every view back at the origin", T::Button, FInputChord(EKey::Home));
	Cmd.ShowGrid = Add("ShowGrid", "Grid", "Show the grid in the 2D views", T::ToggleButton, FInputChord(EKey::G, M::Shift));
	Cmd.SnapToGrid = Add("SnapToGrid", "Snap", "Snap to the grid (hold Alt while dragging to move freely)", T::ToggleButton, FInputChord(EKey::N));
	Cmd.GridSmaller = Add("GridSmaller", "Smaller Grid", "Halve the grid size", T::Button, FInputChord(EKey::LeftBracket));
	Cmd.GridLarger = Add("GridLarger", "Larger Grid", "Double the grid size", T::Button, FInputChord(EKey::RightBracket));
	Cmd.SolidView = Add("SolidView", "Solid 3D", "Draw the 3D view with solid faces instead of wireframes", T::ToggleButton, FInputChord(EKey::F3));

	Cmd.ToolSelect = Add("ToolSelect", "Select", "Select, and move the selection by dragging it", T::RadioButton, FInputChord(EKey::M));
	Cmd.ToolRotate = Add("ToolRotate", "Rotate", "Turn the selection about its center by dragging", T::RadioButton, FInputChord(EKey::R));
	Cmd.ToolScale = Add("ToolScale", "Scale", "Stretch the selection by dragging (Shift: evenly)", T::RadioButton, FInputChord(EKey::S));
	Cmd.ToolClip = Add("ToolClip", "Clip", "Draw a line to clip the selected brushes", T::RadioButton, FInputChord(EKey::C));
	Cmd.ToolVertex = Add("ToolVertex", "Vertices", "Drag the corners of the selected brush", T::RadioButton, FInputChord(EKey::V));
	Cmd.ToolEdge = Add("ToolEdge", "Edges", "Drag the edges of the selected brush", T::RadioButton, FInputChord(EKey::G));
	Cmd.ToolFace = Add("ToolFace", "Faces", "Drag the faces of the selected brush", T::RadioButton, FInputChord(EKey::F));

	Cmd.AddCube = Add("AddCube", "Add Cube", "Add a cube brush where the view looks", T::Button, FInputChord(EKey::B));
	Cmd.Group = Add("Group", "Group", "Make the selection one group", T::Button, FInputChord(EKey::G, M::Control));
	Cmd.Ungroup = Add("Ungroup", "Ungroup", "Break the selected groups up", T::Button, FInputChord(EKey::U, M::Control));
	Cmd.EnterGroup = Add("EnterGroup", "Edit Group", "Edit the inside of the selected group", T::Button, FInputChord(EKey::PageDown, M::Control));
	Cmd.ScopeUp = Add("ScopeUp", "Leave Group", "Go back to the group around this one", T::Button, FInputChord(EKey::PageUp, M::Control));
	Cmd.HollowIn = Add("HollowIn", "Hollow", "Make the selected brushes rooms: walls inside their outline", T::Button, FInputChord(EKey::Insert, M::Control));
	Cmd.HollowOut = Add("HollowOut", "Hollow Outward", "Make the selected brushes rooms: walls around their outline", T::Button, FInputChord(EKey::Delete, M::Control));
	Cmd.Subtract = Add("Subtract", "Subtract", "Cut the selected brushes out of the brushes they touch", T::Button, FInputChord(EKey::Subtract, M::Control));
	Cmd.Intersect = Add("Intersect", "Intersect", "Replace the selected brushes by their common volume", T::Button, FInputChord(EKey::Add, M::Control));
	Cmd.FlipX = Add("FlipX", "Flip X", "Mirror the selection across its center, along X");
	Cmd.FlipY = Add("FlipY", "Flip Y", "Mirror the selection across its center, along Y");
	Cmd.FlipZ = Add("FlipZ", "Flip Z", "Mirror the selection across its center, along Z");
	Cmd.RotateLeft = Add("RotateLeft", "Rotate 90° Left", "Turn the selection a quarter counterclockwise, as seen in the last 2D view used", T::Button, FInputChord(EKey::Left, M::Control));
	Cmd.RotateRight = Add("RotateRight", "Rotate 90° Right", "Turn the selection a quarter clockwise, as seen in the last 2D view used", T::Button, FInputChord(EKey::Right, M::Control));
	Cmd.SavePrefab = Add("SavePrefab", "Save as Prefab...", "Save the selection as a prefab for the Prefabs panel");

	Cmd.ResetLayout = Add("ResetLayout", "Reset Layout", "Go back to the default window layout");
	Cmd.About = Add("About", "About Genesis Editor", "Version and license");
	FInputBindingManager::Get().RegisterContext(Context);

	FUICommandList& List = *Commands;
	const auto HasDocument = [this] { return Document != nullptr; };
	const auto HasSelection = [this] { return Document && !Document->GetSelection().empty(); };
	const auto HasTwoSelected = [this] { return Document && Document->GetSelection().size() >= 2; };

	// File.
	List.MapAction(ToSharedRef(Cmd.NewMap), [this]
	{
		if (ConfirmDiscard())
		{
			NewMap(FMapEditorEnvironment::GetBuiltInGameSet());
		}
	});
	List.MapAction(ToSharedRef(Cmd.OpenMap), [this]
	{
		if (!ConfirmDiscard())
		{
			return;
		}
		FFileDialogOptions Options;
		Options.Title = "Open map";
		Options.Filters = MapFilters();
		Options.InitialPath = DefaultFolder();
		FString Path;
		if (FServices::GetFileDialogs().OpenFile(Options, Path))
		{
			OpenMap(Path);
		}
	});
	List.MapAction(ToSharedRef(Cmd.SaveMap), [this]
	{
		if (Document->GetPath().IsEmpty())
		{
			Commands->ExecuteAction(*Cmd.SaveMapAs);
			return;
		}
		SaveMap(Document->GetPath());
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.SaveMapAs), [this]
	{
		FFileDialogOptions Options;
		Options.Title = "Save map as";
		Options.Filters = { { "Genesis maps (*.qle)", "*.qle" } };
		Options.DefaultExtension = "qle";
		Options.InitialPath = DefaultFolder();
		FString Path;
		if (FServices::GetFileDialogs().SaveFile(Options, Path))
		{
			SaveMap(Path);
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.ExportMap), [this]
	{
		FFileDialogOptions Options;
		Options.Title = "Export .map";
		Options.Filters = { { "Quake maps (*.map)", "*.map" } };
		Options.DefaultExtension = "map";
		const FString Base = DefaultFolder();
		Options.InitialPath = Base.EndsWith(".qle", ESearchCase::IgnoreCase) ? Base.Left(Base.Len() - 4) + ".map" : Base;
		FString Path;
		if (FServices::GetFileDialogs().SaveFile(Options, Path))
		{
			ExportMap(Path);
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.Exit), [this] { if (ConfirmDiscard()) Host.Quit(); });

	// Edit.
	List.MapAction(ToSharedRef(Cmd.Undo), [this] { Log(FString::Format("Undo {}", Document->GetUndoDescription())); Document->Undo(); },
		[this] { return Document && Document->CanUndo(); });
	List.MapAction(ToSharedRef(Cmd.Redo), [this] { Log(FString::Format("Redo {}", Document->GetRedoDescription())); Document->Redo(); },
		[this] { return Document && Document->CanRedo(); });
	List.MapAction(ToSharedRef(Cmd.Copy), [this]
	{
		Clipboard.clear();
		for (Object* Obj : Document->GetSelection())
		{
			Clipboard.push_back(MakeUnique<Object>(*Obj));
		}
		Log(FString::Format("Copied {} object(s)", Clipboard.size()));
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Cut), [this]
	{
		Commands->ExecuteAction(*Cmd.Copy);
		Document->BeginTransaction("Cut");
		Document->DeleteObjects(Document->GetSelection());
		Document->EndTransaction();
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Paste), [this]
	{
		TArray<Object*> Copies;
		const float Step = ViewSettings.GridSize;
		for (const TUniquePtr<Object>& Obj : Clipboard)
		{
			Object* Copy = new Object(*Obj);
			Vector3d Position;
			Copy->GetPosition(Position).AddVector(Vector3d(Step, Step, 0.0f));
			Copy->SetPosition(Position);
			Copies.push_back(Copy);
		}
		Document->BeginTransaction("Paste");
		Document->AddObjects(Copies);
		Document->EndTransaction();
	}, [this] { return Document && !Clipboard.empty(); });
	List.MapAction(ToSharedRef(Cmd.Duplicate), [this]
	{
		const float Step = ViewSettings.GridSize;
		Document->DuplicateObjects(Document->GetSelection(), Vector3d(Step, Step, 0.0f));
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Delete), [this] { Document->DeleteObjects(Document->GetSelection()); }, HasSelection);
	List.MapAction(ToSharedRef(Cmd.SelectAll), [this] { Document->SelectAll(); }, HasDocument);
	List.MapAction(ToSharedRef(Cmd.SelectNone), [this] { Document->ClearSelection(); }, HasSelection);

	// View.
	List.MapAction(ToSharedRef(Cmd.FrameSelection), [this]
	{
		for (const TSharedPtr<FMapViewportClient>& View : Views)
		{
			View->FrameSelection();
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.ResetViews), [this]
	{
		for (const TSharedPtr<FMapViewportClient>& View : Views)
		{
			View->ResetCamera();
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.ShowGrid), [this] { ViewSettings.bShowGrid = !ViewSettings.bShowGrid; ViewSettings.Changed(); }, nullptr,
		[this] { return ViewSettings.bShowGrid; });
	List.MapAction(ToSharedRef(Cmd.SnapToGrid), [this] { ViewSettings.bSnapToGrid = !ViewSettings.bSnapToGrid; ViewSettings.Changed(); }, nullptr,
		[this] { return ViewSettings.bSnapToGrid; });
	List.MapAction(ToSharedRef(Cmd.GridSmaller), [this] { SetGridSize(ViewSettings.GridSize * 0.5f); },
		[this] { return ViewSettings.GridSize > FMapViewSettings::MinGridSize; });
	List.MapAction(ToSharedRef(Cmd.GridLarger), [this] { SetGridSize(ViewSettings.GridSize * 2.0f); },
		[this] { return ViewSettings.GridSize < FMapViewSettings::MaxGridSize; });
	List.MapAction(ToSharedRef(Cmd.SolidView), [this]
	{
		FMapViewportClient& View = *Views[static_cast<int32>(EMapViewType::Perspective)];
		View.SetRenderMode(View.GetRenderMode() == EMapRenderMode::Solid ? EMapRenderMode::Wireframe : EMapRenderMode::Solid);
	}, nullptr, [this] { return Views[static_cast<int32>(EMapViewType::Perspective)]->GetRenderMode() == EMapRenderMode::Solid; });

	// Tools.
	const TPair<TSharedPtr<FUICommandInfo>, EMapTool> Tools[] = {
		{ Cmd.ToolSelect, EMapTool::Select }, { Cmd.ToolRotate, EMapTool::Rotate }, { Cmd.ToolScale, EMapTool::Scale },
		{ Cmd.ToolClip, EMapTool::Clip }, { Cmd.ToolVertex, EMapTool::Vertex }, { Cmd.ToolEdge, EMapTool::Edge }, { Cmd.ToolFace, EMapTool::Face },
	};
	for (const auto& [Command, Tool] : Tools)
	{
		const EMapTool ThisTool = Tool;
		List.MapAction(ToSharedRef(Command), [this, ThisTool] { SetTool(ThisTool); }, nullptr, [this, ThisTool] { return ViewSettings.Tool == ThisTool; });
	}

	// Object.
	List.MapAction(ToSharedRef(Cmd.AddCube), [this]
	{
		for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
		{
			if (Prefab.Name == "Cube" && !Prefab.bUser)
			{
				AddPrefab(Prefab.Path);
				return;
			}
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.Group), [this] { Document->GroupObjects(Document->GetSelection()); }, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Ungroup), [this] { Document->UngroupObjects(Document->GetSelection()); }, [this]
	{
		if (!Document)
		{
			return false;
		}
		for (Object* Obj : Document->GetSelection())
		{
			if (FMapDocument::IsGroup(*Obj))
			{
				return true;
			}
		}
		return false;
	});
	List.MapAction(ToSharedRef(Cmd.EnterGroup), [this] { Document->SetScope(*Document->GetSelection()[0]); }, [this]
	{
		return Document && Document->GetSelection().size() == 1 && FMapDocument::IsGroup(*Document->GetSelection()[0]);
	});
	List.MapAction(ToSharedRef(Cmd.ScopeUp), [this] { Document->ScopeUp(); }, [this] { return Document && Document->CanScopeUp(); });
	List.MapAction(ToSharedRef(Cmd.HollowIn), [this]
	{
		if (!Document->HollowObjects(Document->GetSelection(), ViewSettings.GridSize, true))
		{
			Log("Hollow: the brushes are too small for walls as thick as the grid");
		}
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.HollowOut), [this]
	{
		if (!Document->HollowObjects(Document->GetSelection(), ViewSettings.GridSize, false))
		{
			Log("Hollow: nothing to hollow in the selection");
		}
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Subtract), [this]
	{
		if (!Document->SubtractSelection())
		{
			Log("Subtract: the selection does not touch other brushes");
		}
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Intersect), [this]
	{
		if (!Document->IntersectSelection())
		{
			Log("Intersect: the selected brushes have nothing in common");
		}
	}, HasTwoSelected);
	const TPair<TSharedPtr<FUICommandInfo>, int32> Flips[] = { { Cmd.FlipX, 0 }, { Cmd.FlipY, 1 }, { Cmd.FlipZ, 2 } };
	for (const auto& [Command, Axis] : Flips)
	{
		const int32 ThisAxis = Axis;
		List.MapAction(ToSharedRef(Command), [this, ThisAxis] { Document->FlipObjects(Document->GetSelection(), ThisAxis, SelectionCenter()); }, HasSelection);
	}
	const auto RotateQuarter = [this](float Degrees)
	{
		// About the depth axis of the last 2D view used (the top view's by default).
		TSharedPtr<FMapViewportClient> View = Views[LastViewIndex];
		if (View->IsPerspective())
		{
			View = Views[static_cast<int32>(EMapViewType::Top)];
		}
		const EMapViewType Type = View->GetType();
		const int32 Axis = Type == EMapViewType::Top ? 2 : (Type == EMapViewType::Front ? 1 : 0);
		Document->RotateObjects(Document->GetSelection(), Axis, Degrees, SelectionCenter());
	};
	List.MapAction(ToSharedRef(Cmd.RotateLeft), [RotateQuarter] { RotateQuarter(90.0f); }, HasSelection);
	List.MapAction(ToSharedRef(Cmd.RotateRight), [RotateQuarter] { RotateQuarter(-90.0f); }, HasSelection);
	List.MapAction(ToSharedRef(Cmd.SavePrefab), [this] { SaveSelectionAsPrefab(); }, HasSelection);

	// Window, Help.
	List.MapAction(ToSharedRef(Cmd.ResetLayout), [this] { ResetLayout(); });
	List.MapAction(ToSharedRef(Cmd.About), [this] { ShowAbout(); });

	if (!Host.SettingsDir.IsEmpty())
	{
		FString Text;
		if (FFileHelper::LoadFileToString(Text, Host.SettingsDir + "Bindings.json"))
		{
			if (const TOptional<FJsonValue> Json = FJsonValue::Parse(Text.View()))
			{
				FInputBindingManager::Get().LoadUserBindings(*Json);
			}
		}
	}
}

void FMapEditorShell::RegisterMenus()
{
	FMenuRegistry& Registry = FMenuRegistry::Get();
	FMenuSection& Bar = Registry.RegisterMenu("MapEditor.MainMenu").AddSection("Menus");
	Bar.AddSubMenu("File", "File", "", FName("MapEditor.MainMenu.File"));
	Bar.AddSubMenu("Edit", "Edit", "", FName("MapEditor.MainMenu.Edit"));
	Bar.AddSubMenu("View", "View", "", FName("MapEditor.MainMenu.View"));
	Bar.AddSubMenu("Tools", "Tools", "", FName("MapEditor.MainMenu.Tools"));
	Bar.AddSubMenu("Object", "Object", "", FName("MapEditor.MainMenu.Object"));
	Bar.AddSubMenu("Window", "Window", "", FName("MapEditor.MainMenu.Window"));
	Bar.AddSubMenu("Help", "Help", "", FName("MapEditor.MainMenu.Help"));

	FMenuDefinition& File = Registry.RegisterMenu("MapEditor.MainMenu.File");
	FMenuSection& Open = File.AddSection("Open");
	Open.AddCommand(Cmd.NewMap);
	Open.AddCommand(Cmd.OpenMap);
	FMenuSection& Save = File.AddSection("Save");
	Save.AddCommand(Cmd.SaveMap);
	Save.AddCommand(Cmd.SaveMapAs);
	Save.AddCommand(Cmd.ExportMap);
	File.AddSection("Exit").AddCommand(Cmd.Exit);

	// Undo and redo name what they would do, so the section is built on open.
	FMenuDefinition& Edit = Registry.RegisterMenu("MapEditor.MainMenu.Edit");
	Edit.AddDynamicSection("History", [this](FMenuSection& Section, const FMenuContext&)
	{
		const bool bUndo = Document && Document->CanUndo();
		const bool bRedo = Document && Document->CanRedo();
		Section.AddCommand(Cmd.Undo, bUndo ? FString::Format("Undo {}", Document->GetUndoDescription()) : FString("Undo"));
		Section.AddCommand(Cmd.Redo, bRedo ? FString::Format("Redo {}", Document->GetRedoDescription()) : FString("Redo"));
	});
	FMenuSection& Clip = Edit.AddSection("Clipboard");
	Clip.AddCommand(Cmd.Cut);
	Clip.AddCommand(Cmd.Copy);
	Clip.AddCommand(Cmd.Paste);
	Clip.AddCommand(Cmd.Duplicate);
	Clip.AddCommand(Cmd.Delete);
	FMenuSection& Selection = Edit.AddSection("Selection");
	Selection.AddCommand(Cmd.SelectAll);
	Selection.AddCommand(Cmd.SelectNone);

	FMenuDefinition& View = Registry.RegisterMenu("MapEditor.MainMenu.View");
	FMenuSection& Camera = View.AddSection("Camera");
	Camera.AddCommand(Cmd.FrameSelection);
	Camera.AddCommand(Cmd.ResetViews);
	Camera.AddCommand(Cmd.SolidView);
	FMenuSection& Grid = View.AddSection("Grid", "Grid");
	Grid.AddCommand(Cmd.ShowGrid);
	Grid.AddCommand(Cmd.SnapToGrid);
	Grid.AddCommand(Cmd.GridSmaller);
	Grid.AddCommand(Cmd.GridLarger);
	Grid.AddSubMenu("GridSize", "Grid Size", "Map units between grid lines", [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const float Size : { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f })
		{
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, Size] { SetGridSize(Size); });
			Action.GetCheckState = TDelegate<ECheckBoxState()>::CreateLambda([this, Size]
			{
				return ViewSettings.GridSize == Size ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			});
			Section.AddEntry(FName(FString::Format("Grid{}", Size)), FString::Format("{:.0f}", Size), "", Action, EUserInterfaceActionType::RadioButton);
		}
	});
	View.AddSection("Scale").AddSubMenu("AppScale", "Interface Scale", "Scale the interface on top of the monitor's DPI", [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const float Scale : { 1.0f, 1.25f, 1.5f, 2.0f })
		{
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, Scale] { Host.SetAppScale(Scale); });
			Action.GetCheckState = TDelegate<ECheckBoxState()>::CreateLambda([Scale]
			{
				return FMath::IsNearlyEqual(FUIApplication::Get().GetApplicationScale(), Scale, 0.01f) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			});
			Section.AddEntry(FName(FString::Format("Scale{}", Scale * 100.0f)), FString::Format("{:.0f}%", Scale * 100.0f), "", Action, EUserInterfaceActionType::RadioButton);
		}
	});

	FMenuDefinition& ToolsMenu = Registry.RegisterMenu("MapEditor.MainMenu.Tools");
	FMenuSection& Edit2D = ToolsMenu.AddSection("Tools", "In the 2D views");
	for (const TSharedPtr<FUICommandInfo>& Command : { Cmd.ToolSelect, Cmd.ToolRotate, Cmd.ToolScale, Cmd.ToolClip })
	{
		Edit2D.AddCommand(Command);
	}
	FMenuSection& BrushTools = ToolsMenu.AddSection("Brush", "One brush");
	for (const TSharedPtr<FUICommandInfo>& Command : { Cmd.ToolVertex, Cmd.ToolEdge, Cmd.ToolFace })
	{
		BrushTools.AddCommand(Command);
	}

	// Prefabs and the entity classes of the current game set.
	FMenuDefinition& ObjectMenu = Registry.RegisterMenu("MapEditor.MainMenu.Object");
	FMenuSection& Add = ObjectMenu.AddSection("Add", "Add");
	Add.AddCommand(Cmd.AddCube);
	Add.AddSubMenu("AddBrush", "Add Prefab", "Add a prefab where the view looks", [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
		{
			const FString Path = Prefab.Path;
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, Path] { AddPrefab(Path); });
			Action.CanExecute = TDelegate<bool()>::CreateLambda([this] { return Document != nullptr; });
			Section.AddEntry(FName(Path), Prefab.bUser ? FString::Format("{} (yours)", Prefab.Name) : Prefab.Name, "", Action);
		}
	});
	Add.AddSubMenu("AddEntity", "Add Entity", "Add an entity where the view looks", [this](FMenuSection& Section, const FMenuContext&)
	{
		TArray<FString> Categories;
		const TArray<FMapEditorEnvironment::FEntityClass> Classes = FMapEditorEnvironment::GetPlaceableEntities();
		for (const FMapEditorEnvironment::FEntityClass& Class : Classes)
		{
			if (std::find(Categories.begin(), Categories.end(), Class.Category) == Categories.end())
			{
				Categories.push_back(Class.Category);
			}
		}
		for (const FString& Category : Categories)
		{
			Section.AddSubMenu(FName(Category), Category, "", [this, Category, Classes](FMenuSection& Sub, const FMenuContext&)
			{
				for (const FMapEditorEnvironment::FEntityClass& Class : Classes)
				{
					if (Class.Category != Category)
					{
						continue;
					}
					const FString ClassName = Class.ClassName;
					FUIAction Action;
					Action.Execute = FSimpleDelegate::CreateLambda([this, ClassName] { AddEntity(ClassName); });
					Action.CanExecute = TDelegate<bool()>::CreateLambda([this] { return Document != nullptr; });
					Sub.AddEntry(FName(ClassName), Class.Label, ClassName, Action);
				}
			});
		}
	});
	FMenuSection& Groups = ObjectMenu.AddSection("Groups", "Groups");
	Groups.AddCommand(Cmd.Group);
	Groups.AddCommand(Cmd.Ungroup);
	Groups.AddCommand(Cmd.EnterGroup);
	Groups.AddCommand(Cmd.ScopeUp);
	FMenuSection& Brushes = ObjectMenu.AddSection("Brushes", "Brushes");
	Brushes.AddCommand(Cmd.HollowIn);
	Brushes.AddCommand(Cmd.HollowOut);
	Brushes.AddCommand(Cmd.Subtract);
	Brushes.AddCommand(Cmd.Intersect);
	FMenuSection& Transform = ObjectMenu.AddSection("Transform", "Transform");
	Transform.AddCommand(Cmd.RotateLeft);
	Transform.AddCommand(Cmd.RotateRight);
	Transform.AddCommand(Cmd.FlipX);
	Transform.AddCommand(Cmd.FlipY);
	Transform.AddCommand(Cmd.FlipZ);
	ObjectMenu.AddSection("Prefab").AddCommand(Cmd.SavePrefab);

	FMenuDefinition& Window = Registry.RegisterMenu("MapEditor.MainMenu.Window");
	Window.AddDynamicSection("Tabs", [this](FMenuSection& Section, const FMenuContext&) { TabManager->PopulateTabSpawnerMenu(Section); });
	Window.AddSection("Layout").AddCommand(Cmd.ResetLayout);

	Registry.RegisterMenu("MapEditor.MainMenu.Help").AddSection("About").AddCommand(Cmd.About);

	FMenuDefinition& Bars = Registry.RegisterMenu("MapEditor.ToolBar");
	FMenuSection& FileTools = Bars.AddSection("File");
	FileTools.AddCommand(Cmd.NewMap, "New");
	FileTools.AddCommand(Cmd.OpenMap, "Open");
	FileTools.AddCommand(Cmd.SaveMap);
	FMenuSection& History = Bars.AddSection("History");
	History.AddCommand(Cmd.Undo);
	History.AddCommand(Cmd.Redo);
	FMenuSection& ToolButtons = Bars.AddSection("Tools");
	for (const TSharedPtr<FUICommandInfo>& Command : { Cmd.ToolSelect, Cmd.ToolRotate, Cmd.ToolScale, Cmd.ToolClip, Cmd.ToolVertex, Cmd.ToolEdge, Cmd.ToolFace })
	{
		ToolButtons.AddCommand(Command);
	}
	FMenuSection& ObjectTools = Bars.AddSection("Objects");
	ObjectTools.AddCommand(Cmd.AddCube);
	ObjectTools.AddCommand(Cmd.Group);
	ObjectTools.AddCommand(Cmd.Subtract);
	ObjectTools.AddCommand(Cmd.HollowIn);
	FMenuSection& ViewTools = Bars.AddSection("View");
	ViewTools.AddCommand(Cmd.SnapToGrid);
	ViewTools.AddCommand(Cmd.SolidView);
}

// ----- About -----

void FMapEditorShell::ShowAbout()
{
	const TSharedPtr<SWindow> Parent = MainWindow.lock();
	// The OK button closes the window it is in, which does not exist yet
	// when the button is made.
	auto Self = std::make_shared<TWeakPtr<SWindow>>();
	const TSharedRef<SWindow> About = SNew(SWindow)
		.Title("About Genesis Editor")
		.ClientSize(FVector2(460.0f, 220.0f))
		[
			SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Panel")).Padding(FMargin(20.0f, 16.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text("Genesis Editor").TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock).AutoWrapText(true).Text(
						"The map editor for Genesis Engine, built on Genesis UI. It descends from Qoole 99 (GX Media, Inc.) and is free software under the GNU GPL, version 2 or later.")
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock).TextStyle(&SmallText()).Text("Copyright (C) Dazzle Software, LLC.  Fonts: Noto Sans (SIL Open Font License).")
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(SButton).Text("OK").OnClicked([Self]
					{
						if (const TSharedPtr<SWindow> Window = Self->lock())
						{
							FUIApplication::Get().RequestDestroyWindow(ToSharedRef(Window));
						}
						return FReply::Handled();
					})
				]
			]
		];
	*Self = About.ToSharedPtr();
	FUIApplication::Get().AddModalWindow(About, Parent);
}

TSharedRef<SWidget> FMapEditorShell::MakeContent(const TSharedRef<SWindow>& Window)
{
	MainWindow = Window.ToSharedPtr();
	FString Saved;
	if (!Host.SettingsDir.IsEmpty())
	{
		FFileHelper::LoadFileToString(Saved, Host.SettingsDir + "Layout.json");
	}
	const TSharedRef<FDockLayout> Layout = FTabManager::LoadLayout(Saved, MakeDefaultLayout());
	DockArea = TabManager->RestoreFrom(Layout, Window.ToSharedPtr()).ToSharedPtr();
	Window->SetOnRequestClose(FRequestWindowClose::CreateLambda([this] { return ConfirmDiscard(); }));
	UpdateTitle();

	const FMenuContext Context(ToSharedRef(Commands));
	FMenuRegistry& Registry = FMenuRegistry::Get();
	return SNew(SCommandBindingScope).CommandList(Commands)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(10.0f, 2.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
				[
					SNew(STextBlock).Text("Genesis").TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))
				]
				+ SHorizontalBox::Slot().AutoWidth()[Registry.GenerateMenuBar("MapEditor.MainMenu", Context)]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[
					SNew(STextBlock).TextStyle(&SmallText()).Text([this]
					{
						if (!Document)
						{
							return FString();
						}
						FString Text = Document->GetDisplayName();
						if (&Document->GetScope() != &Document->GetRoot())
						{
							const char* Name = Document->GetScope().GetObjName();
							Text += FString::Format("  ·  editing {}", Name && Name[0] ? FString::FromAnsi(Name) : FString("a group"));
						}
						return Text;
					})
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[Registry.GenerateToolBar("MapEditor.ToolBar", Context)]
		+ SVerticalBox::Slot().FillHeight(1.0f)[SAssignNew(DockHost, SBox)[ToSharedRef(DockArea)]]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(10.0f, 3.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).TextStyle(&SmallText()).Clipping(EWidgetClipping::ClipToBounds).Text([this] { return StatusText; })]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(16.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).Text([this] { return CursorText; })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).TextStyle(&SmallText()).Text([this]
				{
					if (!Document)
					{
						return FString();
					}
					return FString::Format("{}  ·  {} brushes  ·  {} entities  ·  {} selected  ·  grid {:.0f}{}", ToString(ViewSettings.Tool),
						Document->CountBrushes(), Document->CountEntities(), Document->GetSelection().size(), ViewSettings.GridSize,
						ViewSettings.bSnapToGrid ? " (snap)" : "");
				})]
			]
		]
	];
}

void FMapEditorShell::ResetLayout()
{
	const TSharedPtr<SWindow> Window = MainWindow.lock();
	if (!Window || !DockHost)
	{
		return;
	}
	TabManager->CloseAllAreas();
	Outliner.reset();
	Properties.reset();
	PrefabList.reset();
	DockArea = TabManager->RestoreFrom(MakeDefaultLayout(), Window).ToSharedPtr();
	DockHost->SetContent(ToSharedRef(DockArea));
	Log("Layout reset");
}

void FMapEditorShell::SaveSettings()
{
	SaveEditorSettings();
	if (Host.SettingsDir.IsEmpty())
	{
		return;
	}
	const FString Layout = TabManager->PersistLayout()->ToJson().ToString();
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Layout), static_cast<SIZE_T>(Layout.Len())), Host.SettingsDir + "Layout.json");
	const FString Bindings = FInputBindingManager::Get().SaveUserBindings().ToString();
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Bindings), static_cast<SIZE_T>(Bindings.Len())), Host.SettingsDir + "Bindings.json");
}
