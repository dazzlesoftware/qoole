// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The map editor's main window content, laid out like Unreal's level
// editor: a menu bar, and a toolbar with Save, the editor mode (Selection,
// Brush Editing) and Add; the viewport tab (SMapViewportLayout: up to four
// map views, each with its own toolbar; top, perspective, front and right by
// default); and the panels (MapEditorPanels.cpp): Place Actors and the
// Brush Editing mode's tools at the left, the Outliner and Details at the
// right, and up to four Content Browsers and the Output Log at the bottom;
// plus a status bar. Edit > Editor Preferences opens the preferences window
// (MapEditorPreferences.cpp). One map (FMapDocument) is open at a time.
//
// Files: .qle maps (the editor's own format, with the game set in their
// header) are opened and saved; .map files are imported and exported.

#pragma once

#include "Framework/Commands/UICommands.h"
#include "Framework/Docking/TabManager.h"
#include "RHI.h"
#include "MapDocument.h"
#include "MapViewportClient.h"
#include "SContentBrowser.h"
#include "SMapViewport.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Views/SListView.h"

class FSlateRHIRenderer;
class SPlaceActors;
class SPropertyGrid;
class SWindow;

// Unreal's editor modes, for the ones this editor has. A mode picks the
// tools (and the panel at the left): Selection has Select, Rotate and Scale
// with Place Actors; Brush Editing has Clip and the vertex, edge and face
// tools with the brush operations.
enum class EMapEditorMode : uint8
{
	Selection,
	BrushEditing,
};

const char* ToString(EMapEditorMode Mode);

class FMapEditorShell : public TSharedFromThis<FMapEditorShell>
{
public:
	struct FHost
	{
		FRHIDevice* Device = nullptr;
		FSlateRHIRenderingPolicy* Renderer = nullptr;
		FSlateRHIRenderer* WindowRenderer = nullptr;
		FString SettingsDir; // Layout.json, Bindings.json, Prefabs\; empty: nothing is saved
		TFunction<void()> Quit;
		TFunction<void(float)> SetAppScale;
	};

	explicit FMapEditorShell(FHost InHost);
	~FMapEditorShell();

	TSharedRef<SWidget> MakeContent(const TSharedRef<SWindow>& Window);
	void SaveSettings();
	void ResetLayout();

	// Asks to save unsaved changes. False if the user cancelled.
	bool ConfirmDiscard();

	// ----- Files (also used by the self-test) -----
	void NewMap(const FMapGameSet& GameSet);
	bool OpenMap(const FString& Path);
	bool SaveMap(const FString& Path);
	bool ExportMap(const FString& Path);

	FMapDocument* GetDocument() const { return Document.get(); }
	FMapViewSettings& GetViewSettings() { return ViewSettings; }
	// A view of that type, preferring one on screen (null if there is none).
	TSharedPtr<FMapViewportClient> GetView(EMapViewType Type) const;
	// The view the user worked in last (default: the first pane).
	TSharedPtr<FMapViewportClient> GetActiveView() const;
	TSharedPtr<SMapViewportLayout> GetViewportLayout() const { return ViewportLayout; }
	FTabManager& GetTabManager() const { return *TabManager; }
	TSharedPtr<FUICommandList> GetCommandList() const { return Commands; }
	TSharedPtr<SPropertyGrid> GetPropertyGrid() const { return Properties; }

	// Adds a prefab or an entity where the active view looks.
	Object* AddPrefab(const FString& Path);
	Object* AddEntity(const FString& ClassName);
	// Adds a placeable item (Place Actors, Add, a content browser) at a map
	// point (snapped to the grid when snapping), or where the active view looks.
	// bOnGround (a drop in a perspective view): a mesh's bottom goes on the
	// point instead of its pivot, as Unreal sets actors on the surface.
	Object* Place(const FMapPlaceable& Item, const Vector3d& Point, bool bOnGround = false);
	Object* Place(const FMapPlaceable& Item);
	// What can be placed: entities by category, Unreal's basic shapes as
	// static mesh actors (Shapes), then the prefabs (Geometry).
	TArray<FMapPlaceable> GetPlaceables() const;
	// Sets the tool; the mode follows (brush tools are Brush Editing's).
	void SetTool(EMapTool Tool);
	EMapEditorMode GetMode() const { return Mode; }
	void SetMode(EMapEditorMode NewMode);

	// The Editor Preferences window (opens it, or brings it to the front).
	void OpenPreferences();
	// Preferences the editor reads before the shell exists (from the config).
	struct FStartupGraphics
	{
		TOptional<ERHIBackend> Backend; // unset: the default (D3D12)
		TOptional<bool> DebugLayer;     // unset: the build's default
	};
	static FStartupGraphics GetStartupGraphics();
	enum class EStartupMap : uint8 { Empty, LastOpened };
	static EStartupMap GetStartupMapPreference();
	// The map opened or saved last (empty if none).
	static FString GetLastMap();
	TSharedPtr<SWindow> GetPreferencesWindow() const { return PreferencesWindow.lock(); }
	// Content browsers 1 to 4 (null while closed).
	static constexpr int32 MaxContentBrowsers = 4;
	TSharedPtr<SContentBrowser> GetContentBrowser(int32 Index) const { return ContentBrowsers[Index].lock(); }
	TSharedPtr<SPlaceActors> GetPlaceActors() const { return PlaceActors; }

