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

		// Transform + Material (no callback)
		void CreateWithTransformAndMaterial(const Transform& transform, const Material& material);

		// Transform + Material (templated callback)
		// The callback receives: (entt::entity e, Transform& t, Material& m, ...args)
		template<typename Func, typename... Args>
		void CreateWithTransformAndMaterial(const Transform& transform, const Material& material, Func&& func, Args&&... args)
		{
			QueueCreate(
				[transform, material, fn = std::forward<Func>(func), ...a = std::forward<Args>(args)](entt::registry& reg, entt::entity e) mutable
			{
				// Create components
				Transform& tRef = reg.emplace<Transform>(e, transform);
				Material& mRef = reg.emplace<Material>(e, material);

				// User callback: entity + created components + rest
				fn(e, tRef, mRef, std::forward<Args>(a)...);
			}
			);
		}

		// Transform + Material + Behaviors (no callback) 
		template<typename... BehaviorTypes>
		void CreateWithTransformAndMaterialAndBehaviors(const Transform& transform, const Material& material)
		{
			QueueCreate(
				[transform, material](entt::registry& reg, entt::entity e)
			{
				// Add components
				reg.emplace<Transform>(e, transform);
				reg.emplace<Material>(e, material);

				// Add behaviors
				Scene* scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene().get();
				([](Scene* s, entt::entity entity)
				{
					s->EmplaceBehavior<BehaviorTypes>(entity);
				}(scene, e), ...);
			}
			);
		}

		// Transform + Material + Behaviors (templated callback)
		// The callback receives: (entt::entity e, Transform& t, Material& m, BehaviorTypes*... ptrs, ...args)
		template<typename... BehaviorTypes, typename Func, typename... Args>
		void CreateWithTransformAndMaterialAndBehaviors(const Transform& transform, const Material& material, Func&& func, Args&&... args)
		{
			QueueCreate(
				[transform, material, fn = std::forward<Func>(func), ...a = std::forward<Args>(args)](entt::registry& reg, entt::entity e) mutable
			{
				// Add components
				Transform& tRef = reg.emplace<Transform>(e, transform);
				Material& mRef = reg.emplace<Material>(e, material);

				// Add behaviors and collect pointers
				Scene* scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene().get();
				auto behaviorPtrs = std::make_tuple(scene->EmplaceBehavior<BehaviorTypes>(e)...);

				// Invoke user callback: entity + components + behavior ptrs + rest
				std::apply([&](auto*... bs)
				{
					fn(e, tRef, mRef, bs..., std::forward<Args>(a)...);
				}, behaviorPtrs);
			}
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

		// Behaviors + callback variant: callback receives (entt::entity e, BehaviorTypes*... created, ...args)
		template<typename... BehaviorTypes, typename Func, typename... Args>
		void CreateWithBehaviors(Func&& func, Args&&... args)
		{
			QueueCreate(
				[fn = std::forward<Func>(func), ...a = std::forward<Args>(args)](entt::registry& reg, entt::entity e) mutable
			{
				Scene* scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene().get();

				auto tuplePtrs = std::make_tuple(scene->EmplaceBehavior<BehaviorTypes>(e)...);

				std::apply([&](auto*... ps)
				{
					fn(e, ps..., std::forward<Args>(a)...);
				}, tuplePtrs);
			}
			);
		}

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

		// Processes all queued creates and destroys. Should be called once per frame.
		void ProcessQueues();

	private:

		std::queue<std::function<void(entt::registry&, entt::entity)>> createQueue;
		std::queue<std::function<void()>> destroyQueue;

	};

}
