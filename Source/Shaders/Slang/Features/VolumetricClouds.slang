// Volumetric clouds (Engine::VolumetricClouds), five programs from one source:
//  - VolumetricCloudsBake (CLOUDS_BAKE=1): runs once. Bakes the tileable noise the march
//    samples: a 128^3 shape volume stored as an atlas of padded 2D slices (r: Perlin-Worley
//    billows, gba: Worley fBm at three frequencies) and a 256^2 weather map (r: where clouds
//    form, g: how tall they grow, b: how high their bases sit, a: cirrus streaks). Rows >=
//    CLOUD_ATLAS_HEIGHT of the dispatch write the map.
//  - VolumetricCloudsMarch (default): at a reduced resolution, each pixel's view ray is
//    marched through a horizontal cloud layer whose density comes from the baked noise
//    (local coverage from the weather map, a rounded height profile per cloud, billowy or
//    wispy erosion), lit by the sun (a cone of shadow samples, Beer-Lambert with
//    multiple-scattering octaves and a powder term, a two-lobe Henyey-Greenstein phase) and
//    the sky's ambient colors, with energy-conserving integration (Hillaire 2016). Steps grow
//    with distance; empty space is crossed in double steps; a cirrus sheet lies beyond.
//    Output: in-scattered radiance (rgb) and transmittance (a). The start of each march is
//    dithered per pixel and frame.
//  - VolumetricCloudsTemporal (CLOUDS_TEMPORAL=1): accumulates the march over frames.
//  - VolumetricCloudsComposite (CLOUDS_COMPOSITE=1): upsamples that onto the sky pixels
//    of the HDR scene color (color * transmittance + radiance).
//  - VolumetricCloudsEnvironment (CLOUDS_ENVIRONMENT=1): the same march for every texel
//    of the environment cube (Size.x = face size, a Size.x x 6 * Size.x atlas with face f
//    in rows f * Size.x..), from the probe at Camera.xyz, with a fixed dither. The
//    EnvironmentBuilder folds it into the sky cube before prefiltering, so reflections
//    and ambient light see the clouds. Forward.w holds the environment rotation.
#include "Features/FeatureCommon.slang"

#ifndef CLOUDS_COMPOSITE
#define CLOUDS_COMPOSITE 0
#endif
#ifndef CLOUDS_ENVIRONMENT
#define CLOUDS_ENVIRONMENT 0
#endif
#ifndef CLOUDS_BAKE
#define CLOUDS_BAKE 0
#endif
#ifndef CLOUDS_TEMPORAL
#define CLOUDS_TEMPORAL 0
#endif
#if CLOUDS_ENVIRONMENT
#include "Environment/EnvironmentCommon.slang"
#endif

// Keep in step with VolumetricClouds.h.
#define CLOUD_NOISE_SIZE 128
#define CLOUD_NOISE_TILE 130
#define CLOUD_ATLAS_COLUMNS 16
#define CLOUD_ATLAS_WIDTH 2080
#define CLOUD_ATLAS_HEIGHT 1040
#define CLOUD_WEATHER_SIZE 256

float CloudRemap(float value, float low, float high, float newLow, float newHigh)
{
	return newLow + (value - low) / max(high - low, 1.0e-5) * (newHigh - newLow);
}

#if CLOUDS_BAKE
[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> ShapeAtlas;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> WeatherMap;

uint CloudCellHash(int3 cell, uint seed)
{
	return FeatureHash(uint(cell.x) * 73856093u ^ FeatureHash(uint(cell.y) * 19349663u ^ FeatureHash(uint(cell.z) * 83492791u ^ seed)));
}

float3 CloudCellPoint(int3 cell, uint seed)
{
	const uint h = CloudCellHash(cell, seed);
	return float3(float(h & 1023u), float((h >> 10u) & 1023u), float((h >> 20u) & 1023u)) * (1.0 / 1023.0);
}

int3 CloudWrap(int3 cell, int period)
{
	return ((cell % period) + period) % period;
}

// Inverted cellular noise (1 at the feature points), tiling every `cells` cells over p in 0..1.
float CloudWorley(float3 p, int cells, uint seed)
{
	const float3 q = p * float(cells);
	const int3 base = int3(floor(q));
	const float3 f = q - floor(q);
	float best = 1.0e9;
	for (int z = -1; z <= 1; ++z)
	{
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				const int3 offset = int3(x, y, z);
				const float3 d = float3(offset) + CloudCellPoint(CloudWrap(base + offset, cells), seed) - f;
				best = min(best, dot(d, d));
			}
		}
	}
	return 1.0 - saturate(sqrt(best));
}

