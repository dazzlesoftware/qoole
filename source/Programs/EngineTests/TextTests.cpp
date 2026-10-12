// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestHelpers.h"

FTestFonts& FTestFonts::Get()
{
	static FTestFonts Fonts;
	static const bool bLoaded = []
	{
		const FString Dir = FPaths::FindContentDir();
		Fonts.Regular = Fonts.Cache.LoadFace(Dir + "Fonts/NotoSans-Regular.ttf");
		Fonts.Devanagari = Fonts.Cache.LoadFace(Dir + "Fonts/NotoSansDevanagari-Regular.ttf");
		Fonts.Latin = std::make_shared<FCompositeFont>(FName("Latin"), TArray<TSharedPtr<FFontFace>>{ Fonts.Regular });
		Fonts.Composite = std::make_shared<FCompositeFont>(FName("Composite"), TArray<TSharedPtr<FFontFace>>{ Fonts.Regular, Fonts.Devanagari });
		return true;
	}();
	(void)bLoaded;
	return Fonts;
}

TEST_SUITE("Text")
{
	TEST_CASE("Measurement grows with the text and is stable across DPI scales")
	{
		FTestFonts& Fonts = FTestFonts::Get();
		REQUIRE(Fonts.IsValid());
		const FFontInfo Font(Fonts.Latin, 16.0f);

		const FVector2 Empty = Fonts.Cache.MeasureText("", Font);
		const FVector2 Short = Fonts.Cache.MeasureText("Hello", Font);
		const FVector2 Long = Fonts.Cache.MeasureText("Hello, world", Font);
		CHECK(Empty.X == 0.0f);
		CHECK(Empty.Y > 16.0f); // a line still has height
		CHECK(Short.X > 30.0f);
		CHECK(Short.X < 60.0f);
		CHECK(Long.X > Short.X);
		CHECK(Short.Y == Long.Y);

		// Shaped at 2x the pixel size, the logical size stays about the same
		// (hinting may move it by a fraction of a pixel per glyph).
		for (const float Scale : { 1.25f, 1.5f, 2.0f })
		{
			const FVector2 Scaled = Fonts.Cache.MeasureText("Hello, world", Font, Scale);
			CHECK(Scaled.X == doctest::Approx(Long.X).epsilon(0.05));
			CHECK(Scaled.Y == doctest::Approx(Long.Y).epsilon(0.1));
		}
	}

	TEST_CASE("Advances add up to the measured width")
	{
		FTestFonts& Fonts = FTestFonts::Get();
		REQUIRE(Fonts.IsValid());
		const auto Shaped = Fonts.Cache.ShapeText("Typography", FFontInfo(Fonts.Latin, 20.0f), 1.0f);
		float Sum = 0.0f;
		for (const FShapedGlyph& Glyph : Shaped->Glyphs)
		{
			Sum += Glyph.Advance;
		}
		CHECK(Sum == doctest::Approx(Shaped->Width));
		CHECK(Shaped->Glyphs.size() == 10);
		CHECK(Shaped->PixelSize == 20.0f);
	}

	TEST_CASE("Devanagari is shaped (conjuncts and marks), using the fallback face")
	{
		FTestFonts& Fonts = FTestFonts::Get();
		REQUIRE(Fonts.IsValid());
		const FFontInfo Font(Fonts.Composite, 18.0f);
		const std::string_view Namaste = "नमस्ते"; // 6 code points
		const auto Shaped = Fonts.Cache.ShapeText(Namaste, Font, 1.0f);
		CHECK(FUtf8::CountCodePoints(Namaste) == 6);
		// स + ् + त form a conjunct and े is a mark: fewer glyphs than code points.
		CHECK(Shaped->Glyphs.size() < 6);
		for (const FShapedGlyph& Glyph : Shaped->Glyphs)
		{
			CHECK(Glyph.Face == Fonts.Devanagari.get());
			CHECK(Glyph.GlyphIndex != 0); // no .notdef boxes
		}

		const auto Mixed = Fonts.Cache.ShapeText("Hello नमस्ते!", Font, 1.0f);
		bool bUsedLatin = false;
		bool bUsedDevanagari = false;
		for (const FShapedGlyph& Glyph : Mixed->Glyphs)
		{
			bUsedLatin |= Glyph.Face == Fonts.Regular.get();
			bUsedDevanagari |= Glyph.Face == Fonts.Devanagari.get();
			CHECK(Glyph.GlyphIndex != 0);
		}
		CHECK(bUsedLatin);
		CHECK(bUsedDevanagari);
		CHECK(Mixed->Width > Shaped->Width);
		// The line is tall enough for Devanagari's head line and marks.
		CHECK(Mixed->LineHeight >= Fonts.Cache.ShapeText("Hello", FFontInfo(Fonts.Latin, 18.0f), 1.0f)->LineHeight);
	}

	TEST_CASE("Shaping results are cached per pixel size")
	{
		FTestFonts& Fonts = FTestFonts::Get();
		REQUIRE(Fonts.IsValid());
		const FFontInfo Font(Fonts.Latin, 12.0f);
		const auto A = Fonts.Cache.ShapeText("cache me", Font, 1.0f);
		const auto B = Fonts.Cache.ShapeText("cache me", Font, 1.0f);
		const auto C = Fonts.Cache.ShapeText("cache me", Font, 2.0f);
		CHECK(A.ToSharedPtr() == B.ToSharedPtr());
		CHECK(A.ToSharedPtr() != C.ToSharedPtr());
		CHECK(C->PixelSize == 24.0f);
	}

	TEST_CASE("Glyphs rasterize once into the alpha atlas")
	{
		FTestFonts& Fonts = FTestFonts::Get();
		REQUIRE(Fonts.IsValid());
		const auto Shaped = Fonts.Cache.ShapeText("A", FFontInfo(Fonts.Latin, 40.0f), 1.0f);
		REQUIRE(Shaped->Glyphs.size() == 1);
		const FShapedGlyph& Glyph = Shaped->Glyphs[0];
		const FCachedGlyph& First = Fonts.Cache.GetGlyph(*Glyph.Face, Shaped->PixelSize, Glyph.GlyphIndex);
		const FCachedGlyph& Again = Fonts.Cache.GetGlyph(*Glyph.Face, Shaped->PixelSize, Glyph.GlyphIndex);
		CHECK(&First == &Again);
		REQUIRE(First.PageIndex >= 0);
		CHECK(First.AtlasRect.Width() > 15);
		CHECK(First.AtlasRect.Height() > 20);
		CHECK(First.BearingY > 20); // a capital stands on the baseline

		// The bitmap has ink.
		const FFontAtlasPage& Page = Fonts.Cache.GetPage(First.PageIndex);
		int32 Ink = 0;
		for (int32 Y = First.AtlasRect.Top; Y < First.AtlasRect.Bottom; ++Y)
		{
			for (int32 X = First.AtlasRect.Left; X < First.AtlasRect.Right; ++X)
			{
				Ink += Page.Pixels[SIZE_T(Y) * FFontAtlasPage::Size + X] > 128 ? 1 : 0;
			}
		}
		CHECK(Ink > 50);
	}
}
