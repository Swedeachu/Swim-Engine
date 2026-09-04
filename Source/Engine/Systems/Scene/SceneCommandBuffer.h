#pragma once

#include "DeferredCommandBuffer.h"
#include "Scene.h"

#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Engine
{

	class SceneCommandBuffer
	{
	public:

		explicit SceneCommandBuffer(Scene& scene)
			: scene(&scene)
		{}

		SceneCommandBuffer(const SceneCommandBuffer&) = delete;
		SceneCommandBuffer& operator=(const SceneCommandBuffer&) = delete;
		SceneCommandBuffer(SceneCommandBuffer&&) = delete;
		SceneCommandBuffer& operator=(SceneCommandBuffer&&) = delete;

		std::size_t Flush();
		void Clear();
		std::size_t GetPendingCount() const { return commands.GetPendingCount(); }
		bool IsFlushing() const { return commands.IsFlushing(); }

		// Queue a custom scene mutation. Commands queued while Flush() is running
		// are deliberately retained for the next flush instead of being executed
		// recursively in the current frame.
		template<typename Func, typename... Args>
		void Defer(Func&& func, Args&&... args)
		{
			commands.Enqueue(
				[fn = std::forward<Func>(func), ...capturedArgs = std::forward<Args>(args)](Scene& owningScene) mutable
			{
				std::invoke(fn, owningScene, std::move(capturedArgs)...);
			}
			);
		}

		// Queue creation of one entity. The callback receives the owning Scene and
		// the real EnTT entity after it has been created during Flush().
		template<typename Func, typename... Args>
		void Create(Func&& func, Args&&... args)
		{
			Defer(
				[fn = std::forward<Func>(func), ...capturedArgs = std::forward<Args>(args)](Scene& owningScene) mutable
			{
				entt::entity entity = owningScene.CreateEntity();
				std::invoke(fn, owningScene, entity, std::move(capturedArgs)...);
			}
			);
		}

		void Create();
		void Destroy(entt::entity entity, bool callExit = true, bool destroyChildren = true);

		template<typename T, typename... Args>
		void AddComponent(entt::entity entity, Args&&... args)
		{
			using CapturedArgs = std::tuple<std::decay_t<Args>...>;
			CapturedArgs captured(std::forward<Args>(args)...);

			Defer(
				[entity, captured = std::move(captured)](Scene& owningScene) mutable
			{
				if (!owningScene.GetRegistry().valid(entity))
				{
					return;
				}

				std::apply(
					[&](auto&&... unpacked)
				{
					owningScene.EmplaceComponent<T>(entity, std::forward<decltype(unpacked)>(unpacked)...);
				},
					std::move(captured)
				);
			}
			);
		}

		template<typename T>
		void RemoveComponent(entt::entity entity)
		{
			Defer(
				[entity](Scene& owningScene)
			{
				owningScene.RemoveComponent<T>(entity);
			}
			);
		}

		// High-level creation helpers retained while gameplay call sites move toward
		// explicit generic SceneCommandBuffer operations.
		void CreateWithTransform(const Transform& transform);
		void CreateWithTransformAndMaterial(const Transform& transform, const Material& material);

		template<typename Func, typename... Args>
		void CreateWithTransformAndMaterial(const Transform& transform, const Material& material, Func&& func, Args&&... args)
		{
			Create(
				[transform, material, fn = std::forward<Func>(func), ...capturedArgs = std::forward<Args>(args)](Scene& owningScene, entt::entity entity) mutable
			{
				Transform& transformRef = owningScene.AddComponent<Transform>(entity, transform);
				Material& materialRef = owningScene.AddComponent<Material>(entity, material);
				std::invoke(fn, entity, transformRef, materialRef, std::move(capturedArgs)...);
			}
			);
		}

		template<typename... BehaviorTypes>
		void CreateWithTransformAndMaterialAndBehaviors(const Transform& transform, const Material& material)
		{
			Create(
				[transform, material](Scene& owningScene, entt::entity entity)
			{
				owningScene.AddComponent<Transform>(entity, transform);
				owningScene.AddComponent<Material>(entity, material);
				((void)owningScene.EmplaceBehavior<BehaviorTypes>(entity), ...);
				owningScene.RefreshBehaviorFieldCacheForEntity(entity);
			}
			);
		}

		template<typename... BehaviorTypes, typename Func, typename... Args>
		void CreateWithTransformAndMaterialAndBehaviors(const Transform& transform, const Material& material, Func&& func, Args&&... args)
		{
			Create(
				[transform, material, fn = std::forward<Func>(func), ...capturedArgs = std::forward<Args>(args)](Scene& owningScene, entt::entity entity) mutable
			{
				Transform& transformRef = owningScene.AddComponent<Transform>(entity, transform);
				Material& materialRef = owningScene.AddComponent<Material>(entity, material);
				auto behaviorPointers = std::make_tuple(owningScene.EmplaceBehavior<BehaviorTypes>(entity)...);

				std::apply(
					[&](auto*... behaviors)
				{
					std::invoke(fn, entity, transformRef, materialRef, behaviors..., std::move(capturedArgs)...);
				},
					behaviorPointers
				);

				owningScene.RefreshBehaviorFieldCacheForEntity(entity);
			}
			);
		}

		template<typename... BehaviorTypes>
		void CreateWithBehaviors()
		{
			Create(
				[](Scene& owningScene, entt::entity entity)
			{
				((void)owningScene.EmplaceBehavior<BehaviorTypes>(entity), ...);
				owningScene.RefreshBehaviorFieldCacheForEntity(entity);
			}
			);
		}

		template<typename... BehaviorTypes, typename Func, typename... Args>
		void CreateWithBehaviors(Func&& func, Args&&... args)
		{
			Create(
				[fn = std::forward<Func>(func), ...capturedArgs = std::forward<Args>(args)](Scene& owningScene, entt::entity entity) mutable
			{
				auto behaviorPointers = std::make_tuple(owningScene.EmplaceBehavior<BehaviorTypes>(entity)...);

				std::apply(
					[&](auto*... behaviors)
				{
					std::invoke(fn, entity, behaviors..., std::move(capturedArgs)...);
				},
					behaviorPointers
				);

				owningScene.RefreshBehaviorFieldCacheForEntity(entity);
			}
			);
		}

	private:

		Scene* scene = nullptr;
		DeferredCommandBuffer<Scene> commands;

	};

}