float CloudGradientDot(int3 cell, int period, uint seed, float3 offset)
{
	const float3 g = normalize(CloudCellPoint(CloudWrap(cell, period), seed) * 2.0 - 1.0 + 1.0e-4);
	return dot(g, offset);
}

// Gradient noise in about -1..1, tiling every `cells` cells over p in 0..1.
float CloudPerlin(float3 p, int cells, uint seed)
{
	const float3 q = p * float(cells);
	const int3 i = int3(floor(q));
	const float3 f = q - floor(q);
	const float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	const float n000 = CloudGradientDot(i, cells, seed, f);
	const float n100 = CloudGradientDot(i + int3(1, 0, 0), cells, seed, f - float3(1, 0, 0));
	const float n010 = CloudGradientDot(i + int3(0, 1, 0), cells, seed, f - float3(0, 1, 0));
	const float n110 = CloudGradientDot(i + int3(1, 1, 0), cells, seed, f - float3(1, 1, 0));
	const float n001 = CloudGradientDot(i + int3(0, 0, 1), cells, seed, f - float3(0, 0, 1));
	const float n101 = CloudGradientDot(i + int3(1, 0, 1), cells, seed, f - float3(1, 0, 1));
	const float n011 = CloudGradientDot(i + int3(0, 1, 1), cells, seed, f - float3(0, 1, 1));
	const float n111 = CloudGradientDot(i + int3(1, 1, 1), cells, seed, f - float3(1, 1, 1));
	const float x00 = lerp(n000, n100, u.x);
	const float x10 = lerp(n010, n110, u.x);
	const float x01 = lerp(n001, n101, u.x);
	const float x11 = lerp(n011, n111, u.x);
	return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z) * 1.4;
}

float CloudPerlinFbm(float3 p, int cells, uint octaves, uint seed)
{
	float sum = 0.0;
	float amplitude = 0.5;
	float total = 0.0;
	for (uint o = 0; o < octaves; ++o)
	{
		sum += CloudPerlin(p, cells << o, seed + o * 7919u) * amplitude;
		total += amplitude;
		amplitude *= 0.5;
	}
	return saturate(sum / total * 0.5 + 0.5);
}

float CloudWorleyFbm(float3 p, int cells, uint seed)
{
	return CloudWorley(p, cells, seed) * 0.625 + CloudWorley(p, cells * 2, seed + 1u) * 0.25 + CloudWorley(p, cells * 4, seed + 2u) * 0.125;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.y < CLOUD_ATLAS_HEIGHT)
	{
		if (id.x >= CLOUD_ATLAS_WIDTH)
		{
			return;
		}
		// Padded slices: texel (i, j) of slice s sits at 1 + (i, j) in its tile, and the one-texel
		// border repeats the opposite edge so bilinear lookups wrap inside a tile.
		const uint2 tile = id.xy / CLOUD_NOISE_TILE;
		const int2 local = int2(id.xy % CLOUD_NOISE_TILE) - 1;
		const int slice = int(tile.y * CLOUD_ATLAS_COLUMNS + tile.x);
		const int3 texel = CloudWrap(int3(local, slice), CLOUD_NOISE_SIZE);
		const float3 p = (float3(texel) + 0.5) / float(CLOUD_NOISE_SIZE);
		const float perlin = CloudPerlinFbm(p, 4, 4u, 11u);
		const float worley = CloudWorleyFbm(p, 4, 23u);
		// Perlin dilated by low-frequency Worley: connected, billowy cloud bodies, stretched from
		// its narrow natural range (about 0.5..0.82) to fill 0..1.
		const float billows = saturate(CloudRemap(CloudRemap(perlin, worley - 1.0, 1.0, 0.0, 1.0), 0.5, 0.82, 0.0, 1.0));
		ShapeAtlas[id.xy] = float4(billows, CloudWorleyFbm(p, 8, 41u), CloudWorleyFbm(p, 12, 59u), CloudWorleyFbm(p, 16, 71u));
		return;
	}
	const uint2 cell = uint2(id.x, id.y - CLOUD_ATLAS_HEIGHT);
	if (cell.x >= CLOUD_WEATHER_SIZE || cell.y >= CLOUD_WEATHER_SIZE)
	{
		return;
	}
	const float3 p = float3((float2(cell) + 0.5) / float(CLOUD_WEATHER_SIZE), 0.37);
	// r: where clouds form, stretched to spread evenly over 0..1 so the coverage threshold
	// covers about its own fraction of the map; g: how tall they grow; b: how high their bases
	// sit; a: streaky high-frequency fBm for the cirrus sheet.
	const float patches = CloudPerlinFbm(p, 4, 4u, 101u) * 0.7 + CloudWorleyFbm(p, 6, 131u) * 0.3;
	const float coverage = saturate(CloudRemap(patches, 0.3, 0.72, 0.0, 1.0));
	const float type = saturate(CloudRemap(CloudPerlinFbm(p, 3, 3u, 151u), 0.3, 0.7, 0.0, 1.0));
	const float base = saturate(CloudRemap(CloudPerlinFbm(p, 8, 3u, 173u), 0.3, 0.7, 0.0, 1.0));
	const float cirrus = CloudPerlinFbm(float3(p.xy, 0.71), 6, 5u, 191u);
	WeatherMap[cell] = float4(coverage, type, base, cirrus);
}
#else

