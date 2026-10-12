// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "CoreMinimal.h"

// Saves a window's client area (as composed on screen) to a PNG file.
bool CaptureWindowClientToPng(void* NativeHandle, const FString& Path);
