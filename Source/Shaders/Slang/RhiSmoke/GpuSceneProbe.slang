// GPU Scene probe: for each row, the world-space bounds center under the
// current and previous transform plus the row's flags/object id. Validates the
// shared record layouts on a real device.
#include "GpuScene/GpuSceneRecords.slang"

struct ProbeSettings
{
	uint RowCount;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<ProbeSettings> Probe;
[[vk::binding(0, 0)]] StructuredBuffer<GpuInstanceRecord> Instances;
[[vk::binding(1, 0)]] StructuredBuffer<GpuTransformRecord> Transforms;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> Results;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Probe.RowCount)
	{
		return;
	}
	const GpuInstanceRecord instance = Instances[id.x];
	if ((instance.Flags & GpuSceneFlagLive) == 0)
	{
		Results[id.x * 2] = float4(0, 0, 0, -1);
		Results[id.x * 2 + 1] = float4(0, 0, 0, -1);
		return;
	}
	const GpuTransformRecord transform = Transforms[instance.TransformIndex];
	const float drawable = GpuSceneIsDrawable(instance) ? 1.0 : 0.0;
	Results[id.x * 2] = float4(GpuSceneTransformPoint(transform.Current, instance.LocalCenter), drawable);
	Results[id.x * 2 + 1] = float4(GpuSceneTransformPoint(transform.Previous, instance.LocalCenter), float(instance.ObjectId));
}
