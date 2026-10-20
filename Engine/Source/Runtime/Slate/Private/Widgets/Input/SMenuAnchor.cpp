// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Input/SMenuAnchor.h"

#include "Application/UIApplication.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

// ---------------------------------------------------------------------------
// SToolTip

void SToolTip::Construct(const FArguments& Args)
{
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FToolTipStyle>("ToolTip");
	Text = Args._Text;
	ChildSlot.Padding(Style->Padding);
	if (Args._Content.Widget)
	{
		bHasCustomContent = true;
		ChildSlot[ToSharedRef(Args._Content.Widget)];
	}
	else
	{
		ChildSlot[SNew(STextBlock).Text(Args._Text).TextStyle(&Style->Text).WrapTextAt(Style->WrapWidth)];
	}
	SetVisibility(EVisibility::HitTestInvisible);
}

bool SToolTip::IsEmpty() const
{
	return !bHasCustomContent && Text.Get().IsEmpty();
}

int32 SToolTip::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	Style->Background.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
	return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, MakeChildStyle(InWidgetStyle));
}

// ---------------------------------------------------------------------------
// SMenuAnchor

void SMenuAnchor::Construct(const FArguments& Args)
{
	Placement = Args._Placement;
	MenuContent = Args._MenuContent.Widget;
	OnGetMenuContent = Args._OnGetMenuContent;
	OnMenuOpenChanged = Args._OnMenuOpenChanged;
	bFocusMenuOnOpen = Args._FocusMenuOnOpen;
	if (Args._Content.Widget)
	{
		ChildSlot[ToSharedRef(Args._Content.Widget)];
	}
}

bool SMenuAnchor::IsOpen() const
{
	const TSharedPtr<IMenu> Pinned = Menu.lock();
	return Pinned && !Pinned->IsDismissed();
}

FVector2 SMenuAnchor::ComputePopupPosition(const FGeometry& AnchorGeometry, float WindowScale, FVector2 WindowSize, FVector2 PopupSize, EMenuPlacement Placement)
{
	const FRect Anchor = AnchorGeometry.GetLayoutBoundingRect().Scale(1.0f / WindowScale);
	FVector2 Position;
	switch (Placement)
	{
	case EMenuPlacement::AboveAnchor:
		Position = FVector2(Anchor.Left, Anchor.Top - PopupSize.Y);
		if (Position.Y < 0.0f && Anchor.Bottom + PopupSize.Y <= WindowSize.Y)
		{
			Position.Y = Anchor.Bottom;
		}
		break;
	case EMenuPlacement::RightOfAnchor:
		Position = FVector2(Anchor.Right, Anchor.Top);
		if (Position.X + PopupSize.X > WindowSize.X && Anchor.Left - PopupSize.X >= 0.0f)
		{
			Position.X = Anchor.Left - PopupSize.X;
		}
		break;
	case EMenuPlacement::LeftOfAnchor:
		Position = FVector2(Anchor.Left - PopupSize.X, Anchor.Top);
		if (Position.X < 0.0f && Anchor.Right + PopupSize.X <= WindowSize.X)
		{
			Position.X = Anchor.Right;
		}
		break;
	case EMenuPlacement::CenteredBelowAnchor:
		Position = FVector2(Anchor.GetCenter().X - PopupSize.X * 0.5f, Anchor.Bottom);
		break;
	case EMenuPlacement::BelowAnchor:
	case EMenuPlacement::ComboBox:
	default:
		Position = FVector2(Anchor.Left, Anchor.Bottom);
		break;
	}
	// Below-type placements open upwards when there is no room below.
	if ((Placement == EMenuPlacement::BelowAnchor || Placement == EMenuPlacement::ComboBox || Placement == EMenuPlacement::CenteredBelowAnchor)
		&& Position.Y + PopupSize.Y > WindowSize.Y && Anchor.Top - PopupSize.Y >= 0.0f)
	{
		Position.Y = Anchor.Top - PopupSize.Y;
	}
	return Position;
}

