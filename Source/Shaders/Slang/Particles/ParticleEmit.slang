// Particle emission (item 77): Particles::SpawnParticle for this frame's spawns (x:
// spawn index, y: emitter), each popping a free slot. Spawns beyond the free list are
// dropped and counted. The first thread of each emitter also resets its live count for
// the compact pass. Bindings follow ParticleProgramBindings::Emit.
#include "Particles/ParticleRecords.slang"

[[vk::binding(1, 0)]] StructuredBuffer<ParticleEmitter> Emitters;
[[vk::binding(2, 0)]] RWStructuredBuffer<Particle> Particles;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> FreeList;
[[vk::binding(5, 0)]] RWStructuredBuffer<ParticleCounters> Counters;

// Particles::SpawnParticle.
Particle Spawn(ParticleEmitter e, uint particleId)
{
	Particle p;
	p.Id = particleId;
	p.Age = 0.0;
	p.Lifetime = e.LifetimeMin + (e.LifetimeMax - e.LifetimeMin) * ParticleRandom(particleId, e.Seed, 0u);
	const float speed = e.SpeedMin + (e.SpeedMax - e.SpeedMin) * ParticleRandom(particleId, e.Seed, 1u);
	const float cosTheta = 1.0 - ParticleRandom(particleId, e.Seed, 2u) * (1.0 - e.CosConeAngle);
	const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
	const float phi = 2.0 * ParticlePi * ParticleRandom(particleId, e.Seed, 3u);
	const float3 d = e.Direction;
	const float axisSign = d.z >= 0.0 ? 1.0 : -1.0;
	const float a = -1.0 / (axisSign + d.z);
	const float b = d.x * d.y * a;
	const float3 tangent = float3(1.0 + axisSign * d.x * d.x * a, axisSign * b, -axisSign * d.x);
	const float3 bitangent = float3(b, axisSign + d.y * d.y * a, -d.y);
	float3 velocity = (tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + d * cosTheta) * speed;
	float3 position = float3(0.0, 0.0, 0.0);
	if (e.Shape == ParticleShapeSphere)
	{
		const float radius = e.ShapeExtent.x * pow(ParticleRandom(particleId, e.Seed, 4u), 1.0 / 3.0);
		const float z = 2.0 * ParticleRandom(particleId, e.Seed, 5u) - 1.0;
		const float ring = sqrt(max(0.0, 1.0 - z * z));
		const float angle = 2.0 * ParticlePi * ParticleRandom(particleId, e.Seed, 6u);
		position = float3(radius * ring * cos(angle), radius * ring * sin(angle), radius * z);
	}
	else if (e.Shape == ParticleShapeBox)
	{
		position = e.ShapeExtent * (2.0 * float3(ParticleRandom(particleId, e.Seed, 4u), ParticleRandom(particleId, e.Seed, 5u),
			ParticleRandom(particleId, e.Seed, 6u)) - 1.0);
	}
	p.Size = e.SizeMin + (e.SizeMax - e.SizeMin) * ParticleRandom(particleId, e.Seed, 7u);
	p.Rotation = e.RotationMin + (e.RotationMax - e.RotationMin) * ParticleRandom(particleId, e.Seed, 8u);
	p.AngularVelocity = e.AngularVelocityMin + (e.AngularVelocityMax - e.AngularVelocityMin) * ParticleRandom(particleId, e.Seed, 9u);
	if ((e.Flags & ParticleFlagLocalSpace) == 0u)
	{
		position = ParticleTransformPoint(e.Transform, position);
		velocity = ParticleTransformVector(e.Transform, velocity);
	}
	p.Position = position;
	p.Velocity = velocity;
	return p;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ParticleEmitter e = Emitters[id.y];
	if (id.x == 0u)
	{
		Counters[e.Row].Alive = 0u;
	}
	if (id.x >= e.SpawnCount)
	{
		return;
	}
	uint previous;
	InterlockedAdd(Counters[e.Row].Free, 0xFFFFFFFFu, previous);
	if (int(previous) <= 0)
	{
		InterlockedAdd(Counters[e.Row].Free, 1u);
		InterlockedAdd(Counters[e.Row].Dropped, 1u);
		return;
	}
	const uint slot = FreeList[e.FirstSlot + previous - 1u];
	Particles[slot] = Spawn(e, e.FirstId + id.x);
}
