// Volumetric clouds (Engine::VolumetricClouds), two programs from one source:
//  - VolumetricCloudsMarch (CLOUDS_COMPOSITE=0): at a reduced resolution, each pixel's
//    view ray is marched through a horizontal cloud layer (procedural fBm density shaped
//    by coverage and height), lit by the sun (a short shadow march, Beer-Lambert with a
//    powder term, a two-lobe Henyey-Greenstein phase) and the sky's ambient colors, with
//    energy-conserving integration (Hillaire 2016). Output: in-scattered radiance (rgb) and
//    transmittance (a). The start of each march is dithered per pixel and frame; TAA,
//    which runs after this stage, resolves the noise.
//  - VolumetricCloudsComposite (CLOUDS_COMPOSITE=1): upsamples that onto the sky pixels
//    of the HDR scene color (color * transmittance + radiance).
#include "Features/FeatureCommon.slang"

#ifndef CLOUDS_COMPOSITE
#define CLOUDS_COMPOSITE 0
#endif

struct CloudParams
{
	float4 Forward;		  // w: tan(half horizontal fov).
	float4 Right;		  // w: tan(half vertical fov).
	float4 Up;			  // w: time (seconds).
	float4 Camera;		  // xyz: world position; w: coverage (0..1).
	float4 SunDirection;  // Toward the sun; w: density (extinction per meter at full density).
	float4 SunColor;	  // Sun radiance; w: ambient strength.
	float4 Layer;		  // x: bottom altitude; y: top altitude; z: shape frequency (1/m); w: detail frequency (1/m).
	float4 Wind;		  // xyz: noise offset (m, integrated wind); w: max ray distance (m).
	float4 AmbientTop;	  // rgb: sky radiance above the clouds; w: phase anisotropy g.
	float4 AmbientBottom; // rgb: radiance below (horizon); w: powder strength.
	float4 Lighting;	  // x: shadow step length (m); y: silver lining (back lobe weight); z: detail erosion; w: horizon fade start (0..1 of max distance).
	uint4 Size;			  // xy: march size; z: march steps; w: frame.
	uint4 Output;		  // xy: full size; z: shadow steps; w: unused.
};

[[vk::binding(0, 0)]] StructuredBuffer<CloudParams> Params;

#if !CLOUDS_COMPOSITE
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Clouds;

float CloudHash3(int3 cell)
{
	const uint h = FeatureHash(uint(cell.x) * 73856093u ^ FeatureHash(uint(cell.y) * 19349663u ^ FeatureHash(uint(cell.z) * 83492791u)));
	return float(h) * (1.0 / 4294967296.0);
}

