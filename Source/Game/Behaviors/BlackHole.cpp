#include "Game/Behaviors/BlackHole.h"

#include "Engine/Components/Transform.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Math/Ray.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Scene/Picking/ScenePicking.h"
#include "Engine/Systems/Scene/Scene.h"

namespace Game
{

	BlackHole::BlackHole(Engine::Scene* sceneValue, entt::entity owner, std::shared_ptr<Engine::GravitationalLensing> lensingValue,
		float schwarzschildRadiusValue)
		: Behavior(sceneValue, owner), lensing(std::move(lensingValue)), schwarzschildRadius(schwarzschildRadiusValue)
	{
	}

	std::uint64_t BlackHole::Key() const
	{
		return static_cast<std::uint64_t>(entt::to_integral(entity)) + 1u;
	}

	void BlackHole::Update(double)
	{
		auto feature = lensing.lock();
		const auto* transform = GetTransform();

		if (!feature || !transform)
		{
			return;
		}

		const glm::vec3 position = transform->GetWorldPosition(scene->GetRegistry());
		auto& lens = feature->Upsert(Key());
		lens.Position = { position.x, position.y, position.z };
		lens.SchwarzschildRadius = schwarzschildRadius;
		// The gas disk's axis is the entity's local +Y.
		const glm::vec3 axis = transform->GetWorldRotation(scene->GetRegistry()) * glm::vec3(0.0f, 1.0f, 0.0f);
		lens.DiskNormal = { axis.x, axis.y, axis.z };

		if (pendingGasDensity)
		{
			lens.GasDensity = *pendingGasDensity;
			pendingGasDensity.reset();
		}
	}

	int BlackHole::Exit()
	{
		if (auto feature = lensing.lock())
		{
			feature->Remove(Key());
		}

		return 0;
	}

	MouseDrag::MouseDrag(Engine::Scene* sceneValue, entt::entity owner) : Behavior(sceneValue, owner)
	{
	}

	bool MouseDrag::Owns(entt::entity hit) const
	{
		for (entt::entity e = hit; e != entt::null; e = scene->GetParent(e))
		{
			if (e == entity)
			{
				return true;
			}
		}

		return false;
	}

	void MouseDrag::Press(float x, float y)
	{
		const auto* cameras = GetCameraSystem();
		auto* transform = GetTransform();

		if (!cameras || !transform)
		{
			return;
		}

		const auto hit = Engine::ScenePicking::PickAtScreen(scene->GetRegistry(), *cameras, x, y);

		if (!hit || !Owns(hit->Entity))
		{
			return;
		}

		// The plane faces the camera through the entity's centre; the point of it under the
		// cursor keeps its offset from the centre, so the entity stays at its depth and the
		// grabbed spot stays under the cursor.
		const glm::vec3 centre = transform->GetWorldPosition(scene->GetRegistry());
		planePoint = centre;
		planeNormal = -cameras->GetCamera().GetForward();
		const auto onPlane = Engine::RayQueries::Plane(cameras->ScreenPointToRay(x, y), planePoint, planeNormal);
		grabOffset = onPlane ? centre - onPlane->Point : glm::vec3(0.0f);
		dragging = true;
	}

	void MouseDrag::Move(float x, float y)
	{
		const auto* cameras = GetCameraSystem();
		auto* transform = GetTransform();

		if (!dragging || !cameras || !transform)
		{
			return;
		}

		const auto ray = cameras->ScreenPointToRay(x, y);

		if (const auto hit = Engine::RayQueries::Plane(ray, planePoint, planeNormal))
		{
			transform->SetWorldPosition(scene->GetRegistry(), hit->Point + grabOffset);
		}
	}

	void MouseDrag::Release()
	{
		dragging = false;
	}

	void MouseDrag::Update(double)
	{
		if (!input)
		{
			return;
		}

		using Swim::Platform::MouseButton;
		const auto mouse = input->GetMousePosition();

		if (!dragging)
		{
			if (input->IsMouseButtonTriggered(MouseButton::Left) && !(inputGate && inputGate()))
			{
				Press(mouse.X, mouse.Y);
			}

			return;
		}

		if (!input->IsMouseButtonDown(MouseButton::Left))
		{
			Release();
			return;
		}

		Move(mouse.X, mouse.Y);
	}

} // namespace Game
