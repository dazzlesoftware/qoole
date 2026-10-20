// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Input events as widgets receive them (Engine/Documentation/Source/GenesisUI.md §7). Pointer
// positions are in window pixels ("absolute" space); use
// FGeometry::AbsoluteToLocal for a widget's local units.

#pragma once

#include "GenericPlatform/InputTypes.h"
#include "Types/WidgetTypes.h"

class FInputEvent
{
public:
	FInputEvent() = default;
	FInputEvent(const FModifierKeysState& InModifiers, int32 InUserIndex, bool bInIsRepeat)
		: Modifiers(InModifiers), UserIndex(InUserIndex), bIsRepeat(bInIsRepeat)
	{
	}

	const FModifierKeysState& GetModifierKeys() const { return Modifiers; }
	bool IsShiftDown() const { return Modifiers.IsShiftDown(); }
	bool IsControlDown() const { return Modifiers.IsControlDown(); }
	bool IsAltDown() const { return Modifiers.IsAltDown(); }
	int32 GetUserIndex() const { return UserIndex; }
	bool IsRepeat() const { return bIsRepeat; }

protected:
	FModifierKeysState Modifiers;
	int32 UserIndex = 0;
	bool bIsRepeat = false;
};

// A key on the keyboard or a gamepad button.
class FKeyEvent : public FInputEvent
{
public:
	FKeyEvent() = default;
	FKeyEvent(EKey InKey, const FModifierKeysState& InModifiers, int32 InUserIndex, bool bInIsRepeat, uint32 InCharacterCode = 0)
		: FInputEvent(InModifiers, InUserIndex, bInIsRepeat), Key(InKey), CharacterCode(InCharacterCode)
	{
	}

	static FKeyEvent FromGamepad(EGamepadButton Button, const FModifierKeysState& InModifiers, int32 InUserIndex, bool bInIsRepeat)
	{
		FKeyEvent Event(EKey::Invalid, InModifiers, InUserIndex, bInIsRepeat);
		Event.GamepadButton = Button;
		return Event;
	}

	EKey GetKey() const { return Key; }
	// Set for gamepad buttons (GetKey() is then EKey::Invalid).
	TOptional<EGamepadButton> GetGamepadButton() const { return GamepadButton; }
	bool IsGamepad() const { return GamepadButton.has_value(); }
	uint32 GetCharacterCode() const { return CharacterCode; }

private:
	EKey Key = EKey::Invalid;
	TOptional<EGamepadButton> GamepadButton;
	uint32 CharacterCode = 0;
};

// A typed character (after keyboard layout and IME).
class FCharacterEvent : public FInputEvent
{
public:
	FCharacterEvent() = default;
	FCharacterEvent(char32_t InCharacter, const FModifierKeysState& InModifiers, int32 InUserIndex, bool bInIsRepeat)
		: FInputEvent(InModifiers, InUserIndex, bInIsRepeat), Character(InCharacter)
	{
	}

	char32_t GetCharacter() const { return Character; }

private:
	char32_t Character = 0;
};

class FPointerEvent : public FInputEvent
{
public:
	FPointerEvent() = default;
	FPointerEvent(FVector2 InPosition, FVector2 InLastPosition, uint8 InPressedButtons, EMouseButton InEffectingButton, float InWheelDelta,
		const FModifierKeysState& InModifiers, int32 InUserIndex = 0)
		: FInputEvent(InModifiers, InUserIndex, false)
		, Position(InPosition)
		, LastPosition(InLastPosition)
		, PressedButtons(InPressedButtons)
		, EffectingButton(InEffectingButton)
		, WheelDelta(InWheelDelta)
	{
	}

	// Window pixels.
	FVector2 GetAbsolutePosition() const { return Position; }
	FVector2 GetLastAbsolutePosition() const { return LastPosition; }
	FVector2 GetCursorDelta() const { return Position - LastPosition; }

	// The button that went down or up (Invalid for moves and wheel).
	EMouseButton GetEffectingButton() const { return EffectingButton; }
	bool IsMouseButtonDown(EMouseButton Button) const { return (PressedButtons & ButtonBit(Button)) != 0; }
	uint8 GetPressedButtons() const { return PressedButtons; }
	// Notches; positive away from the user.
	float GetWheelDelta() const { return WheelDelta; }
	bool IsHorizontalWheel() const { return bHorizontalWheel; }
	void SetHorizontalWheel(bool bHorizontal) { bHorizontalWheel = bHorizontal; }

	static constexpr uint8 ButtonBit(EMouseButton Button) { return Button == EMouseButton::Invalid ? 0 : uint8(1u << uint8(Button)); }

private:
	FVector2 Position;
	FVector2 LastPosition;
	uint8 PressedButtons = 0;
	EMouseButton EffectingButton = EMouseButton::Invalid;
	float WheelDelta = 0.0f;
	bool bHorizontalWheel = false;
};

class FFocusEvent
{
public:
	FFocusEvent() = default;
	FFocusEvent(EFocusCause InCause, int32 InUserIndex) : Cause(InCause), UserIndex(InUserIndex) {}

	EFocusCause GetCause() const { return Cause; }
	int32 GetUserIndex() const { return UserIndex; }

private:
	EFocusCause Cause = EFocusCause::SetDirectly;
	int32 UserIndex = 0;
};

class FNavigationEvent : public FInputEvent
{
public:
	FNavigationEvent() = default;
	FNavigationEvent(EUINavigation InType, ENavigationGenesis InGenesis, const FModifierKeysState& InModifiers, int32 InUserIndex)
		: FInputEvent(InModifiers, InUserIndex, false), Type(InType), Genesis(InGenesis)
	{
	}

	EUINavigation GetNavigationType() const { return Type; }
	ENavigationGenesis GetNavigationGenesis() const { return Genesis; }

private:
	EUINavigation Type = EUINavigation::Invalid;
	ENavigationGenesis Genesis = ENavigationGenesis::User;
};
