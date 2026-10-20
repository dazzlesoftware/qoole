// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Genesis UI shaders, compiled to DXIL at build time (SlateRHIRenderer.vcxproj)
// and to SPIR-V by BuildSlateShaders.cmd (checked-in headers).
//
// The vertex format is FSlateVertex (ElementBatcher.h). Colors are sRGB-encoded
// with straight alpha and blended as-is (the UI blends in sRGB space).
//
// The ENTRY lines tell the project generator which entry points to compile.
// ENTRY: vs VSMain
// ENTRY: ps PSDefault
// ENTRY: ps PSRoundedBox
// ENTRY: ps PSLine

// Vulkan-only binding attributes; DXIL warns about them otherwise.
#if defined(__spirv__)
	#define VK_PUSH_CONSTANT [[vk::push_constant]]
	#define VK_BINDING(Index) [[vk::binding(Index)]]
	#define VK_LOCATION(Index) [[vk::location(Index)]]
#else
	#define VK_PUSH_CONSTANT
	#define VK_BINDING(Index)
	#define VK_LOCATION(Index)
#endif

struct FConstants
{
	// Pixels to clip space: clip = Position * Transform.xy + Transform.zw
	float4 Transform;
};

VK_PUSH_CONSTANT ConstantBuffer<FConstants> Constants : register(b0);
VK_BINDING(0) Texture2D Texture : register(t0);
VK_BINDING(1) SamplerState Sampler : register(s0);

struct FVertexInput
{
	VK_LOCATION(0) float2 Position : POSITION;
	VK_LOCATION(1) float2 UV : TEXCOORD0;
	VK_LOCATION(2) float4 Color : COLOR0;
	VK_LOCATION(3) float4 SecondaryColor : COLOR1;
	VK_LOCATION(4) float4 Params0 : TEXCOORD1;
	VK_LOCATION(5) float4 Params1 : TEXCOORD2;
};

struct FPixelInput
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
	float4 Color : COLOR0;
	float4 SecondaryColor : COLOR1;
	float4 Params0 : TEXCOORD1;
	float4 Params1 : TEXCOORD2;
};

FPixelInput VSMain(FVertexInput Input)
{
	FPixelInput Output;
	Output.Position = float4(Input.Position * Constants.Transform.xy + Constants.Transform.zw, 0.0, 1.0);
	Output.UV = Input.UV;
	Output.Color = Input.Color;
	Output.SecondaryColor = Input.SecondaryColor;
	Output.Params0 = Input.Params0;
	Output.Params1 = Input.Params1;
	return Output;
}

// Images, glyphs (R8 textures read as (1, 1, 1, coverage)), solid colors.
float4 PSDefault(FPixelInput Input) : SV_Target
{
	return Texture.Sample(Sampler, Input.UV) * Input.Color;
}

// Rounded rectangle from a signed distance field.
//   UV       pixel offset from the rectangle's center
//   Params0  (half width, half height, outline width) in pixels
//   Params1  corner radii in pixels: top-left, top-right, bottom-right, bottom-left
//   Color    fill; SecondaryColor outline (drawn inside the edge)
// Output is premultiplied.
float RoundedBoxDistance(float2 P, float2 HalfSize, float4 Radii)
{
	float Radius = P.x < 0.0 ? (P.y < 0.0 ? Radii.x : Radii.w) : (P.y < 0.0 ? Radii.y : Radii.z);
	float2 Q = abs(P) - HalfSize + Radius;
	return length(max(Q, 0.0)) + min(max(Q.x, Q.y), 0.0) - Radius;
}

float4 PSRoundedBox(FPixelInput Input) : SV_Target
{
	float Distance = RoundedBoxDistance(Input.UV, Input.Params0.xy, Input.Params1);
	float Outline = Input.Params0.z;
	// Distances are in pixels, so 0.5 is half a pixel of anti-aliasing.
	float Coverage = saturate(0.5 - Distance);
	float FillCoverage = Outline > 0.0 ? saturate(0.5 - (Distance + Outline)) : 1.0;
	float4 Fill = float4(Input.Color.rgb * Input.Color.a, Input.Color.a);
	float4 Border = float4(Input.SecondaryColor.rgb * Input.SecondaryColor.a, Input.SecondaryColor.a);
	return lerp(Border, Fill, FillCoverage) * Coverage;
}

// Anti-aliased line strip.
//   UV.y     signed distance from the center line, pixels
//   Params0  (half width in pixels, opacity for sub-pixel lines)
float4 PSLine(FPixelInput Input) : SV_Target
{
	float Coverage = saturate(Input.Params0.x + 0.5 - abs(Input.UV.y)) * Input.Params0.y;
	return float4(Input.Color.rgb, Input.Color.a * Coverage);
}
