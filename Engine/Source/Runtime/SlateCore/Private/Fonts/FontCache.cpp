// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Fonts/FontCache.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>

DEFINE_LOG_CATEGORY(LogFont, Log);

namespace
{
	// Vertical-only hinting: crisp horizontal stems at every size without
	// distorting glyph shapes or advances.
	constexpr FT_Int32 GlyphLoadFlags = FT_LOAD_TARGET_LIGHT;

	bool StaysWithPreviousRun(hb_unicode_funcs_t* Unicode, char32_t CodePoint)
	{
		switch (hb_unicode_general_category(Unicode, CodePoint))
		{
		case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
		case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
		case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
		case HB_UNICODE_GENERAL_CATEGORY_FORMAT: // ZWJ, ZWNJ
			return true;
		default:
			break;
		}
		const hb_script_t Script = hb_unicode_script(Unicode, CodePoint);
		return Script == HB_SCRIPT_COMMON || Script == HB_SCRIPT_INHERITED;
	}
}

// ---------------------------------------------------------------------------

FFontFace::~FFontFace()
{
	if (HBFont)
	{
		hb_font_destroy(HBFont);
	}
	if (Face)
	{
		FT_Done_Face(Face);
	}
}

bool FFontFace::HasCharacter(char32_t CodePoint) const
{
	return FT_Get_Char_Index(Face, CodePoint) != 0;
}

void FFontFace::SetPixelSize(int32 Size26_6) const
{
	if (Size26_6 != CurrentSize26_6)
	{
		FT_Set_Char_Size(Face, 0, Size26_6, 72, 72); // 72 dpi: 1 point == 1 pixel
		hb_ft_font_changed(HBFont);
		CurrentSize26_6 = Size26_6;
	}
}

void FFontAtlasPage::MarkDirty(const FIntRect& Rect)
{
	if (!DirtyRect)
	{
		DirtyRect = Rect;
	}
	else
	{
		DirtyRect = FIntRect(FMath::Min(DirtyRect->Left, Rect.Left), FMath::Min(DirtyRect->Top, Rect.Top),
			FMath::Max(DirtyRect->Right, Rect.Right), FMath::Max(DirtyRect->Bottom, Rect.Bottom));
	}
	++Revision;
}

// ---------------------------------------------------------------------------

FSlateFontCache::FSlateFontCache()
{
	FT_Library RawLibrary = nullptr;
	if (FT_Init_FreeType(&RawLibrary) == 0)
	{
		Library = TSharedPtr<FT_LibraryRec_>(RawLibrary, [](FT_Library L) { FT_Done_FreeType(L); });
	}
	else
	{
		GE_LOG(LogFont, Error, "FT_Init_FreeType failed");
	}
	Buffer = hb_buffer_create();
}

FSlateFontCache::~FSlateFontCache()
{
	ShapeCache.clear();
	hb_buffer_destroy(Buffer);
}

TSharedPtr<FFontFace> FSlateFontCache::LoadFace(const FString& Path)
{
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, Path))
	{
		GE_LOG(LogFont, Error, "Cannot read font file \"{}\"", Path);
		return nullptr;
	}
	return LoadFaceFromMemory(std::move(Data));
}

TSharedPtr<FFontFace> FSlateFontCache::LoadFaceFromMemory(TArray<uint8> Data)
{
	if (!Library || Data.empty())
	{
		return nullptr;
	}
	TSharedPtr<FFontFace> Face(new FFontFace());
	Face->Library = Library;
	Face->Data = std::move(Data);
	if (FT_New_Memory_Face(Library.get(), Face->Data.data(), static_cast<FT_Long>(Face->Data.size()), 0, &Face->Face) != 0)
	{
		GE_LOG(LogFont, Error, "FreeType cannot open the font");
		return nullptr;
	}
	FT_Select_Charmap(Face->Face, FT_ENCODING_UNICODE);
	Face->HBFont = hb_ft_font_create_referenced(Face->Face);
	hb_ft_font_set_load_flags(Face->HBFont, GlyphLoadFlags);
	Face->Id = NextFaceId++;
	Face->FamilyName = Face->Face->family_name ? Face->Face->family_name : "";
	Face->StyleName = Face->Face->style_name ? Face->Face->style_name : "";
	GE_LOG(LogFont, Log, "Loaded font {} {} ({} glyphs)", Face->FamilyName, Face->StyleName, Face->Face->num_glyphs);
	return Face;
}

