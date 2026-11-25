#include "Engine/Systems/Renderer/Features/VolumetricClouds.h"
#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Tests/Fixtures/GpuSceneFixture.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <chrono>
#include <fstream>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{

	namespace R = Swim::Render;
	namespace S = Swim::Rhi;

	// A compute program as ShaderLibrary would load it: named, reflected bindings on the
	// mock device (Color: sampled, Depth: sampled, Output: an RGBA16F storage image).
	struct FeatureWorld
	{
		FeatureWorld()
		{
			fixture.device.CreateTextures = true;
			auto layout = std::make_unique<Swim::Testing::MockPipelineLayout>();
			S::DescriptorSchemaDesc space{ 0, {} };
			space.Bindings.push_back({ 0, S::DescriptorType::SampledTexture, 1, S::ShaderStageMask::Compute });
			space.Bindings.push_back({ 1, S::DescriptorType::SampledTexture, 1, S::ShaderStageMask::Compute });
			S::DescriptorBindingDesc output{ 2, S::DescriptorType::StorageTexture, 1, S::ShaderStageMask::Compute };
			output.StorageTextureFormat = S::Format::RGBA16Float;
			space.Bindings.push_back(output);
			layout->program.Interface.DescriptorSchemas = { space };
			layout->program.Interface.PushConstants = { { 0, 16, S::ShaderStageMask::Compute } };
			program.Layout = std::move(layout);
			program.Pipeline = std::make_unique<Swim::Testing::MockComputePipeline>();
			program.Bindings = { { "Color", 0, 0, S::DescriptorType::SampledTexture, S::Format::Undefined },
				{ "Depth", 0, 1, S::DescriptorType::SampledTexture, S::Format::Undefined },
				{ "Output", 0, 2, S::DescriptorType::StorageTexture, S::Format::RGBA16Float } };
			program.ThreadGroupSize = { 8, 8, 1 };
			view.Width = 100;
			view.Height = 50;
		}

		Engine::RenderFeatureContext Context(R::RenderGraph& graph, R::GraphTexture color, R::GraphTexture depth)
		{
			Engine::RenderFeatureContext::Services services;
			services.LoadCompute = [this](std::string_view name) -> const Engine::RuntimeComputeProgram&
			{
				loaded.push_back(std::string(name));
				return program;
			};
			services.GetSampler = [](std::string_view) -> S::Sampler&
			{
				throw std::logic_error("no samplers in this test");
			};
			return Engine::RenderFeatureContext(
				graph, Engine::RenderFeatureStage::BeforePostProcess, view, settings, color, depth, services);
		}

		R::GraphTexture Texture(R::RenderGraph& graph, S::Format format, S::TextureUsage usage)
		{
			S::TextureDesc desc;
			desc.Extent = { view.Width, view.Height, 1 };
			desc.PixelFormat = format;
			desc.Usage = usage;
			return graph.CreateTexture(desc);
		}

		Swim::Testing::GpuSceneFixture fixture;
		Engine::RuntimeComputeProgram program;
		Engine::RenderFeatureView view;
		Engine::RenderSettings settings;
		std::vector<std::string> loaded;
	};

} // namespace

