#pragma once
#include "Game/Scenes/Sandbox.h"
#include "Engine/Runtime/RenderToggles.h"
#include "Engine/Platform/FileSystem.h"

#include "Engine/Commands/CommandRegistry.h"
#include "Engine/Components/CameraComponent.h"
#include "Engine/Components/Light.h"
#include "Engine/Components/MeshRenderer.h"
#include "Engine/Components/ParticleEmitter.h"
#include "Engine/Components/PlanarReflector.h"
#include "Engine/Components/ReflectionProbe.h"
#include "Game/Behaviors/BlackHole.h"
#include "Game/Behaviors/ReflectionLabFloor.h"
#include "Engine/Components/SkinnedMeshRenderer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/UiCanvas.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Camera/FlyCameraController.h"
#include "Engine/Systems/Physics/RigidBody.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Scene/RenderExtraction/Runtime/SceneRenderBridge.h"
#include "Engine/Systems/Scene/SceneCommandBuffer.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Game/Behaviors/BallShooter.h"
#include "Engine/Assets/AssetSystem.h"
#include "Game/Behaviors/LightSwarm.h"
#include "Game/Behaviors/Motion.h"
#include "Game/Behaviors/TentacleAnimator.h"
#include "Engine/Systems/Scene/ModelImporter/ModelImport.h"
#include "Game/SandboxContent.h"
#include "Game/Ui/SandboxHud.h"
#include "Game/Ui/UiShowcase.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

// Layout constants and helpers shared by Sandbox.cpp and SandboxContent.cpp.
namespace Game::SandboxDetail
{

	constexpr float Pi = 3.14159265358979f;
	constexpr Engine::EngineState AllStates = Engine::EngineState::Playing | Engine::EngineState::Paused | Engine::EngineState::Stopped;

	const glm::vec3 PhysicsCenter{ 0.0f, 0.0f, 10.0f };
	const glm::vec3 FountainCenter{ 0.0f, 0.0f, -12.0f };
	const glm::vec3 HallCenter{ 12.0f, 0.0f, -6.0f };
	// Sponza sits behind the playgrounds (its long axis along X), the swarm of coloured
	// lights inside its atrium.
	// Side of the square checkered ground plane and of its collider, in metres.
	constexpr float GroundSize = 130.0f;
	const glm::vec3 SponzaCenter{ 0.0f, 0.02f, -52.0f };
	constexpr float SponzaLength = 30.0f;
	constexpr std::uint32_t SwarmLightCount = 256;

	// Camera bookmarks: eye and target per playground.
	struct Bookmark
	{
		const char* Name;
		glm::vec3 Eye;
		glm::vec3 Target;
	};

	const std::array<Bookmark, 11> Bookmarks{ {
		{ "Overview", { 2.0f, 7.5f, 26.0f }, { -1.0f, 1.5f, 0.0f } },
		{ "PBR gallery", { -13.1f, 3.4f, 1.5f }, { -13.1f, 2.7f, -9.0f } },
		{ "Instance hall", { 12.0f, 9.0f, 9.0f }, { 12.0f, 0.5f, -6.0f } },
		{ "Physics", { 7.0f, 5.0f, 19.0f }, { 0.0f, 1.2f, 9.0f } },
		{ "Fountain", { 0.0f, 3.5f, -3.0f }, { 0.0f, 1.8f, -12.0f } },
		{ "Sponza atrium", { -9.5f, 1.8f, -52.2f }, { 8.0f, 5.5f, -52.2f } },
		{ "Sponza from above", { -4.0f, 19.0f, -52.2f }, { 3.0f, 0.5f, -52.2f } },
		// The reflection lab (Sandbox::GetReflectionLabCenter) and the chrome playground balls.
		{ "Reflection lab", { -22.0f, 1.7f, 21.5f }, { -22.0f, 0.6f, 16.0f } },
		{ "Chrome spheres", { 5.2f, 1.1f, 12.6f }, { 3.9f, 0.5f, 9.2f } },
		// Sandbox::GetBlackHoleHome, from the side its disk is tilted toward.
		{ "Black hole", { 24.0f, 6.2f, 31.0f }, { 24.0f, 5.0f, 18.0f } },
		// The GPU-driven box field (Game::BoxField).
		{ "Box field", { 31.0f, 7.0f, 8.0f }, { 46.0f, 0.5f, -9.0f } },
	} };

	inline glm::quat LookRotation(const glm::vec3& direction)
	{
		const glm::vec3 d = glm::normalize(direction);
		const glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
		return glm::quatLookAt(d, up);
	}

	inline Swim::Render::ParticleCurve Curve(float from, float to)
	{
		Swim::Render::ParticleCurve curve;
		curve.KeyCount = 2;
		curve.Times = { 0.0f, 1.0f, 1.0f, 1.0f };
		curve.Values = { from, to, to, to };
		return curve;
	}

	inline Swim::Render::ParticleGradient Gradient(std::array<float, 4> from, std::array<float, 4> to)
	{
		Swim::Render::ParticleGradient gradient;
		gradient.KeyCount = 2;
		gradient.Times = { 0.0f, 1.0f, 1.0f, 1.0f };
		gradient.Colors = { from, to, to, to };
		return gradient;
	}

} // namespace Game::SandboxDetail
