// Expanding impact rings on a water surface (Engine::GpuWaterRipple, from
// VfxDirector::GetRipples). A water shader binds the records as a StructuredBuffer and
// calls WaterRippleField at the fragment's world xz:
//   float4 r = WaterRippleField(ripples, count, world.xz);
//   normal = normalize(normal + float3(-r.x, 0.0, -r.y));
//   foam = max(foam, r.w);
// r.z is the height offset (metres) for a vertex stage that displaces the surface.
#ifndef SWIM_WATER_RIPPLES_SLANG
#define SWIM_WATER_RIPPLES_SLANG

struct WaterRippleRecord
{
	float4 Centre; // xyz: impact point; w: age (s).
	float4 Wave;   // x: amplitude (m); y: speed (m/s); z: wavelength (m); w: width (m).
};

// One ring: float4(dh/dx, dh/dz, height, foam).
float4 WaterRipple(WaterRippleRecord ripple, float2 xz)
{
	const float2 offset = xz - ripple.Centre.xz;
	const float distance = length(offset);
	const float age = ripple.Centre.w;
	const float front = ripple.Wave.y * age;
	const float width = max(ripple.Wave.w, 0.05);
	// The packet trails behind the front; nothing ahead of it yet.
	const float behind = front - distance;
	if (behind < -width * 2.0 || behind > width * 4.0)
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	const float envelope = exp(-behind * behind / (width * width));
	// Energy spreads over the growing circumference and the water damps it.
	const float spread = rsqrt(1.0 + front * 2.0);
	const float amplitude = ripple.Wave.x * spread;
	const float k = 6.2831853 / max(ripple.Wave.z, 0.02);
	const float wave = cos(k * behind);
	const float height = amplitude * envelope * wave;
	// d/d(distance) of envelope * cos(k * (front - distance)).
	const float dEnvelope = envelope * 2.0 * behind / (width * width);
	const float slope = amplitude * (dEnvelope * wave + envelope * k * sin(k * behind));
	const float2 direction = distance > 1.0e-4 ? offset / distance : float2(0.0, 0.0);
	// Foam rides the first crest while the ring is young.
	const float foam = saturate(envelope * saturate(wave) * amplitude * 40.0) * exp(-age * 1.5);
	return float4(direction * slope, height, foam);
}

float4 WaterRippleField(StructuredBuffer<WaterRippleRecord> ripples, uint count, float2 xz)
{
	float4 sum = float4(0.0, 0.0, 0.0, 0.0);
	for (uint i = 0u; i < count; ++i)
	{
		const float4 r = WaterRipple(ripples[i], xz);
		sum.xyz += r.xyz;
		sum.w = max(sum.w, r.w);
	}
	// Keep stacked rings from tilting the normal past grazing.
	sum.xy = clamp(sum.xy, -1.5, 1.5);
	return sum;
}

#endif
