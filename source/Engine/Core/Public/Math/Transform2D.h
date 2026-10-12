// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Math/Vector2.h"

// 2D affine transform: P' = M * P + T with M = [[M00, M01], [M10, M11]]
// (row-major, column vectors). Used for layout (scale + translation) and
// render transforms (any affine).
struct FTransform2D
{
	float M00 = 1.0f, M01 = 0.0f;
	float M10 = 0.0f, M11 = 1.0f;
	FVector2 Translation;

	constexpr FTransform2D() = default;
	constexpr explicit FTransform2D(FVector2 InTranslation) : Translation(InTranslation) {}
	constexpr FTransform2D(float Scale, FVector2 InTranslation) : M00(Scale), M11(Scale), Translation(InTranslation) {}
	constexpr FTransform2D(float In00, float In01, float In10, float In11, FVector2 InTranslation)
		: M00(In00), M01(In01), M10(In10), M11(In11), Translation(InTranslation) {}

	static constexpr FTransform2D FromScale(float S) { return FTransform2D(S, {}); }
	static constexpr FTransform2D FromScale(FVector2 S) { return FTransform2D(S.X, 0.0f, 0.0f, S.Y, {}); }
	static constexpr FTransform2D FromTranslation(FVector2 T) { return FTransform2D(T); }
	static FTransform2D FromRotation(float Radians)
	{
		const float C = FMath::Cos(Radians), S = FMath::Sin(Radians);
		return FTransform2D(C, -S, S, C, {});
	}

	constexpr FVector2 TransformVector(FVector2 V) const { return { M00 * V.X + M01 * V.Y, M10 * V.X + M11 * V.Y }; }
	constexpr FVector2 TransformPoint(FVector2 P) const { return TransformVector(P) + Translation; }

	// Returns the transform that applies this one first, then Other.
	constexpr FTransform2D Concatenate(const FTransform2D& Other) const
	{
		return FTransform2D(
			Other.M00 * M00 + Other.M01 * M10, Other.M00 * M01 + Other.M01 * M11,
			Other.M10 * M00 + Other.M11 * M10, Other.M10 * M01 + Other.M11 * M11,
			Other.TransformPoint(Translation));
	}

	constexpr float Determinant() const { return M00 * M11 - M01 * M10; }

	FTransform2D Inverse() const
	{
		const float Det = Determinant();
		const float InvDet = FMath::IsNearlyZero(Det) ? 0.0f : 1.0f / Det;
		const FTransform2D Inv(M11 * InvDet, -M01 * InvDet, -M10 * InvDet, M00 * InvDet, {});
		return FTransform2D(Inv.M00, Inv.M01, Inv.M10, Inv.M11, -Inv.TransformVector(Translation));
	}

	// No rotation or shear (scissor clipping stays exact).
	constexpr bool IsAxisAligned() const { return M01 == 0.0f && M10 == 0.0f; }
	constexpr bool IsIdentity() const { return M00 == 1.0f && M01 == 0.0f && M10 == 0.0f && M11 == 1.0f && Translation == FVector2(); }
	constexpr bool operator==(const FTransform2D&) const = default;
};
