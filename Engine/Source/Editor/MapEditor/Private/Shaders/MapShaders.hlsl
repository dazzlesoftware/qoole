// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Solid map faces for FMapViewportClient (RHI conventions: see RHI.h).
// DXIL is compiled at build time by MapEditor.vcxproj; the SPIR-V headers
// are made by BuildMapShaders.cmd (see there).
//
// Vertices arrive in window pixels (y down) plus q = 1 / view depth, as
// FDraw::ZBufTriangle produces them. Writing clip w = 1/q gives
// perspective-correct interpolation, and depth = 1 - q orders them (nearer
// is smaller), with a "less or equal" depth test.

// SPIR-V: dxc defines __spirv__, glslc (BuildMapShaders.cmd) MAP_SPIRV.
#if defined(__spirv__) || defined(MAP_SPIRV)
	#define VK_PUSH_CONSTANT [[vk::push_constant]]
	#define VK_BINDING(Index, Set) [[vk::binding(Index, Set)]]
	#define VK_LOCATION(Index) [[vk::location(Index)]]
	#define CB_REGISTER
#else
	#define VK_PUSH_CONSTANT
	#define VK_BINDING(Index, Set)
	#define VK_LOCATION(Index)
	#define CB_REGISTER : register(b0)
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

// All 64 bytes, so every pipeline these shaders are in has the same layout.
struct ViewConsts
{
	float4 scale;     // xy = 2 / viewport size in pixels
	float4 land;      // landscapes: xy = size in units, z = layers (0 to 8)
	float4 repeat[2]; // landscapes: 1 / tiling of layers 0-3, then 4-7
};
VK_PUSH_CONSTANT ConstantBuffer<ViewConsts> consts CB_REGISTER;

// Texture slot k is tk (D3D12) / set k (Vulkan); the sampler is s0, in
// set 0. Faces sample T0. Landscapes use slots 1 and 2 (slot 0 is unused
// there): the layers' weights and their textures, as arrays, so that a
// landscape takes three descriptor sets, within Vulkan's guaranteed four.
VK_BINDING(0, 0) Texture2D T0 : register(t0);
VK_BINDING(0, 1) Texture2DArray Weights : register(t1);
VK_BINDING(0, 2) Texture2DArray Layers : register(t2);
VK_BINDING(1, 0) SamplerState Sampler : register(s0);

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

// Copies of a mesh (foliage), drawn instanced: each copy's object-to-view
// matrix arrives as three rows; the projection is the one View uses for
// the faces above (x and z scaled by winWidth / depth about the window's
// center), so depths agree with them. Lit as View lights meshes (MapDraw's
// shade), from the light and the sky in view space. Constants for these:
// scale as above; land = (center x, center y, winWidth, 1 lit / 0 unlit);
// repeat[0].xyz the light, repeat[1].xyz the sky.
struct VSFoliageIn
{
	VK_LOCATION(0) float3 pos : POSITION;
	VK_LOCATION(1) float3 normal : NORMAL;
	VK_LOCATION(2) float4 color : COLOR0;
	VK_LOCATION(3) float4 row0 : TEXCOORD1;
	VK_LOCATION(4) float4 row1 : TEXCOORD2;
	VK_LOCATION(5) float4 row2 : TEXCOORD3;
};

VSOut VSFoliage(VSFoliageIn i)
{
	VSOut o;
	const float4 p = float4(i.pos, 1.0);
	// View space: x right, y away from the camera, z up.
	const float3 v = float3(dot(i.row0, p), dot(i.row1, p), dot(i.row2, p));
	const float2 center = consts.land.xy;
	const float width = consts.land.z;
	// Pixel x = center.x + v.x * width / v.y (y down: center.y - v.z * ...),
	// q = 1 / v.y, written so that w = v.y as VSMain's w = 1 / q.
	o.pos = float4((center.x * v.y + v.x * width) * consts.scale.x - v.y,
		v.y - (center.y * v.y - v.z * width) * consts.scale.y,
		v.y - 1.0,
		v.y);
	const float3 n = normalize(float3(dot(i.row0.xyz, i.normal), dot(i.row1.xyz, i.normal), dot(i.row2.xyz, i.normal)));
	const float light = min(1.0, 0.32 + 0.58 * max(dot(n, consts.repeat[0].xyz), 0.0) + 0.12 * (dot(n, consts.repeat[1].xyz) * 0.5 + 0.5));
	const float shade = consts.land.w > 0.5 ? saturate(light * 1.1) : 0.86;
	o.uv = float2(0.0, 0.0);
	o.color = float4(i.color.rgb * shade, 1.0);
	return o;
}

// Faces are their color times the texture: a landscape's baked layers, or
// white for everything else.
float4 PSTextured(VSOut i) : SV_Target
{
	return T0.Sample(Sampler, i.uv) * i.color;
}

// Landscapes with textured layers, blended per pixel. uv is where the
// pixel is on the landscape's grid (0 to 1 across); Weights holds the
// layers' weights at the grid points (slice 0 RGBA: layers 0-3, slice 1:
// 4-7); Layers holds their textures (already tinted), a slice each,
// repeating every tiling units. What the layers leave shows the ground's
// own color (Landscape::GetBaseColor). The vertex color is the light.
float4 PSLandscape(VSOut i) : SV_Target
{
	// The weights between grid points: their texels' centers.
	float3 dim;
	Weights.GetDimensions(dim.x, dim.y, dim.z);
	const float2 wuv = clamp((i.uv * (dim.xy - 1.0) + 0.5) / dim.xy, 0.5 / dim.xy, 1.0 - 0.5 / dim.xy);
	const float4 w0 = Weights.Sample(Sampler, float3(wuv, 0.0));
	const float4 w1 = Weights.Sample(Sampler, float3(wuv, 1.0));
	const float weight[8] = { w0.r, w0.g, w0.b, w0.a, w1.r, w1.g, w1.b, w1.a };
	const float layers = consts.land.z;
	const float2 ground = i.uv * consts.land.xy;

	float3 color = 0.0;
	float total = 0.0;
	[unroll]
	for (int k = 0; k < 8; ++k)
	{
		if (k < layers)
		{
			const float repeat = consts.repeat[k / 4][k % 4];
			color += Layers.Sample(Sampler, float3(ground * repeat, k)).rgb * weight[k];
			total += weight[k];
		}
	}

	const float3 base = float3(158.0, 168.0, 148.0) / 255.0;
	const float rest = saturate(1.0 - total);
	const float scale = total > 1.0 ? 1.0 / total : 1.0;
	return float4((color * scale + base * rest) * i.color.rgb, 1.0);
}
