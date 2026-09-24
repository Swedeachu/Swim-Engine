// Particle draw finalization (item 77), one group per emitter: alpha-blended emitters
// sort their draw list back to front (Particles::DrawsBefore: larger view depth first,
// then lower id) with a shared-memory bitonic network, then the group writes the
// emitter's DrawIndexedIndirectCommand (six indices, one instance per live particle).
// Bindings follow ParticleProgramBindings::Finalize.
#include "Particles/ParticleRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ParticleFrame> Frame;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleEmitter> Emitters;
[[vk::binding(2, 0)]] StructuredBuffer<Particle> Particles;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> DrawList;
[[vk::binding(5, 0)]] RWStructuredBuffer<ParticleCounters> Counters;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint> DrawArgs;

static const uint MaxSorted = 2048; // MaxSortedParticles.
static const uint GroupSize = 256;	// ParticleFinalizeGroupSize.

groupshared float Keys[MaxSorted];
groupshared uint Ids[MaxSorted];
groupshared uint Slots[MaxSorted];

bool DrawsBefore(float depthA, uint idA, float depthB, uint idB)
{
	return depthA > depthB || (depthA == depthB && idA < idB);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
	const uint emitterIndex = group.x;
	const uint row = Emitters[emitterIndex].Row;
	const uint alive = Counters[row].Alive;
	const uint flags = Emitters[emitterIndex].Flags;
	const uint firstSlot = Emitters[emitterIndex].FirstSlot;
	if ((flags & ParticleFlagSorted) != 0u && alive > 1u)
	{
		float4 transform[3];
		for (uint r = 0; r < 3; ++r)
		{
			transform[r] = Emitters[emitterIndex].Transform[r];
		}
		const float3 camera = Frame[0].CameraPosition;
		const float3 forward = Frame[0].CameraForward;
		uint size = 1u;
		while (size < alive)
		{
			size <<= 1u;
		}
		for (uint i = thread.x; i < size; i += GroupSize)
		{
			if (i < alive)
			{
				const uint slot = DrawList[firstSlot + i];
				const Particle p = Particles[slot];
				const float3 world = ParticleWorldPosition(transform, flags, p);
				Keys[i] = dot(world - camera, forward);
				Ids[i] = p.Id;
				Slots[i] = slot;
			}
			else
			{
				Keys[i] = -3.402823466e+38; // Padding sorts last.
				Ids[i] = 0xFFFFFFFFu;
				Slots[i] = 0u;
			}
		}
		GroupMemoryBarrierWithGroupSync();
		for (uint k = 2u; k <= size; k <<= 1u)
		{
			for (uint j = k >> 1u; j > 0u; j >>= 1u)
			{
				for (uint i = thread.x; i < size; i += GroupSize)
				{
					const uint partner = i ^ j;
					if (partner > i)
					{
						const bool ascending = (i & k) == 0u;
						const bool before = DrawsBefore(Keys[i], Ids[i], Keys[partner], Ids[partner]);
						// "Ascending" here means draw order: element i must draw before its partner.
						if (before != ascending)
						{
							const float key = Keys[i];
							const uint particleId = Ids[i];
							const uint slot = Slots[i];
							Keys[i] = Keys[partner];
							Ids[i] = Ids[partner];
							Slots[i] = Slots[partner];
							Keys[partner] = key;
							Ids[partner] = particleId;
							Slots[partner] = slot;
						}
					}
				}
				GroupMemoryBarrierWithGroupSync();
			}
		}
		for (uint i = thread.x; i < alive; i += GroupSize)
		{
			DrawList[firstSlot + i] = Slots[i];
		}
	}
	if (thread.x == 0u)
	{
		DrawArgs[row * 5u + 0u] = 6u;
		DrawArgs[row * 5u + 1u] = alive;
		DrawArgs[row * 5u + 2u] = 0u;
		DrawArgs[row * 5u + 3u] = 0u;
		DrawArgs[row * 5u + 4u] = 0u;
	}
}
