// Evaluates StandardPbrEvaluateBrdf for a table of inputs so the native smoke can
// compare it with the CPU definition (StandardPbr::EvaluateBrdf).
#include "Materials/StandardPbr.slang"

struct PbrCase
{
	float3 Normal;
	float Metallic;
	float3 View;
	float Roughness;
	float3 Light;
	float Reserved0;
	float3 BaseColor;
	float Reserved1;
};

struct ProbeSettings
{
	uint Count;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<ProbeSettings> Settings;
[[vk::binding(0, 0)]] StructuredBuffer<PbrCase> Cases;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Results;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Settings.Count)
	{
		return;
	}
	const PbrCase c = Cases[id.x];
	Results[id.x] = float4(StandardPbrEvaluateBrdf(c.BaseColor, c.Metallic, c.Roughness, c.Normal, c.View, c.Light), 1.0);
}
