#include "Game/Behaviors/Motion.h"

#include "Engine/Components/Transform.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Scene/SceneCommandBuffer.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace Game
{
	Spin::Spin(Engine::Scene* sceneValue, entt::entity owner, glm::vec3 axisValue, float degreesPerSecond)
		: Behavior(sceneValue, owner), axis(glm::length(axisValue) > 0.0f ? glm::normalize(axisValue) : glm::vec3(0, 1, 0)),
		  speed(degreesPerSecond)
	{
	}

	void Spin::Update(double dt)
	{
		if (auto* transform = GetTransform())
		{
			const glm::quat step = glm::angleAxis(glm::radians(speed * static_cast<float>(dt)), axis);
			transform->SetRotation(glm::normalize(step * transform->GetRotation()));
		}
	}

	Bob::Bob(Engine::Scene* sceneValue, entt::entity owner, float amplitudeValue, float frequencyValue, float phase)
		: Behavior(sceneValue, owner), amplitude(amplitudeValue), frequency(frequencyValue), time(phase)
	{
	}

	int Bob::Init()
	{
		if (const auto* transform = GetTransform())
		{
			origin = transform->GetPosition();
		}
		return 0;
	}

	void Bob::Update(double dt)
	{
		time += static_cast<float>(dt);
		if (auto* transform = GetTransform())
		{
			transform->SetPosition(origin + glm::vec3(0.0f, amplitude * std::sin(time * frequency * 6.2831853f), 0.0f));
		}
	}

	Orbit::Orbit(Engine::Scene* sceneValue, entt::entity owner, glm::vec3 centerValue, float radiusValue, float heightValue,
		float speedValue, float phase)
		: Behavior(sceneValue, owner), center(centerValue), radius(radiusValue), height(heightValue), speed(speedValue), angle(phase)
	{
	}

	void Orbit::Update(double dt)
	{
		angle += speed * static_cast<float>(dt);
		if (auto* transform = GetTransform())
		{
			transform->SetPosition(
				center + glm::vec3(std::cos(angle) * radius, height + 0.4f * std::sin(angle * 2.0f), std::sin(angle) * radius));
		}
	}

	Lifetime::Lifetime(Engine::Scene* sceneValue, entt::entity owner, float seconds) : Behavior(sceneValue, owner), remaining(seconds)
	{
	}

	void Lifetime::Update(double dt)
	{
		remaining -= static_cast<float>(dt);
		if (remaining <= 0.0f && !queued)
		{
			queued = true;
			scene->GetCommandBuffer().Destroy(entity);
		}
	}
} // namespace Game
