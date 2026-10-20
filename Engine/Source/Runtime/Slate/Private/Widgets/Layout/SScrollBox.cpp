// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Layout/SScrollBox.h"

#include "Layout/LayoutUtils.h"

namespace
{
	const FScrollBarStyle& DefaultScrollBarStyle() { return FAppStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar"); }
}

// ---------------------------------------------------------------------------
// SScrollBar

void SScrollBar::Construct(const FArguments& Args)
{
	Style = Args._Style ? Args._Style : &DefaultScrollBarStyle();
	Orientation = Args._Orientation;
	OnUserScrolled = Args._OnUserScrolled;
}

void SScrollBar::SetState(float InOffsetFraction, float InThumbSizeFraction)
{
	InThumbSizeFraction = FMath::Clamp(InThumbSizeFraction, 0.0f, 1.0f);
	InOffsetFraction = FMath::Clamp(InOffsetFraction, 0.0f, FMath::Max(0.0f, 1.0f - InThumbSizeFraction));
	if (InOffsetFraction != OffsetFraction || InThumbSizeFraction != ThumbSizeFraction)
	{
		OffsetFraction = InOffsetFraction;
		ThumbSizeFraction = InThumbSizeFraction;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FVector2 SScrollBar::ComputeDesiredSize(float) const
{
	return Orientation == EOrientation::Vertical ? FVector2(Style->Thickness, 0.0f) : FVector2(0.0f, Style->Thickness);
}

float SScrollBar::TrackLength(const FGeometry& Geometry) const
{
	return Along(Geometry.Size);
}

TPair<float, float> SScrollBar::GetThumb(const FGeometry& Geometry) const
{
	const float Track = TrackLength(Geometry);
	const float Length = FMath::Min(Track, FMath::Max(Style->MinThumbLength, Track * ThumbSizeFraction));
	const float Scrollable = 1.0f - ThumbSizeFraction;
	const float Start = Scrollable > 0.0f ? (Track - Length) * (OffsetFraction / Scrollable) : 0.0f;
	return { Start, Length };
}

void SScrollBar::ScrollTo(float NewFraction)
{
	NewFraction = FMath::Clamp(NewFraction, 0.0f, FMath::Max(0.0f, 1.0f - ThumbSizeFraction));
	if (NewFraction != OffsetFraction)
	{
		SetState(NewFraction, ThumbSizeFraction);
		OnUserScrolled.ExecuteIfBound(NewFraction);
	}
}

FReply SScrollBar::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left || !IsNeeded())
	{
		return FReply::Unhandled();
	}
	const float Position = Along(Geometry.AbsoluteToLocal(Event.GetAbsolutePosition()));
	const auto [Start, Length] = GetThumb(Geometry);
	if (Position >= Start && Position < Start + Length)
	{
		bDragging = true;
		DragGrabOffset = Position - Start;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	// Page towards the click.
	ScrollTo(OffsetFraction + (Position < Start ? -ThumbSizeFraction : ThumbSizeFraction));
	return FReply::Handled();
}

FReply SScrollBar::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (bDragging && Event.GetEffectingButton() == EMouseButton::Left)
	{
		bDragging = false;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SScrollBar::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const float Position = Along(Geometry.AbsoluteToLocal(Event.GetAbsolutePosition()));
	const auto [Start, Length] = GetThumb(Geometry);
	if (bDragging)
	{
		const float Free = TrackLength(Geometry) - Length;
		const float Scrollable = 1.0f - ThumbSizeFraction;
		if (Free > 0.0f)
		{
			ScrollTo((Position - DragGrabOffset) / Free * Scrollable);
		}
		return FReply::Handled();
	}
	const bool bOverThumb = Position >= Start && Position < Start + Length;
	if (bOverThumb != bThumbHovered)
	{
		bThumbHovered = bOverThumb;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Unhandled();
}

void SScrollBar::OnMouseLeave(const FPointerEvent&)
{
	if (bThumbHovered)
	{
		bThumbHovered = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 SScrollBar::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	Style->Track.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
	if (!IsNeeded())
	{
		return LayerId;
	}
	const auto [Start, Length] = GetThumb(Geometry);
	const FSlateBrush& Thumb = bDragging ? Style->ThumbDragged : (bThumbHovered ? Style->ThumbHovered : Style->Thumb);
	const float Inset = 2.0f;
	const FRect Rect = Orientation == EOrientation::Vertical
		? FRect(Inset, Start, Geometry.Size.X - Inset, Start + Length)
		: FRect(Start, Inset, Start + Length, Geometry.Size.Y - Inset);
	Thumb.Paint(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(Rect), InWidgetStyle.ColorAndOpacityTint);
	return LayerId + 1;
}

// ---------------------------------------------------------------------------
// SScrollBox

// The clipped area holding the scrolled slots.
class SScrollBox::SScrollPanel final : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SScrollPanel)
	SLATE_END_ARGS()

	void Construct(const FArguments&, SScrollBox* InOwner, EOrientation InOrientation)
	{
		Owner = InOwner;
		Orientation = InOrientation;
	}

	FChildren* GetChildren() override { return &Children; }

	TPanelChildren<SScrollBox::FSlot> Children{ this };

protected:
	FVector2 ComputeDesiredSize(float) const override
	{
		FVector2 Size;
		for (int32 I = 0; I < Children.Num(); ++I)
		{
			const SScrollBox::FSlot& Slot = Children[I];
			if (!IsVisibleForLayout(Slot.GetWidget()->GetVisibility()))
			{
				continue;
			}
			const FVector2 Child = Slot.GetWidget()->GetDesiredSize() + Slot.GetPadding().GetDesiredSize();
			if (Orientation == EOrientation::Vertical)
			{
				Size = FVector2(FMath::Max(Size.X, Child.X), Size.Y + Child.Y);
			}
			else
			{
				Size = FVector2(Size.X + Child.X, FMath::Max(Size.Y, Child.Y));
			}
		}
		return Size;
	}

	void OnPreArrange(const FGeometry& AllottedGeometry) override
	{
		const bool bVertical = Orientation == EOrientation::Vertical;
		const FVector2 Desired = GetDesiredSize();
		Owner->OnPanelArranged(bVertical ? AllottedGeometry.Size.Y : AllottedGeometry.Size.X, bVertical ? Desired.Y : Desired.X);
	}

	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
	{
		const bool bVertical = Orientation == EOrientation::Vertical;
		float Position = -Owner->GetScrollOffset();
		for (int32 I = 0; I < Children.Num(); ++I)
		{
			const SScrollBox::FSlot& Slot = Children[I];
			if (!IsVisibleForLayout(Slot.GetWidget()->GetVisibility()))
			{
				continue;
			}
			const FVector2 Child = Slot.GetWidget()->GetDesiredSize() + Slot.GetPadding().GetDesiredSize();
			const FVector2 Offset = bVertical ? FVector2(0.0f, Position) : FVector2(Position, 0.0f);
			const FVector2 Size = bVertical ? FVector2(AllottedGeometry.Size.X, Child.Y) : FVector2(Child.X, AllottedGeometry.Size.Y);
			ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Slot, Offset, Size);
			Position += bVertical ? Child.Y : Child.X;
		}
	}

private:
	SScrollBox* Owner = nullptr;
	EOrientation Orientation = EOrientation::Vertical;
};

void SScrollBox::Construct(FArguments& Args)
{
	Orientation = Args._Orientation;
	WheelScrollAmount = Args._WheelScrollAmount;
	OnUserScrolledDelegate = Args._OnUserScrolled;
	bScrollBarAlwaysVisible = Args._ScrollBarAlwaysVisible;
	bScrollBarAllowed = Args._ScrollBarVisibility != EVisibility::Collapsed;

	SAssignNew(Panel, SScrollPanel, this, Orientation).Clipping(EWidgetClipping::ClipToBounds);
	for (FSlot& Slot : Args._Slots)
	{
		Panel->Children.Add(std::move(Slot));
	}
	Args._Slots.clear();

	SAssignNew(ScrollBar, SScrollBar)
		.Style(Args._ScrollBarStyle)
		.Orientation(Orientation)
		.Visibility(EVisibility::Collapsed)
		.OnUserScrolled([this](float Fraction) { SetScrollOffset(Fraction * ContentSize); });

	const TSharedRef<SWidget> PanelRef = ToSharedRef(StaticCastSharedPtr<SWidget>(Panel));
	const TSharedRef<SWidget> BarRef = ToSharedRef(StaticCastSharedPtr<SWidget>(ScrollBar));
	if (Orientation == EOrientation::Vertical)
	{
		ChildSlot[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.0f)[PanelRef] + SHorizontalBox::Slot().AutoWidth()[BarRef]];
	}
	else
	{
		ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().FillHeight(1.0f)[PanelRef] + SVerticalBox::Slot().AutoHeight()[BarRef]];
	}
}

