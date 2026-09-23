// GeometryHeap record layouts (Engine/Systems/Renderer/Geometry/GpuMeshMetadata.h).
// ShaderCompiler.GpuSceneLayout compares the reflected offsets with the C++ structs.
#ifndef SWIM_GEOMETRY_RECORDS_SLANG
#define SWIM_GEOMETRY_RECORDS_SLANG

static const uint GeometryMaxLods = 8;
static const uint GeometryInvalidPage = 0xffffffffu;

struct GpuMeshLod
{
	uint FirstSubmesh;
	uint SubmeshCount;
	float Error;
	uint Padding;
};

struct GpuSubmeshRecord
{
	uint FirstIndex;
	uint IndexCount;
	int VertexOffset;
	uint MaterialSlot;
};

struct GpuMeshMetadata
{
	uint VertexPage;
	uint VertexOffset;
	uint VertexCount;
	uint VertexStride;
	uint IndexPage;
	uint FirstIndex;
	uint IndexCount;
	uint IndexBytes;
	uint VertexLayout;
	uint LodCount;
	uint MeshletPage;
	uint MeshletOffset;
	uint MeshletCount;
	uint Generation;
	uint FirstSubmesh;
	uint SubmeshCount;
	GpuMeshLod Lods[GeometryMaxLods];
};

#endif
