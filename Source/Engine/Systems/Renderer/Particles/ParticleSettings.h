#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace Swim::Render
{
	inline constexpr std::uint32_t MaxParticleCurveKeys = 4;
	// Alpha-blended emitters are sorted back to front in one workgroup, so their
	// capacity is bounded by its shared memory.
	inline constexpr std::uint32_t MaxSortedParticles = 2048;

	enum class ParticleSpace : std::uint8_t
	{
		World, // Particles keep their world position when the emitter moves.
		Local, // Particles live in the emitter's space and move with it.
	};

	enum class ParticleBlendMode : std::uint8_t
	{
		Additive,	// One / One: order independent, never sorted.
		AlphaBlend, // Premultiplied One / OneMinusSourceAlpha, sorted back to front.
	};

	enum class ParticleShape : std::uint8_t
	{
		Point,
		Sphere, // Uniform in a ball of radius ShapeExtent[0].
		Box,	// Uniform in the box of half extents ShapeExtent.
	};

	// A scalar over a particle's normalized age t = age / lifetime: piecewise linear
	// through up to four keys, constant before the first and after the last.
	struct ParticleCurve
	{
		std::uint32_t KeyCount = 1;									  // 1 .. MaxParticleCurveKeys.
		std::array<float, MaxParticleCurveKeys> Times{ 0, 1, 1, 1 };  // Strictly increasing in [0, 1].
		std::array<float, MaxParticleCurveKeys> Values{ 1, 1, 1, 1 }; // Finite.
	};

	// A linear RGBA color (straight alpha) over normalized age, keyed like ParticleCurve.
	struct ParticleGradient
	{
		std::uint32_t KeyCount = 1;
		std::array<float, MaxParticleCurveKeys> Times{ 0, 1, 1, 1 };
		std::array<std::array<float, 4>, MaxParticleCurveKeys> Colors{ { { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 } } };
	};

	struct ParticleBurst
	{
		float Time = 0.0f; // Seconds into the emitter's cycle, [0, Duration) when Duration > 0.
		std::uint32_t Count = 0;
	};

	// One GPU particle emitter (critical-path item 77). Spawn values are uniform in
	// [Min, Max]; every random draw is a pure function of the particle's id and Seed, so
	// the GPU and ParticleReference.h produce the same particles.
	struct ParticleEmitterDesc
	{
		std::uint32_t Capacity = 1024; // Live particles at once, >= 1 (<= MaxSortedParticles when AlphaBlend).
		ParticleSpace Space = ParticleSpace::World;
		ParticleBlendMode Blend = ParticleBlendMode::Additive;

		// Emission. Rate is continuous (per second); bursts fire once per cycle.
		float Rate = 50.0f;	   // >= 0.
		float Duration = 0.0f; // Seconds; 0 = emit forever.
		bool Looping = true;   // Repeat the cycle (Duration > 0).
		std::vector<ParticleBurst> Bursts;

		// Spawn shape and motion, in the emitter's space.
		ParticleShape Shape = ParticleShape::Point;
		std::array<float, 3> ShapeExtent{ 0, 0, 0 };  // >= 0.
		std::array<float, 3> Direction{ 0, 1, 0 };	  // Cone axis (normalized when packed).
		float ConeAngle = 0.5f;						  // Half angle in radians, [0, pi].
		float SpeedMin = 1.0f, SpeedMax = 2.0f;		  // >= 0.
		float LifetimeMin = 1.0f, LifetimeMax = 2.0f; // > 0.
		float SizeMin = 0.1f, SizeMax = 0.2f;		  // Billboard edge length, > 0.
		float RotationMin = 0.0f, RotationMax = 0.0f; // Radians.
		float AngularVelocityMin = 0.0f, AngularVelocityMax = 0.0f;

		// Simulation, in the simulation space (world for World, emitter for Local).
		std::array<float, 3> Gravity{ 0, -9.81f, 0 };
		float Drag = 0.0f; // Velocity scales by max(0, 1 - Drag * dt) per step, >= 0.
		// Optional ground plane y = GroundHeight (World space only): particles below it
		// are put on it and bounce with Restitution, losing Friction of their tangential speed.
		bool Collision = false;
		float GroundHeight = 0.0f;
		float Restitution = 0.5f; // [0, 1].
		float Friction = 0.1f;	  // [0, 1].

		// Appearance over normalized age: size multiplies the spawn size; color is linear
		// with straight alpha.
		ParticleCurve SizeOverLife;
		ParticleGradient ColorOverLife;
		// Sprite: a BindlessResourceTable texture/sampler pair (0 = a procedural soft
		// disc, no texture) laid out as a Columns x Rows flipbook of Frames frames,
		// row-major from the top left. FrameRate > 0 plays at that many frames per second
		// (wrapping); 0 spreads the frames over the particle's life.
		std::uint32_t TextureIndex = 0;
		std::uint32_t SamplerIndex = 0;
		std::uint32_t FlipbookColumns = 1;
		std::uint32_t FlipbookRows = 1;
		std::uint32_t FlipbookFrames = 1; // 1 .. Columns * Rows.
		float FlipbookFrameRate = 0.0f;	  // >= 0.

		std::uint32_t Seed = 0;
	};

	// Throws std::invalid_argument for values outside the ranges above.
	void ValidateParticleEmitterDesc(const ParticleEmitterDesc& desc);
} // namespace Swim::Render
