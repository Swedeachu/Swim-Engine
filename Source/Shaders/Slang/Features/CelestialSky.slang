// Sun, moon and night sky (Engine::CelestialSky), two programs from one source:
//  - CelestialSkyBake (CELESTIAL_BAKE=1): runs once. Bakes the deep sky into a 2048 x 2048
//    texture of two equirectangular maps in galactic coordinates (longitude 0 at the
//    galactic centre, the band along the equator): rows 0..1023 the galaxy band (star
//    clouds, a bright bulge, dust lanes, unresolved faint stars), rows 1024.. a few
//    nebulae and distant galaxies.
//  - CelestialSky (default): over the sky pixels (depth 0) of the HDR scene, adds the sun
//    disc (limb-darkened), the moon (lit at its phase, earthshine on the dark side, covering
//    what lies behind it), procedural stars (cells on a cube over the celestial sphere: one
//    possible star per cell, magnitude and temperature from its hash, a 3x3 cell search,
//    twinkle and colour scintillation that grow with airmass), the baked deep sky and
//    meteors, all dimmed by the atmosphere's extinction toward the horizon.
#include "Features/FeatureCommon.slang"

#ifndef CELESTIAL_BAKE
#define CELESTIAL_BAKE 0
#endif

#define CELESTIAL_PI 3.14159265
#define CELESTIAL_BAKE_WIDTH 2048
#define CELESTIAL_BAKE_HALF 1024

float CelestialHash3(float3 p)
{
	const uint3 q = uint3(int3(floor(p)) + 32768);
	return FeatureHash01(q.x * 73856093u ^ FeatureHash(q.y * 19349663u ^ FeatureHash(q.z * 83492791u)));
}

// Value noise over a 3D point, smoothly interpolated.
float CelestialNoise(float3 p)
{
	const float3 i = floor(p);
	const float3 f = p - i;
	const float3 u = f * f * (3.0 - 2.0 * f);
	const float n000 = CelestialHash3(i);
	const float n100 = CelestialHash3(i + float3(1, 0, 0));
	const float n010 = CelestialHash3(i + float3(0, 1, 0));
	const float n110 = CelestialHash3(i + float3(1, 1, 0));
	const float n001 = CelestialHash3(i + float3(0, 0, 1));
	const float n101 = CelestialHash3(i + float3(1, 0, 1));
	const float n011 = CelestialHash3(i + float3(0, 1, 1));
	const float n111 = CelestialHash3(i + float3(1, 1, 1));
	return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y), lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

float CelestialFbm(float3 p, uint octaves)
{
	float sum = 0.0;
	float amplitude = 0.5;
	float total = 0.0;
	for (uint o = 0; o < octaves; ++o)
	{
		sum += CelestialNoise(p) * amplitude;
		total += amplitude;
		p = p * 2.03 + float3(17.1, 5.3, 11.7);
		amplitude *= 0.5;
	}
	return sum / total;
}

// Equirectangular texel <-> direction in galactic coordinates (y = galactic north).
float3 CelestialFromEquirect(float2 uv)
{
	const float longitude = (uv.x - 0.5) * 2.0 * CELESTIAL_PI;
	const float latitude = (0.5 - uv.y) * CELESTIAL_PI;
	return float3(cos(latitude) * sin(longitude), sin(latitude), cos(latitude) * cos(longitude));
}

float2 CelestialToEquirect(float3 d)
{
	return float2(atan2(d.x, d.z) / (2.0 * CELESTIAL_PI) + 0.5, 0.5 - asin(clamp(d.y, -1.0, 1.0)) / CELESTIAL_PI);
}

