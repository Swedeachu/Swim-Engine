// Screen-space composite (item 76): ScreenSpace::CompositeTexel for every pixel, AO on
// the indirect radiance, reflections in place of the specular IBL, then fog. Bindings
// follow ScreenSpaceCompositeBindings.
//
// Reflections are a hierarchy: the screen-space hit where one was found (weighted by its
// confidence), else the local reflection probes around the surface (ReflectionProbes::
// Select, with parallax correction from the captured distances), else the global
// environment the forward pass already wrote (Specular). Each layer replaces the next
// by its weight, so the transitions are blends, not switches.
#include "ScreenSpace/ScreenSpaceRecords.slang"

[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float4> Indirect;
[[vk::binding(2, 0)]] Texture2D<float> Ao;
[[vk::binding(3, 0)]] Texture2D<float> Depth;
[[vk::binding(4, 0)]] StructuredBuffer<ScreenSpaceParams> Params;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;
[[vk::binding(6, 0)]] Texture2D<float4> Reflection;
[[vk::binding(7, 0)]] Texture2D<float4> Reflectance;
[[vk::binding(8, 0)]] Texture2D<float4> Specular;
[[vk::binding(9, 0)]] Texture2D<float4> Normal;
[[vk::binding(10, 0)]] TextureCubeArray<float4> ProbeCubes;
[[vk::binding(11, 0)]] SamplerState ProbeSampler;

struct ReflectionProbeRecord
{
	float4 PositionRadius; // xyz: capture position; w: influence radius.
	float4 Params;		   // x: blend distance; y: slot; z: age (s); w: owner ObjectId + 1 (0: area probe).
};

[[vk::binding(12, 0)]] StructuredBuffer<ReflectionProbeRecord> ProbeRecords;
[[vk::binding(13, 0)]] Texture2D<float> ObjectId;
// The reflection term for the temporal filter (ReflectionTemporal != 0; a 1x1 stand-in
// otherwise): rgb = what replaced the specular IBL this frame, a = the weight it entered the
// colour with (AO times the fog transmittance); ScreenSpaceReflectionTemporal.slang filters it.
[[vk::binding(14, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> ReflectionTermOut;

// ReflectionProbes::ParallaxSteps / ParallaxRefineSteps / ParallaxReach.
static const uint ProbeParallaxSteps = 96;
static const float ProbeParallaxTexels = 3.0;
static const float ProbeParallaxMinStep = 0.01;
static const uint ProbeParallaxRefineSteps = 5;
static const float ProbeParallaxReach = 40.0;

// ReflectionProbes::Select.
struct ProbeSelection
{
	int First;
	int Second;
	float FirstShare;
	float SecondShare;
	float Coverage;
};

ProbeSelection SelectProbes(uint count, float3 position, float pixelObject)
{
	ProbeSelection selection;
	selection.First = -1;
	selection.Second = -1;
	selection.FirstShare = 0.0;
	selection.SecondShare = 0.0;
	selection.Coverage = 0.0;
	float firstWeight = 0.0;
	float secondWeight = 0.0;
	float firstShare = 0.0;
	float secondShare = 0.0;
	for (uint i = 0; i < count; ++i)
	{
		const ReflectionProbeRecord record = ProbeRecords[i];
		const float owner = record.Params.w;
		if (owner != 0.0)
		{
			if (owner == pixelObject)
			{
				selection.First = int(i);
				selection.Second = -1;
				selection.FirstShare = 1.0;
				selection.SecondShare = 0.0;
				selection.Coverage = 1.0;
				return selection;
			}
			continue;
		}
		const float radius = record.PositionRadius.w;
		const float d = length(position - record.PositionRadius.xyz);
		const float weight = saturate((radius - d) / max(record.Params.x, 1.0e-3));
		if (!(weight > 0.0))
		{
			continue;
		}
		const float share = weight / (d / max(radius, 1.0e-3) + 0.05);
		if (weight > firstWeight || (weight == firstWeight && share > firstShare))
		{
			selection.Second = selection.First;
			secondWeight = firstWeight;
			secondShare = firstShare;
			selection.First = int(i);
			firstWeight = weight;
			firstShare = share;
		}
		else if (weight > secondWeight)
		{
			selection.Second = int(i);
			secondWeight = weight;
			secondShare = share;
		}
	}
	if (selection.First < 0)
	{
		return selection;
	}
	const float total = firstShare + (selection.Second >= 0 ? secondShare : 0.0);
	selection.FirstShare = total > 0.0 ? firstShare / total : 1.0;
	selection.SecondShare = selection.Second >= 0 && total > 0.0 ? secondShare / total : 0.0;
	selection.Coverage = firstWeight;
	return selection;
}

// How far offset + direction * t lies beyond the captured surface in its direction (>= 0:
// on or beyond); `length` receives its distance from the probe.
float ProbeExcess(float slot, float3 offset, float3 direction, float t, out float length)
{
	const float3 x = offset + direction * t;
	length = sqrt(dot(x, x));
	return length > 0.0 ? length - ProbeCubes.SampleLevel(ProbeSampler, float4(x / length, slot), 0.0).a : -1.0;
}

// ReflectionProbes::ParallaxDirection (with ParallaxThickness), then the prefiltered radiance there.
// Alpha: 1 where the ray found the surface it hits (or passed in front of everything the probe
// captured), 0 where it ended behind a nearer surface as the probe sees it (diagnostics).
float4 SampleProbe(ReflectionProbeRecord record, float3 position, float3 direction, float lod, float texelAngle)
{
	const float slot = record.Params.y;
	const float3 offset = position - record.PositionRadius.xyz;
	float3 found = direction;
	float t = 0.0;
	bool wasBeyond = false;
	bool resolved = false;
	for (uint i = 0; i < ProbeParallaxSteps; ++i)
	{
		// Texel-adaptive step: the sample's direction from the probe turns by about
		// ProbeParallaxTexels texels per step (fast where the ray passes close to the probe,
		// long where it heads away from it), so no occluder or edge is stepped over.
		const float3 x = offset + direction * t;
		const float distance = length(x);
		const float3 radial = distance > 1.0e-5 ? x / distance : direction;
		const float across = length(direction - radial * dot(direction, radial));
		const float dt = clamp(max(ProbeParallaxTexels * texelAngle, 0.02) * distance / max(across, 0.02), ProbeParallaxMinStep, ProbeParallaxReach * 0.125);
		const float next = t + dt;
		if (next > ProbeParallaxReach)
		{
			break;
		}
		float length;
		const bool beyond = ProbeExcess(slot, offset, direction, next, length) >= 0.0;
		if (!beyond || wasBeyond)
		{
			wasBeyond = beyond;
			t = next;
			continue;
		}
		float lo = t;
		float hi = next;
		for (uint k = 0; k < ProbeParallaxRefineSteps; ++k)
		{
			const float mid = 0.5 * (lo + hi);
			float midLength;
			if (ProbeExcess(slot, offset, direction, mid, midLength) >= 0.0)
			{
				hi = mid;
			}
			else
			{
				lo = mid;
			}
		}
		float hitLength;
		const float over = ProbeExcess(slot, offset, direction, hi, hitLength);
		if (over <= 0.08 * hitLength + 2.0 * (hi - lo) + 0.02)
		{
			const float3 hit = offset + direction * hi;
			found = hitLength > 0.0 ? hit / hitLength : direction;
			resolved = true;
			break;
		}
		// Behind an occluder (as the probe sees it): march on.
		wasBeyond = true;
		t = next;
	}
	const float confidence = resolved || !wasBeyond ? 1.0 : 0.0;
	return float4(max(ProbeCubes.SampleLevel(ProbeSampler, float4(found, slot), lod).rgb, float3(0.0, 0.0, 0.0)), confidence);
}

float3 DebugHue(float h)
{
	return saturate(abs(frac(h + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
}

// ScreenSpace::GlossyBlurScale / GlossyBlurMax / GlossyTapWeights.
static const float GlossyBlurScale = 20.0;
static const float GlossyBlurMax = 8.0;
static const float GlossyTapWeights[3] = { 1.0, 0.6065307, 0.1353353 };

// ScreenSpace::ResolvedReflectionTexel: a 5 x 5 Gaussian over taps on the same surface,
// its step growing with the roughness.
// Half-resolution reflections (SsrHalf): the same filter over the traced texels around the
// pixel, each weighted by where its traced pixel lies (a Gaussian in full-resolution pixels)
// and kept only on the pixel's own surface (depth and normal as above). 3 x 3 texels for
// sharp surfaces, 5 x 5 for rough ones.
float4 ResolvedHalfReflection(ScreenSpaceView view, int x, int y)
{
	const int2 halfSize = int2((int(view.Width) + 1) / 2, (int(view.Height) + 1) / 2);
	const int2 centreTexel = int2(x, y) / 2;
	const int2 offset = int2(ScreenSpaceHalfOffset(Params[0].NoiseFrame));
	const float4 encoded = Normal.Load(int3(x, y, 0));
	bool valid;
	const float3 position = ScreenSpaceLoadViewPosition(view, Depth, x, y, valid);
	if (!valid)
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	const float r = clamp(encoded.w, 0.0, 1.0);
	const float radius = min(GlossyBlurScale * r * sqrt(r), GlossyBlurMax) * (float(view.Height) / 1080.0);
	const float sigma = max(1.0, floor(radius * 0.5 + 0.5));
	const float inverseTwoSigma2 = 0.5 / (sigma * sigma);
	const int reach = sigma > 1.5 ? 2 : 1;
	const float centreDepth = -position.z;
	float3 radiance = float3(0.0, 0.0, 0.0);
	float radianceWeight = 0.0;
	float confidence = 0.0;
	float weights = 0.0;
	for (int oy = -reach; oy <= reach; ++oy)
	{
		for (int ox = -reach; ox <= reach; ++ox)
		{
			const int2 texel = clamp(centreTexel + int2(ox, oy) * int(max(1.0, floor(sigma * 0.5 + 0.5))), int2(0, 0), halfSize - 1);
			const int2 source = min(texel * 2 + offset, int2(int(view.Width), int(view.Height)) - 1);
			bool tapValid;
			const float3 tapPosition = ScreenSpaceLoadViewPosition(view, Depth, source.x, source.y, tapValid);
			if (!tapValid || abs(-tapPosition.z - centreDepth) > 0.05 * centreDepth)
			{
				continue;
			}
			if (dot(encoded.xyz, Normal.Load(int3(source, 0)).xyz) < 0.9)
			{
				continue;
			}
			const float2 d = float2(source - int2(x, y));
			const float w = exp(-dot(d, d) * inverseTwoSigma2);
			const float4 tap = Reflection.Load(int3(texel, 0));
			const float tapConfidence = max(tap.a, 0.0); // ScreenSpace::RejectedHit counts as a miss.
			confidence += w * tapConfidence;
			weights += w;
			radiance += w * tapConfidence * tap.rgb;
			radianceWeight += w * tapConfidence;
		}
	}
	if (!(weights > 0.0))
	{
		return float4(0.0, 0.0, 0.0, 0.0); // No traced texel on this surface nearby: the fallback.
	}
	return float4(radianceWeight > 0.0 ? radiance / radianceWeight : float3(0.0, 0.0, 0.0), confidence / weights);
}

float4 ResolvedReflection(ScreenSpaceView view, int x, int y)
{
	if (Params[0].SsrHalf != 0u)
	{
		return ResolvedHalfReflection(view, x, y);
	}
	const float4 centre = Reflection.Load(int3(x, y, 0));
	const float4 encoded = Normal.Load(int3(x, y, 0));
	bool valid;
	const float3 position = ScreenSpaceLoadViewPosition(view, Depth, x, y, valid);
	if (!valid)
	{
		return centre;
	}
	const float r = clamp(encoded.w, 0.0, 1.0);
	const float radius = min(GlossyBlurScale * r * sqrt(r), GlossyBlurMax) * (float(view.Height) / 1080.0);
	const int step = max(1, int(floor(radius * 0.5 + 0.5)));
	const float centreDepth = -position.z;
	float3 radiance = float3(0.0, 0.0, 0.0);
	float radianceWeight = 0.0;
	float confidence = 0.0;
	float weights = 0.0;
	for (int j = 0; j < 25; ++j)
	{
		const int ox = j % 5 - 2;
		const int oy = j / 5 - 2;
		const int tx = clamp(x + ox * step, 0, int(view.Width) - 1);
		const int ty = clamp(y + oy * step, 0, int(view.Height) - 1);
		bool tapValid;
		const float3 tapPosition = ScreenSpaceLoadViewPosition(view, Depth, tx, ty, tapValid);
		if (!tapValid || abs(-tapPosition.z - centreDepth) > 0.05 * centreDepth)
		{
			continue;
		}
		if (dot(encoded.xyz, Normal.Load(int3(tx, ty, 0)).xyz) < 0.9)
		{
			continue;
		}
		const float w = GlossyTapWeights[abs(ox)] * GlossyTapWeights[abs(oy)];
		const float4 tap = Reflection.Load(int3(tx, ty, 0));
		const float tapConfidence = max(tap.a, 0.0); // ScreenSpace::RejectedHit counts as a miss.
		confidence += w * tapConfidence;
		weights += w;
		radiance += w * tapConfidence * tap.rgb;
		radianceWeight += w * tapConfidence;
	}
	if (!(weights > 0.0))
	{
		return centre;
	}
	return float4(radianceWeight > 0.0 ? radiance / radianceWeight : float3(0.0, 0.0, 0.0), confidence / weights);
}

struct FogParams
{
	float3 Color;
	float Density;
	float3 SunColor;
	float HeightFalloff;
	float3 SunDirection;
	float BaseHeight;
	float Anisotropy;
	float StartDistance;
	float MaxDistance;
};

// ScreenSpace::FogOpticalDepth.
float FogOpticalDepth(FogParams fog, float3 camera, float3 direction, float fogDistance)
{
	if (!(fog.Density > 0.0) || !(fogDistance > fog.StartDistance))
	{
		return 0.0;
	}
	const float startHeight = camera.y + direction.y * fog.StartDistance;
	const float pathLength = fogDistance - fog.StartDistance;
	const float density = fog.Density * exp(clamp(-fog.HeightFalloff * (startHeight - fog.BaseHeight), -80.0, 80.0));
	const float k = fog.HeightFalloff * direction.y * pathLength;
	const float shape = abs(k) < 1.0e-4 ? 1.0 - 0.5 * k + k * k / 6.0 : (1.0 - exp(clamp(-k, -80.0, 80.0))) / k;
	return density * pathLength * shape;
}

// ScreenSpace::HenyeyGreenstein.
float HenyeyGreenstein(float g, float cosTheta)
{
	const float denominator = max(1.0 + g * g - 2.0 * g * cosTheta, 1.0e-6);
	return (1.0 - g * g) / (denominator * sqrt(denominator));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ScreenSpaceView view = ScreenSpaceLoadView(Params);
	if (id.x >= view.Width || id.y >= view.Height)
	{
		return;
	}
	const int3 at = int3(int(id.x), int(id.y), 0);
	const float4 color = Color.Load(at);
	float3 c = color.rgb;
	const bool aoEnabled = Params[0].AoEnabled != 0u;
	const float ao = aoEnabled ? Ao.Load(at) : 1.0;
	if (aoEnabled)
	{
		c = max(c - (1.0 - ao) * Indirect.Load(at).rgb, float3(0.0, 0.0, 0.0));
	}
	const uint debug = Params[0].ReflectionDebug;
	float3 debugColor = float3(0.0, 0.0, 0.0);
	bool reflective = false;
	const bool ssrEnabled = Params[0].SsrEnabled != 0u;
	float4 reflectionTerm = float4(0.0, 0.0, 0.0, 0.0); // Weight 0: nothing to filter.
	if (ssrEnabled || Params[0].ProbeCount > 0u)
	{
		const float3 reflectance = Reflectance.Load(at).rgb;
		const float3 specular = Specular.Load(at).rgb;
		reflective = dot(reflectance, float3(0.2126, 0.7152, 0.0722)) > 0.02;
		// The screen-space reflection first: where it is (almost) fully confident the fallback
		// (all but cancelled out of the result) skips its probe lookups (the parallax marches);
		// the remaining few percent come from the global environment instead.
		const float4 reflection = ssrEnabled ? ResolvedReflection(view, int(id.x), int(id.y)) : float4(0.0, 0.0, 0.0, 0.0);
		const float confidence = max(reflection.a, 0.0);
		// The fallback: local probes over the global environment (Specular).
		float3 fallback = specular;
		float probeCoverage = 0.0;
		float probeAge = 0.0;
		int probeSlot = -1;
		const uint probeCount = Params[0].ProbeCount;
		const float depth = Depth.Load(at);
		if (probeCount > 0u && reflective && depth > 0.0 && (confidence < 0.97 || debug != 0u))
		{
			bool valid;
			const float3 viewPosition = ScreenSpaceViewPosition(view, float(id.x) + 0.5, float(id.y) + 0.5, depth, valid);
			const float4 encoded = Normal.Load(at);
			if (valid && dot(encoded.xyz, encoded.xyz) > 1.0e-6)
			{
				float4 inverseView[3];
				for (uint row = 0; row < 3; ++row)
				{
					inverseView[row] = Params[0].InverseViewRows[row];
				}
				const float4 p = float4(viewPosition, 1.0);
				const float3 world = float3(dot(inverseView[0], p), dot(inverseView[1], p), dot(inverseView[2], p));
				const float3 camera = float3(inverseView[0].w, inverseView[1].w, inverseView[2].w);
				const float3 n = normalize(encoded.xyz);
				const float3 v = normalize(world - camera);
				const float3 r = reflect(v, n);
				const ProbeSelection selection = SelectProbes(probeCount, world, ObjectId.Load(at));
				if (selection.First >= 0)
				{
					const float lod = saturate(encoded.w) * float(max(Params[0].ProbeMipCount, 1u) - 1u);
					const ReflectionProbeRecord first = ProbeRecords[selection.First];
					// The probe atlas's texel angle: faces are 4 << (mips - 1) texels over 90 degrees.
					const float texelAngle = 1.5707963 / float(4u << (max(Params[0].ProbeMipCount, 1u) - 1u));
					const float4 firstSample = SampleProbe(first, world, r, lod, texelAngle);
					float3 probe = selection.FirstShare * firstSample.rgb;

					if (selection.Second >= 0)
					{
						const float4 secondSample = SampleProbe(ProbeRecords[selection.Second], world, r, lod, texelAngle);
						probe += selection.SecondShare * secondSample.rgb;
					}
					probeCoverage = selection.Coverage;
					probeAge = first.Params.z;
					probeSlot = int(first.Params.y);
					fallback = lerp(specular, reflectance * probe, probeCoverage);
				}
			}
		}
		// The reflection term: the screen-space hit over the fallback by its confidence, then
		// (optionally) filtered over time, replacing the specular IBL Forward+ added.
		const float3 reflected = confidence > 0.0 ? lerp(fallback, reflectance * reflection.rgb, confidence) : fallback;
		c = max(c + ao * (reflected - specular), float3(0.0, 0.0, 0.0));
		if (debug == 0u)
		{
			reflectionTerm = float4(reflected, ao);
		}
		if (debug == 1u)
		{
			debugColor = float3(confidence, (1.0 - confidence) * probeCoverage, (1.0 - confidence) * (1.0 - probeCoverage));
			if (ssrEnabled && Reflection.Load(Params[0].SsrHalf != 0u ? int3(at.xy / 2, 0) : at).a < 0.0)
			{
				debugColor = lerp(debugColor, float3(1.0, 0.0, 1.0), 0.6);
			}
		}
		else if (debug == 2u)
		{
			// The probe (hue by slot, scaled by its coverage), darkening as it ages (4 s: a quarter).
			debugColor = probeSlot >= 0 ? DebugHue(float(probeSlot) * 0.13) * probeCoverage * lerp(1.0, 0.25, saturate(probeAge / 4.0))
										: float3(0.02, 0.02, 0.25);
		}
	}
	if (Params[0].FogEnabled != 0u)
	{
		float4 inverseView[3];
		for (uint row = 0; row < 3; ++row)
		{
			inverseView[row] = Params[0].InverseViewRows[row];
		}
		FogParams fog;
		fog.Color = Params[0].FogColor;
		fog.Density = Params[0].FogDensity;
		fog.SunColor = Params[0].FogSunColor;
		fog.HeightFalloff = Params[0].FogHeightFalloff;
		fog.SunDirection = Params[0].FogSunDirection;
		fog.BaseHeight = Params[0].FogBaseHeight;
		fog.Anisotropy = Params[0].FogAnisotropy;
		fog.StartDistance = Params[0].FogStartDistance;
		fog.MaxDistance = Params[0].FogMaxDistance;
		const float3 camera = float3(inverseView[0].w, inverseView[1].w, inverseView[2].w);
		const float depth = Depth.Load(at);
		// The sky is fogged along its view ray (through the near plane) at the max distance.
		bool valid;
		const float3 viewPosition = ScreenSpaceViewPosition(view, float(id.x) + 0.5, float(id.y) + 0.5, depth > 0.0 ? depth : 1.0, valid);
		if (valid)
		{
			const float4 p = float4(viewPosition, 1.0);
			const float3 delta = float3(dot(inverseView[0], p), dot(inverseView[1], p), dot(inverseView[2], p)) - camera;
			const float distanceToPoint = length(delta);
			if (distanceToPoint > 1.0e-6)
			{
				const float3 direction = delta * (1.0 / distanceToPoint);
				const float fogDistance = depth > 0.0 ? min(distanceToPoint, fog.MaxDistance) : fog.MaxDistance;
				const float transmittance = exp(-FogOpticalDepth(fog, camera, direction, fogDistance));
				const float3 inscatter = fog.Color + fog.SunColor * HenyeyGreenstein(fog.Anisotropy, -dot(fog.SunDirection, direction));
				c = c * transmittance + inscatter * (1.0 - transmittance);
				reflectionTerm.a *= transmittance;
			}
		}
	}
	if (debug != 0u)
	{
		const float grey = dot(c, float3(0.2126, 0.7152, 0.0722));
		c = reflective ? debugColor : float3(grey, grey, grey) * 0.15;
	}
	Output[id.xy] = float4(c, color.a);
	if (Params[0].ReflectionTemporal != 0u)
	{
		ReflectionTermOut[id.xy] = reflectionTerm;
	}
}
