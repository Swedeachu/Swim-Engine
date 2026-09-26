#pragma once

#include "Engine/Components/Tags.h"
#include "Engine/Systems/Physics/RigidBody.h"
#include "Engine/Systems/Renderer/GpuScene/RenderObjectFlags.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace Engine
{
	class Scene;
}

namespace Game
{
	// A renderable entity: Transform + MeshRenderer (+ name and tags).
	struct MeshSpawn
	{
		std::string Name;
		Engine::MeshLibrary::MeshHandle Mesh;
		std::uint32_t Material = 0;
		glm::vec3 Position{ 0.0f };
		glm::vec3 Scale{ 1.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		Swim::Render::RenderObjectFlags Flags = Swim::Render::RenderObjectFlags::Default;
		// A vector, not std::initializer_list: a list member would dangle after the braced
		// initializer's full expression (and MSVC rejects the default member initializer, C2797).
		std::vector<Engine::TagId> Tags;
	};

	// Creates the entity now (call from Init or a command-buffer callback, never while
	// iterating the registry).
	entt::entity SpawnMesh(Engine::Scene& scene, const MeshSpawn& spawn);

	// Physics bodies sized in world units (the collider does not follow Transform scale).
	void AddBoxBody(Engine::Scene& scene, entt::entity entity, Engine::RigidbodyType type, const glm::vec3& halfExtents, float mass = 1.0f);
	void AddSphereBody(Engine::Scene& scene, entt::entity entity, Engine::RigidbodyType type, float radius, float mass = 1.0f);
	void AddCapsuleBody(
		Engine::Scene& scene, entt::entity entity, Engine::RigidbodyType type, float radius, float halfHeight, float mass = 1.0f);

	// Linear colour from sRGB bytes.
	glm::vec3 SrgbColor(int r, int g, int b);
	// A standard material by name (created once; later calls return the same set).
	std::uint32_t Material(Engine::MaterialLibrary& materials, const std::string& name, const glm::vec3& color, float metallic,
		float roughness, const glm::vec3& emissive = glm::vec3(0.0f), Engine::MaterialBlend blend = Engine::MaterialBlend::Opaque,
		float alpha = 1.0f);

	// Game tags (registered on first use by the scene's TagRegistry).
	namespace GameTags
	{
		inline constexpr Engine::TagId PbrGallery = Engine::MakeTag("Game.PbrGallery");
		inline constexpr Engine::TagId InstanceHall = Engine::MakeTag("Game.InstanceHall");
		inline constexpr Engine::TagId PhysicsToy = Engine::MakeTag("Game.PhysicsToy");
		inline constexpr Engine::TagId Glass = Engine::MakeTag("Game.Glass");
		inline constexpr Engine::TagId Emissive = Engine::MakeTag("Game.Emissive");
		inline constexpr Engine::TagId Tentacle = Engine::MakeTag("Game.Tentacle");
		inline constexpr Engine::TagId Spawned = Engine::MakeTag("Game.Spawned");
		inline constexpr Engine::TagId Sponza = Engine::MakeTag("Game.Sponza");
		inline constexpr Engine::TagId SwarmLight = Engine::MakeTag("Game.SwarmLight");
	} // namespace GameTags
} // namespace Game
