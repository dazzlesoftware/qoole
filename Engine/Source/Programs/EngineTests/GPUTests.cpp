// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// End-to-end renderer tests: draw element lists into an offscreen render
// target and read the pixels back. D3D12 runs on WARP (no GPU needed); Vulkan
// runs when a Vulkan driver is present and is skipped otherwise.

#include "TestHelpers.h"

#include "RHI.h"
#include "UIRenderer.h"

namespace
{
	struct FPixels
	{
		TArray<uint8> Data;
		int32 Width = 0;

		FColor At(int32 X, int32 Y) const
		{
			const SIZE_T I = (SIZE_T(Y) * Width + X) * 4;
			return FColor(Data[I], Data[I + 1], Data[I + 2], Data[I + 3]);
		}
	};

	bool Near(FColor A, FColor B, int32 Tolerance = 3)
	{
		return FMath::Abs(int32(A.R) - B.R) <= Tolerance && FMath::Abs(int32(A.G) - B.G) <= Tolerance
			&& FMath::Abs(int32(A.B) - B.B) <= Tolerance && FMath::Abs(int32(A.A) - B.A) <= Tolerance;
	}

	// Renders the list into a Size x Size target and reads it back.
	TOptional<FPixels> RenderOffscreen(ERHIBackend Backend, int32 Size, const TFunction<void(FDrawElementList&, FUIRenderer&)>& Build)
	{
		FRHIDeviceDesc Desc;
		Desc.Backend = Backend;
		Desc.bSoftwareAdapter = true;
		TUniquePtr<FRHIDevice> Device = FRHIDevice::Create(Desc);
		if (!Device)
		{
			return {};
		}
		FTestFonts& Fonts = FTestFonts::Get();
		FUIRenderer Renderer(*Device, Fonts.Cache);
		REQUIRE(Renderer.Initialize());
		const TSharedRef<FUIRenderTarget> Target = Renderer.CreateRenderTarget(Size, Size, "Test target");

		FDrawElementList List;
		List.SetViewportSize({ Size, Size });
		Build(List, Renderer);

		FRHICommandList& CommandList = Device->BeginFrame();
		Renderer.BeginFrame();
		Renderer.Render(CommandList, Target->GetTexture(), List, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		Device->EndFrame();

		FPixels Pixels;
		Pixels.Width = Size;
		REQUIRE(Device->ReadbackTexture(Target->GetTexture(), Pixels.Data));
		return Pixels;
	}

	void RunScene(ERHIBackend Backend)
	{
		FTestLogCapture Log;
		FTestFonts& Fonts = FTestFonts::Get();
		REQUIRE(Fonts.IsValid());
		const auto Checker = MakeShared<FUIImage>(2, 2, TArray<uint8>{
			255, 0, 0, 255,   0, 255, 0, 255,
			0, 0, 255, 255,   255, 255, 255, 255 });

		const TOptional<FPixels> Result = RenderOffscreen(Backend, 128, [&](FDrawElementList& List, FUIRenderer&)
		{
			List.AddSolidBox(0, FPaintGeometry::Make({ 0.0f, 0.0f }, { 32.0f, 32.0f }, 1.0f), FColor(200, 100, 50));
			// Two layers: the later layer must cover the earlier one.
			List.AddSolidBox(2, FPaintGeometry::Make({ 40.0f, 0.0f }, { 20.0f, 20.0f }, 1.0f), FColor::Green());
			List.AddSolidBox(1, FPaintGeometry::Make({ 40.0f, 0.0f }, { 20.0f, 20.0f }, 1.0f), FColor::Red());
			// A rounded box: filled center, transparent corner.
			List.AddRoundedBox(0, FPaintGeometry::Make({ 64.0f, 0.0f }, { 64.0f, 32.0f }, 1.0f), FColor::Blue(), 12.0f, FColor::White(), 2.0f);
			// Clipped box: only the left half survives.
			List.PushClipPixels(FIntRect(0, 40, 16, 72));
			List.AddSolidBox(0, FPaintGeometry::Make({ 0.0f, 40.0f }, { 32.0f, 32.0f }, 1.0f), FColor::White());
			List.PopClip();
			// Image stretched 16x: four solid quadrants (point-like in the middles).
			List.AddBox(0, FPaintGeometry::Make({ 64.0f, 64.0f }, { 64.0f, 64.0f }, 1.0f), FBrush::FromImage(Checker));
			// Text.
			const auto Shaped = Fonts.Cache.ShapeText("Hg", FFontInfo(Fonts.Latin, 24.0f), 1.0f);
			List.AddText(0, FPaintGeometry::Make({ 0.0f, 80.0f }, Shaped->GetLogicalSize(), 1.0f), Shaped, FColor::White());
		});
		if (!Result)
		{
			MESSAGE(ToString(Backend), " device unavailable; skipped");
			return;
		}
		const FPixels& P = *Result;
		CHECK(Near(P.At(16, 16), FColor(200, 100, 50)));
		CHECK(Near(P.At(50, 10), FColor::Green()));
		CHECK(Near(P.At(96, 16), FColor::Blue()));          // rounded box fill
		CHECK(Near(P.At(96, 1), FColor::White(), 40));      // its 2px outline
		CHECK(Near(P.At(64, 0), FColor::Black()));          // outside the rounded corner
		CHECK(Near(P.At(8, 56), FColor::White()));          // inside the clip
		CHECK(Near(P.At(24, 56), FColor::Black()));         // clipped away
		CHECK(Near(P.At(70, 70), FColor::Red(), 8));        // image quadrants
		CHECK(Near(P.At(122, 70), FColor::Green(), 8));
		CHECK(Near(P.At(70, 122), FColor::Blue(), 8));
		CHECK(Near(P.At(122, 122), FColor::White(), 8));

		// Text left ink in its box and nothing outside it.
		int32 Ink = 0;
		for (int32 Y = 80; Y < 120; ++Y)
		{
			for (int32 X = 0; X < 40; ++X)
			{
				Ink += P.At(X, Y).R > 128 ? 1 : 0;
			}
		}
		CHECK(Ink > 40);
		CHECK(Log.Problems.empty());
		for (const FString& Problem : Log.Problems)
		{
			MESSAGE(Problem.c_str());
		}
	}
}

TEST_SUITE("GPU")
{
	TEST_CASE("D3D12 (WARP) renders boxes, rounded boxes, clips, images and text")
	{
		RunScene(ERHIBackend::D3D12);
	}

	TEST_CASE("Vulkan renders boxes, rounded boxes, clips, images and text")
	{
		RunScene(ERHIBackend::Vulkan);
	}
}
