// Offscreen scene viewports for UI (SceneViewports): meshes pulled as StandardVertex from
// one GeometryHeap vertex page, one record per draw (gl_InstanceIndex = firstInstance),
// a directional light plus ambient, alpha-masked base colour through the bindless table.
// Mode 1 draws a full-viewport triangle at the far plane in ClearColor (the region clear).
// Output is premultiplied linear (the target encodes sRGB in hardware). Translucent draws
// (Flags bit 1, blended over) keep their alpha and only the faces turned towards LocalEye.

#include "Resources/DrawIndex.slang"

struct SceneViewportDraw
{
	float4 ClipFromLocal[4];
	float4 NormalFromLocal[3];
	float4 Tint;
	float4 LocalEye; // Eye position (w 1) or direction towards it (w 0), in the mesh's space.
	uint Texture;
	uint Sampler;
	float AlphaCutoff;
	uint Flags; // Bit 0 lit, bit 1 translucent.
};

struct SceneViewportConstants
{
	float4 LightDirection; // Towards the light, normalized.
	float4 LightColor;
	float4 Ambient;
	float4 ClearColor;
	uint Mode;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::binding(0, 0)]] StructuredBuffer<SceneViewportDraw> Draws;
[[vk::binding(1, 0)]] StructuredBuffer<float> Vertices;
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];
[[vk::push_constant]] ConstantBuffer<SceneViewportConstants> Constants;

static const uint VertexFloats = 12;

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 Normal : NORMAL;
	float2 Uv : TEXCOORD0;
	nointerpolation uint Draw : TEXCOORD1;
	float3 LocalPosition : TEXCOORD2;
	float3 LocalNormal : TEXCOORD3;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint instanceIdInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint instanceId = DrawInstanceIndex(instanceIdInput);
	VertexOutput output;
	if (Constants.Mode == 1)
	{
		const float2 corner = float2(float((vertexId << 1) & 2), float(vertexId & 2));
		output.Position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
		output.Normal = float3(0, 0, 1);
		output.Uv = float2(0, 0);
		output.Draw = 0;
		output.LocalPosition = float3(0, 0, 0);
		output.LocalNormal = float3(0, 0, 1);
		return output;
	}

	const SceneViewportDraw draw = Draws[instanceId];
	const uint base = vertexId * VertexFloats;
	const float4 p = float4(Vertices[base], Vertices[base + 1], Vertices[base + 2], 1.0);
	const float3 n = float3(Vertices[base + 3], Vertices[base + 4], Vertices[base + 5]);
	output.Position = float4(dot(draw.ClipFromLocal[0], p), dot(draw.ClipFromLocal[1], p), dot(draw.ClipFromLocal[2], p),
		dot(draw.ClipFromLocal[3], p));
	output.Normal = float3(dot(draw.NormalFromLocal[0].xyz, n), dot(draw.NormalFromLocal[1].xyz, n), dot(draw.NormalFromLocal[2].xyz, n));
	output.Uv = float2(Vertices[base + 10], Vertices[base + 11]);
	output.Draw = instanceId;
	output.LocalPosition = p.xyz;
	output.LocalNormal = n;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	if (Constants.Mode == 1)
	{
		return Constants.ClearColor;
	}

	const SceneViewportDraw draw = Draws[input.Draw];
	const float4 texel = Textures[NonUniformResourceIndex(draw.Texture)].Sample(Samplers[NonUniformResourceIndex(draw.Sampler)], input.Uv);
	const float4 color = texel * draw.Tint;
	if (color.a < draw.AlphaCutoff)
	{
		discard;
	}

	const bool translucent = (draw.Flags & 2u) != 0;
	if (translucent && dot(input.LocalNormal, draw.LocalEye.xyz - input.LocalPosition * draw.LocalEye.w) <= 0.0)
	{
		discard;
	}

	float3 light = float3(1, 1, 1);
	if ((draw.Flags & 1u) != 0)
	{
		const float3 n = normalize(input.Normal);
		light = Constants.Ambient.rgb + Constants.LightColor.rgb * saturate(dot(n, Constants.LightDirection.xyz));
	}

	if (translucent)
	{
		const float alpha = saturate(color.a);
		return float4(color.rgb * light * alpha, alpha);
	}

	return float4(color.rgb * light, 1.0);
}
