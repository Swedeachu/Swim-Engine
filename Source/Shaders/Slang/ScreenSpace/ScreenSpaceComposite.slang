// Screen-space composite (item 76): ScreenSpace::CompositeTexel for every pixel, AO on
// the indirect radiance, then fog. Bindings follow ScreenSpaceCompositeBindings.
#include "ScreenSpace/ScreenSpaceRecords.slang"

[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float4> Indirect;
[[vk::binding(2, 0)]] Texture2D<float> Ao;
[[vk::binding(3, 0)]] Texture2D<float> Depth;
[[vk::binding(4, 0)]] StructuredBuffer<ScreenSpaceParams> Params;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

struct FogParams
{
	float3 Color;
	float Density;
	float3 SunColor;
	float HeightFalloff;
	float3 SunDirection;
	float BaseHeight;
	float Anisotropy;
	float StartDistance;
	float MaxDistance;
};

// ScreenSpace::FogOpticalDepth.
float FogOpticalDepth(FogParams fog, float3 camera, float3 direction, float fogDistance)
{
	if (!(fog.Density > 0.0) || !(fogDistance > fog.StartDistance))
	{
		return 0.0;
	}
	const float startHeight = camera.y + direction.y * fog.StartDistance;
	const float pathLength = fogDistance - fog.StartDistance;
	const float density = fog.Density * exp(clamp(-fog.HeightFalloff * (startHeight - fog.BaseHeight), -80.0, 80.0));
	const float k = fog.HeightFalloff * direction.y * pathLength;
	const float shape = abs(k) < 1.0e-4 ? 1.0 - 0.5 * k + k * k / 6.0 : (1.0 - exp(clamp(-k, -80.0, 80.0))) / k;
	return density * pathLength * shape;
}

// ScreenSpace::HenyeyGreenstein.
float HenyeyGreenstein(float g, float cosTheta)
{
	const float denominator = max(1.0 + g * g - 2.0 * g * cosTheta, 1.0e-6);
	return (1.0 - g * g) / (denominator * sqrt(denominator));
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
	const int3 at = int3(int(id.x), int(id.y), 0);
	const float4 color = Color.Load(at);
	float3 c = color.rgb;
	if (Params[0].AoEnabled != 0u)
	{
		c = max(c - (1.0 - Ao.Load(at)) * Indirect.Load(at).rgb, float3(0.0, 0.0, 0.0));
	}
	if (Params[0].FogEnabled != 0u)
	{
		float4 inverseView[3];
		for (uint row = 0; row < 3; ++row)
		{
			inverseView[row] = Params[0].InverseViewRows[row];
		}
		FogParams fog;
		fog.Color = Params[0].FogColor;
		fog.Density = Params[0].FogDensity;
		fog.SunColor = Params[0].FogSunColor;
		fog.HeightFalloff = Params[0].FogHeightFalloff;
		fog.SunDirection = Params[0].FogSunDirection;
		fog.BaseHeight = Params[0].FogBaseHeight;
		fog.Anisotropy = Params[0].FogAnisotropy;
		fog.StartDistance = Params[0].FogStartDistance;
		fog.MaxDistance = Params[0].FogMaxDistance;
		const float3 camera = float3(inverseView[0].w, inverseView[1].w, inverseView[2].w);
		const float depth = Depth.Load(at);
		// The sky is fogged along its view ray (through the near plane) at the max distance.
		bool valid;
		const float3 viewPosition = ScreenSpaceViewPosition(view, float(id.x) + 0.5, float(id.y) + 0.5, depth > 0.0 ? depth : 1.0, valid);
		if (valid)
		{
			const float4 p = float4(viewPosition, 1.0);
			const float3 delta = float3(dot(inverseView[0], p), dot(inverseView[1], p), dot(inverseView[2], p)) - camera;
			const float distanceToPoint = length(delta);
			if (distanceToPoint > 1.0e-6)
			{
				const float3 direction = delta * (1.0 / distanceToPoint);
				const float fogDistance = depth > 0.0 ? min(distanceToPoint, fog.MaxDistance) : fog.MaxDistance;
				const float transmittance = exp(-FogOpticalDepth(fog, camera, direction, fogDistance));
				const float3 inscatter = fog.Color + fog.SunColor * HenyeyGreenstein(fog.Anisotropy, -dot(fog.SunDirection, direction));
				c = c * transmittance + inscatter * (1.0 - transmittance);
			}
		}
	}
	Output[id.xy] = float4(c, color.a);
}
