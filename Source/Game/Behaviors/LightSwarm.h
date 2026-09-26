#pragma once

#include "Engine/Systems/Entity/Behavior.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <vector>

namespace Game
{
	// Moves a crowd of entities (the coloured lights in the Sponza atrium) around inside an
	// axis-aligned box: each one steers smoothly toward a random target in the box at its
	// own speed and picks a new target when it gets close. One behaviour drives them all
	// (no per-light behaviour overhead). Simulation time: the swarm freezes while paused.
	class LightSwarm : public Engine::Behavior
	{
	  public:
		struct Settings
		{
			glm::vec3 BoxMin{ -1.0f };
			glm::vec3 BoxMax{ 1.0f };
			float MinSpeed = 0.8f; // Metres per second.
			float MaxSpeed = 2.8f;
			float Steering = 1.6f; // How quickly the velocity turns toward the target (1/s).
			float ArriveRadius = 0.8f;
			std::uint32_t Seed = 7u;
		};

		LightSwarm(Engine::Scene* scene, entt::entity owner, Settings settings);

		// Adds an entity with a Transform; its current position is kept (clamped into the box).
		void Add(entt::entity member);
		void Update(double dt) override;

		std::size_t GetCount() const { return members.size(); }

		const Settings& GetSettings() const { return settings; }

		// One integration step of a member (tests use it directly). Returns the new position.
		static glm::vec3 Step(
			const Settings& settings, glm::vec3 position, glm::vec3& velocity, const glm::vec3& target, float speed, float dt);

	  private:
		struct Member
		{
			entt::entity Entity = entt::null;
			glm::vec3 Velocity{ 0.0f };
			glm::vec3 Target{ 0.0f };
			float Speed = 1.0f;
		};

		glm::vec3 RandomPoint();

		Settings settings;
		std::vector<Member> members;
		std::mt19937 random;
	};
} // namespace Game
