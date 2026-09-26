#include "Game/Behaviors/BallShooter.h"

#include "Engine/Components/MeshRenderer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Systems/Physics/RigidBody.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Scene/SceneCommandBuffer.h"
#include "Game/Behaviors/Motion.h"
#include "Game/SandboxContent.h"
#include "Game/Scenes/Sandbox.h"

#include <array>

namespace Game
{
	BallShooter::BallShooter(Engine::Scene* sceneValue, entt::entity owner) : Behavior(sceneValue, owner)
	{
	}

	void BallShooter::Update(double dt)
	{
		cooldown -= static_cast<float>(dt);
		if (!input)
		{
			return;
		}
		using Swim::Platform::KeyCode;
		using Swim::Platform::MouseButton;
		const bool gated = inputGate && inputGate();
		const bool click = input->IsMouseButtonTriggered(MouseButton::Left) && !input->IsMouseButtonDown(MouseButton::Right);
		const bool key = input->IsKeyDown(KeyCode::F) && cooldown <= 0.0f;
		if (!gated && (click || key))
		{
			Fire();
			cooldown = 0.12f;
		}
	}

	void BallShooter::Fire(float speed)
	{
		auto* render = scene->GetRenderServices();
		auto* cameras = scene->GetCameraSystem();
		if (!cameras)
		{
			return;
		}
		const auto& camera = cameras->GetCamera();
		const glm::vec3 forward = camera.GetForward();
		const glm::vec3 position = camera.GetPosition() + forward * 1.2f;
		// Headless (no renderer): the ball still simulates, without a GPU mesh.
		std::uint32_t material = 0;
		Engine::MeshLibrary::MeshHandle mesh;
		if (render && render->HasRenderer())
		{
			static const std::array<glm::vec3, 6> colors{ SrgbColor(240, 90, 60), SrgbColor(250, 200, 60), SrgbColor(80, 200, 120),
				SrgbColor(70, 140, 250), SrgbColor(190, 90, 230), SrgbColor(240, 240, 240) };
			const auto& color = colors[fired % colors.size()];
			material =
				Material(*render->Materials, "Ball " + std::to_string(fired % colors.size()), color, fired % 3 == 0 ? 1.0f : 0.0f, 0.25f);
			mesh = render->Meshes->Get(Engine::BuiltinMesh::Sphere);
		}
		++fired;
		scene->GetCommandBuffer().Create(
			[position, forward, speed, material, mesh](Engine::Scene& owner, entt::entity ball)
			{
				owner.SetEntityName(ball, "Ball");
				owner.AddComponent<Engine::Transform>(ball, Engine::Transform(position, glm::vec3(0.4f)));
				Engine::MeshRenderer renderer;
				renderer.Parts.push_back({ mesh, material });
				owner.AddComponent<Engine::MeshRenderer>(ball, std::move(renderer));
				AddSphereBody(owner, ball, Engine::RigidbodyType::Dynamic, 0.2f, 2.0f);
				owner.GetRegistry().get<Engine::Rigidbody>(ball).SetInitialLinearVelocity(forward * speed);
				owner.AddTag(ball, Engine::Tags::Projectile);
				owner.AddTag(ball, GameTags::Spawned);
				owner.EmplaceBehavior<Lifetime>(ball, 12.0f);
				owner.EmplaceBehavior<Projectile>(ball);
			});
	}

	int Projectile::Awake()
	{
		EnableCollisionCallBacks(true);
		return 0;
	}

	void Projectile::OnCollisionEnter(const Engine::BehaviorCollision& collision)
	{
		if (auto* sandbox = dynamic_cast<Sandbox*>(scene))
		{
			sandbox->RecordImpact(collision.Impulse);
		}
	}
} // namespace Game
