// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// A viewport client that draws into an RHI render target sized to its
// SViewport. Register it with FUIWindowRenderer::AddViewportClient; the
// renderer calls Draw at the start of a GPU frame whenever the client asked
// for a redraw (or every frame for realtime clients), before the windows
// that show it are drawn.

#pragma once

#include "UIRenderer.h"
#include "Widgets/SViewport.h"

class FRenderTargetViewportClient : public IViewportClient
{
public:
	FRenderTargetViewportClient(FRHIDevice& InDevice, FUIRenderer& InRenderer) : Device(InDevice), Renderer(InRenderer) {}

	// IViewportClient
	TSharedPtr<IUIRenderTarget> GetRenderTarget() const override { return Target; }
	void OnResize(FIntPoint PixelSize) override;
	void SetViewport(const TSharedPtr<SViewport>& InViewport) override { Viewport = InViewport; }

	// Draws again at the next frame (and composites the result).
	void RequestRedraw();
	bool NeedsRedraw() const { return bNeedsRedraw || IsRealtime(); }
	TSharedPtr<SViewport> GetViewport() const { return Viewport.lock(); }
	FIntPoint GetSize() const { return Size; }

	// Called by FUIWindowRenderer: (re)creates the target and draws if needed.
	void RenderIfNeeded(FRHICommandList& CommandList);

protected:
	// Draws the picture; Target is Size pixels.
	virtual void Draw(FRHICommandList& CommandList, FUIRenderTarget& Target) = 0;

	FRHIDevice& Device;
	FUIRenderer& Renderer;

private:
	TSharedPtr<FUIRenderTarget> Target;
	TWeakPtr<SViewport> Viewport;
	FIntPoint Size{ 0, 0 };
	bool bNeedsRedraw = true;
};
