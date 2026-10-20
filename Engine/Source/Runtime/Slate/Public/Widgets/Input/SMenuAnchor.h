// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Widgets/Input/SButton.h"

class IMenu;

// Opens a popup menu next to its content (the anchor). The menu is pushed on
// the application's menu stack, so clicking outside it or pressing Escape
// closes it; menus opened from inside it (submenus) close with it.
class SMenuAnchor : public SCompoundWidget
{
public:
	GE_BEGIN_ARGS(SMenuAnchor)
		GE_ARGUMENT(EMenuPlacement, Placement, EMenuPlacement::BelowAnchor)
		GE_NAMED_SLOT(MenuContent)            // fixed menu content
		GE_EVENT(FOnGetContent, OnGetMenuContent) // or built on each open
		GE_EVENT(FOnIsOpenChanged, OnMenuOpenChanged)
		GE_ARGUMENT(bool, FocusMenuOnOpen, true)
		GE_DEFAULT_SLOT(Content)
	GE_END_ARGS()

	void Construct(const FArguments& Args);

	void SetIsOpen(bool bOpen, bool bFocusMenu = true);
	bool IsOpen() const;
	void SetMenuContent(const TSharedRef<SWidget>& Content) { MenuContent = Content.ToSharedPtr(); }
	// Place the menu against this widget instead of the anchor itself (a
	// menu entry whose anchor sits inside its padding).
	void SetPlacementWidget(const TSharedPtr<SWidget>& Widget) { PlacementWidget = Widget; }
	TSharedPtr<IMenu> GetMenu() const { return Menu.lock(); }

	// Where a popup of PopupSize goes for this placement, in the window's
	// logical units (flipped to stay inside the window).
	static FVector2 ComputePopupPosition(const FGeometry& AnchorGeometry, float WindowScale, FVector2 WindowSize, FVector2 PopupSize, EMenuPlacement Placement);

private:
	void OnMenuDismissed();

	EMenuPlacement Placement = EMenuPlacement::BelowAnchor;
	TSharedPtr<SWidget> MenuContent;
	FOnGetContent OnGetMenuContent;
	FOnIsOpenChanged OnMenuOpenChanged;
	bool bFocusMenuOnOpen = true;
	TWeakPtr<IMenu> Menu;
	TWeakPtr<SWidget> PlacementWidget;
};

// A button that opens a menu (drop-down menus, combo pickers).
class SComboButton : public SCompoundWidget
{
public:
	GE_BEGIN_ARGS(SComboButton)
		GE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)
		GE_OPTIONAL_ATTRIBUTE(FString, Text)
		GE_NAMED_SLOT(ButtonContent)
		GE_NAMED_SLOT(MenuContent)
		GE_EVENT(FOnGetContent, OnGetMenuContent)
		GE_EVENT(FOnIsOpenChanged, OnMenuOpenChanged)
		GE_ARGUMENT(EMenuPlacement, Placement, EMenuPlacement::ComboBox)
		GE_ARGUMENT(bool, HasDownArrow, true)
		GE_ARGUMENT(bool, IsFocusable, true)
	GE_END_ARGS()

	void Construct(const FArguments& Args);

	void SetIsOpen(bool bOpen) { Anchor->SetIsOpen(bOpen); }
	bool IsOpen() const { return Anchor->IsOpen(); }
	TSharedPtr<SButton> GetButton() const { return Button; }

private:
	TSharedPtr<SMenuAnchor> Anchor;
	TSharedPtr<SButton> Button;
};

// An SButton styled as a menu entry that dismisses its menu when clicked.
class SMenuEntry : public SButton
{
public:
	GE_BEGIN_ARGS(SMenuEntry)
		GE_ATTRIBUTE(FString, Text, "")
		GE_OPTIONAL_ATTRIBUTE(FString, InputGesture) // shortcut text on the right
		GE_ARGUMENT(bool, ShowCheck, false)          // reserve a check-mark column
		GE_ATTRIBUTE(ECheckBoxState, CheckState, ECheckBoxState::Unchecked)
		GE_EVENT(FSimpleDelegate, OnSelected)
		GE_NAMED_SLOT(SubMenu)                      // makes it open a submenu instead
		GE_EVENT(FOnGetContent, OnGetSubMenu)       // or builds the submenu when it opens
	GE_END_ARGS()

	void Construct(const FArguments& Args);

	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	FReply HandleClicked();

	FSimpleDelegate OnSelected;
	TSharedPtr<SMenuAnchor> SubMenuAnchor;
};
