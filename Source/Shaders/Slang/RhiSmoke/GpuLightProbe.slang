// Brute-force forward lighting probe (item 63): each thread shades one sample
// point with StandardPbrEvaluateBrdf summed over every light in the GpuLightBuffer
// (directional rows, then local rows). Mirrors Lights::ShadeAllLights, the reference
// clustered lighting (items 64-66) must reproduce.
#include "Lights/GpuLightRecords.slang"
#include "Materials/StandardPbr.slang"

struct LightProbeSample
{
	float3 Position;
	float Metallic;
	float3 Normal;
	float Roughness;
	float3 View;
	float Reserved0;
	float3 BaseColor;
	float Reserved1;
};

struct LightProbeConstants
{
	uint SampleCount;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<LightProbeConstants> Constants;
[[vk::binding(0, 0)]] StructuredBuffer<GpuLightRecord> Lights;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLightHeader> Header;
[[vk::binding(2, 0)]] StructuredBuffer<LightProbeSample> Samples;
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> Results;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.SampleCount)
	{
		return;
	}
	const LightProbeSample s = Samples[id.x];
	const GpuLightHeader header = Header[0];
	float3 sum = float3(0.0, 0.0, 0.0);
	for (uint i = 0; i < header.DirectionalCount + header.LocalCount; ++i)
	{
		const uint row = i < header.DirectionalCount ? i : header.FirstLocalRow + (i - header.DirectionalCount);
		const GpuLightSample light = GpuLightEvaluate(Lights[row], s.Position);
		sum += StandardPbrEvaluateBrdf(s.BaseColor, s.Metallic, s.Roughness, s.Normal, s.View, light.Direction) * light.Radiance;
	}
	Results[id.x] = float4(sum, float(header.DirectionalCount + header.LocalCount));
}
