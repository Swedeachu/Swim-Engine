// Screen-space composite (item 76): ScreenSpace::CompositeTexel for every pixel, AO on
// the indirect radiance, reflections in place of the specular IBL, then fog. Bindings
// follow ScreenSpaceCompositeBindings.
//
// Reflections are a hierarchy: the planar reflection of a mirror that has one (exact, from a
// capture through the mirror), the screen-space hit where one was found (weighted by its
// confidence; over a planar reflection only for reflectors small on screen), else the
// local reflection probes around the surface (ReflectionProbes::Select, with parallax
// correction from the captured distances), else the global environment the forward pass
// already wrote (Specular). Each layer replaces the next by its weight, so the transitions
// are blends, not switches.
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

// Planar reflections (PlanarCount > 0; 1x1 stand-ins otherwise): PlanarReflectionRenderer's
// atlas (a layer per capture; alpha: the distance from the capture position) and the active
// GpuPlanarReflectionRecords, largest reflector first.
struct PlanarReflectionRecord
{
	float4 ViewProjection[4]; // World -> capture clip rows.
	float4 Plane;			  // xyz: normal; w: n . p of the plane.
	float4 Camera;			  // xyz: capture position; w: owner ObjectId + 1 (0: any surface on the plane).
	float4 Atlas;			  // x: layer; y, z: the capture's share of the layer; w: SSR preference.
	float4 Params;			  // x: plane tolerance; y: min normal cosine; z, w: roughness fade start, end.
};

[[vk::binding(15, 0)]] Texture2DArray<float4> PlanarAtlas;
[[vk::binding(16, 0)]] StructuredBuffer<PlanarReflectionRecord> PlanarRecords;

// PlanarReflections::LookupIterations.
static const uint PlanarLookupIterations = 2;

// PlanarReflections::ProjectToCapture: (u, v) and clip w (> 0 in front).
float3 PlanarProject(PlanarReflectionRecord record, float3 world)
{
	const float4 p = float4(world, 1.0);
	const float x = dot(record.ViewProjection[0], p);
	const float y = dot(record.ViewProjection[1], p);
	const float w = dot(record.ViewProjection[3], p);
	if (!(w > 1.0e-6))
	{
		return float3(-1.0, -1.0, w);
	}
	return float3(x / w * 0.5 + 0.5, 0.5 - y / w * 0.5, w);
}

// The capture's share of its layer, clamped half a texel inside (bilinear never reads outside).
float4 PlanarFetch(PlanarReflectionRecord record, float2 uv, float2 atlasSize)
{
	const float2 share = record.Atlas.yz;
	const float2 halfTexel = 0.5 / atlasSize;
	const float2 at = clamp(uv * share, halfTexel, share - halfTexel);
	return PlanarAtlas.SampleLevel(ProbeSampler, float3(at, record.Atlas.x), 0.0);
}

// The capture's colour through a Catmull-Rom filter (9 bilinear taps): captures are rendered
// below screen resolution (PlanarReflectionSettings::ResolutionScale) and magnified, and the
// bicubic keeps that magnification smooth without the bilinear's blur and blockiness.
float3 PlanarFetchSmooth(PlanarReflectionRecord record, float2 uv, float2 atlasSize)
{
	const float2 share = record.Atlas.yz;
	const float2 halfTexel = 0.5 / atlasSize;
	const float2 samplePosition = uv * share * atlasSize;
	const float2 texel1 = floor(samplePosition - 0.5) + 0.5;
	const float2 f = samplePosition - texel1;
	const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	const float2 w3 = f * f * (-0.5 + 0.5 * f);
	const float2 w12 = w1 + w2;
	const float2 texel0 = (texel1 - 1.0) / atlasSize;
	const float2 texel3 = (texel1 + 2.0) / atlasSize;
	const float2 texel12 = (texel1 + w2 / w12) / atlasSize;
	const float2 positions[3] = { clamp(texel0, halfTexel, share - halfTexel), clamp(texel12, halfTexel, share - halfTexel),
		clamp(texel3, halfTexel, share - halfTexel) };
	const float2 weights[3] = { w0, w12, w3 };
	float3 sum = float3(0.0, 0.0, 0.0);
	for (int y = 0; y < 3; ++y)
	{
		for (int x = 0; x < 3; ++x)
		{
			const float w = weights[x].x * weights[y].y;
			sum += w * PlanarAtlas.SampleLevel(ProbeSampler, float3(positions[x].x, positions[y].y, record.Atlas.x), 0.0).rgb;
		}
	}
	return max(sum, float3(0.0, 0.0, 0.0));
}

