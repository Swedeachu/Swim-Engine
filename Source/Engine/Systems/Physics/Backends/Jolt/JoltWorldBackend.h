#pragma once

#include "Engine/Systems/Physics/IPhysicsBackend.h"
#include "Engine/Systems/Physics/Internal/GenerationalHandleTable.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace Engine
{

	class JoltWorldBackend final : public IPhysicsWorldBackend
	{

	public:

		JoltWorldBackend(JPH::JobSystem& jobSystem, PhysicsWorldDesc desc);
		~JoltWorldBackend() override;

		bool Initialize();

		PhysicsMaterialHandle CreateMaterial(const PhysicsMaterialDesc& desc) override;
		void DestroyMaterial(PhysicsMaterialHandle material) override;
		bool IsMaterialValid(PhysicsMaterialHandle material) const override;

		ShapeHandle CreateShape(const ShapeDesc& desc, PhysicsMaterialHandle material) override;
		void DestroyShape(ShapeHandle shape) override;
		bool IsShapeValid(ShapeHandle shape) const override;

		BodyHandle CreateBody(const BodyDesc& desc) override;
		void DestroyBody(BodyHandle body) override;
		bool IsBodyValid(BodyHandle body) const override;

		bool SetBodyPose(BodyHandle body, const PhysicsPose& pose, bool autowake = true) override;
		bool SetKinematicTarget(BodyHandle body, const PhysicsPose& pose) override;
		bool GetBodyPose(BodyHandle body, PhysicsPose& pose) const override;

		bool AddForce(BodyHandle body, const glm::vec3& force, ForceMode mode = ForceMode::Force, bool autowake = true) override;
		bool SetLinearVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true) override;
		bool SetAngularVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true) override;
		bool GetLinearVelocity(BodyHandle body, glm::vec3& velocity) const override;
		bool GetAngularVelocity(BodyHandle body, glm::vec3& velocity) const override;

		void BeginSimulation(float dt) override;
		bool FetchResults(bool block = true) override;
		bool IsSimulationInFlight() const override { return simulating; }

		bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter = {}) const override;
		bool Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance, SweepHit& hit, CollisionLayer filter = {}) const override;
		std::size_t Overlap(const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter = {}) const override;

		std::span<const CollisionEvent> GetCollisionEvents() const override { return collisionEvents; }
		std::span<const TriggerEvent> GetTriggerEvents() const override { return triggerEvents; }

	private:

		class BroadPhaseLayerInterface;
		class ObjectVsBroadPhaseFilter;
		class ObjectPairFilter;
		class ContactCallback;
		class QueryBodyFilter;
		class QueryObjectLayerFilter;

		struct ShapeRecord
		{
			JPH::RefConst<JPH::Shape> NativeShape;
			PhysicsMaterialHandle Material{};
			PhysicsMaterialDesc MaterialDesc{};
			bool IsTrigger = false;
		};

		struct BodyRecord
		{
			JPH::BodyID NativeBody{};
			ShapeHandle Shape{};
			CollisionLayer Collision{};
			MotionType Motion = MotionType::Static;
			float Mass = 1.0f;
			std::uint64_t UserData = 0;
			PhysicsPose PendingKinematicTarget{};
			bool HasPendingKinematicTarget = false;
		};

		struct ContactKey
		{
			std::uint32_t BodyA = 0;
			std::uint32_t SubShapeA = 0;
			std::uint32_t BodyB = 0;
			std::uint32_t SubShapeB = 0;

			bool operator==(const ContactKey&) const = default;
		};

		struct ContactKeyHash
		{
			std::size_t operator()(const ContactKey& key) const noexcept;
		};

		struct ActiveContact
		{
			bool IsTrigger = false;
			BodyHandle BodyA{};
			BodyHandle BodyB{};
			ShapeHandle ShapeA{};
			ShapeHandle ShapeB{};
			BodyHandle TriggerBody{};
			BodyHandle OtherBody{};
			ShapeHandle TriggerShape{};
			ShapeHandle OtherShape{};
			glm::vec3 Position{ 0.0f };
			glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		};

		// Jolt's per-step scratch. TempAllocatorMalloc round-trips the general
		// allocator for every temporary the solver makes; a preallocated arena
		// with a malloc fallback keeps the fast path allocation-free without
		// being able to run out.
		static constexpr JPH::uint TempAllocatorBytes = 16u * 1024u * 1024u;

		JPH::TempAllocatorImplWithMallocFallback tempAllocator{ TempAllocatorBytes };
		JPH::JobSystem& jobSystem;
		PhysicsWorldDesc worldDesc{};
		JPH::PhysicsSystem physicsSystem;
		std::unique_ptr<BroadPhaseLayerInterface> broadPhaseLayerInterface;
		std::unique_ptr<ObjectVsBroadPhaseFilter> objectVsBroadPhaseFilter;
		std::unique_ptr<ObjectPairFilter> objectPairFilter;
		std::unique_ptr<ContactCallback> contactCallback;

		GenerationalHandleTable<PhysicsMaterialHandle, PhysicsMaterialDesc> materials;
		GenerationalHandleTable<ShapeHandle, ShapeRecord> shapes;
		GenerationalHandleTable<BodyHandle, BodyRecord> bodies;
		std::vector<JPH::BodyID> pendingDestroy;
		std::vector<CollisionEvent> collisionEvents;
		std::vector<TriggerEvent> triggerEvents;
		std::unordered_map<ContactKey, ActiveContact, ContactKeyHash> activeContacts;
		mutable std::mutex eventMutex;
		bool initialized = false;
		bool simulating = false;
		bool lastStepSucceeded = true;

		// Jolt's broadphase quadtree degrades as bodies are inserted. Optimizing
		// is only worthwhile after non-moving bodies arrive (level load), never
		// per spawned projectile, so track that case specifically.
		bool nonMovingBodiesAdded = false;

		static JPH::Vec3 ToJolt(const glm::vec3& value);
		static JPH::RVec3 ToJoltPosition(const glm::vec3& value);
		static glm::vec3 ToGlm(JPH::Vec3Arg value);
#ifdef JPH_DOUBLE_PRECISION
		static glm::vec3 ToGlm(JPH::RVec3Arg value);
#endif
		static JPH::Quat ToJolt(const glm::quat& value);
		static glm::quat ToGlm(JPH::QuatArg value);
		static JPH::RMat44 ToJoltTransform(const PhysicsPose& pose);
		static PhysicsPose ToEngine(JPH::RVec3Arg position, JPH::QuatArg rotation);
		static std::uint64_t PackHandle(BodyHandle handle);
		static BodyHandle UnpackHandle(std::uint64_t packed);
		static ContactKey MakeContactKey(const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold);
		static ContactKey MakeContactKey(const JPH::SubShapeIDPair& pair);

		JPH::RefConst<JPH::Shape> CreateNativeShape(const ShapeDesc& desc) const;
		BodyHandle ResolveBody(const JPH::Body& body) const;
		BodyHandle ResolveBody(JPH::BodyID bodyId) const;
		const BodyRecord* ResolveBodyRecord(const JPH::Body& body) const;
		ShapeHandle ResolveShape(BodyHandle body) const;
		std::uint64_t ResolveUserData(BodyHandle body) const;
		bool MatchesQuery(const JPH::Body& body, CollisionLayer filter) const;

		void ApplyPendingKinematicTargets(float dt);
		void FlushPendingDestroy();
		void ReleaseBody(JPH::BodyID bodyId);
		void RecordContact(const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold, const JPH::ContactSettings& settings, bool persisted);
		void RemoveContact(const JPH::SubShapeIDPair& pair);

	};

}
