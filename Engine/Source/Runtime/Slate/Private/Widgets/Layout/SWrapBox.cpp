// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Layout/SWrapBox.h"
#include "Containers/ContainerAliases.h"

#include "Layout/LayoutUtils.h"

// ---------------------------------------------------------------------------
// SWrapBox

void SWrapBox::Construct(FArguments& Args)
{
	for (FSlot& Slot : Args._Slots)
	{
		Children.Add(std::move(Slot));
	}
	Args._Slots.clear();
	PreferredSize.Assign(Args._PreferredSize);
	bUseAllottedSize = Args._UseAllottedSize;
	InnerSlotPadding = Args._InnerSlotPadding;
}

float SWrapBox::GetWrapWidth() const
{
	return bUseAllottedSize && AllottedWidth > 0.0f ? AllottedWidth : PreferredSize.Get();
}

FVector2 SWrapBox::Flow(float Width, const TFunction<void(int32, FVector2, FVector2)>& Place) const
{
	FVector2 Cursor;
	float LineHeight = 0.0f;
	float MaxWidth = 0.0f;
	bool bLineEmpty = true;
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const FSlot& Slot = Children[I];
		if (!IsVisibleForLayout(Slot.GetWidget()->GetVisibility()))
		{
			continue;
		}
		const FVector2 Size = Slot.GetWidget()->GetDesiredSize() + Slot.GetPadding().GetDesiredSize();
		const float Gap = bLineEmpty ? 0.0f : InnerSlotPadding.X;
		if (!bLineEmpty && Cursor.X + Gap + Size.X > Width)
		{
			Cursor = FVector2(0.0f, Cursor.Y + LineHeight + InnerSlotPadding.Y);
			LineHeight = 0.0f;
			bLineEmpty = true;
		}
		Cursor.X += bLineEmpty ? 0.0f : InnerSlotPadding.X;
		if (Place)
		{
			Place(I, Cursor, Size);
		}
		Cursor.X += Size.X;
		MaxWidth = FMath::Max(MaxWidth, Cursor.X);
		LineHeight = FMath::Max(LineHeight, Size.Y);
		bLineEmpty = false;
	}
	return FVector2(MaxWidth, Cursor.Y + LineHeight);
}

FVector2 SWrapBox::ComputeDesiredSize(float) const
{
	return Flow(GetWrapWidth(), nullptr);
}

void SWrapBox::OnPreArrange(const FGeometry& AllottedGeometry)
{
	if (bUseAllottedSize && AllottedWidth != AllottedGeometry.Size.X)
	{
		AllottedWidth = AllottedGeometry.Size.X;
		Invalidate(EInvalidateWidgetReason::Layout); // desired height depends on it
	}
}

void SWrapBox::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const float Width = bUseAllottedSize ? AllottedGeometry.Size.X : PreferredSize.Get();
	Flow(Width, [&](int32 Index, FVector2 Offset, FVector2 Size)
	{ ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Children[Index], Offset, Size); });
}

// ---------------------------------------------------------------------------
// SCanvas

void SCanvas::Construct(FArguments& Args)
{
	for (FSlot& Slot : Args._Slots)
	{
		Children.Add(std::move(Slot));
	}
	Args._Slots.clear();
}

FVector2 SCanvas::ComputeDesiredSize(float) const
{
	// Point-anchored children at their positions; stretched ones add nothing.
	FVector2 Size;
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const FSlot& Slot = Children[I];
		if (!IsVisibleForLayout(Slot.GetWidget()->GetVisibility()))
		{
			continue;
		}
		const FVector2 Child = Slot.bAutoSize ? Slot.GetWidget()->GetDesiredSize()
		                                      : FVector2(Slot.OffsetValue.Right, Slot.OffsetValue.Bottom);
		if (!Slot.AnchorsValue.IsStretchedHorizontal())
		{
			Size.X = FMath::Max(Size.X, Slot.OffsetValue.Left + Child.X * (1.0f - Slot.AlignmentValue.X));
		}
		if (!Slot.AnchorsValue.IsStretchedVertical())
		{
			Size.Y = FMath::Max(Size.Y, Slot.OffsetValue.Top + Child.Y * (1.0f - Slot.AlignmentValue.Y));
		}
	}
	return Size;
}

void SCanvas::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	// Arranged in z-order (stable), so paint and hit-testing follow it.
	TArray<int32> Order(static_cast<SIZE_T>(Children.Num()));
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		Order[I] = I;
	}
	std::stable_sort(Order.begin(), Order.end(),
	    [this](int32 A, int32 B) { return Children[A].ZOrderValue < Children[B].ZOrderValue; });

	const FVector2 Area = AllottedGeometry.Size;
	for (const int32 I : Order)
	{
		const FSlot& Slot = Children[I];
		const TSharedRef<SWidget>& Child = Slot.GetWidget();
		const FAnchors& A = Slot.AnchorsValue;
		const FMargin& O = Slot.OffsetValue;
		const FVector2 Desired = Child->GetDesiredSize();

		FVector2 Position, Size;
		if (A.IsStretchedHorizontal())
		{
			Position.X = A.Minimum.X * Area.X + O.Left;
			Size.X = A.Maximum.X * Area.X - O.Right - Position.X;
		}
		else
		{
			Size.X = Slot.bAutoSize ? Desired.X : O.Right;
			Position.X = A.Minimum.X * Area.X + O.Left - Size.X * Slot.AlignmentValue.X;
		}
		if (A.IsStretchedVertical())
		{
			Position.Y = A.Minimum.Y * Area.Y + O.Top;
			Size.Y = A.Maximum.Y * Area.Y - O.Bottom - Position.Y;
		}
		else
		{
			Size.Y = Slot.bAutoSize ? Desired.Y : O.Bottom;
			Position.Y = A.Minimum.Y * Area.Y + O.Top - Size.Y * Slot.AlignmentValue.Y;
		}
		ArrangedChildren.AddWidget(Child->GetVisibility(),
		    FArrangedWidget(Child, AllottedGeometry.MakeChild(Position, FVector2::Max(Size, FVector2::Zero()))));
	}
}