struct CloudParams
{
	float4 Forward;		  // w: tan(half horizontal fov).
	float4 Right;		  // w: tan(half vertical fov).
	float4 Up;			  // w: time (seconds).
	float4 Camera;		  // xyz: world position; w: coverage (0..1).
	float4 SunDirection;  // Toward the sun; w: density (extinction per meter at full density).
	float4 SunColor;	  // Sun radiance; w: ambient strength.
	float4 Layer;		  // x: bottom altitude; y: top altitude; z: shape frequency (1/m); w: detail frequency (1/m).
	float4 Wind;		  // xyz: noise offset (m, integrated wind); w: max ray distance (m).
	float4 AmbientTop;	  // rgb: sky radiance above the clouds; w: phase anisotropy g.
	float4 AmbientBottom; // rgb: radiance below (horizon); w: powder strength.
	float4 Lighting;	  // x: shadow step length (m); y: silver lining (back lobe weight); z: detail erosion; w: horizon fade start (0..1 of max distance).
	float4 Shape;		  // x: weather map frequency (1/m); y: towering (0..1); z: detail wind multiplier; w: coverage variation.
	float4 Form;		  // x: billow (0 wispy .. 1 billowy); y: base variation; z: base roundness; w: wind heading (radians).
	float4 Cirrus;		  // x: coverage; y: altitude (m); z: frequency (1/m); w: opacity.
	float4 PreviousViewProjection[4]; // Rows (clip = M * v), last frame's: the temporal pass reprojects with it.
	uint4 Size;			  // xy: march size; z: march steps; w: frame.
	uint4 Output;		  // xy: full size; z: shadow steps; w: 1 when the history is valid.
};

[[vk::binding(0, 0)]] StructuredBuffer<CloudParams> Params;

#if CLOUDS_TEMPORAL
[[vk::binding(1, 0)]] Texture2D<float4> Clouds;
[[vk::binding(2, 0)]] Texture2D<float4> History;
[[vk::binding(3, 0)]] SamplerState LinearClamp;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Accumulated;

