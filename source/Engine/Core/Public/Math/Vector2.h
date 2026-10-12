// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Math/MathUtility.h"

// 2D float vector (logical or pixel units, depending on context).
struct FVector2
{
	float X = 0.0f;
	float Y = 0.0f;

	constexpr FVector2() = default;
	constexpr FVector2(float InX, float InY) : X(InX), Y(InY) {}
	constexpr explicit FVector2(float Both) : X(Both), Y(Both) {}

	static constexpr FVector2 Zero() { return { 0.0f, 0.0f }; }
	static constexpr FVector2 One() { return { 1.0f, 1.0f }; }

	constexpr FVector2 operator+(FVector2 B) const { return { X + B.X, Y + B.Y }; }
	constexpr FVector2 operator-(FVector2 B) const { return { X - B.X, Y - B.Y }; }
	constexpr FVector2 operator*(FVector2 B) const { return { X * B.X, Y * B.Y }; }
	constexpr FVector2 operator/(FVector2 B) const { return { X / B.X, Y / B.Y }; }
	constexpr FVector2 operator*(float S) const { return { X * S, Y * S }; }
	constexpr FVector2 operator/(float S) const { return { X / S, Y / S }; }
	constexpr FVector2 operator-() const { return { -X, -Y }; }
	constexpr FVector2& operator+=(FVector2 B) { X += B.X; Y += B.Y; return *this; }
	constexpr FVector2& operator-=(FVector2 B) { X -= B.X; Y -= B.Y; return *this; }
	constexpr FVector2& operator*=(float S) { X *= S; Y *= S; return *this; }
	constexpr bool operator==(const FVector2&) const = default;
	friend constexpr FVector2 operator*(float S, FVector2 V) { return V * S; }

	static constexpr float Dot(FVector2 A, FVector2 B) { return A.X * B.X + A.Y * B.Y; }
	static constexpr float Cross(FVector2 A, FVector2 B) { return A.X * B.Y - A.Y * B.X; }
	constexpr float SizeSquared() const { return X * X + Y * Y; }
	float Size() const { return FMath::Sqrt(SizeSquared()); }

	FVector2 GetSafeNormal(float Tolerance = FMath::SmallNumber) const
	{
		const float S = SizeSquared();
		return S > Tolerance ? *this / FMath::Sqrt(S) : Zero();
	}

	// Rotated 90 degrees counter-clockwise (in a y-down space: clockwise on screen).
	constexpr FVector2 GetPerpendicular() const { return { -Y, X }; }
	FVector2 Round() const { return { FMath::RoundToFloat(X), FMath::RoundToFloat(Y) }; }
	static constexpr FVector2 Min(FVector2 A, FVector2 B) { return { FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y) }; }
	static constexpr FVector2 Max(FVector2 A, FVector2 B) { return { FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y) }; }
};

// 2D integer point/size.
struct FIntPoint
{
	int32 X = 0;
	int32 Y = 0;

	constexpr FIntPoint() = default;
	constexpr FIntPoint(int32 InX, int32 InY) : X(InX), Y(InY) {}

	constexpr FIntPoint operator+(FIntPoint B) const { return { X + B.X, Y + B.Y }; }
	constexpr FIntPoint operator-(FIntPoint B) const { return { X - B.X, Y - B.Y }; }
	constexpr bool operator==(const FIntPoint&) const = default;
	constexpr FVector2 ToVector2() const { return { static_cast<float>(X), static_cast<float>(Y) }; }
};
