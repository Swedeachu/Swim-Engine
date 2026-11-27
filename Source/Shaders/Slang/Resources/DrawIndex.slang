// Vertex and instance indices that include the draw's first vertex (or vertex offset) and
// first instance on every target, as SPIR-V's VertexIndex / InstanceIndex do, without the
// SPIR-V DrawParameters capability. DXIL's SV_VertexID / SV_InstanceID start at zero, so the
// draw base is added back: shader model 6.8 reads SV_StartVertexLocation /
// SV_StartInstanceLocation (direct and indirect draws); SWIM_DRAW_BASE_ROOT_CONSTANTS reads
// root constants the backend sets before each direct draw instead.
//
//     VertexOutput vertexMain(uint vertex : SWIM_VERTEX_INDEX, uint instance : SWIM_INSTANCE_INDEX
//         SWIM_DRAW_BASE_PARAMETERS)
//     {
//         const uint vertexId = DrawVertexIndex(vertex);
//         const uint instanceId = DrawInstanceIndex(instance);
#pragma once

#if defined(SWIM_TARGET_DXIL)

#define SWIM_VERTEX_INDEX SV_VertexID
#define SWIM_INSTANCE_INDEX SV_InstanceID

#if defined(SWIM_DRAW_BASE_ROOT_CONSTANTS)

struct DrawBaseConstants
{
	uint FirstVertex;
	uint FirstInstance;
};

// The backend reserves this register for the draw base of graphics pipelines.
ConstantBuffer<DrawBaseConstants> DrawBase : register(b0, space1000);

#define SWIM_DRAW_BASE_PARAMETERS

uint DrawVertexIndex(uint vertex)
{
	return vertex + DrawBase.FirstVertex;
}

uint DrawInstanceIndex(uint instance)
{
	return instance + DrawBase.FirstInstance;
}

#else

// Entry-point inputs, so the helpers are macros over them.
#define SWIM_DRAW_BASE_PARAMETERS , uint drawFirstVertex : SV_StartVertexLocation, uint drawFirstInstance : SV_StartInstanceLocation

#define DrawVertexIndex(vertex) ((vertex) + drawFirstVertex)
#define DrawInstanceIndex(instance) ((instance) + drawFirstInstance)

#endif

#else

#define SWIM_VERTEX_INDEX SV_VulkanVertexID
#define SWIM_INSTANCE_INDEX SV_VulkanInstanceID
#define SWIM_DRAW_BASE_PARAMETERS

uint DrawVertexIndex(uint vertex)
{
	return vertex;
}

uint DrawInstanceIndex(uint instance)
{
	return instance;
}

#endif
