// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Children in rows and columns. Columns are as wide as their widest child
// (and rows as tall as their tallest); space left over goes to the columns
// and rows given a fill weight.

#pragma once

#include "Widgets/SPanel.h"

class SGridPanel : public SPanel
{
public:
	class FSlot : public TSlotBase<FSlot>, public TPaddingMixin<FSlot>, public TAlignmentMixin<FSlot>
	{
	public:
		FSlot() = default;
		FSlot(int32 InColumn, int32 InRow) : Column(InColumn), Row(InRow) {}

		FSlot& ColumnSpan(int32 Span) { ColumnSpanValue = FMath::Max(1, Span); InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }
		FSlot& RowSpan(int32 Span) { RowSpanValue = FMath::Max(1, Span); InvalidateSlot(EInvalidateWidgetReason::Layout); return *this; }

		int32 Column = 0;
		int32 Row = 0;
		int32 ColumnSpanValue = 1;
		int32 RowSpanValue = 1;
	};

	SLATE_BEGIN_ARGS(SGridPanel)
		SLATE_SLOT_ARGUMENT(SGridPanel::FSlot, Slots)
	SLATE_END_ARGS()

	static FSlot Slot(int32 Column, int32 Row) { return FSlot(Column, Row); }

	void Construct(FArguments& Args);

	FChildren* GetChildren() override { return &Children; }
	FSlot& AddSlot(int32 Column, int32 Row) { return Children.Add(FSlot(Column, Row)); }
	void ClearChildren() { Children.Empty(); }

	// Extra space goes to columns/rows by weight.
	void SetColumnFill(int32 Column, float Weight);
	void SetRowFill(int32 Row, float Weight);

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

private:
	// Desired column widths and row heights.
	void ComputeTracks(TArray<float>& Columns, TArray<float>& Rows) const;

	TPanelChildren<FSlot> Children{ this };
	TArray<float> ColumnFill;
	TArray<float> RowFill;
};
