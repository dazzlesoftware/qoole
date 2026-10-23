// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/SViewport.h"
#include "Templates/SharedPointer.h"

#include "Styling/SlateBrush.h"

void SViewport::Construct(const FArguments& Args)
{
	bIsFocusable = Args._IsFocusable;
	bAlphaBlend = Args._AlphaBlend;
	Background = Args._Background;
	SetViewportClient(Args._ViewportClient);
}

void SViewport::SetViewportClient(const TSharedPtr<IViewportClient>& InClient)
{
	Client = InClient;
	SetVolatile(Client && Client->IsRealtime());
	if (Client)
	{
		Client->SetViewport(StaticCastSharedPtr<SViewport>(shared_from_this()));
		if (PixelSize.X > 0 && PixelSize.Y > 0)
		{
			Client->OnResize(PixelSize);
		}
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SViewport::OnPreArrange(const FGeometry& AllottedGeometry)
{
	const FVector2 Size = AllottedGeometry.GetAbsoluteSize();
	const FIntPoint NewSize(FMath::Max(1, FMath::RoundToInt(Size.X)), FMath::Max(1, FMath::RoundToInt(Size.Y)));
	if (!(NewSize == PixelSize))
	{
		PixelSize = NewSize;
		if (Client)
		{
			Client->OnResize(PixelSize);
		}
	}
}

int32 SViewport::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	OutDrawElements.AddSolidBox(LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.Tinted(Background));
	if (const TSharedPtr<ISlateRenderTarget> Target = Client ? Client->GetRenderTarget() : nullptr)
	{
		OutDrawElements.AddViewport(
		    LayerId + 1, Geometry.ToPaintGeometry(), Target, bAlphaBlend, InWidgetStyle.ColorAndOpacityTint);
	}
	if (HasKeyboardFocus())
	{
		OutDrawElements.AddRoundedBox(LayerId + 2, Geometry.ToPaintGeometry(), FColor::Transparent(), 0.0f,
		    InWidgetStyle.Tinted(FSlateColor(EStyleColor::Primary).Resolve()), 1.0f);
	}
	return LayerId + 2;
}

FReply SViewport::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	FReply Reply = Client ? Client->OnMouseButtonDown(Geometry, Event) : FReply::Unhandled();
	// Clicking a viewport focuses it (keys go to the client).
	if (bIsFocusable && !Reply.GetFocusRecipient() && !HasKeyboardFocus())
	{
		if (!Reply.IsEventHandled())
		{
			Reply = FReply::Handled();
		}
		Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return Reply;
}

FReply SViewport::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return Client ? Client->OnMouseButtonUp(Geometry, Event) : FReply::Unhandled();
}
FReply SViewport::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return Client ? Client->OnMouseButtonDoubleClick(Geometry, Event) : FReply::Unhandled();
}
FReply SViewport::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return Client ? Client->OnMouseMove(Geometry, Event) : FReply::Unhandled();
}
FReply SViewport::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return Client ? Client->OnMouseWheel(Geometry, Event) : FReply::Unhandled();
}
FReply SViewport::OnRawMouseMove(FVector2 Delta)
{
	return Client ? Client->OnRawMouseMove(Delta) : FReply::Unhandled();
}
FReply SViewport::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	return Client ? Client->OnKeyDown(Geometry, Event) : FReply::Unhandled();
}
FReply SViewport::OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event)
{
	return Client ? Client->OnKeyUp(Geometry, Event) : FReply::Unhandled();
}
FReply SViewport::OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event)
{
	return Client ? Client->OnKeyChar(Geometry, Event) : FReply::Unhandled();
}

void SViewport::OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Client)
	{
		Client->OnMouseEnter(Geometry, Event);
	}
}

void SViewport::OnMouseLeave(const FPointerEvent& Event)
{
	if (Client)
	{
		Client->OnMouseLeave(Event);
	}
}

void SViewport::OnMouseCaptureLost()
{
	if (Client)
	{
		Client->OnMouseCaptureLost();
	}
}

FReply SViewport::OnFocusReceived(const FGeometry&, const FFocusEvent& Event)
{
	if (Client)
	{
		Client->OnFocusReceived(Event);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SViewport::OnFocusLost(const FFocusEvent& Event)
{
	if (Client)
	{
		Client->OnFocusLost(Event);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

FCursorReply SViewport::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	if (Client)
	{
		const FCursorReply Reply = Client->OnCursorQuery(Geometry, Event);
		if (Reply.IsEventHandled())
		{
			return Reply;
		}
	}
	return SLeafWidget::OnCursorQuery(Geometry, Event);
}
