// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The look shared by the test applications: a header with the app's name
// and status line, pages of titled sections, and a status bar showing the
// last event.

#pragma once

#include "TestApp.h"
#include "Widgets/Layout/SScrollBox.h"

const FTextBlockStyle& TestTextStyle(const char* Name);
const FButtonStyle& TestButtonStyle(const char* Name);

// A titled card.
TSharedRef<SWidget> TestSection(const FString& Title, const FString& Description, const TSharedRef<SWidget>& Content);

// A scrolling column of sections.
TSharedRef<SScrollBox> TestPage(std::initializer_list<TSharedRef<SWidget>> Sections);

// The main window's content: header, Content, status bar.
TSharedRef<SWidget> TestFrame(FTestApp& App, const FString& Title, const TSharedRef<SWidget>& Content);
