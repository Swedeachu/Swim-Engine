// Particle simulation (item 77): Particles::SimulateParticle for every occupied slot of
// every emitter (x: slot in the emitter's range, y: emitter). Dead particles free their
// slot. Bindings follow ParticleProgramBindings::Simulate.
#include "Particles/ParticleRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ParticleFrame> Frame;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleEmitter> Emitters;
[[vk::binding(2, 0)]] RWStructuredBuffer<Particle> Particles;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> FreeList;
[[vk::binding(5, 0)]] RWStructuredBuffer<ParticleCounters> Counters;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const uint emitterIndex = id.y;
	const uint capacity = Emitters[emitterIndex].Capacity;
	if (id.x >= capacity)
	{
		return;
	}
	const uint firstSlot = Emitters[emitterIndex].FirstSlot;
	const uint slot = firstSlot + id.x;
	Particle p = Particles[slot];
	if (!(p.Lifetime > 0.0))
	{
		return;
	}
	const float dt = Frame[0].DeltaTime;
	const float age = p.Age + dt;
	if (age >= p.Lifetime)
	{
		Particles[slot].Lifetime = 0.0;
		uint previous;
		InterlockedAdd(Counters[Emitters[emitterIndex].Row].Free, 1u, previous);
		FreeList[firstSlot + previous] = slot;
		return;
	}
	const float3 gravity = Emitters[emitterIndex].Gravity;
	const float damping = max(0.0, 1.0 - Emitters[emitterIndex].Drag * dt);
	p.Velocity = (p.Velocity + gravity * dt) * damping;
	p.Position = p.Position + p.Velocity * dt;
	p.Rotation = p.Rotation + p.AngularVelocity * dt;
	p.Age = age;
	if ((Emitters[emitterIndex].Flags & ParticleFlagCollision) != 0u)
	{
		const float ground = Emitters[emitterIndex].GroundHeight;
		if (p.Position.y < ground)
		{
			p.Position.y = ground;
			if (p.Velocity.y < 0.0)
			{
				const float friction = Emitters[emitterIndex].Friction;
				p.Velocity.y = -p.Velocity.y * Emitters[emitterIndex].Restitution;
				p.Velocity.x *= 1.0 - friction;
				p.Velocity.z *= 1.0 - friction;
			}
		}
	}
	Particles[slot] = p;
}
