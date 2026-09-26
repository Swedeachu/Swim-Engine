// Screen-space reflections (item 76): ScreenSpace::ReflectionTexel for every pixel, one
// mirror ray marched through the depth buffer. Bindings follow
// ScreenSpaceReflectionBindings.
#include "ScreenSpace/ScreenSpaceRecords.slang"

[[vk::binding(0, 0)]] Texture2D<float> Depth;
[[vk::binding(1, 0)]] Texture2D<float4> Normal;
[[vk::binding(2, 0)]] Texture2D<float4> Color;
[[vk::binding(3, 0)]] Texture2D<float4> Indirect;
[[vk::binding(4, 0)]] Texture2D<float> Ao;
[[vk::binding(5, 0)]] StructuredBuffer<ScreenSpaceParams> Params;
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

// The reflection parameters, loaded once.
struct ReflectionParams
{
	float4 Projection[4];
	float3 ViewRows[3];
	float MaxDistance;
	float Thickness;
	float Stride;
	float MaxRoughness;
	float RoughnessFade;
	float EdgeFade;
	float DistanceFade;
	float NearZ;
	uint MaxSteps;
	uint RefineSteps;
	uint NoiseFrame;
	bool AoEnabled;
};

struct MarchSample
{
	bool Inside;
	int X;
	int Y;
	float RayDepth;
};

// ScreenSpace::ProjectToScreen: pixel position (xy) and clip w (z).
float3 ProjectToScreen(ScreenSpaceView view, ReflectionParams reflection, float3 p)
{
	const float4 point = float4(p, 1.0);
	const float4 clip = float4(dot(reflection.Projection[0], point), dot(reflection.Projection[1], point),
		dot(reflection.Projection[2], point), dot(reflection.Projection[3], point));
	const float inverseW = 1.0 / clip.w;
	return float3((clip.x * inverseW + 1.0 + view.Jitter.x) * 0.5 * float(view.Width),
		(1.0 - clip.y * inverseW - view.Jitter.y) * 0.5 * float(view.Height), clip.w);
}

// ScreenSpace::ViewNormal.
float3 ViewNormalOf(ReflectionParams reflection, float3 world, out bool valid)
{
	const float3 n = float3(dot(reflection.ViewRows[0], world), dot(reflection.ViewRows[1], world), dot(reflection.ViewRows[2], world));
	const float normalLength = length(n);
	valid = normalLength > 1.0e-6;
	return valid ? n * (1.0 / normalLength) : float3(0.0, 0.0, 0.0);
}

// The perspective-correct view depth (positive) at t along the projected ray.
float RayDepthAt(float3 q0, float3 q1, float k0, float k1, float t)
{
	return -(q0.z + (q1.z - q0.z) * t) / (k0 + (k1 - k0) * t);
}

// ScreenSpace::HitRadiance.
float3 HitRadiance(ReflectionParams reflection, int3 at)
{
	float3 radiance = Color.Load(at).rgb;
	if (reflection.AoEnabled)
	{
		radiance = max(radiance - (1.0 - Ao.Load(at)) * Indirect.Load(at).rgb, float3(0.0, 0.0, 0.0));
	}
	return radiance;
}

// ScreenSpace::FilteredHitRadiance: bilinear over the four nearest texels, each weighted
// by 1 / (1 + luminance) and renormalized.
float3 FilteredHitRadiance(ScreenSpaceView view, ReflectionParams reflection, float px, float py)
{
	const float ux = px - 0.5;
	const float uy = py - 0.5;
	const float bx = floor(ux);
	const float by = floor(uy);
	const float fx = ux - bx;
	const float fy = uy - by;
	const int maxX = int(view.Width) - 1;
	const int maxY = int(view.Height) - 1;
	float3 sum = float3(0.0, 0.0, 0.0);
	float weights = 0.0;
	for (int j = 0; j < 4; ++j)
	{
		const int ox = j & 1;
		const int oy = j >> 1;
		const int3 at = int3(clamp(int(bx) + ox, 0, maxX), clamp(int(by) + oy, 0, maxY), 0);
		const float3 radiance = HitRadiance(reflection, at);
		const float luma = 0.2126 * radiance.r + 0.7152 * radiance.g + 0.0722 * radiance.b;
		const float weight = (ox != 0 ? fx : 1.0 - fx) * (oy != 0 ? fy : 1.0 - fy) / (1.0 + luma);
		sum += weight * radiance;
		weights += weight;
	}
	return weights > 0.0 ? sum * (1.0 / weights) : float3(0.0, 0.0, 0.0);
}

