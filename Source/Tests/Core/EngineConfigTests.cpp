#include "Engine/EngineConfig.h"

#include <cassert>
#include <string>
#include <vector>

namespace
{

	Engine::EngineConfigParseResult Parse(std::vector<std::string> args)
	{
		std::vector<char*> argv;
		argv.reserve(args.size());
		for (std::string& arg : args)
		{
			argv.push_back(arg.data());
		}
		return Engine::ParseEngineConfigArgs(static_cast<int>(argv.size()), argv.data());
	}

}

int main()
{
	{
		auto result = Parse({ "SwimEngine" });
		assert(result.IsValid());
		assert(result.Config.Graphics == Engine::GraphicsBackend::Vulkan);
		assert(result.Config.Physics == Engine::PhysicsBackend::PhysX);
		assert(result.Config.InitialState == Engine::EngineState::Editing);
	}

	{
		auto result = Parse({ "SwimEngine", "--graphics=opengl", "--physics", "physx", "--state=playing" });
		assert(result.IsValid());
		assert(result.Config.Graphics == Engine::GraphicsBackend::OpenGLLegacy);
		assert(result.Config.Physics == Engine::PhysicsBackend::PhysX);
		assert(result.Config.InitialState == Engine::EngineState::Playing);
	}

	{
		auto result = Parse({ "SwimEngine", "--graphics", "auto", "--physics=jolt", "--opengl-shadertoy" });
		assert(result.IsValid());
		assert(Engine::ResolveGraphicsBackend(result.Config.Graphics) == Engine::GraphicsBackend::Vulkan);
		assert(Engine::ResolvePhysicsBackend(result.Config.Physics) == Engine::PhysicsBackend::Jolt);
		assert(result.Config.UseOpenGLShaderToy);
	}

	{
		auto result = Parse({ "SwimEngine", "--graphics=software", "--physics=bullet" });
		assert(!result.IsValid());
		assert(result.Errors.size() == 2);
	}

	{
		auto result = Parse({ "SwimEngine", "--state=none" });
		assert(result.IsValid());
		assert(result.Config.InitialState == Engine::EngineState::None);
	}

	{
		auto result = Parse({ "SwimEngine", "--state=playing,invalid" });
		assert(!result.IsValid());
		assert(result.Errors.size() == 1);
	}

	return 0;
}