// Accumulates the dithered march over frames: last frame's result, found by reprojecting
// this texel's view direction (the clouds are far enough for the camera's own movement not
// to matter), clipped to the spread of this frame's 3x3 neighbourhood so moving clouds and
// newly uncovered sky do not leave trails.
[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const CloudParams params = Params[0];
	if (id.x >= params.Size.x || id.y >= params.Size.y)
	{
		return;
	}
	const int2 limit = int2(params.Size.xy) - 1;
	const float4 current = Clouds.Load(int3(id.xy, 0));
	float4 mean = float4(0.0, 0.0, 0.0, 0.0);
	float4 square = float4(0.0, 0.0, 0.0, 0.0);
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const float4 c = Clouds.Load(int3(clamp(int2(id.xy) + int2(x, y), int2(0, 0), limit), 0));
			mean += c;
			square += c * c;
		}
	}
	mean /= 9.0;
	const float4 sigma = sqrt(max(square / 9.0 - mean * mean, 0.0));
	float4 result = current;
	if (params.Output.w != 0u)
	{
		const float2 uv = (float2(id.xy) + 0.5) / float2(params.Size.xy);
		const float3 ray = FeatureViewRay(uv, params.Forward, params.Right, params.Up);
		const float4 direction = float4(ray, 0.0);
		const float4 clip = float4(dot(params.PreviousViewProjection[0], direction), dot(params.PreviousViewProjection[1], direction),
			dot(params.PreviousViewProjection[2], direction), dot(params.PreviousViewProjection[3], direction));
		if (clip.w > 1.0e-5)
		{
			const float2 previous = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
			if (all(previous >= 0.0) && all(previous <= 1.0))
			{
				const float4 history = History.SampleLevel(LinearClamp, previous, 0.0);
				const float4 clipped = clamp(history, mean - sigma * 1.25 - 0.002, mean + sigma * 1.25 + 0.002);
				result = lerp(clipped, current, 0.12);
			}
		}
	}
	Accumulated[id.xy] = result;
}
#elif !CLOUDS_COMPOSITE
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Clouds;
[[vk::binding(2, 0)]] Texture2D<float4> ShapeAtlas;
[[vk::binding(3, 0)]] Texture2D<float4> WeatherMap;
[[vk::binding(4, 0)]] SamplerState LinearClamp;
[[vk::binding(5, 0)]] SamplerState LinearRepeat;
#if !CLOUDS_ENVIRONMENT
// The scene's depth (0 = sky): march cells whose whole footprint is covered by geometry are
// skipped (the composite only draws clouds over sky pixels).
[[vk::binding(6, 0)]] Texture2D<float> Depth;
#endif

float2 CloudSliceUv(int slice, float2 inSlice)
{
	const float2 tile = float2(float(slice % CLOUD_ATLAS_COLUMNS), float(slice / CLOUD_ATLAS_COLUMNS)) * float(CLOUD_NOISE_TILE);
	return (tile + 1.0 + inSlice * float(CLOUD_NOISE_SIZE)) / float2(CLOUD_ATLAS_WIDTH, CLOUD_ATLAS_HEIGHT);
}

// The baked shape volume at p (in tiles; it repeats every 1.0), filtered trilinearly.
float4 CloudShape(float3 p)
{
	p = frac(p);
	const float z = p.z * float(CLOUD_NOISE_SIZE) - 0.5;
	const float z0 = floor(z);
	const int s0 = (int(z0) + CLOUD_NOISE_SIZE) % CLOUD_NOISE_SIZE;
	const int s1 = (s0 + 1) % CLOUD_NOISE_SIZE;
	const float4 a = ShapeAtlas.SampleLevel(LinearClamp, CloudSliceUv(s0, p.xy), 0.0);
	const float4 b = ShapeAtlas.SampleLevel(LinearClamp, CloudSliceUv(s1, p.xy), 0.0);
	return lerp(a, b, z - z0);
}

