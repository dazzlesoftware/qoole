// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Delegate types shared by the widgets (named, so they can be used in
// argument macros).

#pragma once

#include "Widgets/SWidget.h"

using FSimpleDelegate = TDelegate<void()>;
using FOnClicked = TDelegate<FReply()>;
using FPointerEventHandler = TDelegate<FReply(const FGeometry&, const FPointerEvent&)>;
using FOnCheckStateChanged = TDelegate<void(ECheckBoxState)>;
using FOnTextChanged = TDelegate<void(const FString&)>;
using FOnTextCommitted = TDelegate<void(const FString&, ETextCommit)>;
using FOnFloatValueChanged = TDelegate<void(float)>;
using FOnBooleanValueChanged = TDelegate<void(bool)>;
using FOnGetContent = TDelegate<TSharedRef<SWidget>()>;
using FOnIsOpenChanged = TDelegate<void(bool)>;
using FOnUserScrolled = TDelegate<void(float)>;
using FOnInt32ValueChanged = TDelegate<void(int32)>;