TSharedRef<const FShapedGlyphSequence> FSlateFontCache::ShapeText(std::string_view Text, const FSlateFontInfo& Font, float Scale)
{
	check(Font.Font && !Font.Font->Faces.empty() && Scale > 0.0f);
	const float PixelSize = Font.Size * Scale;
	const int32 Size26_6 = ToSize26_6(PixelSize);

	const FString Key = FString::Format("{}|{}|{}|{}", static_cast<const void*>(Font.Font.get()), Size26_6, Scale, Text);
	if (auto It = ShapeCache.find(Key); It != ShapeCache.end())
	{
		return It->second;
	}

	auto Result = std::make_shared<FShapedGlyphSequence>();
	Result->PixelSize = static_cast<float>(Size26_6) / 64.0f;
	Result->Scale = Scale;

	const TArray<TSharedPtr<FFontFace>>& Faces = Font.Font->Faces;
	const FFontFace& Primary = *Faces[0];
	Primary.SetPixelSize(Size26_6);
	const FT_Size_Metrics& PrimaryMetrics = Primary.GetFTFace()->size->metrics;
	float Ascender = static_cast<float>(PrimaryMetrics.ascender) / 64.0f;
	float Descender = static_cast<float>(-PrimaryMetrics.descender) / 64.0f;
	float LineGap = static_cast<float>(PrimaryMetrics.height) / 64.0f - (Ascender + Descender);

	// Itemize: pick a face per code point, keeping marks, joiners and
	// script-neutral characters with the run before them.
	struct FRun
	{
		int32 FaceIndex;
		uint32 Start;
		uint32 Length;
	};
	TArray<FRun> Runs;
	hb_unicode_funcs_t* Unicode = hb_unicode_funcs_get_default();
	const char* Begin = Text.data();
	const char* End = Begin + Text.size();
	for (const char* Cursor = Begin; Cursor < End;)
	{
		const char* CharStart = Cursor;
		const char32_t CodePoint = FUtf8::Decode(Cursor, End);
		int32 FaceIndex = 0;
		if (!Runs.empty() && StaysWithPreviousRun(Unicode, CodePoint) && Faces[Runs.back().FaceIndex]->HasCharacter(CodePoint))
		{
			FaceIndex = Runs.back().FaceIndex;
		}
		else
		{
			for (int32 I = 0; I < static_cast<int32>(Faces.size()); ++I)
			{
				if (Faces[I]->HasCharacter(CodePoint))
				{
					FaceIndex = I;
					break;
				}
			}
		}
		const uint32 Offset = static_cast<uint32>(CharStart - Begin);
		const uint32 Length = static_cast<uint32>(Cursor - CharStart);
		if (!Runs.empty() && Runs.back().FaceIndex == FaceIndex)
		{
			Runs.back().Length += Length;
		}
		else
		{
			Runs.push_back({ FaceIndex, Offset, Length });
		}
	}

	float PenX = 0.0f;
	for (const FRun& Run : Runs)
	{
		const FFontFace& Face = *Faces[Run.FaceIndex];
		Face.SetPixelSize(Size26_6);
		if (Run.FaceIndex != 0)
		{
			const FT_Size_Metrics& M = Face.GetFTFace()->size->metrics;
			Ascender = FMath::Max(Ascender, static_cast<float>(M.ascender) / 64.0f);
			Descender = FMath::Max(Descender, static_cast<float>(-M.descender) / 64.0f);
		}

		hb_buffer_clear_contents(Buffer);
		// The whole text is passed as context so shaping across run edges
		// (e.g. joining) sees its neighbours.
		hb_buffer_add_utf8(Buffer, Text.data(), static_cast<int>(Text.size()), Run.Start, static_cast<int>(Run.Length));
		hb_buffer_guess_segment_properties(Buffer);
		hb_buffer_set_direction(Buffer, HB_DIRECTION_LTR); // right-to-left is a later milestone
		hb_shape(Face.GetHBFont(), Buffer, nullptr, 0);

		unsigned int Count = 0;
		const hb_glyph_info_t* Infos = hb_buffer_get_glyph_infos(Buffer, &Count);
		const hb_glyph_position_t* Positions = hb_buffer_get_glyph_positions(Buffer, nullptr);
		for (unsigned int I = 0; I < Count; ++I)
		{
			FShapedGlyphEntry Glyph;
			Glyph.Face = &Face;
			Glyph.GlyphIndex = Infos[I].codepoint;
			Glyph.Cluster = Infos[I].cluster;
			Glyph.X = PenX + static_cast<float>(Positions[I].x_offset) / 64.0f;
			Glyph.Y = -static_cast<float>(Positions[I].y_offset) / 64.0f;
			Glyph.Advance = static_cast<float>(Positions[I].x_advance) / 64.0f;
			PenX += Glyph.Advance;
			Result->Glyphs.push_back(Glyph);
		}
	}

	Result->Width = PenX;
	Result->Ascender = FMath::CeilToFloat(Ascender);
	Result->Descender = FMath::CeilToFloat(Descender);
	Result->LineHeight = Result->Ascender + Result->Descender + FMath::RoundToFloat(FMath::Max(0.0f, LineGap));

	TSharedRef<const FShapedGlyphSequence> Shared(std::static_pointer_cast<const FShapedGlyphSequence>(Result));
	ShapeCache.emplace(Key, Shared);
	return Shared;
}

