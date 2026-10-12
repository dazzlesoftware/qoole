// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Widgets/SPanel.h"

// Children stacked on top of each other; later slots draw (and hit-test) on top.
class SOverlay : public SPanel
{
public:
	class FSlot : public TSlotBase<FSlot>, public TPaddingMixin<FSlot>, public TAlignmentMixin<FSlot>
	{
	};

	GE_BEGIN_ARGS(SOverlay)
		GE_SLOTS(SOverlay::FSlot, Slots)
	GE_END_ARGS()

	static FSlot Slot() { return FSlot(); }

	void Construct(FArguments& Args);

	FChildren* GetChildren() override { return &Children; }
	FSlot& AddSlot() { return Children.Add(FSlot()); }
	int32 RemoveSlot(const TSharedRef<SWidget>& Widget) { return Children.Remove(Widget.Get()); }
	void ClearChildren() { Children.Empty(); }
	int32 NumSlots() const { return Children.Num(); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

	TPanelChildren<FSlot> Children{ this };
};

// Shows one of its children (tabs, pages). Only the active child takes part
// in layout, paint and input.
class SWidgetSwitcher : public SPanel
{
public:
	class FSlot : public TSlotBase<FSlot>, public TPaddingMixin<FSlot>, public TAlignmentMixin<FSlot>
	{
	};

	GE_BEGIN_ARGS(SWidgetSwitcher)
		GE_ATTRIBUTE(int32, WidgetIndex, 0)
		GE_SLOTS(SWidgetSwitcher::FSlot, Slots)
	GE_END_ARGS()

	static FSlot Slot() { return FSlot(); }

	void Construct(FArguments& Args);

	FChildren* GetChildren() override { return &Children; }
	FSlot& AddSlot() { return Children.Add(FSlot()); }
	int32 NumWidgets() const { return Children.Num(); }

	void SetActiveWidgetIndex(int32 Index) { WidgetIndex.Set(Index); }
	int32 GetActiveWidgetIndex() const { return WidgetIndex.Get(); }
	TSharedPtr<SWidget> GetActiveWidget() const;
	TSharedPtr<SWidget> GetWidget(int32 Index) const { return Index >= 0 && Index < Children.Num() ? Children[Index].GetWidget().ToSharedPtr() : nullptr; }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

private:
	TPanelChildren<FSlot> Children{ this };
	TWidgetAttribute<int32, EInvalidateWidgetReason::Layout> WidgetIndex{ *this, 0 };
};
