// Fire, fireballs, smoke and heat distortion (Engine::PyroVolumes) on the linear HDR scene,
// before temporal anti-aliasing (which resolves the dithered march).
//
// Every volume is analytic: a bounding sphere around a procedural density (value-noise fbm
// advected upward). Per pixel, the volumes the view ray crosses in front of the scene are
// marched front to back with emission-absorption; fire emits a blackbody ramp of its
// temperature (optionally tinted), smoke is lit by the sun, the sky and the fire under it.
// Flames also throw embers: analytic points rising above them, drawn by their distance to
// the ray. Heat haze and shockwaves only bend the background: the scene behind is sampled
// at an offset (noise gradient for haze, radial for a shockwave shell), unless that pixel
// lies in front of the volume.
#include "Features/FeatureCommon.slang"

static const uint MaxVolumes = 64; // PyroVolumes::MaxVolumes.
static const uint MaxHits = 6;	   // Volumes marched per pixel (nearest first).

static const uint TypeFlame = 0u;
static const uint TypeFireball = 1u;
static const uint TypeSmoke = 2u;
static const uint TypeHaze = 3u;
static const uint TypeShockwave = 4u;

struct VolumeRecord
{
	float4 PositionRadius; // xyz: base (flame, smoke) or centre; w: bounding radius.
	float4 Shape;		   // x: type; y: height; z: radius; w: age 0..1.
	float4 Color;		   // rgb: tint; w: emission intensity.
	float4 Params;		   // x: density; y: rise speed; z: seed; w: distortion strength.
	float4 Extra;		   // x: tint mix; y: embers; z: smoke albedo; w: fire light on smoke.
};

struct PyroParams
{
	float4 Forward;	 // xyz; w: tan(half horizontal fov).
	float4 Right;	 // xyz; w: tan(half vertical fov).
	float4 Up;		 // xyz; w: near plane (depth = near / view z).
	float4 Camera;	 // xyz; w: volume count.
	float4 Misc;	 // x: time; y: radians per pixel; z: frame; w: march steps.
	float4 Sun;		 // xyz: direction toward the sun; w: ambient.
	float4 SunColor; // rgb; w: noise octaves.
	uint4 Size;
};

// PyroParams, then MaxVolumes VolumeRecords, as float4s: one struct of everything would exceed
// the 2048-byte structured buffer element limit of some targets.
[[vk::binding(0, 0)]] StructuredBuffer<float4> Params;

static const uint ParamsHeader = 8;	 // float4s of PyroParams.
static const uint VolumeStride = 5; // float4s of a VolumeRecord.

PyroParams LoadParams()
{
	PyroParams params;
	params.Forward = Params[0];
	params.Right = Params[1];
	params.Up = Params[2];
	params.Camera = Params[3];
	params.Misc = Params[4];
	params.Sun = Params[5];
	params.SunColor = Params[6];
	params.Size = asuint(Params[7]);
	return params;
}

VolumeRecord LoadVolume(uint index)
{
	const uint base = ParamsHeader + index * VolumeStride;
	VolumeRecord v;
	v.PositionRadius = Params[base];
	v.Shape = Params[base + 1];
	v.Color = Params[base + 2];
	v.Params = Params[base + 3];
	v.Extra = Params[base + 4];
	return v;
}
[[vk::binding(1, 0)]] Texture2D<float4> Color;
[[vk::binding(2, 0)]] Texture2D<float> Depth;
[[vk::binding(3, 0)]] SamplerState LinearClamp;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

float Lattice(int3 c)
{
	return FeatureHash01(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u);
}

