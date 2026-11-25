// GPU culling of a render feature's own draw list (Engine::IndirectCuller), three programs
// from one source:
//  - IndirectCullReset (CULL_RESET=1): zeroes the per-view draw counts and the late count;
//  - IndirectCullEarly (default): tests every item against every view's frustum and appends
//    its draw command to that view's list; the camera view (view 0) keeps only the items
//    visible last frame (every item on a cut);
//  - IndirectCullLate (CULL_LATE=1): after the depth prepass, tests the camera view's items
//    against the HZB, appends the visible ones the early phase skipped to the late list
//    (slot ViewCount) and records every item's visibility for the next frame.
// Items: Bounds holds a world-space box per item (min, max), Commands a draw command
// (vertex count, instance count, first vertex, first instance) copied as is; items with no
// vertices or instances are never drawn.

#ifndef CULL_RESET
#define CULL_RESET 0
#endif
#ifndef CULL_LATE
#define CULL_LATE 0
#endif

struct CullBounds
{
	float4 Min;
	float4 Max;
};

struct CullView
{
	float4 Planes[6]; // xyz . p + w >= 0 inside.
};

#if CULL_LATE
#include "Features/HzbOcclusion.slang"

struct LateConstants
{
	HzbView View; // MipCount 0: no occlusion test.
	uint Count;
	uint ViewCount;
	uint Cut;
	uint Reserved;
};

[[vk::push_constant]] ConstantBuffer<LateConstants> Constants;
#else
struct EarlyConstants
{
	uint Count;
	uint ViewCount;
	uint Cut; // 1: no history (camera cut, first frame, occlusion off).
	uint Reserved;
};

[[vk::push_constant]] ConstantBuffer<EarlyConstants> Constants;
#endif

bool InFrustum(CullView view, float3 center, float3 extent)
{
	for (uint p = 0; p < 6; ++p)
	{
		const float4 plane = view.Planes[p];
		if (dot(plane.xyz, center) + plane.w < -dot(abs(plane.xyz), extent))
		{
			return false;
		}
	}
	return true;
}

bool Drawable(uint4 command)
{
	return command.x != 0 && command.y != 0;
}

#if CULL_RESET
[[vk::binding(0, 0)]] RWStructuredBuffer<uint> Counts;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x <= Constants.ViewCount)
	{
		Counts[id.x] = 0;
	}
}
#elif CULL_LATE
[[vk::binding(0, 0)]] StructuredBuffer<CullBounds> Bounds;
[[vk::binding(1, 0)]] StructuredBuffer<uint4> Commands;
[[vk::binding(2, 0)]] StructuredBuffer<CullView> Views;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> History; // 1: visible last frame.
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> Counts;
[[vk::binding(5, 0)]] RWStructuredBuffer<uint4> Arguments;
[[vk::binding(6, 0)]] Texture2D<float> Hzb;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Count)
	{
		return;
	}
	const uint4 command = Commands[id.x];
	const CullBounds bounds = Bounds[id.x];
	const float3 center = (bounds.Min.xyz + bounds.Max.xyz) * 0.5;
	const float3 extent = (bounds.Max.xyz - bounds.Min.xyz) * 0.5;
	const bool drawnEarly = Constants.Cut != 0 || History[id.x] != 0;
	const bool visible = Drawable(command) && InFrustum(Views[0], center, extent) && !HzbOccluded(Hzb, Constants.View, bounds.Min.xyz, bounds.Max.xyz);
	History[id.x] = visible ? 1u : 0u;
	if (!visible || drawnEarly)
	{
		return;
	}
	uint slot;
	InterlockedAdd(Counts[Constants.ViewCount], 1u, slot);
	Arguments[Constants.ViewCount * Constants.Count + slot] = command;
}
#else
[[vk::binding(0, 0)]] StructuredBuffer<CullBounds> Bounds;
[[vk::binding(1, 0)]] StructuredBuffer<uint4> Commands;
[[vk::binding(2, 0)]] StructuredBuffer<CullView> Views;
[[vk::binding(3, 0)]] StructuredBuffer<uint> History;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> Counts;
[[vk::binding(5, 0)]] RWStructuredBuffer<uint4> Arguments; // (ViewCount + 1) x Count; the last list is the late phase's.

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Count || id.y >= Constants.ViewCount)
	{
		return;
	}
	if (id.y == 0 && Constants.Cut == 0 && History[id.x] == 0)
	{
		return;
	}
	const uint4 command = Commands[id.x];
	const CullBounds bounds = Bounds[id.x];
	if (!Drawable(command) || !InFrustum(Views[id.y], (bounds.Min.xyz + bounds.Max.xyz) * 0.5, (bounds.Max.xyz - bounds.Min.xyz) * 0.5))
	{
		return;
	}
	uint slot;
	InterlockedAdd(Counts[id.y], 1u, slot);
	Arguments[id.y * Constants.Count + slot] = command;
}
#endif
