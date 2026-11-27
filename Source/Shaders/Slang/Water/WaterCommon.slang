// Shared water helpers: sampling the FFT ocean cascades (Engine::OceanSimulation) and
// procedural caustics. Consumers declare the cascade textures at their own bindings and
// pass them in; a cascade with weight 0 is skipped (bind any texture there).
#ifndef SWIM_WATER_COMMON_SLANG
#define SWIM_WATER_COMMON_SLANG

static const float WaterPi = 3.14159265359;

// Per cascade: 1 / tile size in metres (uv = world xz * InverseSize) and a weight.
struct OceanCascades
{
	float4 InverseSizes; // x, y, z: cascades 0..2; w: unused.
	float4 Weights;		 // x, y, z: cascades 0..2 (0 = off); w: unused.
};

// Displacement (xyz metres, w foam) summed over the cascades at a world xz.
float4 OceanSampleDisplacement(Texture2D<float4> d0, Texture2D<float4> d1, Texture2D<float4> d2, SamplerState wrap,
	OceanCascades cascades, float2 xz)
{
	float4 sum = d0.SampleLevel(wrap, xz * cascades.InverseSizes.x, 0.0) * cascades.Weights.x;
	if (cascades.Weights.y > 0.0)
	{
		sum += d1.SampleLevel(wrap, xz * cascades.InverseSizes.y, 0.0) * cascades.Weights.y;
	}
	if (cascades.Weights.z > 0.0)
	{
		sum += d2.SampleLevel(wrap, xz * cascades.InverseSizes.z, 0.0) * cascades.Weights.z;
	}
	return sum;
}

// Derivatives (dy/dx, dy/dz, dx/dx, dz/dz) summed over the cascades; weights already
// include any distance fade.
float4 OceanSampleDerivatives(Texture2D<float4> d0, Texture2D<float4> d1, Texture2D<float4> d2, SamplerState wrap, float4 inverseSizes,
	float3 weights, float2 xz)
{
	float4 sum = d0.SampleLevel(wrap, xz * inverseSizes.x, 0.0) * weights.x;
	if (weights.y > 0.0)
	{
		sum += d1.SampleLevel(wrap, xz * inverseSizes.y, 0.0) * weights.y;
	}
	if (weights.z > 0.0)
	{
		sum += d2.SampleLevel(wrap, xz * inverseSizes.z, 0.0) * weights.z;
	}
	return sum;
}

// The surface normal of a choppy displaced surface from its summed derivatives.
float3 OceanNormal(float4 derivatives)
{
	const float2 slope = derivatives.xy / max(1.0 + derivatives.zw, float2(0.05, 0.05));
	return normalize(float3(-slope.x, 1.0, -slope.y));
}

// Cascade weights faded by the pixel footprint (metres per pixel), so cascades finer than
// the screen can resolve fade out instead of aliasing (the maps have no mips): a cascade is
// gone before a pixel spans two of its texels, where its slopes would flicker from frame to
// frame. w is the share of detail lost, for a roughness lift.
// The footprint of a pixel on a level surface: metres per pixel at the distance, stretched
// toward grazing angles (cosine = |view direction . surface normal|), between the short and
// the long axis of the pixel's ellipse.
float OceanSurfaceFootprint(float metresPerPixel, float cosine)
{
	return metresPerPixel * rsqrt(max(cosine, 0.1));
}

float4 OceanFootprintWeights(OceanCascades cascades, float footprint, float resolution)
{
	float3 weights = cascades.Weights.xyz;
	float lost = 0.0;
	[unroll]
	for (uint i = 0; i < 3; ++i)
	{
		const float texel = 1.0 / (cascades.InverseSizes[i] * resolution);
		const float fade = 1.0 - smoothstep(0.6 * texel, 2.0 * texel, footprint);
		lost = max(lost, (1.0 - fade) * (weights[i] > 0.0 ? 1.0 : 0.0) * (i == 0 ? 1.0 : 0.5));
		weights[i] *= fade;
	}
	return float4(weights, lost);
}

// Cascade weights for geometry sampled on a grid with the given spacing (metres): a cascade
// fades out once its tile spans too few grid steps to carry its shorter waves, which would
// otherwise alias into vertices that jump from frame to frame.
float3 OceanGridWeights(float4 inverseSizes, float3 weights, float spacing)
{
	float3 result = weights;
	[unroll]
	for (uint i = 0; i < 3; ++i)
	{
		const float tile = 1.0 / inverseSizes[i];
		result[i] *= 1.0 - smoothstep(tile / 32.0, tile / 12.0, spacing);
	}
	return result;
}

