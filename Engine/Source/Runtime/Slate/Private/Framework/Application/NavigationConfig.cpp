// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Framework/Application/NavigationConfig.h"

EUINavigation FNavigationConfig::GetNavigationDirectionFromKey(const FKeyEvent& Event) const
{
	if (const TOptional<EGamepadButton> Button = Event.GetGamepadButton())
	{
		switch (*Button)
		{
		case EGamepadButton::DPadUp: return EUINavigation::Up;
		case EGamepadButton::DPadDown: return EUINavigation::Down;
		case EGamepadButton::DPadLeft: return EUINavigation::Left;
		case EGamepadButton::DPadRight: return EUINavigation::Right;
		default: return EUINavigation::Invalid;
		}
	}
	switch (Event.GetKey())
	{
	case EKey::Tab:
		if (!bTabNavigation || Event.IsControlDown() || Event.IsAltDown())
		{
			return EUINavigation::Invalid;
		}
		return Event.IsShiftDown() ? EUINavigation::Previous : EUINavigation::Next;
	case EKey::Up: return bKeyNavigation ? EUINavigation::Up : EUINavigation::Invalid;
	case EKey::Down: return bKeyNavigation ? EUINavigation::Down : EUINavigation::Invalid;
	case EKey::Left: return bKeyNavigation ? EUINavigation::Left : EUINavigation::Invalid;
	case EKey::Right: return bKeyNavigation ? EUINavigation::Right : EUINavigation::Invalid;
	default: return EUINavigation::Invalid;
	}
}

EUINavigationAction FNavigationConfig::GetNavigationActionFromKey(const FKeyEvent& Event) const
{
	if (const TOptional<EGamepadButton> Button = Event.GetGamepadButton())
	{
		switch (*Button)
		{
		case EGamepadButton::FaceBottom: return EUINavigationAction::Accept;
		case EGamepadButton::FaceRight: return EUINavigationAction::Back;
		default: return EUINavigationAction::Invalid;
		}
	}
	switch (Event.GetKey())
	{
	case EKey::Enter:
	case EKey::SpaceBar: return EUINavigationAction::Accept;
	case EKey::Escape: return EUINavigationAction::Back;
	default: return EUINavigationAction::Invalid;
	}
}

EUINavigation FNavigationConfig::GetNavigationDirectionFromAnalog(EGamepadAxis Axis, float Value) const
{
	if (!bAnalogNavigation)
	{
		return EUINavigation::Invalid;
	}
	if (Axis == EGamepadAxis::LeftX)
	{
		return Value > AnalogThreshold ? EUINavigation::Right : (Value < -AnalogThreshold ? EUINavigation::Left : EUINavigation::Invalid);
	}
	if (Axis == EGamepadAxis::LeftY)
	{
		// Y is up.
		return Value > AnalogThreshold ? EUINavigation::Up : (Value < -AnalogThreshold ? EUINavigation::Down : EUINavigation::Invalid);
	}
	return EUINavigation::Invalid;
}
