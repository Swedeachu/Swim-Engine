// Engine::WaveWater: Gerstner waves on a camera-centred grid, shaded by Water/WaterShading.slang
// in the transparent pass, with continuous LOD (Water/WaterWaves.slang): waves lower and the
// short ones flatten with distance, fine normal detail turns into roughness, reflection steps
// step down, and past the grid a flat far-field ring keeps the water shaded to the horizon.
// Grid: offset(u) = sign(u) * scale * (exp(rate * |u|) - 1) for u in -1..1 (as OceanSurface).
#include "Resources/DrawIndex.slang"
#include "Resources/RowRecord.slang"
#include "ForwardPlus/ForwardFrame.slang"
#include "Water/WaterShading.slang"
#include "Water/WaterWaves.slang"

struct WaveWaterParams
{
	float4 Grid;	  // xy: grid centre (world xz), z: mean height, w: rate.
	float4 GridShape; // x: quads per side, y: scale (m), z: far-field extent (0: none), w: time (s).
	float4 Bounds;	  // Water rectangle: min x, min z, max x, max z.
	float4 Edge;	  // x: wave fade-in from the bounds (m), y: grid extent (m), z: wave count, w: radians per pixel.
	WaterLodGpu Lod;
	float4 Absorption; // rgb, w: roughness.
	float4 Scattering; // rgb, w: refraction.
	float4 Viewport;   // xy: pixels, z: ripple strength, w: max reflection steps.
	WaterWaveGpu Waves[WaterMaxWaves];
	DistanceFog Fog; // Self-applied fog (off with the scene fog).
};

[[vk::binding(0, 2)]] StructuredBuffer<RowRecord<29>> Params; // One WaveWaterParams, read with LoadRowRecord.
[[vk::binding(1, 2)]] Texture2D<float4> SceneColor;
[[vk::binding(2, 2)]] Texture2D<float> SceneDepth;
[[vk::binding(3, 2)]] SamplerState SceneSampler;

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float3 Rest : REST_POSITION; // xz: undisplaced point, y: 1 on the far-field ring.
};

static const uint WaveQuadCorner[6] = { 0, 1, 2, 0, 2, 3 };

float WaveGridOffset(float u, float rate, float scale)
{
	return sign(u) * scale * (exp(rate * abs(u)) - 1.0);
}

float WaveGridSpacing(float u, float rate, float scale, float quads)
{
	return scale * rate * exp(rate * abs(u)) * 2.0 / quads;
}

float WaveEdgeFade(WaveWaterParams p, float2 rest)
{
	const float2 inside = min(rest - p.Bounds.xy, p.Bounds.zw - rest);
	return p.Edge.x > 0.0 ? saturate(min(inside.x, inside.y) / p.Edge.x) : 1.0;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const WaveWaterParams p = LoadRowRecord<WaveWaterParams>(Params, 0);
	const ForwardView view = Views[0];
	const uint quads = uint(p.GridShape.x);
	const uint gridVertices = quads * quads * 6u;
	const uint corner = WaveQuadCorner[vertexId % 6u];
	float2 rest;
	float spacing;
	float edge = 1.0;
	float ring = 0.0;
	if (vertexId < gridVertices)
	{
		const uint quad = vertexId / 6u;
		const uint2 cell = uint2(quad % quads, quad / quads);
		const uint2 grid = cell + uint2(corner == 1u || corner == 2u ? 1u : 0u, corner >= 2u ? 1u : 0u);
		const float2 u = float2(grid) / float(quads) * 2.0 - 1.0;
		rest = p.Grid.xy + float2(WaveGridOffset(u.x, p.Grid.w, p.GridShape.y), WaveGridOffset(u.y, p.Grid.w, p.GridShape.y));
		spacing = WaveGridSpacing(max(abs(u.x), abs(u.y)), p.Grid.w, p.GridShape.y, float(quads));
		// The waves settle to the mean level at the grid's edge, where the flat ring starts.
		edge = p.GridShape.z > 0.0 ? 1.0 - smoothstep(0.85, 1.0, max(abs(u.x), abs(u.y))) : 1.0;
	}
	else
	{
		// Far field: four trapezoids from the grid's edge square to the far-field square.
		const uint side = (vertexId - gridVertices) / 6u;
		const float inner = p.Edge.y;
		const float outer = max(p.GridShape.z, inner);
		const float2 cornersInner[4] = { float2(-inner, -inner), float2(inner, -inner), float2(inner, inner), float2(-inner, inner) };
		const float2 cornersOuter[4] = { float2(-outer, -outer), float2(outer, -outer), float2(outer, outer), float2(-outer, outer) };
		const uint a = side;
		const uint b = (side + 1u) % 4u;
		const float2 quadCorners[4] = { cornersInner[a], cornersInner[b], cornersOuter[b], cornersOuter[a] };
		rest = p.Grid.xy + quadCorners[corner];
		spacing = 1.0e6;
		edge = 0.0;
		ring = 1.0;
	}
	rest = clamp(rest, p.Bounds.xy, p.Bounds.zw);
	edge *= WaveEdgeFade(p, rest);

	const float distance = length(view.CameraPosition - float3(rest.x, p.Grid.z, rest.y));
	const WaterLodFactorsGpu lod = WaterEvaluateLod(p.Lod, distance);
	float3 world = float3(rest.x, p.Grid.z, rest.y);
	const uint count = uint(p.Edge.z);
	if (edge > 0.0)
	{
		for (uint i = 0u; i < count; ++i)
		{
			world += WaterWaveDisplacement(p.Waves[i], rest, WaterWaveWeight(p.Waves[i], lod, spacing) * edge);
		}
	}

	VertexOutput output;
	output.Position = ForwardProjectWorld(world, world).Position;
	output.WorldPosition = world;
	output.Rest = float3(rest.x, ring, rest.y);
	return output;
}

