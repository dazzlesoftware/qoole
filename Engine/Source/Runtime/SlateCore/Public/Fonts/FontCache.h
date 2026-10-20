// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Fonts (docs/GenesisUI.md §5.4): FreeType rasterizes, HarfBuzz shapes.
//
// - FFontFace: one font file.
// - FCompositeFont: a primary face plus fallbacks; text is split into runs by
//   which face has the characters (e.g. Latin + Devanagari).
// - FSlateFontInfo: a composite font at a size in logical units (1/96 inch).
// - FSlateFontCache: shapes text into glyph runs at the final pixel size
//   (size x DPI scale), measures it, and rasterizes glyphs on demand into
//   alpha (R8) atlas pages that the renderer uploads.
//
// Not thread-safe; use it from the UI thread.

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateTextureAtlas.h"

struct FT_LibraryRec_;
struct FT_FaceRec_;
struct hb_font_t;
struct hb_buffer_t;

class FFontFace
{
public:
	~FFontFace();
	FFontFace(const FFontFace&) = delete;
	FFontFace& operator=(const FFontFace&) = delete;

	uint32 GetId() const { return Id; }
	const FString& GetFamilyName() const { return FamilyName; }
	const FString& GetStyleName() const { return StyleName; }
	bool HasCharacter(char32_t CodePoint) const;

	// Sets the current pixel size of the underlying FreeType face and
	// HarfBuzz font (26.6 fixed point).
	void SetPixelSize(int32 Size26_6) const;

	FT_FaceRec_* GetFTFace() const { return Face; }
	hb_font_t* GetHBFont() const { return HBFont; }

private:
	friend class FSlateFontCache;
	FFontFace() = default;

	uint32 Id = 0;
	FString FamilyName;
	FString StyleName;
	TSharedPtr<FT_LibraryRec_> Library; // released after the face
	TArray<uint8> Data; // FreeType reads the file from memory; keep it alive
	FT_FaceRec_* Face = nullptr;
	hb_font_t* HBFont = nullptr;
	mutable int32 CurrentSize26_6 = 0;
};

struct FCompositeFont
{
	FName Name;
	TArray<TSharedPtr<FFontFace>> Faces; // [0] is the primary face

	FCompositeFont() = default;
	FCompositeFont(FName InName, TArray<TSharedPtr<FFontFace>> InFaces) : Name(InName), Faces(std::move(InFaces)) {}
};

struct FSlateFontInfo
{
	TSharedPtr<const FCompositeFont> Font;
	float Size = 13.0f; // logical units (pixels at 100% scale)

	FSlateFontInfo() = default;
	FSlateFontInfo(TSharedPtr<const FCompositeFont> InFont, float InSize) : Font(std::move(InFont)), Size(InSize) {}
};

struct FShapedGlyphEntry
{
	const FFontFace* Face = nullptr;
	uint32 GlyphIndex = 0;
	float X = 0.0f;       // pen position relative to the run origin, pixels
	float Y = 0.0f;       // offset from the baseline, pixels (y down)
	float Advance = 0.0f; // pixels
	uint32 Cluster = 0;   // byte offset into the source text
};

// Text shaped at one pixel size. All metrics are in physical pixels.
struct FShapedGlyphSequence
{
	TArray<FShapedGlyphEntry> Glyphs;
	float PixelSize = 0.0f;   // font size in pixels (FSlateFontInfo::Size x Scale)
	float Scale = 1.0f;       // the DPI/layout scale it was shaped for
	float Width = 0.0f;
	float Ascender = 0.0f;    // baseline to top of line, positive
	float Descender = 0.0f;   // baseline to bottom of line, positive
	float LineHeight = 0.0f;

	// Size in logical units, for layout.
	FVector2 GetLogicalSize() const { return FVector2(Width, LineHeight) / Scale; }
	float GetLogicalBaseline() const { return Ascender / Scale; }
};

// A rasterized glyph in an atlas page.
struct FCachedGlyph
{
	int32 PageIndex = -1; // -1 for glyphs without pixels (spaces)
	FIntRect AtlasRect;   // pixels in the page
	int32 BearingX = 0;   // pen to left edge of the bitmap, pixels
	int32 BearingY = 0;   // baseline to top edge of the bitmap, pixels (positive up)
};

// One alpha atlas page. The renderer uploads DirtyRect and calls ClearDirty.
struct FFontAtlasPage
{
	static constexpr int32 Size = 1024;

	TArray<uint8> Pixels; // Size x Size, one byte per pixel
	FSlateTextureAtlas Packer{ Size, Size, 1 };
	TOptional<FIntRect> DirtyRect;
	uint32 Revision = 0;

	void MarkDirty(const FIntRect& Rect);
};

class FSlateFontCache
{
public:
	FSlateFontCache();
	~FSlateFontCache();
	FSlateFontCache(const FSlateFontCache&) = delete;
	FSlateFontCache& operator=(const FSlateFontCache&) = delete;

	TSharedPtr<FFontFace> LoadFace(const FString& Path);
	TSharedPtr<FFontFace> LoadFaceFromMemory(TArray<uint8> Data);

	// Shapes a single line (no wrapping; newlines are not special yet).
	// Results are cached by (text, font, pixel size).
	TSharedRef<const FShapedGlyphSequence> ShapeText(std::string_view Text, const FSlateFontInfo& Font, float Scale);

	// Logical size of a single line of text.
	FVector2 MeasureText(std::string_view Text, const FSlateFontInfo& Font, float Scale = 1.0f);

	// Rasterizes on first use. PixelSize is FShapedGlyphSequence::PixelSize.
	const FCachedGlyph& GetGlyph(const FFontFace& Face, float PixelSize, uint32 GlyphIndex);

	int32 GetPageCount() const { return static_cast<int32>(Pages.size()); }
	FFontAtlasPage& GetPage(int32 Index) { return *Pages[Index]; }

	// Drops cached shaping results (glyph pages stay).
	void ClearShapingCache() { ShapeCache.clear(); }
	int32 GetShapingCacheSize() const { return static_cast<int32>(ShapeCache.size()); }

	static int32 ToSize26_6(float PixelSize) { return FMath::Max(1, FMath::RoundToInt(PixelSize * 64.0f)); }

private:
	struct FGlyphKey
	{
		uint32 FaceId;
		int32 Size26_6;
		uint32 GlyphIndex;
		bool operator==(const FGlyphKey&) const = default;
	};
	struct FGlyphKeyHash
	{
		SIZE_T operator()(const FGlyphKey& K) const noexcept
		{
			return std::hash<uint64>{}((uint64(K.FaceId) << 40) ^ (uint64(K.Size26_6) << 20) ^ K.GlyphIndex);
		}
	};

	TSharedPtr<FT_LibraryRec_> Library; // shared with the faces
	hb_buffer_t* Buffer = nullptr;
	uint32 NextFaceId = 1;
	TArray<TUniquePtr<FFontAtlasPage>> Pages;
	TMap<FGlyphKey, FCachedGlyph, FGlyphKeyHash> Glyphs;
	TMap<FString, TSharedRef<const FShapedGlyphSequence>> ShapeCache;
};

DECLARE_LOG_CATEGORY_EXTERN(LogFont, Log);