FVector2 FSlateFontCache::MeasureText(std::string_view Text, const FSlateFontInfo& Font, float Scale)
{
	return ShapeText(Text, Font, Scale)->GetLogicalSize();
}

const FCachedGlyph& FSlateFontCache::GetGlyph(const FFontFace& Face, float PixelSize, uint32 GlyphIndex)
{
	const int32 Size26_6 = ToSize26_6(PixelSize);
	const FGlyphKey Key{ Face.GetId(), Size26_6, GlyphIndex };
	if (auto It = Glyphs.find(Key); It != Glyphs.end())
	{
		return It->second;
	}

	FCachedGlyph Cached;
	Face.SetPixelSize(Size26_6);
	FT_Face FTFace = Face.GetFTFace();
	if (FT_Load_Glyph(FTFace, GlyphIndex, GlyphLoadFlags) == 0 && FT_Render_Glyph(FTFace->glyph, FT_RENDER_MODE_NORMAL) == 0)
	{
		const FT_GlyphSlot Slot = FTFace->glyph;
		const FT_Bitmap& Bitmap = Slot->bitmap;
		Cached.BearingX = Slot->bitmap_left;
		Cached.BearingY = Slot->bitmap_top;
		const int32 Width = static_cast<int32>(Bitmap.width);
		const int32 Height = static_cast<int32>(Bitmap.rows);
		if (Width > 0 && Height > 0 && Bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
		{
			TOptional<FIntRect> Slot2;
			int32 PageIndex = static_cast<int32>(Pages.size()) - 1;
			if (PageIndex >= 0)
			{
				Slot2 = Pages[PageIndex]->Packer.Allocate(Width, Height);
			}
			if (!Slot2)
			{
				auto Page = MakeUnique<FFontAtlasPage>();
				Page->Pixels.assign(SIZE_T(FFontAtlasPage::Size) * FFontAtlasPage::Size, 0);
				Pages.push_back(std::move(Page));
				PageIndex = static_cast<int32>(Pages.size()) - 1;
				Slot2 = Pages[PageIndex]->Packer.Allocate(Width, Height);
				GE_LOG(LogFont, Log, "Glyph atlas page {} created", PageIndex);
			}
			if (Slot2)
			{
				FFontAtlasPage& Page = *Pages[PageIndex];
				for (int32 Row = 0; Row < Height; ++Row)
				{
					const uint8* Source = Bitmap.buffer + (Bitmap.pitch >= 0 ? Row * Bitmap.pitch : (Height - 1 - Row) * -Bitmap.pitch);
					memcpy(&Page.Pixels[SIZE_T(Slot2->Top + Row) * FFontAtlasPage::Size + Slot2->Left], Source, SIZE_T(Width));
				}
				Page.MarkDirty(*Slot2);
				Cached.PageIndex = PageIndex;
				Cached.AtlasRect = *Slot2;
			}
			else
			{
				GE_LOG(LogFont, Warning, "Glyph {} ({}x{}) does not fit an atlas page", GlyphIndex, Width, Height);
			}
		}
	}
	return Glyphs.emplace(Key, Cached).first->second;
}
