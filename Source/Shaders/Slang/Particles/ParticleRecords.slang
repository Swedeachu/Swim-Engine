// GPU particles (critical-path item 77): mirrors ParticleRecords.h and every
// Particles:: function of ParticleReference.h.
#ifndef SWIM_PARTICLE_RECORDS_SLANG
#define SWIM_PARTICLE_RECORDS_SLANG

struct Particle
{
	float3 Position;
	float Age;
	float3 Velocity;
	float Lifetime; // 0: a free slot.
	float Size;
	float Rotation;
	float AngularVelocity;
	uint Id;
};

struct ParticleEmitter
{
	float4 Transform[3];
	float3 Gravity;
	float Drag;
	float3 ShapeExtent;
	uint Shape;
	float3 Direction;
	float CosConeAngle;
	float SpeedMin;
	float SpeedMax;
	float LifetimeMin;
	float LifetimeMax;
	float SizeMin;
	float SizeMax;
	float RotationMin;
	float RotationMax;
	float AngularVelocityMin;
	float AngularVelocityMax;
	float GroundHeight;
	float Restitution;
	float Friction;
	uint Flags;
	uint FirstSlot;
	uint Capacity;
	uint SpawnCount;
	uint FirstId;
	uint Seed;
	uint TextureIndex;
	uint SamplerIndex;
	uint FlipbookColumns;
	uint FlipbookRows;
	uint FlipbookFrames;
	float FlipbookFrameRate;
	uint SizeKeyCount;
	uint ColorKeyCount;
	uint Row;
	float4 SizeTimes;
	float4 SizeValues;
	float4 ColorTimes;
	float4 ColorValues[4];
};

struct ParticleCounters
{
	uint Free;
	uint Alive;
	uint Dropped;
	uint Reserved;
};

struct ParticleFrame
{
	float4 ViewProjection[4];
	float3 CameraPosition;
	float DeltaTime;
	float3 CameraRight;
	uint EmitterCount;
	float3 CameraUp;
	uint MaxSpawn;
	float3 CameraForward;
	float Reserved;
};

static const uint ParticleFlagLocalSpace = 1u << 0;
static const uint ParticleFlagCollision = 1u << 1;
static const uint ParticleFlagSorted = 1u << 2;
static const uint ParticleFlagTextured = 1u << 3;
static const uint ParticleShapeSphere = 1u;
static const uint ParticleShapeBox = 2u;
static const float ParticlePi = 3.14159265358979;

// Particles::Hash (PCG).
uint ParticleHash(uint value)
{
	const uint state = value * 747796405u + 2891336453u;
	const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

// Particles::Random.
float ParticleRandom(uint id, uint seed, uint stream)
{
	const uint h = ParticleHash(ParticleHash(seed + stream * 0x9E3779B9u) ^ id);
	return float(h >> 8) * (1.0 / 16777216.0);
}

float3 ParticleTransformPoint(float4 rows[3], float3 p)
{
	return float3(dot(rows[0], float4(p, 1.0)), dot(rows[1], float4(p, 1.0)), dot(rows[2], float4(p, 1.0)));
}

float3 ParticleTransformVector(float4 rows[3], float3 v)
{
	return float3(dot(rows[0].xyz, v), dot(rows[1].xyz, v), dot(rows[2].xyz, v));
}

// Particles::EvaluateCurve.
float ParticleCurve(float4 times, float4 values, uint keyCount, float t)
{
	if (keyCount <= 1u || t <= times[0])
	{
		return values[0];
	}
	for (uint k = 1; k < keyCount; ++k)
	{
		if (t <= times[k])
		{
			const float f = (t - times[k - 1]) / (times[k] - times[k - 1]);
			return values[k - 1] + (values[k] - values[k - 1]) * f;
		}
	}
	return values[keyCount - 1];
}

float ParticleNormalizedAge(Particle p)
{
	return clamp(p.Age / p.Lifetime, 0.0, 1.0);
}

float3 ParticleWorldPosition(float4 transform[3], uint flags, Particle p)
{
	return (flags & ParticleFlagLocalSpace) != 0u ? ParticleTransformPoint(transform, p.Position) : p.Position;
}

#endif
