#pragma once

#include <queue>
#include <functional>
#include <utility>
#include <tuple>

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
				fn(e, tRef, mRef, std::move(a)...);
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
				if (!scene)
				{
					return;
				}

				// Emplace all behaviors first
				((void)scene->EmplaceBehavior<BehaviorTypes>(e), ...);

				// Now that all behaviors are attached, refresh the field cache once
				scene->RefreshBehaviorFieldCacheForEntity(e);
			}
			);
		}

		// Transform + Material + Behaviors (templated callback)
		// The callback receives: (entt::entity e, Transform& t, Material& m, BehaviorTypes*... ptrs, ...args)
		// This assumes all behaviors use the same default (scene*, entity) constructor 
		template<typename... BehaviorTypes, typename Func, typename... Args>
		void CreateWithTransformAndMaterialAndBehaviors(const Transform& transform, const Material& material, Func&& func, Args&&... args)
		{
			QueueCreate(
				[transform, material, fn = std::forward<Func>(func), ...a = std::forward<Args>(args)](entt::registry& reg, entt::entity e) mutable
			{
				// Add components
				Transform& tRef = reg.emplace<Transform>(e, transform);
				Material& mRef = reg.emplace<Material>(e, material);

				Scene* scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene().get();
				if (!scene)
				{
					return;
				}

				// Add behaviors and collect pointers
				auto behaviorPtrs = std::make_tuple(scene->EmplaceBehavior<BehaviorTypes>(e)...);

				// Invoke user callback: entity + components + behavior ptrs + rest
				std::apply([&](auto*... bs)
				{
					fn(e, tRef, mRef, bs..., std::move(a)...);
				}, behaviorPtrs);

				// Refresh behavior field cache once after all behaviors + user callback
				scene->RefreshBehaviorFieldCacheForEntity(e);
			}
			);
		}

		// This assumes all behaviors use the same default (scene*, entity) constructor 
		// Super powerful way to just load all your scripts into the scene that aren't reliant on hierarchy of entities or physical entities (Score Manager, Game Manager, etc)
		template<typename... BehaviorTypes>
		void CreateWithBehaviors()
		{
			QueueCreate(
				[](entt::registry& reg, entt::entity e)
			{
				Scene* scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene().get();
				if (!scene)
				{
					return;
				}

				// Emplace all behaviors
				((void)scene->EmplaceBehavior<BehaviorTypes>(e), ...);

				// Refresh field cache once after all behaviors are attached
				scene->RefreshBehaviorFieldCacheForEntity(e);
			}
			);
		}

		// This assumes all behaviors use the same default (scene*, entity) constructor 
		// Behaviors + callback variant: callback receives (entt::entity e, BehaviorTypes*... created, ...args)
		template<typename... BehaviorTypes, typename Func, typename... Args>
		void CreateWithBehaviors(Func&& func, Args&&... args)
		{
			QueueCreate(
				[fn = std::forward<Func>(func), ...a = std::forward<Args>(args)](entt::registry& reg, entt::entity e) mutable
			{
				Scene* scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene().get();
				if (!scene)
				{
					return;
				}

				auto tuplePtrs = std::make_tuple(scene->EmplaceBehavior<BehaviorTypes>(e)...);

				std::apply([&](auto*... ps)
				{
					fn(e, ps..., std::move(a)...);
				}, tuplePtrs);

				// Refresh behavior field cache once after all behaviors + user callback
				scene->RefreshBehaviorFieldCacheForEntity(e);
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
				fn(reg, e, std::move(args)...);
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
				fn(std::move(args)...);
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