SScrollBox::FSlot& SScrollBox::AddSlot()
{
	return Panel->Children.Add(FSlot());
}

void SScrollBox::ClearChildren()
{
	Panel->Children.Empty();
	SetScrollOffset(0.0f);
}

int32 SScrollBox::NumSlots() const
{
	return Panel->Children.Num();
}

void SScrollBox::SetScrollOffset(float NewOffset)
{
	NewOffset = FMath::Clamp(NewOffset, 0.0f, GetScrollOffsetOfEnd());
	if (NewOffset != ScrollOffset)
	{
		ScrollOffset = NewOffset;
		Panel->Invalidate(EInvalidateWidgetReason::Layout);
		UpdateScrollBar();
		OnUserScrolledDelegate.ExecuteIfBound(ScrollOffset);
	}
}

void SScrollBox::OnPanelArranged(float InViewportSize, float InContentSize)
{
	ViewportSize = InViewportSize;
	ContentSize = InContentSize;
	// The content may have shrunk below the offset.
	ScrollOffset = FMath::Clamp(ScrollOffset, 0.0f, GetScrollOffsetOfEnd());
	UpdateScrollBar();
}

void SScrollBox::UpdateScrollBar()
{
	const float Thumb = ContentSize > 0.0f ? FMath::Min(1.0f, ViewportSize / ContentSize) : 1.0f;
	ScrollBar->SetState(ContentSize > 0.0f ? ScrollOffset / ContentSize : 0.0f, Thumb);
	const bool bShow = bScrollBarAllowed && (bScrollBarAlwaysVisible || Thumb < 1.0f);
	const EVisibility Wanted = bShow ? EVisibility::Visible : EVisibility::Collapsed;
	if (ScrollBar->GetVisibility() != Wanted)
	{
		ScrollBar->SetVisibility(Wanted);
	}
}

