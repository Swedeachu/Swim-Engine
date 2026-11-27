// Particle billboards (item 77): one instance per draw-list entry of the emitter named
// by the push constant, four corners from the index buffer {0, 1, 2, 2, 1, 3}.
// Particles::BillboardCorner places them; Particles::ShadeFragment shades them.
// Bindings follow ParticleRenderBindings; space 1 is the BindlessResourceTable.
#include "Resources/DrawIndex.slang"
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
	nointerpolation uint Material : TEXCOORD5;	// ParticleMaterial.
	nointerpolation float2 Random : TEXCOORD6; // x: per-particle random; y: normalized age.
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint instanceIdInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint instanceId = DrawInstanceIndex(instanceIdInput);
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
	// Particles::ParticleBillboardAxes and BillboardCorner.
	const float ox = (vertexId & 1u) != 0u ? 1.0 : -1.0;
	const float oy = (vertexId & 2u) != 0u ? 1.0 : -1.0;
	const float halfWidth = 0.5 * size;
	float halfHeight = halfWidth * lerp(Emitters[emitterIndex].AspectMin, Emitters[emitterIndex].AspectMax,
		ParticleRandom(p.Id, Emitters[emitterIndex].Seed, 11u));
	const float3 center = ParticleWorldPosition(transform, flags, p);
	const float3 toCamera = Frame[0].CameraPosition - center;
	float3 axisRight = Frame[0].CameraRight;
	float3 axisUp = Frame[0].CameraUp;
	const uint facing = Emitters[emitterIndex].Facing;
	if (facing == ParticleFacingVelocity)
	{
		const float3 velocity = (flags & ParticleFlagLocalSpace) != 0u ? ParticleTransformVector(transform, p.Velocity) : p.Velocity;
		const float speed = length(velocity);
		const float3 side = speed > 1.0e-6 ? cross(velocity / speed, toCamera) : float3(0.0, 0.0, 0.0);
		if (speed > 1.0e-6 && length(side) > 1.0e-6)
		{
			axisUp = velocity / speed;
			axisRight = normalize(side);
			halfHeight *= 1.0 + Emitters[emitterIndex].Stretch * speed;
		}
	}
	else if (facing == ParticleFacingUprightY)
	{
		const float3 side = cross(float3(0.0, 1.0, 0.0), toCamera);
		if (length(side) > 1.0e-6)
		{
			axisUp = float3(0.0, 1.0, 0.0);
			axisRight = normalize(side);
		}
	}
	else if (facing == ParticleFacingHorizontal)
	{
		axisRight = float3(1.0, 0.0, 0.0);
		axisUp = float3(0.0, 0.0, -1.0);
	}
	else if (facing == ParticleFacingEmitter)
	{
		const float3 emitterRight = float3(transform[0].x, transform[1].x, transform[2].x);
		const float3 emitterUp = float3(transform[0].y, transform[1].y, transform[2].y);
		if (length(emitterRight) > 1.0e-6 && length(emitterUp) > 1.0e-6)
		{
			axisRight = normalize(emitterRight);
			axisUp = normalize(emitterUp);
		}
	}
	const float c = cos(p.Rotation);
	const float s = sin(p.Rotation);
	const float3 right = axisRight * c + axisUp * s;
	const float3 up = axisUp * c - axisRight * s;
	const float3 world = center + right * (ox * halfWidth) + up * (oy * halfHeight);
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
	const uint played = uint(floor(p.Age * frameRate));
	const uint looped = (flags & ParticleFlagFlipbookClamp) != 0u ? min(played, frames - 1u) : played % frames;
	uint frame = frameRate > 0.0 ? looped : min(uint(floor(t * float(frames))), frames - 1u);
	if ((Emitters[emitterIndex].Flags & ParticleFlagRandomFrame) != 0u)
	{
		frame = min(uint(ParticleRandom(p.Id, Emitters[emitterIndex].Seed, 10u) * float(frames)), frames - 1u);
	}
	const uint columns = Emitters[emitterIndex].FlipbookColumns;
	const float column = float(frame % columns);
	const float row = float(frame / columns);
	output.Local = float2((ox + 1.0) * 0.5, (1.0 - oy) * 0.5);
	const float2 cell = float2((column + output.Local.x) / float(columns), (row + output.Local.y) / float(Emitters[emitterIndex].FlipbookRows));
	const float4 uvRect = Emitters[emitterIndex].UvRect;
	output.Uv = uvRect.xy + cell * (uvRect.zw - uvRect.xy);
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
	output.Material = (flags >> ParticleFlagMaterialShift) & 15u;
	output.Random = float2(ParticleRandom(p.Id, Emitters[emitterIndex].Seed, 12u), t);
	return output;
}

