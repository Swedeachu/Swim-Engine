// Shared helpers of the built-in render features (Engine/Systems/Renderer/Features).
#ifndef SWIM_FEATURE_COMMON_SLANG
#define SWIM_FEATURE_COMMON_SLANG

float FeatureLuminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// The world-space view ray through a pixel center (uv 0..1, top-left origin).
float3 FeatureViewRay(float2 uv, float4 forward, float4 right, float4 up)
{
	const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	return normalize(forward.xyz + ndc.x * forward.w * right.xyz + ndc.y * right.w * up.xyz);
}

// Integer hash -> [0, 1) (PCG-style), for dithering and noise seeds.
uint FeatureHash(uint value)
{
	uint state = value * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float FeatureHash01(uint value)
{
	return float(FeatureHash(value)) * (1.0 / 4294967296.0);
}

// Interleaved gradient noise (Jimenez 2014): a cheap, well-distributed per-pixel dither.
float FeatureGradientNoise(float2 pixel, uint frame)
{
	const float2 p = pixel + float(frame % 64u) * float2(5.588238, 5.588238);
	return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

#endif
