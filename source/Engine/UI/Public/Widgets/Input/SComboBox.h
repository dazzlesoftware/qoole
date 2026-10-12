// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SComboBox<T>: pick one of a list of options. The button shows its content
// (usually bound to the selection); the drop-down is a list view of the
// options. Arrows move through the list, Enter or a click picks, Escape
// closes.

#pragma once

#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Views/SListView.h"

template <typename OptionType>
class SComboBox : public SCompoundWidget
{
public:
	using FOnGenerateWidget = TDelegate<TSharedRef<SWidget>(OptionType)>;
	using FOnSelectionChanged = TDelegate<void(OptionType, ESelectInfo)>;

	GE_BEGIN_ARGS(SComboBox)
		GE_ARGUMENT(const TArray<OptionType>*, OptionsSource, nullptr)
		GE_EVENT(FOnGenerateWidget, OnGenerateWidget)
		GE_EVENT(FOnSelectionChanged, OnSelectionChanged)
		GE_ARGUMENT(TOptional<OptionType>, InitiallySelectedItem, TOptional<OptionType>())
		GE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)
		GE_ARGUMENT(float, MaxListHeight, 320.0f)
		GE_ARGUMENT(bool, IsFocusable, true)
		GE_DEFAULT_SLOT(Content)
	GE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Options = Args._OptionsSource;
		OnGenerateWidget = Args._OnGenerateWidget;
		OnSelectionChanged = Args._OnSelectionChanged;
		Selected = Args._InitiallySelectedItem;
		MaxListHeight = Args._MaxListHeight;
		ChildSlot
		[
			SAssignNew(Combo, SComboButton)
			.ButtonStyle(Args._ButtonStyle)
			.IsFocusable(Args._IsFocusable)
			.ButtonContent(Args._Content.Widget ? ToSharedRef(Args._Content.Widget) : SNullWidget::NullWidget())
			.OnGetMenuContent([this] { return MakeMenu(); })
		];
	}

	TOptional<OptionType> GetSelectedItem() const { return Selected; }
	void SetSelectedItem(const OptionType& Item) { Select(Item, ESelectInfo::Direct); }
	void ClearSelection() { Selected.reset(); }
	bool IsOpen() const { return Combo->IsOpen(); }
	void SetIsOpen(bool bOpen) { Combo->SetIsOpen(bOpen); }
	// The open drop-down's list (for tests).
	TSharedPtr<SListView<OptionType>> GetList() const { return List.lock(); }

private:
	// Commits on Enter; arrows move the list's selection without committing.
	class SComboList : public SCompoundWidget
	{
	public:
		GE_BEGIN_ARGS(SComboList)
			GE_DEFAULT_SLOT(Content)
		GE_END_ARGS()

		void Construct(const FArguments& Args, SComboBox* InOwner)
		{
			Owner = InOwner;
			ChildSlot[ToSharedRef(Args._Content.Widget)];
		}

		FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
		{
			if (Event.GetKey() == EKey::Enter || Event.GetGamepadButton() == EGamepadButton::FaceBottom)
			{
				Owner->CommitListSelection();
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

	private:
		SComboBox* Owner = nullptr;
	};

	TSharedRef<SWidget> MakeMenu()
	{
		TSharedPtr<SListView<OptionType>> NewList;
		const TSharedRef<SWidget> Menu = SNew(SComboList, this)
		[
			SNew(SBox).MaxDesiredHeight(TOptional<float>(MaxListHeight))
			[
				SAssignNew(NewList, SListView<OptionType>)
				.ListItemsSource(Options)
				.OnGenerateRow([this](OptionType Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
				{
					return SNew(STableRow<OptionType>, Owner).Padding(FMargin(8.0f, 3.0f))[OnGenerateWidget.IsBound() ? OnGenerateWidget.Execute(Item) : SNullWidget::NullWidget()];
				})
				.OnSelectionChanged([this](OptionType Item, ESelectInfo Info)
				{
					if (Info == ESelectInfo::OnMouseClick)
					{
						Select(Item, Info);
						Combo->SetIsOpen(false);
					}
				})
			]
		];
		if (Selected)
		{
			NewList->SetSelection(*Selected, ESelectInfo::Direct);
			NewList->RequestScrollIntoView(*Selected);
		}
		List = NewList;
		// Keys go straight to the list once the menu is up (the menu itself
		// takes focus when it opens, this frame).
		const TWeakPtr<SWidget> WeakList = StaticCastSharedPtr<SWidget>(NewList);
		Menu->RegisterActiveTimer(0.0f, [WeakList](double, float)
		{
			if (const TSharedPtr<SWidget> Pinned = WeakList.lock())
			{
				FUIApplication::Get().SetKeyboardFocus(Pinned, EFocusCause::SetDirectly);
			}
			return EActiveTimerReturn::Stop;
		});
		return Menu;
	}

	void CommitListSelection()
	{
		if (const TSharedPtr<SListView<OptionType>> Pinned = List.lock())
		{
			const TArray<OptionType> Items = Pinned->GetSelectedItems();
			if (!Items.empty())
			{
				Select(Items[0], ESelectInfo::OnKeyPress);
			}
		}
		Combo->SetIsOpen(false);
	}

	void Select(const OptionType& Item, ESelectInfo Info)
	{
		Selected = Item;
		Invalidate(EInvalidateWidgetReason::Layout);
		OnSelectionChanged.ExecuteIfBound(Item, Info);
	}

	const TArray<OptionType>* Options = nullptr;
	FOnGenerateWidget OnGenerateWidget;
	FOnSelectionChanged OnSelectionChanged;
	TOptional<OptionType> Selected;
	float MaxListHeight = 320.0f;
	TSharedPtr<SComboButton> Combo;
	TWeakPtr<SListView<OptionType>> List;
};
