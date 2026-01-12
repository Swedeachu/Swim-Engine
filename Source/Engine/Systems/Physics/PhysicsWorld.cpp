#include "PCH.h"
#include "PhysicsWorld.h"
#include "PhysicsSystem.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/compatibility.hpp> 

namespace Engine
{

	PhysicsWorld::PhysicsWorld(PhysicsSystem& system, entt::registry& reg)
		: physicsSystem(system), registry(reg)
	{}

	PhysicsWorld::~PhysicsWorld()
	{
		// Disconnect hooks (safe because registry outlives PhysicsWorld in Scene layout)
		registry.on_construct<Rigidbody>().disconnect<&PhysicsWorld::OnRigidbodyConstruct>(*this);
		registry.on_destroy<Rigidbody>().disconnect<&PhysicsWorld::OnRigidbodyDestroy>(*this);
		registry.on_destroy<Transform>().disconnect<&PhysicsWorld::OnTransformDestroy>(*this);

		// If a sim step is in-flight, finish it so we can safely remove actors.
		if (scene && simulating)
		{
			scene->fetchResults(true);
			simulating = false;
		}

		// Flush any deferred destroys before tearing down bodies.
		FlushPendingDestroy();

		// Tear down all existing bodies
		registry.view<Rigidbody>().each(
			[&](entt::entity e, Rigidbody& rb)
		{
			DestroyBody(e, rb);
		});

		// Flush again in case anything was deferred during teardown.
		FlushPendingDestroy();

		scene.reset();
		defaultMaterial.reset();
	}

	bool PhysicsWorld::Init()
	{
		if (initialized)
		{
			return true;
		}

		physx::PxPhysics* physics = physicsSystem.GetPxPhysics();
		physx::PxCpuDispatcher* dispatcher = physicsSystem.GetCpuDispatcher();

		if (!physics || !dispatcher)
		{
			return false;
		}

		physx::PxSceneDesc desc(physics->getTolerancesScale());
		desc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
		desc.cpuDispatcher = dispatcher;
		desc.filterShader = physx::PxDefaultSimulationFilterShader;

		// Enable CCD for fast-moving projectiles
		desc.flags |= physx::PxSceneFlag::eENABLE_CCD;

		physx::PxScene* pxScene = physics->createScene(desc);
		if (!pxScene)
		{
			return false;
		}

		scene.reset(pxScene);

		physx::PxMaterial* mat = physics->createMaterial(0.5f, 0.5f, 0.1f);
		if (!mat)
		{
			scene.reset();
			return false;
		}

		defaultMaterial.reset(mat);

		// Hook component lifecycle so bodies are created/destroyed automatically inside of PhysX.
		registry.on_construct<Rigidbody>().connect<&PhysicsWorld::OnRigidbodyConstruct>(*this);
		registry.on_destroy<Rigidbody>().connect<&PhysicsWorld::OnRigidbodyDestroy>(*this);
		registry.on_destroy<Transform>().connect<&PhysicsWorld::OnTransformDestroy>(*this);

		// Build bodies already present in the scene (if any).
		registry.view<Transform, Rigidbody>().each(
			[&](entt::entity e, Transform& tf, Rigidbody& rb)
		{
			CreateOrRebuildBody(e, tf, rb);
		});

		initialized = true;

		return true;
	}

	void PhysicsWorld::OnRigidbodyConstruct(entt::registry& reg, entt::entity entity)
	{
		(void)reg;

		if (!initialized)
		{
			return;
		}

		if (!registry.valid(entity) || !registry.any_of<Transform>(entity))
		{
			return;
		}

		Transform& tf = registry.get<Transform>(entity);
		Rigidbody& rb = registry.get<Rigidbody>(entity);

		CreateOrRebuildBody(entity, tf, rb);
	}

	void PhysicsWorld::OnRigidbodyDestroy(entt::registry& reg, entt::entity entity)
	{
		(void)reg;

		// When on_destroy fires, component still exists and can be fetched.
		if (!registry.valid(entity))
		{
			return;
		}

		Rigidbody& rb = registry.get<Rigidbody>(entity);
		DestroyBody(entity, rb);
	}

	void PhysicsWorld::OnTransformDestroy(entt::registry& reg, entt::entity entity)
	{
		(void)reg;

		// If Transform is removed, physics body becomes meaningless; destroy it.
		if (!registry.valid(entity) || !registry.any_of<Rigidbody>(entity))
		{
			return;
		}

		Rigidbody& rb = registry.get<Rigidbody>(entity);
		DestroyBody(entity, rb);
	}

