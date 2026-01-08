#include "PCH.h"
#include "BallShooter.h"

#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/ObjectTag.h"
#include "Engine/Systems/Physics/RigidBody.h"

#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Meshes/PrimitiveMeshes.h"

namespace Game
{

	void BallShooter::Update(double dt)
	{
		(void)dt;

		if (!scene || !input)
		{
			return;
		}

		if (!input->IsKeyTriggered('1'))
		{
			return;
		}

		auto camSys = scene->GetCameraSystem();
		if (!camSys)
		{
			return;
		}

		auto& cam = camSys->GetCamera();

		const glm::vec3 camPos = cam.GetPosition();
		const glm::quat camRot = cam.GetRotation();

		// Forward is -Z in the ScreenPointToRay code path.
		glm::vec3 forward = glm::normalize(camRot * glm::vec3(0.0f, 0.0f, -1.0f));

		const glm::vec3 spawnPos = camPos + forward * 1.5f;

		auto& meshPool = Engine::MeshPool::GetInstance();
		auto& materialPool = Engine::MaterialPool::GetInstance();

		// Lazy register the mesh/material used for balls.
		if (!meshPool.GetMesh("PhysicsBall"))
		{
			auto sphereData = Engine::MakeSphere(
				24, 48,
				glm::vec3(1, 0, 0),
				glm::vec3(1, 1, 0),
				glm::vec3(0, 0, 1)
			);

			meshPool.RegisterMesh("PhysicsBall", sphereData.vertices, sphereData.indices);
		}

		std::shared_ptr<Engine::MaterialData> matData;

		if (materialPool.MaterialExists("PhysicsBallMat"))
		{
			matData = materialPool.GetMaterialData("PhysicsBallMat");
		}
		else
		{
			auto mesh = meshPool.GetMesh("PhysicsBall");
			matData = materialPool.RegisterMaterialData("PhysicsBallMat", mesh);
		}

		// Create the projectile entity.
		entt::entity ball = scene->CreateEntity();
		scene->SetTag(ball, Engine::TagConstants::WORLD, "physics ball");

		scene->AddComponent<Engine::Transform>(ball,
			Engine::Transform(spawnPos, glm::vec3(shootRadius * 2.0f))
		);

		scene->AddComponent<Engine::Material>(ball, Engine::Material(matData));

		Engine::Rigidbody rb = Engine::Rigidbody();

		rb.type = Engine::RigidbodyType::Dynamic;
		rb.useGravity = true;
		rb.startAwake = true;

		rb.mass = 1.0f;
		rb.linearDamping = 0.01f;
		rb.angularDamping = 0.01f;

		rb.isTrigger = false;

		rb.collider.type = Engine::ColliderType::Sphere;
		rb.collider.sphere.radius = 0.5f; // unit sphere radius; Transform scale applies actual size

		// Actor will be built immediately by on_construct, so this will apply right away.
		rb.SetInitialLinearVelocity(forward * shootSpeed);

		// New component should always build.
		rb.dirty = true;

		scene->AddComponent<Engine::Rigidbody>(ball, rb);
	}

} // Namespace Game
