#include "JoltWorldBackend.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Engine
{

	namespace
	{

		namespace BroadPhaseLayers
		{
			static constexpr JPH::BroadPhaseLayer NonMoving(0);
			static constexpr JPH::BroadPhaseLayer Moving(1);
			static constexpr JPH::uint Count = 2;
		}

		bool IsFiniteVec3(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFiniteQuat(const glm::quat& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
		}

		bool IsValidPose(const PhysicsPose& pose)
		{
			if (!IsFiniteVec3(pose.Position) || !IsFiniteQuat(pose.Rotation))
			{
				return false;
			}

			const float lengthSquared = pose.Rotation.x * pose.Rotation.x
				+ pose.Rotation.y * pose.Rotation.y
				+ pose.Rotation.z * pose.Rotation.z
				+ pose.Rotation.w * pose.Rotation.w;
			return lengthSquared > 0.000001f && std::isfinite(lengthSquared);
		}

		bool IsValidDirection(const glm::vec3& direction, float maxDistance)
		{
			if (!IsFiniteVec3(direction) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance))
			{
				return false;
			}

			const float lengthSquared = glm::dot(direction, direction);
			return lengthSquared > 0.000001f && std::isfinite(lengthSquared);
		}

		bool LayersMatch(const CollisionLayer& a, const CollisionLayer& b)
		{
			return (a.Layer & b.Mask) != 0u && (b.Layer & a.Mask) != 0u;
		}

		JPH::EMotionType ToJoltMotionType(MotionType motion)
		{
			switch (motion)
			{
				case MotionType::Static:
					return JPH::EMotionType::Static;
				case MotionType::Dynamic:
					return JPH::EMotionType::Dynamic;
				case MotionType::Kinematic:
					return JPH::EMotionType::Kinematic;
			}
			return JPH::EMotionType::Static;
		}

	}

	class JoltWorldBackend::BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
	{

	public:

		struct RegisteredLayer
		{
			CollisionLayer Collision{};
			bool IsMoving = false;
		};

		JPH::ObjectLayer Register(MotionType motion, CollisionLayer collision)
		{
			const LayerKey key{ collision.Layer, collision.Mask, motion != MotionType::Static };
			const auto found = registeredLayerLookup.find(key);
			if (found != registeredLayerLookup.end())
			{
				return found->second;
			}

			if (registeredLayers.size() >= static_cast<std::size_t>(JPH::cObjectLayerInvalid))
			{
				return JPH::cObjectLayerInvalid;
			}

			const JPH::ObjectLayer objectLayer = static_cast<JPH::ObjectLayer>(registeredLayers.size());
			registeredLayers.push_back(RegisteredLayer{ collision, key.IsMoving });
			registeredLayerLookup.emplace(key, objectLayer);
			return objectLayer;
		}

		const RegisteredLayer* Get(JPH::ObjectLayer layer) const
		{
			const std::size_t index = static_cast<std::size_t>(layer);
			return index < registeredLayers.size() ? &registeredLayers[index] : nullptr;
		}

		JPH::uint GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::Count;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
		{
			const RegisteredLayer* registeredLayer = Get(layer);
			JPH_ASSERT(registeredLayer != nullptr);
			return registeredLayer && registeredLayer->IsMoving
				? BroadPhaseLayers::Moving
				: BroadPhaseLayers::NonMoving;
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
		{
			switch (static_cast<JPH::BroadPhaseLayer::Type>(layer))
			{
				case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NonMoving):
					return "NON_MOVING";
				case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Moving):
					return "MOVING";
				default:
					return "INVALID";
			}
		}
#endif

	private:

		struct LayerKey
		{
			std::uint32_t Layer = 0;
			std::uint32_t Mask = 0;
			bool IsMoving = false;

			bool operator==(const LayerKey&) const = default;
		};

		struct LayerKeyHash
		{
			std::size_t operator()(const LayerKey& key) const noexcept
			{
				std::uint64_t packed = (static_cast<std::uint64_t>(key.Layer) << 32u) | key.Mask;
				packed ^= packed >> 33u;
				packed *= 0xff51afd7ed558ccdULL;
				packed ^= packed >> 33u;
				return static_cast<std::size_t>(packed ^ (key.IsMoving ? 0x9e3779b97f4a7c15ULL : 0ULL));
			}
		};

		std::vector<RegisteredLayer> registeredLayers;
		std::unordered_map<LayerKey, JPH::ObjectLayer, LayerKeyHash> registeredLayerLookup;

	};

	class JoltWorldBackend::ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{

	public:

		explicit ObjectVsBroadPhaseFilter(const BroadPhaseLayerInterface& selectedLayerInterface)
			: layerInterface(selectedLayerInterface)
		{}

		bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
		{
			const BroadPhaseLayerInterface::RegisteredLayer* registeredLayer = layerInterface.Get(layer);
			if (!registeredLayer)
			{
				return false;
			}

			return registeredLayer->IsMoving || broadPhaseLayer == BroadPhaseLayers::Moving;
		}

	private:

		const BroadPhaseLayerInterface& layerInterface;

	};

	class JoltWorldBackend::ObjectPairFilter final : public JPH::ObjectLayerPairFilter
	{

	public:

		explicit ObjectPairFilter(const BroadPhaseLayerInterface& selectedLayerInterface)
			: layerInterface(selectedLayerInterface)
		{}

		bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
		{
			const BroadPhaseLayerInterface::RegisteredLayer* registeredA = layerInterface.Get(a);
			const BroadPhaseLayerInterface::RegisteredLayer* registeredB = layerInterface.Get(b);
			if (!registeredA || !registeredB || (!registeredA->IsMoving && !registeredB->IsMoving))
			{
				return false;
			}

			return LayersMatch(registeredA->Collision, registeredB->Collision);
		}

	private:

		const BroadPhaseLayerInterface& layerInterface;

	};

	class JoltWorldBackend::QueryBodyFilter final : public JPH::BodyFilter
	{

	public:

		QueryBodyFilter(const JoltWorldBackend& selectedOwner, CollisionLayer selectedFilter)
			: owner(selectedOwner), filter(selectedFilter)
		{}

		bool ShouldCollideLocked(const JPH::Body& body) const override
		{
			return owner.MatchesQuery(body, filter);
		}

	private:

		const JoltWorldBackend& owner;
		CollisionLayer filter{};

	};

	class JoltWorldBackend::QueryObjectLayerFilter final : public JPH::ObjectLayerFilter
	{

	public:

		QueryObjectLayerFilter(const BroadPhaseLayerInterface& selectedLayerInterface, CollisionLayer selectedFilter)
			: layerInterface(selectedLayerInterface), filter(selectedFilter)
		{}

		bool ShouldCollide(JPH::ObjectLayer layer) const override
		{
			const BroadPhaseLayerInterface::RegisteredLayer* registeredLayer = layerInterface.Get(layer);
			return registeredLayer && LayersMatch(filter, registeredLayer->Collision);
		}

	private:

		const BroadPhaseLayerInterface& layerInterface;
		CollisionLayer filter{};

	};

	class JoltWorldBackend::ContactCallback final : public JPH::ContactListener
	{

	public:

		explicit ContactCallback(JoltWorldBackend& selectedOwner)
			: owner(selectedOwner)
		{}

		void OnContactAdded(
			const JPH::Body& bodyA,
			const JPH::Body& bodyB,
			const JPH::ContactManifold& manifold,
			JPH::ContactSettings& settings) override
		{
			owner.RecordContact(bodyA, bodyB, manifold, settings, false);
		}

		void OnContactPersisted(
			const JPH::Body& bodyA,
			const JPH::Body& bodyB,
			const JPH::ContactManifold& manifold,
			JPH::ContactSettings& settings) override
		{
			owner.RecordContact(bodyA, bodyB, manifold, settings, true);
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
		{
			owner.RemoveContact(pair);
		}

	private:

		JoltWorldBackend& owner;

	};

	std::size_t JoltWorldBackend::ContactKeyHash::operator()(const ContactKey& key) const noexcept
	{
		std::uint64_t a = (static_cast<std::uint64_t>(key.BodyA) << 32u) | key.SubShapeA;
		std::uint64_t b = (static_cast<std::uint64_t>(key.BodyB) << 32u) | key.SubShapeB;
		a ^= a >> 33u;
		a *= 0xff51afd7ed558ccdULL;
		b ^= b >> 29u;
		b *= 0xc4ceb9fe1a85ec53ULL;
		return static_cast<std::size_t>(a ^ b ^ (a >> 32u) ^ (b >> 32u));
	}

	JoltWorldBackend::JoltWorldBackend(JPH::JobSystem& selectedJobSystem, PhysicsWorldDesc desc)
		: jobSystem(selectedJobSystem), worldDesc(desc)
	{}

	JoltWorldBackend::~JoltWorldBackend()
	{
		if (!initialized)
		{
			return;
		}

		if (simulating)
		{
			FetchResults(true);
		}

		FlushPendingDestroy();

		JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
		bodies.ForEach(
			[&](BodyHandle handle, BodyRecord& record)
			{
				(void)handle;
				if (!record.NativeBody.IsInvalid())
				{
					if (bodyInterface.IsAdded(record.NativeBody))
					{
						bodyInterface.RemoveBody(record.NativeBody);
					}
					bodyInterface.DestroyBody(record.NativeBody);
					record.NativeBody = {};
				}
			});
		bodies.Reset();
		shapes.Reset();
		materials.Reset();

		physicsSystem.SetContactListener(nullptr);
		contactCallback.reset();
		activeContacts.clear();
		collisionEvents.clear();
		triggerEvents.clear();
		initialized = false;
	}

	bool JoltWorldBackend::Initialize()
	{
		if (initialized)
		{
			return true;
		}

		if (!IsFiniteVec3(worldDesc.Gravity))
		{
			return false;
		}

		broadPhaseLayerInterface = std::make_unique<BroadPhaseLayerInterface>();
		objectVsBroadPhaseFilter = std::make_unique<ObjectVsBroadPhaseFilter>(*broadPhaseLayerInterface);
		objectPairFilter = std::make_unique<ObjectPairFilter>(*broadPhaseLayerInterface);
		contactCallback = std::make_unique<ContactCallback>(*this);

		constexpr JPH::uint MaxBodies = 65536;
		constexpr JPH::uint NumBodyMutexes = 0;
		constexpr JPH::uint MaxBodyPairs = 65536;
		constexpr JPH::uint MaxContactConstraints = 10240;

		physicsSystem.Init(
			MaxBodies,
			NumBodyMutexes,
			MaxBodyPairs,
			MaxContactConstraints,
			*broadPhaseLayerInterface,
			*objectVsBroadPhaseFilter,
			*objectPairFilter);
		physicsSystem.SetGravity(ToJolt(worldDesc.Gravity));
		physicsSystem.SetContactListener(contactCallback.get());
		initialized = true;
		return true;
	}

	PhysicsMaterialHandle JoltWorldBackend::CreateMaterial(const PhysicsMaterialDesc& desc)
	{
		if (!(desc.StaticFriction >= 0.0f) || !(desc.DynamicFriction >= 0.0f)
			|| !(desc.Restitution >= 0.0f && desc.Restitution <= 1.0f)
			|| !std::isfinite(desc.StaticFriction) || !std::isfinite(desc.DynamicFriction) || !std::isfinite(desc.Restitution))
		{
			return {};
		}

		return materials.Insert(desc);
	}

	void JoltWorldBackend::DestroyMaterial(PhysicsMaterialHandle material)
	{
		PhysicsMaterialDesc removed{};
		materials.Remove(material, removed);
	}

	bool JoltWorldBackend::IsMaterialValid(PhysicsMaterialHandle material) const
	{
		return materials.IsValid(material);
	}

	ShapeHandle JoltWorldBackend::CreateShape(const ShapeDesc& desc, PhysicsMaterialHandle material)
	{
		const PhysicsMaterialDesc* materialDesc = materials.Get(material);
		if (!materialDesc || !IsValidPose(desc.LocalPose))
		{
			return {};
		}

		JPH::RefConst<JPH::Shape> nativeShape = CreateNativeShape(desc);
		if (!nativeShape)
		{
			return {};
		}

		ShapeRecord record{};
		record.NativeShape = std::move(nativeShape);
		record.Material = material;
		record.MaterialDesc = *materialDesc;
		record.IsTrigger = desc.IsTrigger;
		return shapes.Insert(std::move(record));
	}

	void JoltWorldBackend::DestroyShape(ShapeHandle shape)
	{
		ShapeRecord removed{};
		shapes.Remove(shape, removed);
	}

	bool JoltWorldBackend::IsShapeValid(ShapeHandle shape) const
	{
		return shapes.IsValid(shape);
	}

	BodyHandle JoltWorldBackend::CreateBody(const BodyDesc& desc)
	{
		if (!initialized || simulating || !IsValidPose(desc.Pose))
		{
			return {};
		}

		const ShapeRecord* shapeRecord = shapes.Get(desc.Shape);
		if (!shapeRecord || !shapeRecord->NativeShape)
		{
			return {};
		}

		if (desc.Motion != MotionType::Static)
		{
			if (!(desc.Mass > 0.0f) || !std::isfinite(desc.Mass)
				|| !(desc.LinearDamping >= 0.0f) || !(desc.AngularDamping >= 0.0f)
				|| !std::isfinite(desc.LinearDamping) || !std::isfinite(desc.AngularDamping))
			{
				return {};
			}
		}

		if ((desc.HasInitialLinearVelocity && !IsFiniteVec3(desc.InitialLinearVelocity))
			|| (desc.HasInitialAngularVelocity && !IsFiniteVec3(desc.InitialAngularVelocity)))
		{
			return {};
		}

		const JPH::ObjectLayer objectLayer = broadPhaseLayerInterface->Register(desc.Motion, desc.Collision);
		if (objectLayer == JPH::cObjectLayerInvalid)
		{
			return {};
		}

		BodyRecord record{};
		record.Shape = desc.Shape;
		record.Collision = desc.Collision;
		record.Motion = desc.Motion;
		record.Mass = desc.Mass;
		record.UserData = desc.UserData;
		const BodyHandle handle = bodies.Insert(record);

		JPH::BodyCreationSettings settings(
			shapeRecord->NativeShape.GetPtr(),
			ToJoltPosition(desc.Pose.Position),
			ToJolt(desc.Pose.Rotation),
			ToJoltMotionType(desc.Motion),
			objectLayer);
		settings.mUserData = PackHandle(handle);
		settings.mIsSensor = shapeRecord->IsTrigger;
		settings.mCollideKinematicVsNonDynamic = desc.Motion == MotionType::Kinematic;
		settings.mGravityFactor = desc.UseGravity ? 1.0f : 0.0f;
		settings.mFriction = shapeRecord->MaterialDesc.DynamicFriction;
		settings.mRestitution = shapeRecord->MaterialDesc.Restitution;
		settings.mLinearDamping = desc.LinearDamping;
		settings.mAngularDamping = desc.AngularDamping;
		settings.mAllowSleeping = true;
		if (worldDesc.EnableContinuousCollisionDetection && desc.Motion == MotionType::Dynamic)
		{
			settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
		}
		if (desc.Motion == MotionType::Dynamic)
		{
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = desc.Mass;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
		JPH::Body* nativeBody = bodyInterface.CreateBody(settings);
		if (!nativeBody)
		{
			BodyRecord removed{};
			bodies.Remove(handle, removed);
			return {};
		}

		BodyRecord* insertedRecord = bodies.Get(handle);
		insertedRecord->NativeBody = nativeBody->GetID();

		const JPH::EActivation activation = desc.Motion != MotionType::Static && desc.StartAwake
			? JPH::EActivation::Activate
			: JPH::EActivation::DontActivate;
		bodyInterface.AddBody(nativeBody->GetID(), activation);

		if (desc.Motion == MotionType::Static)
		{
			nonMovingBodiesAdded = true;
		}

		if (desc.HasInitialLinearVelocity)
		{
			bodyInterface.SetLinearVelocity(nativeBody->GetID(), ToJolt(desc.InitialLinearVelocity));
		}
		if (desc.HasInitialAngularVelocity)
		{
			bodyInterface.SetAngularVelocity(nativeBody->GetID(), ToJolt(desc.InitialAngularVelocity));
		}
		if (desc.Motion != MotionType::Static && !desc.StartAwake && bodyInterface.IsActive(nativeBody->GetID()))
		{
			bodyInterface.DeactivateBody(nativeBody->GetID());
		}

		return handle;
	}

	void JoltWorldBackend::DestroyBody(BodyHandle body)
	{
		BodyRecord record{};
		if (!bodies.Remove(body, record) || record.NativeBody.IsInvalid())
		{
			return;
		}

		if (simulating)
		{
			pendingDestroy.push_back(record.NativeBody);
			return;
		}

		ReleaseBody(record.NativeBody);
	}

	bool JoltWorldBackend::IsBodyValid(BodyHandle body) const
	{
		return bodies.IsValid(body);
	}

	bool JoltWorldBackend::SetBodyPose(BodyHandle body, const PhysicsPose& pose, bool autowake)
	{
		if (simulating || !IsValidPose(pose))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		if (!record || record->NativeBody.IsInvalid())
		{
			return false;
		}

		const JPH::EActivation activation = record->Motion != MotionType::Static && autowake
			? JPH::EActivation::Activate
			: JPH::EActivation::DontActivate;
		physicsSystem.GetBodyInterface().SetPositionAndRotation(
			record->NativeBody,
			ToJoltPosition(pose.Position),
			ToJolt(pose.Rotation),
			activation);
		return true;
	}

	bool JoltWorldBackend::SetKinematicTarget(BodyHandle body, const PhysicsPose& pose)
	{
		if (simulating || !IsValidPose(pose))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		if (!record || record->Motion != MotionType::Kinematic || record->NativeBody.IsInvalid())
		{
			return false;
		}

		record->PendingKinematicTarget = pose;
		record->HasPendingKinematicTarget = true;
		return true;
	}

	bool JoltWorldBackend::GetBodyPose(BodyHandle body, PhysicsPose& pose) const
	{
		const BodyRecord* record = bodies.Get(body);
		if (!record || record->NativeBody.IsInvalid())
		{
			return false;
		}

		JPH::RVec3 position;
		JPH::Quat rotation;
		physicsSystem.GetBodyInterface().GetPositionAndRotation(record->NativeBody, position, rotation);
		pose = ToEngine(position, rotation);
		return true;
	}

	bool JoltWorldBackend::AddForce(BodyHandle body, const glm::vec3& force, ForceMode mode, bool autowake)
	{
		if (simulating || !IsFiniteVec3(force))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		if (!record || record->Motion != MotionType::Dynamic || record->NativeBody.IsInvalid())
		{
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
		const bool wasActive = bodyInterface.IsActive(record->NativeBody);
		const JPH::Vec3 value = ToJolt(force);
		const JPH::EActivation activation = autowake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;

		switch (mode)
		{
			case ForceMode::Force:
				bodyInterface.AddForce(record->NativeBody, value, activation);
				break;
			case ForceMode::Impulse:
				bodyInterface.AddImpulse(record->NativeBody, value);
				break;
			case ForceMode::VelocityChange:
				bodyInterface.AddLinearVelocity(record->NativeBody, value);
				break;
			case ForceMode::Acceleration:
				bodyInterface.AddForce(record->NativeBody, value * record->Mass, activation);
				break;
		}

		if (!autowake && !wasActive && bodyInterface.IsActive(record->NativeBody))
		{
			bodyInterface.DeactivateBody(record->NativeBody);
		}
		return true;
	}

	bool JoltWorldBackend::SetLinearVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake)
	{
		if (simulating || !IsFiniteVec3(velocity))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		if (!record || record->Motion == MotionType::Static || record->NativeBody.IsInvalid())
		{
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
		const bool wasActive = bodyInterface.IsActive(record->NativeBody);
		bodyInterface.SetLinearVelocity(record->NativeBody, ToJolt(velocity));
		if (!autowake && !wasActive && bodyInterface.IsActive(record->NativeBody))
		{
			bodyInterface.DeactivateBody(record->NativeBody);
		}
		return true;
	}

	bool JoltWorldBackend::SetAngularVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake)
	{
		if (simulating || !IsFiniteVec3(velocity))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		if (!record || record->Motion == MotionType::Static || record->NativeBody.IsInvalid())
		{
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
		const bool wasActive = bodyInterface.IsActive(record->NativeBody);
		bodyInterface.SetAngularVelocity(record->NativeBody, ToJolt(velocity));
		if (!autowake && !wasActive && bodyInterface.IsActive(record->NativeBody))
		{
			bodyInterface.DeactivateBody(record->NativeBody);
		}
		return true;
	}

	bool JoltWorldBackend::GetLinearVelocity(BodyHandle body, glm::vec3& velocity) const
	{
		const BodyRecord* record = bodies.Get(body);
		if (!record || record->Motion == MotionType::Static || record->NativeBody.IsInvalid())
		{
			return false;
		}

		velocity = ToGlm(physicsSystem.GetBodyInterface().GetLinearVelocity(record->NativeBody));
		return true;
	}

	bool JoltWorldBackend::GetAngularVelocity(BodyHandle body, glm::vec3& velocity) const
	{
		const BodyRecord* record = bodies.Get(body);
		if (!record || record->Motion == MotionType::Static || record->NativeBody.IsInvalid())
		{
			return false;
		}

		velocity = ToGlm(physicsSystem.GetBodyInterface().GetAngularVelocity(record->NativeBody));
		return true;
	}

	void JoltWorldBackend::BeginSimulation(float dt)
	{
		if (!initialized || simulating || !(dt > 0.0f) || !std::isfinite(dt))
		{
			return;
		}

		FlushPendingDestroy();
		{
			std::scoped_lock lock(eventMutex);
			collisionEvents.clear();
			triggerEvents.clear();
		}

		// Jolt's broadphase is only rebuilt on request. Doing it here, once per
		// batch of non-moving insertions, keeps level load balanced without
		// paying for it on frames that merely spawn dynamic bodies.
		if (nonMovingBodiesAdded)
		{
			physicsSystem.OptimizeBroadPhase();
			nonMovingBodiesAdded = false;
		}

		ApplyPendingKinematicTargets(dt);

		// JPH::PhysicsSystem::Update is synchronous: there is no kick/join split
		// to mirror PhysX's simulate()/fetchResults(). Stepping here rather than
		// in FetchResults keeps results ready the instant the step is issued, so
		// a caller polling with FetchResults(false) makes progress instead of
		// spinning forever. IsSimulationInFlight() still reports the same
		// Begin -> Fetch window, and the write guards still reject mutation
		// inside it, so the generic contract is unchanged.
		simulating = true;
		lastStepSucceeded = physicsSystem.Update(dt, 1, &tempAllocator, &jobSystem) == JPH::EPhysicsUpdateError::None;
	}

	bool JoltWorldBackend::FetchResults(bool block)
	{
		(void)block;

		if (!initialized || !simulating)
		{
			return true;
		}

		// Results are already available; blocking and non-blocking fetches are
		// therefore identical and both complete the step.
		simulating = false;
		FlushPendingDestroy();
		return lastStepSucceeded;
	}

	bool JoltWorldBackend::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter) const
	{
		if (!initialized || simulating || !IsFiniteVec3(origin) || !IsValidDirection(direction, maxDistance))
		{
			return false;
		}

		const glm::vec3 unitDirection = glm::normalize(direction);
		const JPH::RRayCast ray(ToJoltPosition(origin), ToJolt(unitDirection * maxDistance));
		JPH::RayCastResult result;
		QueryObjectLayerFilter objectLayerFilter(*broadPhaseLayerInterface, filter);
		QueryBodyFilter bodyFilter(*this, filter);
		if (!physicsSystem.GetNarrowPhaseQuery().CastRay(ray, result, {}, objectLayerFilter, bodyFilter))
		{
			return false;
		}

		hit.Body = ResolveBody(result.mBodyID);
		hit.Shape = ResolveShape(hit.Body);
		if (!hit.Body || !hit.Shape)
		{
			return false;
		}

		const JPH::RVec3 hitPosition = ray.GetPointOnRay(result.mFraction);
		hit.Position = ToGlm(hitPosition);
		hit.Distance = result.mFraction * maxDistance;
		hit.UserData = ResolveUserData(hit.Body);

		JPH::BodyLockRead bodyLock(physicsSystem.GetBodyLockInterface(), result.mBodyID);
		if (bodyLock.Succeeded())
		{
			hit.Normal = ToGlm(bodyLock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hitPosition));
		}
		return true;
	}

	bool JoltWorldBackend::Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance, SweepHit& hit, CollisionLayer filter) const
	{
		if (!initialized || simulating || !IsValidPose(pose) || !IsValidPose(shape.LocalPose) || !IsValidDirection(direction, maxDistance))
		{
			return false;
		}

		JPH::RefConst<JPH::Shape> nativeShape = CreateNativeShape(shape);
		if (!nativeShape)
		{
			return false;
		}

		const glm::vec3 unitDirection = glm::normalize(direction);
		const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
			nativeShape.GetPtr(),
			JPH::Vec3::sOne(),
			ToJoltTransform(pose),
			ToJolt(unitDirection * maxDistance));
		JPH::ShapeCastSettings settings;
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
		QueryObjectLayerFilter objectLayerFilter(*broadPhaseLayerInterface, filter);
		QueryBodyFilter bodyFilter(*this, filter);
		physicsSystem.GetNarrowPhaseQuery().CastShape(shapeCast, settings, JPH::RVec3::sZero(), collector, {}, objectLayerFilter, bodyFilter);
		if (!collector.HadHit())
		{
			return false;
		}

		const JPH::ShapeCastResult& result = collector.mHit;
		hit.Body = ResolveBody(result.mBodyID2);
		hit.Shape = ResolveShape(hit.Body);
		if (!hit.Body || !hit.Shape)
		{
			return false;
		}

		hit.Position = ToGlm(result.mContactPointOn2);
		hit.Normal = ToGlm(-result.mPenetrationAxis.Normalized());
		hit.Distance = std::max(0.0f, result.mFraction) * maxDistance;
		hit.UserData = ResolveUserData(hit.Body);
		return true;
	}

	std::size_t JoltWorldBackend::Overlap(const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter) const
	{
		if (!initialized || simulating || hits.empty() || !IsValidPose(pose) || !IsValidPose(shape.LocalPose))
		{
			return 0;
		}

		JPH::RefConst<JPH::Shape> nativeShape = CreateNativeShape(shape);
		if (!nativeShape)
		{
			return 0;
		}

		JPH::CollideShapeSettings settings;
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		QueryObjectLayerFilter objectLayerFilter(*broadPhaseLayerInterface, filter);
		QueryBodyFilter bodyFilter(*this, filter);
		const JPH::RMat44 centerOfMassTransform = ToJoltTransform(pose).PreTranslated(nativeShape->GetCenterOfMass());
		physicsSystem.GetNarrowPhaseQuery().CollideShape(
			nativeShape.GetPtr(),
			JPH::Vec3::sOne(),
			centerOfMassTransform,
			settings,
			JPH::RVec3::sZero(),
			collector,
			{},
			objectLayerFilter,
			bodyFilter);

		// One generic OverlapHit identifies a (body, shape) pair, but Jolt reports
		// one result per touching sub-shape. Collapse to the identity the generic
		// contract exposes so both backends report the same hit set.
		std::unordered_set<std::uint64_t> emittedPairs;
		std::size_t outputCount = 0;
		for (const JPH::CollideShapeResult& result : collector.mHits)
		{
			if (outputCount >= hits.size())
			{
				break;
			}

			const BodyHandle body = ResolveBody(result.mBodyID2);
			if (!body)
			{
				continue;
			}

			const ShapeHandle shape = ResolveShape(body);
			const std::uint64_t pairKey = (static_cast<std::uint64_t>(PackHandle(body)) << 1u)
				^ (static_cast<std::uint64_t>(shape.Index) << 33u)
				^ static_cast<std::uint64_t>(shape.Generation);
			if (!emittedPairs.insert(pairKey).second)
			{
				continue;
			}

			OverlapHit& output = hits[outputCount++];
			output.Body = body;
			output.Shape = shape;
			output.UserData = ResolveUserData(body);
		}
		return outputCount;
	}

	JPH::Vec3 JoltWorldBackend::ToJolt(const glm::vec3& value)
	{
		return JPH::Vec3(value.x, value.y, value.z);
	}

	JPH::RVec3 JoltWorldBackend::ToJoltPosition(const glm::vec3& value)
	{
		return JPH::RVec3(value.x, value.y, value.z);
	}

	glm::vec3 JoltWorldBackend::ToGlm(JPH::Vec3Arg value)
	{
		return glm::vec3(value.GetX(), value.GetY(), value.GetZ());
	}

