// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIRenderTest: the UI renderer on its own (milestone 2 of
// docs/GenesisUI.md). One widget fills the window and paints the draw
// element showcase: boxes, images and 9-slices, rounded boxes, lines,
// clipping, shaped text and a render target drawn into each frame. The
// mouse wheel scrolls the clipped list.
//
// Self-test: the showcase at the monitor's DPI, at 100% and 200%, after a
// resize and at an app scale of 125%; checks that it draws, batches and
// scrolls.

#include "DrawShowcase.h"
#include "TestApp.h"

namespace
{
	// Paints the showcase into the widget's area, above the widget's layer.
	class SDrawShowcase final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDrawShowcase)
		SLATE_END_ARGS()

		void Construct(const FArguments&, TFunction<FShowcaseFrame()> InGetFrame, FDrawShowcase* InShowcase, TSharedPtr<FSlateRenderTargetRHI>* InTarget)
		{
			GetFrame = std::move(InGetFrame);
			Showcase = InShowcase;
			Target = InTarget;
			SetVolatile(true);
			SetClipping(EWidgetClipping::ClipToBounds);
		}

		float ListScroll = 0.0f;

		FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override
		{
			ListScroll -= Event.GetWheelDelta() * 24.0f;
			return FReply::Handled();
		}

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(640.0f, 480.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle&) const override
		{
			FShowcaseFrame Frame = GetFrame();
			Frame.Scale = Geometry.Scale;
			Frame.Origin = Geometry.AbsolutePosition / Geometry.Scale;
			Frame.LogicalSize = Geometry.Size;
			Frame.MouseLogical = IsHovered() ? FSlateApplication::Get().GetCursorPosition() / Geometry.Scale - Frame.Origin : FVector2(-1.0f, -1.0f);
			Frame.ListScroll = ListScroll;
			// The showcase uses layers 0-6 and its own clip rectangles; re-add
			// its elements above our layer.
			FSlateWindowElementList Local;
			Local.SetViewportSize(Out.GetViewportSize());
			Showcase->PaintMain(Local, Frame, *Target);
			int32 MaxLayer = LayerId;
			for (const FSlateDrawElement& Element : Local.GetElements())
			{
				const int32 Layer = LayerId + Element.Layer;
				MaxLayer = FMath::Max(MaxLayer, Layer);
				if (Element.ClipIndex != INDEX_NONE)
				{
					Out.PushClipPixels(Local.GetClipRects()[Element.ClipIndex]);
				}
				switch (Element.Type)
				{
				case EElementType::Box: Out.AddBox(Layer, Element.Geometry, Element.Get<FSlateBoxElement>().Brush); break;
				case EElementType::RoundedBox:
				{
					const FSlateRoundedBoxElement& R = Element.Get<FSlateRoundedBoxElement>();
					Out.AddRoundedBox(Layer, Element.Geometry, Element.Color, R.CornerRadii, R.OutlineColor, R.OutlineWidth);
					break;
				}
				case EElementType::Text: Out.AddText(Layer, Element.Geometry, ToSharedRef(Element.Get<FSlateTextElement>().Text), Element.Color); break;
				case EElementType::Lines:
				{
					const FSlateLineElement& L = Element.Get<FSlateLineElement>();
					Out.AddLines(Layer, Element.Geometry, L.Points, Element.Color, L.Thickness, L.bClosed);
					break;
				}
				case EElementType::CustomVerts:
				{
					const FSlateCustomVertsElement& C = Element.Get<FSlateCustomVertsElement>();
					Out.AddCustomVerts(Layer, Element.Geometry, C.Image, C.Vertices, C.Indices);
					break;
				}
				case EElementType::Viewport:
				{
					const FSlateViewportElement& V = Element.Get<FSlateViewportElement>();
					Out.AddViewport(Layer, Element.Geometry, V.RenderTarget, V.bAlphaBlend, Element.Color);
					break;
				}
				}
				if (Element.ClipIndex != INDEX_NONE)
				{
					Out.PopClip();
				}
			}
			return MaxLayer;
		}

	private:
		TFunction<FShowcaseFrame()> GetFrame;
		FDrawShowcase* Showcase = nullptr;
		TSharedPtr<FSlateRenderTargetRHI>* Target = nullptr;
	};
}

