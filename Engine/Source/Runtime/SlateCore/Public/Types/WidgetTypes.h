// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Small enums shared by the widget core and the widgets
// (Engine/Documentation/Source/GenesisUI.md §6).

#pragma once

#include "CoreMinimal.h"

enum class EVisibility : uint8
{
	Visible,              // painted, takes space, receives input
	Collapsed,            // not painted, takes no space
	Hidden,               // not painted, takes space
	HitTestInvisible,     // painted; neither it nor its children receive input
	SelfHitTestInvisible, // painted; its children receive input, it does not
};

inline constexpr bool IsVisibleForLayout(EVisibility V) { return V != EVisibility::Collapsed; }
inline constexpr bool IsPainted(EVisibility V) { return V != EVisibility::Collapsed && V != EVisibility::Hidden; }
inline constexpr bool IsSelfHitTestable(EVisibility V) { return V == EVisibility::Visible; }
inline constexpr bool AreChildrenHitTestable(EVisibility V) { return V == EVisibility::Visible || V == EVisibility::SelfHitTestInvisible; }

const char* ToString(EVisibility Visibility);

enum class EHorizontalAlignment : uint8
{
	Fill,
	Left,
	Center,
	Right,
};

enum class EVerticalAlignment : uint8
{
	Fill,
	Top,
	Center,
	Bottom,
};

inline constexpr EHorizontalAlignment HAlign_Fill = EHorizontalAlignment::Fill;
inline constexpr EHorizontalAlignment HAlign_Left = EHorizontalAlignment::Left;
inline constexpr EHorizontalAlignment HAlign_Center = EHorizontalAlignment::Center;
inline constexpr EHorizontalAlignment HAlign_Right = EHorizontalAlignment::Right;
inline constexpr EVerticalAlignment VAlign_Fill = EVerticalAlignment::Fill;
inline constexpr EVerticalAlignment VAlign_Top = EVerticalAlignment::Top;
inline constexpr EVerticalAlignment VAlign_Center = EVerticalAlignment::Center;
inline constexpr EVerticalAlignment VAlign_Bottom = EVerticalAlignment::Bottom;

enum class EOrientation : uint8
{
	Horizontal,
	Vertical,
};

enum class EWidgetClipping : uint8
{
	Inherit,      // children may draw outside this widget
	ClipToBounds, // this widget and its children are clipped to its bounds
};

enum class ETextJustify : uint8
{
	Left,
	Center,
	Right,
};

// What a change to a widget invalidates (§6.3). Flags combine.
enum class EInvalidateWidgetReason : uint8
{
	None = 0,
	Layout = 1 << 0,          // desired size and arrangement
	Paint = 1 << 1,           // appearance only
	ChildOrder = 1 << 2,      // children added, removed or reordered
	Visibility = 1 << 3,      // visibility changed
	RenderTransform = 1 << 4, // render transform changed (re-arranges, no re-measure)
};

constexpr EInvalidateWidgetReason operator|(EInvalidateWidgetReason A, EInvalidateWidgetReason B) { return EInvalidateWidgetReason(uint8(A) | uint8(B)); }
constexpr EInvalidateWidgetReason operator&(EInvalidateWidgetReason A, EInvalidateWidgetReason B) { return EInvalidateWidgetReason(uint8(A) & uint8(B)); }
constexpr EInvalidateWidgetReason& operator|=(EInvalidateWidgetReason& A, EInvalidateWidgetReason B) { return A = A | B; }
constexpr bool HasAnyFlags(EInvalidateWidgetReason Value, EInvalidateWidgetReason Flags) { return (uint8(Value) & uint8(Flags)) != 0; }

// Why focus changed.
enum class EFocusCause : uint8
{
	Mouse,          // a click
	Navigation,     // keyboard or gamepad navigation (Tab, arrows, D-pad)
	SetDirectly,    // code asked for it
	Cleared,        // focus was cleared
	OtherWidgetLostFocus, // the focused widget went away (destroyed, hidden, disabled)
	WindowActivate, // the window was activated
};

const char* ToString(EFocusCause Cause);

// Directional and tab navigation (§7.4).
enum class EUINavigation : uint8
{
	Left,
	Right,
	Up,
	Down,
	Next,     // Tab
	Previous, // Shift+Tab
	Invalid,
};

const char* ToString(EUINavigation Navigation);

enum class EUINavigationAction : uint8
{
	Accept, // Enter, Space, gamepad A
	Back,   // Escape, gamepad B
	Invalid,
};

// Where a navigation event came from.
enum class ENavigationGenesis : uint8
{
	Keyboard,
	Controller,
	User, // code
};

enum class ESelectionMode : uint8
{
	None,
	Single,
	SingleToggle, // clicking the selected item deselects it
	Multi,
};

enum class ESelectInfo : uint8
{
	OnKeyPress,
	OnNavigation,
	OnMouseClick,
	Direct,
};

// How a text edit was committed.
enum class ETextCommit : uint8
{
	Default,       // programmatic
	OnEnter,
	OnUserMovedFocus,
	OnCleared,     // Escape with revert
};

enum class ECheckBoxState : uint8
{
	Unchecked,
	Checked,
	Undetermined,
};

// Where a popup opens relative to its anchor.
enum class EMenuPlacement : uint8
{
	BelowAnchor,
	AboveAnchor,
	RightOfAnchor, // submenus
	LeftOfAnchor,
	ComboBox,      // below, at least as wide as the anchor
	CenteredBelowAnchor,
};

enum class EActiveTimerReturn : uint8
{
	Continue,
	Stop,
};
