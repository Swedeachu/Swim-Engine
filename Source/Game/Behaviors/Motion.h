#pragma once

#include "Engine/Systems/Entity/Behavior.h"

#include <glm/glm.hpp>

namespace Game
{
	// Rotates its entity about an axis (degrees per second of simulation time).
	class Spin : public Engine::Behavior
	{
	  public:
		Spin(Engine::Scene* scene, entt::entity owner, glm::vec3 axis = { 0, 1, 0 }, float degreesPerSecond = 90.0f);
		void Update(double dt) override;

	  private:
		glm::vec3 axis;
		float speed;
	};

	// Floats its entity up and down around the position it had at Init.
	class Bob : public Engine::Behavior
	{
	  public:
		Bob(Engine::Scene* scene, entt::entity owner, float amplitude = 0.25f, float frequency = 0.5f, float phase = 0.0f);
		int Init() override;
		void Update(double dt) override;

	  private:
		glm::vec3 origin{ 0.0f };
		float amplitude;
		float frequency;
		float time;
	};

	// Moves its entity on a horizontal circle (lights around the instance hall).
	class Orbit : public Engine::Behavior
	{
	  public:
		Orbit(Engine::Scene* scene, entt::entity owner, glm::vec3 center, float radius, float height, float speed, float phase);
		void Update(double dt) override;

	  private:
		glm::vec3 center;
		float radius;
		float height;
		float speed;
		float angle;
	};

	// Destroys its entity after a number of simulated seconds (projectiles, bursts).
	class Lifetime : public Engine::Behavior
	{
	  public:
		Lifetime(Engine::Scene* scene, entt::entity owner, float seconds);
		void Update(double dt) override;

		float GetRemaining() const { return remaining; }

	  private:
		float remaining;
		bool queued = false;
	};
} // namespace Game
