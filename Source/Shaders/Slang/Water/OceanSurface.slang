// Ocean surface (Engine::OceanSurface): a camera-centred grid, vertex-pulled (no buffers),
// displaced by the FFT cascades and shaded in the Forward+ opaque pass with the frame's
// lights, shadows and image-based reflections. Quad spacing grows in proportion to the
// distance from the centre: offset(u) = sign(u) * scale * (exp(rate * |u|) - 1) for u in -1..1.
#include "ForwardPlus/ForwardFrame.slang"
#include "Water/WaterCommon.slang"
#include "Water/WaterWaves.slang"

struct OceanSurfaceParams
{
	float4 Grid;		 // xy: grid centre (world xz), z: water height, w: rate.
	float4 GridShape;	 // x: quads per side, y: scale (m), z: metres per pixel per metre of distance, w: time (s).
	float4 InverseSizes; // 1 / tile size per cascade.
	float4 Weights;		 // Cascade weights; w: simulation resolution.
	float4 DeepColor;	 // Water body albedo; w: base roughness.
	float4 ScatterColor; // Subsurface colour; w: sun scatter strength.
	float4 FoamColor;	 // w: foam strength.
	float4 SunDirection; // Toward the sun; w: ambient scatter strength.
	float4 SunColor;	 // Sun radiance; w: crest height scatter scale (1/m).
	float4 FogColor;	 // Fog radiance; w: density (1/m).
	float4 Ambient;		 // rgb: sky radiance seen by the water body; w: previous displacement valid (motion vectors).
	WaterLodGpu Lod;	 // Continuous LOD (Engine::WaterLodSettings).
	float4 Far;			 // x: far-field extent (0: none), y: grid extent (m).
};

// Cascade weights at a distance: all lower with Amplitude; the finer two flatten with Detail.
float3 OceanLodWeights(WaterLodFactorsGpu lod)
{
	return lod.Amplitude * float3(1.0, lerp(1.0, lod.Detail, 0.5), lod.Detail);
}

[[vk::binding(0, 2)]] StructuredBuffer<OceanSurfaceParams> Params;
[[vk::binding(1, 2)]] Texture2D<float4> Displacement0;
[[vk::binding(2, 2)]] Texture2D<float4> Displacement1;
[[vk::binding(3, 2)]] Texture2D<float4> Displacement2;
[[vk::binding(4, 2)]] Texture2D<float4> Derivatives0;
[[vk::binding(5, 2)]] Texture2D<float4> Derivatives1;
[[vk::binding(6, 2)]] Texture2D<float4> Derivatives2;
[[vk::binding(7, 2)]] Texture2D<float4> PreviousDisplacement0;
[[vk::binding(8, 2)]] Texture2D<float4> PreviousDisplacement1;
[[vk::binding(9, 2)]] Texture2D<float4> PreviousDisplacement2;
[[vk::binding(10, 2)]] SamplerState WaveSampler;

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float3 Rest : REST_POSITION; // xz: undisplaced grid point, y: crest height above the mean.
	float4 CurrentClip : CURRENT_CLIP;
	float4 PreviousClip : PREVIOUS_CLIP;
};

static const uint OceanQuadCorner[6] = { 0, 1, 2, 0, 2, 3 };

float OceanGridOffset(float u, float rate, float scale)
{
	return sign(u) * scale * (exp(rate * abs(u)) - 1.0);
}

// Metres between neighbouring vertices at grid coordinate u.
float OceanGridSpacing(float u, float rate, float scale, float quads)
{
	return scale * rate * exp(rate * abs(u)) * 2.0 / quads;
}

