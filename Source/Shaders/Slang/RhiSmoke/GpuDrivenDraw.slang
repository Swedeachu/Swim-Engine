// GPU-driven draw smoke: every draw comes from GpuVisibility's indirect commands.
// The instance id is the draw slot (the command's FirstInstance), which selects
// the GpuDrawRecord naming the GPU Scene row; vertices are pulled from the
// GeometryHeap vertex page (float3 positions). The fragment writes ObjectId + 1
// as RGB bytes so the test can map every pixel back to an object.
#include "GpuScene/GpuSceneRecords.slang"
#include "GpuScene/VisibilityRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<GpuInstanceRecord> Instances;
[[vk::binding(1, 0)]] StructuredBuffer<GpuTransformRecord> Transforms;
[[vk::binding(2, 0)]] StructuredBuffer<GpuDrawRecord> DrawRecords;
[[vk::binding(3, 0)]] StructuredBuffer<float> Positions;
[[vk::binding(4, 0)]] StructuredBuffer<GpuViewRecord> Views;

struct VertexOutput
{
	float4 Position : SV_Position;
	nointerpolation uint ObjectId : OBJECT_ID;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID, uint drawSlot : SV_VulkanInstanceID)
{
	const GpuDrawRecord record = DrawRecords[drawSlot];
	const GpuInstanceRecord instance = Instances[record.InstanceRow];
	const float3 local = float3(Positions[vertexId * 3], Positions[vertexId * 3 + 1], Positions[vertexId * 3 + 2]);
	const float3 world = GpuSceneTransformPoint(Transforms[instance.TransformIndex].Current, local);
	const GpuViewRecord view = Views[0];
	const float4 p = float4(world, 1.0);
	VertexOutput output;
	output.Position = float4(dot(view.ViewProjection[0], p), dot(view.ViewProjection[1], p), dot(view.ViewProjection[2], p),
		dot(view.ViewProjection[3], p));
	output.ObjectId = instance.ObjectId;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	const uint encoded = input.ObjectId + 1;
	return float4(float(encoded & 0xffu), float((encoded >> 8) & 0xffu), float((encoded >> 16) & 0xffu), 255.0) / 255.0;
}
