// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "SlateRHIRenderer.h"

FSlateRHIRenderer::~FSlateRHIRenderer()
{
	if (!Viewports.empty())
	{
		Device.WaitIdle();
	}
}

void FSlateRHIRenderer::CreateViewport(const TSharedRef<IPlatformWindow>& Window)
{
	if (Viewports.contains(&Window.Get()))
	{
		return;
	}
	FViewport Viewport;
	Viewport.SwapChain = Device.CreateSwapChain(Window.Get());
	Viewports.emplace(&Window.Get(), std::move(Viewport));
}

void FSlateRHIRenderer::ResizeViewport(const IPlatformWindow& Window, FIntPoint PixelSize)
{
	if (FRHISwapChain* SwapChain = FindSwapChain(Window))
	{
		SwapChain->Resize(static_cast<uint32>(FMath::Max(0, PixelSize.X)), static_cast<uint32>(FMath::Max(0, PixelSize.Y)));
	}
}

void FSlateRHIRenderer::DestroyViewport(const IPlatformWindow& Window)
{
	auto It = Viewports.find(&Window);
	if (It != Viewports.end())
	{
		Device.WaitIdle();
		Viewports.erase(It);
	}
}

FRHISwapChain* FSlateRHIRenderer::FindSwapChain(const IPlatformWindow& Window) const
{
	auto It = Viewports.find(&Window);
	return It == Viewports.end() ? nullptr : It->second.SwapChain.get();
}

const FSlateRenderStats& FSlateRHIRenderer::GetStats(const IPlatformWindow& Window) const
{
	static const FSlateRenderStats None;
	auto It = Viewports.find(&Window);
	return It == Viewports.end() ? None : It->second.Stats;
}

void FSlateRHIRenderer::DrawWindows(TArrayView<const FWindowDrawRequest> Requests)
{
	if (bInFrame)
	{
		return; // re-entered from an OS paint during the frame
	}
	bInFrame = true;
	FRHICommandList& CommandList = Device.BeginFrame();
	Renderer.BeginFrame();
	OnBeginFrame.Broadcast(CommandList);
	std::erase_if(ViewportClients, [](const TWeakPtr<FRenderTargetViewportClient>& C) { return C.expired(); });
	for (const TWeakPtr<FRenderTargetViewportClient>& Weak : TArray<TWeakPtr<FRenderTargetViewportClient>>(ViewportClients))
	{
		if (const TSharedPtr<FRenderTargetViewportClient> Client = Weak.lock())
		{
			Client->RenderIfNeeded(CommandList);
		}
	}
	for (const FWindowDrawRequest& Request : Requests)
	{
		auto It = Viewports.find(Request.Window);
		if (It == Viewports.end() || !It->second.SwapChain || !It->second.SwapChain->IsDrawable())
		{
			continue;
		}
		// The swapchain follows the window even if a resize message was missed.
		const FIntPoint WindowSize = Request.Window->GetClientSize();
		if (!(It->second.SwapChain->GetSize() == WindowSize) && WindowSize.X > 0 && WindowSize.Y > 0)
		{
			It->second.SwapChain->Resize(static_cast<uint32>(WindowSize.X), static_cast<uint32>(WindowSize.Y));
		}
		Renderer.Render(CommandList, *It->second.SwapChain, *Request.Elements, Request.ClearColor);
		It->second.Stats = Renderer.GetLastStats();
	}
	Device.EndFrame();
	bInFrame = false;
}