	void Log(const FString& Line);
	const TArray<FString>& GetLogLines() const { return LogLines; }

	struct FCommandSet
	{
		TSharedPtr<FUICommandInfo> NewMap, OpenMap, SaveMap, SaveMapAs, ExportMap, Exit;
		TSharedPtr<FUICommandInfo> Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, SelectAll, SelectNone;
		TSharedPtr<FUICommandInfo> FrameSelection, ResetViews, ShowGrid, SnapToGrid, GridSmaller, GridLarger, SolidView;
		TSharedPtr<FUICommandInfo> ToolSelect, ToolRotate, ToolScale, ToolClip, ToolVertex, ToolEdge, ToolFace;
		TSharedPtr<FUICommandInfo> AddCube, Group, Ungroup, EnterGroup, ScopeUp;
		TSharedPtr<FUICommandInfo> HollowIn, HollowOut, Subtract, Intersect;
		TSharedPtr<FUICommandInfo> FlipX, FlipY, FlipZ, RotateLeft, RotateRight;
		TSharedPtr<FUICommandInfo> SavePrefab;
		TSharedPtr<FUICommandInfo> SelectionMode, BrushEditingMode;
		TSharedPtr<FUICommandInfo> EditorPreferences;
		TSharedPtr<FUICommandInfo> ResetLayout, About;
	} Cmd;

	static TSharedRef<FDockLayout> MakeDefaultLayout();

private:
	void RegisterCommands();
	void RegisterMenus();
	void RegisterTabs(); // MapEditorPanels.cpp
	void SetDocument(TUniquePtr<FMapDocument> NewDocument);
	void OnDocumentChanged(EMapChange Change);
	void UpdateTitle();
	FString DefaultFolder() const;
	Vector3d SnappedPlacement() const;
	Vector3d SelectionCenter() const;
	void SetGridSize(float Size);
	// The perspective view Solid 3D acts on: the active view if it is one.
	TSharedPtr<FMapViewportClient> GetPerspectiveView() const;
	void LoadEditorSettings();
	void SaveEditorSettings() const;
	void ShowAbout();
	void SaveSelectionAsPrefab();

	// Panels (MapEditorPanels.cpp).
	TSharedRef<SDockTab> MakeViewportTab();
	TSharedRef<SDockTab> MakePlaceActorsTab();
	TSharedRef<SDockTab> MakeBrushEditingTab();
	TSharedRef<SDockTab> MakeOutlinerTab();
	TSharedRef<SDockTab> MakeDetailsTab();
	TSharedRef<SDockTab> MakeContentBrowserTab(int32 Index);
	TSharedRef<SDockTab> MakeOutputLogTab();
	void RefreshOutliner();
	void RefreshProperties();
	// After prefabs were saved: Place Actors and the content browsers.
	void RefreshPlaceables();
	void OnAssetActivated(const FContentBrowserItem& Item);
	void ShowModeTab();

	FHost Host;
	TWeakPtr<SWindow> MainWindow;
	TSharedPtr<FTabManager> TabManager;
	TSharedPtr<FUICommandList> Commands;
	TSharedPtr<SWidget> DockArea;
	TSharedPtr<SBox> DockHost;

	TUniquePtr<FMapDocument> Document;
	FDelegateHandle DocumentHandle;
	FMapViewSettings ViewSettings;
	TUniquePtr<FMapRenderResources> RenderResources;
	// The panes' views, in the layout's pane order.
	TArray<TSharedPtr<FMapViewportClient>> Views;
	TSharedPtr<SMapViewportLayout> ViewportLayout;
	EMapViewportLayout ViewportLayoutType = EMapViewportLayout::FourPanes2x2;

	// Copies of what was cut or copied (outside any map).
	TArray<TUniquePtr<Object>> Clipboard;

	// Outliner: the map's objects as a tree (groups open into their members).
	TArray<Object*> OutlinerRoots;
	TSharedPtr<STreeView<Object*>> Outliner;
	bool bSyncingOutliner = false;

	TSharedPtr<SPropertyGrid> Properties;
	bool bPropertiesRefreshQueued = false;

	TSharedPtr<SPlaceActors> PlaceActors;
	TWeakPtr<SContentBrowser> ContentBrowsers[MaxContentBrowsers];
	FString ContentBrowserPaths[MaxContentBrowsers]; // kept while closed, and saved
	EMapEditorMode Mode = EMapEditorMode::Selection;
	TWeakPtr<SWindow> PreferencesWindow;

	TArray<FString> LogLines;
	FString StatusText = "Ready";
	FString CursorText;
};
