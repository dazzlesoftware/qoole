// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SWrapBox: children left to right, wrapping onto new lines.
// SCanvas: children placed by anchors and offsets.
// SScaleBox: one child scaled to fit.

#pragma once

#include "Widgets/SPanel.h"

class SWrapBox : public SPanel
{
public:
	class FSlot : public TSlotBase<FSlot>, public TPaddingMixin<FSlot>, public TAlignmentMixin<FSlot>
	{
	public:
		FSlot() : TAlignmentMixin<FSlot>(HAlign_Left, VAlign_Top) {}
	};

	SLATE_BEGIN_ARGS(SWrapBox)
		// Width to wrap at for the desired size. With UseAllottedSize, the
		// box wraps at its arranged width instead.
		SLATE_ATTRIBUTE(float, PreferredSize, 300.0f)
		SLATE_ARGUMENT(bool, UseAllottedSize, false)
		SLATE_ARGUMENT(FVector2, InnerSlotPadding, FVector2::Zero())
		SLATE_SLOT_ARGUMENT(SWrapBox::FSlot, Slots)
	SLATE_END_ARGS()

	static FSlot Slot() { return FSlot(); }

	void Construct(FArguments& Args);

	FChildren* GetChildren() override { return &Children; }
	FSlot& AddSlot() { return Children.Add(FSlot()); }
	void ClearChildren() { Children.Empty(); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	void OnPreArrange(const FGeometry& AllottedGeometry) override;

private:
	// Lays children out in lines of at most Width; calls Place for each.
	FVector2 Flow(float Width, const TFunction<void(int32 Index, FVector2 Offset, FVector2 Size)>& Place) const;
	float GetWrapWidth() const;

	TPanelChildren<FSlot> Children{ this };
	TWidgetAttribute<float, EInvalidateWidgetReason::Layout> PreferredSize{ *this, 300.0f };
	bool bUseAllottedSize = false;
	FVector2 InnerSlotPadding;
	float AllottedWidth = 0.0f;
};

// Where a canvas child goes: anchors (0..1 of the canvas) and offsets.
// With equal min/max anchors on an axis the offset is a position and size
// (Left/Top = position, Right/Bottom = width/height); with stretched anchors
// it is a margin from the anchor lines.
struct FAnchors
{
	FVector2 Minimum;
	FVector2 Maximum;

	FAnchors() = default;
	FAnchors(float Uniform) : Minimum(Uniform, Uniform), Maximum(Uniform, Uniform) {}
	FAnchors(float X, float Y) : Minimum(X, Y), Maximum(X, Y) {}
	FAnchors(float MinX, float MinY, float MaxX, float MaxY) : Minimum(MinX, MinY), Maximum(MaxX, MaxY) {}

	bool IsStretchedHorizontal() const { return Minimum.X != Maximum.X; }
	bool IsStretchedVertical() const { return Minimum.Y != Maximum.Y; }
};

class SCanvas : public SPanel
{
public:
	class FSlot : public TSlotBase<FSlot>
	{
	public:
		FSlot& Anchors(const FAnchors& In) { AnchorsValue = In; InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }
		FSlot& Offset(const FMargin& In) { OffsetValue = In; InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }
		// Pivot of the child (0..1) placed at the position.
		FSlot& Alignment(FVector2 In) { AlignmentValue = In; InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }
		// Use the child's desired size instead of the offset's size.
		FSlot& AutoSize(bool In) { bAutoSize = In; InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }
		FSlot& ZOrder(int32 In) { ZOrderValue = In; InvalidateSlot(EInvalidateWidgetReason::ChildOrder); return *this; }

		FAnchors AnchorsValue;
		FMargin OffsetValue = FMargin(0.0f, 0.0f, 100.0f, 30.0f);
		FVector2 AlignmentValue;
		bool bAutoSize = false;
		int32 ZOrderValue = 0;
	};

	SLATE_BEGIN_ARGS(SCanvas)
		SLATE_SLOT_ARGUMENT(SCanvas::FSlot, Slots)
	SLATE_END_ARGS()

	static FSlot Slot() { return FSlot(); }

	void Construct(FArguments& Args);

	FChildren* GetChildren() override { return &Children; }
	FSlot& AddSlot() { return Children.Add(FSlot()); }
	void ClearChildren() { Children.Empty(); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	TPanelChildren<FSlot> Children{ this };
};

enum class EStretch : uint8
{
	None,           // natural size
	Fill,           // stretch to fill, ignoring the aspect ratio
	ScaleToFit,     // largest uniform scale that fits
	ScaleToFitX,
	ScaleToFitY,
	ScaleToFill,    // smallest uniform scale that covers (may clip)
	UserSpecified,  // a given scale
};

enum class EStretchDirection : uint8
{
	Both,
	DownOnly, // only shrink
	UpOnly,   // only grow
};

class SScaleBox : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SScaleBox)
		SLATE_ARGUMENT(EStretch, Stretch, EStretch::ScaleToFit)
		SLATE_ARGUMENT(EStretchDirection, StretchDirection, EStretchDirection::Both)
		SLATE_ATTRIBUTE(float, UserSpecifiedScale, 1.0f)
		SLATE_ARGUMENT(EHorizontalAlignment, HAlign, HAlign_Center)
		SLATE_ARGUMENT(EVerticalAlignment, VAlign, VAlign_Center)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	FChildren* GetChildren() override { return &ChildSlotStorage; }
	void SetStretch(EStretch InStretch);
	float GetContentScale() const { return ContentScale; }

	float GetRelativeLayoutScale(int32 ChildIndex, float LayoutScale) const override;

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	void OnPreArrange(const FGeometry& AllottedGeometry) override;

private:
	float ComputeScale(FVector2 Allotted, FVector2 Content) const;

	TSingleWidgetChildren<FSimpleSlot> ChildSlotStorage{ this };
	EStretch Stretch = EStretch::ScaleToFit;
	EStretchDirection StretchDirection = EStretchDirection::Both;
	TWidgetAttribute<float, EInvalidateWidgetReason::Layout> UserSpecifiedScale{ *this, 1.0f };
	FVector2 Scale2D = FVector2::One(); // Fill stretches each axis
	float ContentScale = 1.0f;          // uniform scale used for layout
};
