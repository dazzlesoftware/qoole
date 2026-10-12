// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/StyleSet.h"
#include "Widgets/WidgetDelegates.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

// One child with size overrides and limits.
class SBox : public SWidget
{
public:
	GE_BEGIN_ARGS(SBox)
		GE_ARGUMENT(EHorizontalAlignment, HAlign, HAlign_Fill)
		GE_ARGUMENT(EVerticalAlignment, VAlign, VAlign_Fill)
		GE_ARGUMENT(FMargin, Padding, FMargin())
		GE_OPTIONAL_ATTRIBUTE(TOptional<float>, WidthOverride)
		GE_OPTIONAL_ATTRIBUTE(TOptional<float>, HeightOverride)
		GE_OPTIONAL_ATTRIBUTE(TOptional<float>, MinDesiredWidth)
		GE_OPTIONAL_ATTRIBUTE(TOptional<float>, MinDesiredHeight)
		GE_OPTIONAL_ATTRIBUTE(TOptional<float>, MaxDesiredWidth)
		GE_OPTIONAL_ATTRIBUTE(TOptional<float>, MaxDesiredHeight)
		GE_DEFAULT_SLOT(Content)
	GE_END_ARGS()

	void Construct(const FArguments& Args);

	FChildren* GetChildren() override { return &ChildSlotStorage; }
	void SetContent(const TSharedRef<SWidget>& Content) { ChildSlotStorage.Slot[Content]; }
	void SetWidthOverride(TAttribute<TOptional<float>> Value) { WidthOverride.Assign(Value); }
	void SetHeightOverride(TAttribute<TOptional<float>> Value) { HeightOverride.Assign(Value); }
	void SetPadding(const FMargin& Padding) { ChildSlotStorage.Slot.Padding(Padding); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	TSingleWidgetChildren<FSimpleSlot> ChildSlotStorage{ this };
	TWidgetAttribute<TOptional<float>, EInvalidateWidgetReason::Layout> WidthOverride{ *this };
	TWidgetAttribute<TOptional<float>, EInvalidateWidgetReason::Layout> HeightOverride{ *this };
	TWidgetAttribute<TOptional<float>, EInvalidateWidgetReason::Layout> MinDesiredWidth{ *this };
	TWidgetAttribute<TOptional<float>, EInvalidateWidgetReason::Layout> MinDesiredHeight{ *this };
	TWidgetAttribute<TOptional<float>, EInvalidateWidgetReason::Layout> MaxDesiredWidth{ *this };
	TWidgetAttribute<TOptional<float>, EInvalidateWidgetReason::Layout> MaxDesiredHeight{ *this };
};

// A brush behind one child (panels, backgrounds, frames).
class SBorder : public SCompoundWidget
{
public:
	GE_BEGIN_ARGS(SBorder)
		GE_STYLE_ARGUMENT(FStyleBrush, BorderImage)
		GE_ATTRIBUTE(FStyleColor, BorderBackgroundColor, FStyleColor(FColor::White()))
		GE_OPTIONAL_ATTRIBUTE(TOptional<FStyleColor>, ForegroundColor)
		GE_ATTRIBUTE(FColor, ColorAndOpacity, FColor::White())
		GE_ARGUMENT(FMargin, Padding, FMargin(2.0f))
		GE_ARGUMENT(EHorizontalAlignment, HAlign, HAlign_Fill)
		GE_ARGUMENT(EVerticalAlignment, VAlign, VAlign_Fill)
		GE_EVENT(FPointerEventHandler, OnMouseButtonDown)
		GE_EVENT(FPointerEventHandler, OnMouseDoubleClick)
		GE_DEFAULT_SLOT(Content)
	GE_END_ARGS()

	void Construct(const FArguments& Args);

	void SetContent(const TSharedRef<SWidget>& Content) { ChildSlot[Content]; }
	TSharedRef<SWidget> GetContent() const { return ChildSlot.GetWidget(); }
	void SetBorderImage(const FStyleBrush* Brush);
	void SetBorderBackgroundColor(TAttribute<FStyleColor> Color) { BorderBackgroundColor.Assign(Color); }
	void SetPadding(const FMargin& Padding) { ChildSlot.Padding(Padding); }

	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FStyleBrush* BorderImage = nullptr;
	TWidgetAttribute<FStyleColor, EInvalidateWidgetReason::Paint> BorderBackgroundColor{ *this, FStyleColor(FColor::White()) };
	FPointerEventHandler MouseButtonDownHandler;
	FPointerEventHandler MouseDoubleClickHandler;
};

// Empty space of a given size.
class SSpacer : public SLeafWidget
{
public:
	GE_BEGIN_ARGS(SSpacer)
		GE_ATTRIBUTE(FVector2, Size, FVector2::Zero())
	GE_END_ARGS()

	void Construct(const FArguments& Args) { Size.Assign(Args._Size); }
	void SetSize(TAttribute<FVector2> InSize) { Size.Assign(InSize); }

protected:
	FVector2 ComputeDesiredSize(float) const override { return Size.Get(); }
	int32 OnPaint(const FPaintArgs&, const FGeometry&, const FRect&, FDrawElementList&, int32 LayerId, const FWidgetStyle&) const override { return LayerId; }

private:
	TWidgetAttribute<FVector2, EInvalidateWidgetReason::Layout> Size{ *this };
};
