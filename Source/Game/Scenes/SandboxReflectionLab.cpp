#include "Engine/Components/MeshRenderer.h"
#include "Engine/Components/ReflectionProbe.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Game/Behaviors/Motion.h"
#include "Game/Behaviors/ReflectionLabFloor.h"
#include "Game/SandboxContent.h"
#include "Game/Scenes/Sandbox.h"

namespace Game
{
	namespace
	{
		// An object probe: captured from the entity's centre without the entity, used only by it.
		Engine::ReflectionProbe ObjectProbe(float priority = 1.0f)
		{
			Engine::ReflectionProbe probe;
			probe.ObjectProbe = true;
			probe.Priority = priority;
			return probe;
		}
	} // namespace

	glm::vec3 Sandbox::GetReflectionLabCenter()
	{
		return { -22.0f, 0.0f, 16.0f };
	}

	void Sandbox::BuildReflectionLab()
	{
		// The reflection regression cases of the hybrid reflection work (SSR -> local probes ->
		// environment), side by side on a floor pad whose look changes at run time
		// (ReflectionLabFloor): two chrome spheres 1 cm apart, a chrome sphere 5 mm above the
		// floor, a mirror cube with coloured balls beside it, smooth and brushed chrome, a
		// chrome sphere orbiting the lab and a coloured block orbiting it the other way, and a
		// striped column behind the lab camera that only reflections show.
		const glm::vec3 lab = GetReflectionLabCenter();
		const auto flags = Swim::Render::RenderObjectFlags::Default;
		const std::vector<Engine::TagId> tags{ GameTags::ReflectionLab };
		const auto sphere = palette.Mesh(Engine::BuiltinMesh::Sphere);
		const auto cube = palette.Mesh(Engine::BuiltinMesh::Cube);

		// The floor pad: 0.5 m black and white tiles, 12 m across, just above the ground.
		Engine::MaterialDesc floorDesc;
		floorDesc.Name = "Reflection lab floor";
		floorDesc.Roughness = 0.45f;
		std::uint32_t floorMaterial = 0;
		Engine::MeshLibrary::MeshHandle padMesh;
		if (auto* render = GetRenderServices(); render && render->HasRenderer())
		{
			padMesh = render->Meshes->Find("ReflectionLabPad");
			if (!padMesh.IsValid())
			{
				padMesh = render->Meshes->Register("ReflectionLabPad", Engine::ProceduralMeshes::MakePlane(12.0f, 4, 12.0f));
			}
			auto tiles = render->Meshes->FindTexture("ReflectionLabTiles");
			if (!tiles.IsValid())
			{
				tiles = render->Meshes->RegisterChecker("ReflectionLabTiles", 128, 2, { 245, 245, 245, 255 }, { 22, 22, 26, 255 });
			}
			floorDesc.BaseColorTexture = tiles;
			floorMaterial = render->Materials->GetOrCreate(floorDesc);
		}
		const entt::entity pad = SpawnMesh(*this, { "Reflection lab floor", padMesh, floorMaterial, lab + glm::vec3(0.0f, 0.004f, 0.0f),
													  glm::vec3(1.0f), glm::quat(1, 0, 0, 0), flags, tags });
		labFloor = EmplaceBehavior<ReflectionLabFloor>(pad, floorMaterial, floorDesc);

		const std::uint32_t chrome = Mat("Lab chrome", { 0.95f, 0.93f, 0.90f }, 1.0f, 0.02f);
		const std::uint32_t brushed = Mat("Lab brushed chrome", { 0.95f, 0.93f, 0.90f }, 1.0f, 0.32f);
		const auto chromeSphere = [&](const std::string& name, const glm::vec3& position, std::uint32_t material, float priority = 1.0f)
		{
			const entt::entity e = SpawnMesh(*this, { name, sphere, material, position, glm::vec3(1.0f), glm::quat(1, 0, 0, 0), flags, tags });
			AddComponent<Engine::ReflectionProbe>(e, ObjectProbe(priority));
			return e;
		};
		// Two chrome spheres almost touching (1 cm apart) - screenshot 3.
		chromeSphere("Lab chrome pair A", lab + glm::vec3(-2.505f, 0.5f, 0.0f), chrome);
		chromeSphere("Lab chrome pair B", lab + glm::vec3(-1.495f, 0.5f, 0.0f), chrome);
		// A chrome sphere 5 mm above the floor: the tiles under it must show as tiles.
		chromeSphere("Lab chrome on floor", lab + glm::vec3(0.4f, 0.505f, 1.6f), chrome, 1.5f);
		// A mirror cube with coloured balls beside it - screenshots 1 and 2 (sliced balls).
		const entt::entity mirror = SpawnMesh(*this, { "Lab mirror cube", cube, chrome, lab + glm::vec3(2.6f, 0.75f, -1.2f), glm::vec3(1.5f),
														glm::angleAxis(0.35f, glm::vec3(0, 1, 0)), flags, tags });
		AddComponent<Engine::ReflectionProbe>(mirror, ObjectProbe());
		SpawnMesh(*this, { "Lab purple ball", sphere, Mat("Lab purple", SrgbColor(160, 60, 230), 0.0f, 0.25f),
							 lab + glm::vec3(1.55f, 0.45f, -0.05f), glm::vec3(0.9f), glm::quat(1, 0, 0, 0), flags, tags });
		SpawnMesh(*this, { "Lab yellow ball", sphere, Mat("Lab yellow", SrgbColor(245, 200, 40), 0.0f, 0.3f),
							 lab + glm::vec3(3.6f, 0.4f, 0.1f), glm::vec3(0.8f), glm::quat(1, 0, 0, 0), flags, tags });
		// Smooth and brushed chrome side by side.
		chromeSphere("Lab smooth chrome", lab + glm::vec3(-0.9f, 0.5f, -1.9f), chrome);
		chromeSphere("Lab brushed chrome", lab + glm::vec3(0.4f, 0.5f, -1.9f), brushed);
		// Moving: a chrome sphere orbiting the lab, a coloured block orbiting the other way.
		const entt::entity orbiter = chromeSphere("Lab orbiting chrome", lab + glm::vec3(4.2f, 0.6f, 0.0f), chrome);
		EmplaceBehavior<Orbit>(orbiter, lab, 4.2f, 0.6f, 0.35f, 0.0f);
		const entt::entity block = SpawnMesh(*this, { "Lab orbiting block", cube, Mat("Lab orange", SrgbColor(255, 120, 30), 0.0f, 0.4f),
														lab + glm::vec3(0.0f, 1.0f, 3.0f), glm::vec3(0.6f), glm::quat(1, 0, 0, 0), flags, tags });
		EmplaceBehavior<Orbit>(block, lab, 3.0f, 1.0f, -0.5f, 1.3f);
		EmplaceBehavior<Spin>(block, glm::vec3(0.2f, 1.0f, 0.1f), 60.0f);
		// Behind the lab camera (the "Reflection lab" view): only reflections show it.
		const std::uint32_t red = Mat("Lab red stripe", SrgbColor(220, 30, 40), 0.0f, 0.5f);
		const std::uint32_t white = Mat("Lab white stripe", SrgbColor(235, 235, 235), 0.0f, 0.5f);
		for (int i = 0; i < 6; ++i)
		{
			SpawnMesh(*this, { "Lab column " + std::to_string(i + 1), cube, i % 2 ? white : red,
								 lab + glm::vec3(0.0f, 0.25f + 0.5f * static_cast<float>(i), 7.5f), { 1.2f, 0.5f, 1.2f }, glm::quat(1, 0, 0, 0),
								 flags, tags });
		}
		// An area probe for the pad and anything in the lab without its own probe.
		const entt::entity area = CreateEntity("Reflection lab probe");
		AddComponent<Engine::Transform>(area, Engine::Transform(lab + glm::vec3(0.0f, 1.2f, 0.0f), glm::vec3(1.0f)));
		Engine::ReflectionProbe areaProbe;
		areaProbe.ObjectProbe = false;
		areaProbe.InfluenceRadius = 8.0f;
		areaProbe.BlendDistance = 2.0f;
		AddComponent<Engine::ReflectionProbe>(area, areaProbe);
		AddTag(area, GameTags::ReflectionLab);
	}
} // namespace Game
