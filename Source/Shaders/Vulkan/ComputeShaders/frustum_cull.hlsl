// Writes VkDrawIndexedIndirectCommand entries for visible instances.
// One draw per instance (instanceCount = 1), firstInstance = original instance ID.

static const uint VERTEX_STRIDE_BYTES = 32;
static const uint LOCAL_SIZE = 256;

[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
	float4x4 worldView;
	float4x4 worldProj;
	float4x4 screenView;
	float4x4 screenProj;

	float4 camParams;
	float2 viewportSize;
	float2 _padViewportSize;
};

struct GpuInstanceData
{
	float4x4 model;

	float4 aabbMin;
	float4 aabbMax;

	uint textureIndex;
	float hasTexture;
	uint meshInfoIndex;
	uint materialIndex;

	uint indexCount;
	uint space;

	uint2 vertexOffsetInMegaBuffer;
	uint2 indexOffsetInMegaBuffer;
};

struct DrawIndexedIndirectCommand
{
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int  vertexOffset;
	uint firstInstance;
};

[[vk::binding(1, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t1, space0);

// Output: storage buffer that is ALSO used as an indirect buffer
[[vk::binding(4, 0)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> outCommands : register(u4, space0);

// Output: draw count (uint) for vkCmdDrawIndexedIndirectCount*
[[vk::binding(5, 0)]]
RWStructuredBuffer<uint> outDrawCount : register(u5, space0);

struct PushConstants
{
	uint instanceCount;
	uint _pad0;
	uint _pad1;
	uint _pad2;
};

[[vk::push_constant]]
PushConstants pc;

static float3x3 Abs3x3(float3x3 m)
{
	float3x3 r;
	r[0] = abs(m[0]);
	r[1] = abs(m[1]);
	r[2] = abs(m[2]);
	return r;
}

static bool PlaneAabbOutside(float3 n, float3 c, float3 e)
{
	// Plane is dot(n, p) <= 0 for inside.
	// AABB is outside if the minimum dot(n, p) over the box is > 0.
	
	// min = dot(n, c) - dot(abs(n), e)
	// outside if min > 0

	float s = dot(n, c);
	float r = dot(abs(n), e);

	return (s - r) > 0.0f;
}

static bool FrustumAabbVisible(GpuInstanceData inst)
{
	// Screen-space instances: keep visible for now
	if (inst.space == 1)
	{
		return true;
	}

	float3 localMin = inst.aabbMin.xyz;
	float3 localMax = inst.aabbMax.xyz;

	float3 centerL = (localMin + localMax) * 0.5f;
	float3 extentL = (localMax - localMin) * 0.5f;

	// Center in world then view
	float4 centerW4 = mul(inst.model, float4(centerL, 1.0f));
	float4 centerV4 = mul(worldView, centerW4);
	float3 centerV = centerV4.xyz;

	// Extents into view space using abs(V*M)
	float3x3 VM = mul((float3x3)worldView, (float3x3)inst.model);
	float3x3 absVM = Abs3x3(VM);
	float3 extentV = mul(absVM, extentL);

	// View forward distance
	float z = -centerV.z;
	float nearZ = camParams.z;
	float farZ = camParams.w;

	// Near/Far test
	if (z + extentV.z < nearZ) { return false; }
	if (z - extentV.z > farZ)  { return false; }

	// Frustum side planes in view space (camera looks down -Z)
	// Inside conditions:
	//   x <= (-z) * tanHalfFovX  => x + z*tX <= 0
	//  -x <= (-z) * tanHalfFovX  => -x + z*tX <= 0
	//   y <= (-z) * tanHalfFovY  => y + z*tY <= 0
	//  -y <= (-z) * tanHalfFovY  => -y + z*tY <= 0
	//
	// centerV.z is negative in front of camera, which is what we want here.
	float tX = camParams.x;
	float tY = camParams.y;

	// Right plane
	if (PlaneAabbOutside(float3( 1.0f, 0.0f,  tX), centerV, extentV)) { return false; }
	// Left plane
	if (PlaneAabbOutside(float3(-1.0f, 0.0f,  tX), centerV, extentV)) { return false; }
	// Top plane
	if (PlaneAabbOutside(float3( 0.0f, 1.0f,  tY), centerV, extentV)) { return false; }
	// Bottom plane
	if (PlaneAabbOutside(float3( 0.0f,-1.0f,  tY), centerV, extentV)) { return false; }

	return true;
}

[numthreads(LOCAL_SIZE, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	uint instanceID = dtid.x;

	if (instanceID >= pc.instanceCount)
	{
		return;
	}

	GpuInstanceData inst = instanceBuffer[instanceID];

	if (!FrustumAabbVisible(inst))
	{
		return;
	}

	uint writeIndex;
	InterlockedAdd(outDrawCount[0], 1, writeIndex);

	DrawIndexedIndirectCommand cmd;
	cmd.indexCount = inst.indexCount;
	cmd.instanceCount = 1;
	cmd.firstIndex = inst.indexOffsetInMegaBuffer.x / 4;
	cmd.vertexOffset = (int)(inst.vertexOffsetInMegaBuffer.x / VERTEX_STRIDE_BYTES);
	cmd.firstInstance = instanceID;

	outCommands[writeIndex] = cmd;
}