#if CELESTIAL_BAKE
[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> DeepSky;

float3 GalaxyBand(float3 d)
{
	const float latitude = asin(clamp(d.y, -1.0, 1.0));
	const float longitude = atan2(d.x, d.z);
	// Star clouds: a thin disc, thicker and much brighter in the bulge around longitude 0.
	const float bulge = exp(-(longitude * longitude) / 0.35 - latitude * latitude / 0.04);
	const float clouds = CelestialFbm(d * 7.0, 6u);
	const float fine = CelestialFbm(d * 40.0 + 3.0, 4u);
	const float thickness = 0.1 + 0.06 * (CelestialFbm(d * 2.0 + 9.0, 3u) - 0.5);
	const float disc = exp(-pow(latitude / thickness, 2.0));
	float light = disc * (0.25 + 0.75 * saturate(clouds * 1.6 - 0.35)) * (0.6 + 0.4 * fine) + bulge * (0.8 + 0.6 * clouds);
	// Dust lanes: a dark, ragged rift along the plane, splitting the band toward the bulge.
	const float laneOffset = 0.025 * (CelestialFbm(d * 5.0 + 21.0, 4u) - 0.5) * 4.0;
	const float lane = exp(-pow((latitude - laneOffset) / 0.035, 2.0)) * saturate(CelestialFbm(d * 12.0 + 41.0, 5u) * 2.2 - 0.55);
	const float patches = saturate(CelestialFbm(d * 18.0 + 77.0, 5u) * 2.0 - 0.9) * disc;
	light *= saturate(1.0 - lane * 0.95 - patches * 0.6);
	// Warm bulge, blue-white arms, a few pink emission knots.
	const float3 arms = float3(0.72, 0.8, 1.0);
	const float3 core = float3(1.0, 0.78, 0.5);
	float3 color = lerp(arms, core, saturate(bulge * 2.0)) * light;
	const float knots = pow(saturate(CelestialFbm(d * 25.0 + 5.0, 4u) * 2.0 - 1.05), 2.0) * disc;
	color += float3(1.0, 0.3, 0.4) * knots * 0.25;
	// Unresolved stars: a soft mottling, densest in the band.
	const float mottle = CelestialFbm(d * 90.0 + 13.0, 3u);
	return color * (0.75 + 0.5 * mottle);
}

float3 DeepObjects(float3 d)
{
	float3 color = float3(0.0, 0.0, 0.0);
	for (uint i = 0; i < 10u; ++i)
	{
		// Object i: a direction away from the band (hashes), a size, a type.
		const float h0 = FeatureHash01(i * 7919u + 1u);
		const float h1 = FeatureHash01(i * 7919u + 2u);
		const float h2 = FeatureHash01(i * 7919u + 3u);
		const float h3 = FeatureHash01(i * 7919u + 4u);
		const float latitude = (h0 * 2.0 - 1.0) * 1.3;
		const float longitude = h1 * 2.0 * CELESTIAL_PI;
		const float3 center = float3(cos(latitude) * sin(longitude), sin(latitude), cos(latitude) * cos(longitude));
		const float3 east = normalize(cross(float3(0.0, 1.0, 0.0), center + float3(1.0e-4, 0.0, 0.0)));
		const float3 north = cross(center, east);
		const float2 p = float2(dot(d, east), dot(d, north));
		if (dot(d, center) < 0.99)
		{
			continue;
		}
		if (i < 6u)
		{
			// A nebula: a cloud of glowing gas, ragged by noise, in emission red, reflection
			// blue or oxygen teal.
			const float size = 0.012 + 0.03 * h2;
			const float wisps = CelestialFbm(d * (60.0 / size * 0.02) + float(i) * 13.0, 5u);
			const float shape = exp(-dot(p, p) / (size * size)) * saturate(wisps * 2.0 - 0.5);
			const float3 tint = h3 < 0.5 ? float3(1.0, 0.3, 0.4) : h3 < 0.8 ? float3(0.35, 0.55, 1.0) : float3(0.3, 0.9, 0.85);
			color += tint * shape * 0.8;
		}
		else
		{
			// A distant galaxy: a tilted ellipse with a bright core.
			const float size = 0.004 + 0.012 * h2;
			const float angle = h3 * CELESTIAL_PI;
			const float2 q = float2(p.x * cos(angle) + p.y * sin(angle), -p.x * sin(angle) + p.y * cos(angle)) / size;
			const float r = length(q * float2(1.0, 2.5 + 2.0 * h0));
			color += float3(0.95, 0.9, 1.0) * (exp(-r * 3.0) * 0.7 + exp(-r * 18.0) * 2.0);
		}
	}
	return color;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= CELESTIAL_BAKE_WIDTH || id.y >= CELESTIAL_BAKE_WIDTH)
	{
		return;
	}
	const bool band = id.y < CELESTIAL_BAKE_HALF;
	const float2 uv = (float2(id.x, id.y % CELESTIAL_BAKE_HALF) + 0.5) / float2(CELESTIAL_BAKE_WIDTH, CELESTIAL_BAKE_HALF);
	const float3 d = CelestialFromEquirect(uv);
	DeepSky[id.xy] = float4(band ? GalaxyBand(d) : DeepObjects(d), 1.0);
}
#else

