#pragma once
#include "Engine/Systems/Renderer/Particles/ParticleRecords.h"
#include "Engine/Systems/Renderer/Particles/ParticleSettings.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"

#include <optional>
#include <vector>

namespace Swim::Render
{
	struct ParticleEmitterTag;
	using ParticleEmitterHandle = GpuHandle<ParticleEmitterTag>;

	// One emitter as simulated this frame.
	struct ParticleFrameEmitter
	{
		ParticleEmitterHandle Handle;
		GpuParticleEmitter Record; // Row Index in Emitters = position in ParticleGraphResources::Emitters.
		ParticleBlendMode Blend = ParticleBlendMode::Additive;
		float OriginDepth = 0.0f; // The emitter origin along the camera forward axis (draw order of blended emitters).
	};

	// What one ParticleSystem::Simulate scheduled. Buffers are the persistent pool
	// imported into this graph plus the frame's uploads; Draw needs them.
	struct ParticleGraphResources
	{
		GpuParticleFrame FrameRecord;
		std::vector<ParticleFrameEmitter> Emitters; // Live emitters in row order.
		std::optional<GraphBuffer> Frame;			// One GpuParticleFrame.
		std::optional<GraphBuffer> EmitterRecords;	// GpuParticleEmitter per entry of Emitters.
		std::optional<GraphBuffer> Particles;		// GpuParticle per pool slot.
		std::optional<GraphBuffer> FreeList;		// uint per pool slot, per emitter range.
		std::optional<GraphBuffer> DrawList;		// uint per pool slot, per emitter range.
		std::optional<GraphBuffer> Counters;		// GpuParticleCounters per emitter row.
		std::optional<GraphBuffer> DrawArgs;		// DrawIndexedIndirectCommand per emitter row.
		std::optional<GraphBuffer> QuadIndices;		// Six uint indices of a billboard.
		std::optional<GraphPass> SimulatePass;
		std::optional<GraphPass> EmitPass;
		std::optional<GraphPass> CompactPass;
		std::optional<GraphPass> FinalizePass;
		std::uint32_t InitializedEmitters = 0; // Emitters whose pool range was reset this frame.
	};
} // namespace Swim::Render
