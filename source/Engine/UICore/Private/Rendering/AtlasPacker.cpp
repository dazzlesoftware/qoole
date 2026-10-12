// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Rendering/AtlasPacker.h"

FAtlasPacker::FAtlasPacker(int32 InWidth, int32 InHeight, int32 InPadding)
	: Width(InWidth)
	, Height(InHeight)
	, Padding(InPadding)
{
	check(Width > 0 && Height > 0 && Padding >= 0);
	Reset();
}

void FAtlasPacker::Reset()
{
	Skyline.clear();
	Skyline.push_back({ 0, 0, Width });
	UsedArea = 0;
}

int32 FAtlasPacker::Fit(int32 Index, int32 W, int32 H) const
{
	const int32 X = Skyline[Index].X;
	if (X + W > Width)
	{
		return -1;
	}
	int32 Remaining = W;
	int32 Y = Skyline[Index].Y;
	for (int32 I = Index; Remaining > 0; ++I)
	{
		if (I >= static_cast<int32>(Skyline.size()))
		{
			return -1;
		}
		Y = FMath::Max(Y, Skyline[I].Y);
		if (Y + H > Height)
		{
			return -1;
		}
		Remaining -= Skyline[I].Width;
	}
	return Y;
}

TOptional<FIntRect> FAtlasPacker::Allocate(int32 InWidth, int32 InHeight)
{
	check(InWidth >= 0 && InHeight >= 0);
	const int32 W = InWidth + Padding * 2;
	const int32 H = InHeight + Padding * 2;
	if (W > Width || H > Height)
	{
		return {};
	}

	// Bottom-left rule: lowest top edge, then the narrowest node.
	int32 BestIndex = -1;
	int32 BestY = INT32_MAX;
	int32 BestWidth = INT32_MAX;
	for (int32 I = 0; I < static_cast<int32>(Skyline.size()); ++I)
	{
		const int32 Y = Fit(I, W, H);
		if (Y >= 0 && (Y < BestY || (Y == BestY && Skyline[I].Width < BestWidth)))
		{
			BestIndex = I;
			BestY = Y;
			BestWidth = Skyline[I].Width;
		}
	}
	if (BestIndex < 0)
	{
		return {};
	}

	const int32 X = Skyline[BestIndex].X;
	// Insert the new node, then shrink or remove the nodes it covers.
	Skyline.insert(Skyline.begin() + BestIndex, FSkylineNode{ X, BestY + H, W });
	for (int32 I = BestIndex + 1; I < static_cast<int32>(Skyline.size());)
	{
		FSkylineNode& Node = Skyline[I];
		const FSkylineNode& Prev = Skyline[I - 1];
		const int32 PrevRight = Prev.X + Prev.Width;
		if (Node.X >= PrevRight)
		{
			break;
		}
		const int32 Shrink = PrevRight - Node.X;
		Node.X += Shrink;
		Node.Width -= Shrink;
		if (Node.Width > 0)
		{
			break;
		}
		Skyline.erase(Skyline.begin() + I);
	}
	// Merge neighbours at the same height.
	for (int32 I = 0; I + 1 < static_cast<int32>(Skyline.size());)
	{
		if (Skyline[I].Y == Skyline[I + 1].Y)
		{
			Skyline[I].Width += Skyline[I + 1].Width;
			Skyline.erase(Skyline.begin() + I + 1);
		}
		else
		{
			++I;
		}
	}

	UsedArea += int64(W) * H;
	return FIntRect(X + Padding, BestY + Padding, X + Padding + InWidth, BestY + Padding + InHeight);
}