float ValueNoise(float3 p)
{
	const float3 cell = floor(p);
	const float3 f = p - cell;
	const float3 s = f * f * (3.0 - 2.0 * f);
	const int3 c = int3(cell);
	const float x00 = lerp(Lattice(c), Lattice(c + int3(1, 0, 0)), s.x);
	const float x10 = lerp(Lattice(c + int3(0, 1, 0)), Lattice(c + int3(1, 1, 0)), s.x);
	const float x01 = lerp(Lattice(c + int3(0, 0, 1)), Lattice(c + int3(1, 0, 1)), s.x);
	const float x11 = lerp(Lattice(c + int3(0, 1, 1)), Lattice(c + int3(1, 1, 1)), s.x);
	return lerp(lerp(x00, x10, s.y), lerp(x01, x11, s.y), s.z);
}

float Fbm(float3 p, uint octaves)
{
	float sum = 0.0;
	float amplitude = 0.55;
	float total = 0.0;
	for (uint o = 0; o < octaves; ++o)
	{
		sum += ValueNoise(p) * amplitude;
		total += amplitude;
		p = p * 2.07 + float3(17.3, 5.1, 11.7);
		amplitude *= 0.5;
	}
	return sum / total;
}

// Temperature 0..1 to linear radiance: dark red, orange, yellow, white (a blackbody ramp,
// brightness rising steeply with temperature).
float3 Blackbody(float t)
{
	t = saturate(t);
	float3 c = float3(0.75, 0.06, 0.01) * smoothstep(0.0, 0.25, t);
	c = lerp(c, float3(1.0, 0.36, 0.05), smoothstep(0.2, 0.5, t));
	c = lerp(c, float3(1.0, 0.72, 0.3), smoothstep(0.45, 0.8, t));
	c = lerp(c, float3(1.0, 0.95, 0.85), smoothstep(0.8, 1.0, t));
	return c * (t * t * 6.0 + t * 0.4);
}

float3 FireColor(VolumeRecord v, float temperature)
{
	const float3 hot = Blackbody(temperature);
	const float3 tinted = v.Color.rgb * FeatureLuminance(hot) * 1.6;
	return lerp(hot, tinted, v.Extra.x) * v.Color.w;
}

struct Sample
{
	float3 Emission; // Per metre.
	float Extinction; // Per metre.
};

