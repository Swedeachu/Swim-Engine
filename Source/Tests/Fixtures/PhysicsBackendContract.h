#pragma once

// The behavioural contract every Swim physics backend must satisfy.
//
// This header deliberately depends only on the generic physics API so a backend
// can be validated without leaking its implementation types into the test. The
// `PhysicsBackendContractCompile` header-boundary target compiles it against
// `Swim::Physics` alone to keep that true.
//
// Backends run the contract by registering one test per scenario:
//
//     SWIM_TEST("Physics.MyBackend", "WorldLifecycle")
//     {
//         auto backend = Engine::CreateMyBackend();
//         Engine::Tests::RunPhysicsWorldLifecycleContract(*backend);
//     }

#include "Engine/Systems/Physics/IPhysicsBackend.h"
#include "Engine/Systems/Physics/PhysicsWorld.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace Engine::Tests
{

	inline bool NearlyEqual(float a, float b, float epsilon = 0.05f)
	{
		return std::abs(a - b) <= epsilon;
	}

	inline void StepPhysics(PhysicsWorld& world, float dt)
	{
		world.BeginSimulation(dt);
		SWIM_REQUIRE(world.IsSimulationInFlight());
		SWIM_REQUIRE(world.FetchResults(true));
		SWIM_REQUIRE(!world.IsSimulationInFlight());
	}

	// The shared world every contract scenario starts from: a static floor, a
	// dynamic sphere above it, and a gravity-free kinematic capsule off to one
	// side. Each scenario builds its own instance so the scenarios stay
	// independent and can run in any order.
	class PhysicsContractWorld
	{

	public:

		explicit PhysicsContractWorld(IPhysicsBackend& backend, PhysicsWorldDesc desc = {})
		{
			SWIM_REQUIRE(backend.Initialize(1));

			std::unique_ptr<IPhysicsWorldBackend> worldBackend = backend.CreateWorld(desc);
			SWIM_REQUIRE(worldBackend != nullptr);
			world = std::make_unique<PhysicsWorld>(std::move(worldBackend));

			material = world->CreateMaterial(PhysicsMaterialDesc{});
			SWIM_REQUIRE(static_cast<bool>(material));
			SWIM_REQUIRE(world->IsMaterialValid(material));

			ShapeDesc floorShapeDesc{};
			floorShapeDesc.Type = ShapeType::Box;
			floorShapeDesc.Box.HalfExtents = glm::vec3(8.0f, 0.5f, 8.0f);
			floorShape = world->CreateShape(floorShapeDesc, material);
			SWIM_REQUIRE(static_cast<bool>(floorShape));
			SWIM_REQUIRE(world->IsShapeValid(floorShape));

			BodyDesc floorDesc{};
			floorDesc.Motion = MotionType::Static;
			floorDesc.Shape = floorShape;
			floorDesc.Pose.Position = glm::vec3(0.0f, -0.5f, 0.0f);
			floorDesc.Collision.Layer = 1u;
			floorDesc.Collision.Mask = 0xffffffffu;
			floorDesc.UserData = 101u;
			floor = world->CreateBody(floorDesc);
			SWIM_REQUIRE(static_cast<bool>(floor));
			SWIM_REQUIRE(world->IsBodyValid(floor));

			ShapeDesc sphereShapeDesc{};
			sphereShapeDesc.Type = ShapeType::Sphere;
			sphereShapeDesc.Sphere.Radius = 0.5f;
			sphereShape = world->CreateShape(sphereShapeDesc, material);
			SWIM_REQUIRE(static_cast<bool>(sphereShape));
			SWIM_REQUIRE(world->IsShapeValid(sphereShape));

			BodyDesc dynamicDesc{};
			dynamicDesc.Motion = MotionType::Dynamic;
			dynamicDesc.Shape = sphereShape;
			dynamicDesc.Pose.Position = glm::vec3(0.0f, 4.0f, 0.0f);
			dynamicDesc.Collision.Layer = 2u;
			dynamicDesc.Collision.Mask = 0xffffffffu;
			dynamicDesc.Mass = 2.0f;
			dynamicDesc.LinearDamping = 0.02f;
			dynamicDesc.AngularDamping = 0.03f;
			dynamicDesc.UserData = 202u;
			dynamicBody = world->CreateBody(dynamicDesc);
			SWIM_REQUIRE(static_cast<bool>(dynamicBody));
			SWIM_REQUIRE(world->IsBodyValid(dynamicBody));

			ShapeDesc capsuleShapeDesc{};
			capsuleShapeDesc.Type = ShapeType::Capsule;
			capsuleShapeDesc.Capsule.Radius = 0.3f;
			capsuleShapeDesc.Capsule.HalfHeight = 0.7f;
			capsuleShape = world->CreateShape(capsuleShapeDesc, material);
			SWIM_REQUIRE(static_cast<bool>(capsuleShape));
			SWIM_REQUIRE(world->IsShapeValid(capsuleShape));

			BodyDesc kinematicDesc{};
			kinematicDesc.Motion = MotionType::Kinematic;
			kinematicDesc.Shape = capsuleShape;
			kinematicDesc.Pose.Position = glm::vec3(-3.0f, 1.0f, 0.0f);
			kinematicDesc.Collision.Layer = 4u;
			kinematicDesc.Collision.Mask = 0xffffffffu;
			kinematicDesc.UseGravity = false;
			kinematicDesc.UserData = 303u;
			kinematicBody = world->CreateBody(kinematicDesc);
			SWIM_REQUIRE(static_cast<bool>(kinematicBody));
			SWIM_REQUIRE(world->IsBodyValid(kinematicBody));
		}

		PhysicsContractWorld(const PhysicsContractWorld&) = delete;

		PhysicsContractWorld& operator=(const PhysicsContractWorld&) = delete;

		PhysicsWorld& Get()
		{
			return *world;
		}

		PhysicsMaterialHandle Material() const { return material; }

		ShapeHandle FloorShape() const { return floorShape; }

		ShapeHandle SphereShape() const { return sphereShape; }

		ShapeHandle CapsuleShape() const { return capsuleShape; }

		BodyHandle Floor() const { return floor; }

		BodyHandle Dynamic() const { return dynamicBody; }

		BodyHandle Kinematic() const { return kinematicBody; }

	private:

		std::unique_ptr<PhysicsWorld> world;
		PhysicsMaterialHandle material{};
		ShapeHandle floorShape{};
		ShapeHandle sphereShape{};
		ShapeHandle capsuleShape{};
		BodyHandle floor{};
		BodyHandle dynamicBody{};
		BodyHandle kinematicBody{};

	};

	// Creation, kinematic targeting, handle validity, and destruction/reuse.
	inline void RunPhysicsWorldLifecycleContract(IPhysicsBackend& backend)
	{
		PhysicsContractWorld fixture(backend);
		PhysicsWorld& world = fixture.Get();

		PhysicsPose kinematicTarget{};
		kinematicTarget.Position = glm::vec3(-2.0f, 1.0f, 0.0f);
		SWIM_REQUIRE(world.SetKinematicTarget(fixture.Kinematic(), kinematicTarget));
		StepPhysics(world, 1.0f / 60.0f);

		PhysicsPose kinematicPose{};
		SWIM_REQUIRE(world.GetBodyPose(fixture.Kinematic(), kinematicPose));
		SWIM_CHECK_NEAR(kinematicPose.Position.x, -2.0f, 0.05f);

		// A body destroyed while a simulation is in flight must invalidate its
		// handle immediately and only release the native actor once results are
		// fetched.
		ShapeDesc deferredShapeDesc{};
		deferredShapeDesc.Type = ShapeType::Sphere;
		deferredShapeDesc.Sphere.Radius = 0.2f;
		const ShapeHandle deferredShape = world.CreateShape(deferredShapeDesc, fixture.Material());
		SWIM_REQUIRE(deferredShape.IsValid());

		BodyDesc deferredDesc{};
		deferredDesc.Motion = MotionType::Dynamic;
		deferredDesc.Shape = deferredShape;
		deferredDesc.Pose.Position = glm::vec3(0.0f, 6.0f, 3.0f);
		deferredDesc.UseGravity = false;
		const BodyHandle staleBody = world.CreateBody(deferredDesc);
		SWIM_REQUIRE(staleBody.IsValid());

		world.BeginSimulation(1.0f / 60.0f);
		SWIM_REQUIRE(world.IsSimulationInFlight());
		world.DestroyBody(staleBody);
		SWIM_CHECK(!world.IsBodyValid(staleBody));
		SWIM_REQUIRE(world.FetchResults(true));

		const BodyHandle replacementBody = world.CreateBody(deferredDesc);
		SWIM_CHECK(static_cast<bool>(replacementBody));
		SWIM_CHECK(replacementBody != staleBody);
		SWIM_CHECK(!world.IsBodyValid(staleBody));
		world.DestroyBody(replacementBody);
		world.DestroyShape(deferredShape);

		world.DestroyBody(fixture.Kinematic());
		world.DestroyShape(fixture.CapsuleShape());
		world.DestroyBody(fixture.Dynamic());
		world.DestroyShape(fixture.SphereShape());
		world.DestroyBody(fixture.Floor());
		world.DestroyShape(fixture.FloorShape());
		world.DestroyMaterial(fixture.Material());

		SWIM_CHECK(!world.IsBodyValid(fixture.Dynamic()));
		SWIM_CHECK(!world.IsShapeValid(fixture.SphereShape()));
		SWIM_CHECK(!world.IsMaterialValid(fixture.Material()));
	}

	// Raycast, sweep, and overlap queries, including layer filtering and generic
	// hit identity.
	inline void RunPhysicsSceneQueryContract(IPhysicsBackend& backend)
	{
		PhysicsContractWorld fixture(backend);
		PhysicsWorld& world = fixture.Get();

		CollisionLayer sphereOnlyQuery{};
		sphereOnlyQuery.Layer = 1u;
		sphereOnlyQuery.Mask = 2u;

		RaycastHit rayHit{};
		SWIM_REQUIRE(world.Raycast(
			glm::vec3(0.0f, 10.0f, 0.0f),
			glm::vec3(0.0f, -1.0f, 0.0f),
			20.0f,
			rayHit,
			sphereOnlyQuery));
		SWIM_CHECK(rayHit.Body == fixture.Dynamic());
		SWIM_CHECK(rayHit.Shape == fixture.SphereShape());
		SWIM_CHECK_EQUAL(rayHit.UserData, 202u);

		ShapeDesc sweepShape{};
		sweepShape.Type = ShapeType::Sphere;
		sweepShape.Sphere.Radius = 0.25f;
		PhysicsPose sweepPose{};
		sweepPose.Position = glm::vec3(3.0f, 4.0f, 0.0f);

		CollisionLayer floorOnlyQuery{};
		floorOnlyQuery.Layer = 1u;
		floorOnlyQuery.Mask = 1u;

		SweepHit sweepHit{};
		SWIM_REQUIRE(world.Sweep(
			sweepShape,
			sweepPose,
			glm::vec3(0.0f, -1.0f, 0.0f),
			10.0f,
			sweepHit,
			floorOnlyQuery));
		SWIM_CHECK(sweepHit.Body == fixture.Floor());
		SWIM_CHECK(sweepHit.Shape == fixture.FloorShape());
		SWIM_CHECK_EQUAL(sweepHit.UserData, 101u);

		ShapeDesc overlapShape{};
		overlapShape.Type = ShapeType::Sphere;
		overlapShape.Sphere.Radius = 0.75f;
		PhysicsPose overlapPose{};
		overlapPose.Position = glm::vec3(0.0f, 4.0f, 0.0f);

		OverlapHit overlapHits[8]{};
		const std::size_t overlapCount = world.Overlap(overlapShape, overlapPose, overlapHits, sphereOnlyQuery);
		SWIM_REQUIRE(overlapCount > 0);

		const bool foundDynamicOverlap = std::any_of(overlapHits, overlapHits + overlapCount,
			[&](const OverlapHit& overlapHit)
			{
				return overlapHit.Body == fixture.Dynamic()
					&& overlapHit.Shape == fixture.SphereShape()
					&& overlapHit.UserData == 202u;
			});
		SWIM_CHECK(foundDynamicOverlap);

		// Query-shape local poses are part of the generic contract. This also
		// catches backends that accidentally pass a body/world transform where
		// their native overlap API expects the query shape's center-of-mass pose.
		ShapeDesc offsetOverlapShape{};
		offsetOverlapShape.Type = ShapeType::Sphere;
		offsetOverlapShape.Sphere.Radius = 0.25f;
		offsetOverlapShape.LocalPose.Position = glm::vec3(1.0f, 0.0f, 0.0f);
		PhysicsPose offsetOverlapPose{};
		offsetOverlapPose.Position = glm::vec3(-1.0f, 4.0f, 0.0f);

		OverlapHit offsetOverlapHits[8]{};
		const std::size_t offsetOverlapCount = world.Overlap(
			offsetOverlapShape,
			offsetOverlapPose,
			offsetOverlapHits,
			sphereOnlyQuery);
		SWIM_REQUIRE(offsetOverlapCount > 0);

		const bool foundOffsetDynamicOverlap = std::any_of(offsetOverlapHits, offsetOverlapHits + offsetOverlapCount,
			[&](const OverlapHit& overlapHit)
			{
				return overlapHit.Body == fixture.Dynamic()
					&& overlapHit.Shape == fixture.SphereShape()
					&& overlapHit.UserData == 202u;
			});
		SWIM_CHECK(foundOffsetDynamicOverlap);
	}

	// Gravity integration, collision events, and velocity/force application.
	inline void RunPhysicsSimulationContract(IPhysicsBackend& backend)
	{
		PhysicsContractWorld fixture(backend);
		PhysicsWorld& world = fixture.Get();

		PhysicsPose beforeGravity{};
		SWIM_REQUIRE(world.GetBodyPose(fixture.Dynamic(), beforeGravity));

		bool sawCollisionStart = false;

		for (int i = 0; i < 120 && !sawCollisionStart; ++i)
		{
			StepPhysics(world, 1.0f / 60.0f);

			for (const CollisionEvent& event : world.GetCollisionEvents())
			{
				const bool expectedPair = (event.BodyA == fixture.Floor() && event.BodyB == fixture.Dynamic())
					|| (event.BodyA == fixture.Dynamic() && event.BodyB == fixture.Floor());

				if (expectedPair && event.Type == CollisionEventType::Started)
				{
					sawCollisionStart = true;
				}
			}
		}

		SWIM_CHECK(sawCollisionStart);

		PhysicsPose afterGravity{};
		SWIM_REQUIRE(world.GetBodyPose(fixture.Dynamic(), afterGravity));
		SWIM_CHECK(afterGravity.Position.y < beforeGravity.Position.y);

		SWIM_REQUIRE(world.SetLinearVelocity(fixture.Dynamic(), glm::vec3(0.0f)));
		SWIM_REQUIRE(world.AddForce(fixture.Dynamic(), glm::vec3(0.0f, 4.0f, 0.0f), ForceMode::Impulse));

		// Applied forces and impulses are accumulated by the backend and
		// integrated during the next simulation step, so the velocity readback
		// only reflects them after stepping the world.
		StepPhysics(world, 1.0f / 60.0f);

		glm::vec3 linearVelocity{};
		SWIM_REQUIRE(world.GetLinearVelocity(fixture.Dynamic(), linearVelocity));
		SWIM_CHECK(linearVelocity.y > 0.5f);
	}

	// Trigger shapes report enter and exit for a body passing through them.
	inline void RunPhysicsTriggerContract(IPhysicsBackend& backend)
	{
		PhysicsContractWorld fixture(backend);
		PhysicsWorld& world = fixture.Get();

		ShapeDesc triggerShapeDesc{};
		triggerShapeDesc.Type = ShapeType::Box;
		triggerShapeDesc.Box.HalfExtents = glm::vec3(0.75f, 0.75f, 0.75f);
		triggerShapeDesc.IsTrigger = true;
		const ShapeHandle triggerShape = world.CreateShape(triggerShapeDesc, fixture.Material());
		SWIM_REQUIRE(triggerShape.IsValid());

		BodyDesc triggerDesc{};
		triggerDesc.Motion = MotionType::Static;
		triggerDesc.Shape = triggerShape;
		triggerDesc.Pose.Position = glm::vec3(8.0f, 2.0f, 0.0f);
		triggerDesc.Collision.Layer = 8u;
		triggerDesc.Collision.Mask = 0xffffffffu;
		triggerDesc.UserData = 404u;
		const BodyHandle triggerBody = world.CreateBody(triggerDesc);
		SWIM_REQUIRE(triggerBody.IsValid());

		ShapeDesc triggerMoverShapeDesc{};
		triggerMoverShapeDesc.Type = ShapeType::Sphere;
		triggerMoverShapeDesc.Sphere.Radius = 0.25f;
		const ShapeHandle triggerMoverShape = world.CreateShape(triggerMoverShapeDesc, fixture.Material());
		SWIM_REQUIRE(triggerMoverShape.IsValid());

		BodyDesc triggerMoverDesc{};
		triggerMoverDesc.Motion = MotionType::Dynamic;
		triggerMoverDesc.Shape = triggerMoverShape;
		triggerMoverDesc.Pose.Position = glm::vec3(5.5f, 2.0f, 0.0f);
		triggerMoverDesc.Collision.Layer = 16u;
		triggerMoverDesc.Collision.Mask = 0xffffffffu;
		triggerMoverDesc.UseGravity = false;
		triggerMoverDesc.HasInitialLinearVelocity = true;
		triggerMoverDesc.InitialLinearVelocity = glm::vec3(4.0f, 0.0f, 0.0f);
		triggerMoverDesc.UserData = 505u;
		const BodyHandle triggerMover = world.CreateBody(triggerMoverDesc);
		SWIM_REQUIRE(triggerMover.IsValid());

		bool sawTriggerEnter = false;
		bool sawTriggerExit = false;

		for (int i = 0; i < 120 && !sawTriggerExit; ++i)
		{
			StepPhysics(world, 1.0f / 60.0f);

			for (const TriggerEvent& event : world.GetTriggerEvents())
			{
				if (event.TriggerBody == triggerBody && event.OtherBody == triggerMover)
				{
					sawTriggerEnter = sawTriggerEnter || event.Entered;
					sawTriggerExit = sawTriggerExit || !event.Entered;
				}
			}
		}

		SWIM_CHECK(sawTriggerEnter);
		SWIM_CHECK(sawTriggerExit);

		world.DestroyBody(triggerMover);
		world.DestroyShape(triggerMoverShape);
		world.DestroyBody(triggerBody);
		world.DestroyShape(triggerShape);
	}

	// A ShapeHandle is a reusable template. Collision filtering is per-body state
	// in the generic API, so two bodies built from one handle must be filtered
	// independently and must not disturb each other.
	inline void RunPhysicsSharedShapeContract(IPhysicsBackend& backend)
	{
		PhysicsContractWorld fixture(backend);
		PhysicsWorld& world = fixture.Get();

		ShapeDesc sharedShapeDesc{};
		sharedShapeDesc.Type = ShapeType::Sphere;
		sharedShapeDesc.Sphere.Radius = 0.4f;
		const ShapeHandle sharedShape = world.CreateShape(sharedShapeDesc, fixture.Material());
		SWIM_REQUIRE(sharedShape.IsValid());

		BodyDesc leftDesc{};
		leftDesc.Motion = MotionType::Static;
		leftDesc.Shape = sharedShape;
		leftDesc.Pose.Position = glm::vec3(-6.0f, 3.0f, 0.0f);
		leftDesc.Collision.Layer = 64u;
		leftDesc.Collision.Mask = 0xffffffffu;
		leftDesc.UserData = 606u;
		const BodyHandle left = world.CreateBody(leftDesc);
		SWIM_REQUIRE(left.IsValid());

		BodyDesc rightDesc = leftDesc;
		rightDesc.Pose.Position = glm::vec3(6.0f, 3.0f, 0.0f);
		rightDesc.Collision.Layer = 128u;
		rightDesc.UserData = 707u;
		const BodyHandle right = world.CreateBody(rightDesc);
		SWIM_REQUIRE(right.IsValid());

		// Both bodies must still exist and both must resolve back to the one
		// shared shape handle.
		SWIM_CHECK(world.IsBodyValid(left));
		SWIM_CHECK(world.IsBodyValid(right));

		CollisionLayer leftOnly{};
		leftOnly.Layer = 1u;
		leftOnly.Mask = 64u;

		RaycastHit leftHit{};
		SWIM_REQUIRE(world.Raycast(glm::vec3(-6.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 20.0f, leftHit, leftOnly));
		SWIM_CHECK(leftHit.Body == left);
		SWIM_CHECK(leftHit.Shape == sharedShape);
		SWIM_CHECK_EQUAL(leftHit.UserData, 606u);

		// Creating the second body must not have re-filtered the first: a query
		// restricted to the left body's layer must still miss the right body.
		RaycastHit strayHit{};
		SWIM_CHECK(!world.Raycast(glm::vec3(6.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 20.0f, strayHit, leftOnly));

		CollisionLayer rightOnly{};
		rightOnly.Layer = 1u;
		rightOnly.Mask = 128u;

		RaycastHit rightHit{};
		SWIM_REQUIRE(world.Raycast(glm::vec3(6.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 20.0f, rightHit, rightOnly));
		SWIM_CHECK(rightHit.Body == right);
		SWIM_CHECK(rightHit.Shape == sharedShape);
		SWIM_CHECK_EQUAL(rightHit.UserData, 707u);

		// Destroying one body must leave the other and the shared template intact.
		world.DestroyBody(right);
		SWIM_CHECK(!world.IsBodyValid(right));
		SWIM_CHECK(world.IsBodyValid(left));
		SWIM_CHECK(world.IsShapeValid(sharedShape));

		RaycastHit survivingHit{};
		SWIM_CHECK(world.Raycast(glm::vec3(-6.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 20.0f, survivingHit, leftOnly));

		world.DestroyBody(left);
		world.DestroyShape(sharedShape);
	}

	// Mutating a body while a step is in flight is invalid for every backend, so
	// the generic API must reject it rather than letting the implementation raise
	// its own error. A non-blocking fetch must also make progress.
	inline void RunPhysicsInFlightWriteContract(IPhysicsBackend& backend)
	{
		PhysicsContractWorld fixture(backend);
		PhysicsWorld& world = fixture.Get();

		PhysicsPose beforePose{};
		SWIM_REQUIRE(world.GetBodyPose(fixture.Dynamic(), beforePose));

		world.BeginSimulation(1.0f / 60.0f);
		SWIM_REQUIRE(world.IsSimulationInFlight());

		PhysicsPose rejectedPose{};
		rejectedPose.Position = glm::vec3(0.0f, 100.0f, 0.0f);
		SWIM_CHECK(!world.SetBodyPose(fixture.Dynamic(), rejectedPose));
		SWIM_CHECK(!world.SetKinematicTarget(fixture.Kinematic(), rejectedPose));
		SWIM_CHECK(!world.AddForce(fixture.Dynamic(), glm::vec3(0.0f, 1.0f, 0.0f), ForceMode::Impulse));
		SWIM_CHECK(!world.SetLinearVelocity(fixture.Dynamic(), glm::vec3(0.0f, 1.0f, 0.0f)));
		SWIM_CHECK(!world.SetAngularVelocity(fixture.Dynamic(), glm::vec3(0.0f, 1.0f, 0.0f)));

		BodyDesc rejectedBody{};
		rejectedBody.Motion = MotionType::Static;
		rejectedBody.Shape = fixture.SphereShape();
		SWIM_CHECK(!world.CreateBody(rejectedBody));

		// Reads stay legal while a step is in flight.
		PhysicsPose duringPose{};
		SWIM_CHECK(world.GetBodyPose(fixture.Dynamic(), duringPose));

		// A non-blocking fetch must eventually complete the step rather than
		// spinning forever, so a bounded poll loop has to terminate.
		bool completed = false;

		for (int attempt = 0; attempt < 4096 && !completed; ++attempt)
		{
			completed = world.FetchResults(false);
		}

		SWIM_CHECK(completed);
		SWIM_CHECK(!world.IsSimulationInFlight());

		// Once the step is done the same writes are accepted again.
		SWIM_CHECK(world.SetLinearVelocity(fixture.Dynamic(), glm::vec3(0.0f)));
	}

	// Collision events must carry a usable contact impulse, and persisted contact
	// reporting must be opt-in.
	inline void RunPhysicsContactEventContract(IPhysicsBackend& backend)
	{
		PhysicsWorldDesc quietDesc{};
		{
			PhysicsContractWorld quiet(backend, quietDesc);
			PhysicsWorld& world = quiet.Get();

			bool sawPersisted = false;
			bool sawStarted = false;

			for (int i = 0; i < 240; ++i)
			{
				StepPhysics(world, 1.0f / 60.0f);

				for (const CollisionEvent& event : world.GetCollisionEvents())
				{
					sawStarted = sawStarted || event.Type == CollisionEventType::Started;
					sawPersisted = sawPersisted || event.Type == CollisionEventType::Persisted;
				}
			}

			SWIM_CHECK(sawStarted);
			SWIM_CHECK(!sawPersisted);
		}

		PhysicsWorldDesc verboseDesc{};
		verboseDesc.EnablePersistedCollisionEvents = true;

		PhysicsContractWorld verbose(backend, verboseDesc);
		PhysicsWorld& world = verbose.Get();

		bool sawPersisted = false;
		float strongestImpulse = 0.0f;

		for (int i = 0; i < 240; ++i)
		{
			StepPhysics(world, 1.0f / 60.0f);

			for (const CollisionEvent& event : world.GetCollisionEvents())
			{
				sawPersisted = sawPersisted || event.Type == CollisionEventType::Persisted;
				strongestImpulse = std::max(strongestImpulse, event.Impulse);
			}
		}

		SWIM_CHECK(sawPersisted);

		// A 2 kg sphere falling onto a floor cannot resolve with a zero impulse.
		SWIM_CHECK(strongestImpulse > 0.0f);

		// Destroying a body that is resting in contact: the backend still learns about the
		// lost touch on the next steps (PhysX reports it with the released actor flagged as
		// removed), which must neither crash nor produce events for the dead body.
		const BodyHandle removed = verbose.Dynamic();
		world.DestroyBody(removed);
		SWIM_CHECK(!world.IsBodyValid(removed));

		for (int i = 0; i < 10; ++i)
		{
			StepPhysics(world, 1.0f / 60.0f);

			for (const CollisionEvent& event : world.GetCollisionEvents())
			{
				SWIM_CHECK(event.BodyA != removed && event.BodyB != removed);
			}
		}
	}

	// A dynamic box above water (surface y = 5 over the contract floor, top y = 0).
	inline BodyHandle CreateFloatingBox(PhysicsWorld& world, PhysicsMaterialHandle material, const glm::vec3& halfExtents, float mass,
		const PhysicsPose& pose)
	{
		ShapeDesc shapeDesc{};
		shapeDesc.Type = ShapeType::Box;
		shapeDesc.Box.HalfExtents = halfExtents;
		const ShapeHandle shape = world.CreateShape(shapeDesc, material);
		SWIM_REQUIRE(static_cast<bool>(shape));
		BodyDesc desc{};
		desc.Shape = shape;
		desc.Pose = pose;
		desc.Mass = mass;
		desc.Collision.Layer = 8u;
		const BodyHandle body = world.CreateBody(desc);
		SWIM_REQUIRE(static_cast<bool>(body));
		SWIM_REQUIRE(world.SetBuoyancy(body, BuoyancyDesc::Box(halfExtents)));
		return body;
	}

	inline FluidVolumeDesc ContractWater()
	{
		FluidVolumeDesc water;
		water.SurfaceHeight = 5.0f;
		water.BoundsMin = glm::vec3(-8.0f, 0.0f, -8.0f);
		water.BoundsMax = glm::vec3(8.0f, 20.0f, 8.0f);
		return water;
	}

	inline float BodyHeight(PhysicsWorld& world, BodyHandle body)
	{
		PhysicsPose pose;
		SWIM_REQUIRE(world.GetBodyPose(body, pose));
		return pose.Position.y;
	}

	// Buoyancy (PhysicsWorld, above the backend): bodies float where they displace their own
	// weight, sink when denser than the fluid, right themselves, are slowed by drag, take
	// thrown impulses, and nothing happens outside the volume.
	inline void RunPhysicsBuoyancyContract(IPhysicsBackend& backend)
	{
		constexpr float Dt = 1.0f / 60.0f;
		PhysicsContractWorld contract(backend);
		PhysicsWorld& world = contract.Get();
		const auto water = world.AddFluidVolume(ContractWater());
		SWIM_REQUIRE(static_cast<bool>(water));
		const glm::vec3 cube(0.5f);
		// 1 m^3 cubes: half as dense as water, a quarter, and half again denser.
		const BodyHandle half = CreateFloatingBox(world, contract.Material(), cube, 500.0f, { glm::vec3(4.0f, 7.0f, 4.0f) });
		const BodyHandle quarter = CreateFloatingBox(world, contract.Material(), cube, 250.0f, { glm::vec3(-4.0f, 6.0f, 4.0f) });
		const BodyHandle heavy = CreateFloatingBox(world, contract.Material(), cube, 1500.0f, { glm::vec3(4.0f, 6.0f, -4.0f) });
		// Outside the volume: falls as if there were no water.
		const BodyHandle dry = CreateFloatingBox(world, contract.Material(), cube, 500.0f, { glm::vec3(20.0f, 7.0f, 0.0f) });
		// A plank, tipped 40 degrees: it ends face up.
		const glm::quat tilt = glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		const BodyHandle plank =
			CreateFloatingBox(world, contract.Material(), glm::vec3(1.0f, 0.15f, 0.5f), 150.0f, { glm::vec3(-4.0f, 5.5f, -4.0f), tilt });
		SWIM_CHECK(!world.SetBuoyancy(dry, BuoyancyDesc{ 0.0f }));
		SWIM_CHECK(!world.SetBuoyancy(BodyHandle{}, BuoyancyDesc::Box(cube)));

		for (int i = 0; i < 30; ++i)
		{
			StepPhysics(world, Dt);
		}

		SWIM_CHECK(world.GetBuoyancyState(dry)->Submerged == 0.0f && world.GetBuoyancyState(dry)->Volume == -1);
		SWIM_CHECK(world.GetBuoyancyState(half)->Volume == 0);

		for (int i = 0; i < 600; ++i)
		{
			StepPhysics(world, Dt);
		}

		SWIM_CHECK(NearlyEqual(BodyHeight(world, half), 5.0f, 0.05f));
		SWIM_CHECK(NearlyEqual(world.GetBuoyancyState(half)->Submerged, 0.5f, 0.05f));
		SWIM_CHECK(NearlyEqual(world.GetBuoyancyState(half)->Force.y, 500.0f * 9.81f, 400.0f));
		// A quarter as dense: a quarter under, and settled as quickly (drag scales with density).
		SWIM_CHECK(NearlyEqual(BodyHeight(world, quarter), 5.25f, 0.05f));
		SWIM_CHECK(NearlyEqual(world.GetBuoyancyState(quarter)->Submerged, 0.25f, 0.03f));
		SWIM_CHECK(NearlyEqual(BodyHeight(world, heavy), 0.5f, 0.05f)); // On the floor.
		SWIM_CHECK(BodyHeight(world, dry) < -10.0f);					  // Fell past the floor's edge.
		PhysicsPose plankPose;
		SWIM_REQUIRE(world.GetBodyPose(plank, plankPose));
		SWIM_CHECK(std::abs((plankPose.Rotation * glm::vec3(0.0f, 1.0f, 0.0f)).y) > 0.98f);

		// Thrown: an off-centre impulse sets it moving and spinning; the water slows both.
		SWIM_REQUIRE(world.AddForceAtPosition(half, glm::vec3(2000.0f, 0.0f, 0.0f), glm::vec3(4.0f, 5.0f, 4.5f), ForceMode::Impulse));
		SWIM_CHECK(!world.AddForceAtPosition(half, glm::vec3(1.0f), glm::vec3(0.0f), ForceMode::VelocityChange));
		StepPhysics(world, Dt);
		glm::vec3 velocity(0.0f), spin(0.0f);
		SWIM_REQUIRE(world.GetLinearVelocity(half, velocity) && world.GetAngularVelocity(half, spin));
		const float thrown = velocity.x;
		const float thrownSpin = glm::length(spin);
		SWIM_CHECK(thrown > 3.0f && thrownSpin > 1.0f);

		for (int i = 0; i < 180; ++i)
		{
			StepPhysics(world, Dt);
		}

		SWIM_REQUIRE(world.GetLinearVelocity(half, velocity) && world.GetAngularVelocity(half, spin));
		SWIM_CHECK(std::abs(velocity.x) < 0.35f * thrown && glm::length(spin) < 0.35f * thrownSpin);

		world.ClearBuoyancy(quarter);
		SWIM_CHECK(world.GetBuoyancyState(quarter) == nullptr);
		world.DestroyBody(half);
		SWIM_CHECK(world.GetBuoyancyState(half) == nullptr);
		world.RemoveFluidVolume(water);
		StepPhysics(world, Dt);
		SWIM_CHECK(world.GetBuoyancyState(plank)->Volume == -1);
	}

	inline float BoxTop(PhysicsWorld& world, BodyHandle body, const glm::vec3& halfExtents)
	{
		PhysicsPose pose;
		SWIM_REQUIRE(world.GetBodyPose(body, pose));
		const glm::mat3 rotation = glm::mat3_cast(pose.Rotation);
		return pose.Position.y + std::abs(rotation[0].y) * halfExtents.x + std::abs(rotation[1].y) * halfExtents.y +
			std::abs(rotation[2].y) * halfExtents.z;
	}

	// Floaters: a set density floats a body whatever its mass, and NeverSink keeps the top of a
	// body that would sink at the surface, flat or on waves, even when dropped in from above.
	inline void RunPhysicsFloaterContract(IPhysicsBackend& backend)
	{
		constexpr float Dt = 1.0f / 60.0f;
		const glm::vec3 cube(0.5f);
		{
			PhysicsContractWorld contract(backend);
			PhysicsWorld& world = contract.Get();
			world.AddFluidVolume(ContractWater());
			// 3000 kg cubes would sink in water.
			const BodyHandle floater = CreateFloatingBox(world, contract.Material(), cube, 3000.0f, { glm::vec3(4.0f, 7.0f, 4.0f) });
			SWIM_REQUIRE(world.SetBuoyancy(floater, BuoyancyDesc::Floater(cube)));
			const BodyHandle anchor = CreateFloatingBox(world, contract.Material(), cube, 3000.0f, { glm::vec3(-4.0f, 5.0f, 4.0f) });
			auto top = BuoyancyDesc::Box(cube);
			top.NeverSink = true;
			SWIM_REQUIRE(world.SetBuoyancy(anchor, top));
			const BodyHandle dropped = CreateFloatingBox(world, contract.Material(), cube, 3000.0f, { glm::vec3(4.0f, 10.0f, -4.0f) });
			SWIM_REQUIRE(world.SetBuoyancy(dropped, BuoyancyDesc::Floater(cube, 250.0f)));
			auto bad = BuoyancyDesc::Floater(cube);
			bad.Density = -1.0f;
			SWIM_CHECK(!world.SetBuoyancy(floater, bad));
			float lowest = 1.0e9f;

			for (int i = 0; i < 480; ++i)
			{
				StepPhysics(world, Dt);
				lowest = std::min({ lowest, BoxTop(world, floater, cube), BoxTop(world, anchor, cube), BoxTop(world, dropped, cube) });
			}

			SWIM_CHECK(lowest > 5.0f - 1.0e-3f);
			SWIM_CHECK(NearlyEqual(BodyHeight(world, floater), 5.0f, 0.05f));
			SWIM_CHECK(NearlyEqual(world.GetBuoyancyState(floater)->Submerged, 0.5f, 0.05f));
			SWIM_CHECK(NearlyEqual(BodyHeight(world, anchor), 4.5f, 0.02f));
			SWIM_CHECK(NearlyEqual(BodyHeight(world, dropped), 5.25f, 0.05f));
		}

		PhysicsContractWorld contract(backend);
		PhysicsWorld& world = contract.Get();
		auto water = ContractWater();
		water.Surface = [](float x, float, double time)
		{
			const float phase = 0.8f * x - 2.0f * static_cast<float>(time);
			return FluidSample{ 5.0f + 0.4f * std::sin(phase), glm::vec3(0.4f * std::sin(phase), 0.8f * std::cos(phase), 0.0f) };
		};
		world.AddFluidVolume(water);
		const BodyHandle rider = CreateFloatingBox(world, contract.Material(), cube, 3000.0f, { glm::vec3(3.0f, 5.0f, 3.0f) });
		SWIM_REQUIRE(world.SetBuoyancy(rider, BuoyancyDesc::Floater(cube)));
		float worst = 1.0e9f;

		for (int i = 0; i < 360; ++i)
		{
			StepPhysics(world, Dt);
			PhysicsPose pose;
			SWIM_REQUIRE(world.GetBodyPose(rider, pose));
			const float surface = 5.0f + 0.4f * std::sin(0.8f * pose.Position.x - 2.0f * static_cast<float>(world.GetSimulationTime()));
			worst = std::min(worst, BoxTop(world, rider, cube) - surface);
		}

		SWIM_CHECK(worst > -1.0e-3f);
	}

	// A floating cube on travelling waves: its heights every half second, for comparing
	// backends (buoyancy runs above the backend, so they must agree).
	inline std::vector<float> RunPhysicsWaveRide(IPhysicsBackend& backend)
	{
		PhysicsContractWorld contract(backend);
		PhysicsWorld& world = contract.Get();
		auto water = ContractWater();
		water.Surface = [](float x, float, double time)
		{
			const float phase = 0.8f * x - 2.0f * static_cast<float>(time);
			return FluidSample{ 5.0f + 0.4f * std::sin(phase), glm::vec3(0.4f * std::sin(phase), 0.0f, 0.0f) };
		};
		world.AddFluidVolume(water);
		const BodyHandle box = CreateFloatingBox(world, contract.Material(), glm::vec3(0.4f), 256.0f, { glm::vec3(3.0f, 5.0f, 3.0f) });
		std::vector<float> heights;
		float low = 1.0e9f, high = -1.0e9f;

		for (int i = 1; i <= 480; ++i)
		{
			StepPhysics(world, 1.0f / 60.0f);

			if (i > 120)
			{
				low = std::min(low, BodyHeight(world, box));
				high = std::max(high, BodyHeight(world, box));
			}

			if (i % 30 == 0)
			{
				heights.push_back(BodyHeight(world, box));
			}
		}

		SWIM_CHECK(high - low > 0.3f); // It bobs with the 0.8 m crest-to-trough waves.
		SWIM_CHECK(low > 4.0f && high < 6.0f);
		return heights;
	}

} // namespace Engine::Tests
