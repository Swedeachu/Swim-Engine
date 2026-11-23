// The box field's per-frame compute (Game::BoxField), three programs from one source:
//  - BoxFieldGenerate (BOX_GENERATE=1): one box per thread on a grid, bobbing with time,
//    this frame's and last frame's centre (motion vectors), coloured from the streamed rows;
//  - BoxFieldReset (BOX_RESET=1): zeroes the per-view draw counts and the late count;
//  - BoxFieldCull (default): tests every box against every view's frustum and appends a
//    draw (36 vertices, first instance = the box) to that view's indirect arguments; in
//    the camera view (view 0) only boxes visible last frame (all of them on a cut);
//  - BoxFieldLateCull (BOX_LATE=1): after the depth prepass, tests the camera view's boxes
//    against the HZB, appends the visible ones the early cull skipped to the late slot
//    (ViewCount) and records every box's visibility for the next frame.

#ifndef BOX_GENERATE
#define BOX_GENERATE 0
#endif
#ifndef BOX_RESET
#define BOX_RESET 0
#endif
#ifndef BOX_LATE
#define BOX_LATE 0
#endif

struct Box
{
	float4 CenterHalf;	   // xyz: centre, w: half extent.
	float4 PreviousCenter; // xyz: last frame's centre.
	float4 Color;		   // rgb: base colour, a: roughness.
};

struct CullView
{
	float4 Planes[6]; // xyz . p + w >= 0 inside.
};

struct DrawArguments
{
	uint VertexCount;
	uint InstanceCount;
	uint FirstVertex;
	uint FirstInstance;
};

struct BoxConstants
{
	float4 Origin;	 // xyz: field corner; w: spacing.
	float4 Time;	 // x: now, y: last frame.
	uint BoxCount;
	uint Columns;
	uint ViewCount;
	uint Cut; // 1: no history (camera cut, first frame).
};

#if BOX_LATE
#include "Features/HzbOcclusion.slang"

struct LateConstants
{
	HzbView View;
	uint BoxCount;
	uint ViewCount;
	uint Cut;
	uint Reserved;
};

[[vk::push_constant]] ConstantBuffer<LateConstants> Constants;
#else
[[vk::push_constant]] ConstantBuffer<BoxConstants> Constants;
#endif

bool InFrustum(CullView view, float3 center, float extent)
{
	for (uint p = 0; p < 6; ++p)
	{
		const float4 plane = view.Planes[p];
		const float reach = extent * (abs(plane.x) + abs(plane.y) + abs(plane.z));
		if (dot(plane.xyz, center) + plane.w < -reach)
		{
			return false;
		}
	}
	return true;
}

float Hash(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return float(x & 0xffffffu) / 16777216.0;
}

#if BOX_GENERATE
[[vk::binding(0, 0)]] RWStructuredBuffer<Box> Boxes;
[[vk::binding(1, 0)]] StructuredBuffer<float4> Colors;	// Rows of Columns colours, streamed in.
[[vk::binding(2, 0)]] StructuredBuffer<uint> RowOffsets; // Where each row's colours start (in float4s).

float3 BoxCenter(uint index, float time)
{
	const uint column = index % Constants.Columns;
	const uint row = index / Constants.Columns;
	const float spacing = Constants.Origin.w;
	const float phase = Hash(index) * 6.2831853;
	const float height = 0.6 + 0.5 * sin(time * 1.3 + phase);
	return Constants.Origin.xyz + float3(float(column) * spacing, height, float(row) * spacing);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.BoxCount)
	{
		return;
	}
	Box box;
	box.CenterHalf = float4(BoxCenter(id.x, Constants.Time.x), 0.2 + 0.15 * Hash(id.x * 3u + 1u));
	box.PreviousCenter = float4(BoxCenter(id.x, Constants.Time.y), 0.0);
	box.Color = Colors[RowOffsets[id.x / Constants.Columns] + id.x % Constants.Columns];
	Boxes[id.x] = box;
}
#elif BOX_RESET
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
#elif BOX_LATE
[[vk::binding(0, 0)]] StructuredBuffer<Box> Boxes;
[[vk::binding(1, 0)]] StructuredBuffer<CullView> Views;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> History; // 1: visible last frame.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> Counts;
[[vk::binding(4, 0)]] RWStructuredBuffer<DrawArguments> Arguments;
[[vk::binding(5, 0)]] Texture2D<float> Hzb;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.BoxCount)
	{
		return;
	}
	const Box box = Boxes[id.x];
	const float3 center = box.CenterHalf.xyz;
	const float extent = box.CenterHalf.w;
	const bool drawnEarly = Constants.Cut != 0 || History[id.x] != 0;
	const bool visible = InFrustum(Views[0], center, extent) && !HzbOccluded(Hzb, Constants.View, center - extent, center + extent);
	History[id.x] = visible ? 1u : 0u;
	if (!visible || drawnEarly)
	{
		return;
	}
	uint slot;
	InterlockedAdd(Counts[Constants.ViewCount], 1u, slot);
	DrawArguments arguments;
	arguments.VertexCount = 36;
	arguments.InstanceCount = 1;
	arguments.FirstVertex = 0;
	arguments.FirstInstance = id.x;
	Arguments[Constants.ViewCount * Constants.BoxCount + slot] = arguments;
}
#else
[[vk::binding(0, 0)]] StructuredBuffer<Box> Boxes;
[[vk::binding(1, 0)]] StructuredBuffer<CullView> Views;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> Counts;
[[vk::binding(3, 0)]] RWStructuredBuffer<DrawArguments> Arguments; // (ViewCount + 1) x BoxCount; the last slot is the late phase's.
[[vk::binding(4, 0)]] StructuredBuffer<uint> History;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.BoxCount || id.y >= Constants.ViewCount)
	{
		return;
	}
	if (id.y == 0 && Constants.Cut == 0 && History[id.x] == 0)
	{
		return;
	}
	const Box box = Boxes[id.x];
	if (!InFrustum(Views[id.y], box.CenterHalf.xyz, box.CenterHalf.w))
	{
		return;
	}
	uint slot;
	InterlockedAdd(Counts[id.y], 1u, slot);
	DrawArguments arguments;
	arguments.VertexCount = 36;
	arguments.InstanceCount = 1;
	arguments.FirstVertex = 0;
	arguments.FirstInstance = id.x;
	Arguments[id.y * Constants.BoxCount + slot] = arguments;
}
#endif
