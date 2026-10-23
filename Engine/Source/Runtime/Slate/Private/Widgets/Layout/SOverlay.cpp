// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Layout/SOverlay.h"
#include "Templates/SharedPointer.h"

#include "Layout/LayoutUtils.h"

void SOverlay::Construct(FArguments& Args)
{
	for (FSlot& Slot : Args._Slots)
	{
		Children.Add(std::move(Slot));
	}
	Args._Slots.clear();
}

FVector2 SOverlay::ComputeDesiredSize(float) const
{
	FVector2 Size;
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const FSlot& Slot = Children[I];
		if (IsVisibleForLayout(Slot.GetWidget()->GetVisibility()))
		{
			Size = FVector2::Max(Size, Slot.GetWidget()->GetDesiredSize() + Slot.GetPadding().GetDesiredSize());
		}
	}
	return Size;
}

void SOverlay::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Children[I], FVector2::Zero(), AllottedGeometry.Size);
	}
}

int32 SOverlay::OnPaint(const FPaintArgs& Args, const FGeometry&, const FRect& CullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	// Each child starts above everything the previous one drew.
	int32 MaxLayer = LayerId;
	for (const FArrangedWidget& Child : GetArrangedChildren())
	{
		MaxLayer = Child.Widget->Paint(Args, CullingRect, OutDrawElements, MaxLayer + 1, InWidgetStyle);
	}
	return MaxLayer;
}

void SWidgetSwitcher::Construct(FArguments& Args)
{
	for (FSlot& Slot : Args._Slots)
	{
		Children.Add(std::move(Slot));
	}
	Args._Slots.clear();
	WidgetIndex.Assign(Args._WidgetIndex);
}

TSharedPtr<SWidget> SWidgetSwitcher::GetActiveWidget() const
{
	return GetWidget(WidgetIndex.Get());
}

FVector2 SWidgetSwitcher::ComputeDesiredSize(float) const
{
	const int32 Index = WidgetIndex.Get();
	if (Index < 0 || Index >= Children.Num())
	{
		return FVector2::Zero();
	}
	const FSlot& Slot = Children[Index];
	return Slot.GetWidget()->GetDesiredSize() + Slot.GetPadding().GetDesiredSize();
}

void SWidgetSwitcher::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const int32 Index = WidgetIndex.Get();
	if (Index >= 0 && Index < Children.Num())
	{
		ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Children[Index], FVector2::Zero(), AllottedGeometry.Size);
	}
}
