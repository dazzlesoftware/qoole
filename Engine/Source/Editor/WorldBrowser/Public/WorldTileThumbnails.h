// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "World/WorldComposition.h"
#include "Rendering/RenderingCommon.h"
class Landscape;

// Shaded relief images of each region's terrain, for the world grid. Images
// are built one at a time and rebuilt after the asset registry rescans, so a
// region re-saved in the level editor shows its new terrain.
class FWorldTileThumbnails
{
public:
	static constexpr int32 MaxPixels = 128;
	// The tile's image; null while it has none (not built yet, or its map has no landscape).
	TSharedPtr<FSlateTextureData> Find(const FWorldCompositionTile& Tile) const;
	// Builds at most one missing or stale image. True when it did, so the caller repaints.
	bool BuildNext(const FWorldComposition& World, const FAssetRegistry& Registry);
	// The terrain seen from above (+Y up): its painted colours, lit from the north-west.
	static TSharedPtr<FSlateTextureData> Render(const Landscape& Terrain);

private:
	struct FEntry
	{
		TSharedPtr<FSlateTextureData> Image;
		uint64 Revision = 0;
	};
	TMap<std::string, FEntry> Entries; // by map identity
};
