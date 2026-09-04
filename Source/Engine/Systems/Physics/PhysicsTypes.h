#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "PhysicsHandles.h"

#include <cstdint>
#include <limits>

namespace Engine
{

	enum class MotionType : std::uint8_t
	{
		Static = 0,
		Dynamic,
		Kinematic
	};

	enum class ShapeType : std::uint8_t
	{
		Box = 0,
		Sphere,
		Capsule,
		ConvexMesh,
		TriangleMesh
	};

	enum class ForceMode : std::uint8_t
	{
		Force = 0,
		Impulse,
		VelocityChange,
		Acceleration
	};

	struct CollisionLayer
	{
		std::uint32_t Layer = 1u;
		std::uint32_t Mask = std::numeric_limits<std::uint32_t>::max();
	};

	struct PhysicsPose
	{
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	struct BoxShapeDesc
	{
		glm::vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
	};

	struct SphereShapeDesc
	{
		float Radius = 0.5f;
	};

	struct CapsuleShapeDesc
	{
		float Radius = 0.5f;
		float HalfHeight = 0.5f;
	};

	struct MeshShapeDesc
	{
		std::uint64_t CollisionAsset = 0;
	};

	struct ShapeDesc
	{
		ShapeType Type = ShapeType::Box;
		BoxShapeDesc Box;
		SphereShapeDesc Sphere;
		CapsuleShapeDesc Capsule;
		MeshShapeDesc Mesh;
		PhysicsPose LocalPose{};
		bool IsTrigger = false;
	};

	struct PhysicsMaterialDesc
	{
		// Backends that model a single Coulomb friction coefficient (Jolt) use
		// DynamicFriction and ignore StaticFriction. Backends that separate the
		// two (PhysX) honour both. Author materials against DynamicFriction when
		// cross-backend parity matters.
		float StaticFriction = 0.5f;
		float DynamicFriction = 0.5f;
		float Restitution = 0.1f;
	};

	struct BodyDesc
	{
		MotionType Motion = MotionType::Dynamic;
		ShapeHandle Shape{};
		PhysicsPose Pose{};
		CollisionLayer Collision{};
		bool UseGravity = true;
		bool StartAwake = true;
		float Mass = 1.0f;
		float LinearDamping = 0.01f;
		float AngularDamping = 0.05f;
		glm::vec3 InitialLinearVelocity{ 0.0f };
		glm::vec3 InitialAngularVelocity{ 0.0f };
		bool HasInitialLinearVelocity = false;
		bool HasInitialAngularVelocity = false;
		std::uint64_t UserData = 0;
	};

	struct PhysicsWorldDesc
	{
		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
		bool EnableContinuousCollisionDetection = true;

		// Per-step CollisionEventType::Persisted reporting costs one event per
		// touching pair per step, plus contact-point extraction, which dominates
		// event cost in a densely stacked scene. Started/Ended are always
		// reported; opt in here only when gameplay needs continuous contact.
		bool EnablePersistedCollisionEvents = false;
	};

	struct RaycastHit
	{
		BodyHandle Body{};
		ShapeHandle Shape{};
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		float Distance = 0.0f;
		std::uint64_t UserData = 0;
	};

	struct SweepHit
	{
		BodyHandle Body{};
		ShapeHandle Shape{};
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		float Distance = 0.0f;
		std::uint64_t UserData = 0;
	};

	struct OverlapHit
	{
		BodyHandle Body{};
		ShapeHandle Shape{};
		std::uint64_t UserData = 0;
	};

	enum class CollisionEventType : std::uint8_t
	{
		Started = 0,
		Persisted,
		Ended
	};

	struct CollisionEvent
	{
		CollisionEventType Type = CollisionEventType::Started;
		BodyHandle BodyA{};
		BodyHandle BodyB{};
		ShapeHandle ShapeA{};
		ShapeHandle ShapeB{};
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		float Impulse = 0.0f;
	};

	struct TriggerEvent
	{
		BodyHandle TriggerBody{};
		BodyHandle OtherBody{};
		ShapeHandle TriggerShape{};
		ShapeHandle OtherShape{};
		bool Entered = false;
	};

}
