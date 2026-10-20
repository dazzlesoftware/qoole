// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// A viewport client that draws into an RHI render target sized to its
// SViewport. Register it with FSlateRHIRenderer::AddViewportClient; the
// renderer calls Draw at the start of a GPU frame whenever the client asked
// for a redraw (or every frame for realtime clients), before the windows
// that show it are drawn.

#pragma once

#include "SlateRHIRenderingPolicy.h"
#include "Widgets/SViewport.h"

class FRenderTargetViewportClient : public IViewportClient
{
public:
	FRenderTargetViewportClient(FRHIDevice& InDevice, FSlateRHIRenderingPolicy& InRenderer) : Device(InDevice), Renderer(InRenderer) {}

	// IViewportClient
	TSharedPtr<ISlateRenderTarget> GetRenderTarget() const override { return Target; }
	void OnResize(FIntPoint PixelSize) override;
	void SetViewport(const TSharedPtr<SViewport>& InViewport) override { Viewport = InViewport; }

	// Draws again at the next frame (and composites the result).
	void RequestRedraw();
	bool NeedsRedraw() const { return bNeedsRedraw || IsRealtime(); }
	TSharedPtr<SViewport> GetViewport() const { return Viewport.lock(); }
	FIntPoint GetSize() const { return Size; }

	// Called by FSlateRHIRenderer: (re)creates the target and draws if needed.
	void RenderIfNeeded(FRHICommandList& CommandList);

protected:
	// Draws the picture; Target is Size pixels.
	virtual void Draw(FRHICommandList& CommandList, FSlateRenderTargetRHI& Target) = 0;

	FRHIDevice& Device;
	FSlateRHIRenderingPolicy& Renderer;

private:
	TSharedPtr<FSlateRenderTargetRHI> Target;
	TWeakPtr<SViewport> Viewport;
	FIntPoint Size{ 0, 0 };
	bool bNeedsRedraw = true;
};