Sample Evaluate(VolumeRecord v, float3 world, float time, uint octaves, float3 sun, float3 sunColor, float3 ambient)
{
	Sample s;
	s.Emission = 0.0;
	s.Extinction = 0.0;
	const uint type = uint(v.Shape.x);
	const float seed = v.Params.z * 31.7;
	const float height = max(v.Shape.y, 1.0e-3);
	const float radius = max(v.Shape.z, 1.0e-3);
	if (type == TypeFlame)
	{
		const float3 p = world - v.PositionRadius.xyz;
		const float h = p.y / height;
		if (h < -0.05 || h > 1.0)
		{
			return s;
		}
		const float rise = time * v.Params.y;
		const float3 q = float3(p.x / radius * 1.4, p.y / height * 2.2 - rise, p.z / radius * 1.4) + seed;
		const float n = Fbm(q, octaves);
		// Licks: noise pushes the outline in and out, more toward the tip.
		const float profile = radius * pow(saturate(1.0 - h), 0.65) * (0.55 + 0.9 * n);
		const float r = length(p.xz + (n - 0.5) * radius * 0.5 * h);
		const float body = saturate((profile - r) / (radius * 0.35)) * saturate(h * 12.0 + 0.6);
		const float temperature = body * (1.0 - h * 0.55) * (0.65 + 0.55 * n);
		s.Emission = FireColor(v, temperature) * body * v.Params.x * 3.0;
		s.Extinction = body * v.Params.x * 0.4;
		return s;
	}
	if (type == TypeFireball)
	{
		const float3 p = world - v.PositionRadius.xyz;
		const float age = v.Shape.w;
		const float3 q = p / radius * 1.6 + float3(0.0, -time * v.Params.y, 0.0) + seed;
		const float n = Fbm(q, octaves);
		const float d = length(p) / radius;
		const float body = saturate((0.7 + 0.45 * n - d) * 4.0);
		// The core burns out from the outside in; what is left is smoke.
		const float heat = saturate(1.0 - age * 1.3) * saturate(1.15 - d * (0.6 + age)) * (0.6 + 0.6 * n);
		const float3 smoke = v.Extra.z * (ambient + sunColor * (0.35 + 0.65 * saturate(dot(normalize(p + 1.0e-4), sun))));
		const float smokiness = saturate(age * 1.4 + 0.15);
		// Fades out over the last 40% of its life instead of vanishing.
		const float density = body * v.Params.x * saturate((1.0 - age) / 0.4);
		s.Emission = (FireColor(v, heat) * 3.0 + smoke * smokiness * 1.2) * density;
		s.Extinction = density * lerp(0.35, 1.3, smokiness);
		return s;
	}
	if (type == TypeSmoke)
	{
		const float3 p = world - v.PositionRadius.xyz;
		const float h = p.y / height;
		if (h < 0.0 || h > 1.0)
		{
			return s;
		}
		const float rise = time * v.Params.y;
		const float widen = lerp(0.45, 1.0, sqrt(h));
		const float3 q = float3(p.x / radius * 1.2, p.y / height * 3.0 - rise, p.z / radius * 1.2) + seed;
		const float n = Fbm(q, octaves);
		const float drift = (n - 0.5) * radius * 0.6;
		const float r = length(p.xz + drift * h) / (radius * widen);
		const float body = saturate((0.75 + 0.5 * n - r) * 3.0) * smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.6, h);
		const float density = body * v.Params.x * saturate(1.0 - v.Shape.w);
		// Lit from above and by the fire below it.
		const float lit = saturate(0.35 + 0.65 * h + (n - 0.5) * 0.6);
		const float3 albedo = v.Extra.z;
		const float3 glow = v.Color.rgb * v.Extra.w * pow(saturate(1.0 - h * 1.6), 3.0);
		s.Emission = (albedo * (ambient + sunColor * lit * max(sun.y, 0.15)) + glow) * density;
		s.Extinction = density * 1.8;
		return s;
	}
	return s;
}

// The [enter, exit] distances of the ray through a sphere, empty when enter >= exit.
float2 SphereSpan(float3 origin, float3 ray, float3 centre, float radius)
{
	const float3 oc = origin - centre;
	const float b = dot(oc, ray);
	const float c = dot(oc, oc) - radius * radius;
	const float disc = b * b - c;
	if (disc <= 0.0)
	{
		return float2(1.0, 0.0);
	}
	const float root = sqrt(disc);
	return float2(max(-b - root, 0.0), -b + root);
}

float3 BoundCentre(VolumeRecord v)
{
	const uint type = uint(v.Shape.x);
	return type == TypeFlame || type == TypeSmoke ? v.PositionRadius.xyz + float3(0.0, v.Shape.y * 0.5, 0.0) : v.PositionRadius.xyz;
}

