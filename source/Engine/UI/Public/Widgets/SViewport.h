// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SViewport hosts an IViewportClient (docs/GenesisUI.md §9): the client
// renders into a render target elsewhere (the UI never touches the GPU) and
// gets the widget's input: mouse, keys, focus, cursor queries and, while it
// holds the capture with FReply::UseHighPrecisionMouseMovement, raw mouse
// motion. The map views become viewport clients, so they dock like any
// other panel. UIRenderer's FRenderTargetViewportClient is a base for
// clients that draw with the RHI.

#pragma once

#include "Widgets/SLeafWidget.h"
#include "Widgets/WidgetDelegates.h"

class SViewport;

class IViewportClient
{
public:
	virtual ~IViewportClient() = default;

	// The texture to show; nullptr draws the background.
	virtual TSharedPtr<IUIRenderTarget> GetRenderTarget() const = 0;
	// The viewport's size in pixels changed.
	virtual void OnResize(FIntPoint PixelSize) { (void)PixelSize; }
	// Realtime clients are redrawn and composited every frame.
	virtual bool IsRealtime() const { return false; }
	// Called once the viewport shows this client (to ask it for redraws).
	virtual void SetViewport(const TSharedPtr<SViewport>& Viewport) { (void)Viewport; }

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnRawMouseMove(FVector2 Delta) { (void)Delta; return FReply::Unhandled(); }
	virtual void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; }
	virtual void OnMouseLeave(const FPointerEvent& Event) { (void)Event; }
	virtual void OnMouseCaptureLost() {}
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual void OnFocusReceived(const FFocusEvent& Event) { (void)Event; }
	virtual void OnFocusLost(const FFocusEvent& Event) { (void)Event; }
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const { (void)Geometry; (void)Event; return FCursorReply::Unhandled(); }
};

class SViewport : public SLeafWidget
{
public:
	GE_BEGIN_ARGS(SViewport)
		GE_ARGUMENT(TSharedPtr<IViewportClient>, ViewportClient, nullptr)
		GE_ARGUMENT(bool, IsFocusable, true)
		GE_ARGUMENT(bool, AlphaBlend, false)
		GE_ARGUMENT(FColor, Background, FColor(12, 13, 16))
	GE_END_ARGS()

	void Construct(const FArguments& Args);

	void SetViewportClient(const TSharedPtr<IViewportClient>& Client);
	const TSharedPtr<IViewportClient>& GetViewportClient() const { return Client; }
	FIntPoint GetPixelSize() const { return PixelSize; }
	// The client changed its picture: composite it again.
	void RequestRedraw() { Invalidate(EInvalidateWidgetReason::Paint); }

	bool SupportsKeyboardFocus() const override { return bIsFocusable; }
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnRawMouseMove(FVector2 Delta) override;
	void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseLeave(const FPointerEvent& Event) override;
	void OnMouseCaptureLost() override;
	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event) override;
	FReply OnFocusReceived(const FGeometry& Geometry, const FFocusEvent& Event) override;
	void OnFocusLost(const FFocusEvent& Event) override;
	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;
	EWindowZone GetWindowZoneOverride() const override { return EWindowZone::ClientArea; }

protected:
	FVector2 ComputeDesiredSize(float) const override { return FVector2(64.0f, 64.0f); }
	void OnPreArrange(const FGeometry& AllottedGeometry) override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	TSharedPtr<IViewportClient> Client;
	FIntPoint PixelSize{ 0, 0 };
	FColor Background;
	bool bIsFocusable = true;
	bool bAlphaBlend = false;
};
