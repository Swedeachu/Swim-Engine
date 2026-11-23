#pragma once

#include "Engine/EngineState.h"
#include "Engine/Machine.h"
#include "Engine/Runtime/SimulationClock.h"
#include "Engine/Systems/Scene/SceneTimers.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace Swim::Input
{

	class InputSystem;

}

namespace Engine
{

	class Scene;
	class CameraSystem;
	class Transform;

	// A trigger overlap reported to a behaviour: to the trigger's behaviours (Other is what
	// entered) and to the entering entity's behaviours (Other is the trigger).
	struct BehaviorTrigger
	{
		entt::entity Trigger = entt::null;
		entt::entity Other = entt::null;
	};

	// A collision reported to a behaviour (from the scene's physics world events).
	struct BehaviorCollision
	{
		entt::entity Other = entt::null;
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		float Impulse = 0.0f;
	};

	// Gameplay code attached to an entity (BehaviorComponents). Lifecycle, in order:
	//
	//   Awake        once, when attached
	//   Init         once, before the first Update/FixedUpdate it takes part in
	//   Update(dt)   every frame the behaviour can run; dt is the Behaviors-domain delta
	//                (scaled simulation time, 0 while paused for behaviours that run
	//                while paused, e.g. cameras)
	//   FixedUpdate  every fixed simulation step
	//   OnPlay/OnPause/OnResume/OnStop   engine state transitions (every behaviour)
	//   OnCollisionEnter/Stay/Exit       physics contacts of its entity's body
	//   Exit         once, when removed or when its entity/scene is destroyed
	//
	// Which states a behaviour runs in is its BehaviorComponents mask (Playing by
	// default). Mutate the scene through GetScene().GetCommandBuffer() while iterating.
	//
	// The scene runs behaviours from dense per-phase lists (BehaviorScheduler) in attach
	// order. Only the hooks a type overrides cost anything: a behaviour without Update is not
	// in the per-frame list at all. UsesRealTime is read once, when the behaviour is attached.
	class Behavior : public Machine
	{

	  public:

		Behavior(Scene* scene, entt::entity owner);

		~Behavior() override = default;

		bool HasInited() const { return hasInited; }

		void SetInited() { hasInited = true; }

		void InitIfNeeded()
		{
			if (!hasInited)
			{
				hasInited = true;
				Init();
			}
		}

		// Re-reads the cached services (input, camera).
		void RefreshFieldCache();

		// Real-time behaviours (cameras, UI helpers) receive wall-clock deltas in Update and
		// keep moving while the simulation is paused or time-scaled.
		virtual bool UsesRealTime() const { return false; }

		virtual void OnPlay() {}

		virtual void OnPause() {}

		virtual void OnResume() {}

		virtual void OnStop() {}

		virtual void OnCollisionEnter(const BehaviorCollision& collision) {}

		virtual void OnCollisionStay(const BehaviorCollision& collision) {}

		virtual void OnCollisionExit(const BehaviorCollision& collision) {}

		// Trigger volumes (a Rigidbody with isTrigger): Enter and Exit as the overlap starts
		// and ends, Stay every fixed step in between. Like the collision hooks they need
		// EnableCollisionCallBacks.
		virtual void OnTriggerEnter(const BehaviorTrigger& trigger) {}

		virtual void OnTriggerStay(const BehaviorTrigger& trigger) {}

		virtual void OnTriggerExit(const BehaviorTrigger& trigger) {}

		bool RunCollisionCallBacks() const { return runCollisionCallBacks; }

		void EnableCollisionCallBacks(bool value = true) { runCollisionCallBacks = value; }

		Scene& GetScene() const { return *scene; }

		entt::entity GetEntity() const { return entity; }

		// Null when the entity has no Transform. Looked up each call: EnTT storage may move
		// components, so behaviours never cache component pointers across frames.
		Transform* GetTransform() const;

		Swim::Input::InputSystem* GetInput() const { return input; }

		CameraSystem* GetCameraSystem() const { return cameraSystem; }

		// The current frame's simulated time (pause, time scale, fixed steps).
		const SimulationFrame& GetTime() const;

		// Scene timers owned by this behaviour: cancelled when it is destroyed.
		TimerHandle After(double seconds, std::function<void()> callback);

		TimerHandle Every(double interval, std::function<void()> callback);

		TimerHandle When(std::function<bool()> predicate, std::function<void()> callback);

		TimerHandle StartCoroutine(SceneTask task);

		bool CancelTimer(TimerHandle timer);

	  protected:

		Scene* scene = nullptr;
		entt::entity entity = entt::null;
		Swim::Input::InputSystem* input = nullptr;
		CameraSystem* cameraSystem = nullptr;
		bool runCollisionCallBacks = false;
		bool hasInited = false;

	  private:

		SceneTimers::Owner OwnedTimersToken();

		std::shared_ptr<int> ownedTimersToken; // Owner token of this behaviour's timers.

		friend class BehaviorScheduler;

		static constexpr std::uint32_t NoSlot = UINT32_MAX;

		// Where the scheduler lists this behaviour (all, update, fixed update). Never copied:
		// a copied or moved behaviour starts unscheduled.
		struct SchedulerLink
		{
			std::uint32_t Slots[3] = { NoSlot, NoSlot, NoSlot };
			bool Scheduled = false;

			SchedulerLink() = default;

			SchedulerLink(const SchedulerLink&) {}

			SchedulerLink& operator=(const SchedulerLink&) { return *this; }
		};

		SchedulerLink link;

	};

} // namespace Engine
