#include "Engine/Components/Pickable.h"
#include "Engine/Components/Transform.h"
#include "Game/Behaviors/BlackHole.h"
#include "Game/SandboxContent.h"
#include "Game/Scenes/Sandbox.h"

#include <glm/gtc/quaternion.hpp>

namespace Game
{

	namespace
	{

		constexpr Engine::EngineState AllStates = Engine::EngineState::Playing | Engine::EngineState::Paused | Engine::EngineState::Stopped;

	} // namespace

	glm::vec3 Sandbox::GetBlackHoleHome()
	{
		return { 24.0f, 5.0f, 18.0f };
	}

	void Sandbox::BuildBlackHole()
	{
		// A black hole to drag around with the left mouse button: an entity (scaled to the
		// horizon's diameter, 2 R_s, and tilted: its local +Y is the gas disk's axis) with
		// the BlackHole behaviour feeding the GravitationalLensing render feature, a Pickable
		// the size of its shadow and a MouseDrag. Nothing is rasterized: the shadow, the
		// lensed scene and the volumetric rainbow gas (a turbulent accretion torus and three
		// electron-shell rings) are traced per pixel by the feature, and bloom through the
		// post chain.
		constexpr float Rs = 0.6f;
		const glm::vec3 home = GetBlackHoleHome();
		const glm::quat tilt = glm::angleAxis(glm::radians(16.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
							   glm::angleAxis(glm::radians(-10.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		blackHole = CreateEntity("Black hole");
		AddComponent<Engine::Transform>(blackHole, Engine::Transform(home, glm::vec3(2.0f * Rs), tilt));
		AddTag(blackHole, GameTags::BlackHole);
		auto* hole = EmplaceBehavior<BlackHole>(blackHole, lensing, Rs);
		Engine::Pickable pickable;
		pickable.Radius = hole->GetShadowRadius() / (2.0f * Rs); // Local space: scaled by 2 R_s.
		AddComponent<Engine::Pickable>(blackHole, pickable);
		auto* drag = EmplaceBehavior<MouseDrag>(blackHole);
		drag->SetInputGate(
			[this]
			{
				return IsUiCapturing();
			});
		// Lensing, the gas and dragging run in every state.
		SetEnabledStates(blackHole, AllStates);
	}

} // namespace Game
