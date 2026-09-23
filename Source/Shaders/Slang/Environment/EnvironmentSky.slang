// Procedural sky (item 61): writes one face of the source cube's mip 0 per dispatch,
// each texel the sky radiance at its center direction. Mirrors
// Environment::ProceduralSky::Evaluate; bindings follow EnvironmentSkyBindings.
#include "Environment/EnvironmentCommon.slang"

struct SkyConstants
{
	float4 Zenith; // w: intensity.
	float4 Horizon;
	float4 Ground;
	float4 SunDirection; // w: sharpness.
	float4 SunColor;
	uint Face;
	uint Size;
	uint2 Reserved;
};

[[vk::push_constant]] ConstantBuffer<SkyConstants> Constants;
[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;

float3 EvaluateSky(float3 direction)
{
	const float3 d = normalize(direction);
	const float up = clamp(d.y, -1.0, 1.0);
	const float blend = 1.0 - (1.0 - abs(up)) * (1.0 - abs(up));
	const float3 end = up >= 0.0 ? Constants.Zenith.rgb : Constants.Ground.rgb;
	const float lobe = pow(max(dot(d, Constants.SunDirection.xyz), 0.0), Constants.SunDirection.w);
	return (Constants.Horizon.rgb + (end - Constants.Horizon.rgb) * blend + Constants.SunColor.rgb * lobe) * Constants.Zenith.w;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size || id.y >= Constants.Size)
	{
		return;
	}
	const float3 direction = EnvironmentCubeTexelDirection(Constants.Face, id.x, id.y, Constants.Size);
	Destination[id.xy] = float4(EvaluateSky(direction), 1.0);
}
