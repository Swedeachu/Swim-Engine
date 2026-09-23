// GPU visibility layouts (Engine/Systems/Renderer/Visibility/*.h). The rules
// themselves live in GpuVisibility.slang and VisibilityMath.h.
#ifndef SWIM_VISIBILITY_RECORDS_SLANG
#define SWIM_VISIBILITY_RECORDS_SLANG

static const uint GpuViewFlagDisableFrustumCulling = 1u << 0;
static const uint GpuViewFlagResetLodHistory = 1u << 1;
static const uint GpuViewFlagResetOcclusionHistory = 1u << 2;
static const uint GpuViewFlagForwardDepth = 1u << 3;
static const uint GpuViewFlagDisableOcclusion = 1u << 4;

// VisibilityPhase.
static const uint VisibilityPhaseSingle = 0;
static const uint VisibilityPhaseEarly = 1;
static const uint VisibilityPhaseLate = 2;

struct GpuViewRecord
{
	float4 ViewProjection[4]; // Row-major rows.
	float4 FrustumPlanes[6];
	float3 CameraPosition;
	float LodScale;
	float LodPixelError;
	float LodHysteresis;
	uint Flags;
	uint Reserved;
};

struct DrawIndexedIndirectCommand
{
	uint IndexCount;
	uint InstanceCount;
	uint FirstIndex;
	int VertexOffset;
	uint FirstInstance;
};

struct GpuDrawRecord
{
	uint InstanceRow;
	uint SubmeshRow;
};

struct GpuLodState
{
	uint Generation;
	uint Lod;
};

struct VisibilityBinRange
{
	uint First;
	uint Capacity;
};

// VisibilityStats as uint offsets.
static const uint VisibilityStatTested = 0;
static const uint VisibilityStatFrustumCulled = 1;
static const uint VisibilityStatNotDrawable = 2;
static const uint VisibilityStatVisible = 3;
static const uint VisibilityStatDraws = 4;
static const uint VisibilityStatDropped = 5;
static const uint VisibilityStatOtherPage = 6;
static const uint VisibilityStatOccluded = 7;
static const uint VisibilityStatDeferred = 8;
static const uint VisibilityStatAlreadyDrawn = 9;
static const uint VisibilityStatLodCounts = 12;

#endif
