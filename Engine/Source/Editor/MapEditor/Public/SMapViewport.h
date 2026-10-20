// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The map editor's viewports, laid out like Unreal's level editor:
//
//   SMapViewportLayout  Unreal's FLevelViewportLayout: one to four panes in
//                       one of Unreal's twelve arrangements (OnePane,
//                       TwoPanesHoriz ... FourPanes2x2), any pane maximized.
//   SMapViewport        Unreal's SLevelViewport: one pane, a toolbar above an
//                       SViewport showing an FMapViewportClient.
//
// The toolbar (SMapViewportToolBar, in SMapViewport.cpp) has, from the left:
// the tool menu and the Select, Rotate, Scale and Clip tools; grid snapping
// and the grid size; rotation snapping and its step. Then on the right: the
// viewport type (Perspective, Top, Bottom, Left, Right, Front, Back, with the
// field of view), the camera speed, the view mode (Lit, Unlit, Wireframe),
// the show flags, the viewport settings, the layout menu (⋮) and the
// maximize / restore button.
//
// The pane commands (FMapViewportCommands, Unreal's FEditorViewportCommands)
// are bound for each pane: Alt+G perspective, Alt+J / Alt+Shift+J top and
// bottom, Alt+K / Alt+Shift+K left and right, Alt+H / Alt+Shift+H front and
// back, Alt+2 / Alt+3 / Alt+4 wireframe, unlit and lit. They act on the pane
// with the keyboard focus. The menus are registered with FMenuRegistry
// ("MapViewport.ViewportType", "MapViewport.ViewMode", "MapViewport.Show",
// "MapViewport.Options", "MapViewport.Layout", "MapViewport.Tools",
// "MapViewport.CameraSpeed", "MapViewport.GridSize",
// "MapViewport.RotationSnap"), with the pane as the context's payload, so
// they can be extended.

#pragma once

#include "Framework/Commands/UICommands.h"
#include "MapPlacement.h"
#include "MapViewportClient.h"
#include "Widgets/Layout/SBox.h"

class SMapViewportLayout;
class SViewport;

// Unreal's LevelViewportConfigurationNames.
enum class EMapViewportLayout : uint8
{
	OnePane,
	TwoPanesHoriz,   // side by side
	TwoPanesVert,    // one above the other
	ThreePanesLeft,  // a large pane on the left
	ThreePanesRight,
	ThreePanesTop,
	ThreePanesBottom,
	FourPanes2x2,
	FourPanesLeft,   // a large pane on the left, three on the right
	FourPanesRight,
	FourPanesTop,
	FourPanesBottom,
	Count
};

const char* ToString(EMapViewportLayout Layout);
TOptional<EMapViewportLayout> MapViewportLayoutFromString(const FString& Name);
int32 GetNumPanes(EMapViewportLayout Layout);
// Where each pane goes, as fractions of the area (for the layout menu's pictures).
TArray<FRect> GetPaneRects(EMapViewportLayout Layout);

class FMapViewportCommands
{
public:
	// Registers the "MapViewport" binding context and the toolbar's menus
	// (once; Get does it too).
	static void Register();
	static const FMapViewportCommands& Get();

	TSharedPtr<FUICommandInfo> ViewportTypes[static_cast<int32>(EMapViewType::Count)];
	TSharedPtr<FUICommandInfo> WireframeMode, UnlitMode, LitMode;
	TSharedPtr<FUICommandInfo> FocusViewportToSelection, ResetCamera;
	TSharedPtr<FUICommandInfo> ToggleMaximize;
	TSharedPtr<FUICommandInfo> ViewportConfigs[static_cast<int32>(EMapViewportLayout::Count)];

	const TSharedPtr<FUICommandInfo>& ViewportType(EMapViewType Type) const { return ViewportTypes[static_cast<int32>(Type)]; }
	const TSharedPtr<FUICommandInfo>& ViewportConfig(EMapViewportLayout Layout) const { return ViewportConfigs[static_cast<int32>(Layout)]; }

private:
	FMapViewportCommands();
};