struct CelestialParams
{
	float4 Forward;		   // w: tan(half horizontal fov).
	float4 Right;		   // w: tan(half vertical fov).
	float4 Up;			   // w: time (seconds).
	float4 SunDirection;   // Toward the sun; w: cosine of its angular radius.
	float4 SunColor;	   // Disc radiance at its centre; w: limb darkening (0..1).
	float4 MoonDirection;  // Toward the moon; w: cosine of its angular radius.
	float4 MoonLight;	   // Toward the sun from the moon (its phase); w: radiance of the lit disc.
	float4 MoonParams;	   // x: earthshine; y: visibility (atmosphere and weather); z: 1 = drawn; w: 1 = sun drawn.
	float4 WorldToStar[3]; // Rows: world direction -> celestial sphere.
	float4 StarToGalaxy[3];
	float4 Stars;		   // x: cells per cube face; y: star probability per cell; z: brightness; w: twinkle.
	float4 Night;		   // x: night (0 day .. 1 dark); y: visibility; z: galaxy intensity; w: nebula intensity.
	float4 MeteorStart[4]; // xyz: direction; w: progress (0..1).
	float4 MeteorEnd[4];   // xyz: direction; w: brightness (0 = none).
	uint4 Size;			   // xy: viewport.
};

[[vk::binding(0, 0)]] StructuredBuffer<CelestialParams> Params;
[[vk::binding(2, 0)]] Texture2D<float> Depth;
[[vk::binding(3, 0)]] Texture2D<float4> DeepSky;
[[vk::binding(4, 0)]] SamplerState LinearRepeat;
// The scene colour, updated in place: pixels behind geometry, below the horizon or with nothing
// added are neither read nor written.
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

// Blackbody colour (normalized to its brightest channel) for 2000..15000 K, a fit to the
// Planckian locus.
float3 CelestialTemperature(float kelvin)
{
	const float t = kelvin / 100.0;
	float3 c;
	c.r = t <= 66.0 ? 1.0 : saturate(1.292936 * pow(t - 60.0, -0.1332047));
	c.g = t <= 66.0 ? saturate(0.3900816 * log(t) - 0.6318414) : saturate(1.1298909 * pow(t - 60.0, -0.0755148));
	c.b = t >= 66.0 ? 1.0 : t <= 19.0 ? 0.0 : saturate(0.5432068 * log(t - 10.0) - 1.1962541);
	return c / max(max(c.r, c.g), max(c.b, 1.0e-3));
}

// A direction's cube face (0..5) and position on it (-1..1).
uint CelestialCubeFace(float3 d, out float2 uv)
{
	const float3 a = abs(d);
	if (a.x >= a.y && a.x >= a.z)
	{
		uv = float2(d.z, d.y) / a.x * float2(d.x > 0.0 ? -1.0 : 1.0, 1.0);
		return d.x > 0.0 ? 0u : 1u;
	}
	if (a.y >= a.z)
	{
		uv = float2(d.x, d.z) / a.y * float2(1.0, d.y > 0.0 ? -1.0 : 1.0);
		return d.y > 0.0 ? 2u : 3u;
	}
	uv = float2(d.x, d.y) / a.z * float2(d.z > 0.0 ? 1.0 : -1.0, 1.0);
	return d.z > 0.0 ? 4u : 5u;
}

float3 CelestialCubeDirection(uint face, float2 uv)
{
	switch (face)
	{
	case 0u: return normalize(float3(1.0, uv.y, -uv.x));
	case 1u: return normalize(float3(-1.0, uv.y, uv.x));
	case 2u: return normalize(float3(uv.x, 1.0, -uv.y));
	case 3u: return normalize(float3(uv.x, -1.0, uv.y));
	case 4u: return normalize(float3(uv.x, uv.y, 1.0));
	default: return normalize(float3(-uv.x, uv.y, -1.0));
	}
}

