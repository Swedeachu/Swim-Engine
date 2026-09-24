// Particle billboards (item 77): one instance per draw-list entry of the emitter named
// by the push constant, four corners from the index buffer {0, 1, 2, 2, 1, 3}.
// Particles::BillboardCorner places them; Particles::ShadeFragment shades them.
// Bindings follow ParticleRenderBindings; space 1 is the BindlessResourceTable.
#include "Particles/ParticleRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ParticleFrame> Frame;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleEmitter> Emitters;
[[vk::binding(2, 0)]] StructuredBuffer<Particle> Particles;
[[vk::binding(3, 0)]] StructuredBuffer<uint> DrawList;
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];

struct DrawConstants
{
	uint Emitter;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> Draw;

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;	  // Sprite UV within the flipbook frame.
	float2 Local : TEXCOORD1; // UV within the quad.
	nointerpolation float4 Color : COLOR0;
	nointerpolation uint Textured : TEXCOORD2;
	nointerpolation uint TextureIndex : TEXCOORD3;
	nointerpolation uint SamplerIndex : TEXCOORD4;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
	const uint emitterIndex = Draw.Emitter;
	const uint firstSlot = Emitters[emitterIndex].FirstSlot;
	const Particle p = Particles[DrawList[firstSlot + instanceId]];
	const uint flags = Emitters[emitterIndex].Flags;
	float4 transform[3];
	for (uint r = 0; r < 3; ++r)
	{
		transform[r] = Emitters[emitterIndex].Transform[r];
	}
	const float t = ParticleNormalizedAge(p);
	const float size = p.Size * ParticleCurve(Emitters[emitterIndex].SizeTimes, Emitters[emitterIndex].SizeValues,
		Emitters[emitterIndex].SizeKeyCount, t);
	// Particles::BillboardCorner.
	const float ox = (vertexId & 1u) != 0u ? 1.0 : -1.0;
	const float oy = (vertexId & 2u) != 0u ? 1.0 : -1.0;
	const float half = 0.5 * size;
	const float c = cos(p.Rotation);
	const float s = sin(p.Rotation);
	const float3 cameraRight = Frame[0].CameraRight;
	const float3 cameraUp = Frame[0].CameraUp;
	const float3 right = cameraRight * c + cameraUp * s;
	const float3 up = cameraUp * c - cameraRight * s;
	const float3 world = ParticleWorldPosition(transform, flags, p) + (right * ox + up * oy) * half;
	float4 viewProjection[4];
	for (uint r = 0; r < 4; ++r)
	{
		viewProjection[r] = Frame[0].ViewProjection[r];
	}
	const float4 position = float4(world, 1.0);
	VertexOutput output;
	output.Position = float4(dot(viewProjection[0], position), dot(viewProjection[1], position), dot(viewProjection[2], position),
		dot(viewProjection[3], position));
	// Particles::FlipbookFrame.
	const uint frames = Emitters[emitterIndex].FlipbookFrames;
	const float frameRate = Emitters[emitterIndex].FlipbookFrameRate;
	const uint frame = frameRate > 0.0 ? uint(floor(p.Age * frameRate)) % frames : min(uint(floor(t * float(frames))), frames - 1u);
	const uint columns = Emitters[emitterIndex].FlipbookColumns;
	const float column = float(frame % columns);
	const float row = float(frame / columns);
	output.Local = float2((ox + 1.0) * 0.5, (1.0 - oy) * 0.5);
	output.Uv = float2((column + output.Local.x) / float(columns), (row + output.Local.y) / float(Emitters[emitterIndex].FlipbookRows));
	// Particles::EvaluateGradient.
	const float4 colorTimes = Emitters[emitterIndex].ColorTimes;
	const uint colorKeys = Emitters[emitterIndex].ColorKeyCount;
	float4 keys[4];
	for (uint k = 0; k < 4; ++k)
	{
		keys[k] = Emitters[emitterIndex].ColorValues[k];
	}
	float4 color;
	for (uint channel = 0; channel < 4; ++channel)
	{
		color[channel] = ParticleCurve(colorTimes, float4(keys[0][channel], keys[1][channel], keys[2][channel], keys[3][channel]), colorKeys, t);
	}
	output.Color = color;
	output.Textured = (flags & ParticleFlagTextured) != 0u ? 1u : 0u;
	output.TextureIndex = Emitters[emitterIndex].TextureIndex;
	output.SamplerIndex = Emitters[emitterIndex].SamplerIndex;
	return output;
}

// Particles::ShadeFragment: premultiplied (rgb * a, a).
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	float4 color = input.Color;
	if (input.Textured != 0u)
	{
		color *= Textures[NonUniformResourceIndex(input.TextureIndex)].Sample(Samplers[NonUniformResourceIndex(input.SamplerIndex)], input.Uv);
	}
	else
	{
		const float2 d = input.Local * 2.0 - 1.0;
		color.a *= clamp(1.0 - dot(d, d), 0.0, 1.0);
	}
	return float4(color.rgb * color.a, color.a);
}
