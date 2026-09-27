// The sensor / film end of the camera (Engine::FilmSensor), on the display-referred frame
// (RGBA8Unorm holding sRGB-encoded values) after tone mapping:
//  - sharpening: an unsharp mask over the 4 neighbours whose strength is scaled down where
//    the local contrast is already high (contrast-adaptive: edges do not ring, fine detail
//    is lifted);
//  - grain: zero-mean value noise on cells of GrainSize pixels, new every frame, scaled by
//    the response of film/sensor noise to exposure - strongest in the mid-tones
//    (4 L (1 - L)), weaker in deep shadows and highlights - with an optional colour
//    (chroma) part.
#include "Features/FeatureCommon.slang"

struct SensorConstants
{
	float4 Params; // x: sharpen amount (0..1); y: grain intensity; z: grain size (px, >= 1); w: grain colour (0: luminance only, 1: independent per channel).
	uint4 Size;	   // xy: size; z: frame.
};

[[vk::push_constant]] ConstantBuffer<SensorConstants> Constants;

[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Output;

float3 Load(int2 p)
{
	return Color.Load(int3(clamp(p, int2(0, 0), int2(Constants.Size.xy) - 1), 0)).rgb;
}

// Value noise in [-1, 1] on a grid of `size`-pixel cells, smoothly interpolated.
float CellNoise(float2 pixel, float size, uint seed)
{
	const float2 p = pixel / size;
	const float2 cell = floor(p);
	const float2 f = p - cell;
	const float2 s = f * f * (3.0 - 2.0 * f);
	const int2 c = int2(cell);
	const float n00 = FeatureHash01(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ seed);
	const float n10 = FeatureHash01(uint(c.x + 1) * 73856093u ^ uint(c.y) * 19349663u ^ seed);
	const float n01 = FeatureHash01(uint(c.x) * 73856093u ^ uint(c.y + 1) * 19349663u ^ seed);
	const float n11 = FeatureHash01(uint(c.x + 1) * 73856093u ^ uint(c.y + 1) * 19349663u ^ seed);
	return lerp(lerp(n00, n10, s.x), lerp(n01, n11, s.x), s.y) * 2.0 - 1.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.x || id.y >= Constants.Size.y)
	{
		return;
	}
	const int2 p = int2(id.xy);
	float3 color = Load(p);

	const float sharpen = saturate(Constants.Params.x);
	if (sharpen > 0.0)
	{
		const float3 n = Load(p + int2(0, -1));
		const float3 s = Load(p + int2(0, 1));
		const float3 e = Load(p + int2(1, 0));
		const float3 w = Load(p + int2(-1, 0));
		const float3 lo = min(color, min(min(n, s), min(e, w)));
		const float3 hi = max(color, max(max(n, s), max(e, w)));
		// Headroom to black and white relative to the local range (AMD CAS's limiter).
		const float3 adaptive = sqrt(saturate(min(lo, 1.0 - hi) / max(hi, 1.0e-4)));
		const float3 weight = -adaptive * lerp(0.0, 0.16, sharpen); // Per neighbour (<= 0).
		color = saturate((color + (n + s + e + w) * weight) / (1.0 + 4.0 * weight));
	}

	const float grain = Constants.Params.y;
	if (grain > 0.0)
	{
		const float size = max(Constants.Params.z, 1.0);
		const uint seed = FeatureHash(Constants.Size.z * 9781u + 1u);
		const float2 pixel = float2(p) + 0.5;
		const float mono = CellNoise(pixel, size, seed);
		const float3 chroma = float3(CellNoise(pixel, size, seed ^ 0x68bc21ebu), mono, CellNoise(pixel, size, seed ^ 0x02e5be93u));
		const float3 noise = lerp(float3(mono, mono, mono), chroma, saturate(Constants.Params.w));
		const float luminance = FeatureLuminance(color);
		const float response = 4.0 * luminance * (1.0 - luminance) * 0.8 + 0.2;
		color = saturate(color + noise * grain * response);
	}
	Output[id.xy] = float4(color, 1.0);
}