// PlanarReflections::LookupUv: start where the surface point projects (exact for a current
// mirror capture), then refine the ray length with the captured distances (a camera that
// moved since the capture, curved surfaces). Alpha: the portal edge fade (2 texels).
float4 SamplePlanar(PlanarReflectionRecord record, float3 position, float3 direction, float2 atlasSize)
{
	const float3 camera = record.Camera.xyz;
	const float3 start = PlanarProject(record, position);
	if (!(start.z > 0.0))
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	float2 uv = start.xy;
	float3 hit = camera + normalize(position - camera) * PlanarFetch(record, uv, atlasSize).a;
	float t = max(dot(hit - position, direction), 0.0);
	for (uint i = 0; i < PlanarLookupIterations; ++i)
	{
		const float3 q = position + direction * t;
		const float3 projected = PlanarProject(record, q);
		if (!(projected.z > 0.0))
		{
			break;
		}
		uv = projected.xy;
		hit = camera + normalize(q - camera) * PlanarFetch(record, uv, atlasSize).a;
		t = max(dot(hit - position, direction), 0.0);
	}
	const float3 end = PlanarProject(record, position + direction * t);
	if (end.z > 0.0)
	{
		uv = end.xy;
	}
	const float2 texels = record.Atlas.yz * atlasSize;
	const float2 inside = min(uv, 1.0 - uv) * texels;
	const float edge = saturate(min(inside.x, inside.y) * 0.5);
	return float4(PlanarFetchSmooth(record, uv, atlasSize), edge);
}

