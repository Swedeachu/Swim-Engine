// Gerstner waves and continuous water LOD (Engine::WaterWaveSet, Engine::WaterLodSettings in
// Engine/Systems/Water/WaterWaves.h; the CPU evaluates the same sums for physics).
#ifndef SWIM_WATER_WAVES_SLANG
#define SWIM_WATER_WAVES_SLANG

static const uint WaterMaxWaves = 8u;

struct WaterWaveGpu
{
	float4 DirectionNumber; // xy: direction, z: wave number (2 pi / wavelength), w: horizontal amplitude.
	float4 AmplitudePhase;	// x: amplitude, y: phase at this frame's time, z: detail rank 0..1, w: unused.
};

struct WaterLodGpu
{
	float4 Fade0; // x: detail start, y: detail 1 / range, z: amplitude start, w: amplitude 1 / range.
	float4 Fade1; // x: far amplitude, y: normal start, z: normal 1 / range, w: far roughness.
	float4 Fade2; // x: reflection start, y: reflection 1 / range, z: enabled, w: debug tint.
};

struct WaterLodFactorsGpu
{
	float Detail;
	float Amplitude;
	float Normal;
	float Reflection;
};

// 1 before start, 0 after the end, smooth between (an infinite 1 / range is a hard cut).
float WaterLodFade(float distance, float start, float inverseRange)
{
	const float t = saturate((distance - start) * inverseRange);
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

WaterLodFactorsGpu WaterEvaluateLod(WaterLodGpu lod, float distance)
{
	WaterLodFactorsGpu f;
	if (lod.Fade2.z == 0.0)
	{
		f.Detail = 1.0;
		f.Amplitude = 1.0;
		f.Normal = 1.0;
		f.Reflection = 1.0;
		return f;
	}
	f.Detail = WaterLodFade(distance, lod.Fade0.x, lod.Fade0.y);
	f.Amplitude = lod.Fade1.x + (1.0 - lod.Fade1.x) * WaterLodFade(distance, lod.Fade0.z, lod.Fade0.w);
	f.Normal = WaterLodFade(distance, lod.Fade1.y, lod.Fade1.z);
	f.Reflection = WaterLodFade(distance, lod.Fade2.x, lod.Fade2.y);
	return f;
}

// A wave's weight: the LOD (Amplitude x lerp(1, Detail, rank)) times an anti-alias fade
// that removes waves shorter than about 4 x the sampling distance (grid spacing or pixel).
float WaterWaveWeight(WaterWaveGpu wave, WaterLodFactorsGpu lod, float sampling)
{
	const float wavelength = 6.28318531 / wave.DirectionNumber.z;
	const float resolved = saturate(wavelength / max(sampling * 4.0, 1.0e-4) - 1.0);
	return lod.Amplitude * lerp(1.0, lod.Detail, wave.AmplitudePhase.z) * resolved;
}

float WaterWaveAngle(WaterWaveGpu wave, float2 rest)
{
	return wave.DirectionNumber.z * dot(wave.DirectionNumber.xy, rest) + wave.AmplitudePhase.y;
}

// Displacement of the rest point (xz horizontal, y vertical).
float3 WaterWaveDisplacement(WaterWaveGpu wave, float2 rest, float weight)
{
	const float angle = WaterWaveAngle(wave, rest);
	const float c = cos(angle);
	return float3(wave.DirectionNumber.x * wave.DirectionNumber.w * c, wave.AmplitudePhase.x * sin(angle),
		wave.DirectionNumber.y * wave.DirectionNumber.w * c) * weight;
}

// Adds the wave's derivatives of the displaced position by rest x (dx) and rest z (dz).
void WaterWaveTangents(WaterWaveGpu wave, float2 rest, float weight, inout float3 dx, inout float3 dz)
{
	const float angle = WaterWaveAngle(wave, rest);
	const float s = sin(angle) * weight;
	const float c = cos(angle) * weight;
	const float2 d = wave.DirectionNumber.xy;
	const float hk = wave.DirectionNumber.w * wave.DirectionNumber.z;
	const float ak = wave.AmplitudePhase.x * wave.DirectionNumber.z;
	dx += float3(-d.x * d.x * hk * s, d.x * ak * c, -d.x * d.y * hk * s);
	dz += float3(-d.x * d.y * hk * s, d.y * ak * c, -d.y * d.y * hk * s);
}

#endif