// Short ripples for the normal only (no geometry): a few travelling sines below the
// shortest Gerstner wave, as slopes.
float2 WaveRippleSlope(float2 p, float time, float footprint)
{
	float2 slope = float2(0.0, 0.0);
	float amplitude = 0.012;
	float frequency = 3.1;
	float angle = 0.4;
	for (uint i = 0u; i < 5u; ++i)
	{
		const float wavelength = 6.28318531 / frequency;
		const float resolved = saturate(wavelength / max(footprint * 4.0, 1.0e-4) - 1.0);
		const float2 d = float2(cos(angle), sin(angle));
		const float phase = dot(d, p) * frequency - time * sqrt(9.81 * frequency);
		slope += d * cos(phase) * amplitude * frequency * resolved;
		amplitude *= 0.7;
		frequency *= 1.7;
		angle += 2.1;
	}
	return slope;
}

[shader("fragment")]
ForwardTransparentTargets fragmentMain(VertexOutput input)
{
	const WaveWaterParams p = LoadRowRecord<WaveWaterParams>(Params, 0);
	const ForwardView view = Views[0];
	const float3 toCamera = view.CameraPosition - input.WorldPosition;
	const float distance = length(toCamera);
	const float3 v = toCamera / max(distance, 1.0e-4);
	const WaterLodFactorsGpu lod = WaterEvaluateLod(p.Lod, distance);
	// Metres of surface under one pixel (grazing views stretch it).
	const float footprint = distance * p.Edge.w / max(abs(v.y), 0.15);
	const float2 rest = input.Rest.xz;
	const float edge = WaveEdgeFade(p, rest);

	float3 dx = float3(1.0, 0.0, 0.0);
	float3 dz = float3(0.0, 0.0, 1.0);
	const uint count = uint(p.Edge.z);
	float kept = 0.0;
	float total = 0.0;
	for (uint i = 0u; i < count; ++i)
	{
		const float weight = WaterWaveWeight(p.Waves[i], lod, footprint) * edge;
		WaterWaveTangents(p.Waves[i], rest, weight, dx, dz);
		const float slope = p.Waves[i].AmplitudePhase.x * p.Waves[i].DirectionNumber.z;
		kept += slope * weight;
		total += slope;
	}
	float3 normal = normalize(cross(dz, dx));
	const float2 ripple = WaveRippleSlope(rest, p.GridShape.w, footprint) * p.Viewport.z * lod.Normal * edge;
	normal = normalize(normal + float3(-ripple.x, 0.0, -ripple.y));

	WaterSurface surface;
	surface.WorldPosition = input.WorldPosition;
	surface.Normal = normal;
	surface.Up = float3(0.0, 1.0, 0.0);
	surface.Position = input.Position;
	surface.Time = p.GridShape.w;
	surface.Foam = 0.0;
	WaterOptics optics = WaterDefaultOptics();
	optics.Absorption = p.Absorption.rgb;
	optics.Scattering = p.Scattering.rgb;
	optics.Refraction = p.Scattering.w;
	// Slope energy the LOD took out of the normal widens the highlight instead.
	const float lost = total > 0.0 ? saturate(1.0 - kept / total) : 0.0;
	optics.Roughness = clamp(p.Absorption.w + p.Lod.Fade1.w * max(lost, 1.0 - lod.Normal), 0.02, 1.0);
	optics.ReflectionSteps = floor(p.Viewport.w * lod.Reflection + 0.5);
	WaterScene scene;
	scene.Color = SceneColor;
	scene.Depth = SceneDepth;
	scene.Sampler = SceneSampler;
	scene.Size = p.Viewport.xy;
	ForwardLighting lit;
	lit.Radiance = WaterShade(surface, optics, scene, p.Fog);
	if (p.Lod.Fade2.w > 0.0)
	{
		const float3 tint = input.Rest.y > 0.5 ? float3(0.1, 0.2, 1.0) : lerp(float3(0.1, 1.0, 0.2), float3(1.0, 0.15, 0.1), lod.Detail * lod.Amplitude);
		lit.Radiance = lerp(lit.Radiance, tint * max(dot(lit.Radiance, float3(0.3, 0.6, 0.1)), 0.15) * 2.0, 0.6);
	}
	lit.Indirect = float3(0.0, 0.0, 0.0);
	lit.Reflectance = float3(0.0, 0.0, 0.0);
	lit.Specular = float3(0.0, 0.0, 0.0);
	return ForwardTransparentWrite(lit, 1.0);
}