// The planar reflection of a pixel: the first record that matches it (by object for sphere
// caps, by plane for flat reflectors), weighted by how well its normal agrees, its roughness
// and the portal edges. ssrPreference: how much a confident screen-space hit may replace it.
float4 PlanarReflection(uint count, float3 world, float3 n, float3 r, float roughness, float pixelObject, out float ssrPreference)
{
	ssrPreference = 1.0;
	uint width;
	uint height;
	uint layers;
	PlanarAtlas.GetDimensions(width, height, layers);
	const float2 atlasSize = float2(float(width), float(height));
	for (uint i = 0; i < count; ++i)
	{
		const PlanarReflectionRecord record = PlanarRecords[i];
		const float owner = record.Camera.w;
		if (owner != 0.0)
		{
			if (owner != pixelObject)
			{
				continue;
			}
		}
		else if (abs(dot(record.Plane.xyz, world) - record.Plane.w) > record.Params.x)
		{
			continue;
		}
		const float facing = dot(n, record.Plane.xyz);
		const float minCosine = record.Params.y;
		if (!(facing > minCosine))
		{
			continue;
		}
		const float normalWeight = saturate((facing - minCosine) / max((1.0 - minCosine) * 0.5, 1.0e-4));
		const float roughnessWeight = 1.0 - smoothstep(record.Params.z, record.Params.w, roughness);
		if (!(normalWeight * roughnessWeight > 0.0))
		{
			continue;
		}
		const float4 sampled = SamplePlanar(record, world, r, atlasSize);
		ssrPreference = record.Atlas.w;
		return float4(sampled.rgb, sampled.a * normalWeight * roughnessWeight);
	}
	return float4(0.0, 0.0, 0.0, 0.0);
}

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
// Checkerboard reflections (SsrHalf): the same filter over the traced texels around the
// pixel (every row traced one pixel in two, alternating: ScreenSpaceCheckerColumn), each
// weighted by where its traced pixel lies (a Gaussian in full-resolution pixels) and kept
// only on the pixel's own surface (depth and normal as above). The pixel's own trace (when it
// was traced this frame) and its closest neighbours dominate sharp surfaces; rough ones
// reach two texels further.
float4 ResolvedHalfReflection(ScreenSpaceView view, int x, int y)
{
	const int traceWidth = (int(view.Width) + 1) / 2;
	const uint frame = Params[0].NoiseFrame;
	const float4 encoded = Normal.Load(int3(x, y, 0));
	bool valid;
	const float3 position = ScreenSpaceLoadViewPosition(view, Depth, x, y, valid);
	if (!valid)
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	const float r = clamp(encoded.w, 0.0, 1.0);
	const float radius = min(GlossyBlurScale * r * sqrt(r), GlossyBlurMax) * (float(view.Height) / 1080.0);
	const float sigma = max(0.75, floor(radius * 0.5 + 0.5));
	const float inverseTwoSigma2 = 0.5 / (sigma * sigma);
	const int reach = sigma > 1.5 ? 2 : 1;
	const int step = int(max(1.0, floor(sigma * 0.5 + 0.5)));
	const float centreDepth = -position.z;
	float3 radiance = float3(0.0, 0.0, 0.0);
	float radianceWeight = 0.0;
	float confidence = 0.0;
	float weights = 0.0;
	for (int oy = -reach; oy <= reach; ++oy)
	{
		const int row = clamp(y + oy * step, 0, int(view.Height) - 1);
		const int column = int(ScreenSpaceCheckerColumn(frame, uint(row)));
		// The traced texel of this row nearest the pixel, and its neighbours along the row.
		const int nearest = (x - column + 1) / 2;
		for (int ox = -reach; ox <= reach; ++ox)
		{
			const int tx = clamp(nearest + ox * step, 0, traceWidth - 1);
			const int2 source = int2(min(tx * 2 + column, int(view.Width) - 1), row);
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
			const float4 tap = Reflection.Load(int3(tx, row, 0));
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
	const uint planarCount = Params[0].PlanarCount;
	if (ssrEnabled || Params[0].ProbeCount > 0u || planarCount > 0u)
	{
		const float3 reflectance = Reflectance.Load(at).rgb;
		const float3 specular = Specular.Load(at).rgb;
		reflective = dot(reflectance, float3(0.2126, 0.7152, 0.0722)) > 0.02;
		// The screen-space reflection first: where it is (almost) fully confident the fallback
		// (all but cancelled out of the result) skips its probe lookups (the parallax marches);
		// the remaining few percent come from the global environment instead.
		const float4 reflection = ssrEnabled ? ResolvedReflection(view, int(id.x), int(id.y)) : float4(0.0, 0.0, 0.0, 0.0);
		const float confidence = max(reflection.a, 0.0);
		// The surface (shared by the planar lookup and the probes).
		const uint probeCount = Params[0].ProbeCount;
		const float depth = Depth.Load(at);
		const float4 encoded = Normal.Load(at);
		bool surface = false;
		float3 world = float3(0.0, 0.0, 0.0);
		float3 r = float3(0.0, 0.0, 1.0);
		if (reflective && depth > 0.0 && dot(encoded.xyz, encoded.xyz) > 1.0e-6)
		{
			bool valid;
			const float3 viewPosition = ScreenSpaceViewPosition(view, float(id.x) + 0.5, float(id.y) + 0.5, depth, valid);
			if (valid)
			{
				float4 inverseView[3];
				for (uint row = 0; row < 3; ++row)
				{
					inverseView[row] = Params[0].InverseViewRows[row];
				}
				const float4 p = float4(viewPosition, 1.0);
				world = float3(dot(inverseView[0], p), dot(inverseView[1], p), dot(inverseView[2], p));
				const float3 camera = float3(inverseView[0].w, inverseView[1].w, inverseView[2].w);
				r = reflect(normalize(world - camera), normalize(encoded.xyz));
				surface = true;
			}
		}
		// How fast the reflected direction turns per screen pixel: the view ray's step plus twice
		// the normal's (ScreenSpace::NormalTurnPerPixel; curved surfaces).
		float normalTurn = 0.0;
		float reflectionSpread = 0.0;
		if (surface)
		{
			bool centreValid;
			const float centreDepth = -ScreenSpaceViewPosition(view, float(id.x) + 0.5, float(id.y) + 0.5, depth, centreValid).z;
			const int2 offsets[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
			for (int i = 0; i < 4; ++i)
			{
				const int2 there = clamp(int2(id.xy) + offsets[i], int2(0, 0), int2(int(view.Width), int(view.Height)) - 1);
				bool thereValid;
				const float3 position = ScreenSpaceLoadViewPosition(view, Depth, there.x, there.y, thereValid);
				if (thereValid && abs(-position.z - centreDepth) <= 0.05 * centreDepth)
				{
					normalTurn = max(normalTurn, length(Normal.Load(int3(there, 0)).xyz - encoded.xyz));
				}
			}
			const float pixelAngle = 2.0 / max(Params[0].Projection[1].y * float(view.Height), 1.0e-6);
			reflectionSpread = pixelAngle + 2.0 * normalTurn;
		}
		// Planar reflections: exact for the mirrors (and sphere caps) that have one.
		float planarWeight = 0.0;
		float3 planarRadiance = float3(0.0, 0.0, 0.0);
		float ssrPreference = 1.0;
		const float pixelObject = (probeCount > 0u || planarCount > 0u) ? ObjectId.Load(at) : 0.0;
		if (planarCount > 0u && surface)
		{
			const float4 planar = PlanarReflection(planarCount, world, normalize(encoded.xyz), r, saturate(encoded.w), pixelObject, ssrPreference);
			planarWeight = planar.a;
			planarRadiance = planar.rgb;
		}
		// The fallback: local probes over the global environment (Specular).
		float3 fallback = specular;
		float probeCoverage = 0.0;
		float probeAge = 0.0;
		int probeSlot = -1;
		if (probeCount > 0u && surface && (confidence < 0.97 || debug != 0u) && (planarWeight < 0.999 || debug != 0u))
		{
			const ProbeSelection selection = SelectProbes(probeCount, world, pixelObject);
			if (selection.First >= 0)
			{
				const ReflectionProbeRecord first = ProbeRecords[selection.First];
				// The probe atlas's texel angle: faces are 4 << (mips - 1) texels over 90 degrees.
				const float mips = float(max(Params[0].ProbeMipCount, 1u) - 1u);
				const float texelAngle = 1.5707963 / float(4u << (max(Params[0].ProbeMipCount, 1u) - 1u));
				// The mip: the roughness's, or the one whose texels match how far the reflected
				// direction turns per screen pixel (a sphere's rim minifies its surroundings:
				// sampling the sharp mip there shimmered with the jitter).
				// One mip short of the full footprint: TAA averages the rest without the haze a
				// fully matched (pre-blurred) mip gave the rims.
				const float footprintLod = log2(max(reflectionSpread / texelAngle, 1.0)) - 1.0;
				const float lod = min(max(saturate(encoded.w) * mips, footprintLod), mips);
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
		// The reflection term: the planar reflection replaces the fallback; the screen-space hit
		// goes over it by its confidence, but over a planar reflection only as far as its SSR
		// preference allows (0 unless the reflector is small on screen: the far LOD), in
		// proportion to the planar weight (the rim of a sphere cap blends towards plain SSR).
		// It is then (optionally) filtered over time, replacing the specular IBL Forward+ added.
		const float ssrShare = confidence * lerp(1.0, ssrPreference, planarWeight);
		const float3 base = planarWeight > 0.0 ? lerp(fallback, reflectance * planarRadiance, planarWeight) : fallback;
		const float3 reflected = ssrShare > 0.0 ? lerp(base, reflectance * reflection.rgb, ssrShare) : base;
		c = max(c + ao * (reflected - specular), float3(0.0, 0.0, 0.0));
		if (debug == 0u)
		{
			// Only the screen-space share is filtered over time (it is traced one pixel in two and
			// jittered); planar reflections and probes are current and stable on their own, and
			// reprojecting them by the surface's motion dragged old images along (double images,
			// trails on spheres reflecting moving things). Curved surfaces, whose reflections move
			// far faster than the surface, are left alone too.
			const float curved = saturate(normalTurn * 50.0);
			reflectionTerm = float4(reflected, ao * ssrShare * (1.0 - curved));
		}
		if (debug == 1u)
		{
			// Sources: SSR red, probes green, the environment blue, planar yellow.
			debugColor = float3(confidence, (1.0 - confidence) * probeCoverage, (1.0 - confidence) * (1.0 - probeCoverage));
			if (ssrEnabled && Reflection.Load(Params[0].SsrHalf != 0u ? int3(at.x / 2, at.y, 0) : at).a < 0.0)
			{
				debugColor = lerp(debugColor, float3(1.0, 0.0, 1.0), 0.6);
			}
			debugColor = lerp(debugColor, float3(1.0, 0.85, 0.0), planarWeight * (1.0 - ssrShare));
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
