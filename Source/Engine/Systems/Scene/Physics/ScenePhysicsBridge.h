#pragma once

#include "Engine/Systems/Physics/PhysicsWorld.h"
#include "Engine/Systems/Physics/RigidBody.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Engine
{

	class PhysicsSystem;
	class Transform;

	class ScenePhysicsBridge
	{

	public:

		ScenePhysicsBridge(PhysicsSystem& physicsSystem, entt::registry& registry, PhysicsWorldDesc desc = {});
		~ScenePhysicsBridge();

		bool Init();

		PhysicsWorld& GetWorld() { return *world; }
		const PhysicsWorld& GetWorld() const { return *world; }

		void PreSimulateSync(float dt);
		void Step(float dt);
		void FetchResults(bool block = true);
		void PostSimulateSync();
		void Interpolate(float alpha);

		bool HasBody(entt::entity entity) const;
		void AddForce(entt::entity entity, const glm::vec3& force, ForceMode mode = ForceMode::Force, bool autowake = true);
		void SetLinearVelocity(entt::entity entity, const glm::vec3& velocity, bool autowake = true);
		void SetAngularVelocity(entt::entity entity, const glm::vec3& velocity, bool autowake = true);

	private:

		struct BodyResources
		{
			ShapeHandle Shape{};
			PhysicsMaterialHandle Material{};
			glm::vec3 Scale{ 1.0f };
		};

		PhysicsSystem& physicsSystem;
		entt::registry& registry;
		PhysicsWorldDesc worldDesc{};
		std::unique_ptr<PhysicsWorld> world;
		std::unordered_map<entt::entity, BodyResources> bodyResources;
		bool initialized = false;

		void OnRigidbodyConstruct(entt::registry& reg, entt::entity entity);
		void OnRigidbodyDestroy(entt::registry& reg, entt::entity entity);
		void OnTransformDestroy(entt::registry& reg, entt::entity entity);

		void CreateOrRebuildBody(entt::entity entity, Transform& transform, Rigidbody& rigidbody);
		void DestroyEntityBody(entt::entity entity, Rigidbody& rigidbody);

		PhysicsPose GetPoseFromTransform(entt::entity entity, Transform& transform) const;

	};

} // namespace Engine
