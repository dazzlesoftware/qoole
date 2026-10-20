// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Layout/SBoxPanel.h"

#include "Layout/LayoutUtils.h"

void SBoxPanel::ConstructSlots(TArray<FSlot>& Slots)
{
	for (FSlot& Slot : Slots)
	{
		Children.Add(std::move(Slot));
	}
	Slots.clear();
}

FVector2 SBoxPanel::ComputeDesiredSize(float) const
{
	const bool bHorizontal = Orientation == EOrientation::Horizontal;
	FVector2 Size;
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const FSlot& Slot = Children[I];
		const TSharedRef<SWidget>& Child = Slot.GetWidget();
		if (!IsVisibleForLayout(Child->GetVisibility()))
		{
			continue;
		}
		const FVector2 Child2 = Child->GetDesiredSize();
		const FVector2 Padding = Slot.GetPadding().GetDesiredSize();
		float Main = bHorizontal ? Child2.X : Child2.Y;
		if (Slot.GetMaxSize() > 0.0f)
		{
			Main = FMath::Min(Main, Slot.GetMaxSize());
		}
		if (Slot.GetSizeRule() == ESizeRule::Stretch)
		{
			Main = FMath::Max(Main, Slot.GetMinSize());
		}
		if (bHorizontal)
		{
			Size.X += Main + Padding.X;
			Size.Y = FMath::Max(Size.Y, Child2.Y + Padding.Y);
		}
		else
		{
			Size.Y += Main + Padding.Y;
			Size.X = FMath::Max(Size.X, Child2.X + Padding.X);
		}
	}
	return Size;
}

void SBoxPanel::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const bool bHorizontal = Orientation == EOrientation::Horizontal;
	const float Total = bHorizontal ? AllottedGeometry.Size.X : AllottedGeometry.Size.Y;
	const float Cross = bHorizontal ? AllottedGeometry.Size.Y : AllottedGeometry.Size.X;

	// First pass: what auto slots and padding take, and the total weight.
	float Fixed = 0.0f;
	float TotalWeight = 0.0f;
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const FSlot& Slot = Children[I];
		if (!IsVisibleForLayout(Slot.GetWidget()->GetVisibility()))
		{
			continue;
		}
		const FMargin& P = Slot.GetPadding();
		Fixed += bHorizontal ? P.Left + P.Right : P.Top + P.Bottom;
		if (Slot.GetSizeRule() == ESizeRule::Auto)
		{
			const FVector2 Desired = Slot.GetWidget()->GetDesiredSize();
			float Main = bHorizontal ? Desired.X : Desired.Y;
			if (Slot.GetMaxSize() > 0.0f)
			{
				Main = FMath::Min(Main, Slot.GetMaxSize());
			}
			Fixed += Main;
		}
		else
		{
			TotalWeight += Slot.GetFillWeight();
		}
	}
	const float Free = FMath::Max(0.0f, Total - Fixed);

	float Position = 0.0f;
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const FSlot& Slot = Children[I];
		const TSharedRef<SWidget>& Child = Slot.GetWidget();
		if (!IsVisibleForLayout(Child->GetVisibility()))
		{
			continue;
		}
		const FMargin& P = Slot.GetPadding();
		const FVector2 Desired = Child->GetDesiredSize();
		float Main;
		if (Slot.GetSizeRule() == ESizeRule::Auto)
		{
			Main = bHorizontal ? Desired.X : Desired.Y;
		}
		else
		{
			Main = TotalWeight > 0.0f ? Free * Slot.GetFillWeight() / TotalWeight : 0.0f;
			Main = FMath::Max(Main, Slot.GetMinSize());
		}
		if (Slot.GetMaxSize() > 0.0f)
		{
			Main = FMath::Min(Main, Slot.GetMaxSize());
		}

		// The slot's cell: its main size plus padding, the full cross size.
		const float PaddingMain = bHorizontal ? P.Left + P.Right : P.Top + P.Bottom;
		const FVector2 CellOffset = bHorizontal ? FVector2(Position, 0.0f) : FVector2(0.0f, Position);
		const FVector2 CellSize = bHorizontal ? FVector2(Main + PaddingMain, Cross) : FVector2(Cross, Main + PaddingMain);
		ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Slot, CellOffset, CellSize);
		Position += Main + PaddingMain;
	}
}