#ifdef JPH_DOUBLE_PRECISION
	glm::vec3 JoltWorldBackend::ToGlm(JPH::RVec3Arg value)
	{
		return glm::vec3(
			static_cast<float>(value.GetX()),
			static_cast<float>(value.GetY()),
			static_cast<float>(value.GetZ()));
	}
#endif

	JPH::Quat JoltWorldBackend::ToJolt(const glm::quat& value)
	{
		const glm::quat normalized = glm::normalize(value);
		return JPH::Quat(normalized.x, normalized.y, normalized.z, normalized.w);
	}

	glm::quat JoltWorldBackend::ToGlm(JPH::QuatArg value)
	{
		return glm::quat(value.GetW(), value.GetX(), value.GetY(), value.GetZ());
	}

	JPH::RMat44 JoltWorldBackend::ToJoltTransform(const PhysicsPose& pose)
	{
		return JPH::RMat44::sRotationTranslation(ToJolt(pose.Rotation), ToJoltPosition(pose.Position));
	}

	PhysicsPose JoltWorldBackend::ToEngine(JPH::RVec3Arg position, JPH::QuatArg rotation)
	{
		PhysicsPose pose{};
		pose.Position = ToGlm(position);
		pose.Rotation = ToGlm(rotation);
		return pose;
	}

	std::uint64_t JoltWorldBackend::PackHandle(BodyHandle handle)
	{
		return (static_cast<std::uint64_t>(handle.Generation) << 32u)
			| static_cast<std::uint64_t>(handle.Index);
	}

	BodyHandle JoltWorldBackend::UnpackHandle(std::uint64_t packed)
	{
		BodyHandle handle{};
		handle.Index = static_cast<std::uint32_t>(packed & 0xffffffffu);
		handle.Generation = static_cast<std::uint32_t>(packed >> 32u);
		return handle;
	}

	JoltWorldBackend::ContactKey JoltWorldBackend::MakeContactKey(const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold)
	{
		return ContactKey{
			bodyA.GetID().GetIndexAndSequenceNumber(),
			manifold.mSubShapeID1.GetValue(),
			bodyB.GetID().GetIndexAndSequenceNumber(),
			manifold.mSubShapeID2.GetValue()
		};
	}

	JoltWorldBackend::ContactKey JoltWorldBackend::MakeContactKey(const JPH::SubShapeIDPair& pair)
	{
		return ContactKey{
			pair.GetBody1ID().GetIndexAndSequenceNumber(),
			pair.GetSubShapeID1().GetValue(),
			pair.GetBody2ID().GetIndexAndSequenceNumber(),
			pair.GetSubShapeID2().GetValue()
		};
	}

	JPH::RefConst<JPH::Shape> JoltWorldBackend::CreateNativeShape(const ShapeDesc& desc) const
	{
		JPH::RefConst<JPH::Shape> shape;

		switch (desc.Type)
		{
			case ShapeType::Box:
			{
				const glm::vec3 extents = desc.Box.HalfExtents;
				if (!IsFiniteVec3(extents) || !(extents.x > 0.0f) || !(extents.y > 0.0f) || !(extents.z > 0.0f))
				{
					return {};
				}
				shape = new JPH::BoxShape(ToJolt(extents));
				break;
			}
			case ShapeType::Sphere:
			{
				if (!(desc.Sphere.Radius > 0.0f) || !std::isfinite(desc.Sphere.Radius))
				{
					return {};
				}
				shape = new JPH::SphereShape(desc.Sphere.Radius);
				break;
			}
			case ShapeType::Capsule:
			{
				if (!(desc.Capsule.Radius > 0.0f) || !(desc.Capsule.HalfHeight >= 0.0f)
					|| !std::isfinite(desc.Capsule.Radius) || !std::isfinite(desc.Capsule.HalfHeight))
				{
					return {};
				}

				if (desc.Capsule.HalfHeight == 0.0f)
				{
					shape = new JPH::SphereShape(desc.Capsule.Radius);
				}
				else
				{
					shape = new JPH::CapsuleShape(desc.Capsule.HalfHeight, desc.Capsule.Radius);
				}
				break;
			}
			case ShapeType::ConvexMesh:
			case ShapeType::TriangleMesh:
			{
				// Collision mesh cooking is an offline asset-compiler checkpoint. Keep
				// Jolt aligned with PhysX and reject source/runtime mesh cooking here.
				return {};
			}
		}

		if (!shape)
		{
			return {};
		}

		const bool hasLocalTranslation = glm::dot(desc.LocalPose.Position, desc.LocalPose.Position) > 0.000001f;
		const glm::quat normalizedRotation = glm::normalize(desc.LocalPose.Rotation);
		const bool hasLocalRotation = std::abs(normalizedRotation.x) > 0.000001f
			|| std::abs(normalizedRotation.y) > 0.000001f
			|| std::abs(normalizedRotation.z) > 0.000001f
			|| std::abs(normalizedRotation.w - 1.0f) > 0.000001f;
		if (hasLocalTranslation || hasLocalRotation)
		{
			shape = new JPH::RotatedTranslatedShape(
				ToJolt(desc.LocalPose.Position),
				ToJolt(normalizedRotation),
				shape.GetPtr());
		}

		return shape;
	}

	BodyHandle JoltWorldBackend::ResolveBody(const JPH::Body& body) const
	{
		const BodyHandle handle = UnpackHandle(body.GetUserData());
		return bodies.IsValid(handle) ? handle : BodyHandle{};
	}

	BodyHandle JoltWorldBackend::ResolveBody(JPH::BodyID bodyId) const
	{
		if (bodyId.IsInvalid())
		{
			return {};
		}

		const BodyHandle handle = UnpackHandle(physicsSystem.GetBodyInterface().GetUserData(bodyId));
		return bodies.IsValid(handle) ? handle : BodyHandle{};
	}

	const JoltWorldBackend::BodyRecord* JoltWorldBackend::ResolveBodyRecord(const JPH::Body& body) const
	{
		return bodies.Get(UnpackHandle(body.GetUserData()));
	}

	ShapeHandle JoltWorldBackend::ResolveShape(BodyHandle body) const
	{
		const BodyRecord* record = bodies.Get(body);
		return record ? record->Shape : ShapeHandle{};
	}

	std::uint64_t JoltWorldBackend::ResolveUserData(BodyHandle body) const
	{
		const BodyRecord* record = bodies.Get(body);
		return record ? record->UserData : 0;
	}

	bool JoltWorldBackend::MatchesQuery(const JPH::Body& body, CollisionLayer filter) const
	{
		const BodyRecord* record = ResolveBodyRecord(body);
		return record && LayersMatch(filter, record->Collision);
	}

	void JoltWorldBackend::ApplyPendingKinematicTargets(float dt)
	{
		JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
		bodies.ForEach(
			[&](BodyHandle handle, BodyRecord& record)
			{
				(void)handle;
				if (record.Motion != MotionType::Kinematic || !record.HasPendingKinematicTarget || record.NativeBody.IsInvalid())
				{
					return;
				}

				bodyInterface.MoveKinematic(
					record.NativeBody,
					ToJoltPosition(record.PendingKinematicTarget.Position),
					ToJolt(record.PendingKinematicTarget.Rotation),
					dt);
				record.HasPendingKinematicTarget = false;
			});
	}

	void JoltWorldBackend::FlushPendingDestroy()
	{
		for (const JPH::BodyID bodyId : pendingDestroy)
		{
			ReleaseBody(bodyId);
		}
		pendingDestroy.clear();
	}

	void JoltWorldBackend::ReleaseBody(JPH::BodyID bodyId)
	{
		if (bodyId.IsInvalid())
		{
			return;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
		if (bodyInterface.IsAdded(bodyId))
		{
			bodyInterface.RemoveBody(bodyId);
		}
		bodyInterface.DestroyBody(bodyId);
	}

	void JoltWorldBackend::RecordContact(
		const JPH::Body& bodyA,
		const JPH::Body& bodyB,
		const JPH::ContactManifold& manifold,
		const JPH::ContactSettings& settings,
		bool persisted)
	{
		// A persisted contact that nobody will observe still costs a map write and
		// a lock on every worker thread, every step, for every touching pair. When
		// persisted reporting is off there is nothing left to do: the entry needed
		// for the eventual Ended/exit event was already stored when the contact
		// was added.
		if (persisted && !worldDesc.EnablePersistedCollisionEvents)
		{
			return;
		}

		const BodyHandle handleA = ResolveBody(bodyA);
		const BodyHandle handleB = ResolveBody(bodyB);
		if (!handleA || !handleB)
		{
			return;
		}

		const ShapeHandle shapeA = ResolveShape(handleA);
		const ShapeHandle shapeB = ResolveShape(handleB);
		const ShapeRecord* shapeRecordA = shapes.Get(shapeA);
		const ShapeRecord* shapeRecordB = shapes.Get(shapeB);
		if (!shapeRecordA || !shapeRecordB)
		{
			return;
		}

		ActiveContact active{};
		active.BodyA = handleA;
		active.BodyB = handleB;
		active.ShapeA = shapeA;
		active.ShapeB = shapeB;
		active.Position = manifold.mRelativeContactPointsOn1.empty()
			? glm::vec3(0.0f)
			: ToGlm(manifold.GetWorldSpaceContactPointOn1(0));
		active.Normal = ToGlm(manifold.mWorldSpaceNormal);
		active.IsTrigger = shapeRecordA->IsTrigger || shapeRecordB->IsTrigger;

		if (shapeRecordA->IsTrigger)
		{
			active.TriggerBody = handleA;
			active.OtherBody = handleB;
			active.TriggerShape = shapeA;
			active.OtherShape = shapeB;
		}
		else if (shapeRecordB->IsTrigger)
		{
			active.TriggerBody = handleB;
			active.OtherBody = handleA;
			active.TriggerShape = shapeB;
			active.OtherShape = shapeA;
		}

		// Jolt does not hand the solver's contact impulses to the listener, so the
		// generic CollisionEvent::Impulse is estimated from the manifold with the
		// helper Jolt provides for exactly this purpose. Triggers never resolve
		// contacts, so skip the work for them.
		float impulse = 0.0f;
		if (!active.IsTrigger)
		{
			JPH::CollisionEstimationResult estimate;
			JPH::EstimateCollisionResponse(
				bodyA,
				bodyB,
				manifold,
				estimate,
				settings.mCombinedFriction,
				settings.mCombinedRestitution);

			for (const float contactImpulse : estimate.mContactImpulse)
			{
				impulse += contactImpulse;
			}
		}

		const ContactKey key = MakeContactKey(bodyA, bodyB, manifold);
		std::scoped_lock lock(eventMutex);
		activeContacts[key] = active;

		if (active.IsTrigger)
		{
			if (!persisted && active.TriggerBody && active.OtherBody)
			{
				TriggerEvent event{};
				event.TriggerBody = active.TriggerBody;
				event.OtherBody = active.OtherBody;
				event.TriggerShape = active.TriggerShape;
				event.OtherShape = active.OtherShape;
				event.Entered = true;
				triggerEvents.push_back(event);
			}
			return;
		}

		CollisionEvent event{};
		event.Type = persisted ? CollisionEventType::Persisted : CollisionEventType::Started;
		event.BodyA = handleA;
		event.BodyB = handleB;
		event.ShapeA = shapeA;
		event.ShapeB = shapeB;
		event.Position = active.Position;
		event.Normal = active.Normal;
		event.Impulse = impulse;
		collisionEvents.push_back(event);
	}

	void JoltWorldBackend::RemoveContact(const JPH::SubShapeIDPair& pair)
	{
		const ContactKey key = MakeContactKey(pair);
		std::scoped_lock lock(eventMutex);
		const auto found = activeContacts.find(key);
		if (found == activeContacts.end())
		{
			return;
		}

		const ActiveContact active = found->second;
		activeContacts.erase(found);

		if (active.IsTrigger)
		{
			if (active.TriggerBody && active.OtherBody)
			{
				TriggerEvent event{};
				event.TriggerBody = active.TriggerBody;
				event.OtherBody = active.OtherBody;
				event.TriggerShape = active.TriggerShape;
				event.OtherShape = active.OtherShape;
				event.Entered = false;
				triggerEvents.push_back(event);
			}
			return;
		}

		CollisionEvent event{};
		event.Type = CollisionEventType::Ended;
		event.BodyA = active.BodyA;
		event.BodyB = active.BodyB;
		event.ShapeA = active.ShapeA;
		event.ShapeB = active.ShapeB;
		event.Position = active.Position;
		event.Normal = active.Normal;
		collisionEvents.push_back(event);
	}

}
