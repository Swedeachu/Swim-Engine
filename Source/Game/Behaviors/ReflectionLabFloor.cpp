#include "Game/Behaviors/ReflectionLabFloor.h"

#include "Engine/Components/MeshRenderer.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Scene/Scene.h"

#include <cmath>

namespace Game
{

	ReflectionLabFloor::ReflectionLabFloor(Engine::Scene* sceneValue, entt::entity owner, std::uint32_t materialSetValue,
		Engine::MaterialDesc materialValue)
		: Behavior(sceneValue, owner), materialSet(materialSetValue), material(std::move(materialValue))
	{
	}

	glm::vec3 ReflectionLabFloor::TintFor(Mode mode, float time)
	{
		switch (mode)
		{
		case Mode::Green: return { 0.08f, 0.75f, 0.12f };
		case Mode::Rainbow:
		{
			// A fully saturated hue wheel (linear), one turn per RainbowPeriod.
			const float h = std::fmod(time / RainbowPeriod, 1.0f) * 6.0f;
			const auto channel = [h](float offset)
			{
				const float k = std::fmod(offset + h, 6.0f);
				return glm::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f);
			};
			return glm::vec3(channel(5.0f), channel(3.0f), channel(1.0f)) * 0.85f + glm::vec3(0.02f);
		}
		default: return { 1.0f, 1.0f, 1.0f };
		}
	}

	int ReflectionLabFloor::Init()
	{
		Apply();
		return 0;
	}

	void ReflectionLabFloor::SetMode(Mode value)
	{
		mode = static_cast<std::uint32_t>(value) < static_cast<std::uint32_t>(Mode::Count) ? value : Mode::Checker;
		Apply();
	}

	void ReflectionLabFloor::Update(double dt)
	{
		time += static_cast<float>(dt);

		if (mode == Mode::Rainbow)
		{
			Apply();
		}
	}

	void ReflectionLabFloor::Apply()
	{
		tint = TintFor(mode, time);
		const bool shouldShow = mode != Mode::Removed;
		auto& registry = scene->GetRegistry();

		if (shouldShow != visible && registry.valid(entity) && registry.all_of<Engine::MeshRenderer>(entity))
		{
			// patch: the render extractor sees the flag change.
			registry.patch<Engine::MeshRenderer>(entity,
				[shouldShow](Engine::MeshRenderer& renderer)
				{
					using Flags = Swim::Render::RenderObjectFlags;
					renderer.Flags = shouldShow ? (renderer.Flags | Flags::Visible)
												: static_cast<Flags>(static_cast<std::uint32_t>(renderer.Flags) & ~static_cast<std::uint32_t>(Flags::Visible));
				});
			visible = shouldShow;
		}

		auto* render = scene->GetRenderServices();

		if (render && render->Materials && materialSet != 0)
		{
			material.BaseColor = { tint.r, tint.g, tint.b, 1.0f };
			render->Materials->Update(materialSet, material);
		}
	}

} // namespace Game
