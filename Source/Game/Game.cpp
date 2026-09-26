#include "Game/Game.h"

#include "Engine/Systems/Scene/SceneSystem.h"
#include "Game/Behaviors/BallShooter.h"
#include "Game/Behaviors/Motion.h"
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
		if (scenes.GetStartupScene().empty())
		{
			scenes.SetStartupScene("Sandbox");
		}
	}
} // namespace Game
