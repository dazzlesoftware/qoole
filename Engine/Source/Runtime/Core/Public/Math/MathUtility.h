// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "CoreTypes.h"

#include <cmath>

struct FMath
{
	static constexpr float Pi = 3.14159265358979323846f;
	static constexpr float SmallNumber = 1.e-8f;
	static constexpr float KindaSmallNumber = 1.e-4f;

	template <typename T> static constexpr T Min(T A, T B) { return A < B ? A : B; }
	template <typename T> static constexpr T Max(T A, T B) { return A > B ? A : B; }
	template <typename T> static constexpr T Clamp(T V, T Lo, T Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	template <typename T> static constexpr T Abs(T V) { return V < T(0) ? -V : V; }
	template <typename T> static constexpr T Square(T V) { return V * V; }
	template <typename T, typename U> static constexpr T Lerp(const T& A, const T& B, U Alpha) { return A + (B - A) * Alpha; }

	static float Sqrt(float V) { return std::sqrt(V); }
	static float Sin(float V) { return std::sin(V); }
	static float Cos(float V) { return std::cos(V); }
	static float Atan2(float Y, float X) { return std::atan2(Y, X); }
	static float Fmod(float X, float Y) { return std::fmod(X, Y); }
	static float FloorToFloat(float V) { return std::floor(V); }
	static float CeilToFloat(float V) { return std::ceil(V); }
	static float RoundToFloat(float V) { return std::floor(V + 0.5f); }
	static int32 FloorToInt(float V) { return static_cast<int32>(std::floor(V)); }
	static int32 CeilToInt(float V) { return static_cast<int32>(std::ceil(V)); }
	static int32 RoundToInt(float V) { return static_cast<int32>(std::floor(V + 0.5f)); }

	static bool IsNearlyEqual(float A, float B, float Tolerance = SmallNumber) { return Abs(A - B) <= Tolerance; }
	static bool IsNearlyZero(float V, float Tolerance = SmallNumber) { return Abs(V) <= Tolerance; }

	template <typename T> static constexpr T DivideAndRoundUp(T A, T B) { return (A + B - 1) / B; }
	template <typename T> static constexpr T AlignUp(T V, T Alignment) { return (V + Alignment - 1) / Alignment * Alignment; }
	static constexpr bool IsPowerOfTwo(uint32 V) { return V != 0 && (V & (V - 1)) == 0; }
	static constexpr uint32 RoundUpToPowerOfTwo(uint32 V)
	{
		if (V <= 1) return 1;
		--V;
		V |= V >> 1; V |= V >> 2; V |= V >> 4; V |= V >> 8; V |= V >> 16;
		return V + 1;
	}
};
