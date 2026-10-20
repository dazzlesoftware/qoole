// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Per-widget style structs (Engine/Documentation/Source/GenesisUI.md §8). Widgets take a pointer to
// one as an argument and fall back to the application style by name.

#pragma once

#include "Fonts/FontCache.h"
#include "Styling/SlateBrush.h"

struct FTextBlockStyle
{
	FSlateFontInfo Font;
	FSlateColor ColorAndOpacity = EStyleColor::Foreground;
	FVector2 ShadowOffset;      // logical units; zero = no shadow
	FSlateColor ShadowColor = FSlateColor(FColor(0, 0, 0, 160));
	FSlateColor HighlightColor = EStyleColor::Primary; // search-match highlight

	FTextBlockStyle& SetFont(const FSlateFontInfo& InFont) { Font = InFont; return *this; }
	FTextBlockStyle& SetFontSize(float Size) { Font.Size = Size; return *this; }
	FTextBlockStyle& SetColor(FSlateColor Color) { ColorAndOpacity = Color; return *this; }
};

struct FButtonStyle
{
	FSlateBrush Normal;
	FSlateBrush Hovered;
	FSlateBrush Pressed;
	FSlateBrush Disabled;
	FSlateBrush Focused; // drawn over the button when it has keyboard focus via navigation
	FMargin NormalPadding = FMargin(10.0f, 4.0f);
	FMargin PressedPadding = FMargin(10.0f, 5.0f, 10.0f, 3.0f);
	FSlateColor NormalForeground = EStyleColor::Foreground;
	FSlateColor HoveredForeground = EStyleColor::ForegroundHover;
	FSlateColor PressedForeground = EStyleColor::ForegroundHover;
	FSlateColor DisabledForeground = EStyleColor::ForegroundDisabled;
};

struct FCheckBoxStyle
{
	float BoxSize = 16.0f;
	float Spacing = 6.0f; // between the box and the content
	FSlateBrush Box;
	FSlateBrush BoxHovered;
	FSlateBrush BoxChecked;
	FSlateBrush BoxCheckedHovered;
	FSlateBrush BoxDisabled;
	FSlateColor CheckColor = EStyleColor::ForegroundInverted;
	FSlateBrush Focused;
	FSlateColor Foreground = EStyleColor::Foreground;
	FSlateColor DisabledForeground = EStyleColor::ForegroundDisabled;
};

struct FEditableTextStyle
{
	FSlateFontInfo Font;
	FSlateColor Color = EStyleColor::Foreground;
	FSlateColor HintColor = EStyleColor::ForegroundSecondary;
	FSlateColor CaretColor = EStyleColor::Foreground;
	FSlateColor SelectionColor = EStyleColor::Select;
	FSlateColor InactiveSelectionColor = EStyleColor::SelectInactive;
	FSlateColor CompositionUnderline = EStyleColor::Foreground;
	float CaretWidth = 1.0f;
};

struct FEditableTextBoxStyle
{
	FSlateBrush Normal;
	FSlateBrush Hovered;
	FSlateBrush Focused;
	FSlateBrush ReadOnly;
	FMargin Padding = FMargin(6.0f, 4.0f);
	FEditableTextStyle Text;
};

struct FScrollBarStyle
{
	float Thickness = 10.0f;
	FSlateBrush Track;
	FSlateBrush Thumb;
	FSlateBrush ThumbHovered;
	FSlateBrush ThumbDragged;
	float MinThumbLength = 20.0f;
};

struct FSplitterStyle
{
	float HandleSize = 5.0f;
	FSlateBrush Handle;
	FSlateBrush HandleHighlight;
};

struct FTableRowStyle
{
	FSlateBrush Even;
	FSlateBrush Odd;
	FSlateBrush Hovered;
	FSlateBrush ActiveSelected;   // selected, list focused
	FSlateBrush InactiveSelected; // selected, list not focused
	FSlateBrush SelectedHovered;
	FSlateColor Text = EStyleColor::Foreground;
	FSlateColor SelectedText = EStyleColor::ForegroundHover;
	FSlateColor DropIndicator = EStyleColor::Primary;
	float IndentSize = 14.0f; // tree indentation per level
};

struct FProgressBarStyle
{
	FSlateBrush Background;
	FSlateBrush Fill;
	float Thickness = 6.0f;
};

struct FToolTipStyle
{
	FSlateBrush Background;
	FMargin Padding = FMargin(8.0f, 5.0f);
	FTextBlockStyle Text;
	float WrapWidth = 360.0f;
};

struct FMenuStyle
{
	FSlateBrush Background;
	FMargin Padding = FMargin(4.0f);
	FButtonStyle Item;
	FSlateColor Separator = EStyleColor::Border;
};

struct FWindowStyle
{
	FSlateColor Background = EStyleColor::Background;
	FSlateBrush TitleBar;
	FTextBlockStyle TitleText;
	FButtonStyle TitleButton;
	FButtonStyle CloseButton;
	FSlateBrush Outline; // borderless windows
};

struct FSliderStyle
{
	float BarThickness = 4.0f;
	float ThumbSize = 14.0f;
	FSlateBrush Bar;
	FSlateBrush BarFill;      // from the start to the thumb
	FSlateBrush Thumb;
	FSlateBrush ThumbHovered;
	FSlateBrush Focused;
};

struct FSpinBoxStyle
{
	FSlateBrush Normal;
	FSlateBrush Hovered;
	FSlateBrush Active;       // dragging or typing
	FSlateBrush Fill;         // the part of the range below the value
	FMargin Padding = FMargin(6.0f, 3.0f);
	FEditableTextStyle Text;
	FSlateColor UndeterminedColor = EStyleColor::ForegroundSecondary;
};

// Tabs and the tab well of docking stacks.
struct FDockTabStyle
{
	FSlateBrush WellBackground;
	FSlateBrush Normal;
	FSlateBrush Hovered;
	FSlateBrush Active;        // foreground tab of the focused stack
	FSlateBrush Foreground;    // foreground tab of other stacks
	FSlateBrush ActiveIndicator;
	FSlateBrush ContentBackground;
	FSlateBrush DropTarget;    // where a dragged tab will land
	FSlateBrush DropZoneIcon;  // the docking target buttons
	FMargin TabPadding = FMargin(10.0f, 5.0f);
	float WellHeight = 28.0f;
	FTextBlockStyle Text;
	FButtonStyle CloseButton;
};

struct FToolBarStyle
{
	FSlateBrush Background;
	FButtonStyle Button;
	FButtonStyle ToggleButtonChecked;
	FMargin Padding = FMargin(4.0f, 3.0f);
	float SeparatorWidth = 1.0f;
	FSlateColor SeparatorColor = EStyleColor::Border;
	FTextBlockStyle LabelText;
};

struct FPropertyGridStyle
{
	FSlateBrush CategoryHeader;
	FSlateBrush RowEven;
	FSlateBrush RowOdd;
	FSlateBrush RowHovered;
	FTextBlockStyle CategoryText;
	FTextBlockStyle NameText;
	FSlateColor Divider = EStyleColor::Border;
	float RowHeight = 26.0f;
	float IndentSize = 12.0f;
};

struct FSeparatorStyle
{
	FSlateColor Color = EStyleColor::Border;
	float Thickness = 1.0f;
};
