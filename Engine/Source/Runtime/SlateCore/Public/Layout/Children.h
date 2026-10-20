// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Slots and children (Engine/Documentation/Source/GenesisUI.md §6.2).
//
// A slot holds one child plus the layout data of the parent-child
// relationship (padding, alignment, panel rules). Slots are plain values
// while they are being declared:
//
//   SNew(SVerticalBox)
//   + SVerticalBox::Slot().AutoHeight().Padding(4)
//   [
//       SNew(STextBlock).Text("Hello")
//   ]
//
// and live in the panel's TPanelChildren afterwards, where changing them
// invalidates the panel.

#pragma once

#include "Widgets/SWidget.h"

// The shared empty widget used for unfilled slots.
class SNullWidget
{
public:
	static TSharedRef<SWidget> NullWidget();
};

class FSlotBase
{
public:
	FSlotBase();
	FSlotBase(FSlotBase&& Other) noexcept;
	FSlotBase& operator=(FSlotBase&& Other) noexcept;
	virtual ~FSlotBase();

	const TSharedRef<SWidget>& GetWidget() const { return Widget; }
	SWidget* GetOwnerWidget() const { return Owner; }

	void AttachWidget(TSharedRef<SWidget> InWidget);
	// Replaces the content with the null widget; returns the old content.
	TSharedRef<SWidget> DetachWidget();

	// Called when the slot is added to (or removed from) a panel.
	void SetOwner(SWidget* InOwner);
	// Invalidates the owning panel.
	void InvalidateSlot(EInvalidateWidgetReason Reason) const;

private:
	SWidget* Owner = nullptr;
	TSharedRef<SWidget> Widget;
};

template <typename SlotType>
class TSlotBase : public FSlotBase
{
public:
	SlotType& operator[](TSharedRef<SWidget> InWidget)
	{
		AttachWidget(std::move(InWidget));
		return static_cast<SlotType&>(*this);
	}
	template <typename WidgetType>
		requires std::derived_from<WidgetType, SWidget>
	SlotType& operator[](const TSharedRef<WidgetType>& InWidget)
	{
		AttachWidget(TSharedRef<SWidget>(InWidget));
		return static_cast<SlotType&>(*this);
	}
};

template <typename SlotType>
class TPaddingMixin
{
public:
	SlotType& Padding(const FMargin& InPadding)
	{
		SlotPadding = InPadding;
		static_cast<SlotType*>(this)->InvalidateSlot(EInvalidateWidgetReason::Layout);
		return static_cast<SlotType&>(*this);
	}
	SlotType& Padding(float Uniform) { return Padding(FMargin(Uniform)); }
	SlotType& Padding(float Horizontal, float Vertical) { return Padding(FMargin(Horizontal, Vertical)); }
	SlotType& Padding(float Left, float Top, float Right, float Bottom) { return Padding(FMargin(Left, Top, Right, Bottom)); }
	const FMargin& GetPadding() const { return SlotPadding; }

protected:
	FMargin SlotPadding;
};

template <typename SlotType>
class TAlignmentMixin
{
public:
	TAlignmentMixin(EHorizontalAlignment H = HAlign_Fill, EVerticalAlignment V = VAlign_Fill) : HAlignment(H), VAlignment(V) {}

	SlotType& HAlign(EHorizontalAlignment InAlignment)
	{
		HAlignment = InAlignment;
		static_cast<SlotType*>(this)->InvalidateSlot(EInvalidateWidgetReason::Layout);
		return static_cast<SlotType&>(*this);
	}
	SlotType& VAlign(EVerticalAlignment InAlignment)
	{
		VAlignment = InAlignment;
		static_cast<SlotType*>(this)->InvalidateSlot(EInvalidateWidgetReason::Layout);
		return static_cast<SlotType&>(*this);
	}
	EHorizontalAlignment GetHAlign() const { return HAlignment; }
	EVerticalAlignment GetVAlign() const { return VAlignment; }

protected:
	EHorizontalAlignment HAlignment;
	EVerticalAlignment VAlignment;
};

