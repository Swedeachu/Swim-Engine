#pragma once
#include "Engine/Systems/Renderer/Particles/ParticleRecords.h"
#include "Engine/Systems/Renderer/Particles/ParticleSettings.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Swim::Render
{
	// The camera particles are simulated for and drawn with.
	struct ParticleView
	{
		std::array<float, 16> View{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // World -> view (rigid, -Z forward).
		std::array<float, 16> Projection{};											  // View -> clip, row-major, unjittered.
		std::array<float, 2> Jitter{ 0, 0 }; // NDC jitter of the color target (ForwardPlusView::Jitter).
	};

	// Packs one emitter row. transform is the emitter -> world 3x4 rows. Throws
	// std::invalid_argument for an invalid desc or a non-finite transform.
	GpuParticleEmitter PackParticleEmitter(const ParticleEmitterDesc& desc, const std::array<float, 12>& transform, std::uint32_t firstSlot,
		std::uint32_t spawnCount, std::uint32_t firstId);

	// Throws std::invalid_argument for a non-finite or non-invertible view, a
	// non-finite projection or jitter, or a negative/non-finite delta time.
	GpuParticleFrame BuildParticleFrame(const ParticleView& view, float deltaTime, std::uint32_t emitterCount, std::uint32_t maxSpawn);

	// The CPU-side emission schedule of one emitter.
	struct ParticleEmitterClock
	{
		float Time = 0.0f;		  // Seconds since the emitter started.
		float Accumulator = 0.0f; // Fractional continuous spawns carried to the next frame.
		std::uint32_t NextId = 0; // Id of the next spawn.
	};
} // namespace Swim::Render

namespace Swim::Render::Particles
{
	// The CPU definition of the GPU particle system (critical-path item 77).
	// Shaders/Slang/Particles mirrors every function, and the native smoke compares the
	// two particle by particle (matched by id: slot assignment is concurrent on the GPU).
	using Float2 = std::array<float, 2>;
	using Float3 = std::array<float, 3>;
	using Float4 = std::array<float, 4>;

	// PCG hash (Jarzynski and Olano 2020).
	std::uint32_t Hash(std::uint32_t value);
	// Uniform in [0, 1): stream `stream` of particle `id` under `seed`.
	float Random(std::uint32_t id, std::uint32_t seed, std::uint32_t stream);

	// Spawns this frame and the clock after it, for a frame of deltaTime seconds:
	// continuous Rate over the active part of [Time, Time + dt) (all of it when
	// Duration is 0 or Looping, else up to Duration), plus every burst whose time
	// t_b + k * Duration (k >= 0; k = 0 only without looping) lies in the interval.
	// Capped at Capacity; NextId advances by the returned count.
	std::uint32_t AdvanceEmission(const ParticleEmitterDesc& desc, ParticleEmitterClock& clock, float deltaTime);

	// A new particle (ParticleEmit.slang):
	//  - lifetime, speed, size, rotation and angular velocity uniform in their ranges
	//    (streams 0, 1, 7, 8, 9);
	//  - direction uniform in the cone around Direction (cos theta uniform in
	//    [cos ConeAngle, 1], streams 2-3);
	//  - position uniform in the shape (streams 4-6);
	//  - world-space emitters transform position and velocity by the emitter transform.
	GpuParticle SpawnParticle(const GpuParticleEmitter& emitter, std::uint32_t id);

	// One simulation step (ParticleSimulate.slang). Returns false when the particle dies
	// (age + dt >= lifetime); it is then left unchanged. Otherwise: v += g dt; v *=
	// max(0, 1 - drag dt); p += v dt; rotation += w dt; age += dt; then the ground
	// plane when ParticleFlagCollision is set.
	bool SimulateParticle(const GpuParticleEmitter& emitter, float deltaTime, GpuParticle& particle);

	// Piecewise-linear curve and gradient over normalized age t.
	float EvaluateCurve(const float (&times)[4], const float (&values)[4], std::uint32_t keyCount, float t);
	Float4 EvaluateGradient(const GpuParticleEmitter& emitter, float t);
	float ParticleSize(const GpuParticleEmitter& emitter, const GpuParticle& particle);
	// The flipbook frame shown at the particle's age.
	std::uint32_t FlipbookFrame(const GpuParticleEmitter& emitter, const GpuParticle& particle);

	// World-space centre (local-space particles go through the emitter transform).
	Float3 WorldPosition(const GpuParticleEmitter& emitter, const GpuParticle& particle);
	// Sort key: distance along the camera forward axis.
	float ViewDepth(const GpuParticleFrame& frame, const GpuParticleEmitter& emitter, const GpuParticle& particle);
	// Back to front: larger depth first, then lower id.
	bool DrawsBefore(float depthA, std::uint32_t idA, float depthB, std::uint32_t idB);

	// Billboard corner c (0: bottom-left, 1: bottom-right, 2: top-left, 3: top-right)
	// in world space, facing the camera and rotated by the particle's rotation, and its
	// sprite UV (top-left origin) inside the current flipbook frame.
	struct BillboardVertex
	{
		Float3 Position{};
		Float2 Uv{};
	};

	BillboardVertex BillboardCorner(
		const GpuParticleFrame& frame, const GpuParticleEmitter& emitter, const GpuParticle& particle, std::uint32_t corner);

	// The fragment before blending, premultiplied (rgb * a, a): the color over life
	// times the sprite texel (or the procedural disc 1 - |2 uv - 1|^2, clamped, for
	// untextured emitters; `texel` is ignored then). `local` is the UV within the quad.
	Float4 ShadeFragment(const GpuParticleEmitter& emitter, const GpuParticle& particle, const Float2& local, const Float4& texel);

	// A CPU mirror of one emitter's pool: the particles alive after each frame, in
	// id order, with the same simulate -> spawn order as the GPU. Spawns beyond the
	// capacity are dropped, as on the GPU (which ones is then unspecified there).
	class ReferenceEmitter
	{
	  public:
		explicit ReferenceEmitter(std::uint32_t capacity);
		// Simulates the live particles, then spawns emitter.SpawnCount from FirstId.
		void Step(const GpuParticleEmitter& emitter, float deltaTime);

		const std::vector<GpuParticle>& Particles() const { return particles; }

		// Replaces the live particles (for example with a GPU readback), so the next Step
		// is compared from the same state.
		void Assign(std::vector<GpuParticle> live) { particles = std::move(live); }

		std::uint32_t Dropped() const { return dropped; }

	  private:
		std::uint32_t capacity;
		std::vector<GpuParticle> particles;
		std::uint32_t dropped = 0;
	};
} // namespace Swim::Render::Particles