FReply SScrollBox::OnMouseWheel(const FGeometry&, const FPointerEvent& Event)
{
	const float Before = ScrollOffset;
	SetScrollOffset(ScrollOffset - Event.GetWheelDelta() * WheelScrollAmount);
	// Let an outer scroll box scroll once this one hits its end.
	return ScrollOffset != Before ? FReply::Handled() : FReply::Unhandled();
}

bool SScrollBox::ScrollDescendantIntoView(const SWidget& Descendant)
{
	if (!Panel->IsAncestorOf(&Descendant))
	{
		return false;
	}
	const FGeometry& PanelGeometry = Panel->GetCachedGeometry();
	const FGeometry& Target = Descendant.GetCachedGeometry();
	const bool bVertical = Orientation == EOrientation::Vertical;
	const float Scale = PanelGeometry.Scale > 0.0f ? PanelGeometry.Scale : 1.0f;
	const FVector2 Relative = (Target.AbsolutePosition - PanelGeometry.AbsolutePosition) / Scale;
	const FVector2 Size = Target.GetAbsoluteSize() / Scale;
	const float Start = (bVertical ? Relative.Y : Relative.X) + ScrollOffset;
	const float End = Start + (bVertical ? Size.Y : Size.X);
	const float Before = ScrollOffset;
	if (Start < ScrollOffset)
	{
		SetScrollOffset(Start);
	}
	else if (End > ScrollOffset + ViewportSize)
	{
		SetScrollOffset(End - ViewportSize);
	}
	return ScrollOffset != Before;
}

// ---------------------------------------------------------------------------
// SSplitter

