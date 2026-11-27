// Seeing from inside water (Engine::Underwater), two programs from one source:
//  - UnderwaterMedium (UNDERWATER_LENS=0, before TAA): light through the water body.
//    Extinction and in-scattering over the path through water (to the geometry, or to the
//    surface plane when the ray leaves the water), total internal reflection outside
//    Snell's window, caustics on geometry below the surface and volumetric light shafts
//    (caustics projected along the refracted sun). Dithered; TAA resolves it.
//  - UnderwaterLens (UNDERWATER_LENS=1, before exposure): what a mask or eye sees through
//    water: a slight barrel bulge, an animated refraction wobble and radial chromatic
//    aberration.
// Both blend with the scene by the submersion amount (0 dry .. 1 submerged).
#include "Features/FeatureCommon.slang"
#include "Water/WaterCommon.slang"

#ifndef UNDERWATER_LENS
#define UNDERWATER_LENS 0
#endif

#if !UNDERWATER_LENS
struct UnderwaterParams
{
	float4 InverseViewProjection[4]; // Row-major rows, unjittered.
	float4 Camera;					 // xyz: camera position; w: submersion.
	float4 Surface;					 // x: surface height, y: time, z: caustic cells per metre, w: caustic intensity.
	float4 Absorption;				 // rgb: extinction per metre; w: density scale.
	float4 Scatter;					 // rgb: in-scattered radiance near the surface; w: darkening per metre of depth.
	float4 SunDirection;			 // Toward the sun above the water; w: shaft intensity.
	float4 SunColor;				 // Sun radiance; w: furthest distance the medium is integrated (m).
	float4 Shafts;					 // x: steps, y: shaft length (m), z: chromatic caustics (0/1), w: total internal reflection.
	uint4 Size;						 // xy: viewport; z: dither seed.
};

[[vk::binding(0, 0)]] StructuredBuffer<UnderwaterParams> Params;
[[vk::binding(1, 0)]] Texture2D<float4> Color;
[[vk::binding(2, 0)]] Texture2D<float> Depth;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

static const float WaterIor = 1.333;

float3 Reconstruct(UnderwaterParams p, float2 ndc, float depth)
{
	const float4 h = float4(ndc, depth, 1.0);
	const float4 world = float4(dot(p.InverseViewProjection[0], h), dot(p.InverseViewProjection[1], h), dot(p.InverseViewProjection[2], h),
		dot(p.InverseViewProjection[3], h));
	return world.xyz / world.w;
}

// The direction toward the sun inside the water (Snell refraction through a level surface).
float3 RefractedSun(float3 sun)
{
	const float3 down = refract(-normalize(sun), float3(0.0, 1.0, 0.0), 1.0 / WaterIor);
	return dot(down, down) > 0.0 ? -down : float3(0.0, 1.0, 0.0);
}

