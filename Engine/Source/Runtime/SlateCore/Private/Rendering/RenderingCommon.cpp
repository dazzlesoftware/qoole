// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Rendering/RenderingCommon.h"

#include <atomic>

namespace
{
	std::atomic<uint64> GNextImageId{ 1 };
}

FSlateTextureData::FSlateTextureData(int32 InWidth, int32 InHeight, TArray<uint8> InPixels, FName InName)
	: Id(GNextImageId.fetch_add(1))
	, Name(InName)
	, Width(InWidth)
	, Height(InHeight)
	, Pixels(std::move(InPixels))
{
	check(Width > 0 && Height > 0 && Pixels.size() == SIZE_T(Width) * Height * 4);
}

TSharedRef<FSlateTextureData> FSlateTextureData::CreateSolid(int32 Width, int32 Height, FColor Color, FName Name)
{
	TArray<uint8> Pixels(SIZE_T(Width) * Height * 4);
	for (SIZE_T I = 0; I < Pixels.size(); I += 4)
	{
		Pixels[I + 0] = Color.R;
		Pixels[I + 1] = Color.G;
		Pixels[I + 2] = Color.B;
		Pixels[I + 3] = Color.A;
	}
	return MakeShared<FSlateTextureData>(Width, Height, std::move(Pixels), Name);
}

void FSlateTextureData::UpdatePixels(TArray<uint8> NewPixels)
{
	check(NewPixels.size() == Pixels.size());
	Pixels = std::move(NewPixels);
	++Revision;
}