// Stars around direction d (celestial sphere): each cube cell may hold one star, jittered
// inside it; the 3x3 cells around d are searched. `spread` is the angular size of a pixel.
float3 CelestialStars(CelestialParams params, float3 d, float spread, float airmass)
{
	float2 uv;
	const uint face = CelestialCubeFace(d, uv);
	const float cells = params.Stars.x;
	const float2 cell = floor((uv * 0.5 + 0.5) * cells);
	float3 total = float3(0.0, 0.0, 0.0);
	const float time = params.Up.w;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const float2 c = cell + float2(x, y);
			if (any(c < 0.0) || any(c >= cells))
			{
				continue;
			}
			const uint seed = FeatureHash(uint(c.x) + uint(c.y) * 4099u + face * 16785407u);
			if (FeatureHash01(seed) > params.Stars.y)
			{
				continue;
			}
			const float2 jitter = float2(FeatureHash01(seed ^ 0x68bc21ebu), FeatureHash01(seed ^ 0x02e5be93u));
			const float3 star = CelestialCubeDirection(face, ((c + 0.1 + 0.8 * jitter) / cells) * 2.0 - 1.0);
			const float distance = length(d - star); // ~ the angle, for small angles.
			const float sigma = spread * 0.65;
			const float shape = exp(-distance * distance / (2.0 * sigma * sigma));
			if (shape < 1.0e-3)
			{
				continue;
			}
			// Magnitudes: many faint stars, a few bright ones (a steep power law).
			const float m = FeatureHash01(seed ^ 0x9e3779b9u);
			const float flux = 0.012 + 0.25 * pow(m, 8.0) + 5.0 * pow(m, 60.0);
			const float kelvin = lerp(3200.0, 12000.0, pow(FeatureHash01(seed ^ 0x7f4a7c15u), 0.8));
			// Twinkle: turbulence scintillation, much stronger through more air, and slightly
			// different per colour near the horizon.
			const float phase = FeatureHash01(seed ^ 0x1b873593u) * 100.0;
			const float strength = params.Stars.w * saturate(0.08 + airmass * 0.06);
			const float3 wobble = float3(sin(time * 13.0 + phase), sin(time * 17.0 + phase * 1.3), sin(time * 11.0 + phase * 0.7)) *
				float3(1.0, 0.8, 0.6) + sin(time * 29.0 + phase * 2.1);
			const float3 twinkle = max(1.0 + strength * wobble * lerp(float3(0.5, 0.5, 0.5), float3(1.0, 0.6, 1.2), saturate(airmass / 8.0)), 0.0);
			total += CelestialTemperature(kelvin) * flux * shape * twinkle;
		}
	}
	return total * params.Stars.z;
}

