// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapEditorShell.h"

#include "Application/UIApplication.h"
#include "Framework/MultiBox/MenuRegistry.h"
#include "MapCoreHeaders.h"
#include "Serialization/Json.h"
#include "Services/FileDialogs.h"
#include "Services/MessageDialog.h"
#include "Services/Services.h"
#include "UIWindowRenderer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#include <cmath>
#include <filesystem>

DECLARE_LOG_CATEGORY_EXTERN(LogGenesisMapEditor, Log);

namespace
{
	const FTextBlockStyle& SmallText() { return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"); }

	const FName ViewTabIds[4] = { FName("TopView"), FName("FrontView"), FName("SideView"), FName("PerspectiveView") };

	const TArray<FFileTypeFilter>& MapFilters()
	{
		static const TArray<FFileTypeFilter> Filters = {
			{ "Maps (*.qle, *.map)", "*.qle;*.map" },
			{ "Genesis maps (*.qle)", "*.qle" },
			{ "Quake maps (*.map)", "*.map" },
		};
		return Filters;
	}

	FString ObjectLabel(Object* Obj)
	{
		const char* Name = Obj->GetObjName();
		if (Name && Name[0])
		{
			return FString::FromAnsi(Name);
		}
		if (Obj->HasEntity())
		{
			return FString::FromAnsi(Obj->GetEntityPtr()->GetClassname());
		}
		return Obj->GetNumChildren() > 0 ? FString("Group") : FString("Brush");
	}

	FString ObjectKind(Object* Obj)
	{
		if (Obj->GetNumChildren() > 0)
		{
			return FString::Format("Group of {}", Obj->GetNumChildren());
		}
		return Obj->HasEntity() ? FString("Entity") : (Obj->HasBrush() ? FString("Brush") : FString("Object"));
	}
}

FMapEditorShell::FMapEditorShell(FHost InHost)
	: Host(std::move(InHost))
{
	Commands = std::make_shared<FUICommandList>();
	TabManager = FTabManager::Create().ToSharedPtr();
	RenderResources = MakeUnique<FMapRenderResources>(*Host.Device);
	ViewSettings.OnCursorText = [this](const FString& Text) { CursorText = Text; };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Views[Index] = std::make_shared<FMapViewportClient>(*Host.Device, *Host.Renderer, *RenderResources, ViewSettings, static_cast<EMapViewType>(Index));
		Host.WindowRenderer->AddViewportClient(ToSharedRef(StaticCastSharedPtr<FRenderTargetViewportClient>(Views[Index])));
	}
	Views[3]->SetRenderMode(EMapRenderMode::Solid);
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
	Document.reset();
}

void FMapEditorShell::Log(const FString& Line)
{
	LogLines.push_back(Line);
	StatusText = Line;
	GE_LOG(LogGenesisMapEditor, Display, "{}", Line);
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
	UpdateTitle();
}

void FMapEditorShell::OnDocumentChanged(EMapChange Change)
{
	if (HasAnyChange(Change, EMapChange::Structure | EMapChange::Selection))
	{
		RefreshOutliner();
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
		Window->SetTitle("Genesis Map Editor");
		return;
	}
	Window->SetTitle(FString::Format("{}{} - Genesis Map Editor ({})", Document->GetDisplayName(), Document->IsModified() ? " *" : "",
		Document->GetGameSet().Name));
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
	FServices::GetConfig().SetString("MapEditor", "LastGameSet", GameSet.Name);
	Log(FString::Format("New map ({})", GameSet.Name));
}

