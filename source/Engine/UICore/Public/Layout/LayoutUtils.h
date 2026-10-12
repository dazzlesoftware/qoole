// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Placing a child inside the space its slot gets: padding, then alignment.

#pragma once

#include "Layout/Children.h"

struct FAlignmentArrangeResult
{
	float Offset = 0.0f;
	float Size = 0.0f;
};

inline FAlignmentArrangeResult AlignChildHorizontal(float AllottedSize, float DesiredSize, float PaddingStart, float PaddingEnd, EHorizontalAlignment Align)
{
	const float Available = FMath::Max(0.0f, AllottedSize - PaddingStart - PaddingEnd);
	const float Size = Align == HAlign_Fill ? Available : FMath::Min(DesiredSize, Available);
	switch (Align)
	{
	case EHorizontalAlignment::Center: return { PaddingStart + (Available - Size) * 0.5f, Size };
	case EHorizontalAlignment::Right: return { PaddingStart + Available - Size, Size };
	default: return { PaddingStart, Size };
	}
}

inline FAlignmentArrangeResult AlignChildVertical(float AllottedSize, float DesiredSize, float PaddingStart, float PaddingEnd, EVerticalAlignment Align)
{
	const float Available = FMath::Max(0.0f, AllottedSize - PaddingStart - PaddingEnd);
	const float Size = Align == VAlign_Fill ? Available : FMath::Min(DesiredSize, Available);
	switch (Align)
	{
	case EVerticalAlignment::Center: return { PaddingStart + (Available - Size) * 0.5f, Size };
	case EVerticalAlignment::Bottom: return { PaddingStart + Available - Size, Size };
	default: return { PaddingStart, Size };
	}
}

// Arranges Child inside AllottedSize at Offset (local units of Geometry) with
// padding and alignment.
inline void ArrangeAlignedChild(const FGeometry& Geometry, FArrangedChildren& Out, const TSharedRef<SWidget>& Child, FVector2 Offset, FVector2 AllottedSize,
	const FMargin& Padding, EHorizontalAlignment HAlign, EVerticalAlignment VAlign, float ChildScale = 1.0f)
{
	const FVector2 Desired = Child->GetDesiredSize() * ChildScale;
	const FAlignmentArrangeResult X = AlignChildHorizontal(AllottedSize.X, Desired.X, Padding.Left, Padding.Right, HAlign);
	const FAlignmentArrangeResult Y = AlignChildVertical(AllottedSize.Y, Desired.Y, Padding.Top, Padding.Bottom, VAlign);
	Out.AddWidget(Child->GetVisibility(), FArrangedWidget(Child, Geometry.MakeChild(Offset + FVector2(X.Offset, Y.Offset), FVector2(X.Size, Y.Size) / ChildScale, ChildScale)));
}

template <typename SlotType>
void ArrangeSlotChild(const FGeometry& Geometry, FArrangedChildren& Out, const SlotType& Slot, FVector2 Offset, FVector2 AllottedSize)
{
	ArrangeAlignedChild(Geometry, Out, Slot.GetWidget(), Offset, AllottedSize, Slot.GetPadding(), Slot.GetHAlign(), Slot.GetVAlign());
}