SWIM_TEST("Engine.RenderFeature", "ComputePassesBindByReflectedNameAndCoverTheGrid")
{
	FeatureWorld world;
	R::RenderGraph graph;
	const auto color = world.Texture(graph, S::Format::RGBA16Float, S::TextureUsage::Sampled | S::TextureUsage::ColorAttachment);
	const auto depth = world.Texture(graph, S::Format::D32Float, S::TextureUsage::Sampled | S::TextureUsage::DepthStencilAttachment);
	// Something earlier in the frame renders the scene (the graph rejects reads before writes).
	graph.AddPass(
		"Scene", S::QueueType::Graphics,
		[&](R::RenderGraphBuilder& b)
		{
			b.Write(color, S::ResourceState::ColorAttachment);
			b.Write(depth, S::ResourceState::DepthStencilWrite);
		},
		[](R::RenderCommandContext&)
		{
		});
	auto context = world.Context(graph, color, depth);

	// Missing, unknown and mismatched bindings are rejected before anything is recorded.
	const auto output = context.CreateColorTarget("Test output");
	SWIM_CHECK_THROWS(
		context.Compute("Feature").Texture("Color", color).Storage("Output", output).Dispatch(100, 50), std::invalid_argument);
	SWIM_CHECK_THROWS(context.Compute("Feature")
						  .Texture("Color", color)
						  .Texture("Depth", depth)
						  .Storage("Output", output)
						  .Texture("Missing", color)
						  .Dispatch(100, 50),
		std::invalid_argument);
	SWIM_CHECK_THROWS(
		context.Compute("Feature").Storage("Color", output).Texture("Depth", depth).Storage("Output", output).Dispatch(100, 50),
		std::invalid_argument);
	const auto wrongFormat = context.CreateTexture(S::Format::RGBA8Unorm, 0, 0, "RGBA8");
	SWIM_CHECK_THROWS(
		context.Compute("Feature").Texture("Color", color).Texture("Depth", depth).Storage("Output", wrongFormat).Dispatch(100, 50),
		std::invalid_argument);

	// A complete pass: 100 x 50 threads in 8 x 8 groups, the push constants as given.
	const std::array<std::uint32_t, 4> constants{ 7, 8, 9, 10 };
	context.Compute("Feature")
		.Texture("Color", color)
		.Texture("Depth", depth)
		.Storage("Output", output)
		.Constants(constants)
		.Dispatch(100, 50);
	context.SetColor(output);
	SWIM_CHECK(context.Color() == output);
	SWIM_CHECK_THROWS(context.SetColor(wrongFormat), std::invalid_argument);
	graph.Export(output, S::ResourceState::ShaderRead);
	world.fixture.device.Commands->clear();
	world.fixture.executor->Execute(graph.Compile());
	world.fixture.executor->Wait();

	std::vector<Swim::Testing::MockCommand> dispatches;
	std::vector<Swim::Testing::MockCommand> pushes;

	for (const auto& command : *world.fixture.device.Commands)
	{
		if (command.Kind == "Dispatch")
		{
			dispatches.push_back(command);
		}

		if (command.Kind == "PushConstants")
		{
			pushes.push_back(command);
		}
	}

	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(dispatches[0].SourceOffset, 13u); // ceil(100 / 8).
	SWIM_REQUIRE_EQUAL(pushes.size(), std::size_t(1));
	std::array<std::uint32_t, 4> pushed{};
	std::memcpy(pushed.data(), pushes[0].Data.data(), sizeof(pushed));
	SWIM_CHECK(pushed == constants);
	SWIM_CHECK_EQUAL(world.loaded.size(), std::size_t(5));
}

SWIM_TEST("Engine.RenderFeature", "DirectionsProjectToTheScreenLikePointsAtInfinity")
{
	Engine::RenderFeatureView view;
	// Identity view and a symmetric perspective (row-major, clip.w = -z).
	view.ViewProjection = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.1f, 0, 0, -1, 0 };
	const auto ahead = view.ProjectDirection({ 0, 0, -1 });
	SWIM_CHECK(std::abs(ahead[0] - 0.5f) < 1e-6f && std::abs(ahead[1] - 0.5f) < 1e-6f && ahead[2] > 0.0f);
	const auto upRight = view.ProjectDirection({ 0.5f, 0.5f, -1 });
	SWIM_CHECK(std::abs(upRight[0] - 0.75f) < 1e-6f && std::abs(upRight[1] - 0.25f) < 1e-6f); // Top-left uv origin.
	SWIM_CHECK(view.ProjectDirection({ 0, 0, 1 })[2] < 0.0f);								  // Behind the camera.
}

