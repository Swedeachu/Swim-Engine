#pragma once

#include "Engine/Systems/Entity/Behavior.h"
#include "Library/EnTT/entt.hpp"
#include <memory>
#include <vector>

namespace Engine
{
	class Scene;
	class MaterialData;
	class Mesh;
}

namespace Game
{

	// Spawns a manager entity and attaches the orbiting parenting test behavior.
	void TestParenting(Engine::Scene* scene, const glm::vec3& pos);

	// Behavior that owns a simple solar system:
	// - One star
	// - Several planets that orbit, shrink, get destroyed, and respawn
	class OrbitSystem : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int) override {}
		int Exit() override;

		// Clears and re-initializes the system (destroys star & planets, rebuilds)
		void Reset();

	private:

		int    initialPlanetCount = 6;
		float  minOrbitRadius = 3.5f;
		float  maxOrbitRadius = 12.0f;
		float  minOrbitSpeedDeg = 10.0f;
		float  maxOrbitSpeedDeg = 60.0f;
		float  minScale = 0.15f;
		float  maxScale = 0.6f;
		float  shrinkSpeed = 0.12f;     // scale units per second
		float  respawnDelay = 0.25f;    // seconds after destroy

		std::shared_ptr<Engine::Mesh>          sharedSphereMesh{};
		std::shared_ptr<Engine::MaterialData>  starMat{};

		entt::entity starEntity{ entt::null };

		struct Planet
		{
			entt::entity entity{ entt::null };
			float angleDeg = 0.0f;
			float orbitSpeedDeg = 0.0f;
			float radius = 0.0f;
			float baseScale = 1.0f;
			bool  dying = false;
			float respawnTimer = 0.0f;
		};

		std::vector<Planet> planets;

		void EnsureSharedSphere();
		entt::entity SpawnStar();
		Planet       SpawnPlanet();
		void         DestroyPlanet(Planet& p);
		void         RespawnPlanet(Planet& p);
		void         UpdatePlanet(Planet& p, double dt);
	};

} // namespace Game
