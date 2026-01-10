#pragma once

#include <memory>
#include <vector>

#include "Library/EnTT/entt.hpp"

#include "PxPhysicsAPI.h"
#include "extensions/PxRigidBodyExt.h"

#include "Engine/Components/Transform.h"
#include "Rigidbody.h"

namespace Engine
{

	class PhysicsSystem;

	class PhysicsWorld
	{

	public:

		explicit PhysicsWorld(PhysicsSystem& physicsSystem, entt::registry& registry);

		~PhysicsWorld();

		bool Init();

		void PreSimulateSync(float dt);
		void Step(float dt);
		void FetchResults(bool block = true);
		void PostSimulateSync();

		// Called every frame to smoothly render dynamic bodies between fixed ticks.
		// alpha is in [0,1] where 0 = previous tick, 1 = current tick.
		void Interpolate(float alpha);

		physx::PxScene* GetPxScene() const { return scene.get(); }
		physx::PxMaterial* GetDefaultMaterial() const { return defaultMaterial.get(); }

		bool HasActor(entt::entity e) const;

		void AddForce(entt::entity e, const glm::vec3& force, physx::PxForceMode::Enum mode = physx::PxForceMode::eFORCE, bool autowake = true);

		void SetLinearVelocity(entt::entity e, const glm::vec3& vel, bool autowake = true);
		void SetAngularVelocity(entt::entity e, const glm::vec3& vel, bool autowake = true);

	private:

		struct PxReleaser
		{
			template<typename T>
			void operator()(T* ptr) const
			{
				if (ptr)
				{
					ptr->release();
				}
			}
		};

		PhysicsSystem& physicsSystem;
		entt::registry& registry;

		std::unique_ptr<physx::PxScene, PxReleaser> scene;
		std::unique_ptr<physx::PxMaterial, PxReleaser> defaultMaterial;

		bool initialized = false;

		// If true, PhysX has a simulate() in-flight and we must not remove actors immediately.
		bool simulating = false;

		// Deferred destruction queue (actors are removed/released at a safe point).
		std::vector<physx::PxRigidActor*> pendingDestroy;

	private:

		void OnRigidbodyConstruct(entt::registry& reg, entt::entity entity);
		void OnRigidbodyDestroy(entt::registry& reg, entt::entity entity);
		void OnTransformDestroy(entt::registry& reg, entt::entity entity);

		void CreateOrRebuildBody(entt::entity entity, Transform& tf, Rigidbody& rb);
		void DestroyBody(entt::entity entity, Rigidbody& rb);

		void FlushPendingDestroy();

		physx::PxTransform GetPoseFromTransform(entt::entity entity, Transform& tf) const;

		static physx::PxVec3 ToPx(const glm::vec3& v)
		{
			return physx::PxVec3(v.x, v.y, v.z);
		}

		static glm::vec3 ToGlm(const physx::PxVec3& v)
		{
			return glm::vec3(v.x, v.y, v.z);
		}

		static physx::PxQuat ToPx(const glm::quat& q)
		{
			return physx::PxQuat(q.x, q.y, q.z, q.w);
		}

		static glm::quat ToGlm(const physx::PxQuat& q)
		{
			return glm::quat(q.w, q.x, q.y, q.z);
		}

	};

} // namespace Engine
