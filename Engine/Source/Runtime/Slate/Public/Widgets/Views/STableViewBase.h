// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The untyped half of list and tree views (docs/GenesisUI.md §6.6):
// virtualization and scrolling. Only the rows that are visible exist as
// widgets; they are generated (or reused) while the view is arranged, once
// its height is known. The scroll offset is in items (fractional), so rows
// may have different heights.

#pragma once

#include "Widgets/Layout/SScrollBox.h"

class ITableRow
{
public:
	virtual ~ITableRow() = default;
	virtual TSharedRef<SWidget> AsWidget() = 0;
	virtual void SetIndexInList(int32 Index) = 0;
	virtual int32 GetIndexInList() const = 0;
};

class STableViewBase : public SCompoundWidget
{
public:
	double GetScrollOffset() const { return ScrollOffset; }
	void SetScrollOffset(double InOffset);
	void ScrollToTop() { SetScrollOffset(0.0); }
	void ScrollToBottom() { SetScrollOffset(1.0e12); }
	// Scrolls so the item at Index is fully visible (at the next layout).
	void RequestScrollIntoViewIndex(int32 Index);
	// Index of the first (partly) visible item and how many rows exist now.
	int32 GetFirstVisibleIndex() const { return static_cast<int32>(ScrollOffset); }
	int32 GetNumGeneratedRows() const;
	float GetAverageRowHeight() const { return AverageRowHeight; }
	float GetEstimatedContentHeight() const { return AverageRowHeight * static_cast<float>(GetNumItems()); }

	bool SupportsKeyboardFocus() const override { return true; }
	FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

	// Called by the inner panel when it is arranged.
	void GeneratePanelRows(const FGeometry& PanelGeometry);

protected:
	class SListPanel;

	STableViewBase() = default;

	// Builds the panel and scroll bar; call from the derived Construct.
	void ConstructTableView(const FScrollBarStyle* ScrollBarStyle, float InWheelScrollAmount, EVisibility ScrollBarVisibility);

	virtual int32 GetNumItems() const = 0;
	// The row for an item: reused from the last generation or made now.
	virtual TSharedRef<SWidget> GetRowWidgetForIndex(int32 Index) = 0;
	virtual void BeginGeneration() = 0;
	// Rows not requested since BeginGeneration may be released.
	virtual void EndGeneration() = 0;
	// Rows are rebuilt from scratch at the next layout.
	void RebuildAllRows();
	// Rows are generated again (reusing widgets) at the next layout.
	void RefreshRows();

	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

	TSharedPtr<SListPanel> Panel;
	TSharedPtr<SScrollBar> ScrollBar;
	double ScrollOffset = 0.0;
	float AverageRowHeight = 20.0f;
	float WheelScrollAmount = 48.0f;
	int32 PendingScrollIntoView = INDEX_NONE;
	bool bScrollBarAllowed = true;
	bool bRebuildRows = false;

private:
	// Generates rows from Offset; returns the height they cover.
	float GenerateFrom(double Offset, float ViewHeight, float LayoutScale, TArray<TPair<TSharedRef<SWidget>, float>>& OutRows);
	void UpdateScrollBar(float ViewHeight);
};
