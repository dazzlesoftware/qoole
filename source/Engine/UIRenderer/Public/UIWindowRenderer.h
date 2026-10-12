// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// IUIWindowRenderer on the RHI: one swapchain per window, all windows drawn
// in one GPU frame through an FUIRenderer.
//
//   FUIWindowRenderer WindowRenderer(Device, Renderer);
//   App->SetRenderer(&WindowRenderer);
//
// OnBeginFrame runs at the start of each GPU frame, before the windows, for
// work that renders into render targets the UI then shows (viewports).

#pragma once

#include "Rendering/WindowRenderer.h"
#include "UIRenderer.h"

class FUIWindowRenderer final : public IUIWindowRenderer
{
public:
	FUIWindowRenderer(FRHIDevice& InDevice, FUIRenderer& InRenderer) : Device(InDevice), Renderer(InRenderer) {}
	~FUIWindowRenderer() override;

	void CreateViewport(const TSharedRef<IPlatformWindow>& Window) override;
	void ResizeViewport(const IPlatformWindow& Window, FIntPoint PixelSize) override;
	void DestroyViewport(const IPlatformWindow& Window) override;
	void DrawWindows(TArrayView<const FWindowDrawRequest> Requests) override;

	FRHISwapChain* FindSwapChain(const IPlatformWindow& Window) const;
	const FUIRenderStats& GetStats(const IPlatformWindow& Window) const;

	TMulticastDelegate<void(FRHICommandList&)> OnBeginFrame;

private:
	struct FViewport
	{
		TUniquePtr<FRHISwapChain> SwapChain;
		FUIRenderStats Stats;
	};

	FRHIDevice& Device;
	FUIRenderer& Renderer;
	TMap<const IPlatformWindow*, FViewport> Viewports;
	bool bInFrame = false;
};
