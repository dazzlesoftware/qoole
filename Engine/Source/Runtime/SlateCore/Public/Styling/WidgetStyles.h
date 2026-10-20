// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Per-widget style structs (docs/GenesisUI.md §8). Widgets take a pointer to
// one as an argument and fall back to the application style by name.

#pragma once

#include "Fonts/FontCache.h"
#include "Styling/StyleTypes.h"

struct FTextBlockStyle
{
	FFontInfo Font;
	FStyleColor ColorAndOpacity = EStyleColor::Foreground;
	FVector2 ShadowOffset;      // logical units; zero = no shadow
	FStyleColor ShadowColor = FStyleColor(FColor(0, 0, 0, 160));
	FStyleColor HighlightColor = EStyleColor::Primary; // search-match highlight

	FTextBlockStyle& SetFont(const FFontInfo& InFont) { Font = InFont; return *this; }
	FTextBlockStyle& SetFontSize(float Size) { Font.Size = Size; return *this; }
	FTextBlockStyle& SetColor(FStyleColor Color) { ColorAndOpacity = Color; return *this; }
};

struct FButtonStyle
{
	FStyleBrush Normal;
	FStyleBrush Hovered;
	FStyleBrush Pressed;
	FStyleBrush Disabled;
	FStyleBrush Focused; // drawn over the button when it has keyboard focus via navigation
	FMargin NormalPadding = FMargin(10.0f, 4.0f);
	FMargin PressedPadding = FMargin(10.0f, 5.0f, 10.0f, 3.0f);
	FStyleColor NormalForeground = EStyleColor::Foreground;
	FStyleColor HoveredForeground = EStyleColor::ForegroundHover;
	FStyleColor PressedForeground = EStyleColor::ForegroundHover;
	FStyleColor DisabledForeground = EStyleColor::ForegroundDisabled;
};

struct FCheckBoxStyle
{
	float BoxSize = 16.0f;
	float Spacing = 6.0f; // between the box and the content
	FStyleBrush Box;
	FStyleBrush BoxHovered;
	FStyleBrush BoxChecked;
	FStyleBrush BoxCheckedHovered;
	FStyleBrush BoxDisabled;
	FStyleColor CheckColor = EStyleColor::ForegroundInverted;
	FStyleBrush Focused;
	FStyleColor Foreground = EStyleColor::Foreground;
	FStyleColor DisabledForeground = EStyleColor::ForegroundDisabled;
};

struct FEditableTextStyle
{
	FFontInfo Font;
	FStyleColor Color = EStyleColor::Foreground;
	FStyleColor HintColor = EStyleColor::ForegroundSecondary;
	FStyleColor CaretColor = EStyleColor::Foreground;
	FStyleColor SelectionColor = EStyleColor::Select;
	FStyleColor InactiveSelectionColor = EStyleColor::SelectInactive;
	FStyleColor CompositionUnderline = EStyleColor::Foreground;
	float CaretWidth = 1.0f;
};

struct FEditableTextBoxStyle
{
	FStyleBrush Normal;
	FStyleBrush Hovered;
	FStyleBrush Focused;
	FStyleBrush ReadOnly;
	FMargin Padding = FMargin(6.0f, 4.0f);
	FEditableTextStyle Text;
};

struct FScrollBarStyle
{
	float Thickness = 10.0f;
	FStyleBrush Track;
	FStyleBrush Thumb;
	FStyleBrush ThumbHovered;
	FStyleBrush ThumbDragged;
	float MinThumbLength = 20.0f;
};

struct FSplitterStyle
{
	float HandleSize = 5.0f;
	FStyleBrush Handle;
	FStyleBrush HandleHighlight;
};

struct FTableRowStyle
{
	FStyleBrush Even;
	FStyleBrush Odd;
	FStyleBrush Hovered;
	FStyleBrush ActiveSelected;   // selected, list focused
	FStyleBrush InactiveSelected; // selected, list not focused
	FStyleBrush SelectedHovered;
	FStyleColor Text = EStyleColor::Foreground;
	FStyleColor SelectedText = EStyleColor::ForegroundHover;
	FStyleColor DropIndicator = EStyleColor::Primary;
	float IndentSize = 14.0f; // tree indentation per level
};

struct FProgressBarStyle
{
	FStyleBrush Background;
	FStyleBrush Fill;
	float Thickness = 6.0f;
};

struct FToolTipStyle
{
	FStyleBrush Background;
	FMargin Padding = FMargin(8.0f, 5.0f);
	FTextBlockStyle Text;
	float WrapWidth = 360.0f;
};

struct FMenuStyle
{
	FStyleBrush Background;
	FMargin Padding = FMargin(4.0f);
	FButtonStyle Item;
	FStyleColor Separator = EStyleColor::Border;
};

struct FWindowStyle
{
	FStyleColor Background = EStyleColor::Background;
	FStyleBrush TitleBar;
	FTextBlockStyle TitleText;
	FButtonStyle TitleButton;
	FButtonStyle CloseButton;
	FStyleBrush Outline; // borderless windows
};

struct FSliderStyle
{
	float BarThickness = 4.0f;
	float ThumbSize = 14.0f;
	FStyleBrush Bar;
	FStyleBrush BarFill;      // from the start to the thumb
	FStyleBrush Thumb;
	FStyleBrush ThumbHovered;
	FStyleBrush Focused;
};

struct FSpinBoxStyle
{
	FStyleBrush Normal;
	FStyleBrush Hovered;
	FStyleBrush Active;       // dragging or typing
	FStyleBrush Fill;         // the part of the range below the value
	FMargin Padding = FMargin(6.0f, 3.0f);
	FEditableTextStyle Text;
	FStyleColor UndeterminedColor = EStyleColor::ForegroundSecondary;
};

// Tabs and the tab well of docking stacks.
struct FDockTabStyle
{
	FStyleBrush WellBackground;
	FStyleBrush Normal;
	FStyleBrush Hovered;
	FStyleBrush Active;        // foreground tab of the focused stack
	FStyleBrush Foreground;    // foreground tab of other stacks
	FStyleBrush ActiveIndicator;
	FStyleBrush ContentBackground;
	FStyleBrush DropTarget;    // where a dragged tab will land
	FStyleBrush DropZoneIcon;  // the docking target buttons
	FMargin TabPadding = FMargin(10.0f, 5.0f);
	float WellHeight = 28.0f;
	FTextBlockStyle Text;
	FButtonStyle CloseButton;
};

struct FToolBarStyle
{
	FStyleBrush Background;
	FButtonStyle Button;
	FButtonStyle ToggleButtonChecked;
	FMargin Padding = FMargin(4.0f, 3.0f);
	float SeparatorWidth = 1.0f;
	FStyleColor SeparatorColor = EStyleColor::Border;
	FTextBlockStyle LabelText;
};

struct FPropertyGridStyle
{
	FStyleBrush CategoryHeader;
	FStyleBrush RowEven;
	FStyleBrush RowOdd;
	FStyleBrush RowHovered;
	FTextBlockStyle CategoryText;
	FTextBlockStyle NameText;
	FStyleColor Divider = EStyleColor::Border;
	float RowHeight = 26.0f;
	float IndentSize = 12.0f;
};

struct FSeparatorStyle
{
	FStyleColor Color = EStyleColor::Border;
	float Thickness = 1.0f;
};
