// Planar reflection capture resolve: copies one capture (a Forward+ render from the mirrored
// camera through the reflector's portal, PlanarReflections::Capture) into the top-left
// Width x Height of its atlas layer, with alpha = the distance from the capture position to
// what the texel sees (the composite's lookup marches with it). Sky texels take the global
// environment's mip 0, which holds the clouds. Mode 1 clears the layer (atlas
// initialization). Bindings follow PlanarReflectionResolveBindings.
struct ResolveConstants
{
	uint Width;
	uint Height;
	uint Mode;		  // 0: resolve, 1: clear.
	uint Environment; // 1: the sky comes from the environment (with its clouds).
	float NearClip;	  // Reverse-Z: depth = NearClip / distance along Forward.
	float EnvironmentScale;
	float EnvironmentRotation;
	float Reserved;
	float4 FrustumScale; // P00, P11, P02, P12 of the off-axis projection.
	float4 Right;
	float4 Up;
	float4 Forward;
};

[[vk::push_constant]] ConstantBuffer<ResolveConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;
[[vk::binding(3, 0)]] TextureCube<float4> Environment;
[[vk::binding(4, 0)]] SamplerState EnvironmentSampler;

#include "Environment/EnvironmentCommon.slang"

// PlanarReflections::DistanceSky.
static const float DistanceSky = 10000.0;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Width || id.y >= Constants.Height)
	{
		return;
	}
	if (Constants.Mode != 0u)
	{
		Destination[id.xy] = float4(0.0, 0.0, 0.0, DistanceSky);
		return;
	}
	const int3 at = int3(int(id.x), int(id.y), 0);
	float3 radiance = max(Color.Load(at).rgb, float3(0.0, 0.0, 0.0));
	const float depth = Depth.Load(at);
	// The view ray through the texel at unit depth along Forward (off-axis: shifted centre).
	const float ndcX = (float(id.x) + 0.5) / float(Constants.Width) * 2.0 - 1.0;
	const float ndcY = 1.0 - (float(id.y) + 0.5) / float(Constants.Height) * 2.0;
	const float viewX = (ndcX + Constants.FrustumScale.z) / Constants.FrustumScale.x;
	const float viewY = (ndcY + Constants.FrustumScale.w) / Constants.FrustumScale.y;
	const float rayLength = sqrt(1.0 + viewX * viewX + viewY * viewY);
	if (!(depth > 0.0) && Constants.Environment != 0u)
	{
		const float3 direction = normalize(Constants.Right.xyz * viewX + Constants.Up.xyz * viewY + Constants.Forward.xyz);
		radiance = Environment.SampleLevel(EnvironmentSampler, EnvironmentRotateLookup(direction, Constants.EnvironmentRotation), 0.0).rgb *
			Constants.EnvironmentScale;
	}
	const float distance = depth > 0.0 ? min(Constants.NearClip / depth * rayLength, DistanceSky) : DistanceSky;
	Destination[id.xy] = float4(min(radiance, float3(60000.0, 60000.0, 60000.0)), distance);
}
