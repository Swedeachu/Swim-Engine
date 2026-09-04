#include "ScenePhysicsBridge.h"

#include "Engine/Components/Transform.h"
#include "Engine/Systems/Physics/PhysicsSystem.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace Engine
{

	namespace
	{

		bool IsFiniteVec3(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFiniteQuat(const glm::quat& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
		}

		glm::quat SafeUnitQuat(glm::quat value)
		{
			if (!IsFiniteQuat(value))
			{
				return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			}

			const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
			if (!(lengthSquared > 0.0f) || !std::isfinite(lengthSquared))
			{
				return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			}

			return glm::normalize(value);
		}

		bool ScaleChanged(const glm::vec3& a, const glm::vec3& b)
		{
			constexpr float Epsilon = 0.00001f;
			return std::abs(a.x - b.x) > Epsilon
				|| std::abs(a.y - b.y) > Epsilon
				|| std::abs(a.z - b.z) > Epsilon;
		}

	}

	ScenePhysicsBridge::ScenePhysicsBridge(PhysicsSystem& system, entt::registry& reg, PhysicsWorldDesc desc)
		: physicsSystem(system), registry(reg), worldDesc(desc)
	{}

	ScenePhysicsBridge::~ScenePhysicsBridge()
	{
		registry.on_construct<Rigidbody>().disconnect<&ScenePhysicsBridge::OnRigidbodyConstruct>(*this);
		registry.on_destroy<Rigidbody>().disconnect<&ScenePhysicsBridge::OnRigidbodyDestroy>(*this);
		registry.on_destroy<Transform>().disconnect<&ScenePhysicsBridge::OnTransformDestroy>(*this);

		if (world && world->IsSimulationInFlight())
		{
			world->FetchResults(true);
		}

		registry.view<Rigidbody>().each(
			[&](entt::entity entity, Rigidbody& rigidbody)
			{
				DestroyEntityBody(entity, rigidbody);
			});

		bodyResources.clear();
		world.reset();
		initialized = false;
	}

	bool ScenePhysicsBridge::Init()
	{
		if (initialized)
		{
			return true;
		}

		world = physicsSystem.CreateWorld(worldDesc);
		if (!world || !world->IsValid())
		{
			world.reset();
			return false;
		}

		registry.on_construct<Rigidbody>().connect<&ScenePhysicsBridge::OnRigidbodyConstruct>(*this);
		registry.on_destroy<Rigidbody>().connect<&ScenePhysicsBridge::OnRigidbodyDestroy>(*this);
		registry.on_destroy<Transform>().connect<&ScenePhysicsBridge::OnTransformDestroy>(*this);

		initialized = true;

		registry.view<Transform, Rigidbody>().each(
			[&](entt::entity entity, Transform& transform, Rigidbody& rigidbody)
			{
				CreateOrRebuildBody(entity, transform, rigidbody);
			});

		return true;
	}

	void ScenePhysicsBridge::OnRigidbodyConstruct(entt::registry& reg, entt::entity entity)
	{
		(void)reg;

		if (!initialized || !registry.valid(entity) || !registry.any_of<Transform>(entity))
		{
			return;
		}

		CreateOrRebuildBody(entity, registry.get<Transform>(entity), registry.get<Rigidbody>(entity));
	}

	void ScenePhysicsBridge::OnRigidbodyDestroy(entt::registry& reg, entt::entity entity)
	{
		(void)reg;

		if (!registry.valid(entity))
		{
			return;
		}

		DestroyEntityBody(entity, registry.get<Rigidbody>(entity));
	}

	void ScenePhysicsBridge::OnTransformDestroy(entt::registry& reg, entt::entity entity)
	{
		(void)reg;

		if (!registry.valid(entity) || !registry.any_of<Rigidbody>(entity))
		{
			return;
		}

		DestroyEntityBody(entity, registry.get<Rigidbody>(entity));
	}

	PhysicsPose ScenePhysicsBridge::GetPoseFromTransform(entt::entity entity, Transform& transform) const
	{
		glm::vec3 position = transform.GetWorldPosition(registry);
		glm::quat rotation = transform.GetWorldRotation(registry);

		if (!IsFiniteVec3(position))
		{
			std::cerr << "ScenePhysicsBridge | invalid entity position pos=(" << position.x << "," << position.y << "," << position.z << ")\n";
			position = glm::vec3(0.0f);
		}

		rotation = SafeUnitQuat(rotation);
		return PhysicsPose{ position, rotation };
	}


	void ScenePhysicsBridge::CreateOrRebuildBody(entt::entity entity, Transform& transform, Rigidbody& rigidbody)
	{
		if (!world)
		{
			return;
		}

		const glm::vec3 scale = glm::abs(transform.GetWorldScale(registry));
		const auto resourcesIt = bodyResources.find(entity);
		if (rigidbody.body && world->IsBodyValid(rigidbody.body) && !rigidbody.dirty
			&& resourcesIt != bodyResources.end() && !ScaleChanged(resourcesIt->second.Scale, scale))
		{
			return;
		}

		if (rigidbody.body)
		{
			DestroyEntityBody(entity, rigidbody);
		}

		ShapeDesc shapeDesc{};
		shapeDesc.Type = rigidbody.collider.type;
		shapeDesc.IsTrigger = rigidbody.isTrigger;

		switch (rigidbody.collider.type)
		{
			case ColliderType::Box:
			{
				shapeDesc.Box.HalfExtents = rigidbody.collider.box.halfExtents * scale;
				if (!(shapeDesc.Box.HalfExtents.x > 0.0f) || !(shapeDesc.Box.HalfExtents.y > 0.0f) || !(shapeDesc.Box.HalfExtents.z > 0.0f))
				{
					return;
				}
				break;
			}
			case ColliderType::Sphere:
			{
				const float uniformScale = std::max(scale.x, std::max(scale.y, scale.z));
				shapeDesc.Sphere.Radius = rigidbody.collider.sphere.radius * uniformScale;
				if (!(shapeDesc.Sphere.Radius > 0.0f) || !std::isfinite(shapeDesc.Sphere.Radius))
				{
					return;
				}
				break;
			}
			case ColliderType::Capsule:
			{
				shapeDesc.Capsule.Radius = rigidbody.collider.capsule.radius * std::max(scale.x, scale.z);
				shapeDesc.Capsule.HalfHeight = rigidbody.collider.capsule.halfHeight * scale.y;
				if (!(shapeDesc.Capsule.Radius > 0.0f) || !(shapeDesc.Capsule.HalfHeight >= 0.0f)
					|| !std::isfinite(shapeDesc.Capsule.Radius) || !std::isfinite(shapeDesc.Capsule.HalfHeight))
				{
					return;
				}
				break;
			}
			case ColliderType::ConvexMesh:
			case ColliderType::TriangleMesh:
			{
				// The generic contract owns mesh-collision handles now. The legacy
				// component intentionally cannot request one until cooked collision
				// AssetId data is added in the asset/compiler phase.
				return;
			}
		}

		const PhysicsMaterialHandle material = world->CreateMaterial(PhysicsMaterialDesc{});
		if (!material)
		{
			return;
		}

		const ShapeHandle shape = world->CreateShape(shapeDesc, material);
		if (!shape)
		{
			world->DestroyMaterial(material);
			return;
		}

		BodyDesc bodyDesc{};
		bodyDesc.Motion = rigidbody.type;
		bodyDesc.Shape = shape;
		bodyDesc.Pose = GetPoseFromTransform(entity, transform);
		bodyDesc.Collision = rigidbody.collision;
		bodyDesc.UseGravity = rigidbody.useGravity;
		bodyDesc.StartAwake = rigidbody.startAwake;
		bodyDesc.Mass = rigidbody.mass;
		bodyDesc.LinearDamping = rigidbody.linearDamping;
		bodyDesc.AngularDamping = rigidbody.angularDamping;
		bodyDesc.InitialLinearVelocity = rigidbody.initialLinearVelocity;
		bodyDesc.InitialAngularVelocity = rigidbody.initialAngularVelocity;
		bodyDesc.HasInitialLinearVelocity = rigidbody.hasInitialLinearVelocity;
		bodyDesc.HasInitialAngularVelocity = rigidbody.hasInitialAngularVelocity;
		bodyDesc.UserData = 0;

		const BodyHandle body = world->CreateBody(bodyDesc);
		if (!body)
		{
			world->DestroyShape(shape);
			world->DestroyMaterial(material);
			return;
		}

		rigidbody.body = body;
		rigidbody.dirty = false;
		rigidbody.ClearInitialVelocities();
		bodyResources[entity] = BodyResources{ shape, material, scale };
	}

	void ScenePhysicsBridge::DestroyEntityBody(entt::entity entity, Rigidbody& rigidbody)
	{
		if (world && rigidbody.body)
		{
			world->DestroyBody(rigidbody.body);
			rigidbody.body = BodyHandle{};
		}

		const auto resourcesIt = bodyResources.find(entity);
		if (resourcesIt != bodyResources.end())
		{
			if (world)
			{
				world->DestroyShape(resourcesIt->second.Shape);
				world->DestroyMaterial(resourcesIt->second.Material);
			}
			bodyResources.erase(resourcesIt);
		}

		rigidbody.dirty = true;
	}

	void ScenePhysicsBridge::PreSimulateSync(float dt)
	{
		(void)dt;

		if (!initialized || !world)
		{
			return;
		}

		registry.view<Transform, Rigidbody>().each(
			[&](entt::entity entity, Transform& transform, Rigidbody& rigidbody)
			{
				const auto resourcesIt = bodyResources.find(entity);
				const glm::vec3 scale = glm::abs(transform.GetWorldScale(registry));
				if (!rigidbody.body || !world->IsBodyValid(rigidbody.body) || rigidbody.dirty
					|| resourcesIt == bodyResources.end() || ScaleChanged(resourcesIt->second.Scale, scale))
				{
					CreateOrRebuildBody(entity, transform, rigidbody);
				}
			});

		registry.view<Transform, Rigidbody>().each(
			[&](entt::entity entity, Transform& transform, Rigidbody& rigidbody)
			{
				if (!rigidbody.body || !world->IsBodyValid(rigidbody.body))
				{
					return;
				}

				if (rigidbody.type == RigidbodyType::Static)
				{
					world->SetBodyPose(rigidbody.body, GetPoseFromTransform(entity, transform), false);
				}
				else if (rigidbody.type == RigidbodyType::Kinematic)
				{
					world->SetKinematicTarget(rigidbody.body, GetPoseFromTransform(entity, transform));
				}
			});
	}

	void ScenePhysicsBridge::Step(float dt)
	{
		if (initialized && world)
		{
			world->BeginSimulation(dt);
		}
	}

	void ScenePhysicsBridge::FetchResults(bool block)
	{
		if (initialized && world)
		{
			world->FetchResults(block);
		}
	}

	void ScenePhysicsBridge::PostSimulateSync()
	{
		if (!initialized || !world)
		{
			return;
		}

		registry.view<Transform, Rigidbody>().each(
			[&](entt::entity entity, Transform& transform, Rigidbody& rigidbody)
			{
				if (rigidbody.type != RigidbodyType::Dynamic || !rigidbody.body || !world->IsBodyValid(rigidbody.body))
				{
					return;
				}

				PhysicsPose pose{};
				if (!world->GetBodyPose(rigidbody.body, pose))
				{
					return;
				}

				transform.SetPhysicsTargetWorldPose(registry, pose.Position, pose.Rotation);
				registry.patch<Transform>(entity, [](auto&) {});
			});
	}

	void ScenePhysicsBridge::Interpolate(float alpha)
	{
		if (!initialized || !world)
		{
			return;
		}

		const float t = std::clamp(alpha, 0.0f, 1.0f);

		registry.view<Transform, Rigidbody>().each(
			[&](entt::entity entity, Transform& transform, Rigidbody& rigidbody)
			{
				if (rigidbody.type != RigidbodyType::Dynamic || !rigidbody.body || !world->IsBodyValid(rigidbody.body) || !transform.HasPhysicsTarget())
				{
					return;
				}

				transform.ApplyPhysicsInterpolation(registry, t);
				registry.patch<Transform>(entity, [](auto&) {});
			});
	}

	bool ScenePhysicsBridge::HasBody(entt::entity entity) const
	{
		if (!world || !registry.valid(entity) || !registry.any_of<Rigidbody>(entity))
		{
			return false;
		}

		const Rigidbody& rigidbody = registry.get<Rigidbody>(entity);
		return rigidbody.body && world->IsBodyValid(rigidbody.body);
	}

	void ScenePhysicsBridge::AddForce(entt::entity entity, const glm::vec3& force, ForceMode mode, bool autowake)
	{
		if (!world || !registry.valid(entity) || !registry.any_of<Rigidbody>(entity))
		{
			return;
		}

		Rigidbody& rigidbody = registry.get<Rigidbody>(entity);
		if (rigidbody.type == RigidbodyType::Dynamic && rigidbody.body)
		{
			world->AddForce(rigidbody.body, force, mode, autowake);
		}
	}

	void ScenePhysicsBridge::SetLinearVelocity(entt::entity entity, const glm::vec3& velocity, bool autowake)
	{
		if (!world || !registry.valid(entity) || !registry.any_of<Rigidbody>(entity))
		{
			return;
		}

		Rigidbody& rigidbody = registry.get<Rigidbody>(entity);
		if (rigidbody.body)
		{
			world->SetLinearVelocity(rigidbody.body, velocity, autowake);
		}
	}

	void ScenePhysicsBridge::SetAngularVelocity(entt::entity entity, const glm::vec3& velocity, bool autowake)
	{
		if (!world || !registry.valid(entity) || !registry.any_of<Rigidbody>(entity))
		{
			return;
		}

		Rigidbody& rigidbody = registry.get<Rigidbody>(entity);
		if (rigidbody.body)
		{
			world->SetAngularVelocity(rigidbody.body, velocity, autowake);
		}
	}

} // namespace Engine
