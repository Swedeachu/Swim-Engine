// Runtime sky background (Phase 23). Runs before the Clustered Forward+ opaque pass,
// which then loads (rather than clears) its targets: this pass clears depth (reverse-Z
// far = 0), the object id and every screen-space target to zero, and fills the color
// target with the procedural sky seen through each pixel, so transparent surfaces blend
// over the sky and screen-space effects see an empty background. The sky mirrors
// Environment::ProceduralSky::Evaluate (EnvironmentSky.slang), rotated and scaled like
// the image-based lighting the frame uses.
#include "Environment/EnvironmentCommon.slang"

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
	float4 Ground;		 // w: unused.
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
	float ObjectId : SV_Target1;
	float2 Velocity : SV_Target2;
	float4 Normal : SV_Target3;
	float4 Indirect : SV_Target4;
	float4 Reflectance : SV_Target5;
	float4 Specular : SV_Target6;
};

// gl_VertexIndex directly (SV_VulkanVertexID): the fullscreen triangle is drawn from
// vertex 0, so it equals SV_VertexID without the DrawParameters capability.
[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID)
{
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
	const float blend = 1.0 - (1.0 - abs(up)) * (1.0 - abs(up));
	const float3 end = up >= 0.0 ? Constants.Zenith.rgb : Constants.Ground.rgb;
	const float lobe = pow(max(dot(d, Constants.SunDirection.xyz), 0.0), Constants.RayForward.w);
	return (Constants.Horizon.rgb + (end - Constants.Horizon.rgb) * blend + Constants.SunColor.rgb * lobe) * Constants.RayRight.w;
}

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input)
{
	const float3 direction = Constants.RayForward.xyz + input.Ndc.x * Constants.RayRight.xyz + input.Ndc.y * Constants.RayUp.xyz;
	FragmentOutput output;
	output.Color = float4(EvaluateSky(EnvironmentRotateLookup(normalize(direction), Constants.RayUp.w)), 1.0);
	output.ObjectId = 0.0;
	output.Velocity = float2(0.0, 0.0);
	output.Normal = float4(0.0, 0.0, 0.0, 0.0);
	output.Indirect = float4(0.0, 0.0, 0.0, 0.0);
	output.Reflectance = float4(0.0, 0.0, 0.0, 0.0);
	output.Specular = float4(0.0, 0.0, 0.0, 0.0);
	return output;
}
