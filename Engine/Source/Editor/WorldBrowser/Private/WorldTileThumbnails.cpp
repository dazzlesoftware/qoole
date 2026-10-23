// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "WorldTileThumbnails.h"
#include "World/WorldSceneSnapshot.h"
#include "Landscape/Landscape.h"
#include <algorithm>
#include <cmath>

namespace
{
	// Light from the north-west and above, as on a relief map.
	constexpr float LightX = -0.45f, LightY = 0.45f, LightZ = 0.77f;
	constexpr float AmbientShade = 0.45f, DirectShade = 0.75f;

	FString KeyOf(const FWorldCompositionTile& Tile)
	{
		return Tile.Map.Id.IsEmpty() ? Tile.Map.Path : Tile.Map.Id;
	}
} // namespace

TSharedPtr<FSlateTextureData> FWorldTileThumbnails::Find(const FWorldCompositionTile& Tile) const
{
	const TMap<std::string, FEntry>::const_iterator It = Entries.find(KeyOf(Tile).ToStdString());
	return It == Entries.end() ? nullptr : It->second.Image;
}

bool FWorldTileThumbnails::BuildNext(const FWorldComposition& World, const FAssetRegistry& Registry)
{
	for (const FWorldCompositionTile& Tile : World.GetTiles())
	{
		const std::string Key = KeyOf(Tile).ToStdString();
		const TMap<std::string, FEntry>::const_iterator It = Entries.find(Key);
		if (It != Entries.end() && It->second.Revision == Registry.GetRevision())
			continue;
		FEntry Entry;
		Entry.Revision = Registry.GetRevision();
		FAssetPackage Package;
		FWorldSceneSnapshot Snapshot;
		FString Error;
		if (Registry.Load(Tile.Map, Package, Error, "Genesis.World") &&
		    FWorldSceneSnapshot::Load(Package, &Registry, Snapshot, Error))
			for (const FWorldSceneItem& Item : Snapshot.GetItems())
				if (Item.Terrain)
				{
					Entry.Image = Render(*Item.Terrain);
					break;
				}
		Entries[Key] = std::move(Entry);
		return true;
	}
	return false;
}

TSharedPtr<FSlateTextureData> FWorldTileThumbnails::Render(const Landscape& Terrain)
{
	const int32 PointsX = Terrain.GetPointsX(), PointsY = Terrain.GetPointsY();
	const float Aspect = Terrain.GetSizeX() / Terrain.GetSizeY();
	const int32 Width = std::max(
	    2, Aspect >= 1 ? std::min(MaxPixels, PointsX) : static_cast<int32>(std::min(MaxPixels, PointsY) * Aspect));
	const int32 Height = std::max(2, Aspect >= 1 ? static_cast<int32>(Width / Aspect) : std::min(MaxPixels, PointsY));
	const float Length = std::sqrt(LightX * LightX + LightY * LightY + LightZ * LightZ);
	unsigned char BaseR = 0, BaseG = 0, BaseB = 0;
	Landscape::GetBaseColor(BaseR, BaseG, BaseB);
	TArray<uint8> Pixels(static_cast<size_t>(Width) * Height * 4);
	for (int32 Row = 0; Row < Height; ++Row)
		for (int32 Column = 0; Column < Width; ++Column)
		{
			const int32 X = static_cast<int32>(std::lround(static_cast<float>(Column) * (PointsX - 1) / (Width - 1)));
			// Image rows run from the top (+Y); the grid's rows from -Y.
			const int32 Y =
			    PointsY - 1 - static_cast<int32>(std::lround(static_cast<float>(Row) * (PointsY - 1) / (Height - 1)));
			const unsigned int Color =
			    Terrain.HasColors() ? Terrain.GetColor(X, Y) : (BaseR | (BaseG << 8) | (BaseB << 16) | 0xff000000u);
			const Vector3d& Normal = Terrain.GetNormal(X, Y);
			const float Light =
			    std::max(0.0f, (Normal.GetX() * LightX + Normal.GetY() * LightY + Normal.GetZ() * LightZ) / Length);
			const float Shade = AmbientShade + DirectShade * Light;
			uint8* Pixel = &Pixels[(static_cast<size_t>(Row) * Width + Column) * 4];
			for (int32 Channel = 0; Channel < 3; ++Channel)
				Pixel[Channel] = static_cast<uint8>(
				    std::clamp(static_cast<float>((Color >> (8 * Channel)) & 0xff) * Shade, 0.0f, 255.0f));
			Pixel[3] = 255;
		}
	return std::make_shared<FSlateTextureData>(Width, Height, std::move(Pixels));
}
