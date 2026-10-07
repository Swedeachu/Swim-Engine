// Reflection probe capture resolve: copies one captured cube face (a forward-rendered
// square view, ReflectionProbes::CubeFaceView) into its face of the probe atlas's source
// cube, mirrored left-right (cube faces are left-handed: capture column
// ReflectionProbes::CaptureColumn), with alpha = the distance from the probe to what the
// texel sees (ReflectionProbes::CaptureDistance). Reflective texels show their own probe's
// reflection instead of the bare environment IBL (CaptureShading.slang). Mode 1 clears the
// face instead (atlas initialization). Bindings follow ReflectionProbeResolveBindings.
struct ResolveConstants
{
	uint Size;	 // Face size.
	uint Mode;	 // 0: resolve, 1: clear.
	float Near;	 // Capture near plane.
	uint Face;	 // Cube face (the sky texels' direction).
	uint Environment;		 // 1: the sky comes from the environment (with its clouds).
	float EnvironmentScale;	 // EnvironmentIntensity.
	float EnvironmentRotation;
	uint Reserved;
	float4 Position; // xyz: the probe position.
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
// Where the capture saw only sky, the global environment's sharpest mip (the procedural sky
// with feature overlays such as volumetric clouds, which the capture's sky pass lacks).
[[vk::binding(3, 0)]] TextureCube<float4> Environment;
[[vk::binding(4, 0)]] SamplerState EnvironmentSampler;
[[vk::binding(5, 0)]] Texture2D<float4> Normal;
[[vk::binding(6, 0)]] Texture2D<float4> Reflectance;
[[vk::binding(7, 0)]] Texture2D<float4> Specular;
[[vk::binding(8, 0)]] Texture2D<float> ObjectId;
[[vk::binding(9, 0)]] TextureCubeArray<float4> ProbeCubes;
[[vk::binding(10, 0)]] StructuredBuffer<CaptureProbeRecord> ProbeRecords;

#include "Environment/EnvironmentCommon.slang"

// ReflectionProbes::DistanceSky.
static const float DistanceSky = 10000.0;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const uint size = Constants.Size;
	if (id.x >= size || id.y >= size)
	{
		return;
	}
	if (Constants.Mode != 0u)
	{
		Destination[id.xy] = float4(0.0, 0.0, 0.0, DistanceSky);
		return;
	}
	const int2 source = int2(int(size - 1u - id.x), int(id.y));
	float3 radiance = max(Color.Load(int3(source, 0)).rgb, float3(0.0, 0.0, 0.0));
	const float depth = Depth.Load(int3(source, 0));
	if (!(depth > 0.0) && Constants.Environment != 0u)
	{
		const float3 direction = normalize(EnvironmentCubeTexelDirection(Constants.Face, id.x, id.y, size));
		radiance = Environment.SampleLevel(EnvironmentSampler, EnvironmentRotateLookup(direction, Constants.EnvironmentRotation), 0.0).rgb *
			Constants.EnvironmentScale;
	}
	const float ndcX = (float(source.x) + 0.5) / float(size) * 2.0 - 1.0;
	const float ndcY = 1.0 - (float(source.y) + 0.5) / float(size) * 2.0;
	const float distance = depth > 0.0 ? min(Constants.Near / depth * sqrt(1.0 + ndcX * ndcX + ndcY * ndcY), DistanceSky) : DistanceSky;
	if (depth > 0.0)
	{
		const float3 eye = Constants.Position.xyz;
		const float3 direction = normalize(EnvironmentCubeTexelDirection(Constants.Face, id.x, id.y, size));
		radiance = CaptureReflections(radiance, source, eye, eye + direction * distance, Constants.ProbeCount, Constants.ProbeMips, Normal,
			Reflectance, Specular, ObjectId, ProbeCubes, ProbeRecords, EnvironmentSampler);
	}
	Destination[id.xy] = float4(min(radiance, float3(60000.0, 60000.0, 60000.0)), distance);
}
