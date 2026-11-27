// The box field's draws (Game::BoxField), vertex-pulled from the Boxes buffer the compute
// pass filled (the draw's first instance is the box), three programs from one source:
//  - BoxField: the Forward+ opaque pass, lit like the scene (ForwardFrame.slang);
//  - BoxFieldDepth (BOX_DEPTH_ONLY=1): the depth prepass, the same vertex stage;
//  - BoxFieldShadow (BOX_SHADOW=1): shadow tiles (ShadowFrame.slang).

#include "Resources/DrawIndex.slang"

#ifndef BOX_DEPTH_ONLY
#define BOX_DEPTH_ONLY 0
#endif
#ifndef BOX_SHADOW
#define BOX_SHADOW 0
#endif

#if BOX_SHADOW
#include "Shadows/ShadowFrame.slang"
#else
#include "ForwardPlus/ForwardFrame.slang"
#endif

struct Box
{
	float4 CenterHalf;
	float4 PreviousCenter;
	float4 Color;
};

[[vk::binding(0, 2)]] StructuredBuffer<Box> Boxes;

#if !BOX_SHADOW && !BOX_DEPTH_ONLY
#include "Resources/TextureArray.slang"

static const uint FaceLayers = 4;
[[vk::binding(1, 2)]] Texture2DArray Faces;
[[vk::binding(2, 2)]] SamplerState FaceSampler;
[[vk::binding(3, 2)]] StructuredBuffer<float> FaceMinLod;
#endif

// The corner and normal of vertex `vertexId` of a unit cube (six faces, two triangles each).
void CubeVertex(uint vertexId, out float3 position, out float3 normal)
{
	const uint face = vertexId / 6u;
	const uint corner = vertexId % 6u;
	const uint axis = face / 2u;
	const float side = (face % 2u) == 0u ? 1.0 : -1.0;
	const float2 quad[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
	const float2 uv = quad[corner] * 2.0 - 1.0;
	float3 p = axis == 0u ? float3(side, uv.x, uv.y) : axis == 1u ? float3(uv.y, side, uv.x) : float3(uv.x, uv.y, side);
	position = p;
	normal = axis == 0u ? float3(side, 0, 0) : axis == 1u ? float3(0, side, 0) : float3(0, 0, side);
}

#if BOX_SHADOW
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
	const Box box = Boxes[boxIndex];
	float3 corner;
	float3 normal;
	CubeVertex(vertexId, corner, normal);
	return ShadowProjectWorld(Constants.ViewIndex, box.CenterHalf.xyz + corner * box.CenterHalf.w);
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
	float2 Uv : FACE_UV;
	nointerpolation uint Layer : FACE_LAYER;
};

#if BOX_DEPTH_ONLY
// The opaque variant's position, computed the same way (bit-identical depth).
[shader("vertex")]
float4 vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint boxIndexInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS) : SV_Position
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint boxIndex = DrawInstanceIndex(boxIndexInput);
	const Box box = Boxes[boxIndex];
	float3 corner;
	float3 normal;
	CubeVertex(vertexId, corner, normal);
	precise const float3 world = box.CenterHalf.xyz + corner * box.CenterHalf.w;
	const float3 previous = box.PreviousCenter.xyz + corner * box.CenterHalf.w;
	return ForwardProjectWorld(world, previous).Position;
}

[shader("fragment")]
void fragmentMain()
{
}
#else
[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint boxIndexInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint boxIndex = DrawInstanceIndex(boxIndexInput);
	const Box box = Boxes[boxIndex];
	float3 corner;
	float3 normal;
	CubeVertex(vertexId, corner, normal);
	precise const float3 world = box.CenterHalf.xyz + corner * box.CenterHalf.w;
	const float3 previous = box.PreviousCenter.xyz + corner * box.CenterHalf.w;
	const ForwardClip clip = ForwardProjectWorld(world, previous);
	VertexOutput output;
	output.Position = clip.Position;
	output.WorldPosition = world;
	output.Normal = normal;
	output.Color = box.Color;
	output.CurrentClip = clip.Current;
	output.PreviousClip = clip.Previous;
	const float2 quad[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
	output.Uv = quad[vertexId % 6u];
	output.Layer = boxIndex % FaceLayers;
	return output;
}

[shader("fragment")]
ForwardOpaqueTargets fragmentMain(VertexOutput input)
{
	StandardPbrSurface surface;
	surface.Normal = normalize(input.Normal);
	const float4 face = TextureArraySample(Faces, FaceSampler, input.Uv, float(input.Layer), FaceMinLod[input.Layer]);
	surface.BaseColor = input.Color.rgb * face.rgb;
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
#endif
