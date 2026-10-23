// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "WorldCompositionEditing.h"
#include "WorldTileThumbnails.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/WidgetDelegates.h"

// The world grid seen from above (+Y up). Tiles are drawn on their cells;
// dragging one moves it in whole cells and previews whether it fits. World
// maps dropped from the content browser are registered at the cell under the
// cursor. Middle-drag pans, the wheel zooms, F frames everything and Delete
// removes the selected tile. Right-click opens a menu for the cell.
class SWorldCompositionGrid final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWorldCompositionGrid)
	SLATE_ARGUMENT(std::shared_ptr<FWorldCompositionEditSession>, Session, nullptr)
	SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
	SLATE_EVENT(TDelegate<void(const FString&)>, OnError)
	// A tile was double-clicked, or "Open Region Map" chosen.
	SLATE_EVENT(TDelegate<void(const FString&)>, OnOpenTile)
	// "New Region Here" was chosen for a cell.
	SLATE_EVENT(TDelegate<void(int32, int32)>, OnNewRegion)
	SLATE_END_ARGS()
	void Construct(const FArguments&);

	const FString& GetSelectedTile() const
	{
		return Selected;
	}
	void SetSelectedTile(const FString& TileId);
	void DeleteSelected();
	void FrameAll();
	// Local widget position of a grid point (cell corner coordinates), and the cell under a position.
	FVector2 CellToLocal(double CellX, double CellY) const;
	void LocalToCell(FVector2 Local, int32& CellX, int32& CellY) const;
	float GetPixelsPerCell() const
	{
		return PixelsPerCell;
	}

	bool SupportsKeyboardFocus() const override
	{
		return true;
	}
	FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override;
	FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
	FReply OnDragOver(const FGeometry&, const FDragDropEvent&) override;
	FReply OnDrop(const FGeometry&, const FDragDropEvent&) override;
	void OnMouseCaptureLost() override;
	void Tick(const FGeometry&, double, float) override;

	static constexpr float MinPixelsPerCell = 2.0f;
	static constexpr float MaxPixelsPerCell = 256.0f;

protected:
	FVector2 ComputeDesiredSize(float) const override
	{
		return FVector2(640, 480);
	}
	int32 OnPaint(const FPaintArgs&, const FGeometry&, const FRect&, FSlateWindowElementList&, int32,
	    const FWidgetStyle&) const override;

private:
	const FWorldCompositionTile* HitTile(FVector2 Local) const;
	void ShowCellMenu(const FPointerEvent& Event, int32 CellX, int32 CellY);
	void UpdateDragPreview();
	void Report(const FString& Error);
	std::shared_ptr<FWorldCompositionEditSession> Session;
	FSimpleDelegate OnChanged, OnSelectionChanged;
	TDelegate<void(const FString&)> OnError, OnOpenTile;
	TDelegate<void(int32, int32)> OnNewRegion;
	FWorldTileThumbnails Thumbnails;
	FString Selected;
	FVector2 Pan{320, 240}, Last;
	float PixelsPerCell = 48.0f;
	FVector2 LastSize{640, 480};
	bool bPanning = false, bDragging = false, bFramed = false;
	// The drag: the cell grabbed, and where the tile would land.
	int32 GrabCellX = 0, GrabCellY = 0, StartX = 0, StartY = 0, PreviewX = 0, PreviewY = 0;
	bool bPreviewValid = true;
};