class FRenderTestApp final : public FTestApp
{
public:
	FRenderTestApp() : FTestApp({ "UIRenderTest", "Genesis UI - Draw elements", FVector2(1180.0f, 740.0f) }) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		FShowcaseFonts ShowcaseFonts;
		ShowcaseFonts.Regular = GetFonts().Regular;
		ShowcaseFonts.Bold = GetFonts().Bold;
		Showcase = MakeUnique<FDrawShowcase>(GetFontCache(), ShowcaseFonts);
		return SAssignNew(Widget, SDrawShowcase, [this] { return MakeFrame(); }, Showcase.get(), &Target);
	}

	void OnBeginFrame(FRHICommandList& CommandList) override
	{
		// The render target shown by the viewport panel, sized for the window's scale.
		if (!Showcase)
		{
			return;
		}
		FShowcaseFrame Frame = MakeFrame();
		const FIntPoint TargetSize(FMath::RoundToInt(240.0f * Frame.Scale), FMath::RoundToInt(150.0f * Frame.Scale));
		if (!Target || Target->GetSize() != TargetSize)
		{
			if (Target)
			{
				GetDevice().DeferRelease(Target);
			}
			Target = GetRenderer().CreateRenderTarget(TargetSize.X, TargetSize.Y, "Showcase viewport").ToSharedPtr();
		}
		TargetElements.Reset();
		Frame.Origin = FVector2::Zero();
		Showcase->PaintViewportScene(TargetElements, Frame, TargetSize);
		GetRenderer().Render(CommandList, Target->GetTexture(), TargetElements, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		++TargetFrames;
	}

	void OnShutdown() override
	{
		Widget.reset();
		Target.reset();
		Showcase.reset();
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("native")
			.Do([this]
			{
				const FSlateRenderStats& S = Stats();
				Expect(S.Elements > 100 && S.Batches > 0, FString::Format("the showcase draws ({} elements, {} batches, {} vertices)", S.Elements, S.Batches, S.Vertices));
				Expect(S.Batches < S.Elements, "batching merges elements into fewer draw calls");
			}, 1)
			.Check("the render target is drawn every frame", [this] { return TargetFrames > 10; })
			.Do([this]
			{
				ScrollBefore = Widget->ListScroll;
				Wheel(GetCenter(*Widget), -3.0f);
			})
			.Check("the wheel scrolls the clipped list", [this] { return Widget->ListScroll > ScrollBefore; })
			.Do([this] { SimulateDpiChange(1.0f); })
			.Capture("dpi100", 30)
			.Do([this] { SimulateDpiChange(2.0f); })
			.Capture("dpi200", 30)
			.Check("the window keeps its logical size at 200%", [this] { return FMath::Abs(GetMainWindow()->GetPixelSize().X / 2.0f - 1180.0f) < 40.0f; })
			.Do([this] { SimulateDpiChange(GetNativeDpiScale()); }, 1)
			.Do([this]
			{
				const TSharedRef<IPlatformWindow> Native = ToSharedRef(GetMainWindow()->GetNativeWindow());
				const FIntPoint Pos = Native->GetClientPosition();
				Native->ReshapeWindow(Pos.X, Pos.Y, FMath::RoundToInt(900.0f * Native->GetDpiScale()), FMath::RoundToInt(600.0f * Native->GetDpiScale()));
			}, 20)
			.Capture("resized", 30)
			.Do([this] { SetAppScale(1.25f); })
			.Capture("appscale125", 30);
	}

private:
	FShowcaseFrame MakeFrame() const
	{
		FShowcaseFrame Frame;
		const TSharedPtr<SWindow>& Window = GetMainWindow();
		Frame.Time = GetTime();
		Frame.DpiScale = Window ? Window->GetNativeWindow()->GetDpiScale() : 1.0f;
		Frame.AppScale = GetAppScale();
		Frame.Scale = Frame.DpiScale * Frame.AppScale;
		Frame.PixelSize = Window ? Window->GetPixelSize() : FIntPoint();
		Frame.LogicalSize = Frame.PixelSize.ToVector2() / Frame.Scale;
		Frame.BackendName = ToString(GetBackend());
		Frame.AdapterName = GetDevice().GetAdapterName();
		Frame.FramesPerSecond = GetFramesPerSecond();
		if (Window)
		{
			Frame.Stats = GetWindowRenderer().GetStats(*Window->GetNativeWindow());
		}
		return Frame;
	}

	const FSlateRenderStats& Stats() const { return GetWindowRenderer().GetStats(*GetMainWindow()->GetNativeWindow()); }

	TUniquePtr<FDrawShowcase> Showcase;
	TSharedPtr<SDrawShowcase> Widget;
	TSharedPtr<FSlateRenderTargetRHI> Target;
	FSlateWindowElementList TargetElements;
	int32 TargetFrames = 0;
	float ScrollBefore = 0.0f;
};

GENESIS_TEST_APP(FRenderTestApp)
