// Shaders for FDrawDX12 (compiled with fxc, SM 5.1) and FDrawVulkan
// (compiled with dxc -spirv). See the CustomBuild steps in their projects.
//
// Vertices arrive in window pixels (y down) plus q = 1/view-depth, as
// produced by FDraw::ZBufTriangle. Writing clip w = 1/q gives the
// perspective-correct texture interpolation that FDrawOpenGL gets from
// glTexCoord4f, and depth = 1 - q matches its glOrtho depth ordering.

struct VSIn {
#ifdef __spirv__
	[[vk::location(0)]] float3 pos : POSITION;
	[[vk::location(1)]] float2 uv : TEXCOORD0;
	[[vk::location(2)]] float4 color : COLOR0;
#else
	float3 pos : POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
#endif
};

struct VSOut {
	float4 pos : SV_Position;
#ifdef __spirv__
	[[vk::location(0)]] float2 uv : TEXCOORD0;
	[[vk::location(1)]] float4 color : COLOR0;
#else
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
#endif
};

#ifdef __spirv__
struct ViewConsts { float4 scale; };
[[vk::push_constant]] ViewConsts consts;
#define VIEW_SCALE consts.scale

[[vk::combinedImageSampler]][[vk::binding(0, 0)]] Texture2D tex;
[[vk::combinedImageSampler]][[vk::binding(0, 0)]] SamplerState samp;
#else
cbuffer ViewConsts : register(b0) { float4 viewScale; };
#define VIEW_SCALE viewScale

Texture2D tex : register(t0);
SamplerState samp : register(s0);
#endif

VSOut VSMain(VSIn i) {
	VSOut o;

	// VIEW_SCALE.xy = 2 / viewport size.
	float x = i.pos.x * VIEW_SCALE.x - 1.0;
	float y = 1.0 - i.pos.y * VIEW_SCALE.y;
#ifdef __spirv__
	y = -y;		// Vulkan NDC has y pointing down.
#endif

	float q = max(i.pos.z, 1e-6);
	float w = 1.0 / q;
	o.pos = float4(x * w, y * w, (1.0 - q) * w, w);
	o.uv = i.uv;
	o.color = i.color;
	return o;
}

float4 PSColor(VSOut i) : SV_Target {
	return i.color;
}

float4 PSTexture(VSOut i) : SV_Target {
	return float4(tex.Sample(samp, i.uv).rgb, 1.0);
}
