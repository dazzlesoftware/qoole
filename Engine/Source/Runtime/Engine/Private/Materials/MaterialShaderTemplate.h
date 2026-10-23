// Genesis shader template; keep lighting in sync with Renderer MeshMaterial.hlsl.
#pragma once
static constexpr const char* MaterialGraphShaderTemplate = R"GENESIS(
// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
// Native mesh materials, under the editor's directional/hemisphere light.
#if defined(__spirv__) || defined(MAP_SPIRV)
#define PUSH [[vk::push_constant]]
#define BINDING(Index, Set) [[vk::binding(Index, Set)]]
#define LOCATION(Index) [[vk::location(Index)]]
#define CB_REGISTER
#else
#define PUSH
#define BINDING(Index, Set)
#define LOCATION(Index)
#define CB_REGISTER : register(b0)
#endif

struct Constants
{
	float4 projection; // 2/width, 2/height, view winWidth, flags: lit=1, normal=4, masked=8, translucent=16, cutoff UNORM16 in bits 5-20
	float4 baseColor; // authoring sRGB color, linear opacity alpha
	float4 light; // view-space direction, roughness
	float4 sky; // view-space up, metallic
};
PUSH ConstantBuffer<Constants> params CB_REGISTER;
/*GRAPH_TEXTURES*/
BINDING(1, 0) SamplerState RepeatSampler : register(s0);

struct Input
{
	LOCATION(0) float3 position : POSITION;
	LOCATION(1) float3 normal : NORMAL;
	LOCATION(2) float2 uv : TEXCOORD0;
	LOCATION(3) float4 color : COLOR0;
};
struct Pixel
{
	float4 position : SV_Position;
	LOCATION(0) float3 viewPosition : TEXCOORD0;
	LOCATION(1) float3 normal : TEXCOORD1;
	LOCATION(2) float2 uv : TEXCOORD2;
	LOCATION(3) float4 color : COLOR0;
};
Pixel VSMaterial(Input i)
{
	Pixel o;
	// Leave w signed. The GPU clips crossing triangles and interpolates all
	// attributes with perspective; depth agrees with the existing map pipeline.
	o.position = float4(i.position.x * params.projection.z * params.projection.x,
		i.position.z * params.projection.z * params.projection.y, i.position.y - 1.0, i.position.y);
	o.viewPosition = i.position;
	o.normal = i.normal;
	o.uv = i.uv;
	o.color = i.color;
	return o;
}
float3 Linear(float3 c)
{
	return float3(c.r <= 0.04045 ? c.r / 12.92 : pow((c.r + 0.055) / 1.055, 2.4),
		c.g <= 0.04045 ? c.g / 12.92 : pow((c.g + 0.055) / 1.055, 2.4),
		c.b <= 0.04045 ? c.b / 12.92 : pow((c.b + 0.055) / 1.055, 2.4));
}
float3 Display(float3 c)
{
	c = max(c, 0.0);
	return float3(c.r <= 0.0031308 ? c.r * 12.92 : 1.055 * pow(c.r, 1.0 / 2.4) - 0.055,
		c.g <= 0.0031308 ? c.g * 12.92 : 1.055 * pow(c.g, 1.0 / 2.4) - 0.055,
		c.b <= 0.0031308 ? c.b * 12.92 : 1.055 * pow(c.b, 1.0 / 2.4) - 0.055);
}
float3 Unit(float3 v) { return dot(v,v) > 1e-16 ? normalize(v) : float3(0,0,1); }
float4 PSMaterial(Pixel i) : SV_Target
{
	const uint flags = (uint)params.projection.w;
	/*GRAPH_EVALUATE*/
	float alpha = saturate(surfaceOpacity);
	if ((flags & 8) != 0) clip(alpha - float(flags >> 5) / 65535.0);
	alpha = (flags & 16) != 0 ? alpha : 1.0;
	const float3 base = max(surfaceColor, 0.0);
	if ((flags & 1) == 0) return float4(saturate(Display(base + surfaceEmissive)), alpha);

	float3 N = Unit(i.normal);
	if (/*GRAPH_NORMAL*/)
	{
		// A derivative cotangent frame respects UV seams, mirrored UVs and
		// transformed geometry. Degenerate UVs keep the geometric normal.
		const float3 px = ddx(i.viewPosition), py = ddy(i.viewPosition);
		const float2 ux = ddx(i.uv), uy = ddy(i.uv);
		const float determinant = ux.x * uy.y - ux.y * uy.x;
		if (abs(determinant) > 1e-12)
		{
			float3 T = (px * uy.y - py * ux.y) / determinant;
			const float3 B = (py * ux.x - px * uy.x) / determinant;
			T -= N * dot(N,T);
			if (dot(T,T) > 1e-16)
			{
				T = normalize(T);
				const float3 bitangent = Unit(cross(N,T)) * (dot(cross(N,T),B) < 0 ? -1.0 : 1.0);
				const float3 mapped = Unit(surfaceNormal);
				N = Unit(T * mapped.x + bitangent * mapped.y + N * mapped.z);
			}
		}
	}
	const float PI = 3.14159265359;
	const float roughness = clamp(surfaceRoughness,0.045,1.0), metallic = saturate(surfaceMetallic);
	const float3 L = Unit(params.light.xyz), V = Unit(-i.viewPosition), H = Unit(L+V);
	const float nl = saturate(dot(N,L)), nv = max(saturate(dot(N,V)),0.001);
	const float nh = saturate(dot(N,H)), vh = saturate(dot(V,H));
	const float3 F0 = lerp(float3(0.04,0.04,0.04),base,metallic);
	const float3 F = F0 + (1.0-F0) * pow(1.0-vh,5.0);
	const float a = roughness*roughness, a2 = a*a;
	const float denominator = nh*nh*(a2-1.0)+1.0;
	const float D = a2 / max(PI*denominator*denominator,1e-7);
	const float k = (roughness+1.0)*(roughness+1.0)/8.0;
	const float G = (nl/(nl*(1.0-k)+k)) * (nv/(nv*(1.0-k)+k));
	const float3 specular = D*G*F / max(4.0*nl*nv,1e-5);
	const float3 diffuse = (1.0-F)*(1.0-metallic)*base/PI;
	const float hemisphere = saturate(dot(N,params.sky.xyz)*0.5+0.5);
	float3 color = (diffuse+specular)*nl*3.0 + base*(1.0-metallic)*(0.12+0.16*hemisphere) + F0*(0.06+0.10*hemisphere);
	// ACES fit for the preview light, then encode into the UNORM/sRGB UI target.
	color += max(surfaceEmissive,0.0);
	color = saturate((color*(2.51*color+0.03))/(color*(2.43*color+0.59)+0.14));
	return float4(saturate(Display(color)),alpha);
}


)GENESIS";