bool FMapEditorShell::OpenMap(const FString& Path)
{
	const FMapGameSet* Previous = FMapEditorEnvironment::GetActiveGameSet();
	FString Error;
	TUniquePtr<FMapDocument> Opened = FMapDocument::Open(Path, Error);
	if (!Opened)
	{
		// Opening may have switched the game set; the open map needs its own.
		if (Document && Previous && FMapEditorEnvironment::GetActiveGameSet() != Previous)
		{
			FMapEditorEnvironment::Activate(*Previous);
		}
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

// ----- Commands -----

void FMapEditorShell::RegisterCommands()
{
	auto Context = MakeShared<FBindingContext>(FName("MapEditor"), "Map editor");
	using T = EUserInterfaceActionType;
	using M = EModifierKey;
	Cmd.NewMap = Context->AddCommand("NewMap", "New", "Start a new map with the current game", T::Button, FInputChord(EKey::N, M::Control)).ToSharedPtr();
	Cmd.OpenMap = Context->AddCommand("OpenMap", "Open...", "Open a .qle map or import a .map file", T::Button, FInputChord(EKey::O, M::Control)).ToSharedPtr();
	Cmd.SaveMap = Context->AddCommand("SaveMap", "Save", "Save the map", T::Button, FInputChord(EKey::S, M::Control)).ToSharedPtr();
	Cmd.SaveMapAs = Context->AddCommand("SaveMapAs", "Save As...", "Save the map under another name", T::Button, FInputChord(EKey::S, M::Control | M::Shift)).ToSharedPtr();
	Cmd.ExportMap = Context->AddCommand("ExportMap", "Export .map...", "Write the map as a Quake .map file", T::Button, FInputChord(EKey::E, M::Control)).ToSharedPtr();
	Cmd.Exit = Context->AddCommand("Exit", "Exit", "Close the editor").ToSharedPtr();
	Cmd.Undo = Context->AddCommand("Undo", "Undo", "Undo the last change", T::Button, FInputChord(EKey::Z, M::Control)).ToSharedPtr();
	Cmd.Redo = Context->AddCommand("Redo", "Redo", "Redo the change undone last", T::Button, FInputChord(EKey::Y, M::Control)).ToSharedPtr();
	Cmd.Duplicate = Context->AddCommand("Duplicate", "Duplicate", "Copy the selection, one grid step away", T::Button, FInputChord(EKey::D, M::Control)).ToSharedPtr();
	Cmd.Delete = Context->AddCommand("Delete", "Delete", "Delete the selection", T::Button, FInputChord(EKey::Delete)).ToSharedPtr();
	Cmd.SelectAll = Context->AddCommand("SelectAll", "Select All", "Select every object", T::Button, FInputChord(EKey::A, M::Control)).ToSharedPtr();
	Cmd.SelectNone = Context->AddCommand("SelectNone", "Select None", "Clear the selection", T::Button, FInputChord(EKey::Escape)).ToSharedPtr();
	Cmd.FrameSelection = Context->AddCommand("FrameSelection", "Frame Selection", "Fit every view to the selection (or the whole map)", T::Button, FInputChord(EKey::F)).ToSharedPtr();
	Cmd.ResetViews = Context->AddCommand("ResetViews", "Reset Views", "Put every view back at the origin", T::Button, FInputChord(EKey::Home)).ToSharedPtr();
	Cmd.ShowGrid = Context->AddCommand("ShowGrid", "Grid", "Show the grid in the 2D views", T::ToggleButton, FInputChord(EKey::G, M::Shift)).ToSharedPtr();
	Cmd.SnapToGrid = Context->AddCommand("SnapToGrid", "Snap", "Snap moves and new objects to the grid (hold Alt while dragging to move freely)", T::ToggleButton, FInputChord(EKey::N)).ToSharedPtr();
	Cmd.GridSmaller = Context->AddCommand("GridSmaller", "Smaller Grid", "Halve the grid size", T::Button, FInputChord(EKey::LeftBracket)).ToSharedPtr();
	Cmd.GridLarger = Context->AddCommand("GridLarger", "Larger Grid", "Double the grid size", T::Button, FInputChord(EKey::RightBracket)).ToSharedPtr();
	Cmd.SolidView = Context->AddCommand("SolidView", "Solid 3D", "Draw the 3D view with solid faces instead of wireframes", T::ToggleButton, FInputChord(EKey::F3)).ToSharedPtr();
	Cmd.AddCube = Context->AddCommand("AddCube", "Add Cube", "Add a cube brush where the view looks", T::Button, FInputChord(EKey::B)).ToSharedPtr();
	Cmd.ResetLayout = Context->AddCommand("ResetLayout", "Reset Layout", "Go back to the default window layout").ToSharedPtr();
	FInputBindingManager::Get().RegisterContext(Context);

	FUICommandList& List = *Commands;
	const auto HasDocument = [this] { return Document != nullptr; };
	const auto HasSelection = [this] { return Document && !Document->GetSelection().empty(); };

	List.MapAction(ToSharedRef(Cmd.NewMap), [this]
	{
		if (ConfirmDiscard())
		{
			const FMapGameSet* Set = FMapEditorEnvironment::GetActiveGameSet();
			NewMap(Set ? *Set : FMapEditorEnvironment::GetBuiltInGameSet());
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

	List.MapAction(ToSharedRef(Cmd.Undo), [this] { Log(FString::Format("Undo {}", Document->GetUndoDescription())); Document->Undo(); },
		[this] { return Document && Document->CanUndo(); });
	List.MapAction(ToSharedRef(Cmd.Redo), [this] { Log(FString::Format("Redo {}", Document->GetRedoDescription())); Document->Redo(); },
		[this] { return Document && Document->CanRedo(); });
	List.MapAction(ToSharedRef(Cmd.Duplicate), [this]
	{
		const float Step = ViewSettings.GridSize;
		Document->DuplicateObjects(Document->GetSelection(), Vector3d(Step, Step, 0.0f));
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Delete), [this] { Document->DeleteObjects(Document->GetSelection()); }, HasSelection);
	List.MapAction(ToSharedRef(Cmd.SelectAll), [this] { Document->SelectAll(); }, HasDocument);
	List.MapAction(ToSharedRef(Cmd.SelectNone), [this] { Document->ClearSelection(); }, HasSelection);

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
	List.MapAction(ToSharedRef(Cmd.AddCube), [this]
	{
		for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
		{
			if (Prefab.Name == "Cube")
			{
				AddPrefab(Prefab.Path);
				return;
			}
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.ResetLayout), [this] { ResetLayout(); });

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
	Bar.AddSubMenu("Object", "Object", "", FName("MapEditor.MainMenu.Object"));
	Bar.AddSubMenu("Window", "Window", "", FName("MapEditor.MainMenu.Window"));

	FMenuDefinition& File = Registry.RegisterMenu("MapEditor.MainMenu.File");
	FMenuSection& Open = File.AddSection("Open");
	Open.AddCommand(Cmd.NewMap);
	Open.AddSubMenu("NewWithGame", "New With Game", "Start a new map for another game set", [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const FMapGameSet& Set : FMapEditorEnvironment::GetGameSets())
		{
			const FMapGameSet* SetPtr = &Set;
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, SetPtr] { if (ConfirmDiscard()) NewMap(*SetPtr); });
			Section.AddEntry(FName(Set.Name), Set.Name, FString::Format("{}, entities from {}", Set.Game, Set.EntFile), Action);
		}
	});
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
	FMenuSection& Objects = Edit.AddSection("Objects");
	Objects.AddCommand(Cmd.Duplicate);
	Objects.AddCommand(Cmd.Delete);
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
	View.AddSection("Scale").AddSubMenu("AppScale", "Application Scale", "Scale the interface on top of the monitor's DPI", [this](FMenuSection& Section, const FMenuContext&)
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

	// Prefab brushes and the entity classes of the current game set.
	FMenuDefinition& ObjectMenu = Registry.RegisterMenu("MapEditor.MainMenu.Object");
	FMenuSection& Add = ObjectMenu.AddSection("Add", "Add");
	Add.AddCommand(Cmd.AddCube);
	Add.AddSubMenu("AddBrush", "Add Brush", "Add a prefab brush where the view looks", [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
		{
			const FString Path = Prefab.Path;
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, Path] { AddPrefab(Path); });
			Action.CanExecute = TDelegate<bool()>::CreateLambda([this] { return Document != nullptr; });
			Section.AddEntry(FName(Prefab.Name), Prefab.Name, "", Action);
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

	FMenuDefinition& Window = Registry.RegisterMenu("MapEditor.MainMenu.Window");
	Window.AddDynamicSection("Tabs", [this](FMenuSection& Section, const FMenuContext&) { TabManager->PopulateTabSpawnerMenu(Section); });
	Window.AddSection("Layout").AddCommand(Cmd.ResetLayout);

	FMenuDefinition& Tools = Registry.RegisterMenu("MapEditor.ToolBar");
	FMenuSection& FileTools = Tools.AddSection("File");
	FileTools.AddCommand(Cmd.NewMap, "New");
	FileTools.AddCommand(Cmd.OpenMap, "Open");
	FileTools.AddCommand(Cmd.SaveMap);
	FMenuSection& History = Tools.AddSection("History");
	History.AddCommand(Cmd.Undo);
	History.AddCommand(Cmd.Redo);
	FMenuSection& ObjectTools = Tools.AddSection("Objects");
	ObjectTools.AddCommand(Cmd.AddCube);
	ObjectTools.AddCommand(Cmd.Duplicate);
	ObjectTools.AddCommand(Cmd.Delete);
	FMenuSection& ViewTools = Tools.AddSection("View");
	ViewTools.AddCommand(Cmd.ShowGrid);
	ViewTools.AddCommand(Cmd.SnapToGrid);
	ViewTools.AddCommand(Cmd.SolidView);
	ViewTools.AddCommand(Cmd.FrameSelection);
}

// ----- Tabs -----

TSharedRef<FDockLayout> FMapEditorShell::MakeDefaultLayout()
{
	// The four views in a square, the outliner and log at the right.
	return FDockLayout::New("MapEditor", 1)->AddArea(
		FDockLayout::NewPrimaryArea()->SetOrientation(EOrientation::Horizontal)
			->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Vertical)->SetSizeCoefficient(0.8f)
				->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Horizontal)->SetSizeCoefficient(0.5f)
					->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.5f)->AddTab(ViewTabIds[0]))
					->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.5f)->AddTab(ViewTabIds[3])))
				->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Horizontal)->SetSizeCoefficient(0.5f)
					->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.5f)->AddTab(ViewTabIds[1]))
					->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.5f)->AddTab(ViewTabIds[2]))))
			->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Vertical)->SetSizeCoefficient(0.2f)
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.62f)->AddTab("Outliner"))
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.38f)->AddTab("Log"))));
}

