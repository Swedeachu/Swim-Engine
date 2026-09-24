// Particle compaction (item 77): every occupied slot of every emitter (x: slot, y:
// emitter) appends itself to the emitter's draw list. Bindings follow
// ParticleProgramBindings::Compact.
#include "Particles/ParticleRecords.slang"

[[vk::binding(1, 0)]] StructuredBuffer<ParticleEmitter> Emitters;
[[vk::binding(2, 0)]] StructuredBuffer<Particle> Particles;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> DrawList;
[[vk::binding(5, 0)]] RWStructuredBuffer<ParticleCounters> Counters;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const uint capacity = Emitters[id.y].Capacity;
	if (id.x >= capacity)
	{
		return;
	}
	const uint firstSlot = Emitters[id.y].FirstSlot;
	const uint slot = firstSlot + id.x;
	if (!(Particles[slot].Lifetime > 0.0))
	{
		return;
	}
	uint index;
	InterlockedAdd(Counters[Emitters[id.y].Row].Alive, 1u, index);
	DrawList[firstSlot + index] = slot;
}
