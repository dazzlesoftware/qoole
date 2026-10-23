// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Layout/SGridPanel.h"
#include "Containers/ContainerAliases.h"

#include "Layout/LayoutUtils.h"

#include <numeric>

void SGridPanel::Construct(FArguments& Args)
{
	for (FSlot& Slot : Args._Slots)
	{
		Children.Add(std::move(Slot));
	}
	Args._Slots.clear();
}

void SGridPanel::SetColumnFill(int32 Column, float Weight)
{
	if (static_cast<int32>(ColumnFill.size()) <= Column)
	{
		ColumnFill.resize(static_cast<SIZE_T>(Column) + 1, 0.0f);
	}
	ColumnFill[Column] = Weight;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SGridPanel::SetRowFill(int32 Row, float Weight)
{
	if (static_cast<int32>(RowFill.size()) <= Row)
	{
		RowFill.resize(static_cast<SIZE_T>(Row) + 1, 0.0f);
	}
	RowFill[Row] = Weight;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SGridPanel::ComputeTracks(TArray<float>& Columns, TArray<float>& Rows) const
{
	int32 NumColumns = static_cast<int32>(ColumnFill.size());
	int32 NumRows = static_cast<int32>(RowFill.size());
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const FSlot& Slot = Children[I];
		NumColumns = FMath::Max(NumColumns, Slot.Column + Slot.ColumnSpanValue);
		NumRows = FMath::Max(NumRows, Slot.Row + Slot.RowSpanValue);
	}
	Columns.assign(static_cast<SIZE_T>(NumColumns), 0.0f);
	Rows.assign(static_cast<SIZE_T>(NumRows), 0.0f);

	// Single-cell children size their tracks; spanning children then widen
	// the tracks they cover evenly if they still do not fit.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 I = 0; I < Children.Num(); ++I)
		{
			const FSlot& Slot = Children[I];
			if (!IsVisibleForLayout(Slot.GetWidget()->GetVisibility()))
			{
				continue;
			}
			const FVector2 Size = Slot.GetWidget()->GetDesiredSize() + Slot.GetPadding().GetDesiredSize();
			const bool bSpansColumns = Slot.ColumnSpanValue > 1;
			const bool bSpansRows = Slot.RowSpanValue > 1;
			if ((Pass == 0) == !bSpansColumns)
			{
				float Have = 0.0f;
				for (int32 C = Slot.Column; C < Slot.Column + Slot.ColumnSpanValue; ++C)
				{
					Have += Columns[C];
				}
				const float Extra = (Size.X - Have) / static_cast<float>(Slot.ColumnSpanValue);
				for (int32 C = Slot.Column; Extra > 0.0f && C < Slot.Column + Slot.ColumnSpanValue; ++C)
				{
					Columns[C] += Extra;
				}
			}
			if ((Pass == 0) == !bSpansRows)
			{
				float Have = 0.0f;
				for (int32 R = Slot.Row; R < Slot.Row + Slot.RowSpanValue; ++R)
				{
					Have += Rows[R];
				}
				const float Extra = (Size.Y - Have) / static_cast<float>(Slot.RowSpanValue);
				for (int32 R = Slot.Row; Extra > 0.0f && R < Slot.Row + Slot.RowSpanValue; ++R)
				{
					Rows[R] += Extra;
				}
			}
		}
	}
}

FVector2 SGridPanel::ComputeDesiredSize(float) const
{
	TArray<float> Columns, Rows;
	ComputeTracks(Columns, Rows);
	return FVector2(
	    std::accumulate(Columns.begin(), Columns.end(), 0.0f), std::accumulate(Rows.begin(), Rows.end(), 0.0f));
}

void SGridPanel::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	TArray<float> Columns, Rows;
	ComputeTracks(Columns, Rows);

	const TFunction<void(TArray<float>&, const TArray<float>&, float)> Distribute =
	    [](TArray<float>& Tracks, const TArray<float>& Fill, float Available)
	{
		const float Used = std::accumulate(Tracks.begin(), Tracks.end(), 0.0f);
		float TotalWeight = 0.0f;
		for (SIZE_T I = 0; I < Fill.size() && I < Tracks.size(); ++I)
		{
			TotalWeight += Fill[I];
		}
		const float Free = Available - Used;
		if (TotalWeight <= 0.0f || Free <= 0.0f)
		{
			return;
		}
		for (SIZE_T I = 0; I < Fill.size() && I < Tracks.size(); ++I)
		{
			Tracks[I] += Free * Fill[I] / TotalWeight;
		}
	};
	Distribute(Columns, ColumnFill, AllottedGeometry.Size.X);
	Distribute(Rows, RowFill, AllottedGeometry.Size.Y);

	TArray<float> ColumnStart(Columns.size() + 1, 0.0f), RowStart(Rows.size() + 1, 0.0f);
	for (SIZE_T I = 0; I < Columns.size(); ++I)
	{
		ColumnStart[I + 1] = ColumnStart[I] + Columns[I];
	}
	for (SIZE_T I = 0; I < Rows.size(); ++I)
	{
		RowStart[I + 1] = RowStart[I] + Rows[I];
	}

	for (int32 I = 0; I < Children.Num(); ++I)
	{
		const FSlot& Slot = Children[I];
		const FVector2 Offset(ColumnStart[Slot.Column], RowStart[Slot.Row]);
		const FVector2 Size(ColumnStart[Slot.Column + Slot.ColumnSpanValue] - Offset.X,
		    RowStart[Slot.Row + Slot.RowSpanValue] - Offset.Y);
		ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Slot, Offset, Size);
	}
}
