// The cull demo's draws (Game::CullDemo), vertex-pulled from the Boxes buffer (the draw's
// first instance is the box), four programs from one source:
//  - CullDemo: the Forward+ opaque pass, lit like the scene;
//  - CullDemoDepth (CULL_DEMO_DEPTH=1): the depth prepass, the same vertex stage;
//  - CullDemoShadow (CULL_DEMO_SHADOW=1): shadow tiles;
//  - CullDemoXray (CULL_DEMO_XRAY=1): the transparent pass at the nearest depth, so every
//    box the culler kept shows through whatever is in front (early list green, late red).

#include "Resources/DrawIndex.slang"

#ifndef CULL_DEMO_DEPTH
#define CULL_DEMO_DEPTH 0
#endif
#ifndef CULL_DEMO_SHADOW
#define CULL_DEMO_SHADOW 0
#endif
#ifndef CULL_DEMO_XRAY
#define CULL_DEMO_XRAY 0
#endif

#if CULL_DEMO_SHADOW
#include "Shadows/ShadowFrame.slang"
#else
#include "ForwardPlus/ForwardFrame.slang"
#endif

struct Box
{
	float4 Center;
	float4 Half;
	float4 Color; // rgb: base colour, a: roughness.
};

[[vk::binding(0, 2)]] StructuredBuffer<Box> Boxes;

void CubeVertex(uint vertexId, out float3 position, out float3 normal)
{
	const uint face = vertexId / 6u;
	const uint corner = vertexId % 6u;
	const uint axis = face / 2u;
	const float side = (face % 2u) == 0u ? 1.0 : -1.0;
	const float2 quad[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
	const float2 uv = quad[corner] * 2.0 - 1.0;
	position = axis == 0u ? float3(side, uv.x, uv.y) : axis == 1u ? float3(uv.y, side, uv.x) : float3(uv.x, uv.y, side);
	normal = axis == 0u ? float3(side, 0, 0) : axis == 1u ? float3(0, side, 0) : float3(0, 0, side);
}

float3 BoxWorld(Box box, float3 corner)
{
	return box.Center.xyz + corner * box.Half.xyz;
}

#if CULL_DEMO_SHADOW
struct ShadowConstants
{
	uint ViewIndex;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<ShadowConstants> Constants;

[shader("vertex")]
float4 vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint boxIndexInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS) : SV_Position
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint boxIndex = DrawInstanceIndex(boxIndexInput);
	float3 corner;
	float3 normal;
	CubeVertex(vertexId, corner, normal);
	return ShadowProjectWorld(Constants.ViewIndex, BoxWorld(Boxes[boxIndex], corner));
}

[shader("fragment")]
void fragmentMain()
{
}
#elif CULL_DEMO_XRAY
struct XrayConstants
{
	float4 Tint; // rgb: radiance, a: opacity.
};

[[vk::push_constant]] ConstantBuffer<XrayConstants> Constants;

[shader("vertex")]
float4 vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint boxIndexInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS) : SV_Position
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint boxIndex = DrawInstanceIndex(boxIndexInput);
	float3 corner;
	float3 normal;
	CubeVertex(vertexId, corner, normal);
	const float3 world = BoxWorld(Boxes[boxIndex], corner);
	float4 position = ForwardProjectWorld(world, world).Position;
	position.z = position.w; // Reverse-Z 1: in front of everything.
	return position;
}

[shader("fragment")]
ForwardTransparentTargets fragmentMain()
{
	ForwardLighting lit;
	lit.Radiance = Constants.Tint.rgb;
	lit.Indirect = float3(0.0, 0.0, 0.0);
	lit.Reflectance = float3(0.0, 0.0, 0.0);
	lit.Specular = float3(0.0, 0.0, 0.0);
	return ForwardTransparentWrite(lit, Constants.Tint.a);
}
#elif CULL_DEMO_DEPTH
[shader("vertex")]
float4 vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint boxIndexInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS) : SV_Position
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint boxIndex = DrawInstanceIndex(boxIndexInput);
	float3 corner;
	float3 normal;
	CubeVertex(vertexId, corner, normal);
	precise const float3 world = BoxWorld(Boxes[boxIndex], corner);
	return ForwardProjectWorld(world, world).Position;
}

[shader("fragment")]
void fragmentMain()
{
}
#else
struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float3 Normal : NORMAL;
	float4 Color : COLOR;
	float4 CurrentClip : CURRENT_CLIP;
	float4 PreviousClip : PREVIOUS_CLIP;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint boxIndexInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint boxIndex = DrawInstanceIndex(boxIndexInput);
	const Box box = Boxes[boxIndex];
	float3 corner;
	float3 normal;
	CubeVertex(vertexId, corner, normal);
	precise const float3 world = BoxWorld(box, corner);
	const ForwardClip clip = ForwardProjectWorld(world, world);
	VertexOutput output;
	output.Position = clip.Position;
	output.WorldPosition = world;
	output.Normal = normal;
	output.Color = box.Color;
	output.CurrentClip = clip.Current;
	output.PreviousClip = clip.Previous;
	return output;
}

[shader("fragment")]
ForwardOpaqueTargets fragmentMain(VertexOutput input)
{
	StandardPbrSurface surface;
	surface.Normal = normalize(input.Normal);
	surface.BaseColor = input.Color.rgb;
	surface.Metallic = 0.0;
	surface.PerceptualRoughness = input.Color.a;
	surface.Occlusion = 1.0;
	surface.Emissive = float3(0.0, 0.0, 0.0);
	surface.Alpha = 1.0;
	surface.Discard = false;
	const ForwardLighting lit = ForwardLightSurface(surface, input.WorldPosition, input.Position.xy);
	return ForwardOpaqueWrite(lit, surface, ForwardNoObject, input.CurrentClip, input.PreviousClip);
}
#endif
