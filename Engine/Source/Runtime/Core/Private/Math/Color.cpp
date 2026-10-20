// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Math/Color.h"

#include <cmath>

float FLinearColor::SRGBToLinear(float C)
{
	return C <= 0.04045f ? C / 12.92f : std::pow((C + 0.055f) / 1.055f, 2.4f);
}

float FLinearColor::LinearToSRGB(float C)
{
	return C <= 0.0031308f ? C * 12.92f : 1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f;
}

namespace
{
	uint8 Quantize(float V)
	{
		return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(V, 0.0f, 1.0f) * 255.0f));
	}
}

FLinearColor FColor::ToLinear() const
{
	return FLinearColor(
		FLinearColor::SRGBToLinear(R / 255.0f),
		FLinearColor::SRGBToLinear(G / 255.0f),
		FLinearColor::SRGBToLinear(B / 255.0f),
		A / 255.0f);
}

FColor FLinearColor::ToFColorSRGB() const
{
	return FColor(Quantize(LinearToSRGB(R)), Quantize(LinearToSRGB(G)), Quantize(LinearToSRGB(B)), Quantize(A));
}

FColor FLinearColor::QuantizeRaw() const
{
	return FColor(Quantize(R), Quantize(G), Quantize(B), Quantize(A));
}