void SSplitter::Construct(FArguments& Args)
{
	Orientation = Args._Orientation;
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FSplitterStyle>("Splitter");
	for (FSlot& Slot : Args._Slots)
	{
		Children.Add(std::move(Slot));
	}
	Args._Slots.clear();
}

SSplitter::FSlot& SSplitter::AddSlot(int32 AtIndex)
{
	return Children.Insert(FSlot(), AtIndex == INDEX_NONE ? Children.Num() : AtIndex);
}

void SSplitter::ComputeSizes(float Total, TArray<int32>& Visible, TArray<float>& Sizes) const
{
	Visible.clear();
	Sizes.clear();
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		if (IsVisibleForLayout(Children[I].GetWidget()->GetVisibility()))
		{
			Visible.push_back(I);
		}
	}
	if (Visible.empty())
	{
		return;
	}
	float Available = FMath::Max(0.0f, Total - Style->HandleSize * static_cast<float>(Visible.size() - 1));
	float TotalValue = 0.0f;
	for (const int32 I : Visible)
	{
		const FSlot& Slot = Children[I];
		if (Slot.Rule == ESizeRule::SizeToContent)
		{
			Available -= Along(Slot.GetWidget()->GetDesiredSize());
		}
		else
		{
			TotalValue += Slot.SizeValue;
		}
	}
	Available = FMath::Max(0.0f, Available);
	for (const int32 I : Visible)
	{
		const FSlot& Slot = Children[I];
		const float Size = Slot.Rule == ESizeRule::SizeToContent ? Along(Slot.GetWidget()->GetDesiredSize())
			: (TotalValue > 0.0f ? Available * Slot.SizeValue / TotalValue : 0.0f);
		Sizes.push_back(Size);
	}
}

FVector2 SSplitter::ComputeDesiredSize(float) const
{
	FVector2 Size;
	int32 Count = 0;
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const TSharedRef<SWidget>& Child = Children[I].GetWidget();
		if (!IsVisibleForLayout(Child->GetVisibility()))
		{
			continue;
		}
		const FVector2 D = Child->GetDesiredSize();
		if (Orientation == EOrientation::Horizontal)
		{
			Size = FVector2(Size.X + D.X, FMath::Max(Size.Y, D.Y));
		}
		else
		{
			Size = FVector2(FMath::Max(Size.X, D.X), Size.Y + D.Y);
		}
		++Count;
	}
	const float Handles = Style->HandleSize * static_cast<float>(FMath::Max(0, Count - 1));
	return Orientation == EOrientation::Horizontal ? Size + FVector2(Handles, 0.0f) : Size + FVector2(0.0f, Handles);
}

void SSplitter::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	TArray<int32> Visible;
	TArray<float> Sizes;
	ComputeSizes(Along(AllottedGeometry.Size), Visible, Sizes);
	float Position = 0.0f;
	for (SIZE_T V = 0; V < Visible.size(); ++V)
	{
		const TSharedRef<SWidget>& Child = Children[Visible[V]].GetWidget();
		const FVector2 Offset = Orientation == EOrientation::Horizontal ? FVector2(Position, 0.0f) : FVector2(0.0f, Position);
		const FVector2 Size = Orientation == EOrientation::Horizontal ? FVector2(Sizes[V], AllottedGeometry.Size.Y) : FVector2(AllottedGeometry.Size.X, Sizes[V]);
		ArrangedChildren.AddWidget(Child->GetVisibility(), FArrangedWidget(Child, AllottedGeometry.MakeChild(Offset, Size)));
		Position += Sizes[V] + Style->HandleSize;
	}
}

int32 SSplitter::FindHandle(const FGeometry& Geometry, FVector2 LocalPosition) const
{
	TArray<int32> Visible;
	TArray<float> Sizes;
	ComputeSizes(Along(Geometry.Size), Visible, Sizes);
	const float P = Along(LocalPosition);
	float Position = 0.0f;
	for (SIZE_T V = 0; V + 1 < Visible.size(); ++V)
	{
		Position += Sizes[V];
		// A little slack makes thin handles easier to grab.
		if (P >= Position - 2.0f && P < Position + Style->HandleSize + 2.0f)
		{
			return static_cast<int32>(V);
		}
		Position += Style->HandleSize;
	}
	return INDEX_NONE;
}