float2 WaterHash2(float2 p)
{
	uint2 q = uint2(int2(floor(p)) + int2(32768, 32768));
	q = q * uint2(1597334673u, 3812015801u);
	const uint n = (q.x ^ q.y) * 1597334673u;
	const uint2 r = uint2(n, n * 3812015801u);
	return float2(r >> 8) * (1.0 / 16777216.0);
}

// The animated feature point of one caustic cell (0..1 inside the cell).
float2 WaterCausticSite(float2 cell, float time)
{
	const float2 h = WaterHash2(cell);
	return 0.5 + 0.42 * sin(time * (0.6 + 0.5 * h) + 2.0 * WaterPi * h.yx);
}

// The 3x3 feature points around a cell, kept while a march stays inside that cell.
struct WaterCausticSites
{
	float2 Cell;
	bool Valid;
	float2 Site[9];
};

WaterCausticSites WaterCausticSitesEmpty()
{
	WaterCausticSites sites;
	sites.Cell = float2(0.0, 0.0);
	sites.Valid = false;
	[unroll]
	for (int i = 0; i < 9; ++i)
	{
		sites.Site[i] = float2(0.0, 0.0);
	}
	return sites;
}

// One caustic layer: the edges of an animated cellular pattern (distance to the second
// nearest feature point minus the nearest), sharpened into thin bright filaments. The
// sites of the last cell are reused, so a march through few cells evaluates few sites.
float WaterCausticLayerCached(float2 p, float time, inout WaterCausticSites sites)
{
	const float2 cell = floor(p);
	if (!sites.Valid || any(cell != sites.Cell))
	{
		sites.Cell = cell;
		sites.Valid = true;
		[unroll]
		for (int i = 0; i < 9; ++i)
		{
			sites.Site[i] = WaterCausticSite(cell + float2(float(i % 3 - 1), float(i / 3 - 1)), time);
		}
	}
	const float2 f = p - cell;
	float d1 = 8.0;
	float d2 = 8.0;
	[unroll]
	for (int i = 0; i < 9; ++i)
	{
		const float2 r = float2(float(i % 3 - 1), float(i / 3 - 1)) + sites.Site[i] - f;
		const float d = dot(r, r);
		if (d < d1)
		{
			d2 = d1;
			d1 = d;
		}
		else if (d < d2)
		{
			d2 = d;
		}
	}
	const float edge = sqrt(d2) - sqrt(d1);
	return pow(saturate(1.0 - edge * 2.2), 5.0);
}

float WaterCausticLayer(float2 p, float time)
{
	WaterCausticSites sites = WaterCausticSitesEmpty();
	return WaterCausticLayerCached(p, time, sites);
}

// Caustic light at a world xz (metres / scale = pattern cells): two warped layers that
// cross and brighten where filaments meet. Roughly 0..1.5, mean about 0.35.
// Nearby lookups (colour fringes) pass the same two site caches to share their cells.
float WaterCausticsCached(float2 p, float time, inout WaterCausticSites first, inout WaterCausticSites second)
{
	const float2 warp = float2(sin(p.y * 1.7 + time * 0.9), sin(p.x * 1.3 - time * 0.7)) * 0.12;
	const float a = WaterCausticLayerCached(p + warp, time, first);
	const float2 q = float2(p.x * 0.8 - p.y * 0.6, p.x * 0.6 + p.y * 0.8) * 1.37 + float2(17.3, 4.1);
	const float b = WaterCausticLayerCached(q - warp, time * 1.13, second);
	return a * 0.6 + b * 0.5 + a * b * 1.2;
}

float WaterCaustics(float2 p, float time)
{
	WaterCausticSites first = WaterCausticSitesEmpty();
	WaterCausticSites second = WaterCausticSitesEmpty();
	return WaterCausticsCached(p, time, first, second);
}

// The cheap variant for volumetric shafts: one layer (sites reused along the march).
float WaterCausticsSoft(float2 p, float time, inout WaterCausticSites sites)
{
	return WaterCausticLayerCached(p, time, sites);
}

#endif
