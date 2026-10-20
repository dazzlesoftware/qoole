// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Input method editors (IME) for languages typed through composition
// (Chinese, Japanese, Korean...), Engine/Documentation/Source/GenesisUI.md §4.
//
// A text widget that has focus activates its ITextInputMethodContext. While
// the user composes, the platform reports the composition string; the widget
// draws it inline at the caret. Committed text arrives as ordinary
// OnKeyChar characters after EndComposition. The platform asks the context
// where the caret is to place the candidate window.

#pragma once

#include "GenericPlatform/PlatformWindow.h"

class ITextInputMethodContext
{
public:
	virtual ~ITextInputMethodContext() = default;

	virtual void BeginComposition() = 0;
	// The current composition string and the caret inside it (byte offset).
	virtual void UpdateComposition(const FString& Text, int32 CaretIndex) = 0;
	// Composition finished or was cancelled; its text is removed. Committed
	// text follows as characters.
	virtual void EndComposition() = 0;

	// Caret rectangle in the window's client pixels (candidate window placement).
	virtual FIntRect GetCaretRect() const = 0;
	// The native window the text is in.
	virtual TSharedPtr<IPlatformWindow> GetWindow() const = 0;
};

class ITextInputMethodSystem
{
public:
	virtual ~ITextInputMethodSystem() = default;

	// Only one context is active at a time; activating another deactivates the first.
	virtual void ActivateContext(const TSharedRef<ITextInputMethodContext>& Context) = 0;
	virtual void DeactivateContext(const TSharedRef<ITextInputMethodContext>& Context) = 0;
	// The caret moved; the candidate window should follow.
	virtual void UpdateContextLayout(const TSharedRef<ITextInputMethodContext>& Context) = 0;
};