class SMapViewport final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMapViewport)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<FMapViewportClient>& InClient, SMapViewportLayout* InLayout, int32 InPaneIndex);

	FMapViewportClient& GetClient() const { return *Client; }
	const TSharedPtr<FMapViewportClient>& GetClientPtr() const { return Client; }
	SMapViewportLayout& GetLayout() const { return *Layout; }
	FMapViewSettings& GetSettings() const;
	int32 GetPaneIndex() const { return PaneIndex; }
	TSharedRef<FUICommandList> GetCommandList() const { return ToSharedRef(CommandList); }
	TSharedPtr<SViewport> GetViewportWidget() const { return ViewportWidget; }
	bool IsActive() const;

	FReply OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	// Placeable items (FMapPlaceDragDrop) drop into the view.
	FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override;
	FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override;

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	void BindCommands();

	TSharedPtr<FMapViewportClient> Client;
	SMapViewportLayout* Layout = nullptr;
	int32 PaneIndex = 0;
	TSharedPtr<FUICommandList> CommandList;
	TSharedPtr<SViewport> ViewportWidget;
};

// An item dropped into a pane: the item, the pane's view and the map point.
using FOnMapPlace = TDelegate<void(const FMapPlaceable&, FMapViewportClient&, const Vector3d&)>;

class SMapViewportLayout final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMapViewportLayout)
		SLATE_ARGUMENT(EMapViewportLayout, Layout, EMapViewportLayout::FourPanes2x2)
		// Commands for the tools, by EMapTool (the editor's, with their
		// shortcuts); without them the toolbar sets FMapViewSettings::Tool.
		SLATE_ARGUMENT(TArray<TSharedPtr<FUICommandInfo>>, ToolCommands, {})
		// The list ToolCommands are mapped in.
		SLATE_ARGUMENT(TSharedPtr<FUICommandList>, ToolCommandList, nullptr)
		// An item dragged into a pane: the item, the pane's view and the map
		// point under the cursor (FMapViewportClient::GetDropPoint).
		SLATE_EVENT(FOnMapPlace, OnPlace)
	SLATE_END_ARGS()

	static constexpr int32 MaxPanes = 4;

	// Clients[i] is shown in pane i (up to MaxPanes); all share Settings.
	void Construct(const FArguments& Args, const TArray<TSharedPtr<FMapViewportClient>>& InClients, FMapViewSettings* InSettings);

	EMapViewportLayout GetLayout() const { return Layout; }
	void SetLayout(EMapViewportLayout InLayout);
	int32 GetNumVisiblePanes() const { return MaximizedPane != INDEX_NONE ? 1 : GetNumPanes(Layout); }

	// Shows one pane alone, or all of them again.
	void ToggleMaximize(int32 Pane);
	bool IsMaximized(int32 Pane) const { return MaximizedPane == Pane; }
	int32 GetMaximizedPane() const { return MaximizedPane; }

	// The pane the user worked in last.
	int32 GetActivePane() const { return ActivePane; }
	void SetActivePane(int32 Pane);
	TSharedPtr<FMapViewportClient> GetActiveClient() const { return Clients[static_cast<SIZE_T>(ActivePane)]; }
	const TArray<TSharedPtr<FMapViewportClient>>& GetClients() const { return Clients; }
	// The pane widget showing Pane (null while it is hidden). Layout changes
	// take effect at the next frame.
	TSharedPtr<SMapViewport> GetPane(int32 Pane) const { return Panes[static_cast<SIZE_T>(Pane)].lock(); }
	bool IsPaneVisible(int32 Pane) const { return GetPane(Pane) != nullptr; }

	FMapViewSettings& GetSettings() const { return *Settings; }
	const TArray<TSharedPtr<FUICommandInfo>>& GetToolCommands() const { return ToolCommands; }
	const TSharedPtr<FUICommandList>& GetToolCommandList() const { return ToolCommandList; }

	void Place(const FMapPlaceable& Item, FMapViewportClient& View, const Vector3d& Point) const { OnPlace.ExecuteIfBound(Item, View, Point); }

	// Fired when the layout, the maximized pane or the active pane changes.
	TMulticastDelegate<void()> OnLayoutChanged;

private:
	void Rebuild();
	void QueueRebuild();
	TSharedRef<SWidget> MakePane(int32 Pane);

	TArray<TSharedPtr<FMapViewportClient>> Clients;
	FMapViewSettings* Settings = nullptr;
	EMapViewportLayout Layout = EMapViewportLayout::FourPanes2x2;
	int32 MaximizedPane = INDEX_NONE;
	int32 ActivePane = 0;
	TArray<TWeakPtr<SMapViewport>> Panes;
	TSharedPtr<SBox> Host;
	bool bRebuildQueued = false;
	TArray<TSharedPtr<FUICommandInfo>> ToolCommands;
	TSharedPtr<FUICommandList> ToolCommandList;
	FOnMapPlace OnPlace;
};
