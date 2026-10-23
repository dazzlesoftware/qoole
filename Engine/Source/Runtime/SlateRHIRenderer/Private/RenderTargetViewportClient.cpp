// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "RenderTargetViewportClient.h"
#include "Templates/SharedPointer.h"

void FRenderTargetViewportClient::OnResize(FIntPoint PixelSize)
{
	if (!(PixelSize == Size))
	{
		Size = PixelSize;
		RequestRedraw();
	}
}

void FRenderTargetViewportClient::RequestRedraw()
{
	bNeedsRedraw = true;
	if (const TSharedPtr<SViewport> Pinned = Viewport.lock())
	{
		Pinned->RequestRedraw();
	}
}

void FRenderTargetViewportClient::RenderIfNeeded(FRHICommandList& CommandList)
{
	if (Size.X <= 0 || Size.Y <= 0 || !NeedsRedraw())
	{
		return;
	}
	if (!Target || !(Target->GetSize() == Size))
	{
		if (Target)
		{
			Device.DeferRelease(Target);
		}
		Target = Renderer.CreateRenderTarget(Size.X, Size.Y, "Viewport").ToSharedPtr();
	}
	bNeedsRedraw = false;
	Draw(CommandList, *Target);
}