float2 ScreenUv(PyroParams params, float3 world)
{
	const float3 v = world - params.Camera.xyz;
	const float z = max(dot(v, params.Forward.xyz), 1.0e-3);
	const float x = dot(v, params.Right.xyz) / (z * params.Forward.w);
	const float y = dot(v, params.Up.xyz) / (z * params.Right.w);
	return float2(x * 0.5 + 0.5, 0.5 - y * 0.5);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const PyroParams params = LoadParams();
	if (id.x >= params.Size.x || id.y >= params.Size.y)
	{
		return;
	}
	const float2 size = float2(params.Size.xy);
	const float2 uv = (float2(id.xy) + 0.5) / size;
	const float3 origin = params.Camera.xyz;
	const float3 ray = FeatureViewRay(uv, params.Forward, params.Right, params.Up);
	const float near = params.Up.w;
	const float depth = Depth[id.xy];
	const float sceneDistance = depth > 0.0 ? near / depth / max(dot(ray, params.Forward.xyz), 1.0e-3) : 1.0e9;
	const float time = params.Misc.x;
	const uint count = min(uint(params.Camera.w), MaxVolumes);

	// Pass 1: what the ray crosses. Distortion is analytic (closest approach to each volume);
	// marched volumes are kept nearest first.
	uint hits[MaxHits];
	float2 spans[MaxHits];
	for (uint k = 0; k < MaxHits; ++k)
	{
		hits[k] = 0u;
		spans[k] = float2(1.0e9, 1.0e9);
	}
	uint hitCount = 0;
	float2 distortion = 0.0;
	float distortionNear = 1.0e9;
	for (uint i = 0; i < count; ++i)
	{
		const VolumeRecord v = LoadVolume(i);
		const uint type = uint(v.Shape.x);
		const float3 centre = BoundCentre(v);
		const float2 span = SphereSpan(origin, ray, centre, v.PositionRadius.w);
		if (span.x >= span.y || span.x >= sceneDistance)
		{
			continue;
		}
		const float along = clamp(dot(centre - origin, ray), span.x, min(span.y, sceneDistance));
		const float3 closest = origin + ray * along;
		const float strength = v.Params.w / max(along, 0.5);
		if (strength > 0.0)
		{
			if (type == TypeShockwave)
			{
				const float radius = max(v.Shape.z, 1.0e-3);
				const float miss = length(cross(centre - origin, ray));
				const float shell = exp(-pow((miss - radius) / (radius * 0.12 + 0.05), 2.0)) * saturate(1.0 - v.Shape.w);
				const float2 away = uv - ScreenUv(params, centre);
				distortion += away / max(length(away), 1.0e-4) * shell * strength * 0.02;
			}
			else
			{
				// Haze: flames and lava shimmer above themselves, haze volumes everywhere inside.
				const float3 hazeCentre = type == TypeFlame ? v.PositionRadius.xyz + float3(0.0, v.Shape.y * 0.85, 0.0) : centre;
				const float hazeRadius = type == TypeFlame ? v.Shape.z * 1.8 : v.PositionRadius.w;
				const float3 offset = (closest - hazeCentre) * float3(1.0, type == TypeHaze ? v.Shape.z / max(v.Shape.y, 1.0e-3) : 1.0, 1.0);
				const float weight = pow(saturate(1.0 - length(offset) / max(hazeRadius, 1.0e-3)), 2.0);
				const float3 q = closest * 2.5 + float3(0.0, -time * 2.2, 0.0);
				const float2 wobble = float2(ValueNoise(q), ValueNoise(q + 19.7)) - 0.5;
				distortion += wobble * weight * strength * 0.012;
			}
			distortionNear = min(distortionNear, span.x);
		}
		if (type > TypeSmoke)
		{
			continue;
		}
		// Insert by entry distance; past MaxHits the farthest is dropped.
		uint slot = min(hitCount, MaxHits - 1u);
		if (hitCount == MaxHits && span.x >= spans[slot].x)
		{
			continue;
		}
		while (slot > 0u && spans[slot - 1u].x > span.x)
		{
			hits[slot] = hits[slot - 1u];
			spans[slot] = spans[slot - 1u];
			--slot;
		}
		hits[slot] = i;
		spans[slot] = float2(span.x, min(span.y, sceneDistance));
		hitCount = min(hitCount + 1u, MaxHits);
	}

	// The background, bent by heat, unless the bent-to pixel is in front of the volumes.
	float3 background = Color[id.xy].rgb;
	if (any(distortion != 0.0))
	{
		const float2 bent = clamp(uv + distortion, 0.0, 1.0);
		const float bentDepth = Depth.SampleLevel(LinearClamp, bent, 0.0);
		const float bentDistance = bentDepth > 0.0 ? near / bentDepth : 1.0e9;
		if (bentDistance > distortionNear)
		{
			background = Color.SampleLevel(LinearClamp, bent, 0.0).rgb;
		}
	}

	if (hitCount == 0u && all(distortion == 0.0))
	{
		Output[id.xy] = float4(background, 1.0);
		return;
	}

	// Pass 2: emission-absorption, front to back.
	const uint steps = max(uint(params.Misc.w), 4u);
	const uint octaves = clamp(uint(params.SunColor.w), 1u, 4u);
	const float jitter = FeatureGradientNoise(float2(id.xy), uint(params.Misc.z));
	const float3 sun = params.Sun.xyz;
	const float3 sunColor = params.SunColor.rgb;
	const float3 ambient = params.Sun.w * (sunColor * 0.25 + 0.04);
	float3 radiance = 0.0;
	float transmittance = 1.0;
	for (uint h = 0; h < hitCount; ++h)
	{
		const VolumeRecord v = LoadVolume(hits[h]);
		const float2 span = spans[h];
		const float dt = (span.y - span.x) / float(steps);
		for (uint s = 0; s < steps; ++s)
		{
			const float t = span.x + (float(s) + jitter) * dt;
			Sample sample = Evaluate(v, origin + ray * t, time, octaves, sun, sunColor, ambient);
			// Up close the medium thins out, so the camera can see through a blast it is in.
			const float closeness = smoothstep(0.5, 6.0, t);
			sample.Extinction *= closeness;
			sample.Emission *= lerp(0.35, 1.0, closeness);
			if (sample.Extinction <= 0.0 && all(sample.Emission <= 0.0))
			{
				continue;
			}
			const float absorbed = exp(-sample.Extinction * dt);
			// Energy-conserving integration of the emission over the step.
			const float3 emitted = sample.Extinction > 1.0e-4 ? sample.Emission / sample.Extinction * (1.0 - absorbed) : sample.Emission * dt;
			radiance += transmittance * emitted;
			transmittance *= absorbed;
		}
		if (transmittance < 0.01)
		{
			break;
		}
	}

	// Embers: points rising above each flame, drawn by their distance to the ray.
	const float pixelAngle = params.Misc.y;
	for (uint h = 0; h < hitCount; ++h)
	{
		const VolumeRecord v = LoadVolume(hits[h]);
		const uint embers = uint(v.Extra.y);
		if (uint(v.Shape.x) != TypeFlame || embers == 0u)
		{
			continue;
		}
		const uint seed = uint(v.Params.z * 4096.0);
		for (uint e = 0; e < embers; ++e)
		{
			const float r0 = FeatureHash01(seed * 131u + e * 7u);
			const float r1 = FeatureHash01(seed * 131u + e * 7u + 1u);
			const float r2 = FeatureHash01(seed * 131u + e * 7u + 2u);
			const float life = frac(time * (0.35 + 0.4 * r2) + r0);
			const float rise = v.Shape.y * (0.3 + life * 2.4);
			const float swirl = time * (1.5 + r1) + r0 * 6.2832;
			const float spread = v.Shape.z * (0.25 + 0.6 * r1) * (1.0 + life);
			const float3 point = v.PositionRadius.xyz + float3(cos(swirl) * spread * (r0 - 0.5) * 2.0, rise, sin(swirl * 0.7) * spread * (r1 - 0.5) * 2.0);
			const float along = dot(point - origin, ray);
			if (along <= 0.0 || along >= sceneDistance)
			{
				continue;
			}
			const float miss = length(cross(point - origin, ray));
			const float radius = max(0.018 * v.Shape.z, pixelAngle * along * 0.9);
			const float fade = pow(1.0 - life, 2.0) * smoothstep(0.0, 0.05, life);
			// A point smaller than a pixel keeps its energy: brightness falls as it widens.
			const float energy = 0.018 * v.Shape.z / radius;
			radiance += FireColor(v, 0.85) * exp(-pow(miss / radius, 2.0)) * fade * energy * 1.5;
		}
	}

	Output[id.xy] = float4(background * transmittance + radiance, 1.0);
}
