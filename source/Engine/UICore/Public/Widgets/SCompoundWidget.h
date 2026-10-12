// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Styling/StyleTypes.h"
#include "Widgets/DeclarativeSyntax.h"

// A widget with one child slot, usually built in Construct (buttons,
// borders, most user-made widgets). The child is placed with the slot's
// padding and alignment; the content is tinted by ColorAndOpacity and gets
// ForegroundColor as its inherited text color when one is set.
class SCompoundWidget : public SWidget
{
public:
	FChildren* GetChildren() override { return &ChildSlotStorage; }

	void SetColorAndOpacity(TAttribute<FColor> Color) { ColorAndOpacity.Assign(Color); }
	FColor GetColorAndOpacity() const { return ColorAndOpacity.Get(); }
	void SetForegroundColor(TAttribute<TOptional<FStyleColor>> Color) { ForegroundColor.Assign(Color); }
	TOptional<FStyleColor> GetForegroundColor() const { return ForegroundColor.Get(); }

protected:
	SCompoundWidget() = default;

	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

	// The style children are painted with (tint and foreground applied).
	FWidgetStyle MakeChildStyle(const FWidgetStyle& InWidgetStyle) const;

	TSingleWidgetChildren<FSimpleSlot> ChildSlotStorage{ this };
	FSimpleSlot& ChildSlot = ChildSlotStorage.Slot;

	TWidgetAttribute<FColor, EInvalidateWidgetReason::Paint> ColorAndOpacity{ *this, FColor::White() };
	TWidgetAttribute<TOptional<FStyleColor>, EInvalidateWidgetReason::Paint> ForegroundColor{ *this };
};