SWIM_TEST("Engine.RenderFeature", "CloudsMarchIntoAnEnvironmentAtlasWhenTheyContribute")
{
	// VolumetricCloudsBake's and VolumetricCloudsEnvironment's interfaces (VolumetricClouds.slang).
	Swim::Testing::GpuSceneFixture fixture;
	fixture.device.CreateTextures = true;
	std::map<std::string, Engine::RuntimeComputeProgram> programs;
	const auto f16 = S::Format::RGBA16Float;
	const auto none = S::Format::Undefined;
	const auto add = [&](const std::string& name, const std::vector<std::tuple<const char*, S::DescriptorType, S::Format>>& slots)
	{
		auto& program = programs[name];
		auto layout = std::make_unique<Swim::Testing::MockPipelineLayout>();
		S::DescriptorSchemaDesc space{ 0, {} };

		for (std::uint32_t i = 0; i < slots.size(); ++i)
		{
			const auto& [slotName, type, format] = slots[i];
			S::DescriptorBindingDesc binding{ i, type, 1, S::ShaderStageMask::Compute };
			binding.StorageTextureFormat = format;
			space.Bindings.push_back(binding);
			program.Bindings.push_back({ slotName, 0, i, type, format });
		}

		layout->program.Interface.DescriptorSchemas = { space };
		program.Layout = std::move(layout);
		program.Pipeline = std::make_unique<Swim::Testing::MockComputePipeline>();
		program.ThreadGroupSize = { 8, 8, 1 };
	};
	add("VolumetricCloudsBake", { { "ShapeAtlas", S::DescriptorType::StorageTexture, f16 }, { "WeatherMap", S::DescriptorType::StorageTexture, f16 } });
	add("VolumetricCloudsEnvironment", { { "Params", S::DescriptorType::ReadOnlyStorageBuffer, none },
										   { "Clouds", S::DescriptorType::StorageTexture, f16 }, { "ShapeAtlas", S::DescriptorType::SampledTexture, none },
										   { "WeatherMap", S::DescriptorType::SampledTexture, none }, { "LinearClamp", S::DescriptorType::Sampler, none },
										   { "LinearRepeat", S::DescriptorType::Sampler, none } });
	const auto sampler = fixture.device.CreateSampler(S::SamplerDesc{});
	std::vector<std::string> loaded;
	Engine::RenderFeatureView view;
	Engine::RenderSettings settings;
	Engine::RenderFeatureContext::Services services;
	services.LoadCompute = [&](std::string_view name) -> const Engine::RuntimeComputeProgram&
	{
		loaded.push_back(std::string(name));
		return programs.at(std::string(name));
	};
	services.GetSampler = [&](std::string_view) -> S::Sampler&
	{
		return *sampler;
	};
	services.Device = &fixture.device;

	Engine::VolumetricClouds clouds;
	SWIM_CHECK(clouds.ContributesToEnvironment());
	clouds.Settings.Environment = false;
	SWIM_CHECK(!clouds.ContributesToEnvironment());
	clouds.Settings.Environment = true;
	clouds.Settings.Coverage = 0.0f;
	SWIM_CHECK(!clouds.ContributesToEnvironment());
	clouds.Settings.Coverage = 0.5f;

	R::RenderGraph graph;
	// The environment context has no scene color or depth.
	Engine::RenderFeatureContext context(graph, Engine::RenderFeatureStage::BeforeTemporal, view, settings, {}, {}, services);
	SWIM_CHECK(!clouds.RecordEnvironment(context, 0).has_value());
	const auto result = clouds.RecordEnvironment(context, 32);
	SWIM_REQUIRE(result.has_value());
	SWIM_CHECK(loaded.front() == "VolumetricCloudsBake");
	SWIM_CHECK(loaded.back() == "VolumetricCloudsEnvironment");
	const auto desc = graph.GetDesc(*result);
	SWIM_CHECK(desc.Extent.Width == 32 && desc.Extent.Height == 32 * 6);
	SWIM_CHECK(desc.PixelFormat == S::Format::RGBA16Float);
	SWIM_CHECK((static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(S::TextureUsage::Sampled)) != 0);
	graph.Export(*result, S::ResourceState::ShaderRead);
	fixture.device.Commands->clear();
	fixture.executor->Execute(graph.Compile());
	fixture.executor->Wait();
	std::vector<Swim::Testing::MockCommand> dispatches;

	for (const auto& command : *fixture.device.Commands)
	{
		if (command.Kind == "Dispatch")
		{
			dispatches.push_back(command);
		}
	}

	// The noise is baked once (one thread per atlas texel and weather texel), then one
	// texel per thread over the 32 x 192 atlas.
	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(2));
	SWIM_CHECK_EQUAL(dispatches[0].SourceOffset, 2080u / 8u);
	SWIM_CHECK_EQUAL(dispatches[0].DestinationOffset, (1040u + 256u) / 8u);
	SWIM_CHECK_EQUAL(dispatches[1].SourceOffset, 4u);
	SWIM_CHECK_EQUAL(dispatches[1].DestinationOffset, 24u);

	// Later frames reuse the baked noise.
	R::RenderGraph next;
	view.Frame = 1;
	Engine::RenderFeatureContext nextContext(next, Engine::RenderFeatureStage::BeforeTemporal, view, settings, {}, {}, services);
	loaded.clear();
	const auto nextResult = clouds.RecordEnvironment(nextContext, 32);
	SWIM_REQUIRE(nextResult.has_value());
	SWIM_CHECK(loaded.back() == "VolumetricCloudsEnvironment");
	next.Export(*nextResult, S::ResourceState::ShaderRead);
	fixture.device.Commands->clear();
	fixture.executor->Execute(next.Compile());
	fixture.executor->Wait();
	std::size_t nextDispatches = 0;

	for (const auto& command : *fixture.device.Commands)
	{
		nextDispatches += command.Kind == "Dispatch" ? 1u : 0u;
	}

	SWIM_CHECK_EQUAL(nextDispatches, std::size_t(1));

	// A reloaded bake program bakes again.
	programs["VolumetricCloudsBake"].Pipeline = std::make_unique<Swim::Testing::MockComputePipeline>();
	R::RenderGraph reloaded;
	view.Frame = 2;
	Engine::RenderFeatureContext reloadedContext(reloaded, Engine::RenderFeatureStage::BeforeTemporal, view, settings, {}, {}, services);
	const auto reloadedResult = clouds.RecordEnvironment(reloadedContext, 32);
	SWIM_REQUIRE(reloadedResult.has_value());
	reloaded.Export(*reloadedResult, S::ResourceState::ShaderRead);
	fixture.device.Commands->clear();
	fixture.executor->Execute(reloaded.Compile());
	fixture.executor->Wait();
	std::size_t reloadedDispatches = 0;

	for (const auto& command : *fixture.device.Commands)
	{
		reloadedDispatches += command.Kind == "Dispatch" ? 1u : 0u;
	}

	SWIM_CHECK_EQUAL(reloadedDispatches, std::size_t(2));
}

