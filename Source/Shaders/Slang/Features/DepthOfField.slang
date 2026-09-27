// Depth of field (Engine::DepthOfField) on the linear HDR scene, before the camera lens,
// exposure and bloom. The circle of confusion is the thin-lens one:
//     c = A * f * |s - S| / (s * (S - f)),  A = f / N,
// (f focal length, N f-number, S focus distance, s the pixel's distance), converted to
// pixels through the sensor height, signed (< 0: in front of the focus plane).
//  - DepthOfFieldPrepare (DOF_PREPARE=1): half resolution; rgb = the colour, a = signed CoC
//    in half-resolution pixels (the nearest of the 2x2 depths, so thin foreground edges
//    keep their blur). Autofocus reads the centre of the depth buffer here too.
//  - DepthOfFieldGather (DOF_GATHER=1): half resolution; a golden-angle disc gather where a
//    sample contributes when its own CoC reaches the pixel (scatter-as-gather). Samples
//    behind the pixel may not blur more than the pixel itself (a sharp subject keeps a sharp
//    silhouette against a blurred background); samples in front spill over it. The disc can
//    be squeezed vertically (anamorphic oval bokeh). a = how much of the result is
//    foreground blur (its coverage).
//  - DepthOfField: full resolution; the sharp scene blended toward the gather by the
//    pixel's CoC and the foreground coverage.
#include "Features/FeatureCommon.slang"

#ifndef DOF_PREPARE
#define DOF_PREPARE 0
#endif
#ifndef DOF_GATHER
#define DOF_GATHER 0
#endif

struct DofConstants
{
	float4 Lens;  // x: focal length (m); y: aperture diameter (m); z: focus distance (m, 0: autofocus); w: pixels per metre on the sensor (full resolution).
	float4 Blur;  // x: max CoC radius (full-res px); y: near plane (depth = near / distance); z: anamorphic squeeze (>= 1); w: autofocus minimum distance.
	uint4 Size;	  // xy: full size; zw: half size.
	uint4 Params; // x: gather samples; y: frame.
};

[[vk::push_constant]] ConstantBuffer<DofConstants> Constants;

float DistanceOf(float depth)
{
	return depth > 0.0 ? Constants.Blur.y / depth : 1.0e6;
}

#if DOF_PREPARE
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] SamplerState LinearClamp;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Half;

// Autofocus: the nearest of a small cross around the centre (the subject the camera looks at).
float FocusDistance()
{
	if (Constants.Lens.z > 0.0)
	{
		return Constants.Lens.z;
	}
	const int2 centre = int2(Constants.Size.xy) / 2;
	const int2 spread = int2(max(Constants.Size.y / 40u, 1u), max(Constants.Size.y / 40u, 1u));
	const int2 taps[5] = { int2(0, 0), int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
	float nearest = 1.0e6;
	for (int i = 0; i < 5; ++i)
	{
		nearest = min(nearest, DistanceOf(Depth.Load(int3(centre + taps[i] * spread, 0))));
	}
	return clamp(nearest, Constants.Blur.w, 1.0e4);
}

// Signed CoC radius in full-resolution pixels.
float CircleOfConfusion(float distance, float focus)
{
	const float f = Constants.Lens.x;
	const float coc = Constants.Lens.y * f * (distance - focus) / (distance * max(focus - f, 1.0e-4));
	return clamp(0.5 * coc * Constants.Lens.w, -Constants.Blur.x, Constants.Blur.x);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.z || id.y >= Constants.Size.w)
	{
		return;
	}
	const float focus = FocusDistance();
	const int2 full = int2(id.xy) * 2;
	const int2 limit = int2(Constants.Size.xy) - 1;
	float nearest = 0.0;
	for (int k = 0; k < 4; ++k)
	{
		nearest = max(nearest, Depth.Load(int3(min(full + int2(k & 1, k >> 1), limit), 0)));
	}
	const float2 uv = (float2(full) + 1.0) / float2(Constants.Size.xy);
	const float3 color = Color.SampleLevel(LinearClamp, uv, 0.0).rgb;
	Half[id.xy] = float4(color, CircleOfConfusion(DistanceOf(nearest), focus) * 0.5);
}
#elif DOF_GATHER
[[vk::binding(0, 0)]] Texture2D<float4> Half;
[[vk::binding(1, 0)]] SamplerState LinearClamp;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Blurred;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.z || id.y >= Constants.Size.w)
	{
		return;
	}
	const float2 size = float2(Constants.Size.zw);
	const float2 uv = (float2(id.xy) + 0.5) / size;
	const float4 centre = Half.SampleLevel(LinearClamp, uv, 0.0);
	const float centreCoc = abs(centre.a);
	const float maxRadius = Constants.Blur.x * 0.5; // Half-resolution pixels.
	const uint count = clamp(Constants.Params.x, 8u, 128u);
	const float squeeze = max(Constants.Blur.z, 1.0);
	// Per-pixel rotation of the spiral hides its structure (TAA is already behind us, so it is small).
	const float rotation = FeatureGradientNoise(float2(id.xy), 0u) * 6.2831853;

	float4 sum = float4(centre.rgb, 1.0) * (1.0 / max(centreCoc * centreCoc, 1.0));
	float nearWeight = 0.0;
	float nearTotal = 0.0;
	for (uint i = 1; i < count; ++i)
	{
		const float fraction = sqrt((float(i) + 0.5) / float(count));
		const float radius = fraction * maxRadius;
		const float angle = float(i) * 2.39996323 + rotation;
		const float2 offset = float2(cos(angle), sin(angle) * squeeze) * radius;
		const float4 s = Half.SampleLevel(LinearClamp, uv + offset / size, 0.0);
		const float distance = length(offset / float2(1.0, squeeze)); // In the disc's own (unsqueezed) units.
		// Behind the pixel (larger signed CoC, farther): at most the pixel's own blur.
		const float sampleCoc = s.a > centre.a ? min(abs(s.a), centreCoc) : abs(s.a);
		// Does the sample's disc reach this pixel? (1 px soft edge.) Energy-conserving: a
		// sample spreads over pi * coc^2 pixels.
		const float reach = saturate(sampleCoc - distance + 1.0);
		const float w = reach / max(sampleCoc * sampleCoc, 1.0);
		sum += float4(s.rgb, 1.0) * w;
		if (s.a < 0.0)
		{
			nearWeight += reach;
		}
		nearTotal += 1.0;
	}
	Blurred[id.xy] = float4(sum.rgb / max(sum.a, 1.0e-5), saturate(2.0 * nearWeight / max(nearTotal, 1.0)));
}
#else
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float4> Half;
[[vk::binding(2, 0)]] Texture2D<float4> Blurred;
[[vk::binding(3, 0)]] SamplerState LinearClamp;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.x || id.y >= Constants.Size.y)
	{
		return;
	}
	const float2 uv = (float2(id.xy) + 0.5) / float2(Constants.Size.xy);
	const float3 sharp = Color.Load(int3(id.xy, 0)).rgb;
	const float coc = abs(Half.SampleLevel(LinearClamp, uv, 0.0).a) * 2.0; // Full-resolution pixels.
	const float4 blurred = Blurred.SampleLevel(LinearClamp, uv, 0.0);
	// Below ~1 px of CoC the pixel is in focus; the foreground's coverage spills blur over it.
	const float amount = max(saturate(coc - 0.5), blurred.a);
	Output[id.xy] = float4(lerp(sharp, blurred.rgb, amount), 1.0);
}
#endif
