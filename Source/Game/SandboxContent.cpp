#include "Game/SandboxContent.h"

#include "Engine/Components/MeshRenderer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Scene/Scene.h"

#include <cmath>

namespace Game
{
	entt::entity SpawnMesh(Engine::Scene& scene, const MeshSpawn& spawn)
	{
		const entt::entity entity = scene.CreateEntity(spawn.Name);
		scene.AddComponent<Engine::Transform>(entity, Engine::Transform(spawn.Position, spawn.Scale, spawn.Rotation));
		Engine::MeshRenderer renderer;
		renderer.Parts.push_back({ spawn.Mesh, spawn.Material });
		renderer.Flags = spawn.Flags;
		scene.AddComponent<Engine::MeshRenderer>(entity, std::move(renderer));
		for (const auto tag : spawn.Tags)
		{
			scene.AddTag(entity, tag);
		}
		return entity;
	}

	namespace
	{
		Engine::Rigidbody MakeBody(Engine::RigidbodyType type, float mass)
		{
			Engine::Rigidbody body;
			body.type = type;
			body.mass = mass;
			body.useGravity = type == Engine::RigidbodyType::Dynamic;
			return body;
		}
	} // namespace

	void AddBoxBody(Engine::Scene& scene, entt::entity entity, Engine::RigidbodyType type, const glm::vec3& halfExtents, float mass)
	{
		auto body = MakeBody(type, mass);
		body.collider.type = Engine::ColliderType::Box;
		body.collider.box.halfExtents = halfExtents;
		scene.AddComponent<Engine::Rigidbody>(entity, body);
		scene.AddTag(entity, Engine::Tags::Physics);
	}

	void AddSphereBody(Engine::Scene& scene, entt::entity entity, Engine::RigidbodyType type, float radius, float mass)
	{
		auto body = MakeBody(type, mass);
		body.collider.type = Engine::ColliderType::Sphere;
		body.collider.sphere.radius = radius;
		scene.AddComponent<Engine::Rigidbody>(entity, body);
		scene.AddTag(entity, Engine::Tags::Physics);
	}

	void AddCapsuleBody(Engine::Scene& scene, entt::entity entity, Engine::RigidbodyType type, float radius, float halfHeight, float mass)
	{
		auto body = MakeBody(type, mass);
		body.collider.type = Engine::ColliderType::Capsule;
		body.collider.capsule.radius = radius;
		body.collider.capsule.halfHeight = halfHeight;
		scene.AddComponent<Engine::Rigidbody>(entity, body);
		scene.AddTag(entity, Engine::Tags::Physics);
	}

	glm::vec3 SrgbColor(int r, int g, int b)
	{
		const auto decode = [](int v)
		{
			const float c = static_cast<float>(v) / 255.0f;
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		};
		return { decode(r), decode(g), decode(b) };
	}

	std::uint32_t Material(Engine::MaterialLibrary& materials, const std::string& name, const glm::vec3& color, float metallic,
		float roughness, const glm::vec3& emissive, Engine::MaterialBlend blend, float alpha)
	{
		Engine::MaterialDesc desc;
		desc.Name = name;
		desc.BaseColor = { color.r, color.g, color.b, alpha };
		desc.Metallic = metallic;
		desc.Roughness = roughness;
		desc.Emissive = { emissive.r, emissive.g, emissive.b };
		desc.Blend = blend;
		desc.DoubleSided = blend == Engine::MaterialBlend::Transparent;
		return materials.GetOrCreate(desc);
	}
} // namespace Game