OceanCascades SurfaceCascades(OceanSurfaceParams p)
{
	OceanCascades cascades;
	cascades.InverseSizes = p.InverseSizes;
	cascades.Weights = float4(p.Weights.xyz, 0.0);
	return cascades;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID)
{
	const OceanSurfaceParams p = Params[0];
	const uint quads = uint(p.GridShape.x);
	const uint corner = OceanQuadCorner[vertexId % 6u];
	float2 rest;
	float spacing;
	float edge;
	if (vertexId < quads * quads * 6u)
	{
		const uint quad = vertexId / 6u;
		const uint2 cell = uint2(quad % quads, quad / quads);
		const uint2 grid = cell + uint2(corner == 1u || corner == 2u ? 1u : 0u, corner >= 2u ? 1u : 0u);
		const float2 u = float2(grid) / float(quads) * 2.0 - 1.0;
		rest = p.Grid.xy + float2(OceanGridOffset(u.x, p.Grid.w, p.GridShape.y), OceanGridOffset(u.y, p.Grid.w, p.GridShape.y));
		spacing = OceanGridSpacing(max(abs(u.x), abs(u.y)), p.Grid.w, p.GridShape.y, float(quads));
		// The grid's far edge sinks smoothly to the mean level so it never shows a wave cut.
		edge = 1.0 - smoothstep(0.85, 1.0, max(abs(u.x), abs(u.y)));
	}
	else
	{
		// Far field: a flat ring of four trapezoids from the grid's edge to Far.x.
		const uint side = (vertexId - quads * quads * 6u) / 6u;
		const float inner = p.Far.y;
		const float outer = max(p.Far.x, inner);
		const float2 corners[4] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0) };
		const float2 ring[4] = { corners[side] * inner, corners[(side + 1u) % 4u] * inner, corners[(side + 1u) % 4u] * outer, corners[side] * outer };
		rest = p.Grid.xy + ring[corner];
		spacing = 1.0e6;
		edge = 0.0;
	}

	const ForwardView view = Views[0];
	const WaterLodFactorsGpu lod = WaterEvaluateLod(p.Lod, length(view.CameraPosition - float3(rest.x, p.Grid.z, rest.y)));
	OceanCascades cascades = SurfaceCascades(p);
	cascades.Weights.xyz = OceanGridWeights(cascades.InverseSizes, cascades.Weights.xyz, spacing) * OceanLodWeights(lod);
	const float4 displacement = OceanSampleDisplacement(Displacement0, Displacement1, Displacement2, WaveSampler, cascades, rest);
	float3 previous = displacement.xyz;
	if (p.Ambient.w > 0.0)
	{
		previous = OceanSampleDisplacement(PreviousDisplacement0, PreviousDisplacement1, PreviousDisplacement2, WaveSampler, cascades, rest).xyz;
	}
	const float3 world = float3(rest.x, p.Grid.z, rest.y) + displacement.xyz * edge;
	const float3 previousWorld = float3(rest.x, p.Grid.z, rest.y) + previous * edge;
	const ForwardClip clip = ForwardProjectWorld(world, previousWorld);

	VertexOutput output;
	output.Position = clip.Position;
	output.WorldPosition = world;
	output.Rest = float3(rest.x, displacement.y * edge, rest.y);
	output.CurrentClip = clip.Current;
	output.PreviousClip = clip.Previous;
	return output;
}

float OceanValueNoise(float2 p)
{
	const float2 i = floor(p);
	const float2 f = p - i;
	const float2 s = f * f * (3.0 - 2.0 * f);
	const float a = WaterHash2(i).x;
	const float b = WaterHash2(i + float2(1.0, 0.0)).x;
	const float c = WaterHash2(i + float2(0.0, 1.0)).x;
	const float d = WaterHash2(i + float2(1.0, 1.0)).x;
	return lerp(lerp(a, b, s.x), lerp(c, d, s.x), s.y);
}

