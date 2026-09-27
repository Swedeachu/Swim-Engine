// A physical camera lens over the HDR scene (Engine::CameraLens), before exposure and bloom:
//  - CameraLensHalation (CAMERA_LENS_HALATION=1): a quarter-resolution, blurred image of
//    what is brighter than the threshold (light scattering back through the film base:
//    the warm glow around bright objects), optionally stretched horizontally (anamorphic);
//  - CameraLens: per output pixel, the lens geometry (radial Brown-Conrady distortion k1,
//    k2 - barrel or pincushion - blended with an equidistant fisheye projection, and a
//    zoom), lateral chromatic aberration (red and blue land at slightly different radii),
//    corner softness (a small blur that grows toward the edges), the natural cos^4
//    vignette of the optics, the halation glow and a lens colour filter.
// Everything is continuous maths driven by the settings; presets only pick values.
#include "Features/FeatureCommon.slang"

#ifndef CAMERA_LENS_HALATION
#define CAMERA_LENS_HALATION 0
#endif

struct LensConstants
{
	float4 Distortion; // x: k1; y: k2; z: fisheye (0..1); w: zoom (> 0; CameraLens picks it so the corners map to the frame's corners).
	float4 Optics;	   // x: chromatic aberration; y: corner softness (px at 1080p); z: vignette strength (exponent of cos^4: 0 none, 1 an ideal thin lens); w: unused.
	float4 Halation;   // rgb: tint x intensity; w: threshold (scene luminance).
	float4 Filter;	   // rgb: lens filter colour (linear multiplier); w: halation horizontal stretch (1: round).
	float4 View;	   // x: tan(half horizontal fov); y: tan(half vertical fov); z: aspect; w: unused.
	uint4 Size;		   // xy: output size; zw: halation size.
};

[[vk::push_constant]] ConstantBuffer<LensConstants> Constants;

#if CAMERA_LENS_HALATION
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] SamplerState LinearClamp;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Glow;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.z || id.y >= Constants.Size.w)
	{
		return;
	}
	const float2 texel = 1.0 / float2(Constants.Size.xy);
	const float2 uv = (float2(id.xy) + 0.5) / float2(Constants.Size.zw);
	// A 13-tap disc (radius ~ 6 full-resolution pixels, stretched horizontally) of the
	// thresholded colour, soft-kneed so the glow grows smoothly with brightness.
	const float threshold = max(Constants.Halation.w, 1.0e-3);
	const float stretch = max(Constants.Filter.w, 1.0);
	float3 sum = float3(0.0, 0.0, 0.0);
	float weights = 0.0;
	for (int i = 0; i < 13; ++i)
	{
		const float angle = float(i) * 2.39996323;
		const float radius = i == 0 ? 0.0 : sqrt(float(i) / 12.0) * 6.0;
		const float2 offset = float2(cos(angle) * stretch, sin(angle)) * radius * texel * 2.0;
		const float3 c = Color.SampleLevel(LinearClamp, uv + offset, 0.0).rgb;
		const float luminance = FeatureLuminance(c);
		const float excess = max(luminance - threshold, 0.0);
		const float soft = excess * excess / (excess + threshold);
		const float w = 1.0 - 0.5 * radius / 6.0;
		sum += c * (soft / max(luminance, 1.0e-4)) * w;
		weights += w;
	}
	Glow[id.xy] = float4(sum / max(weights, 1.0e-4), 1.0);
}
#else
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float4> Glow;
[[vk::binding(2, 0)]] SamplerState LinearClamp;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

