// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Views/STableViewBase.h"

#include "Widgets/Layout/SBoxPanel.h"

// The clipped area holding the generated rows, stacked from the first
// visible one (shifted up by the fractional part of the scroll offset).
class STableViewBase::SListPanel final : public SPanel
{
public:
	GE_BEGIN_ARGS(SListPanel)
	GE_END_ARGS()

	void Construct(const FArguments&, STableViewBase* InOwner) { Owner = InOwner; }

	FChildren* GetChildren() override { return &Children; }

	// Replaces the rows if they changed; heights are local units.
	void SetRows(const TArray<TPair<TSharedRef<SWidget>, float>>& Rows, float InFirstRowOffset)
	{
		bool bSame = static_cast<int32>(Rows.size()) == Children.Num();
		for (int32 I = 0; bSame && I < Children.Num(); ++I)
		{
			bSame = &Children[I].GetWidget().Get() == &Rows[I].first.Get();
		}
		Heights.clear();
		for (const auto& Row : Rows)
		{
			Heights.push_back(Row.second);
		}
		FirstRowOffset = InFirstRowOffset;
		if (!bSame)
		{
			Children.Empty();
			for (const auto& Row : Rows)
			{
				FSimpleSlot Slot;
				Slot[Row.first];
				Children.Add(std::move(Slot));
			}
		}
	}

	TPanelChildren<FSimpleSlot> Children{ this };

protected:
	FVector2 ComputeDesiredSize(float) const override
	{
		// Wide enough for the rows we have; as tall as all items would be
		// (matters only where the list sizes to its content, e.g. a drop-down).
		float Width = 0.0f;
		for (int32 I = 0; I < Children.Num(); ++I)
		{
			Width = FMath::Max(Width, Children[I].GetWidget()->GetDesiredSize().X);
		}
		return FVector2(Width, Owner->GetEstimatedContentHeight());
	}

	void OnPreArrange(const FGeometry& AllottedGeometry) override { Owner->GeneratePanelRows(AllottedGeometry); }

	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
	{
		float Y = -FirstRowOffset;
		for (int32 I = 0; I < Children.Num() && I < static_cast<int32>(Heights.size()); ++I)
		{
			const TSharedRef<SWidget>& Row = Children[I].GetWidget();
			ArrangedChildren.AddWidget(Row->GetVisibility(), FArrangedWidget(Row, AllottedGeometry.MakeChild(FVector2(0.0f, Y), FVector2(AllottedGeometry.Size.X, Heights[I]))));
			Y += Heights[I];
		}
	}

private:
	STableViewBase* Owner = nullptr;
	TArray<float> Heights;
	float FirstRowOffset = 0.0f;
};

void STableViewBase::ConstructTableView(const FScrollBarStyle* ScrollBarStyle, float InWheelScrollAmount, EVisibility ScrollBarVisibility)
{
	WheelScrollAmount = InWheelScrollAmount;
	bScrollBarAllowed = ScrollBarVisibility != EVisibility::Collapsed;
	SAssignNew(Panel, SListPanel, this).Clipping(EWidgetClipping::ClipToBounds);
	SAssignNew(ScrollBar, SScrollBar)
		.Style(ScrollBarStyle)
		.Visibility(EVisibility::Collapsed)
		.OnUserScrolled([this](float Fraction) { SetScrollOffset(Fraction * static_cast<double>(GetNumItems())); });
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)[ToSharedRef(StaticCastSharedPtr<SWidget>(Panel))]
		+ SHorizontalBox::Slot().AutoWidth()[ToSharedRef(StaticCastSharedPtr<SWidget>(ScrollBar))]
	];
}

int32 STableViewBase::GetNumGeneratedRows() const
{
	return Panel ? Panel->Children.Num() : 0;
}