float CloudValueNoise(float3 p)
{
	const float3 cell = floor(p);
	const float3 f = p - cell;
	const float3 u = f * f * (3.0 - 2.0 * f);
	const int3 c = int3(cell);
	const float n000 = CloudHash3(c);
	const float n100 = CloudHash3(c + int3(1, 0, 0));
	const float n010 = CloudHash3(c + int3(0, 1, 0));
	const float n110 = CloudHash3(c + int3(1, 1, 0));
	const float n001 = CloudHash3(c + int3(0, 0, 1));
	const float n101 = CloudHash3(c + int3(1, 0, 1));
	const float n011 = CloudHash3(c + int3(0, 1, 1));
	const float n111 = CloudHash3(c + int3(1, 1, 1));
	const float x00 = lerp(n000, n100, u.x);
	const float x10 = lerp(n010, n110, u.x);
	const float x01 = lerp(n001, n101, u.x);
	const float x11 = lerp(n011, n111, u.x);
	return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

float CloudFbm(float3 p, uint octaves)
{
	float sum = 0.0;
	float amplitude = 0.5;
	float total = 0.0;
	for (uint i = 0; i < octaves; ++i)
	{
		sum += CloudValueNoise(p) * amplitude;
		total += amplitude;
		p = p * 2.03 + float3(17.1, 3.7, 9.2);
		amplitude *= 0.5;
	}
	return sum / total;
}

// Density (0..1 of full extinction) at a world point; `cheap` uses fewer octaves (shadow rays).
float CloudDensity(CloudParams params, float3 position, bool cheap)
{
	const float bottom = params.Layer.x;
	const float top = params.Layer.y;
	const float height = saturate((position.y - bottom) / max(top - bottom, 1.0));
	// Rounded bottoms, softer tops (cumulus-like profile).
	const float profile = saturate(height * 5.0) * saturate((1.0 - height) * 2.2);
	if (profile <= 0.0)
	{
		return 0.0;
	}
	const float3 p = (position + params.Wind.xyz) * params.Layer.z;
	const float base = CloudFbm(p, cheap ? 3u : 4u);
	const float coverage = saturate(params.Camera.w);
	float density = saturate((base - (1.0 - coverage)) / max(coverage, 1.0e-3)) * profile;
	if (!cheap && density > 0.0)
	{
		const float detail = CloudFbm((position + params.Wind.xyz * 1.7) * params.Layer.w, 2u);
		density = saturate(density - detail * params.Lighting.z * (1.0 - density));
	}
	return density;
}

float CloudPhase(float cosine, float g)
{
	const float g2 = g * g;
	return (1.0 - g2) / (12.5663706 * pow(max(1.0 + g2 - 2.0 * g * cosine, 1.0e-4), 1.5));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const CloudParams params = Params[0];
	if (id.x >= params.Size.x || id.y >= params.Size.y)
	{
		return;
	}
	const float2 uv = (float2(id.xy) + 0.5) / float2(params.Size.xy);
	const float3 ray = FeatureViewRay(uv, params.Forward, params.Right, params.Up);
	const float3 camera = params.Camera.xyz;
	const float bottom = params.Layer.x;
	const float top = params.Layer.y;
	const float maxDistance = params.Wind.w;
	// The layer's span along the ray (the camera may be below, inside or above it).
	float t0, t1;
	if (abs(ray.y) < 1.0e-4)
	{
		const bool inside = camera.y >= bottom && camera.y <= top;
		t0 = 0.0;
		t1 = inside ? maxDistance : -1.0;
	}
	else
	{
		const float a = (bottom - camera.y) / ray.y;
		const float b = (top - camera.y) / ray.y;
		t0 = max(min(a, b), 0.0);
		t1 = min(max(a, b), maxDistance);
	}
	if (t1 <= t0)
	{
		Clouds[id.xy] = float4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	const uint steps = clamp(params.Size.z, 4u, 256u);
	const float stepLength = (t1 - t0) / float(steps);
	const float jitter = FeatureGradientNoise(float2(id.xy), params.Size.w);
	const float sigma = max(params.SunDirection.w, 1.0e-6);
	const float cosine = dot(ray, params.SunDirection.xyz);
	const float g = params.AmbientTop.w;
	const uint shadowSteps = clamp(params.Output.z, 1u, 16u);
	float transmittance = 1.0;
	float3 radiance = float3(0.0, 0.0, 0.0);
	float t = t0 + stepLength * jitter;
	for (uint i = 0; i < steps; ++i)
	{
		const float3 position = camera + ray * t;
		const float density = CloudDensity(params, position, false);
		if (density > 1.0e-3)
		{
			// Sun visibility through the cloud above this point.
			float optical = 0.0;
			for (uint j = 0; j < shadowSteps; ++j)
			{
				const float3 q = position + params.SunDirection.xyz * (params.Lighting.x * (float(j) + 0.5));
				optical += CloudDensity(params, q, true) * params.Lighting.x;
			}
			// Multiple scattering, approximated with three octaves of ever weaker extinction,
			// weaker contribution and a flatter phase (Wrenninge 2013): without it thick
			// cumulus comes out grey instead of white.
			float sunLight = 0.0;
			float attenuation = 1.0;
			float contribution = 1.0;
			float octaveG = 1.0;
			for (uint octave = 0; octave < 3u; ++octave)
			{
				const float octavePhase = lerp(CloudPhase(cosine, g * octaveG), CloudPhase(cosine, -0.3 * octaveG), params.Lighting.y);
				sunLight += contribution * exp(-optical * sigma * attenuation) * octavePhase;
				attenuation *= 0.35;
				contribution *= 0.55;
				octaveG *= 0.5;
			}
			const float powder = lerp(1.0, 1.0 - exp(-density * sigma * stepLength * 4.0), params.AmbientBottom.w);
			const float height = saturate((position.y - bottom) / max(top - bottom, 1.0));
			// Ambient: sky above brightens the tops, the horizon lights the bases.
			const float3 ambient = lerp(params.AmbientBottom.rgb, params.AmbientTop.rgb, height) * params.SunColor.w * (0.6 + 0.4 * height);
			const float3 scattering = params.SunColor.rgb * (sunLight * powder) + ambient;
			const float extinction = density * sigma;
			const float stepTransmittance = exp(-extinction * stepLength);
			// Energy-conserving: the in-scattering integrated over the step.
			radiance += transmittance * scattering * (1.0 - stepTransmittance);
			transmittance *= stepTransmittance;
			if (transmittance < 0.01)
			{
				break;
			}
		}
		t += stepLength;
	}
	// Distant clouds fade into the sky (hides the far, undersampled part of the layer).
	const float fadeStart = saturate(params.Lighting.w) * maxDistance;
	const float fade = 1.0 - saturate((t0 - fadeStart) / max(maxDistance - fadeStart, 1.0));
	Clouds[id.xy] = float4(radiance * fade, lerp(1.0, transmittance, fade));
}
#else
[[vk::binding(1, 0)]] Texture2D<float4> Color;
[[vk::binding(2, 0)]] Texture2D<float> Depth;
[[vk::binding(3, 0)]] Texture2D<float4> Clouds;
[[vk::binding(4, 0)]] SamplerState LinearClamp;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const CloudParams params = Params[0];
	if (id.x >= params.Output.x || id.y >= params.Output.y)
	{
		return;
	}
	float4 scene = Color.Load(int3(id.xy, 0));
	if (Depth.Load(int3(id.xy, 0)) <= 0.0)
	{
		const float2 uv = (float2(id.xy) + 0.5) / float2(params.Output.xy);
		const float4 clouds = Clouds.SampleLevel(LinearClamp, uv, 0.0);
		scene.rgb = scene.rgb * clouds.a + clouds.rgb;
	}
	Output[id.xy] = scene;
}
#endif
