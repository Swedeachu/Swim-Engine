#pragma once

#include "Engine/Systems/Scene/Scene.h"

#include <cstdint>

namespace Game
{

	// The fixed profiling scene (scripts/profiling): a 256 x 256 column block terrain in 16 x 16
	// chunk meshes, lit by one directional light and a flat ambient (no shadows, sky, image-based
	// lighting, screen-space effects or bloom), seen from a camera on a fixed orbit driven by the
	// frame count, so every run renders the same frames.
	class ProfileTerrain : public Engine::Scene
	{

	  public:
		using Engine::Scene::Scene;

		static constexpr int Size = 256;
		static constexpr int Chunk = 16;

		int Init() override;

		void Update(double dt) override;

		// Column height of the terrain at a cell (deterministic value noise).
		static int Height(int x, int z);

	  private:
		std::uint64_t frame = 0;
	};

} // namespace Game