void STableViewBase::SetScrollOffset(double InOffset)
{
	const double Clamped = FMath::Clamp(InOffset, 0.0, FMath::Max(0.0, static_cast<double>(GetNumItems()) - 1.0));
	if (Clamped != ScrollOffset)
	{
		ScrollOffset = Clamped;
		Panel->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void STableViewBase::RequestScrollIntoViewIndex(int32 Index)
{
	PendingScrollIntoView = Index;
	Panel->Invalidate(EInvalidateWidgetReason::Layout);
}

void STableViewBase::RebuildAllRows()
{
	bRebuildRows = true;
	Panel->Invalidate(EInvalidateWidgetReason::Layout);
}

void STableViewBase::RefreshRows()
{
	Panel->Invalidate(EInvalidateWidgetReason::Layout);
}

FReply STableViewBase::OnMouseWheel(const FGeometry&, const FPointerEvent& Event)
{
	const double Before = ScrollOffset;
	SetScrollOffset(ScrollOffset - Event.GetWheelDelta() * WheelScrollAmount / FMath::Max(1.0f, AverageRowHeight));
	return ScrollOffset != Before ? FReply::Handled() : FReply::Unhandled();
}

float STableViewBase::GenerateFrom(double Offset, float ViewHeight, float LayoutScale, TArray<TPair<TSharedRef<SWidget>, float>>& OutRows)
{
	OutRows.clear();
	const int32 Count = GetNumItems();
	const int32 First = static_cast<int32>(Offset);
	float Y = 0.0f;
	for (int32 I = First; I < Count; ++I)
	{
		const TSharedRef<SWidget> Row = GetRowWidgetForIndex(I);
		Row->Measure(LayoutScale);
		const float Height = FMath::Max(1.0f, Row->GetDesiredSize().Y);
		if (I == First)
		{
			Y -= static_cast<float>(Offset - First) * Height;
		}
		OutRows.push_back({ Row, Height });
		Y += Height;
		if (Y >= ViewHeight)
		{
			break;
		}
	}
	return Y;
}

void STableViewBase::GeneratePanelRows(const FGeometry& PanelGeometry)
{
	const float ViewHeight = PanelGeometry.Size.Y;
	const float LayoutScale = PanelGeometry.Scale;
	const int32 Count = GetNumItems();
	if (bRebuildRows)
	{
		// Forget every row so all are generated again.
		BeginGeneration();
		EndGeneration();
		bRebuildRows = false;
	}
	ScrollOffset = FMath::Clamp(ScrollOffset, 0.0, FMath::Max(0.0, static_cast<double>(Count) - 1.0));

	TArray<TPair<TSharedRef<SWidget>, float>> Rows;
	BeginGeneration();
	float Covered = GenerateFrom(ScrollOffset, ViewHeight, LayoutScale, Rows);

	// Past the end: pull the offset back so the last row sits at the bottom.
	if (Covered < ViewHeight && ScrollOffset > 0.0)
	{
		float Fill = 0.0f;
		double NewOffset = 0.0;
		for (int32 I = Count - 1; I >= 0; --I)
		{
			const TSharedRef<SWidget> Row = GetRowWidgetForIndex(I);
			Row->Measure(LayoutScale);
			const float Height = FMath::Max(1.0f, Row->GetDesiredSize().Y);
			if (Fill + Height >= ViewHeight)
			{
				NewOffset = I + static_cast<double>((Fill + Height - ViewHeight) / Height);
				break;
			}
			Fill += Height;
		}
		ScrollOffset = FMath::Min(ScrollOffset, NewOffset);
		Covered = GenerateFrom(ScrollOffset, ViewHeight, LayoutScale, Rows);
	}

	// Scroll a requested item into view.
	if (PendingScrollIntoView != INDEX_NONE && PendingScrollIntoView < Count)
	{
		const int32 Target = PendingScrollIntoView;
		PendingScrollIntoView = INDEX_NONE;
		const int32 First = static_cast<int32>(ScrollOffset);
		bool bFullyVisible = false;
		float Y = 0.0f;
		for (SIZE_T R = 0; R < Rows.size(); ++R)
		{
			const float Top = (R == 0) ? -static_cast<float>(ScrollOffset - First) * Rows[0].second : Y;
			Y = Top + Rows[R].second;
			if (First + static_cast<int32>(R) == Target)
			{
				bFullyVisible = Top >= -0.01f && Y <= ViewHeight + 0.01f;
			}
		}
		if (!bFullyVisible)
		{
			if (Target <= First)
			{
				ScrollOffset = Target;
			}
			else
			{
				// Put the target's bottom at the bottom of the view.
				float Fill = 0.0f;
				double NewOffset = Target;
				for (int32 I = Target; I >= 0; --I)
				{
					const TSharedRef<SWidget> Row = GetRowWidgetForIndex(I);
					Row->Measure(LayoutScale);
					const float Height = FMath::Max(1.0f, Row->GetDesiredSize().Y);
					if (Fill + Height >= ViewHeight)
					{
						NewOffset = I + static_cast<double>((Fill + Height - ViewHeight) / Height);
						break;
					}
					Fill += Height;
					NewOffset = I;
				}
				ScrollOffset = NewOffset;
			}
			Covered = GenerateFrom(ScrollOffset, ViewHeight, LayoutScale, Rows);
		}
	}
	EndGeneration();

	float Total = 0.0f;
	for (const auto& Row : Rows)
	{
		Total += Row.second;
	}
	if (!Rows.empty())
	{
		AverageRowHeight = Total / static_cast<float>(Rows.size());
	}
	const float FirstOffset = Rows.empty() ? 0.0f : static_cast<float>(ScrollOffset - static_cast<int32>(ScrollOffset)) * Rows[0].second;
	Panel->SetRows(Rows, FirstOffset);
	(void)Covered;
	UpdateScrollBar(ViewHeight);
}

void STableViewBase::UpdateScrollBar(float ViewHeight)
{
	const int32 Count = GetNumItems();
	const float Visible = ViewHeight / FMath::Max(1.0f, AverageRowHeight);
	const float Thumb = Count > 0 ? FMath::Min(1.0f, Visible / static_cast<float>(Count)) : 1.0f;
	ScrollBar->SetState(Count > 0 ? static_cast<float>(ScrollOffset / Count) : 0.0f, Thumb);
	const EVisibility Wanted = bScrollBarAllowed && Thumb < 1.0f ? EVisibility::Visible : EVisibility::Collapsed;
	if (ScrollBar->GetVisibility() != Wanted)
	{
		ScrollBar->SetVisibility(Wanted);
	}
}

int32 STableViewBase::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	(void)Geometry;
	return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId, MakeChildStyle(InWidgetStyle));
}