SWIM_TEST("Engine.ShaderLibrary", "GeneratedCatalogAndCustomProgramsUseTheSameArtifactRegistration")
{
	Swim::Testing::MockDevice device;
	Engine::ShaderLibrary shaders(device, std::filesystem::path(SWIM_RUNTIME_SHADER_DIR));
	SWIM_CHECK(!Engine::ShaderLibrary::RequiredPrograms().empty());

	for (const auto name : Engine::ShaderLibrary::RequiredPrograms())
	{
		SWIM_CHECK(shaders.Contains(name));
	}

	shaders.Register({ "GameHistogram", "PostHistogram.spv", "PostHistogram.reflection.json" });
	SWIM_CHECK(shaders.Contains("GameHistogram"));
	SWIM_CHECK_THROWS(shaders.Register({ "GameHistogram", "PostHistogram.spv", "PostHistogram.reflection.json" }), std::invalid_argument);
	SWIM_CHECK_THROWS(shaders.Register({ "../BadName", "x.spv", "x.json" }), std::invalid_argument);
	SWIM_CHECK(!shaders.Contains("NeverRegistered"));
	SWIM_CHECK_THROWS(shaders.LoadCompute("NeverRegistered"), std::runtime_error);
}

SWIM_TEST("Engine.ShaderLibrary", "ParsedArtifactsStayCachedUntilExplicitInvalidation")
{
	struct Directory
	{
		std::filesystem::path Root;

		~Directory()
		{
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}
	};

	Directory temporary{ std::filesystem::temp_directory_path() /
		("SwimShaderCache_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) };
	SWIM_REQUIRE(std::filesystem::create_directory(temporary.Root));
	const std::filesystem::path originals(SWIM_RUNTIME_SHADER_DIR);
	std::filesystem::copy_file(originals / "PostHistogram.spv", temporary.Root / "test.spv");
	std::filesystem::copy_file(originals / "PostHistogram.reflection.json", temporary.Root / "test.json");
	Swim::Testing::MockDevice device;
	Engine::ShaderLibrary shaders(device, temporary.Root);
	shaders.Register({ "TemporaryHistogram", "test.spv", "test.json" });
	const auto failure = [&]
	{
		try
		{
			shaders.LoadCompute("TemporaryHistogram");
		}
		catch (const std::runtime_error& error)
		{
			return std::string(error.what());
		}

		return std::string();
	};

	// The mock rejects GPU creation, after real reflection parsing/conversion succeeds.
	SWIM_CHECK(failure().find("cannot create compute program") != std::string::npos);
	{
		std::ofstream broken(temporary.Root / "test.json");
		broken << "invalid json";
	}
	SWIM_CHECK(failure().find("cannot create compute program") != std::string::npos);
	shaders.Invalidate("TemporaryHistogram");
	SWIM_CHECK(failure().find("reflection:") != std::string::npos);
}
