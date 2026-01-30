// true-batched gpu culling (production version):
// - 1 indirect draw per mesh
// - visible instances are compacted per mesh into visibleInstanceBuffer
// - firstInstance points into that compacted buffer
//
// passes (all in one cmd buffer, outside render pass):
//  (a) main_count     : frustum cull + atomic count per mesh
//  (b) main_scan512   : exclusive scan meshCounts -> meshOffsets (per 512-mesh chunk) + writes groupSums
//  (c) main_scanGroups: exclusive scan groupSums -> groupOffsets (expects groupCount <= 512)
//  (d) main_fixup     : add groupOffsets to meshOffsets + zero meshWriteCursor
//  (e) main_scatter   : frustum cull again + scatter instances into visibleInstanceBuffer using meshOffsets + cursor
//  (f) main_build     : emit 1 VkDrawIndexedIndirectCommand per mesh that has meshCounts > 0
//
// notes:
// - mesh ids must be dense in [0..meshCount-1]
// - supports up to 512 scan groups => meshCount <= 512 * 512 = 262144 meshes
// - if we ever exceed that, we add a 3rd scan level (super groups), but this is already way beyond normal usage

// 0 = aabb, 1 = sphere
static const uint USE_SPHERE_CULLING = 1;

static const uint VERTEX_STRIDE_BYTES = 32;

// instance threads
static const uint LOCAL_SIZE_INST = 256;

// scan uses 256 threads scanning 512 elements (two per thread)
static const uint LOCAL_SIZE_SCAN = 256;
static const uint SCAN_ELEMS_PER_GROUP = 512;