	static bool IsFiniteVec3(const glm::vec3& v)
	{
		return glm::all(glm::isfinite(v));
	}

	static bool IsFiniteQuat(const glm::quat& q)
	{
		return glm::all(glm::isfinite(glm::vec4(q.x, q.y, q.z, q.w)));
	}

	static glm::quat SafeUnitQuat(glm::quat q)
	{
		if (!IsFiniteQuat(q))
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		float len2 = glm::dot(glm::vec4(q.x, q.y, q.z, q.w), glm::vec4(q.x, q.y, q.z, q.w));
		if (!(len2 > 0.0f) || !std::isfinite(len2))
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		return glm::normalize(q);
	}

	physx::PxTransform PhysicsWorld::GetPoseFromTransform(entt::entity entity, Transform& tf) const
	{
		glm::vec3 pos = tf.GetWorldPosition(registry);
		glm::quat rot = tf.GetWorldRotation(registry);

		if (!IsFiniteVec3(pos))
		{
			std::cerr << "PhysicsWorld | invalid position for entity " << (uint32_t)entt::to_integral(entity)
				<< " pos=(" << pos.x << "," << pos.y << "," << pos.z << ")\n";
			pos = glm::vec3(0.0f);
		}

		rot = SafeUnitQuat(rot);

		physx::PxTransform pose(ToPx(pos), ToPx(rot));

		if (!pose.isValid())
		{
			std::cerr << "PhysicsWorld | PxTransform invalid for entity " << (uint32_t)entt::to_integral(entity) << "\n";
		}

		return pose;
	}

	void PhysicsWorld::CreateOrRebuildBody(entt::entity entity, Transform& tf, Rigidbody& rb)
	{
		if (!scene || !defaultMaterial)
		{
			return;
		}

		physx::PxPhysics* physics = physicsSystem.GetPxPhysics();
		if (!physics)
		{
			return;
		}

		if (rb.actor && !rb.dirty)
		{
			return;
		}

		// Rebuild path
		if (rb.actor)
		{
			DestroyBody(entity, rb);
		}

		const physx::PxTransform pose = GetPoseFromTransform(entity, tf);

		physx::PxRigidActor* actor = nullptr;
		physx::PxRigidDynamic* dyn = nullptr;

		if (rb.type == RigidbodyType::Static)
		{
			actor = physics->createRigidStatic(pose);
		}
		else
		{
			dyn = physics->createRigidDynamic(pose);
			if (!dyn)
			{
				return;
			}

			dyn->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, rb.type == RigidbodyType::Kinematic);
			dyn->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !rb.useGravity);

			dyn->setLinearDamping(rb.linearDamping);
			dyn->setAngularDamping(rb.angularDamping);

