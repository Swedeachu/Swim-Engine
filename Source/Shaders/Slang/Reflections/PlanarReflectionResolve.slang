// Planar reflection capture resolve: one capture (a Forward+ render from the mirrored camera
// through the reflector's portal, PlanarReflections::Capture) into the top-left Width x Height
// of its atlas layer, with alpha = the distance from the capture position to what the texel
// sees (the composite's lookup marches with it). Mode 1 clears the layer (atlas
// initialization). Bindings follow PlanarReflectionResolveBindings.
//
// The capture is rendered Scale x Scale times larger (supersampled; it has no TAA of its own)
// and each atlas texel averages its block: radiance weighted by 1 / (1 + luminance), so a
// bright texel cannot alias through, and the nearest distance (the foreground wins at an
// edge, where the lookup would otherwise land between two surfaces). Sky texels take the
// global environment's mip 0, which holds the clouds; reflective texels their probe's
// reflection (CaptureShading.slang).
struct ResolveConstants
{
	uint Width;		  // Stored (atlas) size.
	uint Height;
	uint Mode;		  // 0: resolve, 1: clear.
	uint Environment; // 1: the sky comes from the environment (with its clouds).
	float NearClip;	  // Reverse-Z: depth = NearClip / distance along Forward.
	float EnvironmentScale;
	float EnvironmentRotation;
	uint Scale;			 // Rendered texels per stored texel along each axis (1 .. 4).
	float4 FrustumScale; // P00, P11, P02, P12 of the off-axis projection.
	float4 Right;
	float4 Up;
	float4 Forward;
	float4 Position; // xyz: the capture position.
	uint ProbeCount; // Reflection probes for reflective texels (0: none).
	uint ProbeMips;
	uint Reserved0;
	uint Reserved1;
};

#include "Reflections/CaptureShading.slang"

[[vk::push_constant]] ConstantBuffer<ResolveConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;
[[vk::binding(3, 0)]] TextureCube<float4> Environment;
[[vk::binding(4, 0)]] SamplerState LinearSampler;
[[vk::binding(5, 0)]] Texture2D<float4> Normal;
[[vk::binding(6, 0)]] Texture2D<float4> Reflectance;
[[vk::binding(7, 0)]] Texture2D<float4> Specular;
[[vk::binding(8, 0)]] Texture2D<float> ObjectId;
[[vk::binding(9, 0)]] TextureCubeArray<float4> ProbeCubes;
[[vk::binding(10, 0)]] StructuredBuffer<CaptureProbeRecord> ProbeRecords;

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
	const uint scale = clamp(Constants.Scale, 1u, 4u);
	const float renderWidth = float(Constants.Width * scale);
	const float renderHeight = float(Constants.Height * scale);
	const float3 eye = Constants.Position.xyz;
	float3 sum = float3(0.0, 0.0, 0.0);
	float weights = 0.0;
	float nearest = DistanceSky;
	for (uint sy = 0; sy < scale; ++sy)
	{
		for (uint sx = 0; sx < scale; ++sx)
		{
			const int2 texel = int2(id.xy * scale + uint2(sx, sy));
			const int3 at = int3(texel, 0);
			float3 radiance = max(Color.Load(at).rgb, float3(0.0, 0.0, 0.0));
			const float depth = Depth.Load(at);
			// The view ray through the texel at unit depth along Forward (off-axis: shifted centre).
			const float ndcX = (float(texel.x) + 0.5) / renderWidth * 2.0 - 1.0;
			const float ndcY = 1.0 - (float(texel.y) + 0.5) / renderHeight * 2.0;
			const float viewX = (ndcX + Constants.FrustumScale.z) / Constants.FrustumScale.x;
			const float viewY = (ndcY + Constants.FrustumScale.w) / Constants.FrustumScale.y;
			const float3 ray = Constants.Right.xyz * viewX + Constants.Up.xyz * viewY + Constants.Forward.xyz;
			const float rayLength = length(ray);
			float distance = DistanceSky;
			if (depth > 0.0)
			{
				distance = min(Constants.NearClip / depth * rayLength, DistanceSky);
				radiance = CaptureReflections(radiance, texel, eye, eye + ray * (distance / rayLength), Constants.ProbeCount,
					Constants.ProbeMips, Normal, Reflectance, Specular, ObjectId, ProbeCubes, ProbeRecords, LinearSampler);
			}
			else if (Constants.Environment != 0u)
			{
				radiance = Environment.SampleLevel(LinearSampler, EnvironmentRotateLookup(ray / rayLength, Constants.EnvironmentRotation), 0.0).rgb *
					Constants.EnvironmentScale;
			}
			radiance = min(radiance, float3(60000.0, 60000.0, 60000.0));
			const float weight = 1.0 / (1.0 + dot(radiance, float3(0.2126, 0.7152, 0.0722)));
			sum += radiance * weight;
			weights += weight;
			nearest = min(nearest, distance);
		}
	}
	Destination[id.xy] = float4(sum / max(weights, 1.0e-6), nearest);
}