static const uint MaterialSoft = 1u;
static const uint MaterialFire = 2u;
static const uint MaterialSmoke = 3u;
static const uint MaterialSpark = 4u;
static const uint MaterialGlow = 5u;
static const uint MaterialRing = 6u;

float ParticleValueNoise(float2 p)
{
	const float2 i = floor(p);
	const float2 f = p - i;
	const float2 s = f * f * (3.0 - 2.0 * f);
	const uint2 c = uint2(int2(i) + 4096);
	const float a = float(ParticleHash(c.x * 73856093u ^ c.y * 19349663u)) / 4294967295.0;
	const float b = float(ParticleHash((c.x + 1u) * 73856093u ^ c.y * 19349663u)) / 4294967295.0;
	const float d = float(ParticleHash(c.x * 73856093u ^ (c.y + 1u) * 19349663u)) / 4294967295.0;
	const float e = float(ParticleHash((c.x + 1u) * 73856093u ^ (c.y + 1u) * 19349663u)) / 4294967295.0;
	return lerp(lerp(a, b, s.x), lerp(d, e, s.x), s.y);
}

float ParticleFbm(float2 p)
{
	return ParticleValueNoise(p) * 0.55 + ParticleValueNoise(p * 2.03 + 17.1) * 0.3 + ParticleValueNoise(p * 4.1 + 41.7) * 0.15;
}

// Procedural VFX shapes (ParticleMaterial): straight rgb and alpha, before premultiplying.
float4 ShadeMaterial(uint material, float4 color, float2 local, float2 random)
{
	const float2 d = local * 2.0 - 1.0;
	const float r2 = dot(d, d);
	const float seed = random.x * 97.0;
	const float age = random.y;
	if (material == MaterialSoft)
	{
		const float n = ParticleFbm(local * 3.0 + seed);
		color.a *= exp(-r2 * 3.2) * saturate(1.0 - r2) * lerp(0.75, 1.15, n);
		return color;
	}
	if (material == MaterialFire)
	{
		// A tongue: wide at the bottom, licking upward, eroded by rising noise as it ages.
		const float n = ParticleFbm(float2(d.x * 2.2, local.y * 2.6 + age * 3.5) + seed);
		const float width = lerp(0.95, 0.25, saturate(1.0 - local.y)) * (0.75 + 0.5 * n);
		const float body = saturate(1.0 - abs(d.x) / max(width, 0.05)) * saturate(1.0 - r2 * 0.6);
		const float mask = smoothstep(age * 0.85, age * 0.85 + 0.35, body * (0.6 + 0.6 * n));
		const float core = pow(saturate(body * 1.4 - 0.3), 2.0);
		color.rgb *= 1.0 + core * 2.5;
		color.rgb = lerp(color.rgb, float3(1.0, 0.95, 0.85) * max(max(color.r, color.g), color.b), core * 0.45);
		color.a *= mask;
		return color;
	}
	if (material == MaterialSmoke)
	{
		const float n = ParticleFbm(local * 2.5 + seed + age * 0.6);
		const float puff = saturate(1.0 - r2 * (1.0 + 0.8 * (1.0 - n)));
		const float density = smoothstep(age * 0.7, age * 0.7 + 0.4, puff * (0.55 + 0.6 * n));
		// Lit from above: the top of the billow is brighter, the underside in shade.
		const float light = lerp(0.45, 1.15, saturate(0.5 - d.y * 0.6 + (n - 0.5) * 0.5));
		color.rgb *= light;
		color.a *= density;
		return color;
	}
	if (material == MaterialSpark)
	{
		const float core = exp(-r2 * 18.0);
		const float halo = exp(-r2 * 4.0);
		color.rgb = color.rgb * (halo + core * 2.0) + core * 1.5 * max(max(color.r, color.g), color.b);
		color.a *= saturate(halo + core);
		return color;
	}
	if (material == MaterialGlow)
	{
		color.a *= exp(-r2 * 5.0) * saturate(1.0 - r2);
		return color;
	}
	if (material == MaterialRing)
	{
		const float r = sqrt(r2);
		const float width = lerp(0.18, 0.04, age);
		const float n = ParticleFbm(float2(atan2(d.y, d.x) * 3.0, age * 4.0) + seed);
		color.a *= exp(-pow((r - 0.85) / width, 2.0)) * lerp(0.6, 1.0, n) * step(r, 1.0);
		return color;
	}
	return color;
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
	if (input.Material != 0u)
	{
		color = ShadeMaterial(input.Material, color, input.Local, input.Random);
	}
	else if (input.Textured == 0u)
	{
		const float2 d = input.Local * 2.0 - 1.0;
		color.a *= clamp(1.0 - dot(d, d), 0.0, 1.0);
	}
	return float4(color.rgb * color.a, color.a);
}