// Where light reaching p entered the surface.
float2 SurfaceEntry(float3 p, float3 sunInWater, float surface)
{
	return p.xz + sunInWater.xz * ((surface - p.y) / max(sunInWater.y, 0.15));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const UnderwaterParams p = Params[0];
	if (any(id.xy >= p.Size.xy))
	{
		return;
	}
	const float4 source = Color.Load(int3(id.xy, 0));
	const float submersion = p.Camera.w;
	if (submersion <= 0.0)
	{
		Output[id.xy] = source;
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(p.Size.xy);
	const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	const float depth = Depth.Load(int3(id.xy, 0));
	const bool sky = depth <= 0.0;
	const float3 camera = p.Camera.xyz;
	const float3 far = Reconstruct(p, ndc, sky ? 1.0e-5 : depth);
	const float3 ray = normalize(far - camera);
	const float maxDistance = p.SunColor.w;
	const float sceneDistance = sky ? maxDistance : min(length(far - camera), maxDistance);
	const float surface = p.Surface.x;
	const float time = p.Surface.y;
	const float3 sunInWater = RefractedSun(p.SunDirection.xyz);
	const float sunUp = saturate(p.SunDirection.y * 4.0 + 0.2);

	float pathLength = sceneDistance;
	bool leaves = false;
	if (camera.y < surface && ray.y > 1.0e-4)
	{
		const float t = (surface - camera.y) / ray.y;
		if (t < sceneDistance)
		{
			pathLength = t;
			leaves = true;
		}
	}

	float3 color = source.rgb;

	// Caustics: light focused by the waves above, on geometry below the surface.
	if (!sky && far.y < surface && p.Surface.w > 0.0)
	{
		const float below = surface - far.y;
		const float2 entry = SurfaceEntry(far, sunInWater, surface) * p.Surface.z;
		float3 caustic;
		if (p.Shafts.z > 0.0)
		{
			const float2 spread = float2(0.035, 0.0) * min(below, 8.0);
			WaterCausticSites first = WaterCausticSitesEmpty();
			WaterCausticSites second = WaterCausticSitesEmpty();
			const float r = WaterCausticsCached(entry - spread, time, first, second);
			const float g = WaterCausticsCached(entry, time, first, second);
			caustic = float3(r, g, WaterCausticsCached(entry + spread, time, first, second));
		}
		else
		{
			caustic = WaterCaustics(entry, time).xxx;
		}
		const float fade = exp(-below * 0.06) * smoothstep(0.0, 0.4, below);
		color *= 1.0 + caustic * (p.Surface.w * fade * sunUp);
	}

	const float3 extinction = p.Absorption.rgb * p.Absorption.w;
	const float3 inscatter = p.Scatter.rgb * exp(-max(surface - camera.y, 0.0) * p.Scatter.w);

	// Outside Snell's window the surface mirrors the water below it.
	if (leaves && p.Shafts.w > 0.0)
	{
		const float critical = sqrt(1.0 - 1.0 / (WaterIor * WaterIor));
		const float mirror = smoothstep(critical + 0.04, critical - 0.04, ray.y) * p.Shafts.w;
		color = lerp(color, inscatter * 0.6, mirror);
	}

	const float3 transmittance = exp(-extinction * pathLength);
	color = color * transmittance + inscatter * (1.0 - transmittance);

	// Light shafts: in-scattered sun along the view ray, modulated by the caustic pattern at
	// the point where that light entered the water.
	const uint steps = uint(p.Shafts.x);
	if (steps > 0u && p.SunDirection.w > 0.0)
	{
		const float shaftLength = min(pathLength, p.Shafts.y);
		const float stepSize = shaftLength / float(steps);
		const float dither = FeatureGradientNoise(float2(id.xy), p.Size.z);
		float3 sum = float3(0.0, 0.0, 0.0);
		WaterCausticSites sites = WaterCausticSitesEmpty();
		for (uint i = 0u; i < steps; ++i)
		{
			const float t = (float(i) + dither) * stepSize;
			const float3 at = camera + ray * t;
			const float depthBelow = surface - at.y;
			if (depthBelow <= 0.0)
			{
				continue;
			}
			const float light = WaterCausticsSoft(SurfaceEntry(at, sunInWater, surface) * p.Surface.z * 0.35, time * 0.6, sites);
			sum += exp(-extinction * (t + depthBelow)) * (light * light + 0.08);
		}
		const float g = 0.55;
		const float cosine = dot(ray, sunInWater);
		const float phase = (1.0 - g * g) / (4.0 * WaterPi * pow(1.0 + g * g - 2.0 * g * cosine, 1.5));
		color += sum * stepSize * phase * p.SunDirection.w * p.SunColor.rgb * normalize(p.Scatter.rgb + 1.0e-4) * sunUp;
	}

	Output[id.xy] = float4(lerp(source.rgb, color, submersion), source.a);
}
#else
struct LensConstants
{
	float4 Params; // x: bulge, y: wobble amplitude (uv), z: chromatic aberration, w: time.
	float4 Shape;  // x: wobble frequency, y: wobble speed, z: submersion, w: aspect (width / height).
	uint4 Size;
};

[[vk::push_constant]] ConstantBuffer<LensConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] SamplerState LinearClamp;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Constants.Size.xy))
	{
		return;
	}
	const float2 uv = (float2(id.xy) + 0.5) / float2(Constants.Size.xy);
	const float submersion = Constants.Shape.z;
	const float time = Constants.Params.w;
	// Centre-weighted coordinates: the corners keep their place, the centre swells.
	float2 c = (uv - 0.5) * float2(Constants.Shape.w, 1.0);
	const float r2 = dot(c, c) / (0.25 * (Constants.Shape.w * Constants.Shape.w + 1.0));
	c *= 1.0 - Constants.Params.x * submersion * (1.0 - r2);
	const float frequency = Constants.Shape.x;
	const float speed = Constants.Shape.y * time;
	const float2 wobble = float2(sin(uv.y * frequency + speed) + 0.5 * sin(uv.y * frequency * 2.3 - speed * 1.4 + uv.x * 5.0),
							  sin(uv.x * frequency * 0.8 - speed * 1.1) + 0.5 * sin(uv.x * frequency * 1.9 + speed * 1.7 + uv.y * 4.0)) *
		(Constants.Params.y * submersion / 1.5);
	const float2 base = c / float2(Constants.Shape.w, 1.0) + 0.5 + wobble;
	const float spread = Constants.Params.z * submersion * r2;
	const float2 offset = (base - 0.5) * spread;
	const float r = Color.SampleLevel(LinearClamp, base + offset, 0.0).r;
	const float4 g = Color.SampleLevel(LinearClamp, base, 0.0);
	const float b = Color.SampleLevel(LinearClamp, base - offset, 0.0).b;
	Output[id.xy] = float4(r, g.g, b, g.a);
}
#endif
