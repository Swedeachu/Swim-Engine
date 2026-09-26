#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Tests/Fixtures/GpuSceneFixture.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <cstring>
#include <stdexcept>

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