// ScreenSpace::ReflectionTexel.
float4 Trace(ScreenSpaceView view, ReflectionParams reflection, int x, int y)
{
	const float4 miss = float4(0.0, 0.0, 0.0, 0.0);
	bool valid;
	const float3 p = ScreenSpaceLoadViewPosition(view, Depth, x, y, valid);
	if (!valid || !(-p.z > 0.0))
	{
		return miss;
	}
	const float4 encoded = Normal.Load(int3(x, y, 0));
	bool normalValid;
	const float3 n = ViewNormalOf(reflection, encoded.xyz, normalValid);
	const float roughness = encoded.w;
	if (!normalValid || !(roughness < reflection.MaxRoughness))
	{
		return miss;
	}
	const float roughnessFade = clamp((reflection.MaxRoughness - roughness) / reflection.RoughnessFade, 0.0, 1.0);
	const float3 v = p * (1.0 / length(p));
	const float3 reflectedRaw = v - n * (2.0 * dot(v, n));
	const float3 r = reflectedRaw * (1.0 / length(reflectedRaw));
	float rayLength = reflection.MaxDistance;
	if (p.z + r.z * rayLength > reflection.NearZ)
	{
		rayLength = (reflection.NearZ - p.z) / r.z;
	}
	if (!(rayLength > 0.0))
	{
		return miss;
	}
	const float3 rayEnd = p + r * rayLength;
	const float3 s0 = ProjectToScreen(view, reflection, p);
	const float3 s1 = ProjectToScreen(view, reflection, rayEnd);
	const float k0 = 1.0 / s0.z;
	const float k1 = 1.0 / s1.z;
	const float3 q0 = p * k0;
	const float3 q1 = rayEnd * k1;
	const float dx = s1.x - s0.x;
	const float dy = s1.y - s0.y;
	const float pixelLength = sqrt(dx * dx + dy * dy);
	const uint steps = min(reflection.MaxSteps, uint(ceil(pixelLength / reflection.Stride)));
	const float jitter = ScreenSpaceNoise(float(x) + 23.0, float(y) + 41.0, reflection.NoiseFrame);
	const float width = float(view.Width);
	const float height = float(view.Height);

	// Candidates: within the thickness behind the depth buffer, or just crossed from in
	// front to behind (a thin intersection the stride stepped over). A refined candidate
	// that ends up far behind a closer surface is not a hit; the march goes on.
	float previous = 0.0;
	bool previousBehind = false;
	for (uint i = 1; i <= steps; ++i)
	{
		const float t = min((float(i) + jitter) / float(steps), 1.0);
		const float sx = s0.x + dx * t;
		const float sy = s0.y + dy * t;
		if (!(sx >= 0.0 && sy >= 0.0 && sx < width && sy < height))
		{
			return miss;
		}
		const int ix = int(floor(sx));
		const int iy = int(floor(sy));
		const float rayDepth = RayDepthAt(q0, q1, k0, k1, t);
		bool sceneValid = false;
		float3 scene = float3(0.0, 0.0, 0.0);
		if (ix != x || iy != y)
		{
			scene = ScreenSpaceLoadViewPosition(view, Depth, ix, iy, sceneValid);
		}
		const bool behind = sceneValid && rayDepth >= -scene.z;
		const bool within = behind && rayDepth <= -scene.z + reflection.Thickness;
		if (!within && !(behind && !previousBehind))
		{
			previous = t;
			previousBehind = behind;
			continue;
		}
		float lo = previous;
		float hi = t;
		for (uint k = 0; k < reflection.RefineSteps; ++k)
		{
			const float mid = 0.5 * (lo + hi);
			const float mx = s0.x + dx * mid;
			const float my = s0.y + dy * mid;
			bool probeValid = false;
			float3 probe = float3(0.0, 0.0, 0.0);
			if (mx >= 0.0 && my >= 0.0 && mx < width && my < height)
			{
				probe = ScreenSpaceLoadViewPosition(view, Depth, int(floor(mx)), int(floor(my)), probeValid);
			}
			const float probeDepth = RayDepthAt(q0, q1, k0, k1, mid);
			if (probeValid && probeDepth >= -probe.z)
			{
				hi = mid;
			}
			else
			{
				lo = mid;
			}
		}
		int hx = ix;
		int hy = iy;
		float hitSceneDepth = -scene.z;
		bool refined = false;
		const float fx = s0.x + dx * hi;
		const float fy = s0.y + dy * hi;
		if (fx >= 0.0 && fy >= 0.0 && fx < width && fy < height)
		{
			const int rx = int(floor(fx));
			const int ry = int(floor(fy));
			bool refinedValid = false;
			float3 refinedScene = float3(0.0, 0.0, 0.0);
			if (rx != x || ry != y)
			{
				refinedScene = ScreenSpaceLoadViewPosition(view, Depth, rx, ry, refinedValid);
			}
			if (refinedValid)
			{
				hx = rx;
				hy = ry;
				hitSceneDepth = -refinedScene.z;
				refined = true;
			}
		}
		if (!refined)
		{
			hi = t;
			lo = previous;
		}
		const float hitRayDepth = RayDepthAt(q0, q1, k0, k1, hi);
		const float span = abs(hitRayDepth - RayDepthAt(q0, q1, k0, k1, lo));
		if (!(hitRayDepth - hitSceneDepth <= max(reflection.Thickness, span)))
		{
			previous = t;
			previousBehind = behind;
			continue;
		}
		bool hitNormalValid;
		const float3 hitNormal = ViewNormalOf(reflection, Normal.Load(int3(hx, hy, 0)).xyz, hitNormalValid);
		if (!hitNormalValid || dot(hitNormal, r) > 0.0)
		{
			return miss;
		}
		const float k = k0 + (k1 - k0) * hi;
		const float3 hitPoint = (q0 + (q1 - q0) * hi) / k;
		const float travelled = length(hitPoint - p);
		const float hitX = s0.x + dx * hi;
		const float hitY = s0.y + dy * hi;
		const float u = hitX / width;
		const float w = hitY / height;
		const float edge = min(min(u, 1.0 - u), min(w, 1.0 - w));
		const float edgeFade = clamp(edge / reflection.EdgeFade, 0.0, 1.0);
		const float distanceFade = clamp((reflection.MaxDistance - travelled) / (reflection.MaxDistance * reflection.DistanceFade), 0.0, 1.0);
		const float confidence = roughnessFade * edgeFade * distanceFade;
		if (!(confidence > 0.0))
		{
			return miss;
		}
		return float4(FilteredHitRadiance(view, reflection, hitX, hitY), confidence);
	}
	return miss;
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
	ReflectionParams reflection;
	for (uint row = 0; row < 4; ++row)
	{
		reflection.Projection[row] = Params[0].Projection[row];
	}
	for (uint row = 0; row < 3; ++row)
	{
		reflection.ViewRows[row] = Params[0].ViewRows[row].xyz;
	}
	reflection.MaxDistance = Params[0].SsrMaxDistance;
	reflection.Thickness = Params[0].SsrThickness;
	reflection.Stride = Params[0].SsrStride;
	reflection.MaxRoughness = Params[0].SsrMaxRoughness;
	reflection.RoughnessFade = Params[0].SsrRoughnessFade;
	reflection.EdgeFade = Params[0].SsrEdgeFade;
	reflection.DistanceFade = Params[0].SsrDistanceFade;
	reflection.NearZ = Params[0].SsrNearZ;
	reflection.MaxSteps = Params[0].SsrMaxSteps;
	reflection.RefineSteps = Params[0].SsrRefineSteps;
	reflection.NoiseFrame = Params[0].NoiseFrame;
	reflection.AoEnabled = Params[0].AoEnabled != 0u;
	Output[id.xy] = Trace(view, reflection, int(id.x), int(id.y));
}