// Density (0..1 of full extinction) at a world point; `cheap` skips the detail erosion
// (shadow rays and the empty-space search), so it is never below the full density.
float CloudDensity(CloudParams params, float3 position, bool cheap)
{
	const float bottom = params.Layer.x;
	const float top = params.Layer.y;
	const float height = (position.y - bottom) / max(top - bottom, 1.0);
	if (height <= 0.0 || height >= 1.0)
	{
		return 0.0;
	}
	const float3 moved = position + params.Wind.xyz;
	const float4 weather = WeatherMap.SampleLevel(LinearRepeat, moved.xz * params.Shape.x, 0.0);
	// The local coverage: the global amount, raised or lowered by the map (clear patches and
	// cloudy patches), about the fraction of this part of the sky that is cloud.
	const float coverage = saturate(params.Camera.w);
	const float local = saturate(coverage + (weather.r - 0.5) * 2.0 * params.Shape.w * min(coverage * 2.5, 1.0));
	if (local <= 0.0)
	{
		return 0.0;
	}
	// Each cloud's extent in the layer: its top from towering and the map's type, its base
	// raised by the map's base noise (bases stay near a common condensation level).
	const float type = saturate(params.Shape.y + (weather.g - 0.5) * 0.6);
	const float cloudTop = lerp(0.2, 1.0, type);
	const float cloudBase = weather.b * params.Form.y * cloudTop * 0.35;
	const float inCloud = (height - cloudBase) / max(cloudTop - cloudBase, 0.02);
	if (inCloud <= 0.0 || inCloud >= 1.0)
	{
		return 0.0;
	}
	// The height profile scales the shape before the coverage threshold, so only the cores
	// reach the base and the top: rounded undersides and domed tops instead of flat cuts.
	const float roundness = 0.03 + 0.3 * params.Form.z;
	const float profile = sqrt(saturate(inCloud / roundness)) * saturate((1.0 - inCloud) / lerp(0.25, 0.6, type));
	// Billows, carved by the lower-frequency Worley cells into cauliflower heaps.
	const float4 shape = CloudShape(moved * params.Layer.z);
	const float lumps = saturate(CloudRemap(shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125, 0.3, 0.67, 0.0, 1.0));
	const float billows = saturate(CloudRemap(shape.r, (1.0 - lumps) * 0.45, 1.0, 0.0, 1.0));
	const float softness = max(0.06, local * 0.3);
	// The threshold is bent so the covered fraction of the sky follows the coverage (the
	// shape's values crowd toward its low end).
	float density = (billows * profile - pow(1.0 - local, 1.5)) / softness;
	if (density <= 0.0)
	{
		return 0.0;
	}
	if (!cheap && density < 1.0)
	{
		// The detail eats into the edge band only: round billows (inverted cells) or torn
		// wisps (the cells themselves), the bases always a little wispy.
		const float4 detail = CloudShape((position + params.Wind.xyz * params.Shape.z) * params.Layer.w);
		const float wisps = saturate(CloudRemap(detail.g * 0.625 + detail.b * 0.25 + detail.a * 0.125, 0.3, 0.67, 0.0, 1.0));
		const float billow = params.Form.x * saturate(inCloud * 4.0);
		const float erosion = lerp(wisps, 1.0 - wisps, billow) * params.Lighting.z * lerp(1.6, 1.0, params.Form.x);
		density = (density - erosion) / max(1.0 - erosion, 0.05);
		if (density <= 0.0)
		{
			return 0.0;
		}
	}
	// Denser toward the tops.
	return saturate(density) * lerp(0.6, 1.2, inCloud);
}

float CloudPhase(float cosine, float g)
{
	const float g2 = g * g;
	return (1.0 - g2) / (12.5663706 * pow(max(1.0 + g2 - 2.0 * g * cosine, 1.0e-4), 1.5));
}

// The thin cirrus sheet along a ray, behind whatever the layer left (transmittance):
// streaks stretched along the wind, lit by the sun's forward-scattering and the sky.
void CirrusSheet(CloudParams params, float3 ray, inout float3 radiance, inout float transmittance)
{
	const float coverage = params.Cirrus.x;
	if (coverage <= 0.0 || ray.y <= 0.01 || transmittance < 0.01)
	{
		return;
	}
	const float t = (params.Cirrus.y - params.Camera.y) / ray.y;
	if (t <= 0.0)
	{
		return;
	}
	const float2 p = (params.Camera.xz + ray.xz * t + params.Wind.xz * 1.5) * params.Cirrus.z;
	const float2 along = float2(sin(params.Form.w), cos(params.Form.w));
	const float2 streak = float2(dot(p, along) * 0.12, dot(p, float2(along.y, -along.x)));
	// Broad patches of the sheet, combed into fine fibres along the wind.
	const float patches = WeatherMap.SampleLevel(LinearRepeat, p * 0.31 + 0.17, 0.0).a;
	const float fibres = WeatherMap.SampleLevel(LinearRepeat, streak * float2(1.0, 2.5), 0.0).a;
	const float n = patches * 0.75 + fibres * 0.25;
	// Grazing rays cross the sheet far away: it thins out toward the horizon.
	const float fade = saturate(ray.y * 5.0);
	const float alpha = saturate((n - (1.0 - coverage) * 0.75 - 0.2) / 0.35) * params.Cirrus.w * fade;
	if (alpha <= 0.0)
	{
		return;
	}
	const float cosine = dot(ray, params.SunDirection.xyz);
	const float3 light = params.SunColor.rgb * (CloudPhase(cosine, 0.6) * 0.7 + 0.08) + params.AmbientTop.rgb * params.SunColor.w;
	radiance += transmittance * alpha * light;
	transmittance *= 1.0 - alpha;
}

