#pragma once

#include "PhysicsTypes.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace Engine
{

	using RigidbodyType = MotionType;
	using ColliderType = ShapeType;

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
		CollisionLayer collision{};

		// Runtime physics identity is backend-neutral. The selected backend owns all
		// PhysX/Jolt objects behind this generational handle.
		BodyHandle body{};

		// If true, the scene physics bridge rebuilds the generic body/shape/material
		// binding at the next synchronization point.
		bool dirty = true;

		// Optional initial velocities to apply once the body exists.
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