void FMapEditorShell::RegisterTabs()
{
	FTabManager& Tabs = *TabManager;
	static const char* const ViewNames[4] = { "Top", "Front", "Side", "3D" };
	static const char* const ViewTips[4] = {
		"Looking down: X across, Y up the screen",
		"Looking along +Y: X across, Z up",
		"Looking along X: Y across, Z up",
		"Perspective: right drag to look around, with W/A/S/D/Q/E to fly",
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Tabs.RegisterTabSpawner(ViewTabIds[Index], [this, Index](const FSpawnTabArgs&)
		{
			const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label(ViewNames[Index]).Role(ETabRole::Document)
			[
				SNew(SViewport).ViewportClient(StaticCastSharedPtr<IViewportClient>(Views[Index]))
			];
			ViewTabs[Index] = Tab.ToSharedPtr();
			return Tab;
		}).SetDisplayName(FString::Format("{} View", ViewNames[Index])).SetToolTip(ViewTips[Index]);
	}
	TabManager->OnActiveTabChanged.AddLambda([this](const TSharedPtr<SDockTab>& Tab)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Tab && ViewTabs[Index].lock() == Tab)
			{
				LastViewIndex = Index;
			}
		}
	});

	Tabs.RegisterTabSpawner("Outliner", [this](const FSpawnTabArgs&)
	{
		const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Outliner")
		[
			SAssignNew(Outliner, SListView<Object*>)
			.ListItemsSource(&OutlinerItems)
			.SelectionMode(ESelectionMode::Multi)
			.OnGenerateRow([](Object* Obj, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
			{
				return SNew(STableRow<Object*>, Owner)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Clipping(EWidgetClipping::ClipToBounds).Text(ObjectLabel(Obj))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).Text(ObjectKind(Obj))]
				];
			})
			.OnSelectionChanged([this](Object*, ESelectInfo)
			{
				if (Outliner && Document && !bSyncingOutliner)
				{
					Document->SetSelection(Outliner->GetSelectedItems());
				}
			})
			.OnMouseButtonDoubleClick([this](Object*)
			{
				for (const TSharedPtr<FMapViewportClient>& View : Views)
				{
					View->FrameSelection();
				}
			})
		];
		RefreshOutliner();
		Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this](const TSharedRef<SDockTab>&) { Outliner.reset(); }));
		return Tab;
	}).SetDisplayName("Outliner").SetToolTip("The objects of the map (double-click frames them)");

	Tabs.RegisterTabSpawner("Log", [this](const FSpawnTabArgs&)
	{
		auto Items = std::make_shared<TArray<int32>>();
		TSharedPtr<SListView<int32>> List;
		const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Log")
		[
			SAssignNew(List, SListView<int32>)
			.ListItemsSource(Items.get())
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow([this](int32 Index, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
			{
				return SNew(STableRow<int32>, Owner).Padding(FMargin(8.0f, 1.0f))
				[
					SNew(STextBlock).TextStyle(&SmallText()).Text(Index < static_cast<int32>(LogLines.size()) ? LogLines[static_cast<SIZE_T>(Index)] : FString())
				];
			})
		];
		const TWeakPtr<SListView<int32>> WeakList = List;
		List->RegisterActiveTimer(0.1f, [this, Items, WeakList](double, float)
		{
			const TSharedPtr<SListView<int32>> Pinned = WeakList.lock();
			if (Pinned && Items->size() != LogLines.size())
			{
				while (Items->size() < LogLines.size())
				{
					Items->push_back(static_cast<int32>(Items->size()));
				}
				Pinned->RequestListRefresh();
				Pinned->ScrollToBottom();
			}
			return EActiveTimerReturn::Continue;
		});
		return Tab;
	}).SetDisplayName("Log").SetToolTip("What the editor did");
}

