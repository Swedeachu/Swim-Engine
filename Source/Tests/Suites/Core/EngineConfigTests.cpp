#include "Engine/EngineConfig.h"
#include "Tests/Framework/Test.h"

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

SWIM_TEST("Core.EngineConfig", "DefaultsWithoutArguments")
{
	const auto result = Parse({ "SwimEngine" });
	SWIM_REQUIRE(result.IsValid());
	SWIM_CHECK(result.Config.Graphics == Engine::GraphicsBackend::Vulkan);
	SWIM_CHECK(result.Config.Physics == Engine::PhysicsBackend::PhysX);
	SWIM_CHECK(result.Config.InitialState == Engine::EngineState::Editing);
}

SWIM_TEST("Core.EngineConfig", "ExplicitBackendSelection")
{
	const auto result = Parse({ "SwimEngine", "--graphics=opengl", "--physics", "physx", "--state=playing" });
	SWIM_REQUIRE(result.IsValid());
	SWIM_CHECK(result.Config.Graphics == Engine::GraphicsBackend::OpenGLLegacy);
	SWIM_CHECK(result.Config.Physics == Engine::PhysicsBackend::PhysX);
	SWIM_CHECK(result.Config.InitialState == Engine::EngineState::Playing);
}

SWIM_TEST("Core.EngineConfig", "AutoBackendsResolveToConcreteChoices")
{
	const auto result = Parse({ "SwimEngine", "--graphics", "auto", "--physics=jolt", "--opengl-shadertoy" });
	SWIM_REQUIRE(result.IsValid());
	SWIM_CHECK(Engine::ResolveGraphicsBackend(result.Config.Graphics) == Engine::GraphicsBackend::Vulkan);
	SWIM_CHECK(Engine::ResolvePhysicsBackend(result.Config.Physics) == Engine::PhysicsBackend::Jolt);
	SWIM_CHECK(result.Config.UseOpenGLShaderToy);
}

SWIM_TEST("Core.EngineConfig", "UnsupportedBackendsReportEveryError")
{
	const auto result = Parse({ "SwimEngine", "--graphics=software", "--physics=bullet" });
	SWIM_CHECK(!result.IsValid());
	SWIM_CHECK_EQUAL(result.Errors.size(), std::size_t{ 2 });
}

SWIM_TEST("Core.EngineConfig", "InitialStateParsing")
{
	const auto none = Parse({ "SwimEngine", "--state=none" });
	SWIM_REQUIRE(none.IsValid());
	SWIM_CHECK(none.Config.InitialState == Engine::EngineState::None);

	const auto invalid = Parse({ "SwimEngine", "--state=playing,invalid" });
	SWIM_CHECK(!invalid.IsValid());
	SWIM_CHECK_EQUAL(invalid.Errors.size(), std::size_t{ 1 });
}