// In-scattered radiance (rgb) and transmittance (a) along one ray from Camera.xyz;
// `jitter` (0..1) offsets the first sample within a step.
float4 MarchClouds(CloudParams params, float3 ray, float jitter)
{
	const float3 camera = params.Camera.xyz;
	const float bottom = params.Layer.x;
	const float top = params.Layer.y;
	const float maxDistance = params.Wind.w;
	float transmittance = 1.0;
	float3 radiance = float3(0.0, 0.0, 0.0);
	// The layer's span along the ray (the camera may be below, inside or above it).
	float t0, t1;
	if (abs(ray.y) < 1.0e-4)
	{
		const bool inside = camera.y >= bottom && camera.y <= top;
		t0 = 0.0;
		t1 = inside ? maxDistance : -1.0;
	}
	else
	{
		const float a = (bottom - camera.y) / ray.y;
		const float b = (top - camera.y) / ray.y;
		t0 = max(min(a, b), 0.0);
		t1 = min(max(a, b), maxDistance);
	}
	float hitDistance = 0.0;
	float hitWeight = 0.0;
	if (t1 > t0)
	{
		const uint steps = clamp(params.Size.z, 4u, 256u);
		// Steps cross the layer's thickness in `steps` when looking straight up and grow with
		// distance, where a sample covers more of the sky (the per-frame dither and the
		// temporal pass smooth what they skip).
		const float thickness = max(top - bottom, 1.0);
		const float minStep = thickness / float(steps);
		const float maxStep = max(minStep, thickness * 0.2);
		const float growth = 0.006;
		const float sigma = max(params.SunDirection.w, 1.0e-6);
		const float cosine = dot(ray, params.SunDirection.xyz);
		const float g = params.AmbientTop.w;
		const uint shadowSteps = clamp(params.Output.z, 1u, 16u);
		// The phase of each multiple-scattering octave (Wrenninge 2013): weaker and flatter.
		float phases[3];
		float octaveG = 1.0;
		for (uint octave = 0; octave < 3u; ++octave)
		{
			phases[octave] = lerp(CloudPhase(cosine, g * octaveG), CloudPhase(cosine, -0.3 * octaveG), params.Lighting.y);
			octaveG *= 0.5;
		}
		float stepLength = clamp(minStep + t0 * growth, minStep, maxStep);
		float t = t0 + stepLength * jitter;
		bool inside = false;
		uint emptyRun = 0;
		const uint budget = steps * 4u;
		for (uint i = 0; i < budget && t < t1; ++i)
		{
			stepLength = clamp(minStep + t * growth, minStep, maxStep);
			const float3 position = camera + ray * t;
			if (!inside)
			{
				// Searching: double steps on the cheap density; on a hit, back up and refine.
				if (CloudDensity(params, position, true) <= 0.0)
				{
					t += stepLength * 2.0;
					continue;
				}
				inside = true;
				emptyRun = 0;
				t = max(t - stepLength * 2.0, t0);
				continue;
			}
			const float density = CloudDensity(params, position, false);
			if (density > 1.0e-3)
			{
				emptyRun = 0;
				// Sun visibility through the cloud toward the sun: a cone of growing steps.
				float optical = 0.0;
				float shadowStep = params.Lighting.x;
				float shadowDistance = 0.0;
				for (uint j = 0; j < shadowSteps; ++j)
				{
					const float3 q = position + params.SunDirection.xyz * (shadowDistance + shadowStep * 0.5);
					optical += CloudDensity(params, q, j > 1u) * shadowStep;
					shadowDistance += shadowStep;
					shadowStep *= 1.7;
				}
				float sunLight = 0.0;
				float attenuation = 1.0;
				float contribution = 1.0;
				for (uint octave = 0; octave < 3u; ++octave)
				{
					sunLight += contribution * exp(-optical * sigma * attenuation) * phases[octave];
					attenuation *= 0.3;
					contribution *= 0.5;
				}
				// Powder: the sunlit side of a dense cloud is darker where light has not yet scattered in.
				const float powder = lerp(1.0, 1.0 - exp(-(density * 120.0 + optical) * sigma * 2.0), params.AmbientBottom.w);
				const float height = saturate((position.y - bottom) / thickness);
				// Ambient: sky above brightens the tops, the horizon lights the bases; the cloud
				// above a point (two cheap samples straight up) shades it, so thick storm clouds
				// have dark undersides while thin ones stay bright.
				const float above = CloudDensity(params, position + float3(0.0, thickness * 0.12, 0.0), true) +
					CloudDensity(params, position + float3(0.0, thickness * 0.35, 0.0), true);
				const float skyLight = lerp(0.2, 1.0, exp(-above * thickness * 0.025 * sigma));
				const float3 ambient = lerp(params.AmbientBottom.rgb, params.AmbientTop.rgb, height) * params.SunColor.w * (0.3 + 0.7 * height) * skyLight;
				const float3 scattering = params.SunColor.rgb * (sunLight * powder) + ambient;
				const float extinction = density * sigma;
				const float stepTransmittance = exp(-extinction * stepLength);
				const float absorbed = transmittance * (1.0 - stepTransmittance);
				// Energy-conserving: the in-scattering integrated over the step.
				radiance += scattering * absorbed;
				hitDistance += t * absorbed;
				hitWeight += absorbed;
				transmittance *= stepTransmittance;
				if (transmittance < 0.01)
				{
					break;
				}
			}
			else if (++emptyRun > 4u)
			{
				inside = false;
			}
			t += stepLength;
		}
	}
	// Distant clouds fade into the sky (aerial perspective; hides the undersampled far layer).
	const float distance = hitWeight > 0.0 ? hitDistance / hitWeight : t0;
	const float fadeStart = saturate(params.Lighting.w) * maxDistance;
	const float fade = 1.0 - saturate((distance - fadeStart) / max(maxDistance - fadeStart, 1.0));
	radiance *= fade;
	transmittance = lerp(1.0, transmittance, fade);
	CirrusSheet(params, ray, radiance, transmittance);
	return float4(radiance, transmittance);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const CloudParams params = Params[0];
	if (id.x >= params.Size.x || id.y >= params.Size.y)
	{
		return;
	}
#if CLOUDS_ENVIRONMENT
	// Cube texel -> world direction: lookups rotate world directions by +rotation.
	const uint size = params.Size.x;
	const float3 cube = EnvironmentCubeTexelDirection(id.y / size, id.x, id.y % size, size);
	const float3 ray = EnvironmentRotateLookup(cube, -params.Forward.w);
	// A fixed per-texel dither: the cube is prefiltered, and a changing one would shimmer.
	const float jitter = FeatureGradientNoise(float2(id.xy), 0u);
#else
	const float2 uv = (float2(id.xy) + 0.5) / float2(params.Size.xy);
	// The full-resolution footprint of this cell, one texel wider on each side (the
	// composite filters bilinearly): any sky pixel in it needs the march.
	const float2 scale = float2(params.Output.xy) / float2(params.Size.xy);
	const int2 lo = int2(floor((float2(id.xy) - 1.0) * scale));
	const int2 hi = int2(ceil((float2(id.xy) + 2.0) * scale)) - 1;
	const int2 limit = int2(params.Output.xy) - 1;
	bool sky = false;
	const int2 step = max(int2(1, 1), (hi - lo) / 3);
	for (int y = lo.y; y <= hi.y && !sky; y += step.y)
	{
		for (int x = lo.x; x <= hi.x && !sky; x += step.x)
		{
			sky = Depth.Load(int3(clamp(int2(x, y), int2(0, 0), limit), 0)) <= 0.0;
		}
	}
	if (!sky)
	{
		Clouds[id.xy] = float4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	const float3 ray = FeatureViewRay(uv, params.Forward, params.Right, params.Up);
	// A spatial dither that walks through its range over frames (golden ratio): with the
	// temporal pass every pixel sees evenly spread start offsets.
	const float jitter = frac(FeatureGradientNoise(float2(id.xy), 0u) + float(params.Size.w % 1024u) * 0.618034);
#endif
	Clouds[id.xy] = MarchClouds(params, ray, jitter);
}
#else
[[vk::binding(1, 0)]] Texture2D<float4> Color;
[[vk::binding(2, 0)]] Texture2D<float> Depth;
[[vk::binding(3, 0)]] Texture2D<float4> Clouds;
[[vk::binding(4, 0)]] SamplerState LinearClamp;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const CloudParams params = Params[0];
	if (id.x >= params.Output.x || id.y >= params.Output.y)
	{
		return;
	}
	float4 scene = Color.Load(int3(id.xy, 0));
	if (Depth.Load(int3(id.xy, 0)) <= 0.0)
	{
		const float2 uv = (float2(id.xy) + 0.5) / float2(params.Output.xy);
		const float4 clouds = Clouds.SampleLevel(LinearClamp, uv, 0.0);
		scene.rgb = scene.rgb * clouds.a + clouds.rgb;
	}
	Output[id.xy] = scene;
}
#endif
#endif
