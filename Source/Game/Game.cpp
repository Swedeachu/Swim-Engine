#include "Game/Game.h"

#include "Engine/Systems/Scene/SceneSystem.h"
#include "Game/Behaviors/BallShooter.h"
#include "Game/Behaviors/Motion.h"
#include "Game/Scenes/ProfileTerrain.h"
#include "Game/Scenes/Sandbox.h"

namespace Game
{

	void Register(Engine::SceneSystem& scenes)
	{
		// Behaviours constructible by name (commands, data-driven spawning).
		scenes.RegisterBehaviorType<Spin>("Spin");
		scenes.RegisterBehaviorType<BallShooter>("BallShooter");
		scenes.RegisterBehaviorType<Projectile>("Projectile");

		scenes.RegisterSceneType<Sandbox>("Sandbox");
		scenes.RegisterSceneType<ProfileTerrain>("ProfileTerrain");

		if (scenes.GetStartupScene().empty())
		{
			scenes.SetStartupScene("Sandbox");
		}
	}

} // namespace Game