FReply SSplitter::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left)
	{
		return FReply::Unhandled();
	}
	const int32 Handle = FindHandle(Geometry, Geometry.AbsoluteToLocal(Event.GetAbsolutePosition()));
	if (Handle == INDEX_NONE)
	{
		return FReply::Unhandled();
	}
	DraggedHandle = Handle;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSplitter::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (DraggedHandle != INDEX_NONE && Event.GetEffectingButton() == EMouseButton::Left)
	{
		DraggedHandle = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SSplitter::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	if (DraggedHandle == INDEX_NONE)
	{
		const int32 Handle = FindHandle(Geometry, Local);
		if (Handle != HoveredHandle)
		{
			HoveredHandle = Handle;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Unhandled();
	}

	TArray<int32> Visible;
	TArray<float> Sizes;
	ComputeSizes(Along(Geometry.Size), Visible, Sizes);
	if (DraggedHandle + 1 >= static_cast<int32>(Visible.size()))
	{
		return FReply::Handled();
	}
	FSlot& A = Children[Visible[DraggedHandle]];
	FSlot& B = Children[Visible[DraggedHandle + 1]];
	if (A.Rule != ESizeRule::FractionOfParent || B.Rule != ESizeRule::FractionOfParent)
	{
		return FReply::Handled();
	}
	float Start = 0.0f;
	for (int32 V = 0; V < DraggedHandle; ++V)
	{
		Start += Sizes[V] + Style->HandleSize;
	}
	const float Combined = Sizes[DraggedHandle] + Sizes[DraggedHandle + 1];
	if (Combined <= 0.0f)
	{
		return FReply::Handled();
	}
	const float MaxA = FMath::Max(A.MinSizeValue, Combined - B.MinSizeValue);
	const float NewA = FMath::Clamp(Along(Local) - Style->HandleSize * 0.5f - Start, FMath::Min(A.MinSizeValue, MaxA), MaxA);
	const float TotalValue = A.SizeValue + B.SizeValue;
	A.Value(TotalValue * NewA / Combined);
	B.Value(TotalValue - A.SizeValue);
	A.OnResized.ExecuteIfBound(A.SizeValue);
	B.OnResized.ExecuteIfBound(B.SizeValue);
	return FReply::Handled();
}

void SSplitter::OnMouseLeave(const FPointerEvent&)
{
	if (HoveredHandle != INDEX_NONE && DraggedHandle == INDEX_NONE)
	{
		HoveredHandle = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FCursorReply SSplitter::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	if (DraggedHandle != INDEX_NONE || FindHandle(Geometry, Geometry.AbsoluteToLocal(Event.GetAbsolutePosition())) != INDEX_NONE)
	{
		return FCursorReply::Cursor(Orientation == EOrientation::Horizontal ? EMouseCursor::ResizeLeftRight : EMouseCursor::ResizeUpDown);
	}
	return SPanel::OnCursorQuery(Geometry, Event);
}

int32 SSplitter::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const int32 MaxLayer = PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId, InWidgetStyle);
	TArray<int32> Visible;
	TArray<float> Sizes;
	ComputeSizes(Along(Geometry.Size), Visible, Sizes);
	float Position = 0.0f;
	for (SIZE_T V = 0; V + 1 < Visible.size(); ++V)
	{
		Position += Sizes[V];
		const FRect Rect = Orientation == EOrientation::Horizontal ? FRect(Position, 0.0f, Position + Style->HandleSize, Geometry.Size.Y)
			: FRect(0.0f, Position, Geometry.Size.X, Position + Style->HandleSize);
		const bool bActive = static_cast<int32>(V) == DraggedHandle || (DraggedHandle == INDEX_NONE && static_cast<int32>(V) == HoveredHandle);
		(bActive ? Style->HandleHighlight : Style->Handle).Paint(OutDrawElements, MaxLayer + 1, Geometry.ToPaintGeometry(Rect), InWidgetStyle.ColorAndOpacityTint);
		Position += Style->HandleSize;
	}
	return MaxLayer + 1;
}
