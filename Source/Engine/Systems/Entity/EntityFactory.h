#pragma once

#include <queue>
#include <functional>
#include <utility>
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"

namespace Engine
{

	// Using Create and Destroy methods will do operations on the current active scene.
	class EntityFactory
	{

	public:

		EntityFactory() = default;

		static EntityFactory& GetInstance()
		{
			static EntityFactory instance;
			return instance;
		}

		// High-level helpers for common entity creation which do the common queue create lambdas for us
		void CreateWithTransform(const Transform& transform);
		void CreateWithTransformAndMaterial(const Transform& transform, const Material& material);

		// High-level helper for destruction
		void Destroy(entt::entity entity);

		// Queues an entity creation with a templated callback and optional args, often used for creation with a bunch of components.
		template<typename Func, typename... Args>
		void QueueCreate(Func&& func, Args&&... args)
		{
			createQueue.push(
				[fn = std::forward<Func>(func), ...args = std::forward<Args>(args)](entt::registry& reg, entt::entity e) mutable
			{
				fn(reg, e, std::forward<Args>(args)...);
			}
			);
		}

		// Queues an entity destruction with a templated callback and optional args.
		template<typename Func, typename... Args>
		void QueueDestroy(Func&& func, Args&&... args)
		{
			destroyQueue.push(
				[fn = std::forward<Func>(func), ...args = std::forward<Args>(args)]() mutable
			{
				fn(std::forward<Args>(args)...);
			}
			);
		}

		template<typename... BehaviorTypes>
		void CreateWithTransformMaterialAndBehaviors(const Transform& transform, const Material& material)
		{
			QueueCreate(
				[](entt::registry& reg, entt::entity e, const Transform& t, const Material& m)
			{
				reg.emplace<Transform>(e, t);
				reg.emplace<Material>(e, m);

				Scene* scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene().get();

				// Expand the parameter pack for behaviors
				([](Scene* s, entt::entity entity)
				{
					s->EmplaceBehavior<BehaviorTypes>(entity);
				}(scene, e), ...);
			},
				transform, material
			);
		}

		// Super powerful way to just load all your scripts into the scene that aren't reliant on hierarchy of entities or physical entities (Score Manager, Game Manager, etc)
		template<typename... BehaviorTypes>
		void CreateWithBehaviors()
		{
			QueueCreate(
				[](entt::registry& reg, entt::entity e)
			{
				Scene* scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene().get();

				([](Scene* s, entt::entity entity)
				{
					s->EmplaceBehavior<BehaviorTypes>(entity);
				}(scene, e), ...);
			}
			);
		}

		// Processes all queued creates and destroys. Should be called once per frame.
		void ProcessQueues();

	private:

		std::queue<std::function<void(entt::registry&, entt::entity)>> createQueue;
		std::queue<std::function<void()>> destroyQueue;

	};

}
