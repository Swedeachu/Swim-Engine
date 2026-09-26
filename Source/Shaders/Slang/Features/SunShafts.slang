// Sun shafts (god rays), two programs from one source (Engine::SunShafts):
//  - SunShaftsMask (SUN_SHAFTS_COMPOSITE=0): a reduced-resolution occlusion mask: sky
//    pixels near the sun, weighted by how much brighter than the sky they are (clouds
//    and geometry in front of the sun dim it);
//  - SunShaftsComposite (SUN_SHAFTS_COMPOSITE=1): a radial blur of the mask toward the
//    sun's screen position (Mitchell, GPU Gems 3 ch. 13), added to the HDR scene color.
#include "Features/FeatureCommon.slang"

#ifndef SUN_SHAFTS_COMPOSITE
#define SUN_SHAFTS_COMPOSITE 0
#endif

struct SunShaftConstants
{
	float4 Forward;		 // w: tan(half horizontal fov).
	float4 Right;		 // w: tan(half vertical fov).
	float4 Up;			 // w: cosine of the mask cone around the sun.
	float4 SunDirection; // Toward the sun; w: on-screen fade (0..1).
	float4 SunColor;	 // Radiance; w: intensity.
	float4 SunUv;		 // xy: sun screen uv; z: density (blur length); w: decay per sample.
	float4 Params;		 // x: sample weight; y: sample count; z: brightness threshold (luminance); w: unused.
	uint4 Size;			 // xy: output size; zw: mask size.
};

[[vk::push_constant]] ConstantBuffer<SunShaftConstants> Constants;

#if !SUN_SHAFTS_COMPOSITE
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Mask;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.z || id.y >= Constants.Size.w)
	{
		return;
	}
	const float2 uv = (float2(id.xy) + 0.5) / float2(Constants.Size.zw);
	const int2 pixel = int2(uv * float2(Constants.Size.xy));
	const bool sky = Depth.Load(int3(pixel, 0)) <= 0.0;
	float mask = 0.0;
	if (sky)
	{
		const float3 ray = FeatureViewRay(uv, Constants.Forward, Constants.Right, Constants.Up);
		const float cosine = dot(ray, Constants.SunDirection.xyz);
		const float cone = saturate((cosine - Constants.Up.w) / max(1.0 - Constants.Up.w, 1.0e-4));
		const float threshold = max(Constants.Params.z, 1.0e-3);
		const float bright = saturate((FeatureLuminance(Color.Load(int3(pixel, 0)).rgb) - threshold) / threshold);
		mask = cone * cone * bright;
	}
	Mask[id.xy] = float4(mask, mask, mask, 1.0);
}
#else
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float4> Mask;
[[vk::binding(2, 0)]] SamplerState LinearClamp;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.x || id.y >= Constants.Size.y)
	{
		return;
	}
	const float2 uv = (float2(id.xy) + 0.5) / float2(Constants.Size.xy);
	const uint samples = clamp(uint(Constants.Params.y), 1u, 128u);
	const float2 step = (uv - Constants.SunUv.xy) * (Constants.SunUv.z / float(samples));
	float2 at = uv;
	float illumination = 1.0;
	float sum = 0.0;
	for (uint i = 0; i < samples; ++i)
	{
		at -= step;
		sum += Mask.SampleLevel(LinearClamp, at, 0.0).r * illumination * Constants.Params.x;
		illumination *= Constants.SunUv.w;
	}
	const float4 scene = Color.Load(int3(id.xy, 0));
	const float3 shafts = sum * Constants.SunColor.rgb * (Constants.SunColor.w * Constants.SunDirection.w);
	Output[id.xy] = float4(scene.rgb + shafts, scene.a);
}
#endif