// The slot of compound widgets and simple containers: padding + alignment.
class FSimpleSlot : public TSlotBase<FSimpleSlot>, public TPaddingMixin<FSimpleSlot>, public TAlignmentMixin<FSimpleSlot>
{
};

// A widget's children, for tree walks.
class FChildren
{
public:
	explicit FChildren(SWidget* InOwner) : Owner(InOwner) {}
	virtual ~FChildren() = default;
	FChildren(const FChildren&) = delete;
	FChildren& operator=(const FChildren&) = delete;

	virtual int32 Num() const = 0;
	virtual const TSharedRef<SWidget>& GetChildAt(int32 Index) const = 0;
	virtual const FSlotBase& GetSlotAt(int32 Index) const = 0;
	SWidget& GetOwner() const { return *Owner; }

protected:
	SWidget* Owner;
};

class FNoChildren final : public FChildren
{
public:
	FNoChildren() : FChildren(nullptr) {}
	int32 Num() const override { return 0; }
	const TSharedRef<SWidget>& GetChildAt(int32) const override;
	const FSlotBase& GetSlotAt(int32) const override;
};

// Exactly one slot (compound widgets, borders, boxes).
template <typename SlotType>
class TSingleWidgetChildren final : public FChildren
{
public:
	explicit TSingleWidgetChildren(SWidget* InOwner) : FChildren(InOwner) { Slot.SetOwner(InOwner); }

	int32 Num() const override { return 1; }
	const TSharedRef<SWidget>& GetChildAt(int32) const override { return Slot.GetWidget(); }
	const FSlotBase& GetSlotAt(int32) const override { return Slot; }

	SlotType Slot;
};

// Many slots of one type (panels).
template <typename SlotType>
class TPanelChildren final : public FChildren
{
public:
	explicit TPanelChildren(SWidget* InOwner) : FChildren(InOwner) {}

	int32 Num() const override { return static_cast<int32>(Slots.size()); }
	const TSharedRef<SWidget>& GetChildAt(int32 Index) const override { return Slots[Index]->GetWidget(); }
	const FSlotBase& GetSlotAt(int32 Index) const override { return *Slots[Index]; }

	SlotType& operator[](int32 Index) { return *Slots[Index]; }
	const SlotType& operator[](int32 Index) const { return *Slots[Index]; }

	SlotType& Add(SlotType&& Slot) { return Insert(std::move(Slot), Num()); }

	SlotType& Insert(SlotType&& Slot, int32 Index)
	{
		auto Owned = MakeUnique<SlotType>(std::move(Slot));
		SlotType& Ref = *Owned;
		Slots.insert(Slots.begin() + FMath::Clamp(Index, 0, Num()), std::move(Owned));
		Ref.SetOwner(Owner);
		Owner->Invalidate(EInvalidateWidgetReason::ChildOrder);
		return Ref;
	}

	void RemoveAt(int32 Index)
	{
		Slots[Index]->SetOwner(nullptr);
		Slots.erase(Slots.begin() + Index);
		Owner->Invalidate(EInvalidateWidgetReason::ChildOrder);
	}

	// Removes the slot holding Widget; returns its former index or INDEX_NONE.
	int32 Remove(const SWidget& Widget)
	{
		for (int32 I = 0; I < Num(); ++I)
		{
			if (&Slots[I]->GetWidget().Get() == &Widget)
			{
				RemoveAt(I);
				return I;
			}
		}
		return INDEX_NONE;
	}

	void Move(int32 From, int32 To)
	{
		auto Slot = std::move(Slots[From]);
		Slots.erase(Slots.begin() + From);
		Slots.insert(Slots.begin() + FMath::Clamp(To, 0, Num()), std::move(Slot));
		Owner->Invalidate(EInvalidateWidgetReason::ChildOrder);
	}

	void Empty()
	{
		if (Slots.empty())
		{
			return;
		}
		for (auto& Slot : Slots)
		{
			Slot->SetOwner(nullptr);
		}
		Slots.clear();
		Owner->Invalidate(EInvalidateWidgetReason::ChildOrder);
	}

private:
	TArray<TUniquePtr<SlotType>> Slots;
};
