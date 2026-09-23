// Clustered shading probe (items 65-66): each thread shades one sample with
// ClusteredShade (its pixel's cluster list) and, for comparison, the brute-force
// sum over every light.
#include "ClusteredLighting/ClusteredLighting.slang"

struct ClusterProbeSample
{
	float3 Position; // World.
	float Metallic;
	float3 Normal;
	float Roughness;
	float3 View;
	float ViewDepth;
	float3 BaseColor;
	float Reserved;
	float2 Pixel;
	float2 Reserved1;
};

struct ClusterProbeConstants
{
	uint SampleCount;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<ClusterProbeConstants> Constants;
[[vk::binding(0, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLightRecord> Lights;
[[vk::binding(2, 0)]] StructuredBuffer<GpuLightHeader> LightHeader;
[[vk::binding(3, 0)]] StructuredBuffer<ClusterRecord> Records;
[[vk::binding(4, 0)]] StructuredBuffer<uint> Indices;
[[vk::binding(5, 0)]] StructuredBuffer<ClusterProbeSample> Samples;
[[vk::binding(6, 0)]] RWStructuredBuffer<float4> Results; // Per sample: clustered, then brute force.

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.SampleCount)
	{
		return;
	}
	const ClusterProbeSample s = Samples[id.x];
	const GpuLightHeader header = LightHeader[0];
	const float3 clustered = ClusteredShade(Grid[0], Lights, header, Records, Indices, s.BaseColor, s.Metallic, s.Roughness, s.Normal,
		s.View, s.Position, s.Pixel.x, s.Pixel.y, s.ViewDepth);
	float3 brute = float3(0.0, 0.0, 0.0);
	for (uint i = 0; i < header.DirectionalCount + header.LocalCount; ++i)
	{
		const uint row = i < header.DirectionalCount ? i : header.FirstLocalRow + (i - header.DirectionalCount);
		const GpuLightSample light = GpuLightEvaluate(Lights[row], s.Position);
		brute += StandardPbrEvaluateBrdf(s.BaseColor, s.Metallic, s.Roughness, s.Normal, s.View, light.Direction) * light.Radiance;
	}
	const uint cluster = ClusterIndexFor(Grid[0], s.Pixel.x, s.Pixel.y, s.ViewDepth);
	Results[id.x * 2] = float4(clustered, float(cluster));
	Results[id.x * 2 + 1] = float4(brute, float(Records[cluster].Count));
}
