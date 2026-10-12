// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// How the UI application gets its windows onto the screen without knowing
// about the GPU (docs/GenesisUI.md §3: widgets never touch the GPU).
// UIRenderer's FUIWindowRenderer implements it on the RHI; headless tests
// use none or a recording fake.

#pragma once

#include "GenericPlatform/PlatformWindow.h"
#include "Rendering/DrawElements.h"

struct FWindowDrawRequest
{
	IPlatformWindow* Window = nullptr;
	const FDrawElementList* Elements = nullptr;
	FLinearColor ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
};

class IUIWindowRenderer
{
public:
	virtual ~IUIWindowRenderer() = default;

	virtual void CreateViewport(const TSharedRef<IPlatformWindow>& Window) = 0;
	virtual void ResizeViewport(const IPlatformWindow& Window, FIntPoint PixelSize) = 0;
	virtual void DestroyViewport(const IPlatformWindow& Window) = 0;

	// Draws and presents the given windows (one GPU frame).
	virtual void DrawWindows(TArrayView<const FWindowDrawRequest> Requests) = 0;
};
