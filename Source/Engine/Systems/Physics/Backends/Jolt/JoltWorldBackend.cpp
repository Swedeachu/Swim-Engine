#include "JoltWorldBackend.h"

#include "Engine/Systems/Physics/Backends/Jolt/Callbacks/JoltContactCallback.h"
#include "Engine/Systems/Physics/Backends/Jolt/Filters/JoltBroadPhaseLayerInterface.h"
#include "Engine/Systems/Physics/Backends/Jolt/Filters/JoltObjectPairFilter.h"
#include "Engine/Systems/Physics/Backends/Jolt/Filters/JoltObjectVsBroadPhaseFilter.h"
#include "Engine/Systems/Physics/Backends/Jolt/Filters/JoltQueryBodyFilter.h"
#include "Engine/Systems/Physics/Backends/Jolt/Filters/JoltQueryObjectLayerFilter.h"
#include "Engine/Systems/Physics/Backends/Jolt/Internal/JoltPhysicsUtils.h"

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

	using namespace JoltPhysicsDetail;

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

	bool JoltWorldBackend::AddForceAtPosition(BodyHandle body, const glm::vec3& force, const glm::vec3& position, ForceMode mode, bool autowake)
	{
		if (simulating || !IsFiniteVec3(force) || !IsFiniteVec3(position) || (mode != ForceMode::Force && mode != ForceMode::Impulse))
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

		if (mode == ForceMode::Force)
		{
			bodyInterface.AddForce(record->NativeBody, ToJolt(force), ToJoltPosition(position),
				autowake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		}
		else
		{
			bodyInterface.AddImpulse(record->NativeBody, ToJolt(force), ToJoltPosition(position));
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

	bool JoltWorldBackend::GetMass(BodyHandle body, float& mass) const
	{
		const BodyRecord* record = bodies.Get(body);

		if (!record || record->Motion != MotionType::Dynamic || record->NativeBody.IsInvalid())
		{
			return false;
		}

		JPH::BodyLockRead lock(physicsSystem.GetBodyLockInterface(), record->NativeBody);

		if (!lock.Succeeded() || !lock.GetBody().GetMotionPropertiesUnchecked())
		{
			return false;
		}

		const float inverse = lock.GetBody().GetMotionPropertiesUnchecked()->GetInverseMassUnchecked();
		mass = inverse > 0.0f ? 1.0f / inverse : 0.0f;
		return inverse > 0.0f;
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

} // namespace Engine