// Output-centred, aspect-corrected radius r (1 at the half-diagonal) -> the source radius.
float SourceRadius(float r)
{
	const float k1 = Constants.Distortion.x;
	const float k2 = Constants.Distortion.y;
	const float fisheye = saturate(Constants.Distortion.z);
	// Radial polynomial: k1 > 0 samples farther out toward the edges (barrel: straight lines
	// bow outward), k1 < 0 pincushion.
	float source = r * (1.0 + k1 * r * r + k2 * r * r * r * r);
	// Equidistant fisheye: output radius is proportional to the ray angle; the rectilinear
	// image has that angle at tan(angle). Normalized so the half-diagonal maps to itself.
	const float maxAngle = fisheye * 1.35;
	if (maxAngle > 1.0e-3)
	{
		const float fish = tan(min(r, 1.1) * maxAngle) / tan(maxAngle);
		source = lerp(source, fish, fisheye);
	}
	return source / max(Constants.Distortion.w, 1.0e-3);
}

float3 SampleLens(float2 uv, float scale, float2 centreOffset)
{
	// uv around the centre, in aspect-corrected units (half-diagonal = 1).
	const float aspect = Constants.View.z;
	const float halfDiagonal = sqrt(aspect * aspect + 1.0);
	const float2 p = (uv - 0.5) * 2.0 * float2(aspect, 1.0) / halfDiagonal;
	const float r = length(p);
	const float source = SourceRadius(r) * scale;
	const float2 q = r > 1.0e-6 ? p * (source / r) : p;
	const float2 sourceUv = q * halfDiagonal / (2.0 * float2(aspect, 1.0)) + 0.5 + centreOffset;
	if (any(sourceUv < 0.0) || any(sourceUv > 1.0))
	{
		return float3(0.0, 0.0, 0.0); // Outside the rendered frame: the lens barrel.
	}
	return Color.SampleLevel(LinearClamp, sourceUv, 0.0).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.x || id.y >= Constants.Size.y)
	{
		return;
	}
	const float2 size = float2(Constants.Size.xy);
	const float2 uv = (float2(id.xy) + 0.5) / size;
	const float aspect = Constants.View.z;
	const float2 centred = (uv - 0.5) * 2.0 * float2(aspect, 1.0) / sqrt(aspect * aspect + 1.0);
	const float r = length(centred);

	// Lateral chromatic aberration: red magnified, blue shrunk, growing with r^2.
	const float ca = Constants.Optics.x * r * r;
	// Corner softness: a 5-tap cross whose radius grows with r^2 (lenses are sharpest in the centre).
	const float soft = Constants.Optics.y * r * r * (size.y / 1080.0);
	const float2 texel = 1.0 / size;
	float3 color = float3(0.0, 0.0, 0.0);
	const float2 taps[5] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(-1.0, 0.0), float2(0.0, 1.0), float2(0.0, -1.0) };
	const float tapWeights[5] = { 0.4, 0.15, 0.15, 0.15, 0.15 };
	const int tapCount = soft > 0.05 ? 5 : 1;
	float total = 0.0;
	for (int t = 0; t < tapCount; ++t)
	{
		const float2 offset = taps[t] * soft * texel;
		const float w = tapCount == 1 ? 1.0 : tapWeights[t];
		float3 c;
		c.r = SampleLens(uv, 1.0 + ca, offset).r;
		c.g = SampleLens(uv, 1.0, offset).g;
		c.b = SampleLens(uv, 1.0 - ca, offset).b;
		color += c * w;
		total += w;
	}
	color /= total;

	// Halation: the glow of what is bright, tinted (warm for film).
	color += Glow.SampleLevel(LinearClamp, uv, 0.0).rgb * Constants.Halation.rgb;

	// Natural vignetting: cos^4 of the field angle (the angle of the pixel's ray to the
	// optical axis), raised to the strength (faster lenses and older designs vignette more).
	const float2 ndc = (uv - 0.5) * 2.0;
	const float2 field = ndc * Constants.View.xy;
	const float cos2 = 1.0 / (1.0 + dot(field, field));
	color *= pow(cos2 * cos2, max(Constants.Optics.z, 0.0));

	Output[id.xy] = float4(color * Constants.Filter.rgb, 1.0);
}
#endif
