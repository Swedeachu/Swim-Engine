#pragma once

#include "Library/glm/glm.hpp"
#include <cstdint>

namespace physx
{
	class PxRigidActor;
	class PxShape;
}

namespace Engine
{

	enum class RigidbodyType : std::uint8_t
	{
		Static = 0,
		Dynamic,
		Kinematic
	};

	enum class ColliderType : std::uint8_t
	{
		Box = 0,
		Sphere,
		Capsule
	};

	struct BoxCollider
	{
		glm::vec3 halfExtents{ 0.5f, 0.5f, 0.5f };
	};

	struct SphereCollider
	{
		float radius = 0.5f;
	};

	struct CapsuleCollider
	{
		float radius = 0.5f;
		float halfHeight = 0.5f;
	};

	struct Collider
	{
		ColliderType type = ColliderType::Box;

		BoxCollider box;
		SphereCollider sphere;
		CapsuleCollider capsule;
	};

	class Rigidbody
	{

	public:

		RigidbodyType type = RigidbodyType::Dynamic;

		bool useGravity = true;
		bool startAwake = true;

		bool isTrigger = false;

		float mass = 1.0f;
		float linearDamping = 0.01f;
		float angularDamping = 0.05f;

		Collider collider;

		// Backend-owned handles (created/owned by PhysicsWorld).
		// Forward declared so gameplay can include Rigidbody without PhysX headers.
		physx::PxRigidActor* actor = nullptr;
		physx::PxShape* shape = nullptr;

		// If true, PhysicsWorld will rebuild this actor/shape on next sync.
		bool dirty = true;

		// Optional initial velocities to apply once actor exists.
		// (Needed because SetLinearVelocity() early-outs if actor isn't created yet.)
		bool hasInitialLinearVelocity = false;
		bool hasInitialAngularVelocity = false;

		glm::vec3 initialLinearVelocity{ 0.0f };
		glm::vec3 initialAngularVelocity{ 0.0f };

		void SetInitialLinearVelocity(const glm::vec3& v)
		{
			hasInitialLinearVelocity = true;
			initialLinearVelocity = v;
		}

		void SetInitialAngularVelocity(const glm::vec3& v)
		{
			hasInitialAngularVelocity = true;
			initialAngularVelocity = v;
		}

		void ClearInitialVelocities()
		{
			hasInitialLinearVelocity = false;
			hasInitialAngularVelocity = false;
			initialLinearVelocity = glm::vec3(0.0f);
			initialAngularVelocity = glm::vec3(0.0f);
		}

	};

} // namespace Engine
