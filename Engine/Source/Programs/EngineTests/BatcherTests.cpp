// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestHelpers.h"

#include "Rendering/DrawElements.h"

namespace
{
	FPaintGeometry Box(float X, float Y, float W = 10.0f, float H = 10.0f, float Scale = 1.0f)
	{
		return FPaintGeometry::Make({ X, Y }, { W, H }, Scale);
	}

	FBatchedElements BatchList(const FSlateWindowElementList& List, FSlateFontCache* Fonts = nullptr)
	{
		FFakeResolver Resolver;
		FBatchedElements Out;
		FSlateElementBatcher().Batch(List, Fonts, Resolver, Out);
		return Out;
	}

	TSharedRef<FSlateTextureData> MakeImage(int32 W, int32 H)
	{
		return FSlateTextureData::CreateSolid(W, H, FColor::White());
	}
}

TEST_SUITE("Batcher")
{
	TEST_CASE("Compatible elements merge into one batch")
	{
		FSlateWindowElementList List;
		for (int32 I = 0; I < 3; ++I)
		{
			List.AddSolidBox(0, Box(I * 20.0f, 0.0f), FColor::Red());
		}
		const FBatchedElements Out = BatchList(List);
		REQUIRE(Out.Batches.size() == 1);
		CHECK(Out.Batches[0].NumElements == 3);
		CHECK(Out.Batches[0].NumIndices == 18);
		CHECK(Out.Vertices.size() == 12);
		CHECK(Out.Indices.size() == 18);
	}

	TEST_CASE("Layers sort the draw order; submission order breaks ties")
	{
		const auto ImageA = MakeImage(4, 4);
		const auto ImageB = MakeImage(4, 4);
		FSlateWindowElementList List;
		List.AddSolidBox(2, Box(0, 0), FColor::Green());        // drawn last
		List.AddBox(1, Box(0, 0), FBrush::FromImage(ImageB));
		List.AddBox(1, Box(0, 0), FBrush::FromImage(ImageA));
		List.AddSolidBox(0, Box(0, 0), FColor::Red());        // drawn first

		const FBatchedElements Out = BatchList(List);
		REQUIRE(Out.Batches.size() == 4);
		CHECK(Out.Batches[0].Layer == 0);
		CHECK(Out.Batches[0].Texture == 1);
		CHECK(Out.Batches[1].Layer == 1);
		CHECK(Out.Batches[1].Texture == 1000 + ImageB->GetId());
		CHECK(Out.Batches[2].Layer == 1);
		CHECK(Out.Batches[2].Texture == 1000 + ImageA->GetId());
		CHECK(Out.Batches[3].Layer == 2);
		CHECK(Out.Batches[3].Texture == 1);
		CHECK(Out.Vertices.back().Color == FColor::Green());
		// Index ranges follow the draw order.
		for (SIZE_T I = 1; I < Out.Batches.size(); ++I)
		{
			CHECK(Out.Batches[I].FirstIndex == Out.Batches[I - 1].FirstIndex + Out.Batches[I - 1].NumIndices);
		}
	}

	TEST_CASE("Interleaved textures in one layer are not reordered")
	{
		const auto ImageA = MakeImage(4, 4);
		const auto ImageB = MakeImage(4, 4);
		FSlateWindowElementList List;
		List.AddBox(0, Box(0, 0), FBrush::FromImage(ImageA));
		List.AddBox(0, Box(0, 0), FBrush::FromImage(ImageB));
		List.AddBox(0, Box(0, 0), FBrush::FromImage(ImageA));
		const FBatchedElements Out = BatchList(List);
		REQUIRE(Out.Batches.size() == 3);
		CHECK(Out.Batches[0].Texture == Out.Batches[2].Texture);
	}

	TEST_CASE("Adjacent compatible batches merge across layers")
	{
		FSlateWindowElementList List;
		List.AddSolidBox(5, Box(0, 0), FColor::Red());
		List.AddSolidBox(1, Box(0, 0), FColor::Green());
		List.AddSolidBox(3, Box(0, 0), FColor::Blue());
		const FBatchedElements Out = BatchList(List);
		REQUIRE(Out.Batches.size() == 1);
		CHECK(Out.Batches[0].NumElements == 3);
		CHECK(Out.Batches[0].Layer == 1);
		// Sorted by layer: green, blue, red.
		CHECK(Out.Vertices[0].Color == FColor::Green());
		CHECK(Out.Vertices[4].Color == FColor::Blue());
		CHECK(Out.Vertices[8].Color == FColor::Red());
	}

	TEST_CASE("Shader changes split batches")
	{
		FSlateWindowElementList List;
		List.AddSolidBox(0, Box(0, 0), FColor::Red());
		List.AddRoundedBox(0, Box(0, 0, 40, 20), FColor::Red(), 4.0f, FColor::White(), 1.0f);
		List.AddRoundedBox(0, Box(0, 30, 40, 20), FColor::Red(), 4.0f);
		List.AddLines(0, Box(0, 0), { { 0, 0 }, { 10, 10 }, { 20, 0 } }, FColor::White(), 2.0f);
		List.AddSolidBox(0, Box(0, 0), FColor::Red());
		const FBatchedElements Out = BatchList(List);
		REQUIRE(Out.Batches.size() == 4);
		CHECK(Out.Batches[0].Shader == ESlateShader::Default);
		CHECK(Out.Batches[1].Shader == ESlateShader::RoundedBox);
		CHECK(Out.Batches[1].Blend == ESlateBlendMode::Premultiplied);
		CHECK(Out.Batches[1].NumElements == 2);
		CHECK(Out.Batches[2].Shader == ESlateShader::Line);
		CHECK(Out.Batches[2].NumIndices == 12); // three points: two segments
		CHECK(Out.Batches[3].Shader == ESlateShader::Default);
	}

	TEST_CASE("Viewport elements draw opaque unless asked to blend")
	{
		struct FFakeTarget final : ISlateRenderTarget
		{
			FIntPoint GetSize() const override { return { 64, 64 }; }
		};
		auto Target = std::make_shared<FFakeTarget>();
		FSlateWindowElementList List;
		List.AddViewport(0, Box(0, 0, 64, 64), Target);
		List.AddViewport(0, Box(0, 0, 64, 64), Target, true);
		const FBatchedElements Out = BatchList(List);
		REQUIRE(Out.Batches.size() == 2);
		CHECK(Out.Batches[0].Blend == ESlateBlendMode::Opaque);
		CHECK(Out.Batches[0].Texture == 5000);
		CHECK(Out.Batches[1].Blend == ESlateBlendMode::Alpha);
	}

	TEST_CASE("9-slice boxes tessellate into nine quads, borders into eight")
	{
		const auto Image = MakeImage(16, 16);
		FSlateWindowElementList List;
		List.AddBox(0, Box(0, 0, 100, 60), FBrush::NineSlice(Image, FMargin(4.0f)));
		List.AddBox(0, Box(0, 0, 100, 60), FBrush::NineSlice(Image, FMargin(4.0f), FColor::White(), false));
		const FBatchedElements Out = BatchList(List);
		REQUIRE(Out.Batches.size() == 1);
		CHECK(Out.Indices.size() == (9 + 8) * 6);

		// Corner cells keep their pixel size; UVs reach 4/16 into the image.
		const FSlateVertex& TopLeftCellBR = Out.Vertices[2];
		CHECK(TopLeftCellBR.Position == FVector2(4.0f, 4.0f));
		CHECK(TopLeftCellBR.UV.X == doctest::Approx(0.25f));
		const FSlateVertex& Last = Out.Vertices[9 * 4 - 2]; // bottom-right corner of the last cell
		CHECK(Last.Position == FVector2(100.0f, 60.0f));
		CHECK(Last.UV.X == doctest::Approx(1.0f));
	}

	TEST_CASE("Boxes snap to whole pixels at fractional DPI scales")
	{
		FSlateWindowElementList List;
		List.AddSolidBox(0, Box(10.3f, 7.1f, 33.3f, 9.9f, 1.25f), FColor::Red());
		const FBatchedElements Out = BatchList(List);
		REQUIRE(Out.Vertices.size() == 4);
		for (const FSlateVertex& V : Out.Vertices)
		{
			CHECK(V.Position.X == FMath::RoundToFloat(V.Position.X));
			CHECK(V.Position.Y == FMath::RoundToFloat(V.Position.Y));
		}
		CHECK(Out.Vertices[0].Position == FVector2(13.0f, 9.0f)); // 12.875, 8.875
	}

	TEST_CASE("Text produces one quad per visible glyph on the glyph page")
	{
		FTestFonts& Fonts = FTestFonts::Get();
		REQUIRE(Fonts.IsValid());
		const float Scale = 1.5f;
		const FSlateFontInfo Font(Fonts.Latin, 14.0f);
		const auto Shaped = Fonts.Cache.ShapeText("Hi there", Font, Scale);

		FSlateWindowElementList List;
		List.AddText(0, FPaintGeometry::Make({ 10.0f, 10.0f }, Shaped->GetLogicalSize(), Scale), Shaped, FColor::White());
		const FBatchedElements Out = BatchList(List, &Fonts.Cache);
		REQUIRE(Out.Batches.size() == 1);
		CHECK(Out.Batches[0].Texture == 100); // glyph page 0
		CHECK(Out.Batches[0].NumIndices == 7 * 6); // eight glyphs, one is a space
		for (const FSlateVertex& V : Out.Vertices)
		{
			// Glyph bitmaps map 1:1 onto pixels.
			CHECK(V.Position.X == FMath::RoundToFloat(V.Position.X));
			CHECK(V.Position.Y == FMath::RoundToFloat(V.Position.Y));
		}
	}
}
