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
	}

} // namespace Engine::Tests