void SMenuAnchor::SetIsOpen(bool bOpen, bool bFocusMenu)
{
	if (bOpen == IsOpen())
	{
		return;
	}
	FUIApplication& App = FUIApplication::Get();
	if (!bOpen)
	{
		if (const TSharedPtr<IMenu> Pinned = Menu.lock())
		{
			App.DismissMenuAndChildren(*Pinned);
		}
		return;
	}

	TSharedPtr<SWidget> Content = OnGetMenuContent.IsBound() ? OnGetMenuContent.Execute().ToSharedPtr() : MenuContent;
	const TSharedPtr<SWindow> Window = App.FindWindowForWidget(*this);
	if (!Content || !Window)
	{
		return;
	}
	if (Placement == EMenuPlacement::ComboBox)
	{
		// At least as wide as the anchor.
		const float AnchorWidth = GetCachedGeometry().GetLayoutBoundingRect().Width() / Window->GetScale();
		Content = SNew(SBox).MinDesiredWidth(TOptional<float>(AnchorWidth))[ToSharedRef(Content)];
	}

	const TSharedRef<IMenu> NewMenu = App.PushMenu(SharedThis(this), ToSharedRef(Content), FVector2::Zero(), bFocusMenu && bFocusMenuOnOpen);
	// Place it now that the menu (with its background) can be measured.
	const TSharedRef<SWidget> Host = NewMenu->GetContent();
	Host->Measure(Window->GetScale());
	const TSharedPtr<SWidget> PlaceAgainst = PlacementWidget.lock();
	const FGeometry& AnchorGeometry = PlaceAgainst ? PlaceAgainst->GetCachedGeometry() : GetCachedGeometry();
	const FVector2 Position = ComputePopupPosition(AnchorGeometry, Window->GetScale(), Window->GetLogicalSize(), Host->GetDesiredSize(), Placement);
	Window->MovePopup(Host.Get(), Position);

	Menu = NewMenu.ToSharedPtr();
	TWeakPtr<SWidget> WeakThis = weak_from_this();
	NewMenu->OnMenuDismissed.AddLambda([WeakThis](const TSharedRef<IMenu>&)
	{
		if (const TSharedPtr<SWidget> Pinned = WeakThis.lock())
		{
			static_cast<SMenuAnchor*>(Pinned.get())->OnMenuDismissed();
		}
	});
	OnMenuOpenChanged.ExecuteIfBound(true);
}

void SMenuAnchor::OnMenuDismissed()
{
	Menu.reset();
	OnMenuOpenChanged.ExecuteIfBound(false);
}

// ---------------------------------------------------------------------------
// SComboButton

namespace
{
	// A small downward chevron.
	class SDownArrow final : public SLeafWidget
	{
	public:
		GE_BEGIN_ARGS(SDownArrow)
		GE_END_ARGS()
		void Construct(const FArguments&) {}

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(10.0f, 10.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FDrawElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			const FVector2 C = Geometry.Size * 0.5f;
			Out.AddLines(LayerId, Geometry.ToPaintGeometry(), { C + FVector2(-3.5f, -1.5f), C + FVector2(0.0f, 2.0f), C + FVector2(3.5f, -1.5f) }, Style.GetForegroundColor(), 1.5f);
			return LayerId;
		}
	};

	// The check mark of checkable menu entries (empty when unchecked).
	class SCheckMark final : public SLeafWidget
	{
	public:
		GE_BEGIN_ARGS(SCheckMark)
			GE_ATTRIBUTE(ECheckBoxState, State, ECheckBoxState::Unchecked)
		GE_END_ARGS()
		void Construct(const FArguments& Args) { State.Assign(Args._State); }

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(14.0f, 14.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FDrawElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			const float S = Geometry.Size.Y;
			if (State.Get() == ECheckBoxState::Checked)
			{
				Out.AddLines(LayerId, Geometry.ToPaintGeometry(), { FVector2(S * 0.18f, S * 0.52f), FVector2(S * 0.42f, S * 0.76f), FVector2(S * 0.84f, S * 0.28f) }, Style.GetForegroundColor(), 1.6f);
			}
			else if (State.Get() == ECheckBoxState::Undetermined)
			{
				Out.AddLines(LayerId, Geometry.ToPaintGeometry(), { FVector2(S * 0.25f, S * 0.5f), FVector2(S * 0.75f, S * 0.5f) }, Style.GetForegroundColor(), 1.6f);
			}
			return LayerId;
		}

	private:
		TWidgetAttribute<ECheckBoxState, EInvalidateWidgetReason::Paint> State{ *this, ECheckBoxState::Unchecked };
	};

	// A small right-pointing chevron (submenu entries).
	class SRightArrow final : public SLeafWidget
	{
	public:
		GE_BEGIN_ARGS(SRightArrow)
		GE_END_ARGS()
		void Construct(const FArguments&) {}

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(10.0f, 10.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FDrawElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			const FVector2 C = Geometry.Size * 0.5f;
			Out.AddLines(LayerId, Geometry.ToPaintGeometry(), { C + FVector2(-1.5f, -3.5f), C + FVector2(2.0f, 0.0f), C + FVector2(-1.5f, 3.5f) }, Style.GetForegroundColor(), 1.5f);
			return LayerId;
		}
	};
}

