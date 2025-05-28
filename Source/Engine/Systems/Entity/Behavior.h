#pragma once

#include "Engine/Machine.h"

namespace Engine
{

	// Forward declare
	class Scene;
	class SceneSystem;
	class InputManager;
	class CameraSystem;
	class Renderer;
	struct Transform;
	struct Material;

	class Behavior : public Machine
	{

	public:

		Behavior(Scene* scene, entt::entity owner);

		virtual ~Behavior() = default;

	protected:

		Scene* scene = nullptr;
		entt::entity entity = entt::null;

		std::shared_ptr<InputManager> input;
		std::shared_ptr<SceneSystem> sceneSystem;
		std::shared_ptr<CameraSystem> cameraSystem;
		std::shared_ptr<Renderer> renderer;

		// These may be nullptr if the entity does not have the components but since they are so common we attempt to cache them on construction
		Transform* transform = nullptr;
		Material* material = nullptr;

	};

}
