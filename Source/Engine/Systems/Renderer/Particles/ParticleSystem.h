#pragma once
#include "Engine/Systems/Renderer/Particles/ParticleBindings.h"
#include "Engine/Systems/Renderer/Particles/ParticleGraphResources.h"
#include "Engine/Systems/Renderer/Particles/ParticleReference.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Swim::Render
{
	struct ParticleComputeProgram
	{
		Rhi::ComputePipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
	};

	struct ParticleSystemDesc
	{
		std::uint32_t Capacity = 65536;	 // Pool slots shared by every emitter.
		std::uint32_t MaxEmitters = 64;	 // Live and retiring emitters at once.
		ParticleComputeProgram Simulate; // SwimParticleSimulate
		ParticleComputeProgram Emit;	 // SwimParticleEmit
		ParticleComputeProgram Compact;	 // SwimParticleCompact
		ParticleComputeProgram Finalize; // SwimParticleFinalize
		std::string DebugName = "Particles";
	};

	// SwimParticleRender built twice from ParticleSystem::PipelineDesc: one pipeline per
	// blend mode, sharing a layout whose space 1 is the bindless table's space.
	struct ParticleRenderProgram
	{
		Rhi::GraphicsPipeline* Additive = nullptr;
		Rhi::GraphicsPipeline* AlphaBlend = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
	};

	struct ParticleDrawTargets
	{
		GraphTexture Color; // RGBA16Float ColorAttachment: the HDR scene color (loaded, blended into).
		GraphTexture Depth; // D32Float DepthStencilAttachment, reverse-Z: tested, never written.
	};

	struct ParticleSystemStats
	{
		std::uint32_t LiveEmitters = 0;
		std::uint32_t RetiringEmitters = 0;
		std::uint32_t AllocatedSlots = 0; // Pool slots owned by live and retiring emitters.
		std::uint32_t LargestFreeRange = 0;
	};

	// GPU particles (critical-path item 77). Every emitter owns a contiguous range of a
	// persistent pool, with a free list and a draw list over the same range. Per frame,
	// Simulate records four compute passes over every live emitter:
	//  1. simulate: ages and integrates each occupied slot; dead ones go back on the
	//     free list;
	//  2. emit: pops this frame's spawns off the free list and initializes them
	//     (ParticleReference.h); spawns beyond the free slots are counted as dropped;
	//  3. compact: appends every live slot to the draw list;
	//  4. finalize: sorts alpha-blended emitters' draw lists back to front in one
	//     workgroup, and writes each emitter's DrawIndexedIndirectCommand (six quad
	//     indices, one instance per live particle).
	// Draw then records one graphics pass: additive emitters, then alpha-blended ones
	// back to front by emitter origin, each a single indirect draw of camera-facing
	// billboards. No particle ever exists on the CPU.
	//
	// Owner thread, externally synchronized. After Execute, call CommitFrame (or
	// AbortFrame to rewind emission clocks and redo range resets).
	class ParticleSystem
	{
	  public:
		static constexpr Rhi::Format ColorFormat = Rhi::Format::RGBA16Float;
		static constexpr std::uint32_t MaxCapacity = 1u << 24;

		// Both faces, depth tested with the canonical reverse-Z compare and never
		// written; premultiplied color with One / One (additive, destination alpha kept)
		// or One / OneMinusSourceAlpha (alpha blend).
		static Rhi::GraphicsPipelineDesc PipelineDesc(ParticleBlendMode blend, Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout);

		// Throws std::invalid_argument when a program is missing or the capacity is not
		// 1 .. MaxCapacity, and std::runtime_error when a buffer cannot be created.
		ParticleSystem(Rhi::Device& device, ParticleSystemDesc desc);
		~ParticleSystem();
		ParticleSystem(const ParticleSystem&) = delete;
		ParticleSystem& operator=(const ParticleSystem&) = delete;

		// Empty when every row is taken or no free range fits the capacity. Throws
		// std::invalid_argument for an invalid desc or transform.
		std::optional<ParticleEmitterHandle> TryCreateEmitter(
			const ParticleEmitterDesc& desc, const std::array<float, 12>& transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 });
		// As above, but a full system throws std::length_error.
		ParticleEmitterHandle CreateEmitter(
			const ParticleEmitterDesc& desc, const std::array<float, 12>& transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 });
		bool IsValid(ParticleEmitterHandle emitter) const;
		// False for invalid handles; throws std::invalid_argument for a non-finite transform.
		bool SetTransform(ParticleEmitterHandle emitter, const std::array<float, 12>& transform);
		// Stops emission (the clock keeps running, so bursts do not repeat) or resumes it.
		bool SetEmitting(ParticleEmitterHandle emitter, bool emitting);
		// Invalidates the handle now; its pool range is reused after lastUse completes.
		bool Release(ParticleEmitterHandle emitter, Rhi::TimelinePoint lastUse = {});
		std::size_t Collect();
		std::size_t Drain();

		// Advances every live emitter by deltaTime and records the four compute passes.
		// Nothing is recorded without live emitters. Throws std::logic_error while a
		// frame awaits CommitFrame/AbortFrame, and std::invalid_argument for an invalid
		// view or delta time.
		ParticleGraphResources Simulate(RenderGraph& graph, const ParticleView& view, float deltaTime);
		void CommitFrame();
		void AbortFrame();

		// Records the draw pass for a Simulate of the same graph (nothing without
		// emitters). The bindless table must use the layout's space 1. Throws
		// std::invalid_argument for a missing pipeline or targets that are not
		// same-sized RGBA16Float color and D32Float depth.
		std::optional<GraphPass> Draw(RenderGraph& graph, const ParticleGraphResources& frame, const ParticleRenderProgram& program,
			const ParticleDrawTargets& targets, Rhi::DescriptorTable& bindless) const;

		ParticleSystemStats GetStats() const;

		std::uint32_t GetCapacity() const { return capacity; }

		// The emitter's pool range and counter/argument row; empty for invalid handles.
		std::optional<std::pair<std::uint32_t, std::uint32_t>> GetRange(ParticleEmitterHandle emitter) const;

		// The persistent buffers, for diagnostics and readback.
		Rhi::Buffer& GetParticleBuffer() const { return *particles; }

		Rhi::Buffer& GetFreeListBuffer() const { return *freeList; }

		Rhi::Buffer& GetDrawListBuffer() const { return *drawList; }

		Rhi::Buffer& GetCounterBuffer() const { return *counters; }

		Rhi::Buffer& GetDrawArgumentBuffer() const { return *drawArgs; }

	  private:
		struct Range
		{
			std::uint32_t First = 0;
			std::uint32_t Count = 0;
		};

		struct Emitter
		{
			ParticleEmitterDesc Desc;
			std::array<float, 12> Transform{};
			ParticleEmitterClock Clock;
			ParticleEmitterClock SavedClock; // After the last committed frame (AbortFrame rewinds to it).
			Range Slots;
			bool Emitting = true;
			bool NeedsReset = true;
		};

		using Registry = GpuResourceRegistry<ParticleEmitterTag, Emitter>;

		ParticleGraphResources RecordSimulation(RenderGraph& graph, const ParticleView& view, float deltaTime);
		std::optional<Range> Allocate(std::uint32_t count);
		void Free(Range range);

		ParticleSystemDesc desc;
		std::uint32_t capacity = 0;
		std::unique_ptr<Rhi::Buffer> particles, freeList, drawList, counters, drawArgs, quadIndices;
		std::unique_ptr<Registry> registry;
		std::vector<Range> freeRanges; // Sorted by First, coalesced.
		std::vector<ParticleEmitterHandle> resetInFlight;
		bool quadIndicesReady = false;
		bool quadIndicesInFlight = false;
		bool pending = false;
	};
} // namespace Swim::Render