[shader("fragment")]
ForwardOpaqueTargets fragmentMain(VertexOutput input)
{
	const OceanSurfaceParams p = Params[0];
	const ForwardView view = Views[0];
	const float3 toCamera = view.CameraPosition - input.WorldPosition;
	const float viewDistance = length(toCamera);
	const float3 v = toCamera / max(viewDistance, 1.0e-4);

	float4 weights = OceanFootprintWeights(SurfaceCascades(p), OceanSurfaceFootprint(viewDistance * p.GridShape.z, abs(v.y)), p.Weights.w);
	const WaterLodFactorsGpu lod = WaterEvaluateLod(p.Lod, viewDistance);
	weights.xyz *= OceanLodWeights(lod);
	// Detail the LOD took out of the normal widens the highlight instead.
	weights.w = max(weights.w, p.Lod.Fade1.w / 0.4 * (1.0 - lod.Normal));
	const float4 derivatives =
		OceanSampleDerivatives(Derivatives0, Derivatives1, Derivatives2, WaveSampler, p.InverseSizes, weights.xyz, input.Rest.xz);
	float3 normal = OceanNormal(derivatives);
	// A surface seen from below faces the camera.
	normal = dot(normal, v) < 0.0 && view.CameraPosition.y < p.Grid.z ? -normal : normal;

	// Foam: the simulation's whitecaps broken into streaks by noise, thinning with distance.
	OceanCascades resolved = SurfaceCascades(p);
	resolved.Weights.xyz = weights.xyz;
	const float4 displacement = OceanSampleDisplacement(Displacement0, Displacement1, Displacement2, WaveSampler, resolved, input.Rest.xz);
	const float time = p.GridShape.w;
	const float streaks = OceanValueNoise(input.Rest.xz * 0.35 + float2(time * 0.05, 0.0)) * 0.6 +
		OceanValueNoise(input.Rest.xz * 1.7 - float2(0.0, time * 0.11)) * 0.4;
	const float foam = saturate(displacement.w * p.FoamColor.w * smoothstep(0.15, 0.75, streaks + displacement.w * 0.35));

	const float3 sun = normalize(p.SunDirection.xyz);
	// Subsurface light through thin crests facing away from the sun, and the body's glow
	// toward the viewer (an approximation after Atlas' ocean, GDC 2019).
	const float height = max(input.Rest.y, 0.0) * p.SunColor.w;
	const float backlit = pow(saturate(dot(-v, sun) * 0.5 + 0.5), 4.0) * pow(saturate(0.5 - 0.5 * dot(sun, normal)), 3.0);
	const float facing = pow(saturate(dot(v, normal)), 2.0);
	const float3 scatter = p.ScatterColor.rgb * (p.ScatterColor.w * height * backlit * p.SunColor.rgb + p.SunDirection.w * facing * p.Ambient.rgb);

	StandardPbrSurface surface;
	surface.Normal = normal;
	surface.BaseColor = lerp(p.DeepColor.rgb, p.FoamColor.rgb, foam);
	surface.Metallic = 0.0;
	// Wave detail faded out of the normal becomes roughness, so distant glints do not sparkle.
	surface.PerceptualRoughness = lerp(clamp(p.DeepColor.w + weights.w * 0.4, 0.02, 1.0), 0.85, foam);
	surface.Occlusion = 1.0;
	surface.Emissive = scatter * (1.0 - foam);
	surface.Alpha = 1.0;
	surface.Discard = false;
	surface.Thin = false;

	ForwardLighting lit = ForwardLightSurface(view, Grid[0], surface, input.WorldPosition, input.Position.xy);
	const float fog = 1.0 - exp(-viewDistance * p.FogColor.w);
	lit.Radiance = lerp(lit.Radiance, p.FogColor.rgb, fog);
	lit.Indirect *= 1.0 - fog;
	lit.Reflectance *= 1.0 - fog;
	lit.Specular *= 1.0 - fog;
	if (p.Lod.Fade2.w > 0.0)
	{
		const float3 tint = input.Rest.y == 0.0 && viewDistance > p.Far.y ? float3(0.1, 0.2, 1.0)
																		 : lerp(float3(0.1, 1.0, 0.2), float3(1.0, 0.15, 0.1), lod.Detail * lod.Amplitude);
		lit.Radiance = lerp(lit.Radiance, tint * max(dot(lit.Radiance, float3(0.3, 0.6, 0.1)), 0.15) * 2.0, 0.6);
	}
	return ForwardOpaqueWrite(lit, surface, ForwardNoObject, input.CurrentClip, input.PreviousClip);
}