			actor = dyn;
		}

		if (!actor)
		{
			return;
		}

		// Store entity id in userData for future collision callbacks etc.
		actor->userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entt::to_integral(entity)));

		// Build geometry (apply transform scale)
		const glm::vec3 scl = tf.GetWorldScale(registry);
		const glm::vec3 absScl = glm::abs(scl);

		physx::PxShape* shape = nullptr;

		switch (rb.collider.type)
		{
			case ColliderType::Box:
			{
				const glm::vec3 he = rb.collider.box.halfExtents * absScl;
				physx::PxBoxGeometry geom(he.x, he.y, he.z);
				shape = physics->createShape(geom, *defaultMaterial, true);
				break;
			}
			case ColliderType::Sphere:
			{
				const float s = glm::max(absScl.x, glm::max(absScl.y, absScl.z));
				const float r = rb.collider.sphere.radius * s;

				if (!(r > 0.0f) || !physx::PxIsFinite(r))
				{
					std::cerr << "PhysicsWorld::CreateOrRebuildBody | invalid sphere radius: " << r << "\n";
					actor->release();
					return;
				}

				physx::PxSphereGeometry geom(r);
				shape = physics->createShape(geom, *defaultMaterial, true);
				break;
			}
			case ColliderType::Capsule:
			{
				// PhysX capsule is along +X by default; rotate so it matches typical Y-up capsule.
				const float rS = glm::max(absScl.x, absScl.z);
				const float hS = absScl.y;

				const float r = rb.collider.capsule.radius * rS;
				const float hh = rb.collider.capsule.halfHeight * hS;

				physx::PxCapsuleGeometry geom(r, hh);
				shape = physics->createShape(geom, *defaultMaterial, true);

				if (shape)
				{
					const physx::PxQuat rotYUp(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f));
					shape->setLocalPose(physx::PxTransform(physx::PxVec3(0.0f), rotYUp));
				}
				break;
			}
		}

		if (!shape)
		{
			actor->release();
			return;
		}

		// Trigger setup
		if (rb.isTrigger)
		{
			shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
			shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
		}
		else
		{
			shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
			shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, false);
		}

		actor->attachShape(*shape);

		// We keep a raw pointer for convenience, but we must release our ref.
		// The actor now owns a reference to the shape.
		shape->release();

		// IMPORTANT: compute mass/inertia AFTER shape is attached
		if (rb.type == RigidbodyType::Dynamic && dyn)
		{
			if (!physx::PxRigidBodyExt::setMassAndUpdateInertia(*dyn, rb.mass))
			{
				std::cerr << "PhysicsWorld::CreateOrRebuildBody | setMassAndUpdateInertia failed\n";
			}
		}

		// MUST be in a scene before wakeUp(), etc.
		scene->addActor(*actor);

		rb.actor = actor;
		rb.shape = shape;
		rb.dirty = false;

		// Apply initial velocities after actor exists and is in scene.
		if (rb.type == RigidbodyType::Dynamic && dyn)
		{
			if (rb.hasInitialLinearVelocity) { dyn->setLinearVelocity(ToPx(rb.initialLinearVelocity), true); }
			if (rb.hasInitialAngularVelocity) { dyn->setAngularVelocity(ToPx(rb.initialAngularVelocity), true); }
			rb.ClearInitialVelocities();

			if (rb.startAwake)
			{
				dyn->wakeUp();
			}
		}
	}

	void PhysicsWorld::DestroyBody(entt::entity entity, Rigidbody& rb)
	{
		(void)entity;

		if (!rb.actor)
		{
			rb.shape = nullptr;
			return;
		}

		physx::PxRigidActor* actor = rb.actor;

		// If a sim step is in-flight, defer destruction until it is safe.
		if (simulating)
		{
			pendingDestroy.push_back(actor);

			rb.actor = nullptr;
			rb.shape = nullptr;
			rb.dirty = true;
			return;
		}

		// If the actor is still in a scene, remove it from that scene.
		if (physx::PxScene* owner = actor->getScene())
		{
			owner->removeActor(*actor);
		}

		actor->release();

		rb.actor = nullptr;
		rb.shape = nullptr;
		rb.dirty = true;
	}

	void PhysicsWorld::FlushPendingDestroy()
	{
		if (pendingDestroy.empty())
		{
			return;
		}

		for (physx::PxRigidActor* actor : pendingDestroy)
		{
			if (!actor)
			{
				continue;
			}

			// If the actor is still in a scene, remove it from that scene.
			if (physx::PxScene* owner = actor->getScene())
			{
				owner->removeActor(*actor);
			}

			actor->release();
		}

		pendingDestroy.clear();
	}

	void PhysicsWorld::PreSimulateSync(float dt)
	{
		(void)dt;

		if (!initialized || !scene)
		{
			return;
		}

		// Safe point: simulation is not in-flight here.
		FlushPendingDestroy();

		// Ensure any dirty or missing bodies get built.
		registry.view<Transform, Rigidbody>().each(
			[&](entt::entity e, Transform& tf, Rigidbody& rb)
		{
			if (!rb.actor || rb.dirty)
			{
				CreateOrRebuildBody(e, tf, rb);
			}
		});

		// Push Transform -> PhysX for Static and Kinematic bodies (authoritative from Transform).
		registry.view<Transform, Rigidbody>().each([&](entt::entity e, Transform& tf, Rigidbody& rb)
		{
			(void)e;

			if (!rb.actor)
			{
				return;
			}

			if (rb.type == RigidbodyType::Static)
			{
				const physx::PxTransform pose = GetPoseFromTransform(e, tf);
				rb.actor->setGlobalPose(pose);
			}
			else if (rb.type == RigidbodyType::Kinematic)
			{
				physx::PxRigidDynamic* dyn = rb.actor->is<physx::PxRigidDynamic>();
				if (!dyn)
				{
					return;
				}

				const physx::PxTransform target = GetPoseFromTransform(e, tf);
				dyn->setKinematicTarget(target);
			}
		});
	}

	void PhysicsWorld::Step(float dt)
	{
		if (!initialized || !scene)
		{
			return;
		}

		scene->simulate(dt);
		simulating = true;
	}

	void PhysicsWorld::FetchResults(bool block)
	{
		if (!initialized || !scene)
		{
			return;
		}

		if (!simulating)
		{
			return;
		}

		scene->fetchResults(block);
		simulating = false;
	}

	void PhysicsWorld::PostSimulateSync()
	{
		if (!initialized || !scene)
		{
			return;
		}

		// Pull PhysX -> Transform targets for Dynamic bodies (authoritative from simulation).
		// We do NOT set the transform directly here; we set targets and let per-frame interpolation drive visuals.
		registry.view<Transform, Rigidbody>().each([&](entt::entity e, Transform& tf, Rigidbody& rb)
		{
			if (!rb.actor)
			{
				return;
			}

			if (rb.type != RigidbodyType::Dynamic)
			{
				return;
			}

			physx::PxRigidDynamic* dyn = rb.actor->is<physx::PxRigidDynamic>();
			if (!dyn)
			{
				return;
			}

			const physx::PxTransform pose = dyn->getGlobalPose();
			const glm::vec3 pos = ToGlm(pose.p);
			const glm::quat rot = ToGlm(pose.q);

			tf.SetPhysicsTargetWorldPose(registry, pos, rot);

			// IMPORTANT: notify EnTT that Transform was updated so observers / BVH / culling can react
			registry.patch<Transform>(e, [](auto&) {});
		});
	}

	// The problem with this is if we do things like setting a position or rotation directly from higher up gameplay code (like a teleporter),
	// the next frame during this interpolation will just overwrite it.
	void PhysicsWorld::Interpolate(float alpha)
	{
		if (!initialized)
		{
			return;
		}

		float t = alpha;

		if (t < 0.0f) { t = 0.0f; }
		if (t > 1.0f) { t = 1.0f; }

		// Smooth render pose for Dynamic bodies only.
		registry.view<Transform, Rigidbody>().each([&](entt::entity e, Transform& tf, Rigidbody& rb)
		{
			if (!rb.actor)
			{
				return;
			}

			if (rb.type != RigidbodyType::Dynamic)
			{
				return;
			}

			if (!tf.HasPhysicsTarget())
			{
				return;
			}

			tf.ApplyPhysicsInterpolation(registry, t);

			// IMPORTANT: notify EnTT that Transform was updated so observers / BVH / culling can react
			registry.patch<Transform>(e, [](auto&) {});
		});
	}

	bool PhysicsWorld::HasActor(entt::entity e) const
	{
		if (!registry.valid(e) || !registry.any_of<Rigidbody>(e))
		{
			return false;
		}

		const Rigidbody& rb = registry.get<Rigidbody>(e);
		return rb.actor != nullptr;
	}

	void PhysicsWorld::AddForce(entt::entity e, const glm::vec3& force, physx::PxForceMode::Enum mode, bool autowake)
	{
		if (!registry.valid(e) || !registry.any_of<Rigidbody>(e))
		{
			return;
		}

		Rigidbody& rb = registry.get<Rigidbody>(e);

		if (!rb.actor || rb.type != RigidbodyType::Dynamic)
		{
			return;
		}

		physx::PxRigidDynamic* dyn = rb.actor->is<physx::PxRigidDynamic>();
		if (!dyn)
		{
			return;
		}

		dyn->addForce(ToPx(force), mode, autowake);
	}

	void PhysicsWorld::SetLinearVelocity(entt::entity e, const glm::vec3& vel, bool autowake)
	{
		if (!registry.valid(e) || !registry.any_of<Rigidbody>(e))
		{
			return;
		}

		Rigidbody& rb = registry.get<Rigidbody>(e);

		physx::PxRigidDynamic* dyn = rb.actor ? rb.actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!dyn)
		{
			return;
		}

		dyn->setLinearVelocity(ToPx(vel), autowake);
	}

	void PhysicsWorld::SetAngularVelocity(entt::entity e, const glm::vec3& vel, bool autowake)
	{
		if (!registry.valid(e) || !registry.any_of<Rigidbody>(e))
		{
			return;
		}

		Rigidbody& rb = registry.get<Rigidbody>(e);

		physx::PxRigidDynamic* dyn = rb.actor ? rb.actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!dyn)
		{
			return;
		}

		dyn->setAngularVelocity(ToPx(vel), autowake);
	}

} // namespace Engine