void FMapEditorShell::RefreshOutliner()
{
	OutlinerItems = Document ? Document->GetObjects() : TArray<Object*>{};
	if (!Outliner)
	{
		return;
	}
	bSyncingOutliner = true;
	Outliner->RequestListRefresh();
	Outliner->ClearSelection();
	if (Document)
	{
		for (Object* Obj : Document->GetSelection())
		{
			Outliner->SetItemSelection(Obj, true);
		}
	}
	bSyncingOutliner = false;
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
						return Document ? FString::Format("{}  ·  {}", Document->GetDisplayName(), Document->GetGameSet().Name) : FString();
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
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).TextStyle(&SmallText()).Text([this] { return StatusText; })]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(16.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).Text([this] { return CursorText; })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).TextStyle(&SmallText()).Text([this]
				{
					if (!Document)
					{
						return FString();
					}
					return FString::Format("{} brushes  ·  {} entities  ·  {} selected  ·  grid {:.0f}{}", Document->CountBrushes(), Document->CountEntities(),
						Document->GetSelection().size(), ViewSettings.GridSize, ViewSettings.bSnapToGrid ? " (snap)" : "");
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
	DockArea = TabManager->RestoreFrom(MakeDefaultLayout(), Window).ToSharedPtr();
	DockHost->SetContent(ToSharedRef(DockArea));
	Log("Layout reset");
}

void FMapEditorShell::SaveSettings()
{
	if (Host.SettingsDir.IsEmpty())
	{
		return;
	}
	const FString Layout = TabManager->PersistLayout()->ToJson().ToString();
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Layout), static_cast<SIZE_T>(Layout.Len())), Host.SettingsDir + "Layout.json");
	const FString Bindings = FInputBindingManager::Get().SaveUserBindings().ToString();
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Bindings), static_cast<SIZE_T>(Bindings.Len())), Host.SettingsDir + "Bindings.json");
}
