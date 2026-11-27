#include "Engine/Systems/Physics/Backends/Jolt/JoltWorldBackend.h"

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

// Scene queries (raycast, sweep, overlap), the Jolt conversions, native shapes, body
// resolution, deferred kinematic targets and destruction, and contact bookkeeping.
namespace Engine
{

	using namespace JoltPhysicsDetail;

	bool JoltWorldBackend::Raycast(
		const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter) const
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

	bool JoltWorldBackend::Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance,
		SweepHit& hit, CollisionLayer filter) const
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
			nativeShape.GetPtr(), JPH::Vec3::sOne(), ToJoltTransform(pose), ToJolt(unitDirection * maxDistance));
		JPH::ShapeCastSettings settings;
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
		QueryObjectLayerFilter objectLayerFilter(*broadPhaseLayerInterface, filter);
		QueryBodyFilter bodyFilter(*this, filter);
		physicsSystem.GetNarrowPhaseQuery().CastShape(
			shapeCast, settings, JPH::RVec3::sZero(), collector, {}, objectLayerFilter, bodyFilter);

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

	std::size_t JoltWorldBackend::Overlap(
		const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter) const
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
		physicsSystem.GetNarrowPhaseQuery().CollideShape(nativeShape.GetPtr(), JPH::Vec3::sOne(), centerOfMassTransform, settings,
			JPH::RVec3::sZero(), collector, {}, objectLayerFilter, bodyFilter);

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
			const std::uint64_t pairKey = (static_cast<std::uint64_t>(PackHandle(body)) << 1u) ^
				(static_cast<std::uint64_t>(shape.Index) << 33u) ^ static_cast<std::uint64_t>(shape.Generation);

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
		return glm::vec3(static_cast<float>(value.GetX()), static_cast<float>(value.GetY()), static_cast<float>(value.GetZ()));
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
		return (static_cast<std::uint64_t>(handle.Generation) << 32u) | static_cast<std::uint64_t>(handle.Index);
	}

	BodyHandle JoltWorldBackend::UnpackHandle(std::uint64_t packed)
	{
		BodyHandle handle{};
		handle.Index = static_cast<std::uint32_t>(packed & 0xffffffffu);
		handle.Generation = static_cast<std::uint32_t>(packed >> 32u);
		return handle;
	}

	JoltWorldBackend::ContactKey JoltWorldBackend::MakeContactKey(
		const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold)
	{
		return ContactKey{ bodyA.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID1.GetValue(),
			bodyB.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID2.GetValue() };
	}

	JoltWorldBackend::ContactKey JoltWorldBackend::MakeContactKey(const JPH::SubShapeIDPair& pair)
	{
		return ContactKey{ pair.GetBody1ID().GetIndexAndSequenceNumber(), pair.GetSubShapeID1().GetValue(),
			pair.GetBody2ID().GetIndexAndSequenceNumber(), pair.GetSubShapeID2().GetValue() };
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
			if (!(desc.Capsule.Radius > 0.0f) || !(desc.Capsule.HalfHeight >= 0.0f) || !std::isfinite(desc.Capsule.Radius) ||
				!std::isfinite(desc.Capsule.HalfHeight))
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
		const bool hasLocalRotation = std::abs(normalizedRotation.x) > 0.000001f || std::abs(normalizedRotation.y) > 0.000001f ||
			std::abs(normalizedRotation.z) > 0.000001f || std::abs(normalizedRotation.w - 1.0f) > 0.000001f;

		if (hasLocalTranslation || hasLocalRotation)
		{
			shape = new JPH::RotatedTranslatedShape(ToJolt(desc.LocalPose.Position), ToJolt(normalizedRotation), shape.GetPtr());
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

				bodyInterface.MoveKinematic(record.NativeBody, ToJoltPosition(record.PendingKinematicTarget.Position),
					ToJolt(record.PendingKinematicTarget.Rotation), dt);
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

	void JoltWorldBackend::RecordContact(const JPH::Body& bodyA, const JPH::Body& bodyB, const JPH::ContactManifold& manifold,
		const JPH::ContactSettings& settings, bool persisted)
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
		active.Position = manifold.mRelativeContactPointsOn1.empty() ? glm::vec3(0.0f) : ToGlm(manifold.GetWorldSpaceContactPointOn1(0));
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
			JPH::EstimateCollisionResponse(bodyA, bodyB, manifold, estimate, settings);

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

} // namespace Engine
