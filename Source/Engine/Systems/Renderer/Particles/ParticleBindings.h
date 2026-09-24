#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{
	// Descriptor contracts (space 0) of the particle programs. The compute programs run
	// 64-thread groups along x with one group row per emitter (y); the finalize pass
	// runs one group per emitter.
	inline constexpr std::uint32_t ParticleThreadGroupSize = 64;
	inline constexpr std::uint32_t ParticleFinalizeGroupSize = 256;

	// One binding numbering for the four compute programs; each declares only the
	// bindings it uses (ParticleProgramBindings).
	struct ParticleSimulationBindings // SwimParticleSimulate, SwimParticleEmit, SwimParticleCompact, SwimParticleFinalize
	{
		static constexpr std::uint32_t Frame = 0;	  // StructuredBuffer<ParticleFrame>.
		static constexpr std::uint32_t Emitters = 1;  // StructuredBuffer<ParticleEmitter>.
		static constexpr std::uint32_t Particles = 2; // RWStructuredBuffer<Particle>.
		static constexpr std::uint32_t FreeList = 3;  // RWStructuredBuffer<uint>: free slots per emitter range.
		static constexpr std::uint32_t DrawList = 4;  // RWStructuredBuffer<uint>: live slots per emitter range.
		static constexpr std::uint32_t Counters = 5;  // RWStructuredBuffer<ParticleCounters>.
		static constexpr std::uint32_t DrawArgs = 6;  // RWStructuredBuffer<uint>: 5 per emitter row (DrawIndexedIndirectCommand).
		static constexpr std::uint32_t Count = 7;
	};

	// The bindings each compute program declares.
	struct ParticleProgramBindings
	{
		using B = ParticleSimulationBindings;
		static constexpr std::array<std::uint32_t, 5> Simulate{ B::Frame, B::Emitters, B::Particles, B::FreeList, B::Counters };
		static constexpr std::array<std::uint32_t, 4> Emit{ B::Emitters, B::Particles, B::FreeList, B::Counters };
		static constexpr std::array<std::uint32_t, 4> Compact{ B::Emitters, B::Particles, B::DrawList, B::Counters };
		static constexpr std::array<std::uint32_t, 6> Finalize{ B::Frame, B::Emitters, B::Particles, B::DrawList, B::Counters,
			B::DrawArgs };
	};

	struct ParticleRenderBindings // SwimParticleRender: vertexMain + fragmentMain.
	{
		static constexpr std::uint32_t Frame = 0;
		static constexpr std::uint32_t Emitters = 1;
		static constexpr std::uint32_t Particles = 2; // StructuredBuffer<Particle>.
		static constexpr std::uint32_t DrawList = 3;  // StructuredBuffer<uint>.
		static constexpr std::uint32_t Count = 4;
		// Push constant: the emitter row (uint).
		static constexpr std::uint32_t PushConstantBytes = 4;
		// The bindless space shared with Forward+ (BindlessResourceTable).
		static constexpr std::uint32_t BindlessSpace = 1;
		static constexpr std::uint32_t BindlessSamplers = 0; // SamplerState[].
		static constexpr std::uint32_t BindlessTextures = 1; // Texture2D<float4>[].
	};
} // namespace Swim::Render
