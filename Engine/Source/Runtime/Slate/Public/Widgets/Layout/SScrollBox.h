// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/SlateStyle.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/WidgetDelegates.h"

// A scroll bar. The owner tells it what part of the content is visible
// (SetState) and hears about user scrolling through OnUserScrolled, which
// reports the new offset as a fraction of the content.
class SScrollBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScrollBar)
		SLATE_STYLE_ARGUMENT(FScrollBarStyle, Style)
		SLATE_ARGUMENT(EOrientation, Orientation, EOrientation::Vertical)
		SLATE_EVENT(FOnUserScrolled, OnUserScrolled)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	// OffsetFraction: start of the visible part / content size.
	// ThumbSizeFraction: visible size / content size (>= 1: nothing to scroll).
	void SetState(float InOffsetFraction, float InThumbSizeFraction);
	float GetOffsetFraction() const { return OffsetFraction; }
	float GetThumbSizeFraction() const { return ThumbSizeFraction; }
	bool IsNeeded() const { return ThumbSizeFraction < 1.0f; }
	bool IsDragging() const { return bDragging; }

	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseLeave(const FPointerEvent& Event) override;
	void OnMouseCaptureLost() override { bDragging = false; Invalidate(EInvalidateWidgetReason::Paint); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	// Thumb start and length along the track, local units.
	TPair<float, float> GetThumb(const FGeometry& Geometry) const;
	float TrackLength(const FGeometry& Geometry) const;
	float Along(FVector2 V) const { return Orientation == EOrientation::Vertical ? V.Y : V.X; }
	void ScrollTo(float NewFraction);

	const FScrollBarStyle* Style = nullptr;
	EOrientation Orientation = EOrientation::Vertical;
	FOnUserScrolled OnUserScrolled;
	float OffsetFraction = 0.0f;
	float ThumbSizeFraction = 1.0f;
	bool bDragging = false;
	bool bThumbHovered = false;
	float DragGrabOffset = 0.0f; // where on the thumb it was grabbed
};

// Scrolls its slots (stacked vertically or horizontally) inside a clipped
// area, with a scroll bar that appears when the content does not fit.
class SScrollBox : public SCompoundWidget
{
public:
	class FSlot : public TSlotBase<FSlot>, public TPaddingMixin<FSlot>, public TAlignmentMixin<FSlot>
	{
	};

	SLATE_BEGIN_ARGS(SScrollBox)
		SLATE_ARGUMENT(EOrientation, Orientation, EOrientation::Vertical)
		SLATE_STYLE_ARGUMENT(FScrollBarStyle, ScrollBarStyle)
		SLATE_ARGUMENT(bool, ScrollBarAlwaysVisible, false)
		SLATE_ARGUMENT(EVisibility, ScrollBarVisibility, EVisibility::Visible) // Collapsed hides it for good
		SLATE_ARGUMENT(float, WheelScrollAmount, 48.0f) // logical units per wheel notch
		SLATE_EVENT(FOnUserScrolled, OnUserScrolled)    // new offset, logical units
		SLATE_SLOT_ARGUMENT(SScrollBox::FSlot, Slots)
	SLATE_END_ARGS()

	static FSlot Slot() { return FSlot(); }

	void Construct(FArguments& Args);

	FSlot& AddSlot();
	void ClearChildren();
	int32 NumSlots() const;

	float GetScrollOffset() const { return ScrollOffset; }
	float GetScrollOffsetOfEnd() const { return FMath::Max(0.0f, ContentSize - ViewportSize); }
	float GetViewportSize() const { return ViewportSize; }
	float GetContentSize() const { return ContentSize; }
	void SetScrollOffset(float NewOffset);
	void ScrollToStart() { SetScrollOffset(0.0f); }
	void ScrollToEnd() { SetScrollOffset(GetScrollOffsetOfEnd()); }

	FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	bool ScrollDescendantIntoView(const SWidget& Descendant) override;

	// Called by the inner panel when it knows its size.
	void OnPanelArranged(float InViewportSize, float InContentSize);

private:
	class SScrollPanel;

	void UpdateScrollBar();

	TSharedPtr<SScrollPanel> Panel;
	TSharedPtr<SScrollBar> ScrollBar;
	EOrientation Orientation = EOrientation::Vertical;
	FOnUserScrolled OnUserScrolledDelegate;
	float WheelScrollAmount = 48.0f;
	float ScrollOffset = 0.0f;
	float ViewportSize = 0.0f;
	float ContentSize = 0.0f;
	bool bScrollBarAlwaysVisible = false;
	bool bScrollBarAllowed = true;
};

// Splits its area between slots along one axis, with draggable handles.
class SSplitter : public SPanel
{
public:
	enum class ESizeRule : uint8
	{
		FractionOfParent, // a share of the space (Value)
		SizeToContent,    // the child's desired size
	};

	class FSlot : public TSlotBase<FSlot>
	{
	public:
		FSlot& Value(float InValue) { SizeValue = InValue; InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }
		FSlot& SizeRule(ESizeRule InRule) { Rule = InRule; InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }
		FSlot& MinSize(float InMin) { MinSizeValue = InMin; InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }
		FSlot& OnSlotResized(FOnFloatValueChanged InDelegate) { OnResized = std::move(InDelegate); return *this; }

		float SizeValue = 1.0f;
		ESizeRule Rule = ESizeRule::FractionOfParent;
		float MinSizeValue = 20.0f;
		FOnFloatValueChanged OnResized;
	};

	SLATE_BEGIN_ARGS(SSplitter)
		SLATE_ARGUMENT(EOrientation, Orientation, EOrientation::Horizontal)
		SLATE_STYLE_ARGUMENT(FSplitterStyle, Style)
		SLATE_SLOT_ARGUMENT(SSplitter::FSlot, Slots)
	SLATE_END_ARGS()

	static FSlot Slot() { return FSlot(); }

	void Construct(FArguments& Args);

	FChildren* GetChildren() override { return &Children; }
	FSlot& AddSlot(int32 AtIndex = INDEX_NONE);
	FSlot& GetSlot(int32 Index) { return Children[Index]; }
	int32 NumSlots() const { return Children.Num(); }
	EOrientation GetOrientation() const { return Orientation; }

	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseLeave(const FPointerEvent& Event) override;
	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;
	void OnMouseCaptureLost() override { DraggedHandle = INDEX_NONE; }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	// Start and size of each visible slot along the axis.
	void ComputeSizes(float Total, TArray<int32>& Visible, TArray<float>& Sizes) const;
	// Handle index under a local position along the axis, or INDEX_NONE.
	int32 FindHandle(const FGeometry& Geometry, FVector2 LocalPosition) const;
	float Along(FVector2 V) const { return Orientation == EOrientation::Horizontal ? V.X : V.Y; }

	TPanelChildren<FSlot> Children{ this };
	EOrientation Orientation = EOrientation::Horizontal;
	const FSplitterStyle* Style = nullptr;
	int32 HoveredHandle = INDEX_NONE;
	int32 DraggedHandle = INDEX_NONE;
};
