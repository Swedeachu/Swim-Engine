#pragma once

#include "Engine/Systems/Entity/Behavior.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Game
{
	// Poses a GPU-skinned column (ProceduralMeshes::MakeSkinnedColumn) as a swaying
	// tentacle: each joint bends a little more than the one below it, in a travelling
	// sine wave. Writes SkinnedMeshRenderer::Palette (joint model x inverse bind) and bumps
	// its PoseRevision; the SkinningSystem deforms the mesh on the GPU.
	class TentacleAnimator : public Engine::Behavior
	{
	  public:
		TentacleAnimator(Engine::Scene* scene, entt::entity owner, std::uint32_t joints, float height, float phase);
		void Update(double dt) override;

		// The palette of a pose at time t (tests use it directly).
		static std::vector<std::array<float, 12>> ComputePalette(std::uint32_t joints, float height, float t, float phase);

	  private:
		std::uint32_t joints;
		float height;
		float phase;
		float time = 0.0f;
	};
} // namespace Game
