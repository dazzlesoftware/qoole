// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Layout/SBox.h"

#include "Layout/LayoutUtils.h"

void SBox::Construct(const FArguments& Args)
{
	FSimpleSlot& Slot = ChildSlotStorage.Slot;
	Slot.HAlign(Args._HAlign).VAlign(Args._VAlign).Padding(Args._Padding);
	if (Args._Content.Widget)
	{
		Slot[ToSharedRef(Args._Content.Widget)];
	}
	WidthOverride.Assign(Args._WidthOverride);
	HeightOverride.Assign(Args._HeightOverride);
	MinDesiredWidth.Assign(Args._MinDesiredWidth);
	MinDesiredHeight.Assign(Args._MinDesiredHeight);
	MaxDesiredWidth.Assign(Args._MaxDesiredWidth);
	MaxDesiredHeight.Assign(Args._MaxDesiredHeight);
}

FVector2 SBox::ComputeDesiredSize(float) const
{
	const FSimpleSlot& Slot = ChildSlotStorage.Slot;
	FVector2 Size = Slot.GetWidget()->GetDesiredSize() + Slot.GetPadding().GetDesiredSize();
	const auto Limit = [](float Value, const TOptional<float>& Override, const TOptional<float>& Min, const TOptional<float>& Max)
	{
		if (Override)
		{
			return *Override;
		}
		if (Min)
		{
			Value = FMath::Max(Value, *Min);
		}
		if (Max)
		{
			Value = FMath::Min(Value, *Max);
		}
		return Value;
	};
	Size.X = Limit(Size.X, WidthOverride.Get(), MinDesiredWidth.Get(), MaxDesiredWidth.Get());
	Size.Y = Limit(Size.Y, HeightOverride.Get(), MinDesiredHeight.Get(), MaxDesiredHeight.Get());
	return Size;
}

void SBox::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	ArrangeSlotChild(AllottedGeometry, ArrangedChildren, ChildSlotStorage.Slot, FVector2::Zero(), AllottedGeometry.Size);
}

int32 SBox::OnPaint(const FPaintArgs& Args, const FGeometry&, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId, InWidgetStyle);
}

void SBorder::Construct(const FArguments& Args)
{
	BorderImage = Args._BorderImage ? Args._BorderImage : &FAppStyle::Get().GetBrush("Border");
	BorderBackgroundColor.Assign(Args._BorderBackgroundColor);
	ForegroundColor.Assign(Args._ForegroundColor);
	ColorAndOpacity.Assign(Args._ColorAndOpacity);
	MouseButtonDownHandler = Args._OnMouseButtonDown;
	MouseDoubleClickHandler = Args._OnMouseDoubleClick;
	ChildSlot.HAlign(Args._HAlign).VAlign(Args._VAlign).Padding(Args._Padding);
	if (Args._Content.Widget)
	{
		ChildSlot[ToSharedRef(Args._Content.Widget)];
	}
}

void SBorder::SetBorderImage(const FSlateBrush* Brush)
{
	if (BorderImage != Brush)
	{
		BorderImage = Brush;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SBorder::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return MouseButtonDownHandler.IsBound() ? MouseButtonDownHandler.Execute(Geometry, Event) : FReply::Unhandled();
}

FReply SBorder::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return MouseDoubleClickHandler.IsBound() ? MouseDoubleClickHandler.Execute(Geometry, Event) : FReply::Unhandled();
}

int32 SBorder::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	if (BorderImage && BorderImage->IsVisible())
	{
		const FColor Tint = InWidgetStyle.Tinted(BorderBackgroundColor.Get().Resolve());
		BorderImage->Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Tint);
	}
	return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, MakeChildStyle(InWidgetStyle));
}
