// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Platform-neutral input identifiers.

#pragma once

#include "CoreMinimal.h"

enum class EMouseButton : uint8
{
	Left,
	Right,
	Middle,
	Thumb1,
	Thumb2,
	Invalid,
};

// Keyboard keys by position-independent meaning (not scan codes). Letters and
// digits use their ASCII values so 'A' == EKey::A.
enum class EKey : uint16
{
	Invalid = 0,

	BackSpace = 8, Tab = 9, Enter = 13, Pause = 19, CapsLock = 20, Escape = 27, SpaceBar = 32,
	PageUp = 33, PageDown = 34, End = 35, Home = 36, Left = 37, Up = 38, Right = 39, Down = 40,
	Insert = 45, Delete = 46,

	Zero = '0', One, Two, Three, Four, Five, Six, Seven, Eight, Nine,

	A = 'A', B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

	NumPad0 = 0x60, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9,
	Multiply = 0x6A, Add, Separator, Subtract, Decimal, Divide,

	F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

	NumLock = 0x90, ScrollLock = 0x91,
	LeftShift = 0xA0, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
	LeftCommand = 0x5B, RightCommand = 0x5C, Menu = 0x5D,

	Semicolon = 0xBA, Equals, Comma, Hyphen, Period, Slash, Tilde = 0xC0,
	LeftBracket = 0xDB, Backslash, RightBracket, Apostrophe,
};

const char* ToString(EKey Key);

struct FModifierKeysState
{
	bool bLeftShift = false, bRightShift = false;
	bool bLeftControl = false, bRightControl = false;
	bool bLeftAlt = false, bRightAlt = false;
	bool bLeftCommand = false, bRightCommand = false;
	bool bCapsLocked = false;

	bool IsShiftDown() const { return bLeftShift || bRightShift; }
	bool IsControlDown() const { return bLeftControl || bRightControl; }
	bool IsAltDown() const { return bLeftAlt || bRightAlt; }
	bool IsCommandDown() const { return bLeftCommand || bRightCommand; }
};

enum class EGamepadButton : uint8
{
	FaceBottom,  // A / Cross
	FaceRight,   // B / Circle
	FaceLeft,    // X / Square
	FaceTop,     // Y / Triangle
	LeftShoulder,
	RightShoulder,
	Special_Left,  // Back / View
	Special_Right, // Start / Menu
	LeftThumb,
	RightThumb,
	DPadUp,
	DPadDown,
	DPadLeft,
	DPadRight,
	LeftTriggerThreshold,
	RightTriggerThreshold,
	Count,
};

enum class EGamepadAxis : uint8
{
	LeftX,
	LeftY,
	RightX,
	RightY,
	LeftTrigger,
	RightTrigger,
	Count,
};

const char* ToString(EGamepadButton Button);
const char* ToString(EGamepadAxis Axis);

enum class EMouseCursor : uint8
{
	None,       // hidden
	Default,
	TextEditBeam,
	ResizeLeftRight,
	ResizeUpDown,
	ResizeSouthEast,
	ResizeSouthWest,
	CardinalCross,
	Crosshairs,
	Hand,
	GrabHand,
	SlashedCircle,
	Wait,
	Count,
};

enum class EWindowActivation : uint8
{
	Activate,
	ActivateByMouse,
	Deactivate,
};

// What a point in a window is, for OS-level dragging/resizing of borderless
// windows (answer to IPlatformMessageHandler::GetWindowZoneForPoint).
enum class EWindowZone : uint8
{
	NotInWindow,
	ClientArea,
	TitleBar,
	TopLeftBorder,
	TopBorder,
	TopRightBorder,
	LeftBorder,
	RightBorder,
	BottomLeftBorder,
	BottomBorder,
	BottomRightBorder,
	MinimizeButton,
	MaximizeButton,
	CloseButton,
	SysMenu,
	Unspecified, // let the platform decide
};
