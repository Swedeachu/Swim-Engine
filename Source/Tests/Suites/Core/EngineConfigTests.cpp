#include "Engine/EngineConfig.h"
#include "Tests/Framework/Test.h"

#include <cstdint>
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
	SWIM_CHECK(result.Config.Graphics == Engine::GraphicsBackend::Auto);
	SWIM_CHECK(result.Config.Physics == Engine::PhysicsBackend::Auto);
	SWIM_CHECK(result.Config.InitialState == Engine::EngineState::Playing);
	SWIM_CHECK(result.Config.Present == Engine::PresentMode::Window);
	SWIM_CHECK(result.Config.VSync);
	SWIM_CHECK_EQUAL(result.Config.MaxFrames, std::uint64_t{ 0 });
	SWIM_CHECK(result.Config.CapturePath.empty());
	SWIM_CHECK(result.Config.StartupScene.empty());
	SWIM_CHECK(!result.Config.ShowHelp);
}

SWIM_TEST("Core.EngineConfig", "ExplicitBackendSelection")
{
	const auto result = Parse({ "SwimEngine", "--graphics=vulkan", "--physics", "physx", "--state=paused" });
	SWIM_REQUIRE(result.IsValid());
	SWIM_CHECK(result.Config.Graphics == Engine::GraphicsBackend::Vulkan);
	SWIM_CHECK(result.Config.Physics == Engine::PhysicsBackend::PhysX);
	SWIM_CHECK(result.Config.InitialState == Engine::EngineState::Paused);
}

SWIM_TEST("Core.EngineConfig", "OpenGLIsNoLongerABackend")
{
	const auto result = Parse({ "SwimEngine", "--graphics=opengl" });
	SWIM_CHECK(!result.IsValid());
	const auto shaderToy = Parse({ "SwimEngine", "--opengl-shadertoy" });
	SWIM_CHECK(!shaderToy.IsValid());
}

SWIM_TEST("Core.EngineConfig", "AutoBackendsResolveToConcreteChoices")
{
	const auto result = Parse({ "SwimEngine", "--graphics", "auto", "--physics=auto" });
	SWIM_REQUIRE(result.IsValid());
	SWIM_CHECK(Engine::ResolveGraphicsBackend(result.Config.Graphics) == Engine::GraphicsBackend::Vulkan);
	SWIM_CHECK(Engine::ResolvePhysicsBackend(result.Config.Physics, true, true) == Engine::PhysicsBackend::PhysX);
	SWIM_CHECK(Engine::ResolvePhysicsBackend(result.Config.Physics, false, true) == Engine::PhysicsBackend::Jolt);
	SWIM_CHECK(Engine::ResolvePhysicsBackend(Engine::PhysicsBackend::PhysX, false, true) == Engine::PhysicsBackend::Auto);
	SWIM_CHECK(Engine::ResolvePhysicsBackend(Engine::PhysicsBackend::Jolt, true, true) == Engine::PhysicsBackend::Jolt);
	SWIM_CHECK(Engine::ResolvePhysicsBackend(Engine::PhysicsBackend::Auto, false, false) == Engine::PhysicsBackend::Auto);
}

SWIM_TEST("Core.EngineConfig", "UnsupportedBackendsReportEveryError")
{
	const auto result = Parse({ "SwimEngine", "--graphics=software", "--physics=bullet" });
	SWIM_CHECK(!result.IsValid());
	SWIM_CHECK_EQUAL(result.Errors.size(), std::size_t{ 2 });
}

SWIM_TEST("Core.EngineConfig", "InitialStateMustBeOneState")
{
	SWIM_CHECK(Parse({ "SwimEngine", "--state=stopped" }).Config.InitialState == Engine::EngineState::Stopped);
	SWIM_CHECK(Parse({ "SwimEngine", "--state", "PLAYING" }).Config.InitialState == Engine::EngineState::Playing);
	SWIM_CHECK(!Parse({ "SwimEngine", "--state=none" }).IsValid());
	SWIM_CHECK(!Parse({ "SwimEngine", "--state=editing" }).IsValid());
	SWIM_CHECK(!Parse({ "SwimEngine", "--state=all" }).IsValid());
	SWIM_CHECK(!Parse({ "SwimEngine", "--state" }).IsValid());
}

