// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Styling/SlateStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
namespace MapFeaturePanelWidgets
{
// A header bar, as the mode panels have.
inline TSharedRef<SWidget> PanelHeader(const char* Title)
{
	return SNew(SBorder)
		.BorderImage(&FAppStyle::Get().GetBrush("Header"))
		.Padding(FMargin(8.0f, 4.0f))
			[SNew(STextBlock).Text(Title).TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))];
}

template <typename T>
TSharedRef<SWidget> SpinRow(const char* Label, const char* Tip, TFunction<T()> Get, TFunction<void(T)> Set, T Min,
							T Max, int32 Digits)
{
	return SNew(SHorizontalBox).ToolTipText(Tip) +
		   SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Label)] +
		   SHorizontalBox::Slot().FillWidth(0.5f)[SNew(SSpinBox<T>)
													  .MinValue(Min)
													  .MaxValue(Max)
													  .FractionalDigits(Digits)
													  .Value([Get] { return TOptional<T>(Get()); })
													  .OnValueChanged([Set](T Value) { Set(Value); })];
}

// A button that shows when its choice is the current one.
class SChoiceButton final : public SCompoundWidget
{
  public:
	SLATE_BEGIN_ARGS(SChoiceButton)
	SLATE_ATTRIBUTE(bool, IsChosen, false)
	SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const FString& Label, const FString& Tip)
	{
		IsChosen.Assign(Args._IsChosen);
		const FSimpleDelegate Clicked = Args._OnClicked;
		ChildSlot[SNew(SButton).IsFocusable(false).Text(Label).ToolTipText(Tip).OnClicked([Clicked] {
			Clicked.ExecuteIfBound();
			return FReply::Handled();
		})];
	}

  protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
				  FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
	{
		const int32 Top = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Out, LayerId, Style);
		if (IsChosen.Get())
		{
			Out.AddRoundedBox(Top + 1, Geometry.ToPaintGeometry(), FColor(0, 0, 0, 0), 4.0f,
							  FSlateColor(EStyleColor::Primary).Resolve(), 2.0f);
		}
		return Top + 1;
	}

  private:
	TWidgetAttribute<bool, EInvalidateWidgetReason::Paint> IsChosen{*this, false};
};
} // namespace MapFeaturePanelWidgets
