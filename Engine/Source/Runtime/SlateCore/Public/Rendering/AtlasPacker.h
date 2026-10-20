// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Skyline (bottom-left) rectangle packer for atlas pages: glyphs and small UI
// images. Each allocation is surrounded by Padding empty pixels so linear
// filtering never bleeds neighbours in.

#pragma once

#include "CoreMinimal.h"

class FAtlasPacker
{
public:
	FAtlasPacker(int32 InWidth, int32 InHeight, int32 InPadding = 1);

	// Returns the slot for a Width x Height rectangle (excluding padding), or
	// nothing when the page is full.
	TOptional<FIntRect> Allocate(int32 Width, int32 Height);
	void Reset();

	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }
	int32 GetPadding() const { return Padding; }
	// Fraction of the page covered by allocations (including padding).
	float GetOccupancy() const { return static_cast<float>(UsedArea) / (static_cast<float>(Width) * Height); }

private:
	struct FSkylineNode
	{
		int32 X;
		int32 Y;
		int32 Width;
	};

	// Lowest Y at which a W-wide rectangle fits starting at node Index, or -1.
	int32 Fit(int32 Index, int32 W, int32 H) const;

	int32 Width;
	int32 Height;
	int32 Padding;
	int64 UsedArea = 0;
	TArray<FSkylineNode> Skyline;
};