SWIM_TEST("Core.EngineConfig", "WindowAndPresentationOptions")
{
	const auto result = Parse({ "SwimEngine", "--size=1600x900", "--no-vsync", "--headless", "--scene", "Sandbox", "--validation=off" });
	SWIM_REQUIRE(result.IsValid());
	SWIM_CHECK_EQUAL(result.Config.Window.Width, 1600u);
	SWIM_CHECK_EQUAL(result.Config.Window.Height, 900u);
	SWIM_CHECK(!result.Config.VSync);
	SWIM_CHECK(!result.Config.Validation);
	SWIM_CHECK(result.Config.Present == Engine::PresentMode::Headless);
	SWIM_CHECK(result.Config.StartupScene == "Sandbox");

	const auto sized = Parse({ "SwimEngine", "--width", "800", "--height=600", "--vsync=on" });
	SWIM_REQUIRE(sized.IsValid());
	SWIM_CHECK_EQUAL(sized.Config.Window.Width, 800u);
	SWIM_CHECK_EQUAL(sized.Config.Window.Height, 600u);
	SWIM_CHECK(sized.Config.VSync);

	SWIM_CHECK(!Parse({ "SwimEngine", "--size=800" }).IsValid());
	SWIM_CHECK(!Parse({ "SwimEngine", "--width=4" }).IsValid());
	SWIM_CHECK(!Parse({ "SwimEngine", "--vsync=maybe" }).IsValid());
}

SWIM_TEST("Core.EngineConfig", "AutomationOptions")
{
	const auto result = Parse({ "SwimEngine", "--frames=12", "--capture=out.ppm", "--fixed-delta=0.016", "--fixed-rate=120", "--time-scale=0.5" });
	SWIM_REQUIRE(result.IsValid());
	SWIM_CHECK_EQUAL(result.Config.MaxFrames, std::uint64_t{ 12 });
	SWIM_CHECK(result.Config.CapturePath == "out.ppm");
	SWIM_CHECK(result.Config.FixedFrameDelta > 0.0159 && result.Config.FixedFrameDelta < 0.0161);
	SWIM_CHECK(result.Config.FixedRate == 120.0);
	SWIM_CHECK(result.Config.TimeScale == 0.5);

	// A capture without a frame budget gets a default one.
	const auto capture = Parse({ "SwimEngine", "--capture", "frame.ppm" });
	SWIM_REQUIRE(capture.IsValid());
	SWIM_CHECK(capture.Config.MaxFrames > 0);

	SWIM_CHECK(!Parse({ "SwimEngine", "--frames=0" }).IsValid());
	SWIM_CHECK(!Parse({ "SwimEngine", "--fixed-rate=0" }).IsValid());
	SWIM_CHECK(!Parse({ "SwimEngine", "--time-scale=-1" }).IsValid());
	SWIM_CHECK(!Parse({ "SwimEngine", "--fixed-delta=abc" }).IsValid());
}

SWIM_TEST("Core.EngineConfig", "UnknownArgumentsAndHelp")
{
	const auto unknown = Parse({ "SwimEngine", "--editor" });
	SWIM_CHECK(!unknown.IsValid());
	SWIM_CHECK_EQUAL(unknown.Errors.size(), std::size_t{ 1 });

	const auto help = Parse({ "SwimEngine", "--help" });
	SWIM_REQUIRE(help.IsValid());
	SWIM_CHECK(help.Config.ShowHelp);
	SWIM_CHECK(Engine::GetEngineConfigUsage().find("--headless") != std::string::npos);
}
