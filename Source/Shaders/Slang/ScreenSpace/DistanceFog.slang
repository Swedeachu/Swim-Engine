// Distance fog curves (Engine/Systems/Renderer/ScreenSpace/DistanceFog.h): the scene fog and
// surfaces that fog themselves (ocean, water) share them, so water fades like the ground.
#ifndef SWIM_DISTANCE_FOG_SLANG
#define SWIM_DISTANCE_FOG_SLANG

static const uint DistanceFogExponential = 0u;
static const uint DistanceFogLinear = 1u;
static const uint DistanceFogSmooth = 2u;
static const uint DistanceFogPower = 3u;

struct DistanceFog
{
	float4 ColorOpacity; // rgb: colour, w: max opacity (0: off).
	float4 Range;		 // Start, end, density, exponent.
	uint Curve;
	uint Pad0;
	uint Pad1;
	uint Pad2;
};

float DistanceFogShape(uint curve, float start, float end, float density, float exponent, float distance)
{
	if (!(distance > start))
	{
		return 0.0;
	}
	if (curve == DistanceFogExponential)
	{
		return 1.0 - exp(-max(density, 0.0) * (distance - start));
	}
	const float t = saturate((distance - start) / max(end - start, 1.0e-3));
	if (curve == DistanceFogLinear)
	{
		return t;
	}
	if (curve == DistanceFogSmooth)
	{
		return t * t * (3.0 - 2.0 * t);
	}
	return pow(t, max(exponent, 1.0e-3));
}

float DistanceFogAmount(DistanceFog fog, float distance)
{
	return fog.ColorOpacity.w > 0.0 ? fog.ColorOpacity.w * DistanceFogShape(fog.Curve, fog.Range.x, fog.Range.y, fog.Range.z, fog.Range.w, distance)
									: 0.0;
}

float3 DistanceFogApply(DistanceFog fog, float3 color, float distance)
{
	return lerp(color, fog.ColorOpacity.rgb, DistanceFogAmount(fog, distance));
}

#endif