[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
	float4x4 worldView;
	float4x4 worldProj;
	float4x4 screenView;
	float4x4 screenProj;

	float4 camParams;      // x=tanHalfFovX, y=tanHalfFovY, z=near, w=far
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

struct MeshInfo
{
	// this is the per-mesh immutable draw info (no need to read a random instance for it)
	uint indexCount;
	uint firstIndex;     // in indices (not bytes)
	int  vertexOffset;   // in vertices (not bytes)
	uint _pad0;
};

struct DrawIndexedIndirectCommand
{
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int  vertexOffset;
	uint firstInstance;
};

struct PushConstants
{
	uint instanceCount;     // instances in instanceBuffer
	uint meshCount;         // mesh bins
	uint meshGroupCount;    // ceil(meshCount / 512)
	uint _pad0;
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(1, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t1, space0);

[[vk::binding(2, 0)]]
StructuredBuffer<MeshInfo> meshInfoBuffer : register(t2, space0);

// per-mesh counters + offsets
[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> meshCounts : register(u3, space0);

[[vk::binding(4, 0)]]
RWStructuredBuffer<uint> meshOffsets : register(u4, space0);

[[vk::binding(5, 0)]]
RWStructuredBuffer<uint> meshWriteCursor : register(u5, space0);

// scan group sums/offsets
[[vk::binding(6, 0)]]
RWStructuredBuffer<uint> groupSums : register(u6, space0);

[[vk::binding(7, 0)]]
RWStructuredBuffer<uint> groupOffsets : register(u7, space0);

// compacted visible instances (this is what the gfx pipeline binds as the instance vertex buffer)
[[vk::binding(8, 0)]]
RWStructuredBuffer<GpuInstanceData> visibleInstanceBuffer : register(u8, space0);

// indirect output
[[vk::binding(9, 0)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> outCommands : register(u9, space0);

[[vk::binding(10, 0)]]
RWStructuredBuffer<uint> outDrawCount : register(u10, space0);

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
	// min = dot(n, c) - dot(abs(n), e)
	// outside if min > 0
	float s = dot(n, c);
	float r = dot(abs(n), e);
	return (s - r) > 0.0f;
}

static bool PlaneSphereOutside(float3 n, float3 c, float r)
{
	// outside if dot(n, c) > r * |n|
	float s = dot(n, c);
	float rn = r * length(n);
	return s > rn;
}

static bool FrustumSphereVisible(GpuInstanceData inst)
{
	// screen-space instances: keep visible
	if (inst.space == 1)
	{
		return true;
	}

	float3 localMin = inst.aabbMin.xyz;
	float3 localMax = inst.aabbMax.xyz;

	float3 centerL = (localMin + localMax) * 0.5f;
	float3 extentL = (localMax - localMin) * 0.5f;

	float4 centerW4 = mul(inst.model, float4(centerL, 1.0f));
	float4 centerV4 = mul(worldView, centerW4);
	float3 centerV = centerV4.xyz;

	float3x3 VM = mul((float3x3)worldView, (float3x3)inst.model);
	float3x3 absVM = Abs3x3(VM);
	float3 extentV = mul(absVM, extentL);

	float radius = length(extentV);

	float z = -centerV.z;
	float nearZ = camParams.z;
	float farZ = camParams.w;

	if (z + radius < nearZ) { return false; }
	if (z - radius > farZ)  { return false; }

	float tX = camParams.x;
	float tY = camParams.y;

	if (PlaneSphereOutside(float3( 1.0f, 0.0f, tX), centerV, radius)) { return false; }
	if (PlaneSphereOutside(float3(-1.0f, 0.0f, tX), centerV, radius)) { return false; }
	if (PlaneSphereOutside(float3( 0.0f, 1.0f, tY), centerV, radius)) { return false; }
	if (PlaneSphereOutside(float3( 0.0f,-1.0f, tY), centerV, radius)) { return false; }

	return true;
}

static bool FrustumAabbVisible(GpuInstanceData inst)
{
	// screen-space instances: keep visible
	if (inst.space == 1)
	{
		return true;
	}

	float3 localMin = inst.aabbMin.xyz;
	float3 localMax = inst.aabbMax.xyz;

	float3 centerL = (localMin + localMax) * 0.5f;
	float3 extentL = (localMax - localMin) * 0.5f;

	float4 centerW4 = mul(inst.model, float4(centerL, 1.0f));
	float4 centerV4 = mul(worldView, centerW4);
	float3 centerV = centerV4.xyz;

	float3x3 VM = mul((float3x3)worldView, (float3x3)inst.model);
	float3x3 absVM = Abs3x3(VM);
	float3 extentV = mul(absVM, extentL);

	float z = -centerV.z;
	float nearZ = camParams.z;
	float farZ = camParams.w;

	if (z + extentV.z < nearZ) { return false; }
	if (z - extentV.z > farZ)  { return false; }

	float tX = camParams.x;
	float tY = camParams.y;

	if (PlaneAabbOutside(float3( 1.0f, 0.0f, tX), centerV, extentV)) { return false; }
	if (PlaneAabbOutside(float3(-1.0f, 0.0f, tX), centerV, extentV)) { return false; }
	if (PlaneAabbOutside(float3( 0.0f, 1.0f, tY), centerV, extentV)) { return false; }
	if (PlaneAabbOutside(float3( 0.0f,-1.0f, tY), centerV, extentV)) { return false; }

	return true;
}

static bool FrustumVisible(GpuInstanceData inst)
{
	if (USE_SPHERE_CULLING == 1)
	{
		return FrustumSphereVisible(inst);
	}

	return FrustumAabbVisible(inst);
}

// (a) cull + per-mesh count
[numthreads(LOCAL_SIZE_INST, 1, 1)]
void main_count(uint3 dtid : SV_DispatchThreadID)
{
	uint instanceID = dtid.x;

	if (instanceID >= pc.instanceCount)
	{
		return;
	}

	GpuInstanceData inst = instanceBuffer[instanceID];

	// out of range mesh ids don't get batched
	if (inst.meshInfoIndex >= pc.meshCount)
	{
		return;
	}

	if (!FrustumVisible(inst))
	{
		return;
	}

	InterlockedAdd(meshCounts[inst.meshInfoIndex], 1);
}

// scan shared for exactly 512 elements
groupshared uint sScan[SCAN_ELEMS_PER_GROUP];

// (b) scan meshCounts -> meshOffsets (exclusive), per 512-mesh chunk
// dispatch: groupCountX = pc.meshGroupCount
[numthreads(LOCAL_SIZE_SCAN, 1, 1)]
void main_scan512(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
	uint lane = gtid.x;
	uint groupBase = gid.x * SCAN_ELEMS_PER_GROUP;

	// load 2 elems per thread
	uint i0 = groupBase + lane * 2 + 0;
	uint i1 = groupBase + lane * 2 + 1;

	uint v0 = (i0 < pc.meshCount) ? meshCounts[i0] : 0;
	uint v1 = (i1 < pc.meshCount) ? meshCounts[i1] : 0;

	sScan[lane * 2 + 0] = v0;
	sScan[lane * 2 + 1] = v1;

	GroupMemoryBarrierWithGroupSync();

	// up-sweep (blelloch) over 512
	for (uint offset = 1; offset < SCAN_ELEMS_PER_GROUP; offset <<= 1)
	{
		uint idx = (lane + 1) * offset * 2 - 1;
		if (idx < SCAN_ELEMS_PER_GROUP)
		{
			sScan[idx] += sScan[idx - offset];
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// total sum lives at last element after up-sweep
	uint total = sScan[SCAN_ELEMS_PER_GROUP - 1];

	// exclusive: set last to 0
	if (lane == 0)
	{
		sScan[SCAN_ELEMS_PER_GROUP - 1] = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	// down-sweep
	for (uint offset = (SCAN_ELEMS_PER_GROUP >> 1); offset > 0; offset >>= 1)
	{
		uint idx = (lane + 1) * offset * 2 - 1;
		if (idx < SCAN_ELEMS_PER_GROUP)
		{
			uint t = sScan[idx - offset];
			sScan[idx - offset] = sScan[idx];
			sScan[idx] += t;
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// write offsets
	if (i0 < pc.meshCount) { meshOffsets[i0] = sScan[lane * 2 + 0]; }
	if (i1 < pc.meshCount) { meshOffsets[i1] = sScan[lane * 2 + 1]; }

	// write group sums
	if (lane == 0)
	{
		groupSums[gid.x] = total;
	}
}

// (c) scan groupSums -> groupOffsets (exclusive)
// dispatch: groupCountX = 1
// expects pc.meshGroupCount <= 512
[numthreads(LOCAL_SIZE_SCAN, 1, 1)]
void main_scanGroups(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
	uint lane = gtid.x;

	// load 2 group sums per thread into the same 512 scan buffer
	uint i0 = lane * 2 + 0;
	uint i1 = lane * 2 + 1;

	uint v0 = (i0 < pc.meshGroupCount) ? groupSums[i0] : 0;
	uint v1 = (i1 < pc.meshGroupCount) ? groupSums[i1] : 0;

	sScan[i0] = v0;
	sScan[i1] = v1;

	GroupMemoryBarrierWithGroupSync();

	// up-sweep over 512
	for (uint offset = 1; offset < SCAN_ELEMS_PER_GROUP; offset <<= 1)
	{
		uint idx = (lane + 1) * offset * 2 - 1;
		if (idx < SCAN_ELEMS_PER_GROUP)
		{
			sScan[idx] += sScan[idx - offset];
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// exclusive
	if (lane == 0)
	{
		sScan[SCAN_ELEMS_PER_GROUP - 1] = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	// down-sweep
	for (uint offset = (SCAN_ELEMS_PER_GROUP >> 1); offset > 0; offset >>= 1)
	{
		uint idx = (lane + 1) * offset * 2 - 1;
		if (idx < SCAN_ELEMS_PER_GROUP)
		{
			uint t = sScan[idx - offset];
			sScan[idx - offset] = sScan[idx];
			sScan[idx] += t;
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// write group offsets
	if (i0 < pc.meshGroupCount) { groupOffsets[i0] = sScan[i0]; }
	if (i1 < pc.meshGroupCount) { groupOffsets[i1] = sScan[i1]; }
}

// (d) fixup: meshOffsets += groupOffsets[group], and zero meshWriteCursor
[numthreads(LOCAL_SIZE_INST, 1, 1)]
void main_fixup(uint3 dtid : SV_DispatchThreadID)
{
	uint meshID = dtid.x;

	if (meshID >= pc.meshCount)
	{
		return;
	}

	uint groupID = meshID / SCAN_ELEMS_PER_GROUP;
	uint base = groupOffsets[groupID];

	meshOffsets[meshID] += base;
	meshWriteCursor[meshID] = 0;
}

// (e) scatter visible instances into visibleInstanceBuffer
[numthreads(LOCAL_SIZE_INST, 1, 1)]
void main_scatter(uint3 dtid : SV_DispatchThreadID)
{
	uint instanceID = dtid.x;

	if (instanceID >= pc.instanceCount)
	{
		return;
	}

	GpuInstanceData inst = instanceBuffer[instanceID];

	if (inst.meshInfoIndex >= pc.meshCount)
	{
		return;
	}

	if (!FrustumVisible(inst))
	{
		return;
	}

	uint meshID = inst.meshInfoIndex;

	uint localIndex;
	InterlockedAdd(meshWriteCursor[meshID], 1, localIndex);

	uint dstIndex = meshOffsets[meshID] + localIndex;

	visibleInstanceBuffer[dstIndex] = inst;
}

// (f) build 1 indirect command per mesh with visible count > 0
[numthreads(LOCAL_SIZE_INST, 1, 1)]
void main_build(uint3 dtid : SV_DispatchThreadID)
{
	uint meshID = dtid.x;

	if (meshID >= pc.meshCount)
	{
		return;
	}

	uint count = meshCounts[meshID];
	if (count == 0)
	{
		return;
	}

	uint base = meshOffsets[meshID];

	MeshInfo mi = meshInfoBuffer[meshID];

	uint drawIndex;
	InterlockedAdd(outDrawCount[0], 1, drawIndex);

	DrawIndexedIndirectCommand cmd;
	cmd.indexCount = mi.indexCount;
	cmd.instanceCount = count;
	cmd.firstIndex = mi.firstIndex;
	cmd.vertexOffset = mi.vertexOffset;

	// this is the key: firstInstance indexes into visibleInstanceBuffer
	cmd.firstInstance = base;

	outCommands[drawIndex] = cmd;
}
