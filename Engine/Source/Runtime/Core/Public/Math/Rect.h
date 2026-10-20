// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Math/Margin.h"

// Axis-aligned float rectangle, [Left, Right) x [Top, Bottom), y down.
struct FRect
{
	float Left = 0.0f;
	float Top = 0.0f;
	float Right = 0.0f;
	float Bottom = 0.0f;

	constexpr FRect() = default;
	constexpr FRect(float InLeft, float InTop, float InRight, float InBottom) : Left(InLeft), Top(InTop), Right(InRight), Bottom(InBottom) {}
	constexpr FRect(FVector2 TopLeft, FVector2 BottomRight) : Left(TopLeft.X), Top(TopLeft.Y), Right(BottomRight.X), Bottom(BottomRight.Y) {}

	static constexpr FRect FromPosSize(FVector2 Pos, FVector2 Size) { return { Pos.X, Pos.Y, Pos.X + Size.X, Pos.Y + Size.Y }; }

	constexpr float Width() const { return Right - Left; }
	constexpr float Height() const { return Bottom - Top; }
	constexpr FVector2 GetSize() const { return { Right - Left, Bottom - Top }; }
	constexpr FVector2 GetTopLeft() const { return { Left, Top }; }
	constexpr FVector2 GetBottomRight() const { return { Right, Bottom }; }
	constexpr FVector2 GetCenter() const { return { (Left + Right) * 0.5f, (Top + Bottom) * 0.5f }; }

	// True when the rectangle has no area.
	constexpr bool IsEmpty() const { return Right <= Left || Bottom <= Top; }
	constexpr bool Contains(FVector2 P) const { return P.X >= Left && P.X < Right && P.Y >= Top && P.Y < Bottom; }
	constexpr bool Intersects(const FRect& O) const { return !(O.Right <= Left || O.Left >= Right || O.Bottom <= Top || O.Top >= Bottom); }

	// Overlap of the two rectangles; empty (zero-sized, at the clamped
	// position) when they do not overlap.
	constexpr FRect IntersectionWith(const FRect& O) const
	{
		FRect R(FMath::Max(Left, O.Left), FMath::Max(Top, O.Top), FMath::Min(Right, O.Right), FMath::Min(Bottom, O.Bottom));
		if (R.Right < R.Left) R.Right = R.Left;
		if (R.Bottom < R.Top) R.Bottom = R.Top;
		return R;
	}

	constexpr FRect Inset(const FMargin& M) const { return { Left + M.Left, Top + M.Top, Right - M.Right, Bottom - M.Bottom }; }
	constexpr FRect Expand(const FMargin& M) const { return { Left - M.Left, Top - M.Top, Right + M.Right, Bottom + M.Bottom }; }
	constexpr FRect Offset(FVector2 D) const { return { Left + D.X, Top + D.Y, Right + D.X, Bottom + D.Y }; }
	constexpr FRect Scale(float S) const { return { Left * S, Top * S, Right * S, Bottom * S }; }
	constexpr bool operator==(const FRect&) const = default;
};

// Integer rectangle (pixel scissors, atlas slots).
struct FIntRect
{
	int32 Left = 0;
	int32 Top = 0;
	int32 Right = 0;
	int32 Bottom = 0;

	constexpr FIntRect() = default;
	constexpr FIntRect(int32 InLeft, int32 InTop, int32 InRight, int32 InBottom) : Left(InLeft), Top(InTop), Right(InRight), Bottom(InBottom) {}

	constexpr int32 Width() const { return Right - Left; }
	constexpr int32 Height() const { return Bottom - Top; }
	constexpr bool IsEmpty() const { return Right <= Left || Bottom <= Top; }
	constexpr bool Intersects(const FIntRect& O) const { return !(O.Right <= Left || O.Left >= Right || O.Bottom <= Top || O.Top >= Bottom); }
	constexpr bool operator==(const FIntRect&) const = default;
};
