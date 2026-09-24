#pragma once
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{
	// GPU records of the particle system (Shaders/Slang/Particles/ParticleRecords.slang).

	// One pool slot. Lifetime 0 marks a free slot.
	struct GpuParticle
	{
		float Position[3] = {}; // Simulation space: world, or the emitter's for ParticleSpace::Local.
		float Age = 0.0f;
		float Velocity[3] = {};
		float Lifetime = 0.0f;
		float Size = 0.0f; // Spawn edge length; SizeOverLife multiplies it.
		float Rotation = 0.0f;
		float AngularVelocity = 0.0f;
		std::uint32_t Id = 0; // Emission serial number within the emitter.
	};

	static_assert(sizeof(GpuParticle) == 48);

	// GpuParticleEmitter::Flags.
	inline constexpr std::uint32_t ParticleFlagLocalSpace = 1u << 0;
	inline constexpr std::uint32_t ParticleFlagCollision = 1u << 1;
	inline constexpr std::uint32_t ParticleFlagSorted = 1u << 2; // Alpha-blended: back-to-front draw list.
	inline constexpr std::uint32_t ParticleFlagTextured = 1u << 3;

	// ParticleShape as packed.
	inline constexpr std::uint32_t ParticleShapePoint = 0;
	inline constexpr std::uint32_t ParticleShapeSphere = 1;
	inline constexpr std::uint32_t ParticleShapeBox = 2;

	// One emitter row, uploaded every frame (spawn counts and the transform change).
	struct GpuParticleEmitter
	{
		float Transform[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }; // Emitter -> world rows (rigid or scaled affine).
		float Gravity[3] = {};
		float Drag = 0.0f;
		float ShapeExtent[3] = {};
		std::uint32_t Shape = ParticleShapePoint;
		float Direction[3] = { 0, 1, 0 };
		float CosConeAngle = 1.0f;
		float SpeedMin = 0.0f, SpeedMax = 0.0f, LifetimeMin = 1.0f, LifetimeMax = 1.0f;
		float SizeMin = 0.0f, SizeMax = 0.0f, RotationMin = 0.0f, RotationMax = 0.0f;
		float AngularVelocityMin = 0.0f, AngularVelocityMax = 0.0f, GroundHeight = 0.0f, Restitution = 0.0f;
		float Friction = 0.0f;
		std::uint32_t Flags = 0;
		std::uint32_t FirstSlot = 0; // Pool range [FirstSlot, FirstSlot + Capacity).
		std::uint32_t Capacity = 0;
		std::uint32_t SpawnCount = 0; // This frame.
		std::uint32_t FirstId = 0;	  // Id of this frame's first spawn.
		std::uint32_t Seed = 0;
		std::uint32_t TextureIndex = 0;
		std::uint32_t SamplerIndex = 0;
		std::uint32_t FlipbookColumns = 1;
		std::uint32_t FlipbookRows = 1;
		std::uint32_t FlipbookFrames = 1;
		float FlipbookFrameRate = 0.0f;
		std::uint32_t SizeKeyCount = 1;
		std::uint32_t ColorKeyCount = 1;
		std::uint32_t Row = 0; // The emitter's persistent counter and draw-argument row.
		float SizeTimes[4] = { 0, 1, 1, 1 };
		float SizeValues[4] = { 1, 1, 1, 1 };
		float ColorTimes[4] = { 0, 1, 1, 1 };
		float ColorValues[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }; // RGBA per key.
	};

	static_assert(sizeof(GpuParticleEmitter) == 320);
	static_assert(offsetof(GpuParticleEmitter, Friction) == 144 && offsetof(GpuParticleEmitter, SizeTimes) == 208);

	// Per-emitter counters (std430 uint4).
	struct GpuParticleCounters
	{
		std::uint32_t Free = 0;	   // Entries on the emitter's free list.
		std::uint32_t Alive = 0;   // Entries on its draw list after the compact pass.
		std::uint32_t Dropped = 0; // Spawns lost to a full pool, cumulative.
		std::uint32_t Reserved = 0;
	};

	static_assert(sizeof(GpuParticleCounters) == 16);

	// The per-frame record.
	struct GpuParticleFrame
	{
		float ViewProjection[16] = {}; // World -> clip (row-major), jittered like the color target.
		float CameraPosition[3] = {};
		float DeltaTime = 0.0f;
		float CameraRight[3] = { 1, 0, 0 };
		std::uint32_t EmitterCount = 0;
		float CameraUp[3] = { 0, 1, 0 };
		std::uint32_t MaxSpawn = 0; // Largest SpawnCount of the frame.
		float CameraForward[3] = { 0, 0, -1 };
		float Reserved = 0.0f;
	};

	static_assert(sizeof(GpuParticleFrame) == 128);
} // namespace Swim::Render
