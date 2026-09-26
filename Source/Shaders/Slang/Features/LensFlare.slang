// Lens flare (Engine::LensFlare): an analytic flare of the sun, added to the HDR scene
// color before exposure and bloom. The sun's visibility is measured in the frame itself:
// a ring of taps around its screen position counts how many see bright sky (geometry
// and clouds in front of the sun dim the flare), so no occlusion query is needed.
// Elements: a glare with a starburst around the sun, chromatic ghosts mirrored through
// the screen center, and a halo ring.
#include "Features/FeatureCommon.slang"

struct LensFlareConstants
{
	float4 SunUv;	  // xy: sun screen uv; z: aspect (width / height); w: on-screen fade.
	float4 SunColor;  // Radiance; w: intensity.
	float4 Ghosts;	  // x: ghost count; y: ghost spacing; z: ghost intensity; w: halo radius.
	float4 Glare;	  // x: glare size; y: starburst streaks; z: starburst intensity; w: halo intensity.
	float4 Params;	  // x: brightness threshold (luminance); y: occlusion tap radius (uv); z: chromatic spread; w: time.
	uint4 Size;		  // xy: output size.
};

[[vk::push_constant]] ConstantBuffer<LensFlareConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

float SunVisibility()
{
	const float2 size = float2(Constants.Size.xy);
	const float threshold = max(Constants.Params.x, 1.0e-3);
	float visible = 0.0;
	static const uint Taps = 12;
	for (uint i = 0; i < Taps; ++i)
	{
		const float angle = float(i) * (6.2831853 / float(Taps));
		const float radius = Constants.Params.y * (i % 2u == 0u ? 0.35 : 1.0);
		const float2 uv = Constants.SunUv.xy + float2(cos(angle) / Constants.SunUv.z, sin(angle)) * radius;
		if (any(uv < 0.0) || any(uv > 1.0))
		{
			continue;
		}
		const int2 pixel = int2(uv * size);
		const bool sky = Depth.Load(int3(pixel, 0)) <= 0.0;
		const float bright = saturate((FeatureLuminance(Color.Load(int3(pixel, 0)).rgb) - threshold) / threshold);
		visible += sky ? bright : 0.0;
	}
	return visible / float(Taps);
}

float Disc(float2 delta, float radius, float softness)
{
	return 1.0 - smoothstep(radius * (1.0 - softness), radius, length(delta));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size.x || id.y >= Constants.Size.y)
	{
		return;
	}
	const float4 scene = Color.Load(int3(id.xy, 0));
	const float visibility = SunVisibility() * Constants.SunUv.w;
	if (visibility <= 0.0)
	{
		Output[id.xy] = scene;
		return;
	}
	const float aspect = Constants.SunUv.z;
	const float2 uv = (float2(id.xy) + 0.5) / float2(Constants.Size.xy);
	const float2 sun = Constants.SunUv.xy;
	const float2 toSun = (uv - sun) * float2(aspect, 1.0);
	const float distance = length(toSun);

	// Glare and starburst around the sun.
	const float glare = exp(-distance / max(Constants.Glare.x, 1.0e-4));
	const float angle = atan2(toSun.y, toSun.x);
	const float streaks = max(Constants.Glare.y, 1.0);
	const float burst = pow(abs(cos(angle * streaks * 0.5 + Constants.Params.w * 0.05)), 24.0) * exp(-distance * 6.0);
	float3 flare = float3(1.0, 0.95, 0.85) * (glare * 0.6 + burst * Constants.Glare.z);

	// Ghosts along the line from the sun through the screen center, each with a tint and
	// a slight chromatic offset.
	const float2 axis = float2(0.5, 0.5) - sun;
	const uint ghosts = clamp(uint(Constants.Ghosts.x), 0u, 8u);
	static const float3 Tints[8] = { float3(0.9, 0.5, 0.2), float3(0.3, 0.8, 0.9), float3(0.6, 0.3, 0.9), float3(0.3, 0.9, 0.4),
		float3(1.0, 0.8, 0.3), float3(0.4, 0.5, 1.0), float3(0.9, 0.4, 0.6), float3(0.5, 0.9, 0.9) };
	static const float Radii[8] = { 0.05, 0.03, 0.08, 0.02, 0.12, 0.04, 0.06, 0.025 };
	for (uint i = 0; i < ghosts; ++i)
	{
		const float t = 0.4 + float(i) * Constants.Ghosts.y;
		const float2 center = sun + axis * 2.0 * t;
		const float2 delta = (uv - center) * float2(aspect, 1.0);
		const float spread = Constants.Params.z;
		const float3 ring = float3(Disc(delta * (1.0 + spread), Radii[i], 0.6), Disc(delta, Radii[i], 0.6), Disc(delta * (1.0 - spread), Radii[i], 0.6));
		flare += ring * Tints[i] * Constants.Ghosts.z;
	}

	// A halo ring around the screen center, opposite the sun.
	const float2 fromCenter = (uv - 0.5) * float2(aspect, 1.0);
	const float halo = exp(-pow((length(fromCenter) - Constants.Ghosts.w) * 18.0, 2.0)) * saturate(1.0 - length(axis) * 1.2);
	flare += float3(0.7, 0.8, 1.0) * halo * Constants.Glare.w;

	const float3 light = Constants.SunColor.rgb / max(FeatureLuminance(Constants.SunColor.rgb), 1.0e-3);
	Output[id.xy] = float4(scene.rgb + flare * light * (Constants.SunColor.w * visibility), scene.a);
}
