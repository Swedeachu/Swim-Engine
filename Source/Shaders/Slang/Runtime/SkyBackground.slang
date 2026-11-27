// Runtime sky background (Phase 23). Runs before the Clustered Forward+ opaque pass,
// which then loads its colour and depth and clears every other target
// (ForwardPlusTargets::ClearAuxiliary): this pass clears depth (reverse-Z far = 0) and
// fills the color target with the procedural sky seen through each pixel, so transparent surfaces blend
// over the sky and screen-space effects see an empty background. The sky mirrors
// Environment::ProceduralSky::Evaluate (EnvironmentSky.slang), rotated and scaled like
// the image-based lighting the frame uses.
//
// SkyCubemap (SKY_CUBEMAP=1, FrameRenderer::SetSkyCubemap) draws the same background from a
// cube map instead: the view ray, rotated like the environment, looks the cube up with z
// mirrored (the faces are a left-handed cube whose +Z is the camera's default forward,
// world -Z), scaled by RenderSettings::SkyCubemapIntensity (RayRight.w).
#include "Resources/DrawIndex.slang"
#include "Environment/EnvironmentCommon.slang"

#ifndef SKY_CUBEMAP
#define SKY_CUBEMAP 0
#endif

#if SKY_CUBEMAP
[[vk::binding(0, 0)]] TextureCube<float4> SkyCube; // sRGB view: linear on read.
[[vk::binding(1, 0)]] SamplerState SkySampler;
#endif

// 128 bytes (the guaranteed push-constant budget). The view ray of an NDC point is
// Forward + x * Right + y * Up, with Right/Up scaled by the tangents of the half fields
// of view (the camera's unjittered frustum; the far plane is at infinity).
struct SkyBackgroundConstants
{
	float4 RayRight;	 // w: sky intensity x environment intensity.
	float4 RayUp;		 // w: environment rotation (radians).
	float4 RayForward;	 // w: sun sharpness.
	float4 Zenith;		 // w: unused.
	float4 Horizon;		 // w: unused.
	float4 Ground;		 // w: ProceduralSky::GroundFalloff.
	float4 SunDirection; // Normalized, toward the sun; w: unused.
	float4 SunColor;	 // w: unused.
};

[[vk::push_constant]] ConstantBuffer<SkyBackgroundConstants> Constants;

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Ndc : TEXCOORD0;
};

struct FragmentOutput
{
	float4 Color : SV_Target0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	static const float2 positions[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
	VertexOutput output;
	output.Position = float4(positions[vertexId], 0.0, 1.0);
	output.Ndc = positions[vertexId];
	return output;
}

float3 EvaluateSky(float3 direction)
{
	const float3 d = normalize(direction);
	const float up = clamp(d.y, -1.0, 1.0);
	const float falloff = up >= 0.0 ? 2.0 : max(Constants.Ground.w, 1.0);
	const float blend = 1.0 - pow(1.0 - abs(up), falloff);
	const float3 end = up >= 0.0 ? Constants.Zenith.rgb : Constants.Ground.rgb;
	const float lobe = pow(max(dot(d, Constants.SunDirection.xyz), 0.0), Constants.RayForward.w);
	return (Constants.Horizon.rgb + (end - Constants.Horizon.rgb) * blend + Constants.SunColor.rgb * lobe) * Constants.RayRight.w;
}

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input)
{
	const float3 direction = Constants.RayForward.xyz + input.Ndc.x * Constants.RayRight.xyz + input.Ndc.y * Constants.RayUp.xyz;
	FragmentOutput output;
#if SKY_CUBEMAP
	const float3 d = EnvironmentRotateLookup(normalize(direction), Constants.RayUp.w);
	output.Color = float4(SkyCube.SampleLevel(SkySampler, float3(d.x, d.y, -d.z), 0.0).rgb * Constants.RayRight.w, 1.0);
#else
	output.Color = float4(EvaluateSky(EnvironmentRotateLookup(normalize(direction), Constants.RayUp.w)), 1.0);
#endif
	return output;
}
