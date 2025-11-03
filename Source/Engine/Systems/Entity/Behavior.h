#pragma once

#include "Engine/Machine.h"
#include "Engine/EngineState.h"

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

		// These will be implemented once PhysX is integrated later on

		virtual void OnCollisionEnter(entt::entity other) {}
		virtual void OnCollisionStay(entt::entity other) {}
		virtual void OnCollisionExit(entt::entity other) {}

		// Mouse behavior callbacks to be optionally overriden:

		virtual void OnMouseEnter() {}
		virtual void OnMouseHover() {}
		virtual void OnMouseExit() {}

		virtual void OnLeftClicked() {}
		virtual void OnRightClicked() {}

		virtual void OnLeftClickDown() {}
		virtual void OnRightClickDown() {}

		virtual void OnLeftClickUp() {}
		virtual void OnRightClickUp() {}

		const bool RunMouseCallBacks() const { return runMouseCallBacks; }
		const bool RunCollisionCallBacks() const { return runCollisionCallBacks; }

		void EnableMouseCallBacks(bool value = true) { runMouseCallBacks = value; }
		void EnableCollisionCallBacks(bool value = true) { runCollisionCallBacks = value; }

		const bool FocusedByMouse() const { return focusedByMouse; }
		void SetFocusedByMouse(bool value) { focusedByMouse = value; }

		// Set exactly which engine states this behavior is enabled in.
		// Default is EngineState::Playing.
		void SetEnabledStates(EngineState states) { enabledStates = states; }

		// Add one or more states to the enable mask.
		void AddEnabledStates(EngineState states) { enabledStates |= states; }

		// Remove one or more states from the enable mask.
		void RemoveEnabledStates(EngineState states)
		{
			enabledStates = static_cast<EngineState>(
				static_cast<std::underlying_type_t<EngineState>>(enabledStates) &
				~static_cast<std::underlying_type_t<EngineState>>(states)
				);
		}

		// Query which states are enabled for this behavior (bitmask).
		EngineState GetEnabledStates() const { return enabledStates; }

		// Convenient predicate: is this behavior enabled in a specific state?
		bool IsEnabledIn(EngineState state) const
		{
			return HasAny(enabledStates, state);
		}

		// Main gate: can this behavior execute given current engine state?
		// Typical use: if (!behavior->CanExecute(currentEngineState)) return;
		bool CanExecute(EngineState currentEngineState) const
		{
			return HasAny(enabledStates, currentEngineState);
		}

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

		bool runMouseCallBacks = false;
		bool runCollisionCallBacks = false;

		bool focusedByMouse = false;

		// Which engine states this behavior is active in (bitmask)
		// Default: active only while Playing
		EngineState enabledStates = EngineState::Playing;

	};

}
