// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FSlateRenderer on the RHI: one swapchain per window, all windows drawn
// in one GPU frame through an FSlateRHIRenderingPolicy.
//
//   FSlateRHIRenderer WindowRenderer(Device, Renderer);
//   App->SetRenderer(&WindowRenderer);
//
// OnBeginFrame runs at the start of each GPU frame, before the windows, for
// work that renders into render targets the UI then shows (viewports).

#pragma once

#include "Rendering/SlateRenderer.h"
#include "RenderTargetViewportClient.h"
#include "SlateRHIRenderingPolicy.h"

class FSlateRHIRenderer final : public FSlateRenderer
{
public:
	FSlateRHIRenderer(FRHIDevice& InDevice, FSlateRHIRenderingPolicy& InRenderer) : Device(InDevice), Renderer(InRenderer) {}
	~FSlateRHIRenderer() override;

	void CreateViewport(const TSharedRef<IPlatformWindow>& Window) override;
	void ResizeViewport(const IPlatformWindow& Window, FIntPoint PixelSize) override;
	void DestroyViewport(const IPlatformWindow& Window) override;
	void DrawWindows(TArrayView<const FWindowDrawRequest> Requests) override;

	FRHISwapChain* FindSwapChain(const IPlatformWindow& Window) const;
	const FSlateRenderStats& GetStats(const IPlatformWindow& Window) const;

	TMulticastDelegate<void(FRHICommandList&)> OnBeginFrame;

	// Viewport clients rendered at the start of every frame that needs them
	// (held weakly).
	void AddViewportClient(const TSharedRef<FRenderTargetViewportClient>& Client) { ViewportClients.push_back(Client.ToSharedPtr()); }

private:
	struct FViewport
	{
		TUniquePtr<FRHISwapChain> SwapChain;
		FSlateRenderStats Stats;
	};

	FRHIDevice& Device;
	FSlateRHIRenderingPolicy& Renderer;
	TMap<const IPlatformWindow*, FViewport> Viewports;
	TArray<TWeakPtr<FRenderTargetViewportClient>> ViewportClients;
	bool bInFrame = false;
};
