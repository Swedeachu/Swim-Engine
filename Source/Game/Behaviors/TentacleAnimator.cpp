#include "Game/Behaviors/TentacleAnimator.h"

#include "Engine/Components/SkinnedMeshRenderer.h"
#include "Engine/Systems/Scene/Scene.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Game
{
	TentacleAnimator::TentacleAnimator(
		Engine::Scene* sceneValue, entt::entity owner, std::uint32_t jointCount, float heightValue, float phaseValue)
		: Behavior(sceneValue, owner), joints(jointCount), height(heightValue), phase(phaseValue)
	{
	}

	std::vector<std::array<float, 12>> TentacleAnimator::ComputePalette(std::uint32_t joints, float height, float t, float phase)
	{
		std::vector<std::array<float, 12>> palette(joints);
		if (joints == 0)
		{
			return palette;
		}
		const float segment = height / static_cast<float>(joints);
		glm::mat4 model(1.0f);
		for (std::uint32_t j = 0; j < joints; ++j)
		{
			if (j > 0)
			{
				model = model * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, segment, 0.0f));
			}
			const float k = static_cast<float>(j + 1) / static_cast<float>(joints);
			const float bendX = 0.35f * k * std::sin(t * 1.7f + phase + static_cast<float>(j) * 0.8f);
			const float bendZ = 0.25f * k * std::cos(t * 1.3f + phase * 1.3f + static_cast<float>(j) * 0.6f);
			model =
				model * glm::rotate(glm::mat4(1.0f), bendX, glm::vec3(1, 0, 0)) * glm::rotate(glm::mat4(1.0f), bendZ, glm::vec3(0, 0, 1));
			// Joint j's bind pose is a translation to y = j * segment.
			const glm::mat4 inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -segment * static_cast<float>(j), 0.0f));
			const glm::mat4 skin = model * inverseBind;
			for (int r = 0; r < 3; ++r)
			{
				for (int c = 0; c < 4; ++c)
				{
					palette[j][static_cast<std::size_t>(r * 4 + c)] = skin[c][r];
				}
			}
		}
		return palette;
	}

	void TentacleAnimator::Update(double dt)
	{
		time += static_cast<float>(dt);
		auto* skin = scene->GetRegistry().try_get<Engine::SkinnedMeshRenderer>(entity);
		if (!skin || (dt <= 0.0 && skin->Palette.size() == joints))
		{
			return; // Paused: keep the last pose (no re-skinning).
		}
		skin->Palette = ComputePalette(joints, height, time, phase);
		++skin->PoseRevision;
	}
} // namespace Game
