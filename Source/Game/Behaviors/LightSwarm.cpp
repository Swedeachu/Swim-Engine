#include "Game/Behaviors/LightSwarm.h"

#include "Engine/Components/Transform.h"
#include "Engine/Systems/Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace Game
{
	LightSwarm::LightSwarm(Engine::Scene* sceneValue, entt::entity owner, Settings value)
		: Behavior(sceneValue, owner), settings(value), random(value.Seed)
	{
		settings.BoxMax = glm::max(settings.BoxMin, settings.BoxMax);
		settings.MaxSpeed = std::max(settings.MinSpeed, settings.MaxSpeed);
	}

	glm::vec3 LightSwarm::RandomPoint()
	{
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const glm::vec3 t(unit(random), unit(random), unit(random));
		return settings.BoxMin + t * (settings.BoxMax - settings.BoxMin);
	}

	void LightSwarm::Add(entt::entity member)
	{
		std::uniform_real_distribution<float> speed(settings.MinSpeed, settings.MaxSpeed);
		std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
		Member entry;
		entry.Entity = member;
		entry.Target = RandomPoint();
		entry.Speed = speed(random);
		entry.Velocity = glm::vec3(unit(random), unit(random), unit(random)) * entry.Speed * 0.5f;
		if (auto* transform = scene->GetRegistry().try_get<Engine::Transform>(member))
		{
			transform->SetPosition(glm::clamp(transform->GetPosition(), settings.BoxMin, settings.BoxMax));
		}
		members.push_back(entry);
	}

	glm::vec3 LightSwarm::Step(
		const Settings& settings, glm::vec3 position, glm::vec3& velocity, const glm::vec3& target, float speed, float dt)
	{
		const glm::vec3 toTarget = target - position;
		const float distance = glm::length(toTarget);
		// Slow down on arrival so members glide instead of overshooting.
		const float arrive = std::clamp(distance / std::max(settings.ArriveRadius * 2.0f, 1e-3f), 0.35f, 1.0f);
		const glm::vec3 desired = distance > 1e-5f ? toTarget / distance * speed * arrive : glm::vec3(0.0f);
		const float blend = 1.0f - std::exp(-settings.Steering * dt);
		velocity += (desired - velocity) * blend;
		position += velocity * dt;
		// Stay inside the box (bounce off its faces).
		for (int axis = 0; axis < 3; ++axis)
		{
			if (position[axis] < settings.BoxMin[axis])
			{
				position[axis] = settings.BoxMin[axis];
				velocity[axis] = std::abs(velocity[axis]);
			}
			else if (position[axis] > settings.BoxMax[axis])
			{
				position[axis] = settings.BoxMax[axis];
				velocity[axis] = -std::abs(velocity[axis]);
			}
		}
		return position;
	}

	void LightSwarm::Update(double dt)
	{
		if (dt <= 0.0)
		{
			return;
		}
		const float step = static_cast<float>(std::min(dt, 0.1));
		auto& registry = scene->GetRegistry();
		for (auto& member : members)
		{
			auto* transform = registry.valid(member.Entity) ? registry.try_get<Engine::Transform>(member.Entity) : nullptr;
			if (!transform)
			{
				continue;
			}
			const glm::vec3 position = Step(settings, transform->GetPosition(), member.Velocity, member.Target, member.Speed, step);
			transform->SetPosition(position);
			if (glm::length(member.Target - position) < settings.ArriveRadius)
			{
				member.Target = RandomPoint();
			}
		}
	}
} // namespace Game