void SComboButton::Construct(const FArguments& Args)
{
	TSharedPtr<SWidget> ButtonContent = Args._ButtonContent.Widget;
	if (!ButtonContent)
	{
		ButtonContent = SNew(STextBlock).Text(Args._Text.IsSet() ? Args._Text : TAttribute<FString>(""));
	}
	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ToSharedRef(ButtonContent)];
	if (Args._HasDownArrow)
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))[SNew(SDownArrow)];
	}

	ChildSlot
	[
		SAssignNew(Anchor, SMenuAnchor)
		.Placement(Args._Placement)
		.MenuContent(Args._MenuContent.Widget ? ToSharedRef(Args._MenuContent.Widget) : SNullWidget::NullWidget())
		.OnGetMenuContent(Args._OnGetMenuContent)
		.OnMenuOpenChanged(Args._OnMenuOpenChanged)
		[
			SAssignNew(Button, SButton)
			.ButtonStyle(Args._ButtonStyle)
			.IsFocusable(Args._IsFocusable)
			.HAlign(HAlign_Fill)
			.OnClicked([this]()
			{
				Anchor->SetIsOpen(!Anchor->IsOpen());
				return FReply::Handled();
			})
			[
				Row
			]
		]
	];
}

// ---------------------------------------------------------------------------
// SMenuEntry

void SMenuEntry::Construct(const FArguments& Args)
{
	OnSelected = Args._OnSelected;

	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (Args._ShowCheck)
	{
		const TAttribute<ECheckBoxState> CheckState = Args._CheckState;
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))[SNew(SCheckMark).State(CheckState)];
	}
	Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Args._Text)];
	if (Args._InputGesture.IsSet())
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(24.0f, 0.0f, 0.0f, 0.0f))
			[SNew(STextBlock).Text(Args._InputGesture).ColorAndOpacity(FStyleColor(EStyleColor::ForegroundSecondary))];
	}
	TSharedRef<SWidget> Content = Row;
	if (Args._SubMenu.Widget || Args._OnGetSubMenu.IsBound())
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(16.0f, 0.0f, 0.0f, 0.0f))[SNew(SRightArrow)];
		SAssignNew(SubMenuAnchor, SMenuAnchor).Placement(EMenuPlacement::RightOfAnchor).OnGetMenuContent(Args._OnGetSubMenu)[Row];
		if (Args._SubMenu.Widget)
		{
			SubMenuAnchor->SetMenuContent(ToSharedRef(Args._SubMenu.Widget));
		}
		Content = ToSharedRef(StaticCastSharedPtr<SWidget>(SubMenuAnchor));
	}

	SButton::FArguments ButtonArgs;
	ButtonArgs.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FMenuStyle>("Menu").Item)
		.HAlign(HAlign_Fill)
		.OnClicked(this, &SMenuEntry::HandleClicked);
	ButtonArgs._Content.Widget = Content.ToSharedPtr();
	SButton::Construct(ButtonArgs);
	if (SubMenuAnchor)
	{
		SubMenuAnchor->SetPlacementWidget(shared_from_this());
	}
}

FReply SMenuEntry::HandleClicked()
{
	if (SubMenuAnchor)
	{
		SubMenuAnchor->SetIsOpen(true);
		return FReply::Handled();
	}
	OnSelected.ExecuteIfBound();
	FUIApplication::Get().DismissAllMenus();
	return FReply::Handled();
}

FReply SMenuEntry::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (SubMenuAnchor && Event.GetKey() == EKey::Right)
	{
		SubMenuAnchor->SetIsOpen(true, true);
		return FReply::Handled();
	}
	return SButton::OnKeyDown(Geometry, Event);
}

void SMenuEntry::OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event)
{
	SButton::OnMouseEnter(Geometry, Event);
	FUIApplication& App = FUIApplication::Get();
	// Hovering an entry closes submenus of its siblings and opens its own.
	if (const TSharedPtr<IMenu> Menu = App.FindMenuContaining(*this))
	{
		App.DismissMenusAfter(*Menu);
	}
	if (SubMenuAnchor)
	{
		SubMenuAnchor->SetIsOpen(true, false);
	}
}
