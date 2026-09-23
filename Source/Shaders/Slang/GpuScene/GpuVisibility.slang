// GPU-driven visibility and draw generation over the GPU Scene (items 49, 52-55).
// One thread per GPU Scene row. Mirrors VisibilityMath.h / VisibilityReference.cpp;
// bindings follow GpuVisibilityBindings.
#include "GpuScene/GpuSceneRecords.slang"
#include "GpuScene/GeometryRecords.slang"
#include "GpuScene/VisibilityRecords.slang"

struct VisibilityConstants
{
	uint RowCount;
	uint BinCount;
	uint MaterialSetCount;
	uint PageSlotCount;
};

[[vk::push_constant]] ConstantBuffer<VisibilityConstants> Constants;
[[vk::binding(0, 0)]] StructuredBuffer<GpuInstanceRecord> Instances;
[[vk::binding(1, 0)]] StructuredBuffer<GpuTransformRecord> Transforms;
[[vk::binding(2, 0)]] StructuredBuffer<GpuMeshMetadata> Meshes;
[[vk::binding(3, 0)]] StructuredBuffer<GpuSubmeshRecord> Submeshes;
[[vk::binding(4, 0)]] StructuredBuffer<GpuViewRecord> Views;
[[vk::binding(5, 0)]] StructuredBuffer<uint> MaterialBins;
[[vk::binding(6, 0)]] StructuredBuffer<VisibilityBinRange> BinRanges;
[[vk::binding(7, 0)]] StructuredBuffer<uint> IndexPages;
[[vk::binding(8, 0)]] RWStructuredBuffer<GpuLodState> LodStates;
[[vk::binding(9, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> Commands;
[[vk::binding(10, 0)]] RWStructuredBuffer<GpuDrawRecord> DrawRecords;
[[vk::binding(11, 0)]] RWStructuredBuffer<uint> Counts;
[[vk::binding(12, 0)]] RWStructuredBuffer<uint> Stats;

void Count(uint stat)
{
	uint ignored;
	InterlockedAdd(Stats[stat], 1u, ignored);
}

uint LodCount(GpuMeshMetadata mesh)
{
	return clamp(mesh.LodCount, 1u, GeometryMaxLods);
}

uint SelectLod(GpuMeshMetadata mesh, float errorScale, float threshold)
{
	uint lod = 0;
	const uint count = LodCount(mesh);
	for (uint i = 1; i < count; ++i)
	{
		if (mesh.Lods[i].Error * errorScale > threshold)
		{
			break;
		}
		lod = i;
	}
	return lod;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const uint row = id.x;
	if (row >= Constants.RowCount)
	{
		return;
	}
	const GpuInstanceRecord instance = Instances[row];
	if ((instance.Flags & GpuSceneFlagLive) == 0)
	{
		return;
	}
	Count(VisibilityStatTested);
	const uint drawable = GpuSceneFlagLive | GpuSceneFlagHasMesh | GpuSceneFlagVisible;
	uint meshCount;
	uint meshStride;
	Meshes.GetDimensions(meshCount, meshStride);
	if ((instance.Flags & drawable) != drawable || instance.MeshIndex >= meshCount)
	{
		Count(VisibilityStatNotDrawable);
		return;
	}

	// World bounding sphere of the local AABB under the current transform.
	const GpuViewRecord view = Views[0];
	const GpuTransformRecord transform = Transforms[instance.TransformIndex];
	const float3 center = GpuSceneTransformPoint(transform.Current, instance.LocalCenter);
	const float3 e = instance.LocalExtents;
	const bool unbounded = max(e.x, max(e.y, e.z)) >= 0.5e30;
	float scale = 0.0;
	for (uint c = 0; c < 3; ++c)
	{
		const float3 column = float3(transform.Current[0][c], transform.Current[1][c], transform.Current[2][c]);
		scale = max(scale, length(column));
	}
	const float radius = sqrt(dot(e, e)) * scale;

	if (!unbounded && (view.Flags & GpuViewFlagDisableFrustumCulling) == 0)
	{
		for (uint p = 0; p < 6; ++p)
		{
			const float4 plane = view.FrustumPlanes[p];
			if (dot(plane.xyz, center) + plane.w < -radius)
			{
				Count(VisibilityStatFrustumCulled);
				return;
			}
		}
	}
	Count(VisibilityStatVisible);

	// LOD with hysteresis against this row's history.
	const GpuMeshMetadata mesh = Meshes[instance.MeshIndex];
	const float distance = max(length(center - view.CameraPosition) - radius, 1.0e-4);
	const float errorScale = view.LodScale / distance;
	const float threshold = view.LodPixelError * exp2(instance.LodBias);
	const GpuLodState history = LodStates[row];
	const bool hasHistory = (view.Flags & GpuViewFlagResetLodHistory) == 0 && history.Generation == instance.Generation &&
		history.Lod < LodCount(mesh);
	uint lod = SelectLod(mesh, errorScale, threshold);
	if (hasHistory)
	{
		if (lod > history.Lod)
		{
			lod = max(history.Lod, SelectLod(mesh, errorScale, threshold * (1.0 - view.LodHysteresis)));
		}
		else if (lod < history.Lod)
		{
			lod = min(history.Lod, SelectLod(mesh, errorScale, threshold * (1.0 + view.LodHysteresis)));
		}
	}
	GpuLodState updated;
	updated.Generation = instance.Generation;
	updated.Lod = lod;
	LodStates[row] = updated;
	Count(VisibilityStatLodCounts + lod);

	// Bin: material bin x index-page slot.
	uint materialBin = instance.MaterialSet < Constants.MaterialSetCount ? MaterialBins[instance.MaterialSet] : 0u;
	const uint materialBins = Constants.BinCount / Constants.PageSlotCount;
	materialBin = materialBin < materialBins ? materialBin : 0u;
	uint slot = 0xffffffffu;
	for (uint i = 0; i < Constants.PageSlotCount; ++i)
	{
		if (IndexPages[i] == mesh.IndexPage)
		{
			slot = i;
			break;
		}
	}
	const uint first = mesh.LodCount == 0 ? mesh.FirstSubmesh : mesh.Lods[lod].FirstSubmesh;
	const uint count = mesh.LodCount == 0 ? mesh.SubmeshCount : mesh.Lods[lod].SubmeshCount;
	for (uint s = first; s < first + count; ++s)
	{
		if (slot == 0xffffffffu)
		{
			Count(VisibilityStatOtherPage);
			continue;
		}
		const uint bin = materialBin * Constants.PageSlotCount + slot;
		const VisibilityBinRange range = BinRanges[bin];
		uint index;
		InterlockedAdd(Counts[bin], 1u, index);
		if (index >= range.Capacity)
		{
			Count(VisibilityStatDropped);
			continue;
		}
		const GpuSubmeshRecord submesh = Submeshes[s];
		DrawIndexedIndirectCommand command;
		command.IndexCount = submesh.IndexCount;
		command.InstanceCount = 1;
		command.FirstIndex = submesh.FirstIndex;
		command.VertexOffset = submesh.VertexOffset;
		command.FirstInstance = range.First + index;
		Commands[range.First + index] = command;
		GpuDrawRecord record;
		record.InstanceRow = row;
		record.SubmeshRow = s;
		DrawRecords[range.First + index] = record;
		Count(VisibilityStatDraws);
	}
}
