#include "PCH.h"
#include "OrbitSystem.h"

#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/Core/Meshes/PrimitiveMeshes.h"

#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/MeshDecorator.h"

#include "Engine/Systems/Scene/Scene.h"                 
#include "Engine/Utility/RandomUtils.h"                 
#include "Engine/Utility/BrightColorGenerator.h"

#include "Library/glm/gtc/quaternion.hpp"
#include "Library/glm/gtc/matrix_transform.hpp"

namespace Game
{

	// Public entry: create a manager and attach the OrbitSystem behavior
	void TestParenting(Engine::Scene* scene, const glm::vec3& pos)
	{
		auto e = scene->CreateEntity();
		// Manager transform (world origin)
		scene->AddComponent<Engine::Transform>(e, Engine::Transform(pos, glm::vec3(1.0f)));

		// Attach behavior
		scene->EmplaceBehavior<Game::OrbitSystem>(e);
	}

	int OrbitSystem::Awake()
	{
		EnsureSharedSphere();
		return 0;
	}

	int OrbitSystem::Init()
	{
		// Create star (not the object which has the OrbitSystem behavior, but the object that all the stars are parented off of)
		starEntity = SpawnStar();

		// Create initial planets
		planets.clear();
		planets.reserve(static_cast<size_t>(initialPlanetCount));

		for (int i = 0; i < initialPlanetCount; ++i)
		{
			planets.push_back(SpawnPlanet());
		}

		return 0;
	}

	int OrbitSystem::Exit()
	{
		Reset();
		return 0;
	}

	void OrbitSystem::Reset()
	{
		auto& reg = scene->GetRegistry();

		// Destroy planets
		for (auto& p : planets)
		{
			if (reg.valid(p.entity))
			{
				scene->DestroyEntity(p.entity, true);
			}
		}

		planets.clear();

		// Destroy star
		if (reg.valid(starEntity))
		{
			scene->DestroyEntity(starEntity, true);
			starEntity = entt::null;
		}

		// Rebuild system
		Init();
	}

	void OrbitSystem::Update(double dt)
	{
		// If star was externally destroyed, rebuild system
		if (!scene->GetRegistry().valid(starEntity))
		{
			Init();
			return;
		}

		for (auto& p : planets)
		{
			UpdatePlanet(p, dt);
		}
	}

	void OrbitSystem::EnsureSharedSphere()
	{
		auto& meshPool = Engine::MeshPool::GetInstance();
		auto& matPool = Engine::MaterialPool::GetInstance();

		// Lazy-create shared sphere mesh
		if (!sharedSphereMesh)
		{
			auto data = Engine::MakeSphere(24, 48,
				glm::vec3(1.0f, 0.95f, 0.8f), // top
				glm::vec3(1.0f),              // mid
				glm::vec3(1.0f, 0.85f, 0.5f)  // bottom
			);
			sharedSphereMesh = meshPool.RegisterMesh("OrbitSharedSphere", data.vertices, data.indices);
		}

		// Lazy-create star material (bright)
		if (!starMat)
		{
			starMat = matPool.RegisterMaterialData("OrbitStarMat", sharedSphereMesh);
		}
	}

	entt::entity OrbitSystem::SpawnStar()
	{
		auto& matPool = Engine::MaterialPool::GetInstance();
		auto  matStar = starMat ? starMat : matPool.RegisterMaterialData("OrbitStarMatFallback", sharedSphereMesh);

		auto& reg = scene->GetRegistry();

		glm::vec3 pos = transform->GetPosition();

		entt::entity e = reg.create();
		// Big star at origin
		reg.emplace<Engine::Transform>(e, pos, glm::vec3(1.8f));
		reg.emplace<Engine::Material>(e, matStar);

		// Give star a random bright color via mesh decorator
		const glm::vec3 tint = Engine::RandomBrightColor();

		reg.emplace<Engine::MeshDecorator>(e, Engine::MeshDecorator(
			glm::vec4(tint.x, tint.y, tint.z, 1.0f) // fill
		));

		return e;
	}