int32 SCanvas::OnPaint(const FPaintArgs& Args, const FGeometry&, const FRect& CullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	int32 MaxLayer = LayerId;
	for (const FArrangedWidget& Child : GetArrangedChildren())
	{
		MaxLayer = Child.Widget->Paint(Args, CullingRect, OutDrawElements, MaxLayer + 1, InWidgetStyle);
	}
	return MaxLayer;
}

// ---------------------------------------------------------------------------
// SScaleBox

void SScaleBox::Construct(const FArguments& Args)
{
	Stretch = Args._Stretch;
	StretchDirection = Args._StretchDirection;
	UserSpecifiedScale.Assign(Args._UserSpecifiedScale);
	ChildSlotStorage.Slot.HAlign(Args._HAlign).VAlign(Args._VAlign);
	if (Args._Content.Widget)
	{
		ChildSlotStorage.Slot[ToSharedRef(Args._Content.Widget)];
	}
}

void SScaleBox::SetStretch(EStretch InStretch)
{
	if (Stretch != InStretch)
	{
		Stretch = InStretch;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

float SScaleBox::GetRelativeLayoutScale(int32, float) const
{
	return ContentScale;
}

float SScaleBox::ComputeScale(FVector2 Allotted, FVector2 Content) const
{
	if (Content.X <= 0.0f || Content.Y <= 0.0f)
	{
		return 1.0f;
	}
	const float FitX = Allotted.X / Content.X;
	const float FitY = Allotted.Y / Content.Y;
	float Scale = 1.0f;
	switch (Stretch)
	{
	case EStretch::None:
	case EStretch::Fill:
		Scale = 1.0f;
		break;
	case EStretch::ScaleToFit:
		Scale = FMath::Min(FitX, FitY);
		break;
	case EStretch::ScaleToFitX:
		Scale = FitX;
		break;
	case EStretch::ScaleToFitY:
		Scale = FitY;
		break;
	case EStretch::ScaleToFill:
		Scale = FMath::Max(FitX, FitY);
		break;
	case EStretch::UserSpecified:
		return UserSpecifiedScale.Get();
	}
	if (StretchDirection == EStretchDirection::DownOnly)
	{
		Scale = FMath::Min(Scale, 1.0f);
	}
	else if (StretchDirection == EStretchDirection::UpOnly)
	{
		Scale = FMath::Max(Scale, 1.0f);
	}
	return FMath::Max(Scale, 0.01f);
}

FVector2 SScaleBox::ComputeDesiredSize(float) const
{
	const FVector2 Child = ChildSlotStorage.Slot.GetWidget()->GetDesiredSize();
	return Stretch == EStretch::UserSpecified ? Child * UserSpecifiedScale.Get() : Child;
}

void SScaleBox::OnPreArrange(const FGeometry& AllottedGeometry)
{
	const float NewScale = ComputeScale(AllottedGeometry.Size, ChildSlotStorage.Slot.GetWidget()->GetDesiredSize());
	if (!FMath::IsNearlyEqual(NewScale, ContentScale, 0.0001f))
	{
		ContentScale = NewScale;
		Invalidate(EInvalidateWidgetReason::Layout); // the child measures at the new scale
	}
}

void SScaleBox::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FSimpleSlot& Slot = ChildSlotStorage.Slot;
	const TSharedRef<SWidget>& Child = Slot.GetWidget();
	if (Stretch == EStretch::Fill)
	{
		// Fill lays the child out at the full size rather than scaling pixels.
		ArrangedChildren.AddWidget(Child->GetVisibility(),
		    FArrangedWidget(Child, AllottedGeometry.MakeChild(FVector2::Zero(), AllottedGeometry.Size)));
		return;
	}
	const FVector2 Scaled = Child->GetDesiredSize() * ContentScale;
	// Aligned by fraction, so content larger than the box (ScaleToFill)
	// overflows evenly on both sides when centered.
	const float FracX = Slot.GetHAlign() == HAlign_Left ? 0.0f : (Slot.GetHAlign() == HAlign_Right ? 1.0f : 0.5f);
	const float FracY = Slot.GetVAlign() == VAlign_Top ? 0.0f : (Slot.GetVAlign() == VAlign_Bottom ? 1.0f : 0.5f);
	const FVector2 Offset((AllottedGeometry.Size.X - Scaled.X) * FracX, (AllottedGeometry.Size.Y - Scaled.Y) * FracY);
	ArrangedChildren.AddWidget(Child->GetVisibility(),
	    FArrangedWidget(Child, AllottedGeometry.MakeChild(Offset, Child->GetDesiredSize(), ContentScale)));
}