// A meteor's streak: its head moves from start to end over its life, trailing a fading tail.
float CelestialMeteor(float3 d, float3 start, float3 end, float progress, float spread)
{
	const float3 axis = normalize(cross(start, end));
	const float off = abs(dot(d, axis));
	if (off > spread * 4.0)
	{
		return 0.0;
	}
	const float arc = acos(clamp(dot(start, end), -1.0, 1.0));
	const float3 flat = normalize(d - axis * dot(d, axis));
	const float along = atan2(dot(cross(start, flat), axis), dot(start, flat)) / max(arc, 1.0e-4);
	const float tail = 0.35;
	const float behind = progress - along;
	if (behind < 0.0 || behind > tail || along < 0.0)
	{
		return 0.0;
	}
	const float width = exp(-off * off / (2.0 * spread * spread * 0.5));
	const float fade = sin(progress * CELESTIAL_PI);
	return width * pow(1.0 - behind / tail, 2.0) * fade;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const CelestialParams params = Params[0];
	if (id.x >= params.Size.x || id.y >= params.Size.y)
	{
		return;
	}
	if (Depth.Load(int3(id.xy, 0)) > 0.0)
	{
		return;
	}
	const float2 uv = (float2(id.xy) + 0.5) / float2(params.Size.xy);
	const float3 ray = FeatureViewRay(uv, params.Forward, params.Right, params.Up);
	// Below the horizon only the ground and haze are seen.
	const float horizon = saturate(ray.y * 60.0 + 1.0);
	if (horizon <= 0.0)
	{
		return;
	}
	const float spread = 2.0 * params.Right.w / float(params.Size.y);
	// Airmass (Kasten-Young) and the extinction it brings: dimmer and redder toward the horizon.
	const float elevation = max(ray.y, 0.0);
	const float airmass = 1.0 / (elevation + 0.50572 * pow(degrees(asin(elevation)) + 6.07995, -1.6364));
	const float3 extinction = exp(-airmass * float3(0.06, 0.1, 0.2)) * horizon;
	float3 added = float3(0.0, 0.0, 0.0);
	float behindMoon = 1.0;

	// The moon: a sphere lit from MoonLight, Lommel-Seeliger (flat, like the real one),
	// with darker maria, and earthshine on its night side.
	if (params.MoonParams.z > 0.0)
	{
		const float3 m = params.MoonDirection.xyz;
		const float c = dot(ray, m);
		const float sinRadius = sqrt(max(1.0 - params.MoonDirection.w * params.MoonDirection.w, 1.0e-8));
		if (c > params.MoonDirection.w - spread * 2.0)
		{
			const float3 east = normalize(cross(float3(0.0, 1.0, 0.0), m + float3(1.0e-4, 0.0, 0.0)));
			const float3 north = cross(m, east);
			const float2 p = float2(dot(ray, east), dot(ray, north)) / sinRadius;
			const float r = length(p);
			const float edge = saturate((1.0 - r) * sinRadius / spread + 0.5);
			if (edge > 0.0)
			{
				const float2 q = r > 0.999 ? p * (0.999 / r) : p;
				const float3 normal = east * q.x + north * q.y - m * sqrt(1.0 - dot(q, q));
				const float mu = saturate(dot(normal, -m));
				const float mu0 = dot(normal, params.MoonLight.xyz);
				const float lit = mu0 > 0.0 ? mu0 / (mu0 + mu + 1.0e-3) * 2.0 : 0.0;
				const float3 surface = float3(q * 3.0, 1.0);
				const float maria = saturate(CelestialFbm(surface * 1.7 + 4.0, 4u) * 2.2 - 0.7);
				const float craters = CelestialFbm(surface * 9.0, 3u);
				const float albedo = (1.0 - 0.45 * maria) * (0.85 + 0.3 * craters);
				const float3 tint = float3(1.0, 0.97, 0.92);
				const float3 moon = tint * albedo * params.MoonLight.w * (lit + params.MoonParams.x);
				added += moon * edge * extinction * params.MoonParams.y;
				behindMoon = 1.0 - edge;
			}
		}
	}

	// The sun's disc, limb-darkened (I(mu) = 1 - u (1 - mu), stronger in blue).
	if (params.MoonParams.w > 0.0)
	{
		const float c = dot(ray, params.SunDirection.xyz);
		const float sinRadius = sqrt(max(1.0 - params.SunDirection.w * params.SunDirection.w, 1.0e-8));
		if (c > params.SunDirection.w - spread * 2.0)
		{
			const float r = sqrt(max(1.0 - c * c, 0.0)) / sinRadius;
			const float edge = saturate((1.0 - r) * sinRadius / spread + 0.5);
			const float mu = sqrt(saturate(1.0 - r * r));
			const float3 u = params.SunColor.w * float3(0.75, 0.95, 1.2);
			added += params.SunColor.rgb * saturate(1.0 - u * (1.0 - mu)) * edge * behindMoon * horizon;
		}
	}

	// The night sky, behind the moon.
	const float night = params.Night.x * params.Night.y * behindMoon;
	if (night > 0.0)
	{
		const float3 star = float3(dot(params.WorldToStar[0].xyz, ray), dot(params.WorldToStar[1].xyz, ray), dot(params.WorldToStar[2].xyz, ray));
		float3 sky = CelestialStars(params, star, spread, airmass);
		const float3 galactic =
			float3(dot(params.StarToGalaxy[0].xyz, star), dot(params.StarToGalaxy[1].xyz, star), dot(params.StarToGalaxy[2].xyz, star));
		const float2 deep = CelestialToEquirect(galactic);
		const float3 band = DeepSky.SampleLevel(LinearRepeat, float2(deep.x, deep.y * 0.5), 0.0).rgb;
		const float3 objects = DeepSky.SampleLevel(LinearRepeat, float2(deep.x, deep.y * 0.5 + 0.5), 0.0).rgb;
		sky += (band * params.Night.z + objects * params.Night.w) * params.Stars.z * 0.12;
		for (uint i = 0; i < 4u; ++i)
		{
			if (params.MeteorEnd[i].w > 0.0)
			{
				sky += float3(0.85, 1.0, 0.9) * CelestialMeteor(ray, params.MeteorStart[i].xyz, params.MeteorEnd[i].xyz, params.MeteorStart[i].w, spread) *
					params.MeteorEnd[i].w;
			}
		}
		added += sky * extinction * night;
	}
	if (any(added != 0.0))
	{
		const float4 scene = Output[id.xy];
		Output[id.xy] = float4(scene.rgb + added, scene.a);
	}
}
#endif
