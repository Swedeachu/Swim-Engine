// Punctual lights (critical-path item 63). Mirrors
// Engine/Systems/Renderer/Lights/GpuLightRecord.h and LightMath.cpp line for line.
// Directional lights occupy rows [0, DirectionalCount) of the light buffer and
// local (point/spot) lights rows [FirstLocalRow, FirstLocalRow + LocalCount).
#ifndef SWIM_GPU_LIGHT_RECORDS_SLANG
#define SWIM_GPU_LIGHT_RECORDS_SLANG

static const uint GpuLightTypeDirectional = 0;
static const uint GpuLightTypePoint = 1;
static const uint GpuLightTypeSpot = 2;
static const uint GpuLightNoShadow = 0xffffffff;
static const uint GpuLightFlagCastsShadows = 1;
static const float GpuLightMinDistanceSquared = 1.0e-4;

struct GpuLightRecord
{
	float3 Position;
	float Range;
	float3 Direction; // Unit; where the light points.
	uint Type;
	float3 Color; // Linear color times intensity.
	float InverseRangeSquared;
	float SpotScale;
	float SpotOffset;
	uint ShadowIndex;
	uint Flags;
};

struct GpuLightHeader
{
	uint DirectionalCount;
	uint LocalCount;
	uint FirstLocalRow;
	uint LocalCapacity;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
	uint Reserved3;
};

float GpuLightRangeAttenuation(float distanceSquared, float inverseRangeSquared)
{
	const float factor = distanceSquared * inverseRangeSquared;
	const float window = clamp(1.0 - factor * factor, 0.0, 1.0);
	return window * window / max(distanceSquared, GpuLightMinDistanceSquared);
}

float GpuLightSpotAttenuation(GpuLightRecord light, float3 toLight)
{
	const float cd = -dot(light.Direction, toLight);
	const float t = clamp(cd * light.SpotScale + light.SpotOffset, 0.0, 1.0);
	return t * t;
}

struct GpuLightSample
{
	float3 Direction; // Toward the light.
	float3 Radiance;
};

GpuLightSample GpuLightEvaluate(GpuLightRecord light, float3 position)
{
	GpuLightSample sample;
	if (light.Type == GpuLightTypeDirectional)
	{
		sample.Direction = -light.Direction;
		sample.Radiance = light.Color;
		return sample;
	}
	const float3 delta = light.Position - position;
	const float distanceSquared = dot(delta, delta);
	const float inverseDistance = 1.0 / sqrt(max(distanceSquared, 1.0e-12));
	sample.Direction = delta * inverseDistance;
	const float attenuation = GpuLightRangeAttenuation(distanceSquared, light.InverseRangeSquared) * GpuLightSpotAttenuation(light, sample.Direction);
	sample.Radiance = light.Color * attenuation;
	return sample;
}

#endif
