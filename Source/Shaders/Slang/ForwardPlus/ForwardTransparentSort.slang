// Transparent Forward+ sort (critical-path item 67). One 256-thread group per
// index-page slot sorts that slot's transparent visibility bin back to front
// (ForwardPlus::SortsBefore: bounds-center depth along the camera forward axis,
// then instance row, then submesh row) with a bitonic network in the scratch
// buffer, then writes the commands in draw order and zeroes the unused ones, so
// the result never depends on the bin's atomic compaction order. Mirrors
// ForwardPlus::SortTransparentDraws; bindings follow ForwardTransparentSortBindings.
#include "ForwardPlus/ForwardPlusRecords.slang"
#include "GpuScene/GpuSceneRecords.slang"
#include "GpuScene/VisibilityRecords.slang"

struct SortConstants
{
	uint FirstBin;	   // Transparent material bin * page slots.
	uint FirstCommand; // Slot 0's range.First; slot s starts at FirstCommand + s * Capacity.
	uint Capacity;
	uint SortSize; // Next power of two of Capacity.
};

[[vk::binding(0, 0)]] StructuredBuffer<DrawIndexedIndirectCommand> Commands;
[[vk::binding(1, 0)]] StructuredBuffer<GpuDrawRecord> DrawRecords;
[[vk::binding(2, 0)]] StructuredBuffer<uint> Counts;
[[vk::binding(3, 0)]] StructuredBuffer<GpuInstanceRecord> Instances;
[[vk::binding(4, 0)]] StructuredBuffer<GpuTransformRecord> Transforms;
[[vk::binding(5, 0)]] StructuredBuffer<ForwardView> Views;
[[vk::binding(6, 0)]] globallycoherent RWStructuredBuffer<ForwardSortEntry> Scratch;
[[vk::binding(7, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> SortedCommands;
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> SortedCounts;
[[vk::push_constant]] ConstantBuffer<SortConstants> Constants;

static const uint Threads = 256;

// ForwardPlus::SortDepth.
float SortDepth(GpuDrawRecord record, ForwardView view)
{
	const GpuInstanceRecord instance = Instances[record.InstanceRow];
	const float3 center = GpuSceneTransformPoint(Transforms[instance.TransformIndex].Current, instance.LocalCenter);
	return dot(center - view.CameraPosition, view.CameraForward);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
	const uint slot = group.x;
	const uint firstCommand = Constants.FirstCommand + slot * Constants.Capacity;
	const uint base = slot * Constants.SortSize;
	const uint count = min(Counts[Constants.FirstBin + slot], Constants.Capacity);
	const ForwardView view = Views[0];

	for (uint i = thread.x; i < Constants.SortSize; i += Threads)
	{
		ForwardSortEntry entry;
		if (i < count)
		{
			const GpuDrawRecord record = DrawRecords[firstCommand + i];
			entry.Depth = SortDepth(record, view);
			entry.InstanceRow = record.InstanceRow;
			entry.SubmeshRow = record.SubmeshRow;
			entry.Slot = firstCommand + i;
		}
		else
		{
			// Padding sorts last.
			entry.Depth = -3.0e38;
			entry.InstanceRow = 0xffffffffu;
			entry.SubmeshRow = 0xffffffffu;
			entry.Slot = 0xffffffffu;
		}
		Scratch[base + i] = entry;
	}
	AllMemoryBarrierWithGroupSync();

	// Bitonic network over SortSize entries; every pair is owned by its lower index.
	for (uint k = 2; k <= Constants.SortSize; k <<= 1)
	{
		for (uint j = k >> 1; j > 0; j >>= 1)
		{
			for (uint i = thread.x; i < Constants.SortSize; i += Threads)
			{
				const uint partner = i ^ j;
				if (partner > i)
				{
					const ForwardSortEntry a = Scratch[base + i];
					const ForwardSortEntry b = Scratch[base + partner];
					const bool ascending = (i & k) == 0;
					const bool swap = ascending ? ForwardSortsBefore(b, a) : ForwardSortsBefore(a, b);
					if (swap)
					{
						Scratch[base + i] = b;
						Scratch[base + partner] = a;
					}
				}
			}
			AllMemoryBarrierWithGroupSync();
		}
	}

	for (uint i = thread.x; i < Constants.Capacity; i += Threads)
	{
		DrawIndexedIndirectCommand command;
		command.IndexCount = 0;
		command.InstanceCount = 0;
		command.FirstIndex = 0;
		command.VertexOffset = 0;
		command.FirstInstance = 0;
		if (i < count)
		{
			// FirstInstance keeps naming the original draw record.
			command = Commands[Scratch[base + i].Slot];
		}
		SortedCommands[slot * Constants.Capacity + i] = command;
	}
	if (thread.x == 0)
	{
		SortedCounts[slot] = count;
	}
}
