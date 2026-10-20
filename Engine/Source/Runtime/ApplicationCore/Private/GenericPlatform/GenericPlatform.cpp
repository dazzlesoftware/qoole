// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "GenericPlatform/PlatformApplication.h"

DEFINE_LOG_CATEGORY(LogApplicationCore, Log);

const FMonitorInfo* FDisplayMetrics::GetPrimary() const
{
	for (const FMonitorInfo& Monitor : Monitors)
	{
		if (Monitor.bIsPrimary)
		{
			return &Monitor;
		}
	}
	return Monitors.empty() ? nullptr : &Monitors.front();
}

const char* ToString(EKey Key)
{
	static thread_local char Single[2] = {};
	const uint16 Code = static_cast<uint16>(Key);
	if ((Code >= 'A' && Code <= 'Z') || (Code >= '0' && Code <= '9'))
	{
		Single[0] = static_cast<char>(Code);
		return Single;
	}
	switch (Key)
	{
	case EKey::BackSpace: return "BackSpace";
	case EKey::Tab: return "Tab";
	case EKey::Enter: return "Enter";
	case EKey::Pause: return "Pause";
	case EKey::CapsLock: return "CapsLock";
	case EKey::Escape: return "Escape";
	case EKey::SpaceBar: return "SpaceBar";
	case EKey::PageUp: return "PageUp";
	case EKey::PageDown: return "PageDown";
	case EKey::End: return "End";
	case EKey::Home: return "Home";
	case EKey::Left: return "Left";
	case EKey::Up: return "Up";
	case EKey::Right: return "Right";
	case EKey::Down: return "Down";
	case EKey::Insert: return "Insert";
	case EKey::Delete: return "Delete";
	case EKey::LeftShift: return "LeftShift";
	case EKey::RightShift: return "RightShift";
	case EKey::LeftControl: return "LeftControl";
	case EKey::RightControl: return "RightControl";
	case EKey::LeftAlt: return "LeftAlt";
	case EKey::RightAlt: return "RightAlt";
	default: break;
	}
	if (Code >= static_cast<uint16>(EKey::F1) && Code <= static_cast<uint16>(EKey::F24))
	{
		static const char* const Names[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
			"F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24" };
		return Names[Code - static_cast<uint16>(EKey::F1)];
	}
	return "Key";
}

const char* ToString(EGamepadButton Button)
{
	static const char* const Names[] = { "FaceBottom", "FaceRight", "FaceLeft", "FaceTop", "LeftShoulder", "RightShoulder",
		"Special_Left", "Special_Right", "LeftThumb", "RightThumb", "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
		"LeftTriggerThreshold", "RightTriggerThreshold" };
	static_assert(std::size(Names) == static_cast<SIZE_T>(EGamepadButton::Count));
	return Button < EGamepadButton::Count ? Names[static_cast<int32>(Button)] : "Invalid";
}

const char* ToString(EGamepadAxis Axis)
{
	static const char* const Names[] = { "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };
	static_assert(std::size(Names) == static_cast<SIZE_T>(EGamepadAxis::Count));
	return Axis < EGamepadAxis::Count ? Names[static_cast<int32>(Axis)] : "Invalid";
}
