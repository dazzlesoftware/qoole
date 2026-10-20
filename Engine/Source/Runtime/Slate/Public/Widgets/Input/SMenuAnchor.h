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
	SLATE_BEGIN_ARGS(SMenuAnchor)
		SLATE_ARGUMENT(EMenuPlacement, Placement, EMenuPlacement::BelowAnchor)
		SLATE_NAMED_SLOT(MenuContent)            // fixed menu content
		SLATE_EVENT(FOnGetContent, OnGetMenuContent) // or built on each open
		SLATE_EVENT(FOnIsOpenChanged, OnMenuOpenChanged)
		SLATE_ARGUMENT(bool, FocusMenuOnOpen, true)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

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
	SLATE_BEGIN_ARGS(SComboButton)
		SLATE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)
		SLATE_OPTIONAL_ATTRIBUTE(FString, Text)
		SLATE_NAMED_SLOT(ButtonContent)
		SLATE_NAMED_SLOT(MenuContent)
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
		SLATE_EVENT(FOnIsOpenChanged, OnMenuOpenChanged)
		SLATE_ARGUMENT(EMenuPlacement, Placement, EMenuPlacement::ComboBox)
		SLATE_ARGUMENT(bool, HasDownArrow, true)
		SLATE_ARGUMENT(bool, IsFocusable, true)
	SLATE_END_ARGS()

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
	SLATE_BEGIN_ARGS(SMenuEntry)
		SLATE_ATTRIBUTE(FString, Text, "")
		SLATE_OPTIONAL_ATTRIBUTE(FString, InputGesture) // shortcut text on the right
		SLATE_ARGUMENT(bool, ShowCheck, false)          // reserve a check-mark column
		SLATE_ATTRIBUTE(ECheckBoxState, CheckState, ECheckBoxState::Unchecked)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
		SLATE_NAMED_SLOT(SubMenu)                      // makes it open a submenu instead
		SLATE_EVENT(FOnGetContent, OnGetSubMenu)       // or builds the submenu when it opens
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	FReply HandleClicked();

	FSimpleDelegate OnSelected;
	TSharedPtr<SMenuAnchor> SubMenuAnchor;
};
