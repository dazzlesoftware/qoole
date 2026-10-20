// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The live docking widgets: SDockingArea (one per window) is the root
// SDockingSplitter; splitters hold splitters and SDockTabStacks; a stack shows
// a tab well and its foreground tab's content. FTabManager drives them.

#pragma once

#include "Framework/Docking/TabManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"

class SDockingNode : public SCompoundWidget
{
public:
	virtual bool IsStack() const { return false; }
	SDockingSplitter* GetParentSplitter() const { return ParentSplitter; }
	void SetParentSplitter(SDockingSplitter* Splitter) { ParentSplitter = Splitter; }
	float GetSizeCoefficient() const;
	SDockingArea* GetDockArea();

protected:
	SDockingSplitter* ParentSplitter = nullptr;
	friend class SDockingSplitter;
	float StoredCoefficient = 1.0f; // until placed in a splitter
};

class SDockingSplitter : public SDockingNode
{
public:
	SLATE_BEGIN_ARGS(SDockingSplitter)
		SLATE_ARGUMENT(EOrientation, Orientation, EOrientation::Horizontal)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	EOrientation GetOrientation() const { return Orientation; }
	const TArray<TSharedRef<SDockingNode>>& GetChildNodes() const { return ChildNodes; }
	int32 IndexOf(const SDockingNode& Node) const;

	void AddChildNode(const TSharedRef<SDockingNode>& Node, float Coefficient, int32 Index = INDEX_NONE);
	void RemoveChildNode(const SDockingNode& Node);
	void ReplaceChildNode(const SDockingNode& Old, const TSharedRef<SDockingNode>& New);
	float GetCoefficient(const SDockingNode& Node) const;
	void SetCoefficient(const SDockingNode& Node, float Coefficient);
	// Folds single-child splitters into their parent, removes empty ones.
	void Simplify();

	TSharedRef<SSplitter> GetSplitter() const { return ToSharedRef(Splitter); }

protected:
	void Rebuild();

	EOrientation Orientation = EOrientation::Horizontal;
	TArray<TSharedRef<SDockingNode>> ChildNodes;
	TArray<float> Coefficients;
	TSharedPtr<SSplitter> Splitter;
};

class SDockingArea : public SDockingSplitter
{
public:
	SLATE_BEGIN_ARGS(SDockingArea)
		SLATE_ARGUMENT(EOrientation, Orientation, EOrientation::Horizontal)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<FTabManager>& InTabManager, bool bInPrimary, const TSharedPtr<SWindow>& InWindow);

	bool IsPrimary() const { return bPrimary; }
	TSharedPtr<SWindow> GetWindow() const { return Window.lock(); }
	TSharedPtr<FTabManager> GetTabManager() const { return TabManager.lock(); }
	void SetWindow(const TSharedPtr<SWindow>& InWindow) { Window = InWindow; }
	// Every stack under this area.
	void GatherStacks(TArray<TSharedRef<SDockTabStack>>& Out) const;

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	TWeakPtr<FTabManager> TabManager;
	TWeakPtr<SWindow> Window;
	bool bPrimary = false;
};

// Drags a tab (Engine/Documentation/Source/GenesisUI.md §9: tabs drag out into floating windows and
// dock back via drop targets).
class FDockTabDragDrop final : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDockTabDragDrop, FDragDropOperation)

	TSharedPtr<SDockTab> Tab;
	FVector2 SourceSize = FVector2(480.0f, 320.0f); // logical, for a new window

	TSharedPtr<SWidget> GetDefaultDecorator() const override;
	FVector2 GetDecoratorOffset() const override { return FVector2(-20.0f, -12.0f); }
	void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;
};

class SDockTabStack : public SDockingNode
{
public:
	SLATE_BEGIN_ARGS(SDockTabStack)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<FTabManager>& InTabManager);

	bool IsStack() const override { return true; }

	const TArray<TSharedRef<SDockTab>>& GetTabs() const { return Tabs; }
	int32 NumTabs() const { return static_cast<int32>(Tabs.size()); }
	TSharedPtr<SDockTab> GetForegroundTab() const;
	int32 IndexOfTab(const SDockTab& Tab) const;
	void AddTab(const TSharedRef<SDockTab>& Tab, int32 Index = INDEX_NONE, bool bMakeForeground = true);
	void RemoveTab(const SDockTab& Tab);
	void BringToFront(const SDockTab& Tab);

	// Tab ids that were closed here (to reopen in place).
	TArray<FName> ClosedTabIds;

	TSharedPtr<FTabManager> GetTabManager() const { return TabManager.lock(); }
	bool IsActiveStack() const;

	FReply OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) override;
	void OnDragLeave(const FDragDropEvent& Event) override;
	FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override;
	FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override;

	// Where a drop at a point lands: in the well (index), on the content
	// (Invalid = add to the stack) or on an edge (split).
	struct FDropTarget
	{
		bool bValid = false;
		bool bInWell = false;
		int32 WellIndex = INDEX_NONE;
		EUINavigation Side = EUINavigation::Invalid;
		FRect PreviewRect; // local units of the stack
	};
	FDropTarget ComputeDropTarget(const FGeometry& Geometry, FVector2 AbsolutePosition, const SDockTab& Dragged) const;

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	void RebuildWell();
	void UpdateContent();

	TWeakPtr<FTabManager> TabManager;
	const FDockTabStyle* Style = nullptr;
	TArray<TSharedRef<SDockTab>> Tabs;
	int32 Foreground = INDEX_NONE;
	TSharedPtr<SHorizontalBox> Well;
	TSharedPtr<SBox> ContentBox;
	TSharedPtr<SWidget> WellArea;
	TOptional<FDropTarget> DragTarget;
};
