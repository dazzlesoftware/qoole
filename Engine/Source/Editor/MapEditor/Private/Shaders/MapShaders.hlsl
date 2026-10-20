// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Solid map faces for FMapViewportClient (RHI conventions: see VulkanRHI.h).
// DXIL is compiled at build time by MapEditor.vcxproj; the SPIR-V headers
// are made by BuildMapShaders.cmd (see there).
//
// Vertices arrive in window pixels (y down) plus q = 1 / view depth, as
// FDraw::ZBufTriangle produces them. Writing clip w = 1/q gives
// perspective-correct interpolation, and depth = 1 - q orders them (nearer
// is smaller), with a "less or equal" depth test.

#ifdef __spirv__
	#define VK_PUSH_CONSTANT [[vk::push_constant]]
	#define VK_LOCATION(Index) [[vk::location(Index)]]
#else
	#define VK_PUSH_CONSTANT
	#define VK_LOCATION(Index)
#endif

struct VSIn
{
	VK_LOCATION(0) float3 pos : POSITION;
	VK_LOCATION(1) float2 uv : TEXCOORD0;
	VK_LOCATION(2) float4 color : COLOR0;
};

struct VSOut
{
	float4 pos : SV_Position;
	VK_LOCATION(0) float2 uv : TEXCOORD0;
	VK_LOCATION(1) float4 color : COLOR0;
};

struct ViewConsts
{
	float4 scale; // xy = 2 / viewport size in pixels
};
VK_PUSH_CONSTANT ConstantBuffer<ViewConsts> consts : register(b0);

VSOut VSMain(VSIn i)
{
	VSOut o;
	float x = i.pos.x * consts.scale.x - 1.0;
	float y = 1.0 - i.pos.y * consts.scale.y;
	float q = max(i.pos.z, 1e-6);
	float w = 1.0 / q;
	o.pos = float4(x * w, y * w, (1.0 - q) * w, w);
	o.uv = i.uv;
	o.color = i.color;
	return o;
}

float4 PSColor(VSOut i) : SV_Target
{
	return i.color;
}
