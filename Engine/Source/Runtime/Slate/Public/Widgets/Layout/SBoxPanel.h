// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SHorizontalBox / SVerticalBox: children in a row or a column. Each slot is
// either Auto (its desired size) or Fill (a share of the space left, by
// weight).

#pragma once

#include "Widgets/SPanel.h"

enum class ESizeRule : uint8
{
	Auto,
	Stretch,
};

class SBoxPanel : public SPanel
{
public:
	class FSlot : public TSlotBase<FSlot>, public TPaddingMixin<FSlot>, public TAlignmentMixin<FSlot>
	{
	public:
		FSlot& AutoWidth() { return SetRule(ESizeRule::Auto, 0.0f); }
		FSlot& AutoHeight() { return SetRule(ESizeRule::Auto, 0.0f); }
		FSlot& FillWidth(float Weight) { return SetRule(ESizeRule::Stretch, Weight); }
		FSlot& FillHeight(float Weight) { return SetRule(ESizeRule::Stretch, Weight); }
		// Upper limit along the box's axis (0 = none).
		FSlot& MaxWidth(float Max) { return SetMax(Max); }
		FSlot& MaxHeight(float Max) { return SetMax(Max); }
		// Lower limit for Fill slots along the box's axis.
		FSlot& MinWidth(float Min) { return SetMin(Min); }
		FSlot& MinHeight(float Min) { return SetMin(Min); }

		ESizeRule GetSizeRule() const { return SizeRule; }
		float GetFillWeight() const { return FillWeight; }
		float GetMaxSize() const { return MaxSize; }
		float GetMinSize() const { return MinSize; }

	private:
		FSlot& SetRule(ESizeRule Rule, float Weight)
		{
			SizeRule = Rule;
			FillWeight = Weight;
			InvalidateSlot(EInvalidateWidgetReason::Layout);
			return *this;
		}
		FSlot& SetMax(float Max)
		{
			MaxSize = Max;
			InvalidateSlot(EInvalidateWidgetReason::Layout);
			return *this;
		}
		FSlot& SetMin(float Min)
		{
			MinSize = Min;
			InvalidateSlot(EInvalidateWidgetReason::Layout);
			return *this;
		}

		ESizeRule SizeRule = ESizeRule::Stretch;
		float FillWeight = 1.0f;
		float MaxSize = 0.0f;
		float MinSize = 0.0f;
	};

	static FSlot Slot() { return FSlot(); }

	FChildren* GetChildren() override { return &Children; }
	FSlot& AddSlot() { return Children.Add(FSlot()); }
	FSlot& InsertSlot(int32 Index) { return Children.Insert(FSlot(), Index); }
	int32 RemoveSlot(const TSharedRef<SWidget>& Widget) { return Children.Remove(Widget.Get()); }
	void ClearChildren() { Children.Empty(); }
	int32 NumSlots() const { return Children.Num(); }
	FSlot& GetSlot(int32 Index) { return Children[Index]; }
	EOrientation GetOrientation() const { return Orientation; }

protected:
	explicit SBoxPanel(EOrientation InOrientation) : Orientation(InOrientation) {}

	void ConstructSlots(TArray<FSlot>& Slots);
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

	TPanelChildren<FSlot> Children{ this };
	EOrientation Orientation;
};

class SHorizontalBox : public SBoxPanel
{
public:
	using FSlot = SBoxPanel::FSlot;

	GE_BEGIN_ARGS(SHorizontalBox)
		GE_SLOTS(SBoxPanel::FSlot, Slots)
	GE_END_ARGS()

	SHorizontalBox() : SBoxPanel(EOrientation::Horizontal) {}
	void Construct(FArguments& Args) { ConstructSlots(Args._Slots); }
};

class SVerticalBox : public SBoxPanel
{
public:
	using FSlot = SBoxPanel::FSlot;

	GE_BEGIN_ARGS(SVerticalBox)
		GE_SLOTS(SBoxPanel::FSlot, Slots)
	GE_END_ARGS()

	SVerticalBox() : SBoxPanel(EOrientation::Vertical) {}
	void Construct(FArguments& Args) { ConstructSlots(Args._Slots); }
};
