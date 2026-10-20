// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/SlateStyle.h"
#include "Widgets/WidgetDelegates.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

// One child with size overrides and limits.
class SBox : public SWidget
{
public:
	SLATE_BEGIN_ARGS(SBox)
		SLATE_ARGUMENT(EHorizontalAlignment, HAlign, HAlign_Fill)
		SLATE_ARGUMENT(EVerticalAlignment, VAlign, VAlign_Fill)
		SLATE_ARGUMENT(FMargin, Padding, FMargin())
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<float>, WidthOverride)
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<float>, HeightOverride)
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<float>, MinDesiredWidth)
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<float>, MinDesiredHeight)
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<float>, MaxDesiredWidth)
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<float>, MaxDesiredHeight)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	FChildren* GetChildren() override { return &ChildSlotStorage; }
	void SetContent(const TSharedRef<SWidget>& Content) { ChildSlotStorage.Slot[Content]; }
	void SetWidthOverride(TAttribute<TOptional<float>> Value) { WidthOverride.Assign(Value); }
	void SetHeightOverride(TAttribute<TOptional<float>> Value) { HeightOverride.Assign(Value); }
	void SetPadding(const FMargin& Padding) { ChildSlotStorage.Slot.Padding(Padding); }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
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
	SLATE_BEGIN_ARGS(SBorder)
		SLATE_STYLE_ARGUMENT(FSlateBrush, BorderImage)
		SLATE_ATTRIBUTE(FSlateColor, BorderBackgroundColor, FSlateColor(FColor::White()))
		SLATE_OPTIONAL_ATTRIBUTE(TOptional<FSlateColor>, ForegroundColor)
		SLATE_ATTRIBUTE(FColor, ColorAndOpacity, FColor::White())
		SLATE_ARGUMENT(FMargin, Padding, FMargin(2.0f))
		SLATE_ARGUMENT(EHorizontalAlignment, HAlign, HAlign_Fill)
		SLATE_ARGUMENT(EVerticalAlignment, VAlign, VAlign_Fill)
		SLATE_EVENT(FPointerEventHandler, OnMouseButtonDown)
		SLATE_EVENT(FPointerEventHandler, OnMouseDoubleClick)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	void SetContent(const TSharedRef<SWidget>& Content) { ChildSlot[Content]; }
	TSharedRef<SWidget> GetContent() const { return ChildSlot.GetWidget(); }
	void SetBorderImage(const FSlateBrush* Brush);
	void SetBorderBackgroundColor(TAttribute<FSlateColor> Color) { BorderBackgroundColor.Assign(Color); }
	void SetPadding(const FMargin& Padding) { ChildSlot.Padding(Padding); }

	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FSlateBrush* BorderImage = nullptr;
	TWidgetAttribute<FSlateColor, EInvalidateWidgetReason::Paint> BorderBackgroundColor{ *this, FSlateColor(FColor::White()) };
	FPointerEventHandler MouseButtonDownHandler;
	FPointerEventHandler MouseDoubleClickHandler;
};

// Empty space of a given size.
class SSpacer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpacer)
		SLATE_ATTRIBUTE(FVector2, Size, FVector2::Zero())
	SLATE_END_ARGS()

	void Construct(const FArguments& Args) { Size.Assign(Args._Size); }
	void SetSize(TAttribute<FVector2> InSize) { Size.Assign(InSize); }

protected:
	FVector2 ComputeDesiredSize(float) const override { return Size.Get(); }
	int32 OnPaint(const FPaintArgs&, const FGeometry&, const FRect&, FSlateWindowElementList&, int32 LayerId, const FWidgetStyle&) const override { return LayerId; }

private:
	TWidgetAttribute<FVector2, EInvalidateWidgetReason::Layout> Size{ *this };
};
