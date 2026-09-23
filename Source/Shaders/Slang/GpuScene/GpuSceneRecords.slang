// GPU Scene record layouts shared by every shader that reads the scene. They
// mirror Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h and
// GpuTransformRecord.h exactly (std430 structured-buffer layout); the
// ShaderCompiler.GpuSceneLayout test compares the reflected offsets.
#ifndef SWIM_GPU_SCENE_RECORDS_SLANG
#define SWIM_GPU_SCENE_RECORDS_SLANG

static const uint GpuSceneFlagVisible = 1u << 0;
static const uint GpuSceneFlagCastShadows = 1u << 1;
static const uint GpuSceneFlagStatic = 1u << 2;
static const uint GpuSceneFlagLive = 1u << 30;
static const uint GpuSceneFlagHasMesh = 1u << 31;
static const uint GpuSceneInvalidIndex = 0xffffffffu;

struct GpuInstanceRecord
{
	float3 LocalCenter;
	uint MeshIndex;
	float3 LocalExtents;
	uint MeshGeneration;
	uint TransformIndex;
	uint MaterialSet;
	uint ObjectId;
	uint Flags;
	uint SkinIndex;
	float LodBias;
	uint Generation;
	uint Reserved;
};

// Row-major 3x4 affine rows: world = float3(dot(Current[0], p1), dot(Current[1], p1), dot(Current[2], p1)).
struct GpuTransformRecord
{
	float4 Current[3];
	float4 Previous[3];
};

bool GpuSceneIsDrawable(GpuInstanceRecord instance)
{
	const uint required = GpuSceneFlagLive | GpuSceneFlagHasMesh | GpuSceneFlagVisible;
	return (instance.Flags & required) == required;
}

float3 GpuSceneTransformPoint(float4 rows[3], float3 p)
{
	const float4 h = float4(p, 1.0);
	return float3(dot(rows[0], h), dot(rows[1], h), dot(rows[2], h));
}

#endif
