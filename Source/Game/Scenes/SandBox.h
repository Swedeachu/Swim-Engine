#pragma once

#include "Engine/Systems/Renderer/Features/LensFlare.h"
#include "Engine/Systems/Renderer/Features/SunShafts.h"
#include "Engine/Systems/Renderer/Features/VolumetricClouds.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/UI/UiDocument.h"
#include "Game/ModelImport.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <random>

namespace Game
{
	class BallShooter;

	// The sandbox demo (Phase 23): every runtime system in one scene.
	//
	//   rendering playground  a PBR sphere gallery (metallic x roughness), an instance hall
	//                         of 576 cubes, glass panes (sorted transparency), emissive tori
	//                         (bloom), GPU particles (fountain, smoke, sparks) and GPU-skinned
	//                         tentacles
	//   lighting              a shadowed sun (cascades), a shadowed spot, a shadowed point
	//                         lantern and orbiting coloured point lights (clustered)
	//   physics playground    a box pyramid, a ramp, capsules and spheres; fire balls with the
	//                         left mouse button or F, or rain them from the panel
	//   UI                    the control panel (simulation, rendering, scene browser,
	//                         findings), a diagnostics overlay, a world-space info panel and
	//                         billboard zone labels
	//
	// Everything is built in Init, so Stop (which reloads the scene) restores it exactly.
	// The fly camera works in every state: hold the right mouse button + WASD.
	class Sandbox : public Engine::Scene
	{
	  public:
		using Engine::Scene::Scene;

		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		int Exit() override;
		void OnStateChanged(Engine::EngineState previous, Engine::EngineState current) override;

		// Physics playground.
		void RecordImpact(float impulse);

		std::uint64_t GetImpacts() const { return impacts; }

		float GetStrongestImpact() const { return strongestImpact; }

		void SpawnBalls(std::uint32_t count);

		void SetRainBalls(bool value) { rainBalls = value; }

		bool GetRainBalls() const { return rainBalls; }

		// A dynamic primitive dropped in front of the camera (or at the playground).
		entt::entity SpawnPrimitive(Engine::BuiltinMesh mesh);
		BallShooter* GetShooter() const;

		// Lighting.
		void SetSunAngles(float elevationDegrees, float azimuthDegrees);
		// The sandbox's sky, ambient, exposure, tone map and grading (applied once at startup).
		static void ApplyTropicalLook(Engine::RenderSettings& settings);
		// Volumetric clouds, sun shafts and a lens flare as renderer features.
		void AddAtmosphereFeatures();

		float GetSunElevation() const { return sunElevation; }

		float GetSunAzimuth() const { return sunAzimuth; }

		// The world-space info panel's document and its body label (the HUD updates it).
		const std::shared_ptr<Swim::UI::UiDocument>& GetInfoDocument() const { return infoDocument; }

		Swim::UI::UiNodeId GetInfoBody() const { return infoBody; }

		Swim::UI::UiNodeId GetInfoButton() const { return infoButton; }

		entt::entity GetCameraRig() const { return cameraRig; }

		// Camera bookmarks (one per playground); false without a camera or out of range.
		static std::uint32_t GetBookmarkCount();
		static const char* GetBookmarkName(std::uint32_t index);
		bool GoToBookmark(std::uint32_t index);

		std::uint32_t GetLastBookmark() const { return lastBookmark; }

		// The Sponza backdrop (when a cooked Sponza exists) and the light swarm in its atrium.
		bool IsSponzaLoaded() const { return sponza.Valid(); }

		const ImportedModel& GetSponza() const { return sponza; }

		entt::entity GetSwarmController() const { return swarmController; }

		// The render features the sandbox adds to the renderer (null without one).
		Engine::VolumetricClouds* GetClouds() const { return clouds.get(); }

		Engine::SunShafts* GetSunShafts() const { return sunShafts.get(); }

		Engine::LensFlare* GetLensFlare() const { return lensFlare.get(); }

		glm::vec3 GetSwarmMin() const { return swarmMin; }

		glm::vec3 GetSwarmMax() const { return swarmMax; }

		std::uint32_t GetBuildCount() const { return builds; }

		// The control panel tab the HUD shows (the "sandbox.tab" command sets it).
		void RequestTab(std::uint32_t tab) { requestedTab = tab; }

		// All sandbox UI on/off: the HUD and every world canvas (C, or "sandbox.hud 0|1").
		void SetHudVisible(bool value) { hudVisible = value; }

		bool IsHudVisible() const { return hudVisible; }

		std::uint32_t GetRequestedTab() const { return requestedTab; }

		// True while the UI has the pointer or keyboard (gameplay input stands back).
		bool IsUiCapturing() const;

	  private:
		struct Palette
		{
			std::array<Engine::MeshLibrary::MeshHandle, static_cast<std::size_t>(Engine::BuiltinMesh::Count)> Meshes{};
			Engine::MeshLibrary::MeshHandle Ground;
			std::uint32_t GroundMaterial = 0;
			bool HasRenderer = false;

			Engine::MeshLibrary::MeshHandle Mesh(Engine::BuiltinMesh mesh) const { return Meshes[static_cast<std::size_t>(mesh)]; }
		};

		void LoadPalette();
		std::uint32_t Mat(const std::string& name, const glm::vec3& color, float metallic, float roughness,
			const glm::vec3& emissive = glm::vec3(0.0f), bool transparent = false, float alpha = 1.0f);
		void BuildCamera();
		void BuildGround();
		void BuildLighting();
		void BuildPbrGallery();
		void BuildInstanceHall();
		void BuildGlassAndEmissive();
		void BuildPhysicsPlayground();
		void BuildParticles();
		void BuildTentacles();
		void BuildSponza();
		void BuildLightSwarm();
		void BuildWorldUi();
		void BuildHud();
		void SpawnBallAt(const glm::vec3& position, const glm::vec3& velocity);

		Palette palette;
		entt::entity cameraRig = entt::null;
		entt::entity shooterEntity = entt::null;
		entt::entity sun = entt::null;
		std::shared_ptr<Swim::UI::UiDocument> infoDocument;
		Swim::UI::UiNodeId infoBody;
		Swim::UI::UiNodeId infoButton;
		float sunElevation = 38.0f;
		float sunAzimuth = 35.0f;
		bool cameraPlaced = false;
		bool rainBalls = false;
		float rainTimer = 0.0f;
		std::uint64_t impacts = 0;
		float strongestImpact = 0.0f;
		std::uint64_t spawned = 0;
		std::uint32_t builds = 0;
		std::uint32_t requestedTab = UINT32_MAX;
		bool commandsRegistered = false;
		bool hudVisible = true;
		std::uint32_t lastBookmark = 0;
		ImportedModel sponza;
		bool sponzaSearched = false;
		entt::entity swarmController = entt::null;
		std::shared_ptr<Engine::VolumetricClouds> clouds;
		std::shared_ptr<Engine::SunShafts> sunShafts;
		std::shared_ptr<Engine::LensFlare> lensFlare;
		glm::vec3 swarmMin{ 0.0f };
		glm::vec3 swarmMax{ 0.0f };
		std::mt19937 random{ 1234u };
	};
} // namespace Game
