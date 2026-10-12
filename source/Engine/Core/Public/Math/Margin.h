// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Math/Vector2.h"

// Space around or inside a rectangle (padding, 9-slice borders).
struct FMargin
{
	float Left = 0.0f;
	float Top = 0.0f;
	float Right = 0.0f;
	float Bottom = 0.0f;

	constexpr FMargin() = default;
	constexpr FMargin(float Uniform) : Left(Uniform), Top(Uniform), Right(Uniform), Bottom(Uniform) {}
	constexpr FMargin(float Horizontal, float Vertical) : Left(Horizontal), Top(Vertical), Right(Horizontal), Bottom(Vertical) {}
	constexpr FMargin(float InLeft, float InTop, float InRight, float InBottom) : Left(InLeft), Top(InTop), Right(InRight), Bottom(InBottom) {}

	constexpr FVector2 GetDesiredSize() const { return { Left + Right, Top + Bottom }; }
	constexpr FVector2 GetTopLeft() const { return { Left, Top }; }
	constexpr FMargin operator*(float S) const { return { Left * S, Top * S, Right * S, Bottom * S }; }
	constexpr FMargin operator+(const FMargin& O) const { return { Left + O.Left, Top + O.Top, Right + O.Right, Bottom + O.Bottom }; }
	constexpr bool operator==(const FMargin&) const = default;
	constexpr bool IsZero() const { return Left == 0.0f && Top == 0.0f && Right == 0.0f && Bottom == 0.0f; }
};
