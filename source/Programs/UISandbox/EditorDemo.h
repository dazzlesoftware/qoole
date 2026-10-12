// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The editor framework demo (milestone 4 of docs/GenesisUI.md): a small 2D
// "map" edited in an editor-shaped window built only from the framework:
// commands with shortcuts, a menu bar and a toolbar from the menu registry,
// dockable tabs (viewport, outliner, details, log, key bindings and the
// widget gallery), a property grid with undo, and a viewport client drawn
// through the RHI. The layout and key bindings persist between runs.

#pragma once

#include "Framework/Docking/TabManager.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "Gallery.h"
#include "RenderTargetViewportClient.h"

struct FDemoEntity
{
	int32 Id = 0;
	FString Name;
	int32 Kind = 0; // 0 brush, 1 light, 2 player start, 3 trigger
	FVector2 Position;
	FVector2 Size = FVector2(64.0f, 64.0f);
	FColor Color = FColor(120, 140, 170);
	float Brightness = 1.0f; // lights only
	bool bVisible = true;

	bool operator==(const FDemoEntity&) const = default;
};

const char* GetEntityKindName(int32 Kind);

// The edited data, its selection and an undo stack of whole-scene snapshots
// (the scene is small).
class FDemoScene
{
public:
	FDemoScene();

	TArray<FDemoEntity> Entities;
	TArray<int32> Selection;

	FDemoEntity* Find(int32 Id);
	const FDemoEntity* Find(int32 Id) const;
	bool IsSelected(int32 Id) const { return ArrayUtil::Contains(Selection, Id); }
	void SetSelection(TArray<int32> Ids);

	// Undo: changes between Begin and End are one step.
	void BeginTransaction(const FString& Description);
	void EndTransaction();
	bool CanUndo() const { return !UndoStack.empty(); }
	bool CanRedo() const { return !RedoStack.empty(); }
	FString GetUndoDescription() const { return UndoStack.empty() ? FString() : UndoStack.back().Description; }
	void Undo();
	void Redo();
	void DeleteSelection();
	void Changed() { OnChanged.Broadcast(); }

	TMulticastDelegate<void()> OnChanged;
	TMulticastDelegate<void()> OnSelectionChanged;
	TMulticastDelegate<void(const FString&)> OnLog;

private:
	struct FUndoStep
	{
		FString Description;
		TArray<FDemoEntity> Before;
		TArray<FDemoEntity> After;
	};
	TArray<FUndoStep> UndoStack;
	TArray<FUndoStep> RedoStack;
	TOptional<FUndoStep> Pending;
	int32 Depth = 0;
};

// Draws the scene into a render target: grid, entities, selection.
// Left click selects (Shift adds), left drag moves the selection, the wheel
// zooms, the middle button pans, and the right button pans with raw mouse
// input (the cursor hides until it is released).
class FDemoViewportClient final : public FRenderTargetViewportClient
{
public:
	FDemoViewportClient(FRHIDevice& InDevice, FUIRenderer& InRenderer, FDemoScene& InScene);

	bool bShowGrid = true;
	bool bSnap = true;
	void FrameSelection();

	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnRawMouseMove(FVector2 Delta) override;
	void OnMouseCaptureLost() override;
	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

protected:
	void Draw(FRHICommandList& CommandList, FUIRenderTarget& Target) override;

private:
	FVector2 ToWorld(const FGeometry& Geometry, FVector2 Absolute) const;
	int32 PickEntity(FVector2 World) const;
	float Snap(float Value) const { return bSnap ? FMath::RoundToFloat(Value / 16.0f) * 16.0f : Value; }

	FDemoScene& Scene;
	FVector2 Pan = FVector2(40.0f, 40.0f); // pixels of the world origin, in logical units
	float Zoom = 1.0f;
	float DpiScale = 1.0f;
	enum class EDrag : uint8 { None, Move, Pan, Look } Drag = EDrag::None;
	FVector2 DragStartWorld;
	FVector2 LastAbsolute;
	TMap<int32, FVector2> DragStartPositions;
	FDrawElementList Elements;
};

class FEditorDemo : public TSharedFromThis<FEditorDemo>
{
public:
	struct FHost
	{
		FRHIDevice* Device = nullptr;
		FUIRenderer* Renderer = nullptr;
		class FUIWindowRenderer* WindowRenderer = nullptr;
		FGalleryHost Gallery;
		TFunction<void()> ToggleInspector;
		TFunction<void(float)> SetAppScale;
		TFunction<void()> Quit;
		FString SettingsDir; // where Layout.json and Bindings.json live; empty: not saved
	};

	explicit FEditorDemo(FHost InHost);

	// The main window's content: menu bar, toolbar, dock area, status bar.
	TSharedRef<SWidget> MakeContent(const TSharedRef<SWindow>& Window);
	// Saves the layout and key bindings (on exit).
	void SaveSettings();
	void ResetLayout();

	FDemoScene Scene;
	TSharedPtr<FTabManager> TabManager;
	TSharedPtr<FUICommandList> Commands;
	TSharedPtr<SWidgetGallery> Gallery;
	TSharedPtr<SPropertyGrid> Details;
	TSharedPtr<FDemoViewportClient> ViewportClient;
	TArray<FString> LogLines;
	FString StatusText = "Ready";

	// Commands (the "SandboxEditor" binding context).
	struct FCommandSet
	{
		TSharedPtr<FUICommandInfo> SaveLayout, ResetLayout, Quit;
		TSharedPtr<FUICommandInfo> Undo, Redo, Delete, SelectAll;
		TSharedPtr<FUICommandInfo> ShowGrid, Snap, FrameSelection, ToggleInspector;
		TSharedPtr<FUICommandInfo> AddBrush, AddLight;
	} Cmd;

	static TSharedRef<FDockLayout> MakeDefaultLayout();

private:
	void RegisterCommands();
	void RegisterMenus();
	void RegisterTabs();
	void AddEntity(int32 Kind);
	void Log(const FString& Line);

	FHost Host;
	TWeakPtr<SWindow> MainWindow;
	TSharedPtr<SWidget> DockArea;
	TSharedPtr<SBox> DockHost;
};
