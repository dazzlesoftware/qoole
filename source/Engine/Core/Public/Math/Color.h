// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FColor: 8-bit sRGB-encoded color, memory order R, G, B, A (matches
// R8G8B8A8_UNORM vertex/texture formats).
// FLinearColor: float color in linear space.
//
// The UI renderer works with sRGB-encoded colors and blends in that space,
// like most desktop UI toolkits; FLinearColor is converted when needed.

#pragma once

#include "Math/MathUtility.h"

struct FLinearColor;

struct FColor
{
	uint8 R = 0;
	uint8 G = 0;
	uint8 B = 0;
	uint8 A = 255;

	constexpr FColor() = default;
	constexpr FColor(uint8 InR, uint8 InG, uint8 InB, uint8 InA = 255) : R(InR), G(InG), B(InB), A(InA) {}

	// From 0xRRGGBB or 0xRRGGBBAA style hex literals.
	static constexpr FColor FromHex(uint32 RGB) { return FColor(uint8(RGB >> 16), uint8(RGB >> 8), uint8(RGB), 255); }
	static constexpr FColor FromHexRGBA(uint32 RGBA) { return FColor(uint8(RGBA >> 24), uint8(RGBA >> 16), uint8(RGBA >> 8), uint8(RGBA)); }

	constexpr uint32 ToPackedRGBA() const { return uint32(R) | (uint32(G) << 8) | (uint32(B) << 16) | (uint32(A) << 24); }
	constexpr FColor WithAlpha(uint8 InA) const { return FColor(R, G, B, InA); }

	// Interprets the bytes as sRGB and converts to linear.
	FLinearColor ToLinear() const;

	constexpr bool operator==(const FColor&) const = default;

	static constexpr FColor White() { return { 255, 255, 255 }; }
	static constexpr FColor Black() { return { 0, 0, 0 }; }
	static constexpr FColor Transparent() { return { 0, 0, 0, 0 }; }
	static constexpr FColor Red() { return { 255, 0, 0 }; }
	static constexpr FColor Green() { return { 0, 255, 0 }; }
	static constexpr FColor Blue() { return { 0, 0, 255 }; }
};

struct FLinearColor
{
	float R = 0.0f;
	float G = 0.0f;
	float B = 0.0f;
	float A = 1.0f;

	constexpr FLinearColor() = default;
	constexpr FLinearColor(float InR, float InG, float InB, float InA = 1.0f) : R(InR), G(InG), B(InB), A(InA) {}

	static float SRGBToLinear(float C);
	static float LinearToSRGB(float C);

	// sRGB-encoded 8-bit color (alpha stays linear).
	FColor ToFColorSRGB() const;
	// Plain quantization, no transfer function.
	FColor QuantizeRaw() const;

	constexpr FLinearColor operator*(float S) const { return { R * S, G * S, B * S, A * S }; }
	constexpr FLinearColor operator*(const FLinearColor& O) const { return { R * O.R, G * O.G, B * O.B, A * O.A }; }
	constexpr FLinearColor operator+(const FLinearColor& O) const { return { R + O.R, G + O.G, B + O.B, A + O.A }; }
	constexpr FLinearColor operator-(const FLinearColor& O) const { return { R - O.R, G - O.G, B - O.B, A - O.A }; }
	constexpr bool operator==(const FLinearColor&) const = default;

	static constexpr FLinearColor White() { return { 1, 1, 1, 1 }; }
	static constexpr FLinearColor Black() { return { 0, 0, 0, 1 }; }
	static constexpr FLinearColor Transparent() { return { 0, 0, 0, 0 }; }
};
