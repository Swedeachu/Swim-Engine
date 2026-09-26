#pragma once

#include "Engine/Systems/Renderer/Particles/ParticleSettings.h"

#include <cstdint>

namespace Engine
{
	// A GPU particle emitter on an entity (Phase 23). The render bridge creates it in the
	// ParticleSystem, keeps its transform on the entity's world Transform every frame and
	// recreates it when Revision changes (edit Desc, then ++Revision). Particles advance
	// with simulation time: they freeze while the engine is paused.
	struct ParticleEmitter
	{
		Swim::Render::ParticleEmitterDesc Desc;
		bool Emitting = true;
		std::uint64_t Revision = 0;
	};
} // namespace Engine
