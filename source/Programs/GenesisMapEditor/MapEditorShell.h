// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The map editor's main window content (milestone 5 of docs/GenesisUI.md):
// a menu bar and toolbar built from commands, the four map views (top,
// front, side, 3D) as dockable tabs, an outliner, a log and a status bar,
// around one open map (FMapDocument).
//
// Files: .qle maps (the editor's own format, with the game set in their
// header) are opened and saved; .map files are imported and exported.

#pragma once

#include "Framework/Commands/UICommands.h"
#include "Framework/Docking/TabManager.h"
#include "MapDocument.h"
#include "MapViewportClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Views/SListView.h"

class FUIWindowRenderer;
class SWindow;

class FMapEditorShell : public TSharedFromThis<FMapEditorShell>
{
public:
	struct FHost
	{
		FRHIDevice* Device = nullptr;
		FUIRenderer* Renderer = nullptr;
		FUIWindowRenderer* WindowRenderer = nullptr;
		FString SettingsDir; // Layout.json, Bindings.json; empty: nothing is saved
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
	TSharedPtr<FMapViewportClient> GetView(EMapViewType Type) const { return Views[static_cast<int32>(Type)]; }
	// The view the user worked in last (default: top).
	TSharedPtr<FMapViewportClient> GetActiveView() const;
	FTabManager& GetTabManager() const { return *TabManager; }
	TSharedPtr<FUICommandList> GetCommandList() const { return Commands; }

	// Adds a prefab or an entity where the active view looks.
	Object* AddPrefab(const FString& Path);
	Object* AddEntity(const FString& ClassName);

	void Log(const FString& Line);
	const TArray<FString>& GetLogLines() const { return LogLines; }

	struct FCommandSet
	{
		TSharedPtr<FUICommandInfo> NewMap, OpenMap, SaveMap, SaveMapAs, ExportMap, Exit;
		TSharedPtr<FUICommandInfo> Undo, Redo, Duplicate, Delete, SelectAll, SelectNone;
		TSharedPtr<FUICommandInfo> FrameSelection, ResetViews, ShowGrid, SnapToGrid, GridSmaller, GridLarger, SolidView;
		TSharedPtr<FUICommandInfo> AddCube;
		TSharedPtr<FUICommandInfo> ResetLayout;
	} Cmd;

	static TSharedRef<FDockLayout> MakeDefaultLayout();

private:
	void RegisterCommands();
	void RegisterMenus();
	void RegisterTabs();
	void SetDocument(TUniquePtr<FMapDocument> NewDocument);
	void OnDocumentChanged(EMapChange Change);
	void UpdateTitle();
	FString DefaultFolder() const;
	Vector3d SnappedPlacement() const;
	void SetGridSize(float Size);

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
	TSharedPtr<FMapViewportClient> Views[4];
	TWeakPtr<SDockTab> ViewTabs[4];
	int32 LastViewIndex = 0;

	// Outliner: the scope's objects, in order.
	TArray<Object*> OutlinerItems;
	TSharedPtr<SListView<Object*>> Outliner;
	bool bSyncingOutliner = false;
	void RefreshOutliner();

	TArray<FString> LogLines;
	FString StatusText = "Ready";
	FString CursorText;
};
