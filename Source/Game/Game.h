#pragma once

namespace Engine
{
	class SceneSystem;
}

namespace Game
{
	// Registers the sandbox's scene and behaviour types and makes the sandbox the
	// startup scene (unless --scene chose another).
	void Register(Engine::SceneSystem& scenes);
} // namespace Game
