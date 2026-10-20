// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include <ostream> // doctest streams std::string_view messages

#include "doctest.h"

#include "CoreMinimal.h"
#include "ElementBatcher.h"
#include "Fonts/FontCache.h"

#include <ostream>

// Lets doctest print FStrings in failed checks.
inline std::ostream& operator<<(std::ostream& Stream, const FString& String)
{
	return Stream << String.View();
}

// Captures log output so tests can assert on warnings and errors.
class FTestLogCapture final : public FOutputDevice
{
public:
	FTestLogCapture() { FLog::AddDevice(this); }
	~FTestLogCapture() override { FLog::RemoveDevice(this); }

	void Serialize(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message) override
	{
		(void)Category;
		if (Verbosity <= ELogVerbosity::Warning)
		{
			Problems.emplace_back(Message);
		}
	}

	TArray<FString> Problems;
};

// Hands out fake texture ids: 1 = white, 100 + n = glyph page n, image ids
// map to 1000 + id, render targets to 5000 + n.
class FFakeResolver final : public IUIResourceResolver
{
public:
	FUITextureId GetWhiteTexture() override { return 1; }
	FUIResolvedTexture ResolveImage(const FUIImage& Image) override
	{
		++ImageResolves;
		return { 1000 + Image.GetId(), FRect(0.0f, 0.0f, 1.0f, 1.0f) };
	}
	FUIResolvedTexture ResolveRenderTarget(IUIRenderTarget& Target) override
	{
		(void)Target;
		return { 5000, FRect(0.0f, 0.0f, 1.0f, 1.0f) };
	}
	FUITextureId GetGlyphPageTexture(int32 PageIndex) override { return 100 + static_cast<FUITextureId>(PageIndex); }

	int32 ImageResolves = 0;
};

// The Noto fonts in Engine/Content/Slate/Fonts, loaded once.
struct FTestFonts
{
	static FTestFonts& Get();

	FFontCache Cache;
	TSharedPtr<FFontFace> Regular;
	TSharedPtr<FFontFace> Devanagari;
	TSharedPtr<FCompositeFont> Latin;     // Noto Sans only
	TSharedPtr<FCompositeFont> Composite; // Noto Sans + Noto Sans Devanagari

	bool IsValid() const { return Regular && Devanagari; }
};