	OrbitSystem::Planet OrbitSystem::SpawnPlanet()
	{
		auto& matPool = Engine::MaterialPool::GetInstance();
		auto& reg = scene->GetRegistry();

		// Create a unique material name or reuse a small pool
		const std::string matName = "OrbitPlanetMat_" + std::to_string(Engine::RandInt(0, 1'000'000));
		auto mat = matPool.RegisterMaterialData(matName, sharedSphereMesh);

		// Entity
		entt::entity e = reg.create();

		// Random orbit params
		Planet p;
		p.entity = e;
		p.radius = Engine::RandFloat(minOrbitRadius, maxOrbitRadius);
		p.orbitSpeedDeg = Engine::RandFloat(minOrbitSpeedDeg, maxOrbitSpeedDeg) * (Engine::RandInt(0, 1) ? 1.0f : -1.0f);
		p.angleDeg = Engine::RandFloat(0.0f, 360.0f);
		p.baseScale = Engine::RandFloat(minScale, maxScale);
		p.dying = false;
		p.respawnTimer = 0.0f;

		int randSign = Engine::RandInt(0, 1) ? 1.0f : -1.0f;
		float height = Engine::RandFloat(minOrbitRadius, maxOrbitRadius) * randSign;

		// Local transform (will be parented to star)
		glm::vec3 localPos{ p.radius, height / 2, 0.0f };
		glm::quat localRot = glm::quat(1, 0, 0, 0);

		reg.emplace<Engine::Transform>(e, localPos, glm::vec3(p.baseScale), localRot);
		reg.emplace<Engine::Material>(e, mat);

		// Give planets a random bright color via mesh decorator
		const glm::vec3 tint = Engine::RandomBrightColor();

		reg.emplace<Engine::MeshDecorator>(e, Engine::MeshDecorator(
			glm::vec4(tint.x, tint.y, tint.z, 1.0f) // fill
		));

		// Parent to star so orbit is simple (local XZ circle)
		scene->SetParent(e, starEntity);

		return p;
	}

	void OrbitSystem::DestroyPlanet(Planet& p)
	{
		auto& reg = scene->GetRegistry();

		if (reg.valid(p.entity))
		{
			scene->DestroyEntity(p.entity, true);
		}
		p.entity = entt::null;
		p.dying = false;
		p.respawnTimer = respawnDelay;
	}

	void OrbitSystem::RespawnPlanet(Planet& p)
	{
		// Reuse struct but new entity/params
		p = SpawnPlanet();
	}

	void OrbitSystem::UpdatePlanet(Planet& p, double dt)
	{
		auto& reg = scene->GetRegistry();

		// Respawn timer (if previously destroyed)
		if (p.entity == entt::null)
		{
			p.respawnTimer -= static_cast<float>(dt);
			if (p.respawnTimer <= 0.0f)
			{
				RespawnPlanet(p);
			}
			return;
		}

		// Validate entity still alive & parent still alive
		if (!reg.valid(p.entity) || !reg.any_of<Engine::Transform>(p.entity))
		{
			// Treat as destroyed and schedule respawn
			p.entity = entt::null;
			p.respawnTimer = respawnDelay;
			return;
		}

		// Update orbit angle
		p.angleDeg += p.orbitSpeedDeg * static_cast<float>(dt);

		// Compute local position on XZ circle
		const float rad = glm::radians(p.angleDeg);
		const float x = p.radius * std::cos(rad);
		const float z = p.radius * std::sin(rad);

		auto& tf = reg.get<Engine::Transform>(p.entity);
		tf.SetPosition(glm::vec3(x, tf.GetPosition().y, z)); // Stay at Y level

		// Shrink over time; when very small, destroy and schedule respawn
		if (!p.dying)
		{
			// Random chance to start dying (rare) to stagger lifecycle
			if (Engine::RandInt(0, 600) == 0) // ~once per few seconds spread
			{
				p.dying = true;
			}
		}

		if (p.dying)
		{
			glm::vec3 s = tf.GetScale();
			float shrink = static_cast<float>(dt) * shrinkSpeed;
			s = glm::max(s - glm::vec3(shrink), glm::vec3(0.02f));
			tf.SetScale(s);

			if (s.x <= 0.03f)
			{
				DestroyPlanet(p);
				// p.dying = false;
				// tf.SetScale(glm::vec3(1.0f));
				return;
			}
		}
	}

} // namespace Game
